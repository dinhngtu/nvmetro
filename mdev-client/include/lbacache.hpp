#include "lbacache-polyfill.hpp"

#define CLUSTER_SIZE 65536
#define LBA_SIZE 512
#define CLUSTER_LBAS (CLUSTER_SIZE / LBA_SIZE)

#define XCACHE_ASSOC 4
#define XCACHE_LINES 65536
#define XCACHE_SIZE (XCACHE_ASSOC * XCACHE_LINES)

enum AuxBits {
    AUXBITS_VALID = 0x1,
    AUXBITS_WRITABLE = 0x2,
};
enum AuxCommand {
    AUXCMD_KEEP = 0x40000000,
    AUXCMD_FORWARD = 0x20000000,
};
#define AUX_CMD_MASK 0x70000000
#define AUX_MASK 0x3
// reserve 1 bit in the aux shift
// this way we have 3 aux bits per assoc and up to 8-way assoc
#define AUX_SHIFT 3
#define AUX_MAX_ASSOC 8

#define META_MRU 0x1000000
#define META_MRU_SHIFT 24
#define META_MRU_MASK (((1 << XCACHE_ASSOC) - 1) << META_MRU_SHIFT)

#define AUX_IS_VALID(meta, i) ((meta) & (AUXBITS_VALID << ((i) * AUX_SHIFT)))
#define AUX_IS_WRITABLE(meta, i) ((meta) & (AUXBITS_WRITABLE << ((i) * AUX_SHIFT)))
#define AUX_GET(meta, i) (((meta) >> ((i) * AUX_SHIFT)) & AUX_MASK)

/*
 * address structure (example with 8192 lines):
 * (msb) tag--------------------------  idx----------  off----      (lsb)
 * ttttttttttttttttttttttttttttttttttt  iiiiiiiiiiiii  ooooooo  ---------
 * [=====================vblk=======================]  [ block boundary ]
 */
#define VLBA_RESOLVE(vlba)            \
    u64 vblk = (vlba) / CLUSTER_LBAS; \
    u64 off = (vlba) % CLUSTER_LBAS;  \
    u64 tag = vblk / XCACHE_LINES;    \
    u32 idx = vblk % XCACHE_LINES;

std::array<u64, XCACHE_SIZE> cache_tag;
std::array<u64, XCACHE_SIZE> cache_plba;
std::array<u32, XCACHE_LINES> cache_meta;

// https://en.wikipedia.org/wiki/Pseudo-LRU#Bit-PLRU
// https://github.com/karlmcguire/plru

// length0 is 0-based (NVMe-style)
static u64 lba_lookup(u64 vlba, u16 length0, u32 *aux) {
    VLBA_RESOLVE(vlba);
    int i;
    *aux = 0;
    if (vblk != (vlba + length0) / CLUSTER_LBAS)
        return U64_MAX;
    u32 *meta = bpf_map_lookup_elem(&cache_meta, &idx);
    if (!meta)
        return U64_MAX;
    for (i = 0; i < XCACHE_ASSOC; i++) {
        if (AUX_IS_VALID(*meta, i)) {
            u32 ci = idx * XCACHE_ASSOC + i;
            u64 *ctag = bpf_map_lookup_elem(&cache_tag, &ci);
            if (ctag && *ctag == tag) {
                *meta |= META_MRU << i;
                if ((*meta & META_MRU_MASK) == META_MRU_MASK)
                    *meta = (*meta & ~META_MRU_MASK) | (META_MRU << i);
                u64 *plba = bpf_map_lookup_elem(&cache_plba, &ci);
                if (!plba)
                    return U64_MAX;
                bpf_map_update_elem(&cache_meta, &idx, meta, BPF_ANY);
                *aux = AUX_GET(*meta, i);
                return *plba + off;
            }
        }
    }
    return U64_MAX;
}

static u64 lba_update(u64 vlba, u64 plba, u32 aux) {
    VLBA_RESOLVE(vlba);
    int i;
    u32 *meta = bpf_map_lookup_elem(&cache_meta, &idx);
    u32 ci;
    if (!meta)
        return U64_MAX;
    for (i = 0; i < XCACHE_ASSOC; i++) {
        if (AUX_IS_VALID(*meta, i)) {
            u64 *ctag;
            ci = idx * XCACHE_ASSOC + i;
            ctag = bpf_map_lookup_elem(&cache_tag, &ci);
            if (ctag && *ctag == tag)
                break;
        }
    }
    if (i == XCACHE_ASSOC)
        for (i = 0; i < XCACHE_ASSOC; i++)
            if (!AUX_IS_VALID(*meta, i))
                break;
    if (i == XCACHE_ASSOC)
        for (i = 0; i < XCACHE_ASSOC; i++)
            if (!(*meta & (META_MRU << i)))
                break;
    if (i == XCACHE_ASSOC)
        i = 0;
    *meta = (*meta & ~(AUX_MASK << ((i * AUX_SHIFT)))) | (aux << (i * AUX_SHIFT)) | (META_MRU << i);
    if ((*meta & META_MRU_MASK) == META_MRU_MASK)
        *meta = (*meta & ~META_MRU_MASK) | (META_MRU << i);
    ci = idx * XCACHE_ASSOC + i;
    bpf_map_update_elem(&cache_tag, &ci, &tag, BPF_ANY);
    bpf_map_update_elem(&cache_plba, &ci, &plba, BPF_ANY);
    bpf_map_update_elem(&cache_meta, &idx, meta, BPF_ANY);
    return plba + off;
}

static void lba_flush_range(u64 vlba, u16 length0) {
    VLBA_RESOLVE(vlba);
    if (vblk != (vlba + length0) / CLUSTER_LBAS)
        return;
    s32 rest = (s32)length0 + 1;
    while (rest > 0) {
        u32 *meta = bpf_map_lookup_elem(&cache_meta, &idx);
        int i;
        if (meta) {
            for (i = 0; i < XCACHE_ASSOC; i++) {
                u64 *ctag;
                u32 ci = idx * XCACHE_ASSOC + i;
                ctag = bpf_map_lookup_elem(&cache_tag, &ci);
                if (ctag && AUX_IS_VALID(*meta, i) && *ctag == tag)
                    *meta &= ~(AUXBITS_VALID << (i * AUX_SHIFT));
            }
            bpf_map_update_elem(&cache_meta, &idx, meta, BPF_ANY);
        }
        idx = (idx + 1) % XCACHE_LINES;
        rest -= CLUSTER_LBAS;
    }
}

static long lba_clear_flag_all(u32 aux) {
    u32 meta_mask = 0;
    int i;
    for (i = 0; i < AUX_MAX_ASSOC; i++)
        meta_mask |= aux << (i * AUX_SHIFT);
    meta_mask = ~meta_mask;
    return bpf_arraymap_elem_band(&cache_meta, &meta_mask);
}

static int nm_do_rw(struct bpf_io_ctx *ctx) {
    u64 val;
    u32 aux;
    u64 slba = ctx->cmd.rw.slba;
    u16 length0 = ctx->cmd.rw.length;

    val = lba_lookup(slba, length0, &aux);
    if (val != U64_MAX && ((ctx->cmd.common.opcode == nvme_cmd_read) || (aux & AUXBITS_WRITABLE))) {
        // direct translation succeeded
        ctx->cmd.rw.slba = val;
        return NMBPF_SEND_HQ | NMBPF_WILL_COMPLETE_HQ;
    } else {
        if (ctx->cmd.common.opcode != nvme_cmd_read)
            lba_flush_range(slba, length0);
        return NMBPF_SEND_FD | NMBPF_HOOK_NFD_WRITE | NMBPF_WAIT_FOR_HOOK;
    }
}

static int nm_on_rw_respond(struct bpf_io_ctx *ctx) {
    u64 val;

    // aux = [ctrl, paddr_lo, paddr_hi]
    // ctrl = cmd (AUX_CMD_MASK) | auxbits
    if (ctx->aux[0] & AUXBITS_VALID) {
        u64 plba;
        val = (u64)ctx->aux[1] | ((u64)ctx->aux[2] << 32);
        plba = lba_update(
            ctx->cmd.rw.slba,
            /* truncate the block offset */
            val / CLUSTER_SIZE * CLUSTER_LBAS,
            ctx->aux[0] & AUX_MASK);
        if (plba == U64_MAX) {
            // this really shouldn't happen but...
            return NVME_SC_DNR | NVME_SC_INTERNAL;
        } else if (ctx->aux[0] & AUXCMD_FORWARD) {
            ctx->cmd.rw.slba = plba;
            return NMBPF_SEND_HQ | NMBPF_WILL_COMPLETE_HQ;
        } else {
            return ctx->data;
        }
    } else {
        if (!(ctx->aux[0] & AUXCMD_KEEP))
            lba_flush_range(ctx->cmd.rw.slba, ctx->cmd.rw.length);
        return ctx->data;
    }
}

int nvme_run_bpf(struct bpf_io_ctx *ctx) {
    if (ctx->cmd.common.flags & NVME_CMD_SGL_ALL)
        return NVME_SC_DNR | NVME_SC_INVALID_OPCODE;

    switch (ctx->cmd.common.opcode) {
    case nvme_cmd_read:
    case nvme_cmd_write:
    case nvme_cmd_write_zeroes:
        switch (ctx->current_hook) {
        case NMBPF_HOOK_VSQ:
            return nm_do_rw(ctx);
        case NMBPF_HOOK_NFD_WRITE:
            return nm_on_rw_respond(ctx);
        default:
            return NMBPF_SEND_HQ | NMBPF_WILL_COMPLETE_HQ;
        }
    case 0x81: { // snapshot
        long ret = lba_clear_flag_all(AUXBITS_WRITABLE);
        if (ret < 0)
            return NVME_SC_DNR | NVME_SC_INTERNAL;
        if (ret != XCACHE_LINES)
            return NVME_SC_INTERNAL;
        return NMBPF_SEND_FD | NMBPF_WILL_COMPLETE_FD;
    }
    default:
        return NMBPF_SEND_HQ | NMBPF_WILL_COMPLETE_HQ;
    }
}

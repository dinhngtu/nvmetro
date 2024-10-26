#pragma once

#include <vector>
#include <utility>
#include <functional>
#include <variant>
#include <tuple>
#include <sys/uio.h>
#include <boost/unordered_map.hpp>

#include "nvme.hpp"
#include "util/uring.hpp"
#include "xcow/xcow_snap.hpp"
#include "xcow/xcow_file.hpp"
#include "xcow/proto.hpp"

static constexpr void set_aux(nmntfy_aux &aux, uint64_t paddr, uint32_t aux0) {
    aux[0] = aux0;
    aux[1] = static_cast<__u32>(paddr & 0xffff'fffful);
    aux[2] = static_cast<__u32>(paddr >> 32);
}

static constexpr void set_aux(nmntfy_aux &aux, xcow::XlateLeaf tl, uint32_t aux0) {
    assert(tl & xcow::XlateBits::valid);
    if (tl & xcow::XlateBits::valid)
        aux0 |= AUXBITS_VALID;
    if (tl & xcow::XlateBits::writable)
        aux0 |= AUXBITS_WRITABLE;
    set_aux(aux, xcow::XlateBits::decode_leaf_unsafe(tl), aux0);
}

struct nm_reply {
    uint32_t tag;
    uint16_t status;
    nmntfy_aux aux{};

    constexpr nm_reply(uint32_t _tag, uint16_t _status) : tag(_tag), status(_status) {
    }

    constexpr nm_reply(uint32_t _tag, uint16_t _status, nmntfy_aux _aux) : tag(_tag), status(_status), aux(_aux) {
    }

    constexpr nm_reply(uint32_t _tag, uint16_t _status, uint64_t paddr, uint32_t aux0) : nm_reply(_tag, _status) {
        set_aux(aux, paddr, aux0);
    }

    constexpr nm_reply(uint32_t _tag, uint16_t _status, xcow::XlateLeaf tl, uint32_t aux0) : nm_reply(_tag, _status) {
        set_aux(aux, tl, aux0);
    }
};
struct xcow_ticket;
using nm_outcome = std::variant<std::monostate, nm_reply, xcow_ticket *>;

struct xcow_ticket : public multi_ticket {
    constexpr xcow_ticket(uint32_t _tag) : multi_ticket(_tag) {
    }

    nmntfy_aux aux{};
    uint64_t locked_cluster = UINT64_MAX;
    cleanup last;
};

static constexpr uint16_t translate_uring_status(__s32 us) {
    if (us < 0)
        return NVME_SC_DNR | NVME_SC_INTERNAL;
    else
        return NVME_SC_SUCCESS;
}

class nvme_xcow final : public nvme {
public:
    // key: cluster index (vblk)
    using workqueue_type = boost::unordered_multimap<uint64_t, std::function<nm_outcome(__s32 uring_status)>>;

    explicit nvme_xcow(
        const std::shared_ptr<mapping> &vm,
        int nfd,
        int bfd,
        xcow::XcowFile *file,
        std::vector<bool> *clock,
        workqueue_type *wq);
    nvme_xcow(const nvme_xcow &) = delete;
    nvme_xcow &operator=(const nvme_xcow &) = delete;
    nvme_xcow(nvme_xcow &&) = default;
    nvme_xcow &operator=(nvme_xcow &&) = default;
    ~nvme_xcow() = default;

    inline int sq_kick() {
        return _ring.sq_kick();
    }

    // true: async, false: immediate return
    nm_outcome submit_async(size_t sq, const nvme_command &cmd, uint32_t tag);
    cq_window get_pending_completions(std::span<io_uring_cqe *> cqebuf);
    static inline sq_ticket *cqe_get_data(io_uring_cqe *cqe) {
        return static_cast<sq_ticket *>(io_uring_cqe_get_data(cqe));
    }
    inline std::span<sq_ticket *> commit_completions(cq_window &wnd, const std::span<sq_ticket *> &ticketbuf) {
        return _ring.cq_commit(wnd, ticketbuf);
    }
    inline void commit_completions(cq_window &wnd) {
        return _ring.cq_commit(wnd);
    }
    inline void commit_completion(io_uring_cqe *cqe) {
        return _ring.cq_commit(cqe);
    }

private:
    nm_outcome do_snapshot(size_t sq, const nvme_command &cmd, uint32_t tag);
    nm_outcome do_read(size_t sq, const nvme_command &cmd, uint32_t tag);
    nm_outcome do_write(size_t sq, const nvme_command &cmd, uint32_t tag);
    nm_outcome do_write_zeroes(size_t sq, const nvme_command &cmd, uint32_t tag);
    nm_outcome do_flush(size_t sq, const nvme_command &cmd, uint32_t tag);

    std::tuple<xcow_ticket *, uint64_t, uint64_t> do_alloc_one_tx(uint32_t tag, uint64_t vblk, xcow::XlateLeaf *entry);
    nm_outcome do_write_one(
        xcow_ticket *ticket,
        const std::vector<iovec> &iovecs,
        xcow::XlateLeaf *ref,
        uint64_t off,
        int flags);

    using cow_ticket_type = mem_ticket<xcow_ticket>;
    std::pair<cow_ticket_type *, io_uring_sqe *> do_cow(uint32_t tag, off_t inoff, off_t outoff, size_t nbytes);

    constexpr size_t cluster_size() {
        return _snap.file().cluster_size();
    }
    constexpr size_t cluster_bits() {
        return _snap.file().cluster_bits();
    }

    std::array<int, 1> _bfd;
    xcow::XcowFile *_file;
    xcow::XcowSnap _snap;
    uring _ring;
    std::vector<bool> *_clock;
    workqueue_type *_wq;
};

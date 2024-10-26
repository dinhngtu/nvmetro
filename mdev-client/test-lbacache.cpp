#include "lbacache.hpp"
#include <catch_amalgamated.hpp>
#include <fmt/format.h>

static void clear_cache() {
    std::fill(cache_tag.begin(), cache_tag.end(), 0);
    std::fill(cache_plba.begin(), cache_plba.end(), 0);
    std::fill(cache_meta.begin(), cache_meta.end(), 0);
}

static void print_cache() {
    fmt::print("meta:          mmmm             wv wv wv wv\n");
    for (size_t i = 0; i < cache_meta.size(); i++) {
        if (cache_meta[i]) {
            fmt::print("{:<10} {:032b} ", i, cache_meta[i]);
            for (int j = XCACHE_ASSOC - 1; j >= 0; j--) {
                auto ci = i * XCACHE_ASSOC + j;
                if (AUX_IS_VALID(cache_meta[i], j))
                    fmt::print("{}={} ", cache_tag[ci], cache_plba[ci]);
                else
                    fmt::print("??? ");
            }
            fmt::print("\n");
        }
    }
    fmt::print("\n");
}

TEST_CASE("lbacache tests") {
    SECTION("rr") {
        clear_cache();

        bpf_io_ctx ctx{};
        ctx.cmd.common.opcode = nvme_cmd_read;
        ctx.cmd.rw.slba = 555;
        ctx.cmd.rw.length = 3;
        ctx.current_hook = NMBPF_HOOK_VSQ;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_FD);

        ctx.aux[0] = AUXBITS_VALID | AUXCMD_FORWARD;
        ctx.aux[1] = 768 * CLUSTER_SIZE;
        ctx.aux[2] = 0;
        ctx.current_hook = NMBPF_HOOK_NFD_WRITE;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_HQ);
        REQUIRE(ctx.cmd.rw.slba == 768 * CLUSTER_LBAS + 555 % CLUSTER_LBAS);

        ctx.cmd.rw.slba = 559;
        ctx.current_hook = NMBPF_HOOK_VSQ;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_HQ);
        REQUIRE(ctx.cmd.rw.slba == 768 * CLUSTER_LBAS + 559 % CLUSTER_LBAS);
    }

    SECTION("rw") {
        clear_cache();

        bpf_io_ctx ctx{};
        ctx.cmd.common.opcode = nvme_cmd_read;
        ctx.cmd.rw.slba = 555;
        ctx.cmd.rw.length = 3;
        ctx.current_hook = NMBPF_HOOK_VSQ;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_FD);

        ctx.aux[0] = AUXBITS_VALID | AUXBITS_WRITABLE | AUXCMD_FORWARD;
        ctx.aux[1] = 1280 * CLUSTER_SIZE;
        ctx.aux[2] = 0;
        ctx.current_hook = NMBPF_HOOK_NFD_WRITE;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_HQ);
        REQUIRE(ctx.cmd.rw.slba == 1280 * CLUSTER_LBAS + 555 % CLUSTER_LBAS);

        ctx.cmd.common.opcode = nvme_cmd_write;
        ctx.cmd.rw.slba = 559;
        ctx.current_hook = NMBPF_HOOK_VSQ;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_HQ);
        REQUIRE(ctx.cmd.rw.slba == 1280 * CLUSTER_LBAS + 559 % CLUSTER_LBAS);
    }

    SECTION("wr") {
        clear_cache();

        bpf_io_ctx ctx{};
        ctx.cmd.common.opcode = nvme_cmd_write;
        ctx.cmd.rw.slba = 555;
        ctx.cmd.rw.length = 3;
        ctx.current_hook = NMBPF_HOOK_VSQ;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_FD);

        ctx.aux[0] = AUXBITS_VALID | AUXBITS_WRITABLE | AUXCMD_FORWARD;
        ctx.aux[1] = 1280 * CLUSTER_SIZE;
        ctx.aux[2] = 0;
        ctx.current_hook = NMBPF_HOOK_NFD_WRITE;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_HQ);
        REQUIRE(ctx.cmd.rw.slba == 1280 * CLUSTER_LBAS + 555 % CLUSTER_LBAS);

        ctx.cmd.common.opcode = nvme_cmd_read;
        ctx.cmd.rw.slba = 559;
        ctx.current_hook = NMBPF_HOOK_VSQ;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_HQ);
        REQUIRE(ctx.cmd.rw.slba == 1280 * CLUSTER_LBAS + 559 % CLUSTER_LBAS);
    }

    SECTION("ww") {
        clear_cache();

        bpf_io_ctx ctx{};
        ctx.cmd.common.opcode = nvme_cmd_write;
        ctx.cmd.rw.slba = 555;
        ctx.cmd.rw.length = 3;
        ctx.current_hook = NMBPF_HOOK_VSQ;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_FD);

        ctx.aux[0] = AUXBITS_VALID | AUXBITS_WRITABLE | AUXCMD_FORWARD;
        ctx.aux[1] = 1280 * CLUSTER_SIZE;
        ctx.aux[2] = 0;
        ctx.current_hook = NMBPF_HOOK_NFD_WRITE;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_HQ);
        REQUIRE(ctx.cmd.rw.slba == 1280 * CLUSTER_LBAS + 555 % CLUSTER_LBAS);

        ctx.cmd.common.opcode = nvme_cmd_write;
        ctx.cmd.rw.slba = 559;
        ctx.current_hook = NMBPF_HOOK_VSQ;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_HQ);
        REQUIRE(ctx.cmd.rw.slba == 1280 * CLUSTER_LBAS + 559 % CLUSTER_LBAS);
    }

    SECTION("cow") {
        clear_cache();

        bpf_io_ctx ctx{};
        ctx.cmd.common.opcode = nvme_cmd_write;
        ctx.cmd.rw.slba = 555;
        ctx.cmd.rw.length = 3;
        ctx.current_hook = NMBPF_HOOK_VSQ;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_FD);

        ctx.aux[0] = AUXBITS_VALID | AUXCMD_FORWARD;
        ctx.aux[1] = 768 * CLUSTER_SIZE;
        ctx.aux[2] = 0;
        ctx.current_hook = NMBPF_HOOK_NFD_WRITE;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_HQ);
        REQUIRE(ctx.cmd.rw.slba == 768 * CLUSTER_LBAS + 555 % CLUSTER_LBAS);

        ctx.cmd.common.opcode = nvme_cmd_write;
        ctx.cmd.rw.slba = 559;
        ctx.current_hook = NMBPF_HOOK_VSQ;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_FD);

        ctx.aux[0] = AUXBITS_VALID | AUXBITS_WRITABLE | AUXCMD_FORWARD;
        ctx.aux[1] = 1280 * CLUSTER_SIZE;
        ctx.aux[2] = 0;
        ctx.current_hook = NMBPF_HOOK_NFD_WRITE;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_HQ);
        REQUIRE(ctx.cmd.rw.slba == 1280 * CLUSTER_LBAS + 559 % CLUSTER_LBAS);
    }

    SECTION("r4") {
        clear_cache();

        bpf_io_ctx ctx{};
        ctx.cmd.common.opcode = nvme_cmd_read;
        ctx.cmd.rw.slba = 555;
        ctx.cmd.rw.length = 3;
        ctx.current_hook = NMBPF_HOOK_VSQ;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_FD);
        ctx.aux[0] = AUXBITS_VALID | AUXCMD_FORWARD;
        ctx.aux[1] = 768 * CLUSTER_SIZE;
        ctx.aux[2] = 0;
        ctx.current_hook = NMBPF_HOOK_NFD_WRITE;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_HQ);
        REQUIRE(ctx.cmd.rw.slba == 768 * CLUSTER_LBAS + 555 % CLUSTER_LBAS);

        print_cache();

        ctx.cmd.common.opcode = nvme_cmd_read;
        ctx.cmd.rw.slba = 8389163;
        ctx.cmd.rw.length = 3;
        ctx.current_hook = NMBPF_HOOK_VSQ;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_FD);
        ctx.aux[0] = AUXBITS_VALID | AUXCMD_FORWARD;
        ctx.aux[1] = 896 * CLUSTER_SIZE;
        ctx.aux[2] = 0;
        ctx.current_hook = NMBPF_HOOK_NFD_WRITE;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_HQ);
        REQUIRE(ctx.cmd.rw.slba == 896 * CLUSTER_LBAS + 8389163 % CLUSTER_LBAS);

        print_cache();

        ctx.cmd.common.opcode = nvme_cmd_read;
        ctx.cmd.rw.slba = 16777771;
        ctx.cmd.rw.length = 3;
        ctx.current_hook = NMBPF_HOOK_VSQ;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_FD);
        ctx.aux[0] = AUXBITS_VALID | AUXCMD_FORWARD;
        ctx.aux[1] = 1024 * CLUSTER_SIZE;
        ctx.aux[2] = 0;
        ctx.current_hook = NMBPF_HOOK_NFD_WRITE;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_HQ);
        REQUIRE(ctx.cmd.rw.slba == 1024 * CLUSTER_LBAS + 16777771 % CLUSTER_LBAS);

        print_cache();

        ctx.cmd.common.opcode = nvme_cmd_read;
        ctx.cmd.rw.slba = 25166379;
        ctx.cmd.rw.length = 3;
        ctx.current_hook = NMBPF_HOOK_VSQ;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_FD);
        ctx.aux[0] = AUXBITS_VALID | AUXCMD_FORWARD;
        ctx.aux[1] = 1280 * CLUSTER_SIZE;
        ctx.aux[2] = 0;
        ctx.current_hook = NMBPF_HOOK_NFD_WRITE;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_HQ);
        REQUIRE(ctx.cmd.rw.slba == 1280 * CLUSTER_LBAS + 25166379 % CLUSTER_LBAS);

        print_cache();

        ctx.cmd.common.opcode = nvme_cmd_read;
        ctx.cmd.rw.slba = 25166379;
        ctx.cmd.rw.length = 3;
        ctx.current_hook = NMBPF_HOOK_VSQ;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_HQ);
        REQUIRE(ctx.cmd.rw.slba == 1280 * CLUSTER_LBAS + 25166379 % CLUSTER_LBAS);

        print_cache();

        ctx.cmd.common.opcode = nvme_cmd_read;
        ctx.cmd.rw.slba = 16777771;
        ctx.cmd.rw.length = 3;
        ctx.current_hook = NMBPF_HOOK_VSQ;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_HQ);
        REQUIRE(ctx.cmd.rw.slba == 1024 * CLUSTER_LBAS + 16777771 % CLUSTER_LBAS);

        print_cache();

        ctx.cmd.common.opcode = nvme_cmd_read;
        ctx.cmd.rw.slba = 8389163;
        ctx.cmd.rw.length = 3;
        ctx.current_hook = NMBPF_HOOK_VSQ;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_HQ);
        REQUIRE(ctx.cmd.rw.slba == 896 * CLUSTER_LBAS + 8389163 % CLUSTER_LBAS);

        print_cache();

        ctx.cmd.common.opcode = nvme_cmd_read;
        ctx.cmd.rw.slba = 555;
        ctx.cmd.rw.length = 3;
        ctx.current_hook = NMBPF_HOOK_VSQ;
        REQUIRE(nvme_run_bpf(&ctx) & NMBPF_SEND_HQ);
        REQUIRE(ctx.cmd.rw.slba == 768 * CLUSTER_LBAS + 555 % CLUSTER_LBAS);

        print_cache();
    }
}

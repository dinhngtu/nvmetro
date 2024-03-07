#include <memory>
#include <vector>
#include <cassert>

#include <fcntl.h>
#include <unistd.h>
#include <sys/uio.h>
#include <liburing/io_uring.h>

#include "nvme_core.hpp"
#include "nvme_sender_aio.hpp"
#include "util.hpp"
#include "prp.hpp"
#include "vm.hpp"

void nvme_sender_aio::submit_write_async(size_t sq, const nvme_command &cmd, uint32_t tag) {
    (void)sq;
    auto slba = cmd.rw.slba;
    size_t nblocks = static_cast<size_t>(cmd.rw.length) + 1;
    int lbas = ns_lba_shift(cmd.rw.nsid);
    size_t nbytes = ns_cmd_check_nbytes(nblocks, lbas);
    prp_list cmd_prpl{reinterpret_cast<prp_list::const_pointer>(&cmd.rw.dptr.prp1), 2};
    prp_chain_iter prp_begin(vm(), cmd_prpl, 0, nbytes);

    auto ticket = new iovec_ticket();
    ticket->tag = tag;
    for (auto &prp_it = prp_begin; !prp_it.at_end(); prp_it++) {
        DBG_PRINTF("page %#lx size %zu\n", *prp_it, prp_it.this_nbytes());
        // lba index inside current page
        auto lbad = vm()->get_span(*prp_it, prp_it.this_nbytes());
        ticket->iovecs.push_back(iovec{.iov_base = lbad.data(), .iov_len = lbad.size()});
    }

    _ring.queue_writev(ticket, true, 0, slba << lbas);
}

void nvme_sender_aio::submit_write_zeroes_async(size_t sq, const nvme_command &cmd, uint32_t tag) {
    (void)sq;
    auto slba = cmd.rw.slba;
    size_t nblocks = static_cast<size_t>(cmd.rw.length) + 1;
    int lbas = ns_lba_shift(cmd.rw.nsid);

    auto ticket = new sq_ticket();
    ticket->tag = tag;
    _ring.queue_fallocate(ticket, true, 0, FALLOC_FL_ZERO_RANGE, slba << lbas, nblocks << lbas);
}

void nvme_sender_aio::submit_flush_async(size_t sq, const nvme_command &cmd, uint32_t tag) {
    (void)sq;
    (void)cmd;
    auto ticket = new sq_ticket();
    ticket->tag = tag;
    _ring.queue_fsync(ticket, true, 0, IORING_FSYNC_DATASYNC);
}

__u16 nvme_sender_aio::submit_async(size_t sq, const nvme_command &cmd, uint32_t tag) {
    (void)sq;
    // try {
    if (cmd.common.opcode == nvme_cmd_write) {
        DBG_PRINTF(
            "sq %zu write cid %hu slba %#llx length %hu+1\n",
            sq,
            cmd.common.command_id,
            cmd.rw.slba,
            cmd.rw.length);
        DBG_PRINTF("prp1=%#llx prp2=%#llx\n", cmd.rw.dptr.prp1, cmd.rw.dptr.prp2);
        if (!(cmd.common.flags & NVME_CMD_SGL_ALL)) {
            submit_write_async(sq, cmd, tag);
            return NVME_SC_SUCCESS;
        }
    } else if (cmd.common.opcode == nvme_cmd_write_zeroes) {
        DBG_PRINTF(
            "sq %zu write_zeroes cid %hu slba %#llx length %hu+1\n",
            sq,
            cmd.common.command_id,
            cmd.write_zeroes.slba,
            cmd.write_zeroes.length);
        submit_write_zeroes_async(sq, cmd, tag);
        return NVME_SC_SUCCESS;
    } else if (cmd.common.opcode == nvme_cmd_flush) {
        submit_flush_async(sq, cmd, tag);
        return NVME_SC_SUCCESS;
    } else {
        DBG_PRINTF("sq %zu unknown opcode %#hhx cid %hu\n", sq, cmd.common.opcode, cmd.common.command_id);
    }
    /*
} catch (const std::exception &e) {
    printf("unhandled exception: %s\n", e.what());
    return NVME_SC_DNR | NVME_SC_INTERNAL;
}
*/
    return NVME_SC_DNR | NVME_SC_INVALID_OPCODE;
}

cq_window nvme_sender_aio::get_pending_completions(std::span<io_uring_cqe *> cqebuf) {
    return _ring.cq_get_ready(cqebuf);
}

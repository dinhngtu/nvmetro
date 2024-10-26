#include <algorithm>
#include <array>
#include <exception>
#include <memory>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <cassert>
#include <system_error>

#include <fcntl.h>
#include <unistd.h>
#include <sys/uio.h>
#include <liburing/io_uring.h>

#include "nvme_core.hpp"
#include "nvme_encryptor_aio.hpp"
#include "util.hpp"
#include "prp.hpp"
#include "vm.hpp"

__u16 nvme_encryptor_aio::receive_read([[maybe_unused]] size_t sq, const nvme_command &cmd) {
    for (auto lit = nvme_cmd_lba_iter(*this, cmd); !lit.at_end(); lit++) {
        if (!_engine->decrypt(*lit, lit.lba())) {
            return NVME_SC_DNR | NVME_SC_INTERNAL;
        }
    }
    return NVME_SC_SUCCESS;
}

void nvme_encryptor_aio::submit_write_async([[maybe_unused]] size_t sq, const nvme_command &cmd, uint32_t tag) {
    nvme_cmd_lba_iter lit(*this, cmd);

    auto ticket = new mem_ticket<sq_ticket>(tag, lit.cmd_nbytes());
    std::span bufspan(ticket->mem.get(), lit.cmd_nbytes());
    for (; !lit.at_end(); lit++) {
        auto ciphert = bufspan.subspan(lit.command_lba_index() << lit.cmd_lba_shift(), lit.cmd_lba_size());
        if (!_engine->encrypt(ciphert, *lit, lit.lba())) {
            throw std::runtime_error("cannot encrypt");
        }
    }

    _ring.queue_write(ticket, ticket->mem.get(), lit.cmd_nbytes(), -1, true, 0, lit.cmd_slba() << lit.cmd_lba_shift());
}

void nvme_encryptor_aio::submit_write_zeroes_async([[maybe_unused]] size_t sq, const nvme_command &cmd, uint32_t tag) {
    auto slba = cmd.rw.slba;
    size_t nblocks = static_cast<size_t>(cmd.rw.length) + 1;
    int lbas = ns_lba_shift(cmd.rw.nsid);
    size_t nbytes = nblocks << lbas;

    auto ticket = new mem_ticket<sq_ticket>(tag, nbytes);
    std::span bufspan(ticket->mem.get(), nbytes);
    std::fill(bufspan.begin(), bufspan.end(), '\0');
    for (uint64_t cli = 0; cli < nblocks; cli++) {
        auto ciphert = bufspan.subspan(cli, 1 << lbas);
        if (!_engine->encrypt(ciphert, ciphert, cli)) {
            throw std::runtime_error("cannot encrypt");
        }
    }

    _ring.queue_write(ticket, ticket->mem.get(), nbytes, -1, true, 0, slba << lbas);
}

void nvme_encryptor_aio::submit_flush_async(
    [[maybe_unused]] size_t sq,
    [[maybe_unused]] const nvme_command &cmd,
    uint32_t tag) {
    auto ticket = new sq_ticket(tag);
    _ring.queue_fsync(ticket, true, 0, IORING_FSYNC_DATASYNC);
}

bool nvme_encryptor_aio::submit_async(
    [[maybe_unused]] size_t sq,
    const nvme_command &cmd,
    uint32_t tag,
    __u16 &outstatus) {
    if (cmd.common.opcode == nvme_cmd_read) {
        outstatus = receive_read(sq, cmd);
        return false;
    } else if (cmd.common.opcode == nvme_cmd_write) {
        DBG_PRINTF(
            "sq %zu write cid %hu slba %#llx length %hu+1\n",
            sq,
            cmd.common.command_id,
            cmd.rw.slba,
            cmd.rw.length);
        DBG_PRINTF("prp1=%#llx prp2=%#llx\n", cmd.rw.dptr.prp1, cmd.rw.dptr.prp2);
        if (!(cmd.common.flags & NVME_CMD_SGL_ALL)) {
            submit_write_async(sq, cmd, tag);
            return true;
        }
    } else if (cmd.common.opcode == nvme_cmd_write_zeroes) {
        DBG_PRINTF(
            "sq %zu write_zeroes cid %hu slba %#llx length %hu+1\n",
            sq,
            cmd.common.command_id,
            cmd.write_zeroes.slba,
            cmd.write_zeroes.length);
        submit_write_zeroes_async(sq, cmd, tag);
        return true;
    } else if (cmd.common.opcode == nvme_cmd_flush) {
        submit_flush_async(sq, cmd, tag);
        return true;
    } else {
        DBG_PRINTF("sq %zu unknown opcode %#hhx cid %hu\n", sq, cmd.common.opcode, cmd.common.command_id);
    }
    outstatus = NVME_SC_DNR | NVME_SC_INVALID_OPCODE;
    return false;
}

cq_window nvme_encryptor_aio::get_pending_completions(std::span<io_uring_cqe *> cqebuf) {
    return _ring.cq_get_ready(cqebuf);
}

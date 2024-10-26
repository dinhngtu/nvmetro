#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
#include <cstring>
#include <vector>
#include <system_error>

#include <fcntl.h>
#include <unistd.h>
#include <sys/uio.h>
#include <liburing/io_uring.h>

#include "nvme_core.hpp"
#include "nvme_encryptor_sgx_aio.hpp"
#include "util.hpp"
#include "sgx/prp_en.hpp"
#include "util/uring.hpp"
#include "vm.hpp"

__u16 nvme_encryptor_sgx_aio::receive_read([[maybe_unused]] size_t sq, const nvme_command &cmd) {
    nvme_cmd_lba_iter lit(*this, cmd);
    auto ret = _e.crypt_command_inplace(&cmd, 1);
    if (ret != static_cast<long>(lit.cmd_nbytes())) {
        std::stringstream ef;
        ef << "unexpected length " << ret << ", expected " << lit.cmd_nbytes();
        throw std::runtime_error(ef.str());
    }
    return NVME_SC_SUCCESS;
}

void nvme_encryptor_sgx_aio::submit_write_async([[maybe_unused]] size_t sq, const nvme_command &cmd, uint32_t tag) {
    nvme_cmd_lba_iter lit(*this, cmd);

    auto ticket = new mem_ticket<sq_ticket>(tag, lit.cmd_nbytes());
    std::span bufspan(ticket->mem.get(), lit.cmd_nbytes());
    _e.crypt_command(&cmd, bufspan.data(), bufspan.size(), 0);

    _ring.queue_write(ticket, ticket->mem.get(), lit.cmd_nbytes(), -1, true, 0, lit.cmd_slba() << lit.cmd_lba_shift());
}

void nvme_encryptor_sgx_aio::submit_write_zeroes_async(
    [[maybe_unused]] size_t sq,
    const nvme_command &cmd,
    uint32_t tag) {
    auto slba = cmd.rw.slba;
    size_t nblocks = static_cast<size_t>(cmd.rw.length) + 1;
    int lbas = ns_lba_shift(cmd.rw.nsid);
    size_t nbytes = nblocks << lbas;

    auto ticket = new mem_ticket<sq_ticket>(tag, nbytes);
    std::span bufspan(ticket->mem.get(), nbytes);
    std::fill(bufspan.begin(), bufspan.end(), '\0');
    _e.crypt_buffer_inplace(slba, bufspan.data(), nblocks, 0);

    _ring.queue_write(ticket, ticket->mem.get(), nbytes, -1, true, 0, slba << lbas);
}

void nvme_encryptor_sgx_aio::submit_flush_async(
    [[maybe_unused]] size_t sq,
    [[maybe_unused]] const nvme_command &cmd,
    uint32_t tag) {
    auto ticket = new sq_ticket(tag);
    _ring.queue_fsync(ticket, true, 0, IORING_FSYNC_DATASYNC);
}

bool nvme_encryptor_sgx_aio::submit_async(
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

cq_window nvme_encryptor_sgx_aio::get_pending_completions(std::span<io_uring_cqe *> cqebuf) {
    return _ring.cq_get_ready(cqebuf);
}

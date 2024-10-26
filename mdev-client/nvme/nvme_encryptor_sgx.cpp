#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
#include <cstring>
#include <vector>
#include <system_error>

#include "nvme_core.hpp"
#include "nvme_encryptor_sgx.hpp"
#include "util.hpp"
#include "sgx/prp_en.hpp"

constexpr size_t wz_nblocks = 512;

__u16 nvme_encryptor_sgx::receive_read([[maybe_unused]] size_t sq, const nvme_command &cmd) {
    nvme_cmd_lba_iter lit(*this, cmd);
    auto ret = _e.crypt_command_inplace(&cmd, 1);
    if (ret != static_cast<long>(lit.cmd_nbytes())) {
        std::stringstream ef;
        ef << "unexpected length " << ret << ", expected " << lit.cmd_nbytes();
        throw std::runtime_error(ef.str());
    }
    return NVME_SC_SUCCESS;
}

__u16 nvme_encryptor_sgx::receive_write_copyback([[maybe_unused]] size_t sq, const nvme_command &cmd) {
    nvme_cmd_lba_iter lit(*this, cmd);
    if (_encbuf.size() < lit.cmd_nbytes()) {
        _encbuf.resize(lit.cmd_nbytes());
    }
    auto encbuf = std::span(&_encbuf[0], lit.cmd_nbytes());
    auto ret = _e.crypt_command(&cmd, encbuf.data(), encbuf.size(), 0);
    if (ret != static_cast<long>(lit.cmd_nbytes())) {
        std::stringstream ef;
        ef << "unexpected length " << ret << ", expected " << lit.cmd_nbytes();
        throw std::runtime_error(ef.str());
    }
    auto remaining = static_cast<ssize_t>(encbuf.size());
    while (remaining) {
        auto boff = encbuf.size() - remaining;
        auto subwrite = encbuf.subspan(boff, remaining);
        auto ret = pwrite(_bfd, subwrite.data(), subwrite.size(), (lit.cmd_slba() << lit.cmd_lba_shift()) + boff);
        if (ret < 0 || ret > remaining) {
            throw std::system_error(errno, std::generic_category(), "cannot write to blkdev");
        } else if (ret == 0) {
            // EOF?
            printf("unexpected eof at lba=%#lx + %#lx bytes\n", lit.cmd_slba(), boff);
            break;
        }
        remaining -= ret;
    }
    return NVME_SC_SUCCESS;
}

__u16 nvme_encryptor_sgx::receive_write_zeroes([[maybe_unused]] size_t sq, const nvme_command &cmd) {
    auto slba = cmd.rw.slba;
    size_t nblocks = static_cast<size_t>(cmd.rw.length) + 1;
    int lbas = ns_lba_shift(cmd.rw.nsid);

    if (_zerobuf.size() < size_t{1} << lbas) {
        _zerobuf.resize(size_t{1} << lbas);
    }
    auto zerobuf = std::span(&_zerobuf[0], 1 << lbas);
    if (_encbuf.size() < wz_nblocks << lbas) {
        _encbuf.resize(wz_nblocks << lbas);
    }
    auto encbuf = std::span(&_encbuf[0], wz_nblocks << lbas);
    size_t cli = 0;
    while (cli < nblocks) {
        ssize_t this_nblocks = static_cast<ssize_t>(std::min(nblocks - cli, wz_nblocks));
        auto ret = _e.crypt_buffer_inplace(slba + cli, encbuf.data(), this_nblocks, 0);
        if (ret != this_nblocks << lbas) {
            std::stringstream ef;
            ef << "unexpected length " << ret << ", expected " << this_nblocks << lbas;
            throw std::runtime_error(ef.str());
        }
        ssize_t remaining = static_cast<ssize_t>(this_nblocks) << lbas;
        while (remaining) {
            auto boff = (this_nblocks << lbas) - remaining;
            auto subwrite = encbuf.subspan(boff, remaining);
            auto ret = pwrite(_bfd, subwrite.data(), subwrite.size(), ((slba + cli) << lbas) + boff);
            if (ret < 0 || ret > remaining) {
                throw std::system_error(errno, std::generic_category(), "cannot write to blkdev");
                return NVME_SC_DNR | NVME_SC_INTERNAL;
            } else if (ret == 0) {
                // EOF/write past the end?
                return NVME_SC_DNR | NVME_SC_LBA_RANGE;
            }
            remaining -= ret;
        }
        cli += this_nblocks;
    }
    return NVME_SC_SUCCESS;
}

__u16 nvme_encryptor_sgx::receive([[maybe_unused]] size_t sq, const nvme_command &cmd) {
    try {
        if (cmd.common.opcode == nvme_cmd_read) {
            DBG_PRINTF(
                "sq %zu read cid %hu slba %#llx length %hu+1\n",
                sq,
                cmd.common.command_id,
                cmd.rw.slba,
                cmd.rw.length);
            DBG_PRINTF("prp1=%#llx prp2=%#llx\n", cmd.rw.dptr.prp1, cmd.rw.dptr.prp2);
            if (!(cmd.common.flags & NVME_CMD_SGL_ALL)) {
                return receive_read(sq, cmd);
            } else {
                return NVME_SC_DNR | NVME_SC_INVALID_FIELD;
            }
        } else if (cmd.common.opcode == nvme_cmd_write) {
            DBG_PRINTF(
                "sq %zu write cid %hu slba %#llx length %hu+1\n",
                sq,
                cmd.common.command_id,
                cmd.rw.slba,
                cmd.rw.length);
            DBG_PRINTF("prp1=%#llx prp2=%#llx\n", cmd.rw.dptr.prp1, cmd.rw.dptr.prp2);
            if (!(cmd.common.flags & NVME_CMD_SGL_ALL)) {
                return receive_write_copyback(sq, cmd);
            } else {
                return NVME_SC_DNR | NVME_SC_INVALID_FIELD;
            }
        } else if (cmd.common.opcode == nvme_cmd_write_zeroes) {
            DBG_PRINTF(
                "sq %zu write_zeroes cid %hu slba %#llx length %hu+1\n",
                sq,
                cmd.common.command_id,
                cmd.write_zeroes.slba,
                cmd.write_zeroes.length);
            return receive_write_zeroes(sq, cmd);
        } else {
            DBG_PRINTF("sq %zu unknown opcode %#hhx cid %hu\n", sq, cmd.common.opcode, cmd.common.command_id);
        }
    } catch (const nvme_exception &e) {
        return e.code();
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return NVME_SC_DNR | NVME_SC_INTERNAL;
    }
    return NVME_SC_DNR | NVME_SC_INTERNAL;
}

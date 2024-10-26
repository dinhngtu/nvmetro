#include <memory>
#include <vector>
#include <cassert>

#include <fcntl.h>
#include <unistd.h>
#include <sys/uio.h>

#include "nvme_core.hpp"
#include "nvme_sender.hpp"
#include "util.hpp"
#include "prp.hpp"
#include "vm.hpp"

nvme_sender::nvme_sender(const std::shared_ptr<mapping> &vm, int nfd, int bfd) : nvme(vm, nfd), _bfd(bfd) {
    _wvec.reserve(1 << id_vctrl()->mdts);
}

__u16 nvme_sender::receive_write([[maybe_unused]] size_t sq, const nvme_command &cmd) {
    auto slba = cmd.rw.slba;
    size_t nblocks = static_cast<size_t>(cmd.rw.length) + 1;
    int lbas = ns_lba_shift(cmd.rw.nsid);
    size_t nbytes = ns_cmd_check_nbytes(nblocks, lbas);
    prp_list cmd_prpl{reinterpret_cast<prp_list::const_pointer>(&cmd.rw.dptr.prp1), 2};
    prp_chain_iter prp_begin(vm(), cmd_prpl, 0, nbytes);

    _wvec.clear();
    for (auto &prp_it = prp_begin; !prp_it.at_end(); prp_it++) {
        DBG_PRINTF("page %#lx size %zu\n", *prp_it, prp_it.this_nbytes());
        // lba index inside current page
        auto lbad = vm()->get_span(*prp_it, prp_it.this_nbytes());
        _wvec.push_back(iovec{.iov_base = lbad.data(), .iov_len = lbad.size()});
    }

    auto ret = pwritev2(_bfd, _wvec.data(), _wvec.size(), slba << lbas, (cmd.rw.control & NVME_RW_FUA) ? RWF_DSYNC : 0);
    if (ret != static_cast<ssize_t>(nbytes)) {
        // writev/pwritev should be atomic
        printf("failed or short write %zd\n", ret);
        return NVME_SC_DNR | NVME_SC_INTERNAL;
    } else {
        return NVME_SC_SUCCESS;
    }
}

__u16 nvme_sender::receive_write_zeroes([[maybe_unused]] size_t sq, const nvme_command &cmd) {
    auto slba = cmd.rw.slba;
    size_t nblocks = static_cast<size_t>(cmd.rw.length) + 1;
    int lbas = ns_lba_shift(cmd.rw.nsid);

    auto ret = fallocate(_bfd, FALLOC_FL_ZERO_RANGE, slba << lbas, nblocks << lbas);
    if (ret < 0) {
        switch (errno) {
        case EFBIG:
            return NVME_SC_DNR | NVME_SC_LBA_RANGE;
        case EIO:
            return NVME_SC_INTERNAL;
        default:
            return NVME_SC_DNR | NVME_SC_INTERNAL;
        }
    } else {
        return NVME_SC_SUCCESS;
    }
}

__u16 nvme_sender::receive_flush([[maybe_unused]] size_t sq, [[maybe_unused]] const nvme_command &cmd) {
    if (fdatasync(_bfd) < 0) {
        return NVME_SC_DNR | NVME_SC_INTERNAL;
    } else {
        return NVME_SC_SUCCESS;
    }
}

__u16 nvme_sender::receive([[maybe_unused]] size_t sq, const nvme_command &cmd) {
    try {
        if (cmd.common.opcode == nvme_cmd_write) {
            DBG_PRINTF(
                "sq %zu write cid %hu slba %#llx length %hu+1\n",
                sq,
                cmd.common.command_id,
                cmd.rw.slba,
                cmd.rw.length);
            DBG_PRINTF("prp1=%#llx prp2=%#llx\n", cmd.rw.dptr.prp1, cmd.rw.dptr.prp2);
            if (!(cmd.common.flags & NVME_CMD_SGL_ALL)) {
                return receive_write(sq, cmd);
            }
        } else if (cmd.common.opcode == nvme_cmd_write_zeroes) {
            DBG_PRINTF(
                "sq %zu write_zeroes cid %hu slba %#llx length %hu+1\n",
                sq,
                cmd.common.command_id,
                cmd.write_zeroes.slba,
                cmd.write_zeroes.length);
            return receive_write_zeroes(sq, cmd);
        } else if (cmd.common.opcode == nvme_cmd_flush) {
            return receive_flush(sq, cmd);
        } else {
            DBG_PRINTF("sq %zu unknown opcode %#hhx cid %hu\n", sq, cmd.common.opcode, cmd.common.command_id);
        }
    } catch (const nvme_exception &e) {
        return e.code();
    } catch (const std::exception &e) {
        printf("unhandled exception: %s\n", e.what());
        return NVME_SC_DNR | NVME_SC_INTERNAL;
    }
    return NVME_SC_DNR | NVME_SC_INVALID_OPCODE;
}

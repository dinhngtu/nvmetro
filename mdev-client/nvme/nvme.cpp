#include <cinttypes>
#include <unistd.h>
#include <sys/ioctl.h>

#include "nvme.hpp"
#include "util.hpp"
#include "prp.hpp"

int nvme::do_id_vctrl() {
    if (_id) {
        return 0;
    }
    auto ioc_id = std::make_unique<nvme_mdev_id_vctrl>();
    auto ret = ioctl(_nfd, NVME_MDEV_NOTIFYFD_ID_VCTRL, ioc_id.get());
    if (ret < 0) {
        return -errno;
    }
    _id = std::make_unique<nvme_id_ctrl>();
    *_id = *reinterpret_cast<nvme_id_ctrl *>(ioc_id->data);
    return 0;
}

int nvme::do_id_vns(__u32 nsid) {
    if (nsid == 0 || nsid >= MAX_VIRTUAL_NAMESPACES) {
        return -EINVAL;
    }
    if (_idns[nsid]) {
        return 0;
    }
    auto ioc_idns = std::make_unique<nvme_mdev_id_vns>();
    ioc_idns->nsid = nsid;
    auto ret = ioctl(_nfd, NVME_MDEV_NOTIFYFD_ID_VNS, ioc_idns.get());
    if (ret < 0) {
        return -errno;
    }
    _idns[nsid] = std::make_unique<nvme_id_ns>();
    *_idns[nsid] = *reinterpret_cast<nvme_id_ns *>(ioc_idns->data);
    return 0;
}

/*
__u16 nvme::receive(size_t sq, const nvme_command &cmd) {
    (void)sq;
    if (cmd.common.opcode == nvme_cmd_read) {
        DBG_PRINTF(
            "sq %zu read cid %hu slba %#llx length %hu+1\n",
            sq,
            cmd.common.command_id,
            cmd.rw.slba,
            cmd.rw.length);
        DBG_PRINTF("prp1=%#llx prp2=%#llx\n", cmd.rw.dptr.prp1, cmd.rw.dptr.prp2);
    } else if (cmd.common.opcode == nvme_cmd_write) {
        DBG_PRINTF(
            "sq %zu write cid %hu slba %#llx length %hu+1\n",
            sq,
            cmd.common.command_id,
            cmd.rw.slba,
            cmd.rw.length);
        DBG_PRINTF("prp1=%#llx prp2=%#llx\n", cmd.rw.dptr.prp1, cmd.rw.dptr.prp2);
        if (!(cmd.common.flags & NVME_CMD_SGL_ALL)) {
            size_t nblocks = static_cast<size_t>(cmd.rw.length) + 1;
            auto &id = id_vctrl();
            auto &idns = id_vns(cmd.rw.nsid);
            if (!idns) {
                return NVME_SC_DNR | NVME_SC_INVALID_NS;
            }
            auto ns_lba_shift = lba_shift(*idns);
            if (!check_nblocks(nblocks, id->mdts, ns_lba_shift)) {
                return NVME_SC_DNR | NVME_SC_INVALID_FIELD;
            }
            prp_list cmd_prpl{reinterpret_cast<prp_list::const_pointer>(&cmd.rw.dptr.prp1), 2};
            prp_chain_iter prp_begin(this->_vm, cmd_prpl, 0, nblocks << ns_lba_shift);
            for (auto &prp_it = prp_begin; !prp_it.at_end(); prp_it++) {
                DBG_PRINTF("page %#lx size %zu\n", *prp_it, prp_it.this_nbytes());
                // DBG_PRINTF("value at page = %#lx\n", _vm->get_u64(*prp_it));
                _vm->get_u64(*prp_it);
            }
        }
    } else if (cmd.common.opcode == nvme_cmd_write_zeroes) {
        DBG_PRINTF(
            "sq %zu write_zeroes cid %hu slba %#llx length %hu+1\n",
            sq,
            cmd.common.command_id,
            cmd.write_zeroes.slba,
            cmd.write_zeroes.length);
    }
    return NVME_SC_SUCCESS;
}
*/

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

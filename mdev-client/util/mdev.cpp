#include <cstdio>
#include <algorithm>
#include <span>
#include <stdexcept>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/vfio.h>
#include <linux/nvme_mdev.h>

#include "nvme_core.hpp"
#include "util.hpp"
#include "util/mdev.hpp"

int mdev_open(const char *arg_iommu_group, const char *arg_mdev_uuid, std::vector<int> &sqfd) {
    auto vcfd = open("/dev/vfio/vfio", O_RDWR);
    if (vcfd < 0) {
        perror("cannot open vfio container");
        return -1;
    }
    auto hvc = cleanup([&] { close(vcfd); });

    if (ioctl(vcfd, VFIO_GET_API_VERSION) != VFIO_API_VERSION) {
        fprintf(stderr, "unknown vfio version\n");
        return -1;
    }

    if (!ioctl(vcfd, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU)) {
        fprintf(stderr, "unsupported iommu\n");
        return -1;
    }

    auto vgfd = open(arg_iommu_group, O_RDWR);
    if (vgfd < 0) {
        perror("cannot open iommu group");
        return -1;
    }
    auto hvg = cleanup([&] { close(vgfd); });

    struct vfio_group_status group_status = {
        .argsz = sizeof(group_status),
        .flags = 0,
    };
    ioctl(vgfd, VFIO_GROUP_GET_STATUS, &group_status);
    if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
        fprintf(stderr, "group not viable\n");
        return -1;
    }

    if (ioctl(vgfd, VFIO_GROUP_SET_CONTAINER, &vcfd) < 0) {
        perror("cannot set container");
        return -1;
    }

    if (ioctl(vcfd, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU) < 0) {
        perror("cannot set iommu");
        return -1;
    }

    auto mdevfd = ioctl(vgfd, VFIO_GROUP_GET_DEVICE_FD, arg_mdev_uuid);
    if (mdevfd < 0) {
        perror("cannot open mdev");
        return -1;
    }
    auto hmdev = cleanup([&] { close(mdevfd); });

    auto qcount = ioctl(mdevfd, NVME_MDEV_GET_VQ_COUNT);
    if (qcount > 1) {
        fprintf(stderr, "detected %d vqs\n", qcount);
        qcount = std::min(qcount, static_cast<int>(sqfd.size()));
    } else {
        perror("cannot detect mdev vq count");
        qcount = sqfd.size();
    }

    std::fill(sqfd.begin(), sqfd.end(), -1);
    // non-open sqs should give us negative fd values which are safe with poll()
    for (int i = 0; i < qcount; i++) {
        struct nmntfy_open_arg arg = {static_cast<__u16>(i)};
        sqfd[i] = ioctl(mdevfd, NVME_MDEV_OPEN_FD, &arg);
        if (sqfd[i] >= 0) {
            fprintf(stderr, "opened sq %d = %d\n", i, sqfd[i]);
        }
    }
    return 0;
}

void *mdev_vm_mmap(int mfd, size_t below_4g_mem_size, size_t &pvm_size) {
    if (below_4g_mem_size & (NVME_PAGE_SIZE - 1)) {
        throw std::invalid_argument("low memory size is not page aligned");
    }

    struct stat mfs {};
    if (fstat(mfd, &mfs) < 0) {
        throw std::system_error(errno, std::generic_category(), "cannot stat mem file");
    }

    if (mfs.st_size <= static_cast<long>(below_4g_mem_size)) {
        pvm_size = mfs.st_size;
        below_4g_mem_size = pvm_size;
    } else {
        pvm_size = (4ull << 30) + mfs.st_size - below_4g_mem_size;
    }
    void *pvm = mmap(NULL, pvm_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (pvm == MAP_FAILED) { // NOLINT
        throw std::system_error(errno, std::generic_category(), "cannot mmap vmmem region");
    }
    if (madvise(pvm, pvm_size, MADV_DONTDUMP) < 0) {
        throw std::system_error(errno, std::generic_category(), "cannot madvise(MADV_DONTDUMP) vmmem region");
    }

    if (mmap(pvm, below_4g_mem_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, mfd, 0) == MAP_FAILED) { // NOLINT
        throw std::system_error(errno, std::generic_category(), "cannot mmap low memory");
    }
    if (madvise(pvm, below_4g_mem_size, MADV_DONTDUMP) < 0) {
        throw std::system_error(errno, std::generic_category(), "cannot madvise(MADV_DONTDUMP) low memory");
    }

    auto pvm_hi = static_cast<char *>(pvm) + (4ull << 30); // NOLINT
    auto pvm_hisize = mfs.st_size - below_4g_mem_size;
    if (pvm_hisize > 0) {
        if (mmap(
                pvm_hi,
                pvm_hisize,
                PROT_READ | PROT_WRITE,
                MAP_SHARED | MAP_FIXED,
                mfd,
                below_4g_mem_size) == MAP_FAILED) { // NOLINT
            throw std::system_error(errno, std::generic_category(), "cannot mmap high memory");
        }
        if (madvise(pvm_hi, pvm_hisize, MADV_DONTDUMP) < 0) {
            throw std::system_error(errno, std::generic_category(), "cannot madvise(MADV_DONTDUMP) high memory");
        }
    }

    return pvm;
}

[[maybe_unused]] static constexpr uint64_t fileoff_to_vaddr(uint64_t off, size_t below_4g_mem_size) {
    if (off < below_4g_mem_size)
        return off;
    return (4ull << 30) + off - below_4g_mem_size;
}

static constexpr uint64_t vaddr_to_fileoff(uint64_t addr, size_t below_4g_mem_size) {
    if (addr < below_4g_mem_size)
        return addr;
    else if (addr < (4ull << 30))
        return UINT64_MAX;
    return addr - (4ull << 30) + below_4g_mem_size;
}

void *mdev_vm_mmap_hmb(
    int mfd,
    size_t below_4g_mem_size,
    uint32_t hmb_pages,
    std::span<volatile hmb_dl_entry> hmdl,
    uint32_t &mapped) {
    auto buf = mmap(nullptr, hmb_pages << NVME_PAGE_SHIFT, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (buf == MAP_FAILED)
        throw std::system_error(errno, std::generic_category(), "cannot mmap hmb scaffolding");
    if (madvise(buf, hmb_pages << NVME_PAGE_SHIFT, MADV_DONTDUMP) < 0)
        throw std::system_error(errno, std::generic_category(), "cannot madvise(MADV_DONTDUMP) hmb scaffolding");

    mapped = 0;
    auto begin = static_cast<uint8_t *>(buf);
    for (const auto &entry : hmdl) {
        uint32_t out;
        auto add = entry.badd & ~(NVME_PAGE_SIZE - 1);
        auto foff = vaddr_to_fileoff(add, below_4g_mem_size);
        auto count = entry.bpages;
        if (__builtin_add_overflow(mapped, count, &out) || out > hmb_pages || foff == UINT64_MAX)
            break;
        if (mmap(begin, count << NVME_PAGE_SHIFT, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, mfd, foff) ==
            MAP_FAILED) {
            munmap(buf, hmb_pages << NVME_PAGE_SHIFT);
            return MAP_FAILED;
        }
        mapped += count;
        begin += count << NVME_PAGE_SHIFT;
    }
    return buf;
}

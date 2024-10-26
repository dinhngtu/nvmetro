#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>
#include <system_error>
#include "nvme_core.hpp"
#include "xcow/file_deref.hpp"

xcow::FileDeref::FileDeref(int fd, int prot) {
    auto flen = lseek(fd, 0, SEEK_END);
    if (flen < 0)
        throw std::system_error(errno, std::generic_category(), "lseek");
    auto m = mmap(nullptr, flen, prot, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED)
        throw std::system_error(errno, std::generic_category(), "mmap");
    if (madvise(m, flen, MADV_DONTDUMP) < 0)
        throw std::system_error(errno, std::generic_category(), "cannot madvise(MADV_DONTDUMP) deref memory");
    _p = {static_cast<uint8_t *>(m), static_cast<size_t>(flen)};
}

void xcow::FileDeref::commit(uint64_t off, size_t nbytes) {
    auto poff = off & (NVME_PAGE_SIZE - 1);
    if (poff) {
        off -= poff;
        nbytes += poff;
    }
    auto region = _p.subspan(off, nbytes);
    if (msync(region.data(), region.size(), MS_SYNC | MS_INVALIDATE) != 0)
        throw std::system_error(errno, std::generic_category(), "msync");
}

void xcow::FileDeref::prefetch(uint64_t off, size_t nbytes) {
    auto region = _p.subspan(off, nbytes);
    if (madvise(region.data(), region.size(), MADV_WILLNEED) < 0)
        throw std::system_error(errno, std::generic_category(), "madvise(MADV_WILLNEED)");
}

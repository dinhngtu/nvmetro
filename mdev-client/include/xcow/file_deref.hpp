#pragma once

#include <fcntl.h>
#include <sys/mman.h>
#include "xcow/deref.hpp"
#include "util.hpp"

namespace xcow {

class FileDeref : public xcow::Deref {
public:
    explicit FileDeref(int fd, int prot = PROT_READ | PROT_WRITE);
    constexpr std::span<uint8_t> deref(uint64_t off, size_t nbytes) override {
        return _p.subspan(off, nbytes);
    }
    void commit(uint64_t off, size_t nbytes) override;
    void prefetch(uint64_t off, size_t nbytes) override;

private:
    std::span<uint8_t> _p;
};

static cleanup file_lock(int fd, short type = F_WRLCK, short whence = SEEK_SET, off_t start = 0, off_t len = 0) {
    flock lk{
        .l_type = type,
        .l_whence = whence,
        .l_start = start,
        .l_len = len,
    };
    if (fcntl(fd, F_OFD_SETLK, &lk) < 0)
        throw std::system_error(errno, std::generic_category(), "fcntl(F_OFD_SETLK)");
    return cleanup([=] {
        flock unlk{
            .l_type = F_UNLCK,
            .l_whence = whence,
            .l_start = start,
            .l_len = len,
        };
        fcntl(fd, F_OFD_SETLK, &unlk);
    });
}

}; // namespace xcow

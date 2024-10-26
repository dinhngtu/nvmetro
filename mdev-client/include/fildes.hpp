#pragma once

#include <cassert>
#include <utility>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

class FileDescriptor {
public:
    constexpr FileDescriptor() {
    }
    constexpr FileDescriptor(int fd) : _fd{fd} {
    }
    constexpr FileDescriptor(int fd, int errno_) : _fd{fd}, _errno{errno_} {
    }
    explicit inline FileDescriptor(const char *pathname, int flags) {
        _fd = open(pathname, flags);
        if (_fd < 0)
            _errno = errno;
    }
    explicit inline FileDescriptor(const char *pathname, int flags, mode_t mode) {
        _fd = open(pathname, flags, mode);
        if (_fd < 0)
            _errno = errno;
    }
    constexpr FileDescriptor(const FileDescriptor &) = delete;
    constexpr FileDescriptor &operator=(const FileDescriptor &) = delete;
    constexpr FileDescriptor(FileDescriptor &&other) {
        swap(*this, other);
    }
    constexpr FileDescriptor &operator=(FileDescriptor &&other) {
        if (this != &other) {
            dispose();
            swap(*this, other);
        }
        return *this;
    };
    constexpr ~FileDescriptor() {
        dispose();
    }

    constexpr operator int() const {
        return _fd;
    }
    constexpr int err() const {
        return _errno;
    }

    constexpr friend void swap(FileDescriptor &self, FileDescriptor &other) {
        using std::swap;
        swap(self._fd, other._fd);
        swap(self._errno, other._errno);
    }

private:
    int _fd = -1;
    int _errno = 0;

    constexpr void dispose() {
        if (_fd >= 0) {
            close(std::exchange(_fd, -1));
            _errno = 0;
        }
    }
};

#pragma once

#include <vector>
#include <sys/uio.h>

#include "nvme.hpp"

class nvme_sender final : public nvme {
public:
    explicit nvme_sender(const std::shared_ptr<mapping> &vm, int nfd, int bfd);
    nvme_sender(const nvme_sender &) = delete;
    nvme_sender &operator=(const nvme_sender &) = delete;
    nvme_sender(nvme_sender &&) = default;
    nvme_sender &operator=(nvme_sender &&) = default;
    ~nvme_sender() = default;

    __u16 receive(size_t sq, const nvme_command &cmd);

private:
    __u16 receive_write(size_t sq, const nvme_command &cmd);
    __u16 receive_write_zeroes(size_t sq, const nvme_command &cmd);
    __u16 receive_flush(size_t sq, const nvme_command &cmd);
    int _bfd;
    std::vector<iovec> _wvec;
};

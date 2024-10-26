#pragma once

#include <vector>

#include "nvme.hpp"
#include "crypto/tbc.hpp"
#include "aligned_allocator.hpp"

class nvme_encryptor final : public nvme {
public:
    explicit nvme_encryptor(
        const std::shared_ptr<mapping> &vm,
        int nfd,
        int bfd,
        std::unique_ptr<tweakable_block_cipher> &&engine)
        : nvme(vm, nfd), _bfd(bfd), _engine(std::move(engine)), _zerobuf(), _encbuf() {
    }
    nvme_encryptor(const nvme_encryptor &) = delete;
    nvme_encryptor &operator=(const nvme_encryptor &) = delete;
    nvme_encryptor(nvme_encryptor &&) = default;
    nvme_encryptor &operator=(nvme_encryptor &&) = default;
    ~nvme_encryptor() = default;

    __u16 receive(size_t sq, const nvme_command &cmd);

private:
    __u16 receive_read(size_t sq, const nvme_command &cmd);
    __u16 receive_write_copyback(size_t sq, const nvme_command &cmd);
    __u16 receive_write_zeroes(size_t sq, const nvme_command &cmd);
    int _bfd;
    std::unique_ptr<tweakable_block_cipher> _engine;
    std::vector<unsigned char, aligned_allocator<unsigned char, 64>> _zerobuf;
    std::vector<unsigned char, aligned_allocator<unsigned char, 64>> _encbuf;
};

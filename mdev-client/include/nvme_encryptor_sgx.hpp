#pragma once

#include "nvme.hpp"
#include "sgx/prp_en.hpp"
#include "aligned_allocator.hpp"

class nvme_encryptor_sgx final : public nvme {
public:
    explicit nvme_encryptor_sgx(
        const std::shared_ptr<mapping> &vm,
        int nfd,
        int bfd,
        const std::string &epath,
        bool edebug,
        std::array<unsigned char, 32> key,
        int lba_shift)
        : nvme(vm, nfd), _bfd(bfd), _vm(vm), _e(epath, edebug, _vm->data(), _vm->size(), key, lba_shift) {
    }
    nvme_encryptor_sgx(const nvme_encryptor_sgx &) = delete;
    nvme_encryptor_sgx &operator=(const nvme_encryptor_sgx &) = delete;
    nvme_encryptor_sgx(nvme_encryptor_sgx &&) = default;
    nvme_encryptor_sgx &operator=(nvme_encryptor_sgx &&) = default;
    ~nvme_encryptor_sgx() = default;

    __u16 receive(size_t sq, const nvme_command &cmd);

private:
    __u16 receive_read(size_t sq, const nvme_command &cmd);
    __u16 receive_write_copyback(size_t sq, const nvme_command &cmd);
    __u16 receive_write_zeroes(size_t sq, const nvme_command &cmd);
    int _bfd;
    std::shared_ptr<mapping> _vm;
    prp_en _e;
    std::vector<unsigned char, aligned_allocator<unsigned char, NVME_PAGE_SIZE>> _zerobuf;
    std::vector<unsigned char, aligned_allocator<unsigned char, NVME_PAGE_SIZE>> _encbuf;
};

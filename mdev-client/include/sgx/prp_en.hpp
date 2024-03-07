#pragma once

#include <array>
#include <stdexcept>

#include "Enclave_u.h"
#include "sgx/enclave.hpp"
#include "sgx/Enclave_u.h"

#include <sgx_error.h>

class prp_en {
public:
    explicit prp_en(
        const std::string &filename,
        bool debug,
        unsigned char *pvm,
        size_t pvm_size,
        std::array<unsigned char, 32> key,
        int lba_shift)
        : _e(filename, debug), _pvm(pvm), _pvm_size(pvm_size) {
        if (_e.invoke(::put_key, key.data(), lba_shift) < 0) {
            throw std::runtime_error("sgx put_key failed");
        }
    }

    inline long crypt_buffer_inplace(size_t slba, unsigned char *buf, size_t nr_blocks, int decrypt) {
        return _e.invoke(::crypt_buffer_inplace, slba, buf, nr_blocks, decrypt);
    }

    inline long crypt_command_inplace(const struct nvme_command *cmd, int decrypt) {
        return _e.invoke(
            ::crypt_command_inplace,
            _pvm,
            _pvm_size,
            reinterpret_cast<const nvme_command_alias *>(cmd),
            decrypt);
    }

    inline long crypt_command(const struct nvme_command *cmd, unsigned char *outbuf, size_t outbuf_size, int decrypt) {
        return _e.invoke(
            ::crypt_command,
            _pvm,
            _pvm_size,
            reinterpret_cast<const nvme_command_alias *>(cmd),
            outbuf,
            outbuf_size,
            decrypt);
    }

private:
    sgx_enclave _e;
    unsigned char *_pvm;
    size_t _pvm_size;
};

#pragma once

#include <memory>
#include <limits>
#include <functional>

#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/err.h>

#include "util.hpp"
#include "crypto/tbc.hpp"

class aes_xts_libcrypto final : public tweakable_block_cipher {
    static_assert(std::numeric_limits<unsigned char>::digits == 8);

public:
    explicit aes_xts_libcrypto(std::span<const unsigned char> key);
    aes_xts_libcrypto(const aes_xts_libcrypto &) = delete;
    aes_xts_libcrypto &operator=(const aes_xts_libcrypto &) = delete;
    aes_xts_libcrypto(aes_xts_libcrypto &&) = default;
    aes_xts_libcrypto &operator=(aes_xts_libcrypto &&) = default;
    ~aes_xts_libcrypto() = default;

    bool encrypt(std::span<unsigned char> out, std::span<const unsigned char> data, uint64_t lba) override;
    bool decrypt(std::span<unsigned char> out, std::span<const unsigned char> data, uint64_t lba) override;

private:
    unique_handle<EVP_CIPHER_CTX> enc;
    unique_handle<EVP_CIPHER_CTX> dec;
    union {
        std::array<uint64_t, 2> lba;
        std::array<unsigned char, 16> tweak;
    } iv;
};

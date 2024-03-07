#include <stdexcept>
#include <algorithm>

#include "crypto/aes_xts_libcrypto.hpp"

constexpr size_t xts_max_bytes = 1 << 20;

aes_xts_libcrypto::aes_xts_libcrypto(std::span<const unsigned char> key) : tweakable_block_cipher(), iv{{0, 0}} {
    auto evp = EVP_aes_128_xts();
    if (EVP_CIPHER_iv_length(evp) != sizeof(iv.tweak)) {
        throw std::runtime_error("unexpected: invalid iv size");
    }
    if (key.size() != (size_t)EVP_CIPHER_key_length(evp)) {
        throw std::length_error("invalid key size");
    }

    enc = unique_handle<EVP_CIPHER_CTX>(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!enc) {
        throw std::runtime_error("cannot create openssl context");
    }
    if (!EVP_EncryptInit_ex(enc.get(), evp, nullptr, key.data(), nullptr)) {
        throw std::runtime_error("cannot init EVP engine");
    }
    if (!EVP_CIPHER_CTX_set_padding(enc.get(), 0)) {
        throw std::runtime_error("cannot setup padding");
    }

    dec = unique_handle<EVP_CIPHER_CTX>(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!dec) {
        throw std::runtime_error("cannot create openssl context");
    }
    if (!EVP_DecryptInit_ex(dec.get(), evp, nullptr, key.data(), nullptr)) {
        throw std::runtime_error("cannot init EVP engine");
    }
    if (!EVP_CIPHER_CTX_set_padding(dec.get(), 0)) {
        throw std::runtime_error("cannot setup padding");
    }
}

bool aes_xts_libcrypto::encrypt(std::span<unsigned char> out, std::span<const unsigned char> data, uint64_t lba) {
    if (!enc || out.size() < data.size() || data.size() > xts_max_bytes) {
        return false;
    }
    iv.lba[0] = lba;
    iv.lba[1] = 0;
    if (!EVP_EncryptInit_ex(enc.get(), nullptr, nullptr, nullptr, iv.tweak.data())) {
        return false;
    }
    int outl = data.size();
    if (!EVP_EncryptUpdate(enc.get(), out.data(), &outl, data.data(), outl)) {
        return false;
    }
    if (!EVP_EncryptFinal_ex(enc.get(), nullptr, &outl)) {
        return false;
    }
    return true;
}

bool aes_xts_libcrypto::decrypt(std::span<unsigned char> out, std::span<const unsigned char> data, uint64_t lba) {
    if (!dec || out.size() < data.size() || data.size() > xts_max_bytes) {
        return false;
    }
    iv.lba[0] = lba;
    iv.lba[1] = 0;
    if (!EVP_DecryptInit_ex(dec.get(), nullptr, nullptr, nullptr, iv.tweak.data())) {
        return false;
    }
    int outl = data.size();
    if (!EVP_DecryptUpdate(dec.get(), out.data(), &outl, data.data(), outl)) {
        return false;
    }
    if (!EVP_DecryptFinal_ex(dec.get(), nullptr, &outl)) {
        return false;
    }
    return true;
}

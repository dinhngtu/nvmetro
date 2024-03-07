#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

class tweakable_block_cipher {
public:
    tweakable_block_cipher(const tweakable_block_cipher &) = delete;
    tweakable_block_cipher &operator=(const tweakable_block_cipher &) = delete;
    tweakable_block_cipher(tweakable_block_cipher &&) = default;
    tweakable_block_cipher &operator=(tweakable_block_cipher &&) = default;
    virtual ~tweakable_block_cipher() = default;

    virtual bool encrypt(std::span<unsigned char> out, std::span<const unsigned char> data, uint64_t lba) = 0;
    virtual bool encrypt(std::span<unsigned char> data, uint64_t lba) {
        return encrypt(data, data, lba);
    }

    virtual bool decrypt(std::span<unsigned char> out, std::span<const unsigned char> data, uint64_t lba) = 0;
    virtual bool decrypt(std::span<unsigned char> data, uint64_t lba) {
        return decrypt(data, data, lba);
    }

protected:
    tweakable_block_cipher() = default;
};

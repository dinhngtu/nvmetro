#pragma once

#include <memory>
#include <cstdlib>
#include <cstddef>
#include <limits>

#include <ippcp.h>
#include <string>
#include <system_error>

#include "util.hpp"
#include "crypto/tbc.hpp"

class aes_xts_ipp final : public tweakable_block_cipher {
    static_assert(std::numeric_limits<unsigned char>::digits == 8);

public:
    explicit aes_xts_ipp(std::span<const unsigned char> key, size_t block_size);
    aes_xts_ipp(const aes_xts_ipp &) = delete;
    aes_xts_ipp &operator=(const aes_xts_ipp &) = delete;
    aes_xts_ipp(aes_xts_ipp &&) = default;
    aes_xts_ipp &operator=(aes_xts_ipp &&) = default;
    ~aes_xts_ipp() = default;

    bool encrypt(std::span<unsigned char> out, std::span<const unsigned char> data, uint64_t lba) override;
    bool decrypt(std::span<unsigned char> out, std::span<const unsigned char> data, uint64_t lba) override;

private:
    size_t _bs;
    union {
        std::array<uint64_t, 2> lba;
        std::array<unsigned char, 16> tweak;
    } iv;
    unique_handle<IppsAES_XTSSpec> ctx;
};

class ippcp_category final : public std::error_category {
public:
    constexpr explicit ippcp_category() noexcept {
    }
    ippcp_category(const ippcp_category &) = delete;
    ippcp_category &operator=(const ippcp_category &) = delete;
    ippcp_category(ippcp_category &&) = delete;
    ippcp_category &operator=(ippcp_category &&) = delete;
    ~ippcp_category() = default;

    const char *name() const noexcept override {
        return "ippcp";
    }

    std::string message(int condition) const override {
        auto ret = ippcpGetStatusString(condition);
        if (!ret) {
            return "catastrophic failure";
        }
        return std::string(ret);
    }
};

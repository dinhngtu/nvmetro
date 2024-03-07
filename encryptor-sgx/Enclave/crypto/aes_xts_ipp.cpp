#include <stdexcept>
#include <algorithm>
#include <system_error>

#include "crypto/aes_xts_ipp.hpp"
#include "nvme_core.hpp"

constexpr size_t xts_max_bytes = 1 << 20;

static void ctx_free(IppsAES_XTSSpec *ctx) {
    free(ctx);
}

static unique_handle<IppsAES_XTSSpec> ctx_create(std::span<const unsigned char> key, size_t block_size) {
    int ret = ippStsNoErr;
    int ctxsz = 0;

    if (block_size > xts_max_bytes) {
        throw std::out_of_range("xts block size too large");
    }
    ret = ippsAES_XTSGetSize(&ctxsz);
    if (ret != ippStsNoErr) {
        throw std::system_error(ret, ippcp_category(), "cannot get xts context size");
    }

    auto ctx = unique_handle<IppsAES_XTSSpec>(static_cast<IppsAES_XTSSpec *>(calloc(1, ctxsz)), ctx_free);
    if (!ctx) {
        throw std::bad_alloc();
    }
    ret = ippsAES_XTSInit(
        key.data(),
        (int)key.size() * std::numeric_limits<unsigned char>::digits,
        (int)block_size * std::numeric_limits<unsigned char>::digits,
        ctx.get(),
        ctxsz);
    if (ret != ippStsNoErr) {
        throw std::system_error(ret, ippcp_category(), "cannot init xts engine");
    }
    return ctx;
}

aes_xts_ipp::aes_xts_ipp(std::span<const unsigned char> key, size_t block_size)
    : tweakable_block_cipher(), _bs(block_size), iv{{0, 0}}, ctx(ctx_create(key, block_size)) {
}

bool aes_xts_ipp::encrypt(std::span<unsigned char> out, std::span<const unsigned char> data, uint64_t lba) {
    if (!ctx || out.size() < data.size() || data.size() != _bs) {
        return false;
    }
    iv.lba[0] = lba;
    iv.lba[1] = 0;
    return ippsAES_XTSEncrypt(
               data.data(),
               out.data(),
               (int)data.size() * std::numeric_limits<unsigned char>::digits,
               ctx.get(),
               iv.tweak.data(),
               0) == ippStsNoErr;
}

bool aes_xts_ipp::decrypt(std::span<unsigned char> out, std::span<const unsigned char> data, uint64_t lba) {
    if (!ctx || out.size() < data.size() || data.size() != _bs) {
        return false;
    }
    iv.lba[0] = lba;
    iv.lba[1] = 0;
    return ippsAES_XTSDecrypt(
               data.data(),
               out.data(),
               (int)data.size() * std::numeric_limits<unsigned char>::digits,
               ctx.get(),
               iv.tweak.data(),
               0) == ippStsNoErr;
}

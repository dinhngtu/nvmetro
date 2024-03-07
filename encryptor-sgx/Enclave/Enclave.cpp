#include "Enclave.hpp"
#include "Enclave_t.h"
#include "sgx_trts.h"

#include <vector>
#include <span>
#include <atomic>
#include <mutex>
#include "crypto/tbc.hpp"
#include "crypto/aes_xts_ipp.hpp"
#include "cmdbuf_en.hpp"
#include "nvme_en.hpp"

struct nvme_command_alias;

static std::unique_ptr<tweakable_block_cipher> engine;
static std::atomic<int> g_lba_shift(-1);
static std::mutex g_wrlock;

int put_key(unsigned char key[32], int lba_shift) {
    std::lock_guard<std::mutex> lock(g_wrlock);
    if (g_lba_shift.load(std::memory_order_acquire) >= 0) {
        return -EPERM;
    }
    engine = std::make_unique<aes_xts_ipp>(std::span<unsigned char>(key, 32), 1 << lba_shift);
    g_lba_shift.exchange(lba_shift, std::memory_order_release);
    return 0;
}

long crypt_buffer_inplace(size_t slba, unsigned char *buf, size_t nr_blocks, int decrypt) {
    auto lbas = g_lba_shift.load(std::memory_order_acquire);
    if (lbas < 0) {
        return -EPERM;
    }
    std::span<unsigned char> encbuf(buf, nr_blocks << lbas);
    if (!sgx_is_outside_enclave(encbuf.data(), encbuf.size())) {
        return -EFAULT;
    }
    try {
        long bytes_done = 0;
        for (size_t eli = 0; eli < nr_blocks; eli++) {
            auto lba = slba + eli;
            auto src = encbuf.subspan(eli << lbas, 1 << lbas);
            bool success = decrypt ? engine->decrypt(src, src, lba) : engine->encrypt(src, src, lba);
            if (!success) {
                return -EIO;
            }
            bytes_done += src.size();
        }
        return bytes_done;
    } catch (const std::contract_violation_error &) {
        return -EFAULT;
    }
}

long crypt_command_inplace(unsigned char *pvm, size_t pvm_size, const struct nvme_command_alias *_cmd, int decrypt) {
    auto lbas = g_lba_shift.load(std::memory_order_acquire);
    if (lbas < 0) {
        return -EPERM;
    }
    if (!sgx_is_outside_enclave(pvm, pvm_size)) {
        return -EFAULT;
    }
    try {
        auto cmd = reinterpret_cast<const struct nvme_command *>(_cmd);
        auto vm = std::make_shared<mapping>(pvm, pvm_size);
        long bytes_done = 0;
        for (auto it = nvme_cmd_lba_iter_en(vm, *cmd, lbas); !it.at_end(); it++) {
            auto src = *it;
            bool success = decrypt ? engine->decrypt(src, src, it.lba()) : engine->encrypt(src, src, it.lba());
            if (!success) {
                return -EIO;
            }
            bytes_done += src.size();
        }
        return bytes_done;
    } catch (const std::contract_violation_error &e) {
        return -EFAULT;
    }
}

long crypt_command(
    unsigned char *pvm,
    size_t pvm_size,
    const struct nvme_command_alias *_cmd,
    unsigned char *outbuf,
    size_t outbuf_size,
    int decrypt) {
    auto lbas = g_lba_shift.load(std::memory_order_acquire);
    if (lbas < 0) {
        return -EPERM;
    }
    if (!sgx_is_outside_enclave(pvm, pvm_size) || !sgx_is_outside_enclave(outbuf, outbuf_size)) {
        return -EFAULT;
    }
    try {
        auto cmd = reinterpret_cast<const struct nvme_command *>(_cmd);
        auto vm = std::make_shared<mapping>(pvm, pvm_size);
        std::span<unsigned char> outspan(outbuf, outbuf_size);
        for (auto it = nvme_cmd_lba_iter_en(vm, *cmd, lbas); !it.at_end(); it++) {
            auto src = *it;
            if (outspan.size() < src.size()) {
                break;
            }
            bool success = decrypt ? engine->decrypt(outspan, src, it.lba()) : engine->encrypt(outspan, src, it.lba());
            if (!success) {
                return -EIO;
            }
            outspan = outspan.subspan(src.size());
        }
        return outbuf_size - outspan.size();
    } catch (const std::contract_violation_error &) {
        return -EFAULT;
    }
}

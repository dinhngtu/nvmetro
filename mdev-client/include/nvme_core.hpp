#pragma once

#include <cstddef>

extern "C" {
#include <linux/uuid.h>
#define uuid_t guid_t                          // NOLINT(cppcoreguidelines-macro-usage)
#define likely(x) __builtin_expect(!!(x), 1)   // NOLINT(cppcoreguidelines-macro-usage)
#define unlikely(x) __builtin_expect(!!(x), 0) // NOLINT(cppcoreguidelines-macro-usage)
#include "linux/nvme.h"
#include <linux/nvme_mdev.h>
}

constexpr size_t NVME_PAGE_SHIFT = 12;
constexpr size_t NVME_PAGE_SIZE = 1 << NVME_PAGE_SHIFT;

struct hmb_dl_entry {
    uint64_t badd;
    uint32_t bpages;
    uint32_t _rsvd;
} __attribute__((packed));

#pragma once

#include <cstdint>
#include "xcow/xcow_util.hpp"

namespace xcow {

// STRONG_TYPEDEF(uint64_t, RcRef);         // NOLINT
STRONG_TYPEDEF_I(uint64_t, SnapListRef, 0);   // NOLINT
STRONG_TYPEDEF_I(uint64_t, FreeListRef, 0);   // NOLINT
STRONG_TYPEDEF_I(uint64_t, FreeListEntry, 0); // NOLINT
STRONG_TYPEDEF_I(uint64_t, XlateRef, 0);      // NOLINT
STRONG_TYPEDEF_I(uint64_t, XlateLeaf, 0);     // NOLINT

static constexpr uint64_t XCOW_MAGIC = 0x306f7274654d564e;

struct XcowHeader {
    uint64_t magic;
    uint64_t fsize;
    uint32_t cluster_bits;
    uint32_t _rsvd;
    uint32_t hwm_limit;
    uint32_t hwm;
    uint64_t disk_hwm_limit;
    SnapListRef snaplist;
    FreeListRef freelist;
};

struct SnapListEntry {
    static constexpr bool strong_typedef = true;
    using backing_type = uint64_t;
    uint64_t val;
    uint64_t disk_hwm;
    constexpr explicit SnapListEntry(const uint64_t &t_, const uint64_t &h_) noexcept : val(t_), disk_hwm(h_) {
    }
    constexpr SnapListEntry() noexcept : val(0), disk_hwm(0) {
    }
    constexpr SnapListEntry(const SnapListEntry &t_) noexcept : val(t_.val), disk_hwm(t_.disk_hwm) {
    }
    constexpr SnapListEntry &operator=(const SnapListEntry &rhs) noexcept {
        val = rhs.val;
        disk_hwm = rhs.disk_hwm;
        return *this;
    }
    constexpr bool operator==(const SnapListEntry &rhs) const noexcept {
        return val == rhs.val && disk_hwm == rhs.disk_hwm;
    }
};

} // namespace xcow

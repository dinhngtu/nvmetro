#pragma once

#include <cassert>
#include <span>
#include <sys/uio.h>
#include "xcow/xcowfmt.hpp"
#include "xcow/nested_table.hpp"

namespace xcow {

namespace XlateBits {
static constexpr uint64_t valid = 0x8000'0000'0000'0000ull;
static constexpr uint64_t writable = 0x2;
static constexpr uint64_t decode_mask = ~(valid | writable);

static constexpr XlateRef encode_ref(uint64_t off, bool is_writable) {
    return XlateRef(off | valid | (is_writable ? writable : 0));
}
static constexpr XlateLeaf encode_leaf(uint64_t off, bool is_writable) {
    return XlateLeaf(off | valid | (is_writable ? writable : 0));
}
static constexpr uint64_t decode_leaf(XlateLeaf off) {
    assert(off & valid);
    return off & decode_mask;
}
static constexpr uint64_t decode_leaf_unsafe(XlateLeaf off) {
    return off & decode_mask;
}

#if XCOW_ZEROPAGE
static constexpr XlateLeaf empty_leaf = XlateLeaf(valid | 0);

static constexpr bool is_empty(XlateLeaf tl) {
    return !(tl & valid) || tl == empty_leaf;
}
#else
static constexpr XlateLeaf empty_leaf = XlateLeaf();

static constexpr bool is_empty(XlateLeaf tl) {
    return !(tl & valid);
}

static constexpr bool needs_cow(XlateLeaf prev) {
    return !is_empty(prev) && !(prev & writable);
}

static constexpr bool needs_alloc(XlateLeaf tl) {
    return is_empty(tl) || needs_cow(tl);
}
#endif
}; // namespace XlateBits

template <>
struct NTRefImpl<XlateRef> {
    static constexpr uint64_t decode(XlateRef t) {
        assert(t & XlateBits::valid);
        return t & XlateBits::decode_mask;
    }
    static constexpr bool valid(XlateRef t) {
        return !!(t & XlateBits::valid);
    }
};
using XlateTable = NestedTable<XlateRef, XlateLeaf, 1>;

struct disk_hwm_exception : public std::exception {};

class XcowFile;

class XcowSnap {
public:
    XcowSnap(const XcowSnap &) = delete;
    XcowSnap &operator=(const XcowSnap &) = delete;
    XcowSnap(XcowSnap &&) = default;
    XcowSnap &operator=(XcowSnap &&) = default;
    ~XcowSnap() = default;

    XlateLeaf translate_read(uint64_t addr_in) const;
    XlateLeaf translate_write(uint64_t addr_in, XlateLeaf *prev = nullptr);
    void erase(uint64_t addr_in);
    XlateLeaf *tx_write_prep(uint64_t addr_in);
    XlateLeaf tx_write_commit(XlateLeaf &ref, XlateLeaf tl, XlateLeaf *prev);

    constexpr const XcowFile &file() {
        return *_f;
    }

    constexpr uint64_t disk_hwm() {
        return *_disk_hwm;
    }

    std::pair<uint64_t, size_t> alloc_data_clusters(size_t count);
    inline std::pair<uint64_t, size_t> alloc_data_cluster() {
        return alloc_data_clusters(1);
    }

    friend class XcowFile;

private:
    explicit XcowSnap(XcowFile *f, XlateTable root, uint64_t *disk_hwm)
        : _f(f), _root(std::move(root)), _disk_hwm(disk_hwm) {
    }

    XcowFile *_f;
    XlateTable _root;
    uint64_t *_disk_hwm;
};

} // namespace xcow

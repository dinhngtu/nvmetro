#pragma once

#include <cassert>
#include <span>
#include <memory>
#include <utility>
#include <exception>
#include <sys/uio.h>
#include "xcow/xcowfmt.hpp"
#include "xcow/linked_table.hpp"
#include "xcow/xcow_snap.hpp"

namespace xcow {

namespace FileBits {
static constexpr uint64_t valid = 0x8000'0000'0000'0000ull;
static constexpr uint64_t decode_mask = ~(valid);

static constexpr SnapListRef encode_snap_ref(uint64_t off) {
    return SnapListRef(off | valid);
}
static constexpr SnapListEntry encode_snap_entry(uint64_t off, uint64_t disk_hwm) {
    return SnapListEntry(off | valid, disk_hwm);
}
static constexpr uint64_t decode_snap_entry(SnapListEntry t) {
    assert(t.val & valid);
    return t.val & decode_mask;
}

static constexpr FreeListRef encode_free_ref(uint64_t off) {
    return FreeListRef(off | valid);
}
static constexpr FreeListEntry encode_free_entry(uint64_t off) {
    return FreeListEntry(off | valid);
}
static constexpr uint64_t decode_free_entry(FreeListEntry t) {
    assert(t & valid);
    return t & decode_mask;
}
}; // namespace FileBits

template <>
struct LTRefImpl<FreeListRef> {
    static constexpr uint64_t decode(FreeListRef t) {
        assert(t & FileBits::valid);
        return t & FileBits::decode_mask;
    }
    static constexpr bool valid(FreeListRef t) {
        return !!(t & FileBits::valid);
    }
};
using FreeList = LinkedTable<FreeListRef, FreeListEntry>;

template <>
struct LTRefImpl<SnapListRef> {
    static constexpr uint64_t decode(SnapListRef t) {
        assert(t & FileBits::valid);
        return t & FileBits::decode_mask;
    }
    static constexpr bool valid(SnapListRef t) {
        return !!(t & FileBits::valid);
    }
};
using SnapList = LinkedTable<SnapListRef, SnapListEntry>;

struct hwm_exception : public std::exception {};

class XcowFile {
public:
    explicit XcowFile(Deref *deref);
    XcowFile(const XcowFile &) = delete;
    XcowFile &operator=(const XcowFile &) = delete;
    XcowFile(XcowFile &&) = default;
    XcowFile &operator=(XcowFile &&) = default;
    ~XcowFile() = default;

    static XcowFile format(Deref *deref, size_t fsize, uint32_t cbits, uint32_t hwm_limit, uint64_t disk_hwm_limit);

    constexpr size_t cluster_bits() const {
        return _hdr->cluster_bits;
    }

    constexpr size_t cluster_size() const {
        return 1 << _hdr->cluster_bits;
    }

    constexpr size_t fsize() const {
        return _hdr->fsize;
    }

    constexpr SnapList::iterator snaps() {
        return SnapList::iterator(_deref_meta, _snaps);
    }

    friend class XcowSnap;

    XcowSnap open_read(int snapi);
    XcowSnap open_write();
    XcowSnap snap_create(XcowSnap &source);
    void peel(XcowSnap &&source);

#if XCOW_TRANSPARENT
public:
#else
private:
#endif
    explicit constexpr XcowFile(Deref *deref_meta, XcowHeader *hdr, SnapList snaps, FreeList flist)
        : _deref_meta(deref_meta), _hdr(hdr), _snaps(snaps), _flist(flist) {
    }

    constexpr size_t l0_cover_bits() const {
        return cluster_bits() * 2 - 3;
    }

    constexpr size_t l0_cover_size() const {
        return 1 << l0_cover_bits();
    }

    constexpr size_t l1_clusters() const {
        auto l0count = (_hdr->fsize + l0_cover_size() - 1) >> l0_cover_bits();
        return (l0count * sizeof(XlateRef) + cluster_size() - 1) >> cluster_bits();
    }

    std::pair<uint64_t, size_t> alloc_meta_clusters(size_t count) {
        if (_hdr->hwm + count > _hdr->hwm_limit)
            throw hwm_exception{};
        auto ret = std::make_pair(_hdr->hwm << cluster_bits(), count << cluster_bits());
        _hdr->hwm += count;
        return ret;
    }
    inline std::pair<uint64_t, size_t> alloc_meta_cluster() {
        return alloc_meta_clusters(1);
    }

    Deref *_deref_meta;
    XcowHeader *_hdr;
    SnapList _snaps;
    FreeList _flist;
};

} // namespace xcow

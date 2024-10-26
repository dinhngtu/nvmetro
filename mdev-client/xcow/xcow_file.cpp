#include <algorithm>
#include <stdexcept>
#include <utility>
#include "xcow/xcow_file.hpp"
#include "xcow/xcowfmt.hpp"
#include "xcow/xcow_snap.hpp"

using namespace xcow::FileBits;

static xcow::XcowHeader *make_hdr(xcow::Deref &deref) {
    auto p = deref.deref_as<xcow::XcowHeader>(0, sizeof(xcow::XcowHeader)).data();
    if (p->magic != xcow::XCOW_MAGIC)
        throw std::runtime_error("invalid snap magic");
    if (p->cluster_bits < 12 || p->cluster_bits > 24)
        throw std::runtime_error("unsupported cluster size");
    return p;
}

xcow::XcowFile::XcowFile(xcow::Deref *deref)
    : _deref_meta(deref), _hdr(make_hdr(*_deref_meta)),
      //_rt(_deref_meta->deref(_hdr->rcl1_offset, _hdr->rcl1_entries * sizeof(RcRef))),
      _snaps(_deref_meta->deref(LTRefImpl<SnapListRef>::decode(_hdr->snaplist), cluster_size())),
      _flist(_deref_meta->deref(LTRefImpl<FreeListRef>::decode(_hdr->freelist), cluster_size())) {
    _deref_meta->prefetch(0, static_cast<size_t>(_hdr->hwm) << cluster_size());
}

xcow::XcowFile xcow::XcowFile::format(
    Deref *deref,
    size_t fsize,
    uint32_t cbits,
    uint32_t hwm_limit,
    uint64_t disk_hwm_limit) {
    size_t csize = 1 << cbits;
    uint64_t hwm = 0;

    auto hdrbytes = deref->deref((hwm++) << cbits, csize);
    std::fill(hdrbytes.begin(), hdrbytes.end(), 0);
    auto hdr = std::launder(reinterpret_cast<XcowHeader *>(hdrbytes.data()));
    hdr->magic = XCOW_MAGIC;
    hdr->fsize = fsize;
    hdr->cluster_bits = cbits;
    hdr->_rsvd = 0;
    hdr->hwm_limit = hwm_limit;
    hdr->disk_hwm_limit = disk_hwm_limit;

    auto snapoff = (hwm++) << cbits;
    auto snaplist = SnapList::format(deref->deref(snapoff, csize));
    hdr->snaplist = encode_snap_ref(snapoff);

    auto freeoff = (hwm++) << cbits;
    auto freelist = FreeList::format(deref->deref(freeoff, csize));
    hdr->freelist = encode_free_ref(freeoff);

    hdr->hwm = hwm;

    XcowFile f(deref, hdr, snaplist, freelist);
    auto [l1_off, l1_nbytes] = f.alloc_meta_clusters(f.l1_clusters());
    XlateTable s0(deref->deref(l1_off, l1_nbytes));
    std::fill(s0.entries().begin(), s0.entries().end(), XlateRef{});
    // skip the first zero block
    f._snaps.push_back(encode_snap_entry(l1_off, 1));

    return f;
}

xcow::XcowSnap xcow::XcowFile::open_read(int snapi) {
    std::optional<SnapList> t = _snaps;
    auto sei = t->active_entries().rbegin();
    while (t.has_value() && snapi > 0) {
        if (sei == t->active_entries().rend()) {
            t = _snaps.next(*_deref_meta);
            sei = t->active_entries().rbegin();
            continue;
        }
        sei++;
        snapi--;
    }
    XlateTable root(_deref_meta->deref(decode_snap_entry(*sei), cluster_size()));
    return XcowSnap(this, root, nullptr);
}

xcow::XcowSnap xcow::XcowFile::open_write() {
    auto &se = _snaps.active_entries().back();
    XlateTable root(_deref_meta->deref(decode_snap_entry(se), cluster_size()));
    return XcowSnap(this, root, &se.disk_hwm);
}

xcow::XcowSnap xcow::XcowFile::snap_create(xcow::XcowSnap &source) {
    auto [l1_off, l1_nbytes] = alloc_meta_clusters(l1_clusters());
    XlateTable snaproot(_deref_meta->deref(l1_off, l1_nbytes));

    auto oit = snaproot.entries().begin();
    for (auto it = source._root.entries().begin(); it != source._root.entries().end(); it++) {
        *oit = XlateRef(it->val & ~XlateBits::writable);
        oit++;
    }

    auto last_hwm = _snaps.active_entries().back().disk_hwm;
    auto res = _snaps.push_back(encode_snap_entry(l1_off, last_hwm));
    if (!res) {
        auto [st_off, st_nbytes] = alloc_meta_cluster();
        SnapList stnext(_deref_meta->deref(st_off, st_nbytes));
        stnext.next_ref() = _hdr->snaplist;
        _hdr->snaplist = encode_snap_ref(st_off);
        _snaps = stnext;
        _snaps.push_back(encode_snap_entry(l1_off, last_hwm));
    }
    source._disk_hwm = nullptr;
    return XcowSnap(this, snaproot, &_snaps.active_entries().back().disk_hwm);
}

void xcow::XcowFile::peel(xcow::XcowSnap &&source) {
    if (!source._disk_hwm)
        throw std::logic_error("snapshot is not writable, cannot peel");
    if (_snaps.active_entries().size() == 1 && !(_snaps.next_ref() & FileBits::valid))
        throw std::logic_error("cannot peel last snapshot");
    if (_snaps.pop_back()) {
        _hdr->snaplist = _snaps.next_ref();
        _snaps = *_snaps.next(*_deref_meta);
    }
}

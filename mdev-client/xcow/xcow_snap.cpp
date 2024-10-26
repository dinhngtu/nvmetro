#include <algorithm>
#include <span>
#include <stdexcept>
#include "xcow/xcowfmt.hpp"
#include "xcow/xcow_snap.hpp"
#include "xcow/xcow_file.hpp"

xcow::XlateLeaf xcow::XcowSnap::translate_read(uint64_t addr_in) const {
    auto l1_off = addr_in >> _f->l0_cover_bits();
    auto l0 = _root.next(*_f->_deref_meta, l1_off, _f->cluster_size());
    if (!l0.has_value())
        return XlateBits::empty_leaf;
    auto l0_off = (addr_in & (_f->l0_cover_size() - 1)) >> _f->cluster_bits();
    return (*l0)[l0_off];
}

xcow::XlateLeaf xcow::XcowSnap::translate_write(uint64_t addr_in, xcow::XlateLeaf *prev) {
    if (!_disk_hwm) {
        throw std::logic_error("snapshot is not writable");
    }
    auto l1_off = addr_in >> _f->l0_cover_bits();
    auto l0 = _root.next(*_f->_deref_meta, l1_off, _f->cluster_size());
    if (!l0.has_value()) {
        auto [newmeta_off, newmeta_nbytes] = _f->alloc_meta_cluster();
        l0 = XlateTable::next_type(_f->_deref_meta->deref(newmeta_off, newmeta_nbytes));
        std::fill(l0->entries().begin(), l0->entries().end(), XlateBits::empty_leaf);
        auto entry = XlateBits::encode_ref(newmeta_off, true);
        _root[l1_off] = entry;
    } else if (!(_root[l1_off] & XlateBits::writable)) {
        auto [newmeta_off, newmeta_nbytes] = _f->alloc_meta_cluster();
        auto new_l0 = XlateTable::next_type(_f->_deref_meta->deref(newmeta_off, newmeta_nbytes));

        auto oit = new_l0.entries().begin();
        for (auto it = l0->entries().begin(); it != l0->entries().end(); it++) {
            *oit = XlateLeaf((*it) & ~XlateBits::writable);
            oit++;
        }

        auto entry = XlateBits::encode_ref(newmeta_off, true);
        _root[l1_off] = entry;
        l0 = new_l0;
    }
    auto l0_off = (addr_in & (_f->l0_cover_size() - 1)) >> _f->cluster_bits();
    auto &cluster_entry = (*l0)[l0_off];
    XlateLeaf _prev = cluster_entry;
    if (!(cluster_entry & XlateBits::valid) || !(cluster_entry & XlateBits::writable)) {
        auto [newdata_off, newdata_nbytes] = alloc_data_cluster();
        _prev = std::exchange(cluster_entry, XlateBits::encode_leaf(newdata_off, true));
    }
    if (prev)
        *prev = _prev;
    return cluster_entry;
}

xcow::XlateLeaf *xcow::XcowSnap::tx_write_prep(uint64_t addr_in) {
    if (!_disk_hwm) {
        throw std::logic_error("snapshot is not writable");
    }
    auto l1_off = addr_in >> _f->l0_cover_bits();
    auto l0 = _root.next(*_f->_deref_meta, l1_off, _f->cluster_size());
    if (!l0.has_value()) {
        auto [newmeta_off, newmeta_nbytes] = _f->alloc_meta_cluster();
        l0 = XlateTable::next_type(_f->_deref_meta->deref(newmeta_off, newmeta_nbytes));
        std::fill(l0->entries().begin(), l0->entries().end(), XlateBits::empty_leaf);
        auto entry = XlateBits::encode_ref(newmeta_off, true);
        _root[l1_off] = entry;
    } else if (!(_root[l1_off] & XlateBits::writable)) {
        auto [newmeta_off, newmeta_nbytes] = _f->alloc_meta_cluster();
        auto new_l0 = XlateTable::next_type(_f->_deref_meta->deref(newmeta_off, newmeta_nbytes));

        auto oit = new_l0.entries().begin();
        for (auto it = l0->entries().begin(); it != l0->entries().end(); it++) {
            *oit = XlateLeaf((*it) & ~XlateBits::writable);
            oit++;
        }

        auto entry = XlateBits::encode_ref(newmeta_off, true);
        _root[l1_off] = entry;
        l0 = new_l0;
    }
    auto l0_off = (addr_in & (_f->l0_cover_size() - 1)) >> _f->cluster_bits();
    return &(*l0)[l0_off];
}

xcow::XlateLeaf xcow::XcowSnap::tx_write_commit(xcow::XlateLeaf &ref, xcow::XlateLeaf tl, xcow::XlateLeaf *prev) {
    XlateLeaf _prev = ref;
    if (!(ref & XlateBits::valid) || !(ref & XlateBits::writable))
        ref = tl;
    if (prev)
        *prev = _prev;
    return ref;
}

void xcow::XcowSnap::erase(uint64_t addr_in) {
    auto l1_off = addr_in >> _f->l0_cover_bits();
    auto l0 = _root.next(*_f->_deref_meta, l1_off, _f->cluster_size());
    if (!l0.has_value())
        return;
    auto l0_off = (addr_in & (_f->l0_cover_size() - 1)) >> _f->cluster_bits();
    auto &cluster_entry = (*l0)[l0_off];
    if (!XlateBits::is_empty(cluster_entry)) {
        // TODO: free disk block
    }
    cluster_entry = XlateBits::empty_leaf;
    if (std::all_of(l0->entries().begin(), l0->entries().end(), XlateBits::is_empty)) {
        // TODO: free l0 meta block
    }
}

std::pair<uint64_t, size_t> xcow::XcowSnap::alloc_data_clusters(size_t count) {
    if (*_disk_hwm + count > _f->_hdr->disk_hwm_limit)
        throw disk_hwm_exception{};
    auto ret = std::make_pair(*_disk_hwm << _f->cluster_bits(), count << _f->cluster_bits());
    *_disk_hwm += count;
    return ret;
}

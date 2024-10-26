#include <cstdint>
#include <cassert>
#include <array>
#include "nvme_core.hpp"
#include "linux/bpf_nvme_mdev.h"

using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s32 = int32_t;

static constexpr u64 U64_MAX = UINT64_MAX;
static constexpr u64 BPF_ANY = 0;

template <class T, size_t N>
T *bpf_map_lookup_elem(std::array<T, N> *a, u32 *i) {
    assert(a && i && *i < N);
    return &(*a)[*i];
}

template <class T, size_t N>
long bpf_map_update_elem(std::array<T, N> *a, u32 *i, T *val, u64 flags) {
    assert(a && i && *i < N && val);
    (*a)[*i] = *val;
    return 0;
}

template <class T, size_t N>
long bpf_arraymap_elem_band(std::array<T, N> *a, T *val) {
    assert(a && val);
    for (auto it = a->begin(); it != a->end(); it++)
        *it &= *val;
    return N;
}

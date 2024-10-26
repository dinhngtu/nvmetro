#pragma once

#include <cstddef>
#include <span>
#include <optional>
#include <type_traits>
#include "xcow/deref.hpp"

namespace xcow {

template <typename RefType>
struct NTRefImpl {};

template <typename RefType>
concept NTRef = requires(RefType a) {
    { NTRefImpl<RefType>::decode(a) } -> std::same_as<typename RefType::backing_type>;
    { NTRefImpl<RefType>::valid(a) } -> std::convertible_to<bool>;
};

template <NTRef RefType, typename LeafType, size_t level>
struct NTEntry {
    using type = RefType;
};

template <NTRef RefType, typename LeafType>
struct NTEntry<RefType, LeafType, 0> {
    using type = LeafType;
};

template <NTRef RefType, typename LeafType, size_t level>
class NestedTable {
public:
    using entry_type = typename NTEntry<RefType, LeafType, level>::type;
    using next_type = NestedTable<RefType, LeafType, level - 1>;

    explicit constexpr NestedTable(std::span<uint8_t> s) : _s(s) {
        _entries = convert_span<entry_type>(_s.subspan(0, _s.size_bytes()));
    }

    constexpr entry_type &operator[](size_t i) {
        return _entries[i];
    }

    constexpr const entry_type &operator[](size_t i) const {
        return _entries[i];
    }

    std::optional<next_type> next(Deref &deref, size_t i, size_t next_nbytes) const {
        static_assert(level > 0);
        if (!NTRefImpl<RefType>::valid(_entries[i])) {
            return {};
        }
        auto decoded = NTRefImpl<RefType>::decode(_entries[i]);
        return next_type(deref.deref(decoded, next_nbytes));
    }

    std::span<entry_type> &entries() {
        return _entries;
    }

    const std::span<entry_type> &entries() const {
        return _entries;
    }

private:
    std::span<uint8_t> _s;
    std::span<entry_type> _entries;
};

} // namespace xcow

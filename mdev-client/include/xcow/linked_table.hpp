#pragma once

#include <cstdint>
#include <span>
#include <optional>
#include <new>
#include <algorithm>
#include <iterator>
#include "xcow/deref.hpp"

namespace xcow {

template <typename RefType>
struct LTRefImpl {};

template <typename RefType>
concept LTRef = requires(RefType a) {
    { LTRefImpl<RefType>::decode(a) } -> std::same_as<typename RefType::backing_type>;
    { LTRefImpl<RefType>::valid(a) } -> std::convertible_to<bool>;
};

template <LTRef RefType, typename EntryType>
class LinkedTableIterator;

template <LTRef RefType, typename EntryType>
class LinkedTable {
public:
    struct LinkedTableMeta {
        RefType ref;
        uint32_t end;
        uint32_t _rsvd;
    };
    static_assert(sizeof(LinkedTableMeta) % sizeof(RefType) == 0);

    explicit constexpr LinkedTable(std::span<uint8_t> s) : _s(s) {
        _meta = std::launder(reinterpret_cast<LinkedTableMeta *>(_s.data()));
        _entries = convert_span<EntryType>(_s.subspan(sizeof(LinkedTableMeta)));
    }

    static LinkedTable format(std::span<uint8_t> s) {
        LinkedTableMeta *meta = std::launder(reinterpret_cast<LinkedTableMeta *>(s.data()));
        meta->ref = RefType{};
        meta->end = 0;
        meta->_rsvd = 0;
        auto ret = LinkedTable(s);
        std::fill(ret.entries().begin(), ret.entries().end(), EntryType{});
        return ret;
    }

    std::optional<LinkedTable> next(Deref &deref) const {
        if (!LTRefImpl<RefType>::valid(_meta->ref)) {
            return {};
        }
        auto decoded = LTRefImpl<RefType>::decode(_meta->ref);
        return LinkedTable(deref.deref(decoded, _s.size_bytes()));
    }

    inline bool push_back(EntryType e) {
        if (_meta->end == _entries.size()) {
            return false;
        }
        _entries[_meta->end++] = e;
        return true;
    }

    inline bool pop_back() {
        if (_meta->end == 0)
            throw std::logic_error("current table is empty");
        _entries[--_meta->end] = EntryType{};
        return !_meta->end;
    }

    constexpr const std::span<EntryType> &entries() const {
        return _entries;
    }

    constexpr std::span<EntryType> &entries() {
        return _entries;
    }

    constexpr const RefType &next_ref() const {
        return _meta->ref;
    }

    constexpr RefType &next_ref() {
        return _meta->ref;
    }

    constexpr std::span<EntryType> active_entries() {
        return _entries.subspan(0, _meta->end);
    }

    friend class LinkedTableIterator<RefType, EntryType>;
    using iterator = LinkedTableIterator<RefType, EntryType>;

private:
    std::span<uint8_t> _s;
    LinkedTableMeta *_meta;
    std::span<EntryType> _entries;
};

template <LTRef RefType, typename EntryType>
class LinkedTableIterator {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = EntryType;
    using difference_type = void;
    using pointer = EntryType *;
    using reference = EntryType &;

    explicit constexpr LinkedTableIterator(Deref *deref, std::optional<LinkedTable<RefType, EntryType>> table)
        : _deref(deref), _table(table), _off(_table ? _table->active_entries().size() : 0) {
    }

    constexpr LinkedTableIterator &operator++() {
        _off--;
        if (!_off) {
            _table = _table->next(*_deref);
            if (_table)
                _off = _table->active_entries().size();
        }
        return *this;
    }
    constexpr LinkedTableIterator operator++(int) {
        LinkedTableIterator tmp(*this);
        ++*this;
        return tmp;
    }

    constexpr pointer operator->() const {
        if (!_table)
            throw std::runtime_error("no table");
        return &(*_table).entries()[_off - 1];
    }
    constexpr reference operator*() const {
        return *this->operator->();
    }

    constexpr bool at_end() const {
        return !_table && !_off;
    }

private:
    Deref *_deref;
    std::optional<LinkedTable<RefType, EntryType>> _table;
    size_t _off;
};

} // namespace xcow

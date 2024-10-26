#pragma once

#include <iterator>
#include "util.hpp"

namespace xcow {
class blk_iter {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = uint64_t;
    using difference_type = void;
    using pointer = void;
    using reference = void;

    explicit constexpr blk_iter(uint64_t addr_in, size_t sz, size_t cluster_bits)
        : _addr(addr_in), _sz(sz), _cluster_bits(cluster_bits) {
        _next = calc_next();
    }

    constexpr blk_iter &operator++() {
        _sz -= size();
        _addr = _next;
        _next = calc_next();
        return *this;
    }
    constexpr blk_iter operator++(int) {
        blk_iter tmp(*this);
        ++*this;
        return tmp;
    }
    constexpr value_type operator*() const {
        return _addr;
    }

    constexpr bool at_end() const {
        return !_sz;
    }

    constexpr size_t size() const {
        return _next - _addr;
    }

    constexpr bool is_last() const {
        return size() == _sz;
    }

private:
    constexpr size_t calc_next() const {
        size_t next = std::min(round_up_bits(_addr, _cluster_bits), _addr + _sz);
        if (next == _addr)
            next += std::min(_sz, 1ul << _cluster_bits);
        return next;
    }

    uint64_t _addr;
    size_t _sz;
    size_t _cluster_bits;
    size_t _next;
};
} // namespace xcow

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <limits>

#include "nvme_core.hpp"

struct mapping {
    static_assert(std::numeric_limits<unsigned char>::digits == 8);

public:
    explicit mapping(unsigned char *ptr, size_t size) : _span(ptr, size) {
    }
    mapping(const mapping &) = delete;
    mapping &operator=(const mapping &) = delete;
    mapping(mapping &&) noexcept;
    mapping &operator=(mapping &&) noexcept;
    ~mapping();

    friend inline bool operator==(const mapping &a, const mapping &b) {
        return a._span.data() == b._span.data() && a._span.size() == b._span.size();
    }

    constexpr unsigned char *data() const {
        return _span.data();
    }
    constexpr size_t size() const {
        return _span.size();
    }
    std::span<uint64_t> get_page(size_t offset);
    std::span<unsigned char> get_span(size_t offset, size_t size);
    uint64_t get_u64(size_t offset);

private:
    void dispose();
    std::span<unsigned char> _span;
};

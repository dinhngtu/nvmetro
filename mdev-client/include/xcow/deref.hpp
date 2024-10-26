#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <type_traits>
#include "util.hpp"
#include "xcow/xcow_util.hpp"

namespace xcow {

struct Deref {
    virtual ~Deref() = default;

    template <typename T>
    inline std::enable_if_t<T::strong_typedef, std::span<uint8_t>> deref(T, size_t) {
        static_assert(!T::strong_typedef, "trying to deref strong typedef");
    }
    virtual std::span<uint8_t> deref(uint64_t off, size_t nbytes) = 0;
    template <typename T>
    inline std::span<T> deref_as(uint64_t off, size_t nbytes) {
        return convert_span<T>(deref(off, nbytes));
    }
    virtual void commit([[maybe_unused]] uint64_t off, [[maybe_unused]] size_t nbytes) {
    }
    virtual void prefetch([[maybe_unused]] uint64_t off, [[maybe_unused]] size_t nbytes) {
    }
};

} // namespace xcow

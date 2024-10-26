#pragma once

#include <type_traits>
#include <boost/config.hpp>
#include <boost/operators.hpp>
#include <boost/type_traits.hpp>

namespace xcow {

#define STRONG_TYPEDEF_I(T, D, I)                                    \
    struct D {                                                       \
        static constexpr bool strong_typedef = true;                 \
        using backing_type = T;                                      \
        T val;                                                       \
        constexpr explicit D(const T &t_) noexcept : val(t_) {       \
        }                                                            \
        constexpr D() noexcept : val(I) {                            \
        }                                                            \
        constexpr D(const D &t_) noexcept : val(t_.val) {            \
        }                                                            \
        constexpr D &operator=(const D &rhs) noexcept {              \
            val = rhs.val;                                           \
            return *this;                                            \
        }                                                            \
        constexpr D &operator=(const T &rhs) noexcept {              \
            val = rhs;                                               \
            return *this;                                            \
        }                                                            \
        constexpr bool operator==(const D &rhs) const noexcept {     \
            return val == rhs.val;                                   \
        }                                                            \
        constexpr bool operator<(const D &rhs) const noexcept {      \
            return val < rhs.val;                                    \
        }                                                            \
        friend constexpr T operator&(D lhs, const T &rhs) noexcept { \
            return lhs.val & rhs;                                    \
        }                                                            \
        friend constexpr T operator|(D lhs, const T &rhs) noexcept { \
            return lhs.val | rhs;                                    \
        }                                                            \
    };
} // namespace xcow

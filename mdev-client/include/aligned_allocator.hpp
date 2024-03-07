#pragma once

#include <cstdlib>
#include <limits>
#include <memory>
#include <cstddef>

template <class T, size_t N = sizeof(T)>
class aligned_allocator {
public:
    using value_type = T;
    using pointer = T *;
    using const_pointer = const T *;
    using reference = T &;
    using const_reference = const T &;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using is_always_equal = std::true_type;
    static_assert(N > 0 && ((N - 1) & N) == 0, "alignment must be a power of 2");

    template <class U>
    struct rebind {
        using other = aligned_allocator<U, N>;
    };

    constexpr T *address(T &x) const noexcept {
        return &x;
    }
    constexpr const T *address(const T &x) const noexcept {
        return &x;
    }

    [[nodiscard]] constexpr T *allocate(size_t n) {
        if (std::numeric_limits<std::size_t>::max() / sizeof(T) < n) {
            throw std::bad_array_new_length();
        }
        // round up to alignment
        auto asize = (sizeof(T) * n + N - 1) & ~(N - 1);
        auto ret = static_cast<T *>(aligned_alloc(N, asize));
        if (!ret)
            throw std::bad_alloc();
        return ret;
    }

    constexpr void deallocate(T *p, size_t) {
        free(p);
    }

    constexpr size_t max_size() const noexcept {
        return (std::numeric_limits<size_t>::max() & ~(N - 1)) / sizeof(T);
    }

    template <class U, class... Args>
    constexpr void construct(U *p, Args &&...args) {
        ::new ((void *)p) U(std::forward<Args>(args)...);
    }

    template <class U>
    constexpr void destroy(U *p) {
        p->~U();
    }
};

template <class T1, int N1, class T2, int N2>
constexpr bool operator==(const aligned_allocator<T1, N1> &, const aligned_allocator<T2, N2> &) noexcept {
    return true;
}

template <class T1, int N1, class T2, int N2>
constexpr bool operator!=(const aligned_allocator<T1, N1> &, const aligned_allocator<T2, N2> &) noexcept {
    return false;
}

#pragma once

#include <cstdlib>
#include <limits>
#include <memory>
#include <cstddef>
#include <mimalloc.h>
#include "util.hpp"

class arena {
public:
    explicit arena(size_t arena_size);
    arena(const arena &) = delete;
    arena &operator=(const arena &) = delete;
    arena(arena &&) = default;
    arena &operator=(arena &&) = default;

    constexpr mi_heap_t *get() {
        return _heap.get();
    }

    constexpr void *ptr() {
        return _ptr;
    }

    constexpr size_t size() {
        return _size;
    }

private:
    void *_ptr;
    size_t _size;
    mi_arena_id_t _arena;
    unique_handle<mi_heap_t> _heap;
};

template <class T>
class arena_allocator {
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
        using other = arena_allocator<U, N>;
    };

    constexpr explicit arena_allocator(arena *a, bool allow_backup) : _a(a), _allow_backup(allow_backup) {
    }

    constexpr arena *arena() const noexcept {
        return _a;
    }

    constexpr T *address(T &x) const noexcept {
        return &x;
    }
    constexpr const T *address(const T &x) const noexcept {
        return &x;
    }

    [[nodiscard]] constexpr T *allocate(size_t n) {
        bool x = false;
        return allocate2(n, x);
    }

    [[nodiscard]] constexpr T *allocate2(size_t n, bool &backup) {
        backup = false;
        auto ret = mi_heap_malloc(_a->get(), n);
        if (!ret && _allow_backup) {
            ret = mi_malloc(n);
            if (ret)
                backup = true;
        }
        if (!ret)
            throw std::bad_alloc();
        return ret;
    }

    constexpr void deallocate(T *p, size_t) {
        mi_free(p);
    }

    constexpr size_t max_size() const noexcept {
        return std::numeric_limits<size_t>::max() / sizeof(T);
    }

    template <class U, class... Args>
    constexpr void construct(U *p, Args &&...args) {
        ::new ((void *)p) U(std::forward<Args>(args)...);
    }

    template <class U>
    constexpr void destroy(U *p) {
        p->~U();
    }

    template <class T1, class T2>
    friend constexpr bool operator==(const arena_allocator<T1> &a, const arena_allocator<T2> &b) noexcept {
        return std::make_tuple(a._a, a._allow_backup) == std::make_tuple(b._a, b._allow_backup);
    }

private:
    arena *_a;
    bool _allow_backup;
};

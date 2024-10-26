#pragma once

#include <memory>
#include <functional>
#include <cstdio>
#include <optional>
#include <utility>
#include <span>
#include <new>

#if DEBUG || DBGPRINT
#define DBG_PRINTF(fmt, args...) printf(fmt, ##args)
#else
#define DBG_PRINTF(fmt, args...) \
    do {                         \
    } while (0)
#endif

// https://stackoverflow.com/a/27885283/8642889
template <typename F>
struct my_function_traits {};

template <typename R, typename... T>
struct my_function_traits<std::function<R(T...)>> {
    using result_type = R;
    using argument_types = std::tuple<T...>;
};

template <typename R, typename... T>
struct my_function_traits<R(T...)> {
    using result_type = R;
    using argument_types = std::tuple<T...>;
};

template <typename R, typename... T>
struct my_function_traits<R (*)(T...)> {
    using result_type = R;
    using argument_types = std::tuple<T...>;
};

template <typename F>
using my_first_argument_t = std::tuple_element_t<0, typename my_function_traits<F>::argument_types>;

template <auto F, typename T = std::remove_cvref_t<my_first_argument_t<decltype(F)>>, T invalid = T()>
    requires std::invocable<decltype(F), T> && std::copyable<T> && std::assignable_from<T, T> &&
             std::equality_comparable<T>
class auto_handle {
public:
    constexpr auto_handle() noexcept : _p(invalid) {
    }
    constexpr explicit auto_handle(T p) noexcept : _p(p) {
    }
    constexpr auto_handle(const auto_handle &) = delete;
    constexpr auto_handle &operator=(const auto_handle &) = delete;
    constexpr auto_handle(auto_handle &&other) noexcept : _p(invalid) {
        swap(*this, other);
    }
    constexpr auto_handle &operator=(auto_handle &&other) noexcept {
        if (this->_p != other._p) {
            dispose();
            swap(*this, other);
        }
        return *this;
    }
    constexpr ~auto_handle() {
        dispose();
    }
    constexpr T release() noexcept {
        return std::exchange(_p, invalid);
    }
    constexpr void reset() noexcept {
        dispose();
    }
    constexpr T get() const noexcept {
        return _p;
    }
    explicit constexpr operator bool() const noexcept {
        return _p != invalid;
    }
    constexpr std::enable_if_t<std::is_pointer_v<T>, std::remove_pointer_t<T> &> operator*() const noexcept {
        return *_p;
    }
    constexpr std::enable_if_t<std::is_pointer_v<T>, T> operator->() const noexcept {
        return _p;
    }
    constexpr T *operator&() {
        assert(_p == invalid);
        return &_p;
    }

    constexpr friend void swap(auto_handle &self, auto_handle &other) noexcept {
        using std::swap;
        swap(self._p, other._p);
    }

private:
    T _p;

    constexpr void dispose() {
        if (_p != invalid)
            F(_p);
        _p = invalid;
    }
};

template <class T, class U>
std::span<T> convert_span(std::span<U> s) {
    auto data = s.data();
    if (!data)
        return {};
    auto bytes = s.size_bytes();
    if (bytes % sizeof(T) != 0)
        throw std::runtime_error("invalid span conversion");
    return {std::launder(reinterpret_cast<T *>(data)), bytes / sizeof(T)};
}

template <typename T, typename DR = void>
using unique_handle = std::unique_ptr<T, std::function<DR(T *)>>;

template <typename R, typename... Params>
class cleanup_t {
public:
    using cb_type = std::function<R(Params...)>;

    explicit cleanup_t(cb_type cleaner, Params &&...args) : _cleaner(cleaner), _args(std::forward_as_tuple(args...)) {
    }
    explicit cleanup_t(R (*cleaner)(Params...), Params &&...args) : cleanup_t(static_cast<cb_type>(cleaner), args...) {
    }
    cleanup_t(const cleanup_t &) = delete;
    cleanup_t &operator=(const cleanup_t &) = delete;
    cleanup_t(cleanup_t &&) = default;
    cleanup_t &operator=(cleanup_t &&) = default;
    ~cleanup_t() {
        std::apply(_cleaner, _args);
    }

private:
    cb_type _cleaner;
    std::tuple<Params &&...> _args;
};

class cleanup {
public:
    using cb_type = std::function<void()>;

    constexpr cleanup() {
    }
    explicit cleanup(cb_type cleaner) : _cleaner(cleaner) {
    }
    cleanup(const cleanup &) = delete;
    cleanup &operator=(const cleanup &) = delete;
    constexpr cleanup(cleanup &&other) {
        swap(*this, other);
    }
    constexpr cleanup &operator=(cleanup &&other) {
        if (this != &other) {
            dispose();
            swap(*this, other);
        }
        return *this;
    };
    ~cleanup() {
        dispose();
    }

    friend void swap(cleanup &self, cleanup &other) {
        using std::swap;
        swap(self._cleaner, other._cleaner);
    }

    void neutralize() {
        _cleaner = std::nullopt;
    }

private:
    std::optional<cb_type> _cleaner = {};

    void dispose() {
        if (_cleaner.has_value()) {
            _cleaner.value()();
            _cleaner = std::nullopt;
        }
    }
};

template <typename T>
static constexpr T round_up(T val, T round) {
    return (val + (round - 1)) / round * round;
}

template <typename T>
static constexpr T round_up_bits(T val, size_t bits) {
    return (val + ((1 << bits) - 1)) >> bits << bits;
}

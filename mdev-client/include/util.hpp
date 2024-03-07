#pragma once

#include <memory>
#include <functional>
#include <cstdio>
#include <utility>

#ifdef DEBUG
#define DBG_PRINTF(fmt, args...) printf(fmt, ##args)
#else
#define DBG_PRINTF(fmt, args...)
#endif

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

    explicit cleanup(cb_type cleaner) : _cleaner(cleaner) {
    }
    cleanup(const cleanup &) = delete;
    cleanup &operator=(const cleanup &) = delete;
    cleanup(cleanup &&) = default;
    cleanup &operator=(cleanup &&) = default;
    ~cleanup() {
        _cleaner();
    }

private:
    cb_type _cleaner;
};

static constexpr uint32_t make_tag(uint16_t qi, uint16_t ucid) {
    return (uint32_t)qi << 16 | (uint32_t)ucid;
}

static constexpr std::pair<uint16_t, uint16_t> unmake_tag(uint32_t tag) {
    return std::make_pair(tag >> 16, tag & 0xffff);
}

#pragma once

#include <memory>
#include <functional>
#include <cstdio>

#ifdef DEBUG
#define DBG_PRINTF(fmt, args...) printf(fmt, ##args)
#else
#define DBG_PRINTF(fmt, args...)
#endif

template <typename T, typename DR = void>
using unique_handle = std::unique_ptr<T, std::function<DR(T *)>>;

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

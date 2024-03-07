#pragma once

#include <bit>
#include <cstring>
#include <stdexcept>
#include <span>
#include <limits>
#include <cstdio>
#include <system_error>
#include <utility>

#include <sys/mman.h>
#include <emmintrin.h>

#include "nvme_core.hpp"

// neither C11 _Atomic nor std::atomic are guaranteed to have the same size/alignment as raw objects
// therefore we have to use gcc atomic builtins in our code

enum class cmdbuf_direction { producer, consumer };

template <typename T, size_t count, cmdbuf_direction direction>
class cmdbuf {
    static_assert(count < size_t(std::numeric_limits<int>::max()));
    static_assert(!(count * sizeof(T) % NVME_PAGE_SIZE));

public:
    explicit cmdbuf(int fd, size_t map_start_pg) {
        if (fd < 0) {
            return;
        }
        size_t map_pg_offset = map_start_pg;
        _cmdbuf = (cmdbuf_pointer)mmap(
            nullptr,
            count * sizeof(T),
            (direction == cmdbuf_direction::producer) ? (PROT_READ | PROT_WRITE) : PROT_READ,
            MAP_SHARED,
            fd,
            map_pg_offset << NVME_PAGE_SHIFT);
        if (_cmdbuf == MAP_FAILED) {
            throw std::system_error(errno, std::generic_category(), "cannot map cmdbuf");
        }
        map_pg_offset += count * sizeof(T) / NVME_PAGE_SIZE;
        _head = (head_pointer)mmap(
            nullptr,
            NVME_PAGE_SIZE,
            // producer writes head
            (direction == cmdbuf_direction::producer) ? (PROT_READ | PROT_WRITE) : PROT_READ,
            MAP_SHARED,
            fd,
            map_pg_offset << NVME_PAGE_SHIFT);
        if (_head == MAP_FAILED) {
            throw std::system_error(errno, std::generic_category(), "cannot map head");
        }
        map_pg_offset++;
        _tail = (tail_pointer)mmap(
            nullptr,
            NVME_PAGE_SIZE,
            // consumer writes tail
            (direction == cmdbuf_direction::producer) ? PROT_READ : (PROT_READ | PROT_WRITE),
            MAP_SHARED,
            fd,
            map_pg_offset << NVME_PAGE_SHIFT);
        if (_tail == MAP_FAILED) {
            throw std::system_error(errno, std::generic_category(), "cannot map tail");
        }
    }
    cmdbuf(const cmdbuf &) = delete;
    cmdbuf &operator=(const cmdbuf &) = delete;
    cmdbuf(cmdbuf &&other) {
        swap(*this, other);
    }
    cmdbuf &operator=(cmdbuf &&other) {
        if (this != &other) {
            this->dispose();
            swap(*this, other);
        }
        return *this;
    }
    ~cmdbuf() {
        dispose();
    }

    inline int peek_space(int &new_head) const {
        static_assert(direction == cmdbuf_direction::producer, "only usable in producer queues");
        check();
        new_head = *_head;
        int tail = __atomic_load_n(_tail, __ATOMIC_ACQUIRE);
        return circ_space(new_head, tail, count);
    }

    inline int peek_space() const {
        int head = 0;
        return peek_space(head);
    }

    inline void produce_raw(std::span<const T> in, int head, size_t to_produce) {
        static_assert(direction == cmdbuf_direction::producer, "only usable in producer queues");
        check();
        if (to_produce >= 1) {
            for (size_t i = 0; i < to_produce; i++) {
                _cmdbuf[head] = in[i];
                head = (head + 1) & (count - 1);
            }
            __atomic_store_n(_head, head, __ATOMIC_RELEASE);
        }
    }

    inline int produce(std::span<const T> in) {
        int head = 0;
        int to_produce = std::min((int)in.size(), peek_space(head));
        produce_raw(in, head, to_produce);
        return to_produce;
    }

    inline int peek_items(int &new_tail) const {
        static_assert(direction == cmdbuf_direction::consumer, "only usable in consumer queues");
        check();
        int head = __atomic_load_n(_head, __ATOMIC_ACQUIRE);
        new_tail = *_tail;
        return circ_cnt(head, new_tail, count);
    }

    inline int peek_items() const {
        int tail = 0;
        return peek_items(tail);
    }

    inline void consume_raw(std::span<T> out, int tail, size_t to_consume) {
        static_assert(direction == cmdbuf_direction::consumer, "only usable in consumer queues");
        check();
        if (to_consume >= 1) {
            for (size_t i = 0; i < to_consume; i++) {
                out[i] = _cmdbuf[tail];
                tail = (tail + 1) & (count - 1);
            }
            __atomic_store_n(_tail, tail, __ATOMIC_RELEASE);
        }
    }

    inline int consume(std::span<T> out) {
        int tail = 0;
        int to_consume = std::min((int)out.size(), peek_items(tail));
        consume_raw(out, tail, to_consume);
        return to_consume;
    }

private:
    using cmdbuf_pointer = std::conditional_t<direction == cmdbuf_direction::producer, T *, const T *>;
    using head_pointer =
        std::conditional_t<direction == cmdbuf_direction::producer, volatile int *, const volatile int *>;
    using tail_pointer =
        std::conditional_t<direction == cmdbuf_direction::producer, const volatile int *, volatile int *>;

#ifdef _DEBUG
    inline void check() const {
        if (!_cmdbuf || !_head || !_tail) {
            throw std::runtime_error("invalid cmdbuf");
        }
    }
#else
    inline void check() const {
    }
#endif

    // linux/circ_buf.h
    static inline int circ_cnt(int head, int tail, size_t size) {
        return (head - tail) & (size - 1);
    }
    static inline int circ_space(int head, int tail, size_t size) {
        return circ_cnt(tail, head + 1, size);
    }

    inline void swap(cmdbuf &self, cmdbuf &other) {
        std::swap(self._cmdbuf, other._cmdbuf);
        std::swap(self._head, other._head);
        std::swap(self._tail, other._tail);
    }

    void dispose() {
        if (_tail) {
            munmap((void *)_tail, NVME_PAGE_SIZE);
            _tail = (tail_pointer) nullptr;
        }
        if (_head) {
            munmap((void *)_head, NVME_PAGE_SIZE);
            _head = (head_pointer) nullptr;
        }
        if (_cmdbuf) {
            munmap((void *)_cmdbuf, count * sizeof(T));
            _cmdbuf = (cmdbuf_pointer) nullptr;
        }
    }

    cmdbuf_pointer _cmdbuf = (cmdbuf_pointer) nullptr;
    head_pointer _head = (head_pointer) nullptr;
    tail_pointer _tail = (tail_pointer) nullptr;
};

using nsqbuf_t = cmdbuf<
    struct nvme_command,
    (NMNTFY_SQ_DATA_NR_PAGES << NVME_PAGE_SHIFT) / sizeof(struct nvme_command),
    cmdbuf_direction::consumer>;

using ncqbuf_t = cmdbuf<
    struct nmntfy_response,
    (NMNTFY_CQ_DATA_NR_PAGES << NVME_PAGE_SHIFT) / sizeof(struct nmntfy_response),
    cmdbuf_direction::producer>;

static inline void cq_produce_one(ncqbuf_t &ncqbuf, const nmntfy_response &resp) {
    std::span<const nmntfy_response> one{&resp, 1};
    while (!ncqbuf.produce(one)) {
        _mm_pause();
    }
}

#pragma once

#include <span>

#include <algorithm>
#include <type_traits>

#include "nvme_core.hpp"

// neither C11 _Atomic nor std::atomic are guaranteed to have the same size/alignment as raw objects
// therefore we have to use gcc atomic builtins in our code

enum class cmdbuf_en_direction { producer, consumer };

template <typename T, size_t count, cmdbuf_en_direction direction>
class cmdbuf_en {
    static_assert(count < size_t(std::numeric_limits<int>::max()));
    static_assert(!(count * sizeof(T) % NVME_PAGE_SIZE));

public:
    using cmdbuf_pointer = std::conditional_t<direction == cmdbuf_en_direction::producer, T *, const T *>;
    using head_pointer =
        std::conditional_t<direction == cmdbuf_en_direction::producer, volatile int *, const volatile int *>;
    using tail_pointer =
        std::conditional_t<direction == cmdbuf_en_direction::producer, const volatile int *, volatile int *>;

    explicit cmdbuf_en(cmdbuf_pointer cmdbuf, head_pointer head, tail_pointer tail)
        : _cmdbuf(cmdbuf), _head(head), _tail(tail) {
    }
    cmdbuf_en(const cmdbuf_en &) = delete;
    cmdbuf_en &operator=(const cmdbuf_en &) = delete;
    cmdbuf_en(cmdbuf_en &&other) {
        swap(*this, other);
    }
    cmdbuf_en &operator=(cmdbuf_en &&other) {
        if (this != &other) {
            _tail = (tail_pointer) nullptr;
            _head = (head_pointer) nullptr;
            _cmdbuf = (cmdbuf_pointer) nullptr;
            swap(*this, other);
        }
        return *this;
    }
    ~cmdbuf_en() = default;

    inline int peek_space(int &new_head) const {
        static_assert(direction == cmdbuf_en_direction::producer, "only usable in producer queues");
        new_head = *_head;
        int tail = __atomic_load_n(_tail, __ATOMIC_ACQUIRE);
        return circ_space(new_head, tail, count);
    }

    inline int peek_space() const {
        int head = 0;
        return peek_space(head);
    }

    inline void produce_raw(std::span<const T> in, int head, size_t to_produce) {
        static_assert(direction == cmdbuf_en_direction::producer, "only usable in producer queues");
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
        static_assert(direction == cmdbuf_en_direction::consumer, "only usable in consumer queues");
        int head = __atomic_load_n(_head, __ATOMIC_ACQUIRE);
        new_tail = *_tail;
        return circ_cnt(head, new_tail, count);
    }

    inline int peek_items() const {
        int tail = 0;
        return peek_items(tail);
    }

    inline void consume_raw(std::span<T> out, int tail, size_t to_consume) {
        static_assert(direction == cmdbuf_en_direction::consumer, "only usable in consumer queues");
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
    // linux/circ_buf.h
    static inline int circ_cnt(int head, int tail, size_t size) {
        return (head - tail) & (size - 1);
    }
    static inline int circ_space(int head, int tail, size_t size) {
        return circ_cnt(tail, head + 1, size);
    }

    inline void swap(cmdbuf_en &self, cmdbuf_en &other) {
        std::swap(self._cmdbuf, other._cmdbuf);
        std::swap(self._head, other._head);
        std::swap(self._tail, other._tail);
    }

    cmdbuf_pointer _cmdbuf;
    head_pointer _head;
    tail_pointer _tail;
};

using nsqbuf_en_t = cmdbuf_en<
    struct nvme_command,
    (NMNTFY_SQ_DATA_NR_PAGES << NVME_PAGE_SHIFT) / sizeof(struct nvme_command),
    cmdbuf_en_direction::consumer>;

using ncqbuf_en_t = cmdbuf_en<
    struct nmntfy_response,
    (NMNTFY_CQ_DATA_NR_PAGES << NVME_PAGE_SHIFT) / sizeof(struct nmntfy_response),
    cmdbuf_en_direction::producer>;

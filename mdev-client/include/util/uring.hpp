#pragma once

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <memory>
#include <span>

#include <liburing.h>
#include <type_traits>

#include "nvme_core.hpp"
#include "util.hpp"

enum class sq_ticket_category {
    none,
    iovec,
};

struct sq_ticket {
    virtual ~sq_ticket() = default;
    sq_ticket_category category = sq_ticket_category::none;
    uint32_t tag = 0;
};

struct iovec_ticket final : public sq_ticket {
    static constexpr size_t reserve_count = 32;
    iovec_ticket() {
        category = sq_ticket_category::iovec;
        iovecs.reserve(reserve_count);
    }
    std::vector<iovec> iovecs;
};

struct cq_window {
    cq_window(const cq_window &) = delete;
    cq_window &operator=(const cq_window &) = delete;
    cq_window(cq_window &&other) {
        std::swap(this->cqes, other.cqes);
    }
    cq_window &operator=(cq_window &&other) {
        this->cqes = std::span<io_uring_cqe *>{};
        std::swap(this->cqes, other.cqes);
        return *this;
    }
    ~cq_window() = default;

    std::span<io_uring_cqe *> cqes;

private:
    friend class uring;

    explicit cq_window(const std::span<io_uring_cqe *> &cqebuf) : cqes(cqebuf) {
    }
};

class uring {
public:
    explicit uring(unsigned int entries, unsigned int flags, std::span<const int> fixed_files);
    uring(const uring &) = delete;
    uring &operator=(const uring &) = delete;
    uring(uring &&other) = default;
    uring &operator=(uring &&other) = default;
    ~uring() = default;

    inline int sq_kick() {
        return io_uring_submit(_ring.get());
    }

    void queue_readv(iovec_ticket *ticket, bool fixed, int fid, off_t offset);
    void queue_writev(iovec_ticket *ticket, bool fixed, int fid, off_t offset);
    void queue_fallocate(sq_ticket *ticket, bool fixed, int fid, int mode, off_t offset, off_t len);
    void queue_fsync(sq_ticket *ticket, bool fixed, int fid, unsigned int flags);

    cq_window cq_get_ready(const std::span<io_uring_cqe *> &cqebuf);
    std::span<sq_ticket *> cq_commit(cq_window &wnd, const std::span<sq_ticket *> &ticketbuf);
    void cq_commit(cq_window &wnd);
    inline void cq_commit(io_uring_cqe *cqe) {
        io_uring_cqe_seen(_ring.get(), cqe);
    }

private:
    inline struct io_uring_sqe *get_sqe() {
        return io_uring_get_sqe(_ring.get());
    };
    unique_handle<struct io_uring> _ring;
};

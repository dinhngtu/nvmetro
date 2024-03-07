#include <algorithm>
#include <memory>
#include <exception>
#include <system_error>

#include <liburing.h>

#include "util/uring.hpp"

static void uring_free(struct io_uring *ring) {
    io_uring_queue_exit(ring);
    delete ring;
}

static unique_handle<io_uring> uring_create(unsigned int entries, unsigned int flags) {
    auto ring = new io_uring();
    auto ret = io_uring_queue_init(entries, ring, flags);
    if (ret < 0) {
        delete ring;
        throw std::system_error(-ret, std::generic_category(), "cannot init uring");
    }
    return unique_handle<io_uring>(ring, uring_free);
}

uring::uring(unsigned int entries, unsigned int flags, std::span<const int> fixed_files)
    : _ring(uring_create(entries, flags)) {
    if (!fixed_files.empty()) {
        auto ret = io_uring_register_files(_ring.get(), fixed_files.data(), fixed_files.size());
        if (ret < 0) {
            throw std::system_error(-ret, std::generic_category(), "cannot register uring files");
        }
    }
}

void uring::queue_readv(iovec_ticket *ticket, bool fixed, int fid, off_t offset) {
    auto sqe = get_sqe();
    io_uring_prep_readv(sqe, fid, ticket->iovecs.data(), ticket->iovecs.size(), offset);
    io_uring_sqe_set_flags(sqe, IOSQE_ASYNC | (fixed ? IOSQE_FIXED_FILE : 0));
    io_uring_sqe_set_data(sqe, ticket);
}

void uring::queue_writev(iovec_ticket *ticket, bool fixed, int fid, off_t offset) {
    auto sqe = get_sqe();
    io_uring_prep_writev(sqe, fid, ticket->iovecs.data(), ticket->iovecs.size(), offset);
    io_uring_sqe_set_flags(sqe, IOSQE_ASYNC | (fixed ? IOSQE_FIXED_FILE : 0));
    io_uring_sqe_set_data(sqe, ticket);
}

void uring::queue_fallocate(sq_ticket *ticket, bool fixed, int fid, int mode, off_t offset, off_t len) {
    auto sqe = get_sqe();
    io_uring_prep_fallocate(sqe, fid, mode, offset, len);
    io_uring_sqe_set_flags(sqe, IOSQE_ASYNC | (fixed ? IOSQE_FIXED_FILE : 0));
    io_uring_sqe_set_data(sqe, ticket);
}

void uring::queue_fsync(sq_ticket *ticket, bool fixed, int fid, unsigned int flags) {
    auto sqe = get_sqe();
    io_uring_prep_fsync(sqe, fid, flags);
    io_uring_sqe_set_flags(sqe, IOSQE_ASYNC | (fixed ? IOSQE_FIXED_FILE : 0));
    io_uring_sqe_set_data(sqe, ticket);
}

cq_window uring::cq_get_ready(const std::span<io_uring_cqe *> &cqebuf) {
    auto rdy = io_uring_peek_batch_cqe(_ring.get(), cqebuf.data(), cqebuf.size());
    return cq_window(cqebuf.subspan(0, rdy));
}

std::span<sq_ticket *> uring::cq_commit(cq_window &wnd, const std::span<sq_ticket *> &ticketbuf) {
    size_t count = 0;
    for (auto it = wnd.cqes.begin(); it != wnd.cqes.end() && count < ticketbuf.size(); it++, count++) {
        auto ticket = static_cast<sq_ticket *>(io_uring_cqe_get_data(*it));
        ticketbuf[count] = ticket;
        io_uring_cqe_seen(_ring.get(), *it);
    }
    wnd.cqes = wnd.cqes.subspan(count);
    return ticketbuf.subspan(0, count);
}

void uring::cq_commit(cq_window &wnd) {
    for (auto it = wnd.cqes.begin(); it != wnd.cqes.end(); it++) {
        io_uring_cqe_seen(_ring.get(), *it);
    }
}

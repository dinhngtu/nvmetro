#pragma once

#include <vector>
#include <utility>
#include <sys/uio.h>

#include "aligned_allocator.hpp"
#include "nvme.hpp"
#include "util/uring.hpp"

class nvme_snap_aio final : public nvme {
public:
    explicit nvme_snap_aio(const std::shared_ptr<mapping> &vm, int nfd, int bfd)
        : nvme(vm, nfd), _bfd{{bfd}}, _ring(2048, 0, std::span(_bfd)) {
    }
    nvme_snap_aio(const nvme_snap_aio &) = delete;
    nvme_snap_aio &operator=(const nvme_snap_aio &) = delete;
    nvme_snap_aio(nvme_snap_aio &&) = default;
    nvme_snap_aio &operator=(nvme_snap_aio &&) = default;
    ~nvme_snap_aio() = default;

    inline int sq_kick() {
        return _ring.sq_kick();
    }

    // true: async, false: immediate return
    bool submit_async(size_t sq, const nvme_command &cmd, uint32_t tag, __u16 &outstatus);
    cq_window get_pending_completions(std::span<io_uring_cqe *> cqebuf);
    static inline sq_ticket *cqe_get_data(io_uring_cqe *cqe) {
        return static_cast<sq_ticket *>(io_uring_cqe_get_data(cqe));
    }
    inline std::span<sq_ticket *> commit_completions(cq_window &wnd, const std::span<sq_ticket *> &ticketbuf) {
        return _ring.cq_commit(wnd, ticketbuf);
    }
    inline void commit_completions(cq_window &wnd) {
        return _ring.cq_commit(wnd);
    }
    inline void commit_completion(io_uring_cqe *cqe) {
        return _ring.cq_commit(cqe);
    }

private:
    void submit_read_async(size_t sq, const nvme_command &cmd, uint32_t tag);
    void submit_write_async(size_t sq, const nvme_command &cmd, uint32_t tag);
    void submit_write_zeroes_async(size_t sq, const nvme_command &cmd, uint32_t tag);
    void submit_flush_async(size_t sq, const nvme_command &cmd, uint32_t tag);
    std::array<int, 1> _bfd;
    uring _ring;
    aligned_allocator<unsigned char, 64> allocator{};
};

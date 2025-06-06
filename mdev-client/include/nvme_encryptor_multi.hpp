#pragma once

#include <vector>
#include <utility>
#include <sys/uio.h>

#include "nvme.hpp"
#include "crypto/tbc.hpp"
#include "util/uring.hpp"

class nvme_encryptor_multi final : public nvme {
public:
    explicit nvme_encryptor_multi(
        const std::shared_ptr<mapping> &vm,
        int nfd,
        const std::shared_ptr<uring> &bring,
        const std::shared_ptr<tweakable_block_cipher> &engine)
        : nvme(vm, nfd), _engine(engine), _ring(bring) {
    }
    nvme_encryptor_multi(const nvme_encryptor_multi &) = delete;
    nvme_encryptor_multi &operator=(const nvme_encryptor_multi &) = delete;
    nvme_encryptor_multi(nvme_encryptor_multi &&) = default;
    nvme_encryptor_multi &operator=(nvme_encryptor_multi &&) = default;
    ~nvme_encryptor_multi() = default;

    inline int sq_kick() {
        return _ring->sq_kick();
    }

    // true: async, false: immediate return
    bool submit_async(size_t sq, const nvme_command &cmd, uint32_t tag, __u16 &outstatus);
    cq_window get_pending_completions(std::span<io_uring_cqe *> cqebuf);
    static inline sq_ticket *cqe_get_data(io_uring_cqe *cqe) {
        return static_cast<sq_ticket *>(io_uring_cqe_get_data(cqe));
    }
    inline std::span<sq_ticket *> commit_completions(cq_window &wnd, const std::span<sq_ticket *> &ticketbuf) {
        return _ring->cq_commit(wnd, ticketbuf);
    }
    inline void commit_completions(cq_window &wnd) {
        return _ring->cq_commit(wnd);
    }
    inline void commit_completion(io_uring_cqe *cqe) {
        return _ring->cq_commit(cqe);
    }

private:
    __u16 receive_read(size_t sq, const nvme_command &cmd);
    void submit_write_async(size_t sq, const nvme_command &cmd, uint32_t tag);
    void submit_write_zeroes_async(size_t sq, const nvme_command &cmd, uint32_t tag);
    void submit_flush_async(size_t sq, const nvme_command &cmd, uint32_t tag);
    std::shared_ptr<tweakable_block_cipher> _engine;
    std::shared_ptr<uring> _ring;
};

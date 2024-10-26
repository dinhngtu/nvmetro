#include <array>
#include <bit>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <sys/poll.h>
#include <vector>
#include <thread>
#include <functional>
#include <optional>
#include <sstream>
#include <span>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "util.hpp"
#include "fildes.hpp"
#include "cmdbuf.hpp"
#include "nvme_xcow.hpp"
#include "xcow/file_deref.hpp"
#include "util/mdev.hpp"
#include "util/time.hpp"
#include "util/uring.hpp"

constexpr size_t MAX_VIRTUAL_QUEUES = 16;
constexpr size_t NOTIFYFD_BURST = 16;
constexpr size_t COMPLETION_BURST = 128;
constexpr int sleeppoll_ms = 100;
constexpr long busypoll_ms = 500;
constexpr unsigned int busypoll_loops = 20;

struct worker_arg {
    std::vector<size_t> sqids;
    std::vector<int> sqfds;
    const char *mapfile;
    const char *blkdev;
    size_t below_4g_mem_size;
    int mfd;
    unsigned char *pvm;
    size_t pvm_size;
    int adm_sqfd;
};

class worker {
    std::shared_ptr<mapping> vm;
    FileDescriptor bfd, mapfd;
    cleanup flk;

    std::unique_ptr<xcow::FileDeref> deref;
    std::unique_ptr<xcow::XcowFile> f;

    std::vector<bool> clock;
    nvme_xcow::workqueue_type wq;
    std::optional<nvme_xcow> controller;

    std::vector<size_t> sqids;
    int adm_sqfd;

    std::vector<nsqbuf_t> nsqbufs;
    std::vector<ncqbuf_t> ncqbufs;
    std::vector<pollfd> pollfds;

    std::optional<nsqbuf_t> adm_nsqbuf;
    std::optional<ncqbuf_t> adm_ncqbuf;

    std::array<nvme_command, NOTIFYFD_BURST> cmds{};
    std::array<io_uring_cqe *, COMPLETION_BURST> cqebuf{};

    std::pair<bool, bool> queue_poll_once(size_t qi, size_t sq, nsqbuf_t &nsqbuf, ncqbuf_t &ncqbuf) {
        bool succeeded = false, submitted_async = false;
        int new_tail = 0;
        int ncmds = std::min(static_cast<int>(cmds.size()), nsqbuf.peek_items(new_tail));
        if (ncmds) {
            succeeded = true;
            nsqbuf.consume_raw(cmds, new_tail, ncmds);
            for (int j = 0; j < ncmds; j++) {
                auto tag = make_tag(qi, cmds[j].common.command_id);
                auto outcome = controller->submit_async(sq, cmds[j], tag);
                if (std::holds_alternative<xcow_ticket *>(outcome)) {
                    submitted_async = true;
                } else if (std::holds_alternative<nm_reply>(outcome)) {
                    const auto &reply = std::get<nm_reply>(outcome);
                    process_reply(cmds[j].common.command_id, reply, ncqbuf);
                }
            }
        }
        return std::make_pair(succeeded, submitted_async);
    }

    void process_reply(uint16_t ucid, const nm_reply &reply, ncqbuf_t &ncqbuf) {
        nmntfy_response resp{
            .ucid = ucid,
            .status = reply.status,
        };
        std::copy(reply.aux.begin(), reply.aux.end(), &resp.aux[0]);
        cq_produce_one(ncqbuf, resp);
    }

public:
    worker(const worker_arg &arg) {
        if (!arg.pvm || !arg.pvm_size || arg.sqids.empty() || arg.sqids.size() != arg.sqfds.size())
            throw std::invalid_argument("worker_arg");

        vm = std::make_shared<mapping>(arg.pvm, arg.pvm_size);
        bfd = FileDescriptor(arg.blkdev, O_RDWR | O_DIRECT);
        if (bfd.err()) {
            throw std::system_error(bfd.err(), std::generic_category(), "cannot open blkdev file");
        }

        mapfd = FileDescriptor(arg.mapfile, O_RDWR);
        if (mapfd.err())
            throw std::system_error(mapfd.err(), std::generic_category(), "cannot open mapfile");
        flk = xcow::file_lock(mapfd);
        deref = std::make_unique<xcow::FileDeref>(mapfd);
        f = std::make_unique<xcow::XcowFile>(deref.get());
        controller = nvme_xcow(vm, arg.sqfds.front(), bfd, f.get(), &clock, &wq);

        sqids = arg.sqids;
        adm_sqfd = arg.adm_sqfd;

        for (auto &sqfd : arg.sqfds) {
            nsqbufs.emplace_back(sqfd, NMNTFY_SQ_DATA_OFFSET);
            ncqbufs.emplace_back(sqfd, NMNTFY_CQ_DATA_OFFSET);
            pollfds.push_back(pollfd{.fd = sqfd, .events = POLLIN});
        }

        if (arg.adm_sqfd >= 0) {
            adm_nsqbuf = nsqbuf_t(arg.adm_sqfd, NMNTFY_SQ_DATA_OFFSET);
            adm_ncqbuf = ncqbuf_t(arg.adm_sqfd, NMNTFY_CQ_DATA_OFFSET);
            pollfds.push_back(pollfd{.fd = arg.adm_sqfd, .events = POLLIN});
        }
    }

    void run() {
        timespec last{};

        while (true) {
            bool succeeded = false, submitted_async = false;
            for (unsigned int ntry = 0; ntry < busypoll_loops; ntry++) {
                for (size_t qi = 0; qi < nsqbufs.size(); qi++) {
                    auto [this_succeeded, this_submitted_async] =
                        queue_poll_once(qi, sqids[qi], nsqbufs[qi], ncqbufs[qi]);
                    succeeded |= this_succeeded;
                    submitted_async |= this_submitted_async;
                }
                if (succeeded) {
                    break;
                }
            }
            if (submitted_async) {
                controller->sq_kick();
            }

            submitted_async = false;
            auto wnd = controller->get_pending_completions(std::span(cqebuf));
            for (auto cqe : wnd.cqes) {
                auto t = static_cast<xcow_ticket *>(controller->cqe_get_data(cqe));
                uint32_t tag = t->tag;
                auto count = --t->count;
                if (!count) {
                    if (cqe->res < 0)
                        printf("unhappy %p %#x %d\n", t, tag, cqe->res);

                    if (tag != noop_tag) {
                        auto [qi, ucid] = unmake_tag(tag);
                        nm_reply reply(tag, translate_uring_status(cqe->res), t->aux);
                        process_reply(ucid, reply, qi == qi_admin ? *adm_ncqbuf : ncqbufs[qi]);
                    }
                    auto vblk = t->locked_cluster;
                    if (cqe->res < 0)
                        // the only ticket->last we have right now is from do_alloc_one_tx
                        // refuse commit
                        t->last.neutralize();
                    // trigger ticket->last before draining cluster locking work queue
                    delete t;
                    if (vblk != UINT64_MAX) {
                        clock[vblk] = false;
                        while (auto we = wq.extract(vblk)) {
                            auto outcome = we.mapped()(cqe->res);
                            if (std::holds_alternative<xcow_ticket *>(outcome)) {
                                submitted_async = true;
                                break;
                            } else if (std::holds_alternative<nm_reply>(outcome)) {
                                const auto &reply = std::get<nm_reply>(outcome);
                                auto [qi, ucid] = unmake_tag(reply.tag);
                                if (qi != qi_invalid)
                                    process_reply(ucid, reply, qi == qi_admin ? *adm_ncqbuf : ncqbufs[qi]);
                            }
                        }
                    }
                }
            }
            controller->commit_completions(wnd);
            if (submitted_async) {
                controller->sq_kick();
            }

            if (succeeded) {
                clock_gettime(CLOCK_MONOTONIC_COARSE, &last);
            } else {
                if (adm_sqfd >= 0) {
                    auto [this_succeeded, this_submitted_async] =
                        queue_poll_once(qi_admin, 0, *adm_nsqbuf, *adm_ncqbuf);
                    if (this_submitted_async)
                        controller->sq_kick();
                }

                timespec now{};
                clock_gettime(CLOCK_MONOTONIC_COARSE, &now);
                if (now - last > 1000000l * busypoll_ms) {
                    // timeout
                    for (auto &pfd : pollfds) {
                        pfd.events = POLLIN;
                    }
                    if (poll(pollfds.data(), pollfds.size(), sleeppoll_ms) < 0) {
                        throw std::system_error(errno, std::generic_category(), "cannot poll queues");
                    }
                }
            }
        }
    }
};

// this function forces all worker allocations to happen within its own thread
static void worker_func(worker_arg arg) {
    worker w(arg);
    w.run();
}

int main(int argc, char **argv) { // NOLINT
    setbuf(stdout, NULL);

    const char *arg_iommu_group = nullptr;
    const char *arg_mdev_uuid = nullptr;
    const char *arg_memfile = nullptr;
    const char *arg_mapfile = nullptr;
    const char *arg_blkdev = nullptr;
    size_t nthreads = 1;
    size_t arg_below_4g_mem_size = 2ull << 30;
    int o = 0;

    while ((o = getopt(argc, argv, "g:d:m:M:Fb:j:l:")) != -1) {
        switch (o) {
        case 'g':
            arg_iommu_group = optarg;
            break;
        case 'd':
            arg_mdev_uuid = optarg;
            break;
        case 'm':
            arg_memfile = optarg;
            break;
        case 'M':
            arg_mapfile = optarg;
            break;
        case 'b':
            arg_blkdev = optarg;
            break;
        case 'j':
            nthreads = static_cast<size_t>(atoi(optarg));
            break;
        case 'l':
            arg_below_4g_mem_size = strtoull(optarg, NULL, 0);
            break;
        default:
            printf("unknown flag %d\n", o);
            return 1;
        }
    }

    if (!arg_iommu_group || !arg_mdev_uuid || !arg_memfile || !arg_mapfile || !arg_blkdev) {
        fprintf(stderr, "bad usage\n");
        return 1;
    }

    std::vector<int> sqfds(MAX_VIRTUAL_QUEUES);
    if (mdev_open(arg_iommu_group, arg_mdev_uuid, sqfds) < 0) {
        return 1;
    }
    auto hsq = cleanup([&] {
        for (auto i = sqfds.begin(); i != sqfds.end(); i++) {
            if (*i >= 0) {
                close(*i);
            }
        }
    });

    int mfd = open(arg_memfile, O_RDWR);
    if (mfd < 0) {
        throw std::system_error(errno, std::generic_category(), "cannot open mem file");
    }
    size_t pvm_size = 0;
    void *pvm = mdev_vm_mmap(mfd, arg_below_4g_mem_size, pvm_size);

    if (nthreads == 0 || nthreads > sqfds.size()) {
        nthreads = sqfds.size();
    }

    std::vector<std::vector<size_t>> worker_sqids(nthreads);
    std::vector<std::vector<int>> worker_sqfds(nthreads);
    size_t active = 0;
    for (size_t i = 1; i < sqfds.size(); i++) {
        if (sqfds[i] >= 0) {
            worker_sqids[active % nthreads].push_back(i);
            worker_sqfds[active % nthreads].push_back(sqfds[i]);
            active++;
        }
    }

    std::vector<std::thread> workers;
    for (size_t tid = 0; tid < nthreads; tid++) {
        auto &t = workers.emplace_back(
            worker_func,
            worker_arg{
                .sqids = worker_sqids[tid],
                .sqfds = worker_sqfds[tid],
                .mapfile = arg_mapfile,
                .blkdev = arg_blkdev,
                .below_4g_mem_size = arg_below_4g_mem_size,
                .mfd = mfd,
                .pvm = static_cast<unsigned char *>(pvm),
                .pvm_size = pvm_size,
                .adm_sqfd = !tid ? sqfds[0] : -1,
            });

        std::ostringstream tn;
        tn << "worker" << tid;
        pthread_setname_np(t.native_handle(), tn.str().c_str());
    }

    for (auto &t : workers) {
        t.join();
    }

    return 0;
}

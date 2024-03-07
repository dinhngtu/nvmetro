#include <array>
#include <bit>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <sys/poll.h>
#include <vector>
#include <thread>
#include <functional>
#include <sstream>
#include <span>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <emmintrin.h>

#include "util.hpp"
#include "cmdbuf.hpp"
#include "nvme_sender.hpp"
#include "util/mdev.hpp"
#include "util/time.hpp"

constexpr size_t MAX_VIRTUAL_QUEUES = 16;
constexpr size_t NOTIFYFD_BURST = 16;
constexpr int sleeppoll_ms = 100;
constexpr long busypoll_ms = 500;
constexpr unsigned int busypoll_loops = 20;

static void worker_func(
    std::vector<size_t> sqids,
    std::vector<int> sqfds,
    const char *arg_blkdev,
    unsigned char *pvm,
    off_t pvm_size) {
    if (!pvm || !pvm_size || sqids.empty() || sqids.size() != sqfds.size()) {
        return;
    }
    auto vm = std::make_shared<mapping>(pvm, pvm_size);
    int bfd = open(arg_blkdev, O_RDWR | O_DIRECT);
    if (bfd < 0) {
        throw std::system_error(errno, std::generic_category(), "cannot open blkdev file");
    }

    nvme_sender controller(vm, sqfds.front(), bfd);

    std::vector<nsqbuf_t> nsqbuf;
    std::vector<ncqbuf_t> ncqbuf;
    std::vector<struct pollfd> pollfds;
    timespec last{};

    std::vector<struct nvme_command> cmds(NOTIFYFD_BURST);
    std::span<struct nvme_command> cmdspan(cmds);
    std::vector<struct nmntfy_response> resps(NOTIFYFD_BURST);

    for (auto &sqfd : sqfds) {
        nsqbuf.emplace_back(sqfd, NMNTFY_SQ_DATA_OFFSET);
        ncqbuf.emplace_back(sqfd, NMNTFY_CQ_DATA_OFFSET);
        pollfds.push_back(pollfd{.fd = sqfd, .events = POLLIN});
    }

    while (true) {
        bool succeeded = false;
        for (unsigned int ntry = 0; ntry < busypoll_loops; ntry++) {
            for (size_t i = 0; i < sqfds.size(); i++) {
                int new_tail = 0;
                int ncmds = std::min(static_cast<int>(cmdspan.size()), nsqbuf[i].peek_items(new_tail));
                if (ncmds) {
                    succeeded = true;
                    nsqbuf[i].consume_raw(cmdspan, new_tail, ncmds);
                    for (int j = 0; j < ncmds; j++) {
                        resps[j].ucid = cmds[j].common.command_id;
                        resps[j].status = controller.receive(sqids[i], cmds[j]);
                    }
                    int remaining = ncmds;
                    while (remaining) {
                        auto processed = ncqbuf[i].produce(std::span(resps).subspan(ncmds - remaining, remaining));
                        remaining -= processed;
                        if (remaining) {
                            _mm_pause();
                        }
                    }
                }
            }
        }

        if (succeeded) {
            clock_gettime(CLOCK_MONOTONIC_COARSE, &last);
        } else {
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

int main(int argc, char **argv) { // NOLINT
    setbuf(stdout, NULL);

    const char *arg_iommu_group = nullptr;
    const char *arg_mdev_uuid = nullptr;
    const char *arg_memfile = nullptr;
    const char *arg_blkdev = nullptr;
    size_t nthreads = 0;
    size_t arg_below_4g_mem_size = 2ull << 30;
    int o = 0;

    while ((o = getopt(argc, argv, "g:d:m:b:e:B:k:j:il:")) != -1) {
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

    if (!arg_iommu_group || !arg_mdev_uuid || !arg_memfile || !arg_blkdev) {
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
    for (size_t i = 1; i < sqfds.size(); i++) {
        worker_sqids[(i - 1) % nthreads].push_back(i);
        worker_sqfds[(i - 1) % nthreads].push_back(sqfds[i]);
    }

    std::vector<std::thread> workers;
    for (size_t tid = 0; tid < nthreads; tid++) {
        auto &t = workers.emplace_back(
            worker_func,
            worker_sqids[tid],
            worker_sqfds[tid],
            arg_blkdev,
            static_cast<unsigned char *>(pvm),
            pvm_size);

        std::ostringstream tn;
        tn << "worker" << tid;
        pthread_setname_np(t.native_handle(), tn.str().c_str());
    }

    for (auto &t : workers) {
        t.join();
    }

    return 0;
}

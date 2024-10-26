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
#include "util/mdev.hpp"
#include "util/time.hpp"
#include "util/uring.hpp"
#include "nvme_encryptor_sgx_aio.hpp"

constexpr size_t MAX_VIRTUAL_QUEUES = 16;
constexpr size_t NOTIFYFD_BURST = 16;
constexpr size_t COMPLETION_BURST = 128;
constexpr int sleeppoll_ms = 100;
constexpr long busypoll_ms = 500;
constexpr unsigned int busypoll_loops = 20;
static const std::string esopath = "../encryptor-sgx/enclave.signed.so";

static void worker_func(
    std::vector<size_t> sqids,
    std::vector<int> sqfds,
    const char *arg_blkdev,
    int arg_lba_shift,
    const std::array<unsigned char, 32> &key,
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

    nvme_encryptor_sgx_aio controller(vm, sqfds.front(), bfd, esopath, false, key, arg_lba_shift);

    std::vector<nsqbuf_t> nsqbuf;
    std::vector<ncqbuf_t> ncqbuf;
    std::vector<pollfd> pollfds;
    timespec last{};

    std::array<nvme_command, NOTIFYFD_BURST> cmds{};
    std::span<nvme_command> cmdspan(cmds);
    std::array<io_uring_cqe *, COMPLETION_BURST> cqebuf{};

    for (auto &sqfd : sqfds) {
        nsqbuf.emplace_back(sqfd, NMNTFY_SQ_DATA_OFFSET);
        ncqbuf.emplace_back(sqfd, NMNTFY_CQ_DATA_OFFSET);
        pollfds.push_back(pollfd{.fd = sqfd, .events = POLLIN});
    }

    while (true) {
        bool succeeded = false, submitted_async = false;
        for (unsigned int ntry = 0; ntry < busypoll_loops; ntry++) {
            for (size_t qi = 0; qi < sqfds.size(); qi++) {
                int new_tail = 0;
                int ncmds = std::min(static_cast<int>(cmdspan.size()), nsqbuf[qi].peek_items(new_tail));
                if (ncmds) {
                    succeeded = true;
                    nsqbuf[qi].consume_raw(cmdspan, new_tail, ncmds);
                    for (int j = 0; j < ncmds; j++) {
                        auto tag = make_tag(qi, cmds[j].common.command_id);
                        __u16 status = 0;
                        auto async = controller.submit_async(sqids[qi], cmds[j], tag, status);
                        if (async) {
                            submitted_async = true;
                        } else {
                            nmntfy_response resp{
                                .ucid = cmds[j].common.command_id,
                                .status = status,
                            };
                            cq_produce_one(ncqbuf[qi], resp);
                        }
                    }
                }
            }
            if (succeeded) {
                break;
            }
        }
        if (submitted_async) {
            controller.sq_kick();
        }

        auto wnd = controller.get_pending_completions(std::span(cqebuf));
        for (auto cqe : wnd.cqes) {
            auto t = static_cast<sq_ticket *>(controller.cqe_get_data(cqe));
            auto [qi, ucid] = unmake_tag(t->tag);
            delete t;

            auto status = cqe->res < 0 ? (NVME_SC_DNR | NVME_SC_INTERNAL) : NVME_SC_SUCCESS;
            nmntfy_response resp{
                .ucid = ucid,
                .status = static_cast<__u16>(status),
            };
            cq_produce_one(ncqbuf[qi], resp);
            controller.commit_completion(cqe);
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
    int arg_lba_shift = 9;
    const char *arg_keyfile = nullptr;
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
        case 'S':
            arg_lba_shift = atoi(optarg);
            break;
        case 'k':
            arg_keyfile = optarg;
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

    if (!arg_iommu_group || !arg_mdev_uuid || !arg_memfile || !arg_blkdev || !arg_keyfile) {
        fprintf(stderr, "bad usage\n");
        return 1;
    }

    std::array<unsigned char, 32> key{};
    {
        int kfd = open(arg_keyfile, O_RDONLY);
        if (kfd < 0) {
            throw std::system_error(errno, std::generic_category(), "cannot open key file");
        }
        auto hkfd = cleanup([&] { close(kfd); });

        if (read(kfd, key.data(), key.size()) != key.size()) {
            throw std::system_error(errno, std::generic_category(), "cannot read key file");
        }
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
            arg_lba_shift,
            key,
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

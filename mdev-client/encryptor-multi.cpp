#include <array>
#include <bit>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <memory>
#include <sys/poll.h>
#include <vector>
#include <thread>
#include <functional>
#include <sstream>
#include <span>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <boost/range/combine.hpp>

#include "cxxopts.hpp"
#include "util.hpp"
#include "cmdbuf.hpp"
#include "aligned_allocator.hpp"
#include "nvme_encryptor_multi.hpp"
#include "crypto/aes_xts_libcrypto.hpp"
#include "crypto/aes_xts_ipp.hpp"
#include "util/mdev.hpp"
#include "util/time.hpp"
#include "util/uring.hpp"

constexpr size_t MAX_VIRTUAL_QUEUES = 16;
constexpr size_t NOTIFYFD_BURST = 16;
constexpr size_t COMPLETION_BURST = 128;
constexpr int sleeppoll_ms = 100;
constexpr long busypoll_ms = 500;
constexpr unsigned int busypoll_loops = 20;

struct worker_ctx {
    size_t sqid;
    int sqfd;
    std::shared_ptr<uring> bring;
    std::shared_ptr<mapping> vm;
};

static cxxopts::Options make_options() {
    cxxopts::Options opt{"encryptor-multi"};
    auto g = opt.add_options();
    g("g,iommu-group", "mdev iommu group", cxxopts::value<std::vector<std::string>>());
    g("d,mdev-uuid", "mdev uuid", cxxopts::value<std::vector<std::string>>());
    g("m,memfile", "vm memfile", cxxopts::value<std::vector<std::string>>());
    g("b,blkdev", "backend block device", cxxopts::value<std::vector<std::string>>());
    g("e,crypto-impl", "crypto type", cxxopts::value<std::string>()->default_value("ippcp"));
    g("B,block-size", "blocksize", cxxopts::value<size_t>()->default_value("512"));
    g("k,keyfile", "keyfile", cxxopts::value<std::string>());
    g("j", "number of threads", cxxopts::value<size_t>()->default_value("1"));
    g("l,lowmem-size", "below-4G VM mem size", cxxopts::value<size_t>()->default_value("2147483648"));
    return opt;
}

static std::shared_ptr<tweakable_block_cipher> make_engine(
    const std::array<unsigned char, 32> &key,
    const char *arg_crypto_impl,
    size_t arg_block_size) {
    if (!strcmp("libcrypto", arg_crypto_impl)) {
        return std::make_shared<aes_xts_libcrypto>(key);
    } else {
        return std::make_shared<aes_xts_ipp>(key, arg_block_size);
    }
}

static void worker_func(
    std::vector<worker_ctx> contexts,
    const char *arg_crypto_impl,
    size_t arg_block_size,
    const std::array<unsigned char, 32> &key) {
    auto engine = make_engine(key, arg_crypto_impl, arg_block_size);

    std::vector<nsqbuf_t> nsqbuf;
    std::vector<ncqbuf_t> ncqbuf;
    std::vector<pollfd> pollfds;
    std::vector<nvme_encryptor_multi> controllers;
    timespec last{};

    std::array<nvme_command, NOTIFYFD_BURST> cmds{};
    std::span<nvme_command> cmdspan(cmds);
    std::array<io_uring_cqe *, COMPLETION_BURST> cqebuf{};
    aligned_allocator<unsigned char, NVME_PAGE_SIZE> allocator{};

    for (auto &ctx : contexts) {
        nsqbuf.emplace_back(ctx.sqfd, NMNTFY_SQ_DATA_OFFSET);
        ncqbuf.emplace_back(ctx.sqfd, NMNTFY_CQ_DATA_OFFSET);
        pollfds.push_back(pollfd{.fd = ctx.sqfd, .events = POLLIN});
        controllers.emplace_back(ctx.vm, ctx.sqfd, ctx.bring, engine);
    }

    std::vector<int> submitted_async(contexts.size());
    while (true) {
        bool succeeded = false;
        submitted_async.assign(submitted_async.size(), 0);
        for (unsigned int ntry = 0; ntry < busypoll_loops; ntry++) {
            for (size_t qi = 0; qi < contexts.size(); qi++) {
                int new_tail = 0;
                int ncmds = std::min(static_cast<int>(cmdspan.size()), nsqbuf[qi].peek_items(new_tail));
                if (ncmds) {
                    succeeded = true;
                    nsqbuf[qi].consume_raw(cmdspan, new_tail, ncmds);
                    for (int j = 0; j < ncmds; j++) {
                        auto tag = make_tag(qi, cmds[j].common.command_id);
                        __u16 status = 0;
                        auto async = controllers[qi].submit_async(contexts[qi].sqid, cmds[j], tag, status);
                        if (async) {
                            submitted_async[qi]++;
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
        for (size_t qi = 0; qi < contexts.size(); qi++) {
            if (submitted_async[qi]) {
                controllers[qi].sq_kick();
            }
        }

        for (auto &controller : controllers) {
            auto wnd = controller.get_pending_completions(std::span(cqebuf));
            for (auto cqe : wnd.cqes) {
                auto t = controller.cqe_get_data(cqe);
                auto [qi, ucid] = unmake_tag(t->tag);
                if (t->category == sq_ticket_category::iovec) {
                    auto it = static_cast<iovec_ticket *>(t); // NOLINT
                    for (auto &v : it->iovecs) {
                        allocator.deallocate(static_cast<unsigned char *>(v.iov_base), v.iov_len);
                    }
                }
                delete t;

                auto status = cqe->res < 0 ? (NVME_SC_DNR | NVME_SC_INTERNAL) : NVME_SC_SUCCESS;
                nmntfy_response resp{
                    .ucid = ucid,
                    .status = static_cast<__u16>(status),
                };
                cq_produce_one(ncqbuf[qi], resp);
                controller.commit_completion(cqe);
            }
        }

        if (succeeded) {
            clock_gettime(CLOCK_MONOTONIC_COARSE, &last);
        } else {
            timespec now{};
            clock_gettime(CLOCK_MONOTONIC_COARSE, &now);
            if (now - last > 1000000l * busypoll_ms) {
                if (poll(pollfds.data(), pollfds.size(), sleeppoll_ms) < 0) {
                    throw std::system_error(errno, std::generic_category(), "cannot poll queues");
                }
            }
        }
    }
}

int main(int argc, char **argv) { // NOLINT
    setbuf(stdout, NULL);

    auto argm = make_options().parse(argc, argv);

    if (!argm.count("iommu-group") ||                           //
        argm.count("iommu-group") != argm.count("mdev-uuid") || //
        argm.count("iommu-group") != argm.count("memfile") ||   //
        argm.count("iommu-group") != argm.count("blkdev")) {
        fprintf(stderr, "bad usage\n");
        return 1;
    }

    std::array<unsigned char, 32> key{};
    {
        int kfd = open(argm["keyfile"].as<std::string>().c_str(), O_RDONLY);
        if (kfd < 0) {
            throw std::system_error(errno, std::generic_category(), "cannot open key file");
        }
        auto hkfd = cleanup([&] { close(kfd); });

        if (read(kfd, key.data(), key.size()) != key.size()) {
            throw std::system_error(errno, std::generic_category(), "cannot read key file");
        }
    }

    auto nthreads = argm["j"].as<size_t>();
    std::vector<std::vector<worker_ctx>> contexts(nthreads);

    auto arg_iommu_groups = argm["iommu-group"].as<std::vector<std::string>>();
    auto arg_mdev_uuids = argm["mdev-uuid"].as<std::vector<std::string>>();
    auto arg_memfiles = argm["memfile"].as<std::vector<std::string>>();
    auto arg_blkdevs = argm["blkdev"].as<std::vector<std::string>>();

    int counter = 0;
    int blkfd = -1;
    for (auto t : boost::combine(arg_iommu_groups, arg_mdev_uuids, arg_memfiles, arg_blkdevs)) {
        std::string iommu_group, mdev_uuid, memfile, blkdev;
        boost::tie(iommu_group, mdev_uuid, memfile, blkdev) = t;

        std::vector<int> sqfds(MAX_VIRTUAL_QUEUES);
        if (mdev_open(iommu_group.c_str(), mdev_uuid.c_str(), sqfds) < 0) {
            return 1;
        }

        int mfd = open(memfile.c_str(), O_RDWR);
        if (mfd < 0) {
            throw std::system_error(errno, std::generic_category(), "cannot open mem file");
        }
        size_t pvm_size = 0;
        void *pvm = mdev_vm_mmap(mfd, argm["lowmem-size"].as<size_t>(), pvm_size);
        auto vm = std::make_shared<mapping>(static_cast<unsigned char *>(pvm), pvm_size);

        blkfd = open(blkdev.c_str(), O_RDWR);
        if (blkfd < 0) {
            throw std::system_error(errno, std::generic_category(), "cannot open blkdev file");
        }
        auto bring = std::make_shared<uring>(2048, 0, std::span(&blkfd, 1));

        for (size_t i = 1; i < sqfds.size(); i++) {
            contexts[counter % nthreads].push_back({i, sqfds[i], bring, vm});
            counter++;
        }
    }

    std::vector<std::thread> workers;
    for (size_t tid = 0; tid < nthreads; tid++) {
        auto &t = workers.emplace_back(
            worker_func,
            contexts[tid],
            argm["crypto-impl"].as<std::string>().c_str(),
            argm["block-size"].as<size_t>(),
            key);

        std::ostringstream tn;
        tn << "worker" << tid;
        pthread_setname_np(t.native_handle(), tn.str().c_str());
    }

    for (auto &t : workers) {
        t.join();
    }

    return 0;
}

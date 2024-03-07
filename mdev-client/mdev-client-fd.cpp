#include <cstring>
#include <cstdio>
#include <iostream>
#include <vector>
#include <span>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <linux/nvme_mdev.h>
#include <linux/vfio.h>
#include <emmintrin.h>

#include "util.hpp"
#include "util/mdev.hpp"
#include "nvme.hpp"
#include "cmdbuf.hpp"

constexpr size_t MAX_VIRTUAL_QUEUES = 16;
constexpr size_t NOTIFYFD_BURST = 16;

int main(int _argc, char **_argv) { // NOLINT
    std::span arg(_argv, _argc);

    setbuf(stdout, NULL);

    if (arg.size() != 4) {
        fprintf(stderr, "usage: %s <path to vfio group> <mdev uuid> <memfile>\n", arg[0]);
        return 1;
    }

    char *arg_iommu_group = arg[1];
    char *arg_mdev_uuid = arg[2];
    char *arg_memfile = arg[3];

    std::vector<struct pollfd> sqfd(MAX_VIRTUAL_QUEUES);
    if (mdev_open(arg_iommu_group, arg_mdev_uuid, sqfd) < 0) {
        return 1;
    }
    auto hsq = cleanup([&] {
        for (auto i = sqfd.begin(); i != sqfd.end(); i++) {
            if (i->fd >= 0) {
                close(i->fd);
            }
        }
    });

    std::vector<nsqbuf_t> nsqbuf;
    for (auto &sq : sqfd) {
        nsqbuf.emplace_back(sq.fd, NMNTFY_SQ_DATA_OFFSET);
    }

    std::vector<ncqbuf_t> ncqbuf;
    for (auto &sq : sqfd) {
        ncqbuf.emplace_back(sq.fd, NMNTFY_CQ_DATA_OFFSET);
    }

    int fd = open(arg_memfile, O_RDONLY);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), "cannot open mem file");
    }
    struct stat fs {};
    if (fstat(fd, &fs) < 0) {
        throw std::system_error(errno, std::generic_category(), "cannot stat mem file");
    }

    void *pvm = mmap(NULL, fs.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (pvm == MAP_FAILED) { // NOLINT
        throw std::system_error(errno, std::generic_category(), "cannot mmap mem file");
    }

    if (madvise(pvm, fs.st_size, MADV_DONTDUMP) < 0) {
        throw std::system_error(errno, std::generic_category(), "cannot madvise(MADV_DONTDUMP) mem file");
    }

    auto vm = std::make_shared<mapping>(static_cast<unsigned char *>(pvm), fs.st_size);
    auto controller = std::make_unique<nvme>(vm, sqfd[1].fd);

    std::vector<struct nvme_command> cmds(NOTIFYFD_BURST);
    std::span<struct nvme_command> cmdspan(cmds);
    std::vector<struct nmntfy_response> resps(NOTIFYFD_BURST);
    while (true) {
        if (poll(&sqfd[0], sqfd.size(), -1) < 0) {
            throw std::system_error(errno, std::generic_category(), "cannot poll queues");
        }
        for (size_t sq = 0; sq < sqfd.size(); sq++) {
            auto &f = sqfd[sq];
            if (f.fd < 0) {
                continue;
            }
            if (f.revents & POLLRDHUP) {
                // consider this queue dead, don't bother with it any more
                // close it now since hsq won't close it for us later
                fprintf(stderr, "queue %zu failed with RDHUP\n", sq);
                close(f.fd);
                f.fd = -f.fd;
                continue;
            }
            if (f.revents & POLLIN) {
                int ncmds = nsqbuf[sq].consume(cmdspan);
                for (int j = 0; j < ncmds; j++) {
                    controller->receive(sq, cmds[j]);
                    resps[j].sqid = sq;
                    resps[j].ucid = cmds[j].common.command_id;
                    resps[j].status = NVME_SC_SUCCESS;
                }
                int remaining = ncmds;
                while (remaining) {
                    auto processed = ncqbuf[sq].produce(std::span(resps).subspan(ncmds - remaining, remaining));
                    remaining -= processed;
                    if (remaining) {
                        _mm_pause();
                    }
                }
            }
        }
    }
    return 0;
}

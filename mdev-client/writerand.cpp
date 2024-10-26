#include <cstddef>
#include <system_error>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <algorithm>
#include <random>

#include <unistd.h>
#include <sys/uio.h>
#include <fmt/format.h>
#include "cxxopts.hpp"

#include "crypto/aes_xts_ipp.hpp"
#include "fildes.hpp"

struct writerand_arg {
    size_t blksize;
    size_t fsize;
    size_t nslices;
    size_t nskips;
    double pct;
    size_t nblocks;
    int bfd;
    std::array<unsigned char, 32> key;
    std::atomic<size_t> idx;
    std::atomic<size_t> written_bytes;
};

static cxxopts::Options make_options() {
    cxxopts::Options opt{"writerand"};
    auto g = opt.add_options();
    g("b,blkdev", "block data file", cxxopts::value<std::string>());
    g("z,fsize", "how much to write", cxxopts::value<size_t>());
    g("B,block-size", "blocksize", cxxopts::value<size_t>()->default_value("512"));
    g("k,keyfile", "keyfile", cxxopts::value<std::string>());
    g("p,nslices", "number of blocks per write", cxxopts::value<size_t>()->default_value("1"));
    g("s,nskips", "number of skipped blocks per write", cxxopts::value<size_t>()->default_value("0"));
    g("P,pct", "target proportion of written blocks", cxxopts::value<double>()->default_value("1.0"));
    g("j,nthreads", "number of worker threads", cxxopts::value<size_t>()->default_value("1"));
    return opt;
}

static void worker_func(writerand_arg *arg) {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_real_distribution<> dis(0.0, 1.0);

    aes_xts_ipp engine(arg->key, arg->blksize);

    std::vector<unsigned char> zeroes(arg->blksize);
    std::vector<std::vector<unsigned char>> slices;
    std::vector<iovec> iov;
    for (size_t i = 0; i < arg->nslices; i++) {
        auto &slice = slices.emplace_back(arg->blksize);
        iov.push_back({slice.data(), slice.size()});
    }

    while (1) {
        size_t idx = arg->idx.fetch_add(arg->nslices + arg->nskips);
        if (idx >= arg->nblocks)
            break;
        if (dis(gen) < arg->pct)
            continue;
        auto this_nblocks = std::min(arg->nslices, arg->nblocks - idx);
        for (size_t b = 0; b < this_nblocks; b++)
            if (!engine.encrypt(slices[b], zeroes, idx + this_nblocks))
                throw std::runtime_error("cannot encrypt");
        auto ret = pwritev(arg->bfd, iov.data(), this_nblocks, idx * arg->blksize);
        if (ret < 0)
            throw std::system_error(errno, std::generic_category(), "write failed");
        arg->written_bytes += ret;
    }
}

int main(int argc, char **argv) {
    auto opts = make_options();
    cxxopts::ParseResult argm;

    try {
        argm = opts.parse(argc, argv);
        if (!argm.count("blkdev") || !argm.count("fsize"))
            throw std::runtime_error("bad usage");
    } catch (const std::exception &) {
        fmt::print("{}\n", opts.help());
        return 1;
    }

    auto arg = new writerand_arg;
    arg->blksize = argm["block-size"].as<size_t>();
    arg->fsize = argm["fsize"].as<size_t>();
    arg->nslices = argm["nslices"].as<size_t>();
    arg->nskips = argm["nskips"].as<size_t>();
    arg->pct = std::clamp(argm["pct"].as<double>(), 0.0, 1.0);
    arg->nblocks = arg->fsize / arg->blksize;
    if (!arg->nblocks || !arg->nslices)
        throw std::runtime_error("invalid block sizes");

    auto nthreads = argm["nthreads"].as<size_t>();
    if (!nthreads)
        throw std::runtime_error("invalid number of threads");

    auto blkdev = argm["blkdev"].as<std::string>();
    FileDescriptor bfd(blkdev.c_str(), O_RDWR);
    if (bfd.err())
        throw std::system_error(bfd.err(), std::generic_category(), "cannot open blkdev");
    arg->bfd = bfd;

    {
        auto keyfile = argm["keyfile"].as<std::string>();
        FileDescriptor kfd(keyfile.c_str(), O_RDONLY);
        if (kfd.err())
            throw std::system_error(kfd.err(), std::generic_category(), "cannot open key file");
        if (read(kfd, arg->key.data(), arg->key.size()) != arg->key.size())
            throw std::system_error(errno, std::generic_category(), "cannot read key file");
    }

    arg->idx = 0;
    arg->written_bytes = 0;

    std::vector<std::thread> workers;
    for (size_t tid = 0; tid < nthreads; tid++)
        workers.emplace_back(worker_func, arg);
    for (auto &t : workers)
        t.join();

    fmt::print("written {} bytes\n", arg->written_bytes);

    return 0;
}

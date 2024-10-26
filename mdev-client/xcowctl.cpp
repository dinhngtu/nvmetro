#include <cstdio>
#include <string>
#include <system_error>
#include <fmt/format.h>
#include <fcntl.h>
#include "cxxopts.hpp"
#include "fildes.hpp"
#include "xcow/xcow_file.hpp"
#include "xcow/file_deref.hpp"

using namespace xcow;

#define DECODE(ref, decoder) (fmt::format("{:x}->{:x}", (ref).val, (decoder)(ref)))

static cxxopts::Options make_options() {
    cxxopts::Options opt{"xcowctl"};
    auto g = opt.add_options();
    g("M,mapfile", "map file", cxxopts::value<std::string>());
    g("o,operation", "operation", cxxopts::value<std::string>());
    g("s,snap-index", "snapshot index", cxxopts::value<int>());
    g("b,blkdev", "block data file", cxxopts::value<std::string>());
    g("z,fsize", "new file size", cxxopts::value<size_t>());
    g("C,cluster-bits", "new cluster bits", cxxopts::value<uint32_t>()->default_value("16"));
    return opt;
}

int main(int argc, char **argv) {
    auto opts = make_options();
    cxxopts::ParseResult argm;

    try {
        argm = opts.parse(argc, argv);
    } catch (const std::exception &) {
        fmt::print("{}\n", opts.help());
        return 1;
    }

    auto mapfile = argm["mapfile"].as<std::string>();
    FileDescriptor mapfd(mapfile.c_str(), O_RDWR);
    if (mapfd.err())
        throw std::system_error(mapfd.err(), std::generic_category(), "cannot open mapfile");
    auto flk = file_lock(mapfd);
    FileDeref deref(mapfd);
    std::unique_ptr<XcowFile> f;

    auto op = argm["operation"].as<std::string>();
    if (op == "format") {
        auto fsize = argm["fsize"].as<size_t>();
        auto cluster_bits = argm["cluster-bits"].as<uint32_t>();

        auto blkdev = argm["blkdev"].as<std::string>();
        FileDescriptor bfd(blkdev.c_str(), O_RDWR);
        if (bfd.err())
            throw std::system_error(bfd.err(), std::generic_category(), "cannot open blkdev");

        auto metasize = lseek(mapfd, 0, SEEK_END);
        if (metasize < 0)
            throw std::system_error(errno, std::generic_category(), "metasize");
        auto disksize = lseek(bfd, 0, SEEK_END);
        if (disksize < 0)
            throw std::system_error(errno, std::generic_category(), "disksize");

        if (fallocate(bfd, FALLOC_FL_ZERO_RANGE, 0, 1l << cluster_bits) < 0)
            throw std::system_error(errno, std::generic_category(), "fallocate");

        f = std::make_unique<XcowFile>(
            XcowFile::format(&deref, fsize, cluster_bits, metasize >> cluster_bits, disksize >> cluster_bits));

    } else {
        f = std::make_unique<XcowFile>(&deref);
        if (op == "snap-list") {
            size_t count = 0;
            for (auto it = f->snaps(); !it.at_end(); it++)
                fmt::print(
                    "snap {}: {} disk_hwm={}\n",
                    count++,
                    DECODE(*it, FileBits::decode_snap_entry),
                    it->disk_hwm);

        } else if (op == "snap-create") {
            int idx = 0;
            if (argm.count("snap-index"))
                idx = argm["snap-index"].as<int>();
            auto snap = f->open_read(idx);
            f->snap_create(snap);

        } else if (op == "snap-peel") {
            auto snap = f->open_write();
            f->peel(std::move(snap));

        } else if (op == "snap-recreate") {
            {
                auto snap = f->open_write();
                f->peel(std::move(snap));
            }
            {
                int idx = 0;
                if (argm.count("snap-index"))
                    idx = argm["snap-index"].as<int>();
                auto snap = f->open_read(idx);
                auto new_snap = f->snap_create(snap);
                fmt::print("{}\n", new_snap.disk_hwm());
            }

        } else {
            throw std::runtime_error("unknown operation");
        }
    }

    return 0;
}

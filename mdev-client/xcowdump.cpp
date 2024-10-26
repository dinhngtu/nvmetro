#include <cstdio>
#include <string>
#include <system_error>
#include <optional>
#include <fmt/format.h>
#include <fcntl.h>
#include "fildes.hpp"

#define XCOW_TRANSPARENT 1
#include "xcow/xcow_file.hpp"
#include "xcow/file_deref.hpp"

using namespace xcow;

#define DECODE(ref, decoder) (fmt::format("{:x}->{:x}", (ref).val, (decoder)(ref)))
#define SEPARATE fmt::print("========\n")

int main(int argc, char **argv) {
    if (argc != 2) {
        fmt::print("bad usage\n");
        return 1;
    }

    FileDescriptor mapfd(argv[1], O_RDONLY);
    if (mapfd.err())
        throw std::system_error(mapfd.err(), std::generic_category(), "cannot open mapfile");
    std::optional<cleanup> flk;
    try {
        flk = file_lock(mapfd, F_RDLCK);
    } catch (const std::exception &) {
        fmt::print("cannot lock mapfile, dump data might not be reliable\n");
        fmt::print("\n");
    }
    FileDeref deref(mapfd, PROT_READ);
    XcowFile f(&deref);

    fmt::print("fsize={}\n", f.fsize());
    fmt::print("cluster_bits={} ({} bytes)\n", f.cluster_bits(), f.cluster_size());
    fmt::print("hwm_limit={}, disk_hwm_limit={}\n", f._hdr->hwm_limit, f._hdr->disk_hwm_limit);
    fmt::print("hwm={}\n", f._hdr->hwm);
    fmt::print("\n");

    std::vector<SnapListEntry> snaps;
    fmt::print("snaplist={}\n", DECODE(f._hdr->snaplist, LTRefImpl<SnapListRef>::decode));
    SEPARATE;
    {
        std::optional<SnapList> sl = f._snaps;
        while (sl.has_value()) {
            for (auto se = sl->active_entries().rbegin(); se != sl->active_entries().rend(); se++) {
                fmt::print(
                    "snap {}: {} disk_hwm={}\n",
                    snaps.size(),
                    DECODE(*se, FileBits::decode_snap_entry),
                    se->disk_hwm);
                snaps.push_back(*se);
            }
            sl = sl->next(deref);
            SEPARATE;
        }
    }
    fmt::print("\n");

    fmt::print("freelist={}\n", DECODE(f._hdr->freelist, LTRefImpl<FreeListRef>::decode));
    SEPARATE;
    {
        std::optional<FreeList> fl = f._flist;
        while (fl.has_value()) {
            for (auto fe : fl->active_entries()) {
                fmt::print("free: {}\n", DECODE(fe, FileBits::decode_free_entry));
            }
            fl = fl->next(deref);
            SEPARATE;
        }
    }
    fmt::print("\n");

    return 0;
}

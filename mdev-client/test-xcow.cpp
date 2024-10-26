#include <cstdint>
#include <cstdio>
#include <system_error>
#include <sys/mman.h>
#include <catch_amalgamated.hpp>
#include "xcow/xcow_file.hpp"
#include "xcow/xcow_snap.hpp"
#include "xcow/blk_iter.hpp"

using namespace xcow;
using namespace xcow::XlateBits;

static constexpr size_t memsize = 1 << 30;

class MemDeref : public Deref {
public:
    MemDeref(std::span<uint8_t> p) : _p(p) {
    }

    std::span<uint8_t> deref(uint64_t off, size_t nbytes) override {
        return _p.subspan(off, nbytes);
    }

private:
    std::span<uint8_t> _p;
};

static XlateLeaf report_read(XcowSnap &s, uint64_t lba) {
    auto a = s.translate_read(lba * 512);
    // printf("translated R %lx -> %lx\n", lba, a);
    return a;
}

static XlateLeaf report_write(XcowSnap &s, uint64_t lba, XlateLeaf *cow = nullptr) {
    auto a = s.translate_write(lba * 512, cow);
    // printf("translated W %lx -> %lx\n", lba, a);
    return a;
}

TEST_CASE("xcow tests") {
    auto mem = mmap(nullptr, memsize, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (mem == MAP_FAILED) // NOLINT
        throw std::system_error(errno, std::generic_category(), "mmap");

    MemDeref deref(std::span<uint8_t>(static_cast<uint8_t *>(mem), memsize));
    auto f = XcowFile::format(&deref, 50ull << 30, 16, UINT32_MAX, UINT64_MAX);
    auto s1 = f.open_write();

    SECTION("writability") {
        auto a = report_write(s1, 0x12349876);
        REQUIRE(a & XlateBits::writable);
    }

    SECTION("repeatable resolution") {
        auto a = report_write(s1, 0x12349876);
        REQUIRE(a == report_read(s1, 0x12349876));
        REQUIRE(a == report_write(s1, 0x12349876));
    }

    SECTION("same block resolution") {
        auto a = report_write(s1, 0x12349876);
        REQUIRE(a == report_write(s1, 0x1234987f));
    }

    SECTION("begin/end block resolution") {
        auto a = report_write(s1, 0x12349880);
        REQUIRE(a == report_read(s1, 0x12349880 + 127));
    }

    SECTION("child snapshot") {
        auto orig = report_write(s1, 0x12349880);
        auto s2 = f.snap_create(s1);

        auto a = report_read(s2, 0x12349880);
        REQUIRE(a & valid);
        REQUIRE(decode_leaf(a) == decode_leaf(orig));
        XlateLeaf prev;
        auto cowdest = report_write(s2, 0x12349880, &prev);
        REQUIRE(decode_leaf(prev) == decode_leaf(a));
        REQUIRE(decode_leaf(cowdest) != decode_leaf(a));
        REQUIRE(cowdest & writable);
        REQUIRE(!(prev & writable));
        REQUIRE(cowdest == report_read(s2, 0x12349880));
        REQUIRE(cowdest == report_write(s2, 0x12349880));
    }
}

TEST_CASE("metadata tests") {
    auto mem = mmap(nullptr, memsize, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (mem == MAP_FAILED) // NOLINT
        throw std::system_error(errno, std::generic_category(), "mmap");

    MemDeref deref(std::span<uint8_t>(static_cast<uint8_t *>(mem), memsize));
    { XcowFile::format(&deref, 50ull << 30, 16, UINT32_MAX, UINT64_MAX); }

    SECTION("reopen") {
        { XcowFile f(&deref); }
        XcowFile f(&deref);
        REQUIRE(f.fsize() == 50ull << 30);
        REQUIRE(f.cluster_bits() == 16);
    }
}

TEST_CASE("blk_iter tests") {
    SECTION("blk_iter 1") {
        blk_iter bi(215, 1, 7);
        REQUIRE(!bi.at_end());
        REQUIRE(*bi == 215);
        REQUIRE(bi.size() == 1);
        REQUIRE(bi.is_last());
        ++bi;
        REQUIRE(bi.at_end());
        REQUIRE(*bi == 216);
        REQUIRE(bi.size() == 0);
    }

    SECTION("blk_iter 2") {
        blk_iter bi(215, 128, 7);
        REQUIRE(!bi.at_end());
        REQUIRE(*bi == 215);
        REQUIRE(bi.size() == 41);
        REQUIRE(!bi.is_last());
        ++bi;
        REQUIRE(!bi.at_end());
        REQUIRE(*bi == 256);
        REQUIRE(bi.size() == 87);
        REQUIRE(bi.is_last());
        ++bi;
        REQUIRE(bi.at_end());
        REQUIRE(bi.size() == 0);
    }

    SECTION("blk_iter 3") {
        blk_iter bi(384, 128, 7);
        REQUIRE(!bi.at_end());
        REQUIRE(*bi == 384);
        REQUIRE(bi.size() == 128);
        REQUIRE(bi.is_last());
        ++bi;
        REQUIRE(bi.at_end());
        REQUIRE(*bi == 512);
        REQUIRE(bi.size() == 0);
    }

    SECTION("blk_iter 4") {
        blk_iter bi(215, 41, 7);
        REQUIRE(!bi.at_end());
        REQUIRE(*bi == 215);
        REQUIRE(bi.size() == 41);
        REQUIRE(bi.is_last());
        ++bi;
        REQUIRE(bi.at_end());
        REQUIRE(*bi == 256);
        REQUIRE(bi.size() == 0);
    }

    SECTION("blk_iter 5") {
        blk_iter bi(384, 52, 7);
        REQUIRE(!bi.at_end());
        REQUIRE(*bi == 384);
        REQUIRE(bi.size() == 52);
        REQUIRE(bi.is_last());
        ++bi;
        REQUIRE(bi.at_end());
        REQUIRE(*bi == 436);
        REQUIRE(bi.size() == 0);
    }

    SECTION("blk_iter 6") {
        blk_iter bi(384, 256, 7);
        REQUIRE(!bi.at_end());
        REQUIRE(*bi == 384);
        REQUIRE(bi.size() == 128);
        REQUIRE(!bi.is_last());
        ++bi;
        REQUIRE(!bi.at_end());
        REQUIRE(*bi == 512);
        REQUIRE(bi.size() == 128);
        REQUIRE(bi.is_last());
        ++bi;
        REQUIRE(bi.at_end());
        REQUIRE(*bi == 640);
        REQUIRE(bi.size() == 0);
    }
}

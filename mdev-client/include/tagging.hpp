#pragma once

#include <cassert>
#include <cstdint>
#include <cstddef>
#include <utility>
#include <array>
#include <vector>
#include <memory>
#include <span>
#include <sys/uio.h>
#include <mimalloc.h>

static constexpr size_t nmntfy_aux_count() {
    nmntfy_response r;
    return std::size(r.aux);
}

using nmntfy_aux = std::array<__u32, nmntfy_aux_count()>;

static constexpr uint32_t make_tag(uint16_t qi, uint16_t ucid) {
    return (uint32_t)qi << 16 | (uint32_t)ucid;
}

static constexpr uint16_t qi_invalid = 0xffff;
static constexpr uint16_t qi_admin = 0xfffe;

// use an obviously invalid QID=0xffff
static constexpr uint32_t busy_tag = make_tag(qi_invalid, 0xffff);
static constexpr uint32_t noop_tag = make_tag(qi_invalid, 0xfffe);

static constexpr std::pair<uint16_t, uint16_t> unmake_tag(uint32_t tag) {
    return std::make_pair(tag >> 16, tag & 0xffff);
}

struct sq_ticket {
    constexpr sq_ticket(uint32_t _tag) : tag(_tag) {
    }
    virtual ~sq_ticket() = default;
    uint32_t tag;
};

struct multi_ticket : public sq_ticket {
    constexpr multi_ticket(uint32_t _tag) : sq_ticket(_tag), count(0) {
    }
    constexpr multi_ticket(uint32_t _tag, int _count) : sq_ticket(_tag), count(_count) {
    }
    int count;
};

template <typename Family, size_t alignment = 64>
struct mem_ticket final : public Family {
    struct deleter {
        void operator()(unsigned char *p) {
            mi_free(p);
        }
    };

    constexpr mem_ticket(uint32_t _tag, size_t _nbytes)
        : Family(_tag), nbytes(_nbytes), mem(static_cast<unsigned char *>(mi_new_aligned(nbytes, alignment))) {
    }
    size_t nbytes;
    std::unique_ptr<unsigned char[], deleter> mem;
};

template <typename Family, size_t alignment = 64>
class mem_ticket_fast final : public Family {
public:
    size_t nbytes;
    unsigned char *mem;

    static mem_ticket_fast *create(uint32_t _tag, size_t _nbytes) {
        auto size = round_up(_nbytes, alignment);
        auto p = static_cast<unsigned char *>(mi_new_aligned(size + sizeof(mem_ticket_fast), alignment));
        return new (p + size) mem_ticket_fast(_tag, size, p);
    }

    void operator delete(mem_ticket_fast *self, std::destroying_delete_t) {
        auto m = reinterpret_cast<unsigned char *>(self) - self->nbytes;
        self->~mem_ticket_fast();
        mi_free(m);
    }

private:
    constexpr mem_ticket_fast(uint32_t _tag, size_t _nbytes, unsigned char *_mem)
        : Family(_tag), nbytes(_nbytes), mem(_mem) {
    }
};

template <typename Family>
struct iovec_ticket_fixed : public Family {
    constexpr iovec_ticket_fixed(uint32_t _tag) : Family(_tag) {
    }
    std::array<iovec, 2> iovecs{};
};

template <typename Family>
struct iovec_ticket : public Family {
    static constexpr size_t reserve_count = 17;
    iovec_ticket(uint32_t _tag) : Family(_tag) {
        iovecs.reserve(reserve_count);
    }
    std::vector<iovec> iovecs;
};

template <typename Family>
struct iovecs_ticket : public Family {
    iovecs_ticket(uint32_t _tag) : Family(_tag) {
    }
    std::vector<std::vector<iovec>> iovecss;
};

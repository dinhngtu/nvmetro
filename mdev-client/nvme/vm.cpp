#include <stdexcept>

#include <sys/mman.h>

#include "vm.hpp"

template <typename T>
constexpr bool check_span(std::span<T> span, size_t offset, size_t size) {
    size_t end = 0;
    if (__builtin_add_overflow(offset, size, &end) || end > span.size()) {
        return false;
    }
    return true;
}

mapping::mapping(mapping &&other) noexcept {
    std::swap(_span, other._span);
}

mapping &mapping::operator=(mapping &&other) noexcept {
    if (this != &other) {
        this->dispose();
        std::swap(_span, other._span);
    }
    return *this;
}

mapping::~mapping() {
    dispose();
}

std::span<uint64_t> mapping::get_page(size_t offset) {
    if (!check_span(_span, offset, NVME_PAGE_SIZE)) {
        throw std::out_of_range("offset is too far");
    }
    auto poff = offset & (NVME_PAGE_SIZE - 1);
    if (poff & (sizeof(uint64_t) - 1)) {
        throw std::invalid_argument("offset is not aligned");
    }
    return std::span(reinterpret_cast<uint64_t *>(_span.data() + offset), (NVME_PAGE_SIZE - poff) / sizeof(uint64_t));
}

uint64_t mapping::get_u64(size_t offset) {
    if (!check_span(_span, offset, sizeof(uint64_t))) {
        throw std::out_of_range("offset is too far");
    }
    return *reinterpret_cast<uint64_t *>(_span.data() + offset);
}

std::span<unsigned char> mapping::get_span(size_t offset, size_t size) {
    if (!check_span(_span, offset, size)) {
        throw std::out_of_range("offset is too far");
    }
    return _span.subspan(offset, size);
}

void mapping::dispose() {
    if (_span.data() != nullptr) {
        munmap((void *)_span.data(), _span.size());
        _span = std::span<unsigned char>();
    }
}

#pragma once

#include <memory>
#include <iterator>

#include "vm.hpp"

using prp_list = std::span<const uint64_t>;

class prp_chain_iter {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = uint64_t;
    using difference_type = int64_t;
    using pointer = const uint64_t *;
    using reference = const uint64_t &;

    explicit prp_chain_iter() : _prpi(), _nbytes() {
    }
    explicit prp_chain_iter(
        const std::shared_ptr<mapping> &vm,
        prp_list current_prp_list,
        size_t prp_index,
        size_t nbytes)
        : _vm(vm), _prpl(current_prp_list), _prpi(prp_index), _nbytes(nbytes) {
    }
    prp_chain_iter &operator++();
    prp_chain_iter operator++(int);
    value_type operator*() const;
    inline bool at_end() const {
        return !_nbytes;
    }
    inline size_t this_nbytes() const {
        auto poff = _prpl[_prpi] & (NVME_PAGE_SIZE - 1);
        if (is_data()) {
            return std::min(NVME_PAGE_SIZE - poff, _nbytes);
        } else {
            return 0;
        }
    }

private:
    inline bool is_data() const {
        return _prpi < _prpl.size() - 1 || _nbytes <= NVME_PAGE_SIZE;
    }

    std::shared_ptr<mapping> _vm;
    prp_list _prpl;
    size_t _prpi;
    // number of bytes remaining, counting from the current point of the iterator;
    // when _nbytes==0, iterator is beyond end
    size_t _nbytes;
};

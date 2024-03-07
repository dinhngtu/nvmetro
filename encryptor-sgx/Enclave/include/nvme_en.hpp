#pragma once

#include <array>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <cstdio>

#include "nvme_core.hpp"
#include "vm.hpp"
#include "prp.hpp"

class nvme_cmd_lba_iter_en {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = std::span<unsigned char>;
    using difference_type = void;
    using pointer = void;
    using reference = void;

    explicit nvme_cmd_lba_iter_en(const std::shared_ptr<mapping> &vm, const nvme_command &cmd, int lba_shift)
        : _vm(vm), _lba_shift(lba_shift), _slba(cmd.rw.slba) {
        size_t nblocks = static_cast<size_t>(cmd.rw.length) + 1;
        prp_list cmd_prpl{reinterpret_cast<prp_list::const_pointer>(&cmd.rw.dptr.prp1), 2};
        _nbytes = nblocks << _lba_shift;
        _prp_iter = prp_chain_iter(_vm, cmd_prpl, 0, _nbytes);
    }
    nvme_cmd_lba_iter_en &operator++() {
        _pli++;
        if (_pli << _lba_shift >= std::min(NVME_PAGE_SIZE, _prp_iter.this_nbytes())) {
            _cli += _pli;
            _pli = 0;
            _prp_iter++;
        }
        return *this;
    }
    inline nvme_cmd_lba_iter_en operator++(int) {
        nvme_cmd_lba_iter_en tmp(*this);
        ++*this;
        return tmp;
    }
    inline value_type operator*() const {
        return _vm->get_span(*_prp_iter + (_pli << _lba_shift), 1 << _lba_shift);
    }

    inline int cmd_lba_shift() const {
        return _lba_shift;
    }
    inline size_t cmd_lba_size() const {
        return size_t{1} << _lba_shift;
    }
    inline uint64_t cmd_slba() const {
        return _slba;
    }
    inline size_t cmd_nbytes() const {
        return _nbytes;
    }

    inline size_t page_begin_lba_index() const {
        return _cli;
    }
    inline size_t in_page_lba_index() const {
        return _pli;
    }
    inline size_t command_lba_index() const {
        return _cli + _pli;
    }
    inline size_t lba() const {
        return _slba + _cli + _pli;
    }

    inline bool at_end() const {
        return _prp_iter.at_end(); // && (_pli + 1) << _lba_shift >= std::min(NVME_PAGE_SIZE, _prp_iter.this_nbytes());
    }

private:
    std::shared_ptr<mapping> _vm;
    int _lba_shift;
    uint64_t _slba;
    size_t _nbytes;
    prp_chain_iter _prp_iter;
    size_t _cli = 0, _pli = 0;
};

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

constexpr size_t MAX_VIRTUAL_NAMESPACES = 16;

constexpr bool check_nblocks(size_t nblocks, int mdts, int lba_shift) {
    return nblocks <= size_t{1} << mdts << NVME_PAGE_SHIFT >> lba_shift;
}

constexpr int lba_shift(const struct nvme_id_ns &idns) {
    return static_cast<int>(idns.lbaf[idns.flbas & 0xf].ds);
}

class nvme_exception : public std::exception {
public:
    nvme_exception(__u16 code) : _code(code) {
        snprintf(&_err[0], sizeof(_err), "NVME error %hu", _code);
        _err[sizeof(_err) - 1] = 0;
    }
    const char *what() const noexcept override {
        return &_err[0];
    }
    constexpr __u16 code() const noexcept {
        return _code;
    }

private:
    __u16 _code;
    std::array<char, 24> _err{};
};

class nvme {
    friend class nvme_cmd_lba_iter;
    friend class nvme_cmd_page_iter;

public:
    explicit nvme(const std::shared_ptr<mapping> &vm, int nfd) : _vm(vm), _nfd(nfd) {
    }
    nvme(const nvme &) = delete;
    nvme &operator=(const nvme &) = delete;
    nvme(nvme &&) = default;
    nvme &operator=(nvme &&) = default;
    virtual ~nvme() = default;

protected:
    inline const std::unique_ptr<nvme_id_ctrl> &id_vctrl() {
        if (!_id) {
            if (do_id_vctrl() < 0) {
                throw std::runtime_error("cannot id vctrl");
            }
        }
        return _id;
    }
    inline const std::unique_ptr<nvme_id_ns> &id_vns(__u32 nsid) {
        if (nsid == 0 || nsid >= MAX_VIRTUAL_NAMESPACES) {
            throw nvme_exception(NVME_SC_DNR | NVME_SC_INVALID_NS);
        }
        if (!_idns[nsid]) {
            if (do_id_vns(nsid) < 0)
                throw nvme_exception(NVME_SC_DNR | NVME_SC_INVALID_NS);
        }
        return _idns[nsid];
    }
    constexpr const std::shared_ptr<mapping> &vm() const {
        return _vm;
    }
    inline int ns_lba_shift(__u32 nsid) {
        auto &idns = id_vns(nsid);
        if (!idns) {
            throw nvme_exception(NVME_SC_DNR | NVME_SC_INVALID_NS);
        }
        return lba_shift(*idns);
    }
    inline size_t ns_cmd_check_nbytes(size_t nblocks, int lbas) {
        auto &id = id_vctrl();
        if (!id) {
            throw nvme_exception(NVME_SC_DNR | NVME_SC_INVALID_NS);
        }
        if (!check_nblocks(nblocks, id->mdts, lbas)) {
            throw nvme_exception(NVME_SC_DNR | NVME_SC_INVALID_FIELD);
        }
        return nblocks << lbas;
    }

private:
    int do_id_vctrl();
    int do_id_vns(__u32 nsid);
    std::shared_ptr<mapping> _vm;
    int _nfd;
    std::unique_ptr<nvme_id_ctrl> _id;
    std::array<std::unique_ptr<nvme_id_ns>, MAX_VIRTUAL_NAMESPACES> _idns;
};

class nvme_cmd_lba_iter {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = std::span<unsigned char>;
    using difference_type = void;
    using pointer = void;
    using reference = void;

    explicit nvme_cmd_lba_iter(nvme &ctrl, const nvme_command &cmd) : _vm(ctrl.vm()), _slba(cmd.rw.slba) {
        size_t nblocks = static_cast<size_t>(cmd.rw.length) + 1;
        prp_list cmd_prpl{reinterpret_cast<prp_list::const_pointer>(&cmd.rw.dptr.prp1), 2};
        _lba_shift = ctrl.ns_lba_shift(cmd.rw.nsid);
        _nbytes = ctrl.ns_cmd_check_nbytes(nblocks, _lba_shift);
        _prp_iter = prp_chain_iter(_vm, cmd_prpl, 0, _nbytes);
    }
    nvme_cmd_lba_iter &operator++() {
        _pli++;
        if (_pli << _lba_shift >= std::min(NVME_PAGE_SIZE, _prp_iter.this_nbytes())) {
            _cli += _pli;
            _pli = 0;
            _prp_iter++;
        }
        return *this;
    }
    inline nvme_cmd_lba_iter operator++(int) {
        nvme_cmd_lba_iter tmp(*this);
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
        return _prp_iter.at_end();
    }

private:
    std::shared_ptr<mapping> _vm;
    int _lba_shift;
    uint64_t _slba;
    size_t _nbytes;
    prp_chain_iter _prp_iter;
    size_t _cli = 0, _pli = 0;
};

class nvme_cmd_page_iter {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = std::span<unsigned char>;
    using difference_type = void;
    using pointer = void;
    using reference = void;

    explicit nvme_cmd_page_iter(nvme &ctrl, const nvme_command &cmd) : _vm(ctrl.vm()), _slba(cmd.rw.slba) {
        size_t nblocks = static_cast<size_t>(cmd.rw.length) + 1;
        prp_list cmd_prpl{reinterpret_cast<prp_list::const_pointer>(&cmd.rw.dptr.prp1), 2};
        _lba_shift = ctrl.ns_lba_shift(cmd.rw.nsid);
        _nbytes = ctrl.ns_cmd_check_nbytes(nblocks, _lba_shift);
        _prp_iter = prp_chain_iter(_vm, cmd_prpl, 0, _nbytes);
    }
    nvme_cmd_page_iter &operator++() {
        _prp_iter++;
        return *this;
    }
    inline nvme_cmd_page_iter operator++(int) {
        nvme_cmd_page_iter tmp(*this);
        ++*this;
        return tmp;
    }
    inline value_type operator*() const {
        return _vm->get_span(*_prp_iter, _prp_iter.this_nbytes());
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

    inline size_t lba() const {
        return _slba + _cli;
    }

    inline bool at_end() const {
        return _prp_iter.at_end();
    }

private:
    std::shared_ptr<mapping> _vm;
    int _lba_shift;
    uint64_t _slba;
    size_t _nbytes;
    prp_chain_iter _prp_iter;
    size_t _cli = 0;
};

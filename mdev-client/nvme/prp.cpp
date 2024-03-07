#include <cassert>

#include "prp.hpp"

prp_chain_iter &prp_chain_iter::operator++() {
    if (at_end()) {
        throw std::runtime_error("tried to advance prp iter beyond end");
    }
    assert(_nbytes >= this_nbytes() && _prpi < _prpl.size() && is_data());

    // this_nbytes depends on _prpi, must decrease nbytes first
    // printf("current prpi %zu nbytes %zu\n", _prpi, _nbytes);
    _nbytes -= this_nbytes();
    _prpi++;
    // printf("new prpi %zu nbytes %zu\n", _prpi, _nbytes);
    if (!is_data()) {
        // end of current prp list, move to next prp list
        auto next_prpl = **this;
        // printf("next page %#lx\n", next_prpl);
        _prpl = _vm->get_page(next_prpl);
        _prpi = 0;
    }
    assert(at_end() || is_data());
    return *this;
}

prp_chain_iter prp_chain_iter::operator++(int) {
    prp_chain_iter tmp(*this);
    ++*this;
    return tmp;
}

prp_chain_iter::value_type prp_chain_iter::operator*() const {
    if (at_end()) {
        throw std::runtime_error("tried to dereference prp iter beyond end");
    }
    assert(_prpi < _prpl.size());
    return _prpl[_prpi];
}

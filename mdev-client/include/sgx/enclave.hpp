#pragma once

#include <functional>
#include <string>
#include <system_error>

#include <sgx_eid.h>
#include <sgx_error.h>
#include <sgx_uswitchless.h>

class sgx_category final : public std::error_category {
public:
    constexpr explicit sgx_category() noexcept {
    }
    sgx_category(const sgx_category &) = delete;
    sgx_category &operator=(const sgx_category &) = delete;
    sgx_category(sgx_category &&) = delete;
    sgx_category &operator=(sgx_category &&) = delete;
    ~sgx_category() = default;

    const char *name() const noexcept override {
        return "sgx";
    }

    std::string message(int condition) const override;
};

class sgx_enclave {
public:
    explicit sgx_enclave(const std::string &filename, bool debug);
    sgx_enclave(const sgx_enclave &) = delete;
    sgx_enclave &operator=(const sgx_enclave &) = delete;
    sgx_enclave(sgx_enclave &&);
    sgx_enclave &operator=(sgx_enclave &&);
    ~sgx_enclave();

    constexpr sgx_enclave_id_t eid() const {
        return _eid;
    }
    template <class R, class... Args>
    inline R invoke(sgx_status_t (*f)(sgx_enclave_id_t, R *, Args...), Args... args) {
        R retval{};
        sgx_status_t err = f(_eid, &retval, args...);
        if (err != SGX_SUCCESS) {
            throw std::system_error(retval, sgx_category(), "sgx call failed");
        }
        return retval;
    }

private:
    void dispose();
    sgx_enclave_id_t _eid = 0;
    std::array<const void *, 32> _exfeat;
    sgx_uswitchless_config_t _exfeat_switchless;
};

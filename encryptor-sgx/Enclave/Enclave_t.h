#ifndef ENCLAVE_T_H__
#define ENCLAVE_T_H__

#include <stdint.h>
#include <wchar.h>
#include <stddef.h>
#include "sgx_edger8r.h" /* for sgx_ocall etc. */


#include <stdlib.h> /* for size_t */

#define SGX_CAST(type, item) ((type)(item))

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _nvme_command_alias
#define _nvme_command_alias
typedef struct nvme_command_alias {
	char buf[64];
} nvme_command_alias;
#endif

int put_key(unsigned char key[32], int lba_shift);
long int crypt_buffer_inplace(size_t slba, unsigned char* buf, size_t nr_blocks, int decrypt);
long int crypt_command_inplace(unsigned char* pvm, size_t pvm_size, const struct nvme_command_alias* cmd, int decrypt);
long int crypt_command(unsigned char* pvm, size_t pvm_size, const struct nvme_command_alias* cmd, unsigned char* outbuf, size_t outbuf_size, int decrypt);
sgx_status_t sl_init_switchless(void* sl_data);
sgx_status_t sl_run_switchless_tworker(void);

sgx_status_t SGX_CDECL sgx_oc_cpuidex(int cpuinfo[4], int leaf, int subleaf);
sgx_status_t SGX_CDECL sgx_thread_wait_untrusted_event_ocall(int* retval, const void* self);
sgx_status_t SGX_CDECL sgx_thread_set_untrusted_event_ocall(int* retval, const void* waiter);
sgx_status_t SGX_CDECL sgx_thread_setwait_untrusted_events_ocall(int* retval, const void* waiter, const void* self);
sgx_status_t SGX_CDECL sgx_thread_set_multiple_untrusted_events_ocall(int* retval, const void** waiters, size_t total);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif

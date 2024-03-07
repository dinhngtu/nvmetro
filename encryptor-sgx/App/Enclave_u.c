#include "Enclave_u.h"
#include <errno.h>

typedef struct ms_put_key_t {
	int ms_retval;
	unsigned char* ms_key;
	int ms_lba_shift;
} ms_put_key_t;

typedef struct ms_crypt_buffer_inplace_t {
	long int ms_retval;
	size_t ms_slba;
	unsigned char* ms_buf;
	size_t ms_nr_blocks;
	int ms_decrypt;
} ms_crypt_buffer_inplace_t;

typedef struct ms_crypt_command_inplace_t {
	long int ms_retval;
	unsigned char* ms_pvm;
	size_t ms_pvm_size;
	const struct nvme_command_alias* ms_cmd;
	int ms_decrypt;
} ms_crypt_command_inplace_t;

typedef struct ms_crypt_command_t {
	long int ms_retval;
	unsigned char* ms_pvm;
	size_t ms_pvm_size;
	const struct nvme_command_alias* ms_cmd;
	unsigned char* ms_outbuf;
	size_t ms_outbuf_size;
	int ms_decrypt;
} ms_crypt_command_t;

typedef struct ms_sl_init_switchless_t {
	sgx_status_t ms_retval;
	void* ms_sl_data;
} ms_sl_init_switchless_t;

typedef struct ms_sl_run_switchless_tworker_t {
	sgx_status_t ms_retval;
} ms_sl_run_switchless_tworker_t;

typedef struct ms_sgx_oc_cpuidex_t {
	int* ms_cpuinfo;
	int ms_leaf;
	int ms_subleaf;
} ms_sgx_oc_cpuidex_t;

typedef struct ms_sgx_thread_wait_untrusted_event_ocall_t {
	int ms_retval;
	const void* ms_self;
} ms_sgx_thread_wait_untrusted_event_ocall_t;

typedef struct ms_sgx_thread_set_untrusted_event_ocall_t {
	int ms_retval;
	const void* ms_waiter;
} ms_sgx_thread_set_untrusted_event_ocall_t;

typedef struct ms_sgx_thread_setwait_untrusted_events_ocall_t {
	int ms_retval;
	const void* ms_waiter;
	const void* ms_self;
} ms_sgx_thread_setwait_untrusted_events_ocall_t;

typedef struct ms_sgx_thread_set_multiple_untrusted_events_ocall_t {
	int ms_retval;
	const void** ms_waiters;
	size_t ms_total;
} ms_sgx_thread_set_multiple_untrusted_events_ocall_t;

static sgx_status_t SGX_CDECL Enclave_sgx_oc_cpuidex(void* pms)
{
	ms_sgx_oc_cpuidex_t* ms = SGX_CAST(ms_sgx_oc_cpuidex_t*, pms);
	sgx_oc_cpuidex(ms->ms_cpuinfo, ms->ms_leaf, ms->ms_subleaf);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_sgx_thread_wait_untrusted_event_ocall(void* pms)
{
	ms_sgx_thread_wait_untrusted_event_ocall_t* ms = SGX_CAST(ms_sgx_thread_wait_untrusted_event_ocall_t*, pms);
	ms->ms_retval = sgx_thread_wait_untrusted_event_ocall(ms->ms_self);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_sgx_thread_set_untrusted_event_ocall(void* pms)
{
	ms_sgx_thread_set_untrusted_event_ocall_t* ms = SGX_CAST(ms_sgx_thread_set_untrusted_event_ocall_t*, pms);
	ms->ms_retval = sgx_thread_set_untrusted_event_ocall(ms->ms_waiter);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_sgx_thread_setwait_untrusted_events_ocall(void* pms)
{
	ms_sgx_thread_setwait_untrusted_events_ocall_t* ms = SGX_CAST(ms_sgx_thread_setwait_untrusted_events_ocall_t*, pms);
	ms->ms_retval = sgx_thread_setwait_untrusted_events_ocall(ms->ms_waiter, ms->ms_self);

	return SGX_SUCCESS;
}

static sgx_status_t SGX_CDECL Enclave_sgx_thread_set_multiple_untrusted_events_ocall(void* pms)
{
	ms_sgx_thread_set_multiple_untrusted_events_ocall_t* ms = SGX_CAST(ms_sgx_thread_set_multiple_untrusted_events_ocall_t*, pms);
	ms->ms_retval = sgx_thread_set_multiple_untrusted_events_ocall(ms->ms_waiters, ms->ms_total);

	return SGX_SUCCESS;
}

static const struct {
	size_t nr_ocall;
	void * table[5];
} ocall_table_Enclave = {
	5,
	{
		(void*)Enclave_sgx_oc_cpuidex,
		(void*)Enclave_sgx_thread_wait_untrusted_event_ocall,
		(void*)Enclave_sgx_thread_set_untrusted_event_ocall,
		(void*)Enclave_sgx_thread_setwait_untrusted_events_ocall,
		(void*)Enclave_sgx_thread_set_multiple_untrusted_events_ocall,
	}
};
sgx_status_t put_key(sgx_enclave_id_t eid, int* retval, unsigned char key[32], int lba_shift)
{
	sgx_status_t status;
	ms_put_key_t ms;
	ms.ms_key = (unsigned char*)key;
	ms.ms_lba_shift = lba_shift;
	status = sgx_ecall(eid, 0, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

sgx_status_t crypt_buffer_inplace(sgx_enclave_id_t eid, long int* retval, size_t slba, unsigned char* buf, size_t nr_blocks, int decrypt)
{
	sgx_status_t status;
	ms_crypt_buffer_inplace_t ms;
	ms.ms_slba = slba;
	ms.ms_buf = buf;
	ms.ms_nr_blocks = nr_blocks;
	ms.ms_decrypt = decrypt;
	status = sgx_ecall_switchless(eid, 1, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

sgx_status_t crypt_command_inplace(sgx_enclave_id_t eid, long int* retval, unsigned char* pvm, size_t pvm_size, const struct nvme_command_alias* cmd, int decrypt)
{
	sgx_status_t status;
	ms_crypt_command_inplace_t ms;
	ms.ms_pvm = pvm;
	ms.ms_pvm_size = pvm_size;
	ms.ms_cmd = cmd;
	ms.ms_decrypt = decrypt;
	status = sgx_ecall_switchless(eid, 2, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

sgx_status_t crypt_command(sgx_enclave_id_t eid, long int* retval, unsigned char* pvm, size_t pvm_size, const struct nvme_command_alias* cmd, unsigned char* outbuf, size_t outbuf_size, int decrypt)
{
	sgx_status_t status;
	ms_crypt_command_t ms;
	ms.ms_pvm = pvm;
	ms.ms_pvm_size = pvm_size;
	ms.ms_cmd = cmd;
	ms.ms_outbuf = outbuf;
	ms.ms_outbuf_size = outbuf_size;
	ms.ms_decrypt = decrypt;
	status = sgx_ecall_switchless(eid, 3, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

sgx_status_t sl_init_switchless(sgx_enclave_id_t eid, sgx_status_t* retval, void* sl_data)
{
	sgx_status_t status;
	ms_sl_init_switchless_t ms;
	ms.ms_sl_data = sl_data;
	status = sgx_ecall(eid, 4, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}

sgx_status_t sl_run_switchless_tworker(sgx_enclave_id_t eid, sgx_status_t* retval)
{
	sgx_status_t status;
	ms_sl_run_switchless_tworker_t ms;
	status = sgx_ecall(eid, 5, &ocall_table_Enclave, &ms);
	if (status == SGX_SUCCESS && retval) *retval = ms.ms_retval;
	return status;
}


#include "Enclave_t.h"

#include "sgx_trts.h" /* for sgx_ocalloc, sgx_is_outside_enclave */
#include "sgx_lfence.h" /* for sgx_lfence */

#include <errno.h>
#include <mbusafecrt.h> /* for memcpy_s etc */
#include <stdlib.h> /* for malloc/free etc */

#define CHECK_REF_POINTER(ptr, siz) do {	\
	if (!(ptr) || ! sgx_is_outside_enclave((ptr), (siz)))	\
		return SGX_ERROR_INVALID_PARAMETER;\
} while (0)

#define CHECK_UNIQUE_POINTER(ptr, siz) do {	\
	if ((ptr) && ! sgx_is_outside_enclave((ptr), (siz)))	\
		return SGX_ERROR_INVALID_PARAMETER;\
} while (0)

#define CHECK_ENCLAVE_POINTER(ptr, siz) do {	\
	if ((ptr) && ! sgx_is_within_enclave((ptr), (siz)))	\
		return SGX_ERROR_INVALID_PARAMETER;\
} while (0)

#define ADD_ASSIGN_OVERFLOW(a, b) (	\
	((a) += (b)) < (b)	\
)


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

static sgx_status_t SGX_CDECL sgx_put_key(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_put_key_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_put_key_t* ms = SGX_CAST(ms_put_key_t*, pms);
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_key = ms->ms_key;
	size_t _len_key = 32 * sizeof(unsigned char);
	unsigned char* _in_key = NULL;

	CHECK_UNIQUE_POINTER(_tmp_key, _len_key);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_key != NULL && _len_key != 0) {
		if ( _len_key % sizeof(*_tmp_key) != 0)
		{
			status = SGX_ERROR_INVALID_PARAMETER;
			goto err;
		}
		_in_key = (unsigned char*)malloc(_len_key);
		if (_in_key == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_key, _len_key, _tmp_key, _len_key)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}

	ms->ms_retval = put_key(_in_key, ms->ms_lba_shift);

err:
	if (_in_key) free(_in_key);
	return status;
}

static sgx_status_t SGX_CDECL sgx_crypt_buffer_inplace(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_crypt_buffer_inplace_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_crypt_buffer_inplace_t* ms = SGX_CAST(ms_crypt_buffer_inplace_t*, pms);
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_buf = ms->ms_buf;



	ms->ms_retval = crypt_buffer_inplace(ms->ms_slba, _tmp_buf, ms->ms_nr_blocks, ms->ms_decrypt);


	return status;
}

static sgx_status_t SGX_CDECL sgx_crypt_command_inplace(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_crypt_command_inplace_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_crypt_command_inplace_t* ms = SGX_CAST(ms_crypt_command_inplace_t*, pms);
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_pvm = ms->ms_pvm;
	const struct nvme_command_alias* _tmp_cmd = ms->ms_cmd;
	size_t _len_cmd = sizeof(struct nvme_command_alias);
	struct nvme_command_alias* _in_cmd = NULL;

	CHECK_UNIQUE_POINTER(_tmp_cmd, _len_cmd);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_cmd != NULL && _len_cmd != 0) {
		_in_cmd = (struct nvme_command_alias*)malloc(_len_cmd);
		if (_in_cmd == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_cmd, _len_cmd, _tmp_cmd, _len_cmd)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}

	ms->ms_retval = crypt_command_inplace(_tmp_pvm, ms->ms_pvm_size, (const struct nvme_command_alias*)_in_cmd, ms->ms_decrypt);

err:
	if (_in_cmd) free(_in_cmd);
	return status;
}

static sgx_status_t SGX_CDECL sgx_crypt_command(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_crypt_command_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_crypt_command_t* ms = SGX_CAST(ms_crypt_command_t*, pms);
	sgx_status_t status = SGX_SUCCESS;
	unsigned char* _tmp_pvm = ms->ms_pvm;
	const struct nvme_command_alias* _tmp_cmd = ms->ms_cmd;
	size_t _len_cmd = sizeof(struct nvme_command_alias);
	struct nvme_command_alias* _in_cmd = NULL;
	unsigned char* _tmp_outbuf = ms->ms_outbuf;

	CHECK_UNIQUE_POINTER(_tmp_cmd, _len_cmd);

	//
	// fence after pointer checks
	//
	sgx_lfence();

	if (_tmp_cmd != NULL && _len_cmd != 0) {
		_in_cmd = (struct nvme_command_alias*)malloc(_len_cmd);
		if (_in_cmd == NULL) {
			status = SGX_ERROR_OUT_OF_MEMORY;
			goto err;
		}

		if (memcpy_s(_in_cmd, _len_cmd, _tmp_cmd, _len_cmd)) {
			status = SGX_ERROR_UNEXPECTED;
			goto err;
		}

	}

	ms->ms_retval = crypt_command(_tmp_pvm, ms->ms_pvm_size, (const struct nvme_command_alias*)_in_cmd, _tmp_outbuf, ms->ms_outbuf_size, ms->ms_decrypt);

err:
	if (_in_cmd) free(_in_cmd);
	return status;
}

static sgx_status_t SGX_CDECL sgx_sl_init_switchless(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_sl_init_switchless_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_sl_init_switchless_t* ms = SGX_CAST(ms_sl_init_switchless_t*, pms);
	sgx_status_t status = SGX_SUCCESS;
	void* _tmp_sl_data = ms->ms_sl_data;



	ms->ms_retval = sl_init_switchless(_tmp_sl_data);


	return status;
}

static sgx_status_t SGX_CDECL sgx_sl_run_switchless_tworker(void* pms)
{
	CHECK_REF_POINTER(pms, sizeof(ms_sl_run_switchless_tworker_t));
	//
	// fence after pointer checks
	//
	sgx_lfence();
	ms_sl_run_switchless_tworker_t* ms = SGX_CAST(ms_sl_run_switchless_tworker_t*, pms);
	sgx_status_t status = SGX_SUCCESS;



	ms->ms_retval = sl_run_switchless_tworker();


	return status;
}

SGX_EXTERNC const struct {
	size_t nr_ecall;
	struct {void* ecall_addr; uint8_t is_priv; uint8_t is_switchless;} ecall_table[6];
} g_ecall_table = {
	6,
	{
		{(void*)(uintptr_t)sgx_put_key, 0, 0},
		{(void*)(uintptr_t)sgx_crypt_buffer_inplace, 0, 1},
		{(void*)(uintptr_t)sgx_crypt_command_inplace, 0, 1},
		{(void*)(uintptr_t)sgx_crypt_command, 0, 1},
		{(void*)(uintptr_t)sgx_sl_init_switchless, 0, 0},
		{(void*)(uintptr_t)sgx_sl_run_switchless_tworker, 0, 0},
	}
};

SGX_EXTERNC const struct {
	size_t nr_ocall;
	uint8_t entry_table[5][6];
} g_dyn_entry_table = {
	5,
	{
		{0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, },
		{0, 0, 0, 0, 0, 0, },
	}
};


sgx_status_t SGX_CDECL sgx_oc_cpuidex(int cpuinfo[4], int leaf, int subleaf)
{
	sgx_status_t status = SGX_SUCCESS;
	size_t _len_cpuinfo = 4 * sizeof(int);

	ms_sgx_oc_cpuidex_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_sgx_oc_cpuidex_t);
	void *__tmp = NULL;

	void *__tmp_cpuinfo = NULL;

	CHECK_ENCLAVE_POINTER(cpuinfo, _len_cpuinfo);

	if (ADD_ASSIGN_OVERFLOW(ocalloc_size, (cpuinfo != NULL) ? _len_cpuinfo : 0))
		return SGX_ERROR_INVALID_PARAMETER;

	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_sgx_oc_cpuidex_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_sgx_oc_cpuidex_t));
	ocalloc_size -= sizeof(ms_sgx_oc_cpuidex_t);

	if (cpuinfo != NULL) {
		ms->ms_cpuinfo = (int*)__tmp;
		__tmp_cpuinfo = __tmp;
		if (_len_cpuinfo % sizeof(*cpuinfo) != 0) {
			sgx_ocfree();
			return SGX_ERROR_INVALID_PARAMETER;
		}
		memset(__tmp_cpuinfo, 0, _len_cpuinfo);
		__tmp = (void *)((size_t)__tmp + _len_cpuinfo);
		ocalloc_size -= _len_cpuinfo;
	} else {
		ms->ms_cpuinfo = NULL;
	}
	
	ms->ms_leaf = leaf;
	ms->ms_subleaf = subleaf;
	status = sgx_ocall(0, ms);

	if (status == SGX_SUCCESS) {
		if (cpuinfo) {
			if (memcpy_s((void*)cpuinfo, _len_cpuinfo, __tmp_cpuinfo, _len_cpuinfo)) {
				sgx_ocfree();
				return SGX_ERROR_UNEXPECTED;
			}
		}
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL sgx_thread_wait_untrusted_event_ocall(int* retval, const void* self)
{
	sgx_status_t status = SGX_SUCCESS;

	ms_sgx_thread_wait_untrusted_event_ocall_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_sgx_thread_wait_untrusted_event_ocall_t);
	void *__tmp = NULL;


	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_sgx_thread_wait_untrusted_event_ocall_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_sgx_thread_wait_untrusted_event_ocall_t));
	ocalloc_size -= sizeof(ms_sgx_thread_wait_untrusted_event_ocall_t);

	ms->ms_self = self;
	status = sgx_ocall(1, ms);

	if (status == SGX_SUCCESS) {
		if (retval) *retval = ms->ms_retval;
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL sgx_thread_set_untrusted_event_ocall(int* retval, const void* waiter)
{
	sgx_status_t status = SGX_SUCCESS;

	ms_sgx_thread_set_untrusted_event_ocall_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_sgx_thread_set_untrusted_event_ocall_t);
	void *__tmp = NULL;


	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_sgx_thread_set_untrusted_event_ocall_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_sgx_thread_set_untrusted_event_ocall_t));
	ocalloc_size -= sizeof(ms_sgx_thread_set_untrusted_event_ocall_t);

	ms->ms_waiter = waiter;
	status = sgx_ocall(2, ms);

	if (status == SGX_SUCCESS) {
		if (retval) *retval = ms->ms_retval;
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL sgx_thread_setwait_untrusted_events_ocall(int* retval, const void* waiter, const void* self)
{
	sgx_status_t status = SGX_SUCCESS;

	ms_sgx_thread_setwait_untrusted_events_ocall_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_sgx_thread_setwait_untrusted_events_ocall_t);
	void *__tmp = NULL;


	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_sgx_thread_setwait_untrusted_events_ocall_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_sgx_thread_setwait_untrusted_events_ocall_t));
	ocalloc_size -= sizeof(ms_sgx_thread_setwait_untrusted_events_ocall_t);

	ms->ms_waiter = waiter;
	ms->ms_self = self;
	status = sgx_ocall(3, ms);

	if (status == SGX_SUCCESS) {
		if (retval) *retval = ms->ms_retval;
	}
	sgx_ocfree();
	return status;
}

sgx_status_t SGX_CDECL sgx_thread_set_multiple_untrusted_events_ocall(int* retval, const void** waiters, size_t total)
{
	sgx_status_t status = SGX_SUCCESS;
	size_t _len_waiters = total * sizeof(void*);

	ms_sgx_thread_set_multiple_untrusted_events_ocall_t* ms = NULL;
	size_t ocalloc_size = sizeof(ms_sgx_thread_set_multiple_untrusted_events_ocall_t);
	void *__tmp = NULL;


	CHECK_ENCLAVE_POINTER(waiters, _len_waiters);

	if (ADD_ASSIGN_OVERFLOW(ocalloc_size, (waiters != NULL) ? _len_waiters : 0))
		return SGX_ERROR_INVALID_PARAMETER;

	__tmp = sgx_ocalloc(ocalloc_size);
	if (__tmp == NULL) {
		sgx_ocfree();
		return SGX_ERROR_UNEXPECTED;
	}
	ms = (ms_sgx_thread_set_multiple_untrusted_events_ocall_t*)__tmp;
	__tmp = (void *)((size_t)__tmp + sizeof(ms_sgx_thread_set_multiple_untrusted_events_ocall_t));
	ocalloc_size -= sizeof(ms_sgx_thread_set_multiple_untrusted_events_ocall_t);

	if (waiters != NULL) {
		ms->ms_waiters = (const void**)__tmp;
		if (_len_waiters % sizeof(*waiters) != 0) {
			sgx_ocfree();
			return SGX_ERROR_INVALID_PARAMETER;
		}
		if (memcpy_s(__tmp, ocalloc_size, waiters, _len_waiters)) {
			sgx_ocfree();
			return SGX_ERROR_UNEXPECTED;
		}
		__tmp = (void *)((size_t)__tmp + _len_waiters);
		ocalloc_size -= _len_waiters;
	} else {
		ms->ms_waiters = NULL;
	}
	
	ms->ms_total = total;
	status = sgx_ocall(4, ms);

	if (status == SGX_SUCCESS) {
		if (retval) *retval = ms->ms_retval;
	}
	sgx_ocfree();
	return status;
}


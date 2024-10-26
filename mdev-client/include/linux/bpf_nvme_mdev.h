#ifndef _BPF_NVME_MDEV_H
#define _BPF_NVME_MDEV_H

#include <linux/types.h>
#include <linux/nvme.h>

/*
 * request lifecycle:
 * - bpf registers list of hooks it's interested in
 * - bpf is called at each registered hook
 * - bpf can have one of the following outcomes:
 *   - immediate return with status
 *   - send to predefined targets (sync or async)
 *     and/or
 *     install additional hook for the given request
 * - request is completed (vcq is written) when either:
 *   - bpf prog returns BPF_COMPLETE + status
 *   - a request with BPF_WILL_COMPLETE_* finishes all of its synchronous sends
 *   A request completed with BPF_COMPLETE may need to wait for remaining sync
 *   sends. Such request enters a grace period until the sync sends are complete.
 */

/*
 * hook order - a bpf program called on a request by a later hook
 * cannot register for an earlier hook for that request:
 * 1. vsq
 * 2. hcq
 * 2. notifyfd completion
 * 3. pre-vcq
 * vcq cannot be hooked
 */

struct bpf_io_ctx {
	/*
	 * RW. Modifications are persisted during vsq hook;
	 * modifications during non-vsq hook are reverted at next hook.
	 */
	struct nvme_command cmd;

	/* RO */
	__u32 sqid;
	__u32 current_hook;
	/* snapshot of iostate for the cmpxchg loop */
	__u32 iostate;
	__u32 data;
	__u32 aux[3];
};

#define NMBPF_STATUS_MASK 0xFFFF
#define NMBPF_HOOK_MASK 0x3F0000
#define NMBPF_SEND_MASK 0xFC00000
#define NMBPF_COMPLETION_MASK 0x70000000
#define NMBPF_SEND_SHIFT 22
#define NMBPF_COMPLETE_SHIFT 28
#define WILL_COMPLETE_TO_SEND(cmpl_type)                                       \
	(1 << (((cmpl_type) >> NMBPF_COMPLETE_SHIFT) + NMBPF_SEND_SHIFT - 1))
enum nmbpf_actions {
	NMBPF_HOOK_VSQ = 1 << 16,
	NMBPF_HOOK_HCQ = 1 << 17,
	NMBPF_HOOK_NFD_WRITE = 1 << 18,
	NMBPF_HOOK_PRE_VCQ = 1 << 19,
	/* SEND_* and WILL_COMPLETE_* must be in the same order
	 * such that COMPLETE_TO_SEND(WILL_COMPLETE_X) == SEND_X
	 */
	NMBPF_SEND_HQ = 1 << (NMBPF_SEND_SHIFT + 0),
	NMBPF_SEND_FD = 1 << (NMBPF_SEND_SHIFT + 1),
	/* exclusive enum values */
	NMBPF_COMPLETE = 0 << NMBPF_COMPLETE_SHIFT,
	NMBPF_WILL_COMPLETE_HQ = 1 << NMBPF_COMPLETE_SHIFT,
	NMBPF_WILL_COMPLETE_FD = 2 << NMBPF_COMPLETE_SHIFT,
	NMBPF_WAIT_FOR_HOOK = 7 << NMBPF_COMPLETE_SHIFT,
};

#endif

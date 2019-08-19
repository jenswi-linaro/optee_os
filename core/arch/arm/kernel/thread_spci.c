/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2018-2019, Arm Limited. All rights reserved.
 * Copyright (c) 2019, Linaro Limited
 */

#include <assert.h>
#include <io.h>
#include <kernel/interrupt.h>
#include <kernel/msg_param.h>
#include <kernel/panic.h>
#include <kernel/thread.h>
#include <mm/core_memprot.h>
#include <mm/mobj.h>
#include <optee_msg.h>
#include <optee_rpc_cmd.h>
#include <sm/optee_smc.h>
#include <spci_private.h>
#include <string.h>
#include <tee/entry_fast.h>
#include <tee/entry_std.h>

#include "thread_private.h"
#include "optee_spci.h"

/* One buffer for each security state */
#define SPCI_MAX_BUFS		2
#define SPCI_MAX_SEC_STATES	2
static struct spci_msg_buf_desc buf_desc[SPCI_MAX_SEC_STATES][SPCI_MAX_BUFS];

/* Special message with initialisation information */
static struct spci_msg_sp_init *sp_init_msg;

struct spci_msg_sp_init *spci_get_msg_sp_init(void)
{
	assert(sp_init_msg);
	return sp_init_msg;
}

void spci_early_init(struct spci_buf *spci_rx_buf)
{
	struct spci_arch_msg_hdr *arch_msg_hdr = NULL;
	struct spci_msg_hdr *msg_hdr = NULL;
	unsigned int ctr = 0;

	assert(memcmp(&spci_rx_buf->hdr.signature, SPCI_BUF_SIGNATURE,
		      sizeof(SPCI_BUF_SIGNATURE)) == 0);

	/* Get the common message header */
	msg_hdr = (void *) spci_rx_buf->buf;

	/* Get the arch message header */
	arch_msg_hdr = (void *)msg_hdr->payload;

	/* Get the arch. initialisation message */
	sp_init_msg = (void *)arch_msg_hdr->payload;

	/* Populate private record of SPCI RX/TX buffers */
	for (ctr = 0; ctr < sp_init_msg->mem_reg_count; ctr++) {
		struct spci_mem_region_desc *mem_reg = NULL;
		struct spci_msg_buf_desc *bdesc = NULL;
		uint32_t type = 0;
		uint32_t sec = 0;
		uint32_t attr = 0;

		mem_reg = &sp_init_msg->mem_regs[ctr];

		/* Find the type of message */
		attr = mem_reg->attributes;
		type = attr >> SPCI_MEM_REG_TYPE_SHIFT;
		type &= SPCI_MEM_REG_TYPE_MASK;

		/* Ignore imp. def. messages */
		if (type != SPCI_MEM_REG_TYPE_ARCH)
			continue;

		/* Check if secure or non-secure RX or TX buffer */
		type = attr >> SPCI_MEM_REG_ARCH_TYPE_SHIFT;
		type &= SPCI_MEM_REG_ARCH_TYPE_MASK;
		sec = attr >> SPCI_MEM_REG_ARCH_SEC_SHIFT;
		sec &= SPCI_MEM_REG_ARCH_SEC_MASK;

		/* Obtain a reference to the buffer descriptor */
		bdesc = &buf_desc[sec][type];

		/* Store available buffer information apart from buffer VA */
		bdesc->pa = mem_reg->address;
		bdesc->page_count = mem_reg->page_count;
		bdesc->attributes = attr;
	}
}

static void print_buf_desc(struct spci_msg_buf_desc *mbd)
{
	struct spci_buf *buf = (struct spci_buf *)mbd->va;

	DMSG("Buf pa: 0x%"PRIxPA, mbd->pa);
	DMSG("Buf va: 0x%"PRIxVA, mbd->va);
	DMSG("Buf pg: 0x%x", mbd->page_count);
	DMSG("Buf at: 0x%"PRIx32, mbd->attributes);
	DMSG("Buf sg: %s", buf->hdr.signature);
	DMSG("Buf st: %s", buf->hdr.state == SPCI_BUF_STATE_EMPTY ?
	     "empty" : "full");

}

/* Populate the VA of message buffers and set their state for future use */
void spci_late_init(void)
{
	unsigned int ctr0 = 0;
	unsigned int ctr1 = 0;

	for (ctr0 = 0; ctr0 < SPCI_MAX_SEC_STATES; ctr0++) {
		for (ctr1 = 0; ctr1 < SPCI_MAX_BUFS; ctr1++) {
			struct spci_msg_buf_desc *mbd = &buf_desc[ctr0][ctr1];
			struct spci_buf *buf = NULL;
			enum teecore_memtypes memtype = 0;

			/* SPM did not describe this buffer */
			if (!mbd->pa)
				continue;

			/* Secure or non-secure */
			if (ctr0 == 0)
				memtype = MEM_AREA_SPCI_SEC_SHM;
			else
				memtype = MEM_AREA_SPCI_NSEC_SHM;
			mbd->va = (vaddr_t)phys_to_virt(mbd->pa, memtype);

			/* Set the buffer state as empty */
			buf = (struct spci_buf *)mbd->va;
			buf->hdr.state = SPCI_BUF_STATE_EMPTY;

			print_buf_desc(mbd);
		}
	}
}

static uint32_t send_optee_spci_std_hdr(uint32_t a0, uint32_t resume_info)
{
	struct optee_spci_std_hdr hdr = {
		.a0 = a0,
		.resume_info = resume_info,
	};

	return spci_msg_send_prepare(&hdr, sizeof(hdr));
}

static uint32_t handle_std_msg(struct optee_spci_std_hdr *hdr,
			       unsigned long len)
{
	struct optee_msg_arg *arg = NULL;
	uint32_t hi = 0;
	uint32_t lo = 0;
	uint32_t rv = 0;

	if (len < sizeof(*hdr))
		panic();

	if (hdr->a0 != OPTEE_SMC_CALL_WITH_ARG &&
	    hdr->a0 != OPTEE_SMC_CALL_RETURN_FROM_RPC)
		panic();

	if (hdr->a0 == OPTEE_SMC_CALL_WITH_ARG || len > sizeof(*hdr)) {
		if (len < sizeof(*hdr) + sizeof(*arg))
			panic();

		arg = (void *)(hdr + 1);
		if (len != sizeof(*hdr) +
			   OPTEE_MSG_GET_ARG_SIZE(arg->num_params))
			panic();
	}

	reg_pair_from_64((vaddr_t)hdr, &hi, &lo);
	if (hdr->a0 == OPTEE_SMC_CALL_RETURN_FROM_RPC) {
		uint32_t resume_info = hdr->resume_info;

		if (len == sizeof(struct optee_spci_std_hdr)) {
			/*
			 * We're returning from a foreign interrupt.  This
			 * is the last spot where a allocated hdr can be
			 * freed since the resumed thread will have all its
			 * registers, the entire state restored.
			 */
			free(hdr);
			hdr = NULL;
			thread_resume_from_rpc(resume_info, 0, 0, 0, 0);
		} else {
			thread_resume_from_rpc(resume_info, hi, lo, len, 0);
		}
		rv = OPTEE_SMC_RETURN_ERESUME;
	} else {
		thread_alloc_and_run(hi, lo, 0, 0);
		rv = OPTEE_SMC_RETURN_ETHREAD_LIMIT;
	}
	free(hdr);

	return send_optee_spci_std_hdr(rv, 0);
}

static uint32_t handle_fast_msg(struct thread_smc_args *args, unsigned long len)
{
	if (len != sizeof(*args))
		panic();

	tee_entry_fast(args);

	/* Only returning the first 4 entries */
	return spci_msg_send_prepare(args, sizeof(*args) / 2);
}

static uint32_t handle_msg(void *msg, unsigned long len)
{
	uint32_t *a0 = msg;

	if (len < sizeof(*a0))
		panic();

	if (OPTEE_SMC_IS_FAST_CALL(*a0))
		return handle_fast_msg(msg, len);
	else
		return handle_std_msg(msg, len);
}

/* Only called from assembly */
uint32_t spci_msg_recv(int32_t status);
uint32_t spci_msg_recv(int32_t status)
{
	size_t max_msg_len = sizeof(struct optee_spci_std_hdr) +
			     OPTEE_MSG_GET_ARG_SIZE(6);
	size_t msg_len = 0;
	uint32_t msg_loc = 0;
	uint32_t  msg_type = 0;
	struct spci_msg_hdr *msg_hdr = NULL;
	struct spci_msg_buf_desc *rx_buf_desc = NULL;
	struct spci_buf *rx_buf = NULL;
	void *p = NULL;

	assert(thread_get_exceptions() & THREAD_EXCP_FOREIGN_INTR);

	/* Check status */
	if (status == SPCI_INVALID_PARAMETER)
		panic();

	/* Panic for now */
	if (status == SPCI_RETRY)
		panic();

	/* FIQ is already supposed to be caught */
	if (status == SPCI_INTERRUPTED)
		panic();

	/* Get a reference to the RX buffer */
	msg_loc = status >> SPCI_MSG_RECV_MSGLOC_SHIFT;
	msg_loc &= SPCI_MSG_RECV_MSGLOC_MASK;
	rx_buf_desc = &buf_desc[msg_loc][SPCI_MEM_REG_ARCH_TYPE_RX];
	rx_buf = (struct spci_buf *)rx_buf_desc->va;

	/*
	 * Get the common message header.
	 * TODO: Assuming there is a single sender and receiver. Hence, sender
	 * and receiver information is not parsed.
	 */
	msg_hdr = (void *)rx_buf->buf;

	/* Get the message payload */
	msg_type = READ_ONCE(msg_hdr->flags) >> SPCI_MSG_TYPE_SHIFT;
	msg_type &= SPCI_MSG_TYPE_MASK;

	/*
	 * Not expecting any architectural messages for now.
	 * TODO: Add arch. messages for power management functions.
	 */
	if (msg_type == SPCI_MSG_TYPE_ARCH)
		panic();

	msg_len = READ_ONCE(msg_hdr->length);
	if (msg_len > max_msg_len)
		panic();

	p = calloc(1, max_msg_len);
	if (!p)
		panic();

	memcpy(p, (void *)msg_hdr->payload, msg_len);

	/* Zero the message memory */
	memset(msg_hdr, 0, sizeof(*msg_hdr) + msg_len);

	/* Release the copied message */
	rx_buf->hdr.state = SPCI_BUF_STATE_EMPTY;

	return handle_msg(p, msg_len);
}

uint32_t spci_msg_send_prepare(const void *msg, size_t msg_len)
{
	uint32_t msg_loc, msg_type, attrs;
	struct spci_msg_hdr *msg_hdr;
	struct spci_msg_buf_desc *tx_buf_desc;
	struct spci_buf *tx_buf;

	assert(thread_get_exceptions() & THREAD_EXCP_FOREIGN_INTR);

	msg_loc = SPCI_MSG_SEND_ATTRS_MSGLOC_NSEC;
	msg_loc	&= SPCI_MSG_SEND_ATTRS_MSGLOC_MASK;
	tx_buf_desc = &buf_desc[msg_loc][SPCI_MEM_REG_ARCH_TYPE_TX];
	tx_buf = (struct spci_buf *)tx_buf_desc->va;

	/* TODO: Assuming UP. Use spinlocks to protect buffer later */
	if (tx_buf->hdr.state != SPCI_BUF_STATE_EMPTY)
		panic();

	/*
	 * Get the common message header.
	 * TODO: Assuming there is a single sender and receiver. Hence, sender
	 * and receiver information is not parsed.
	 */
	msg_hdr = (void *)tx_buf->buf;
	memset(msg_hdr, 0, sizeof(*msg_hdr));

	/* Set the message type. Not expecting architectural messages for now */
	msg_type = SPCI_MSG_TYPE_IMP & SPCI_MSG_TYPE_MASK;
	msg_type <<= SPCI_MSG_TYPE_SHIFT;
	msg_hdr->flags |= msg_type;

	/* Copy the message */
	memcpy(msg_hdr->payload, msg, msg_len);
	msg_hdr->length = msg_len;

	/* Mark the buffer as full */
	tx_buf->hdr.state = SPCI_BUF_STATE_FULL;

	/*
	 * Populate Attributes parameter. TODO: Assume blocking behaviour
	 * without notifications.
	 */
	attrs = msg_loc << SPCI_MSG_SEND_ATTRS_MSGLOC_SHIFT;
	return attrs;
}

/* Called from assembly function thread_foreign_intr_exit() only */
uint32_t spci_msg_send_prepare_foreign_intr(uint32_t thread_index);
uint32_t spci_msg_send_prepare_foreign_intr(uint32_t thread_index)
{
	return send_optee_spci_std_hdr(OPTEE_SMC_RETURN_RPC_FOREIGN_INTR,
				       thread_index);
}

/*
 * Helper routine for the assembly function thread_std_smc_entry()
 *
 * Note: this function is weak just to make it possible to exclude it from
 * the unpaged area.
 */
uint32_t __weak __thread_std_smc_entry(uint32_t a0, uint32_t a1,
				       uint32_t a3 __unused,
				       uint32_t a4 __unused)
{
	struct optee_spci_std_hdr *hdr = NULL;
	struct optee_msg_arg *arg = NULL;
	size_t msg_len = 0;
	uint32_t attrs = 0;

	hdr = (void *)reg_pair_to_64(a0, a1);
	arg = (void *)(hdr + 1);
	hdr->a0 = tee_entry_std(arg, arg->num_params);

	/*
	 * Foreign interrupts must not be enabled when
	 * spci_msg_send_prepare() is called since the SPCI mailbox may be
	 * needed again before this message has been delivered to normal
	 * world.
	 */
	thread_mask_exceptions(THREAD_EXCP_FOREIGN_INTR);

	hdr->resume_info = 0;
	msg_len = sizeof(*hdr);
	if (!hdr->a0)
		msg_len += OPTEE_MSG_GET_ARG_SIZE(arg->num_params);

	attrs = spci_msg_send_prepare(hdr, msg_len);
	free(hdr);

	/*
	 * Pass attributes for spci_msg_send_recv_invoke(), which will be
	 * called from thread_std_smc_entry() when this function has
	 * returned.
	 */
	return attrs;
}

static bool set_rmem(struct optee_msg_param *param,
		     struct thread_param *tpm)
{
	param->attr = tpm->attr - THREAD_PARAM_ATTR_MEMREF_IN +
		      OPTEE_MSG_ATTR_TYPE_RMEM_INPUT;
	param->u.rmem.offs = tpm->u.memref.offs;
	param->u.rmem.size = tpm->u.memref.size;
	if (tpm->u.memref.mobj) {
		param->u.rmem.shm_ref = mobj_get_cookie(tpm->u.memref.mobj);
		if (!param->u.rmem.shm_ref)
			return false;
	} else {
		param->u.rmem.shm_ref = 0;
	}

	return true;
}

static bool set_tmem(struct optee_msg_param *param,
		     struct thread_param *tpm)
{
	paddr_t pa = 0;
	uint64_t shm_ref = 0;
	struct mobj *mobj = tpm->u.memref.mobj;

	param->attr = tpm->attr - THREAD_PARAM_ATTR_MEMREF_IN +
		      OPTEE_MSG_ATTR_TYPE_TMEM_INPUT;
	if (mobj) {
		shm_ref = mobj_get_cookie(mobj);
		if (!shm_ref)
			return false;
		if (mobj_get_pa(mobj, tpm->u.memref.offs, 0, &pa))
			return false;
	}

	param->u.tmem.size = tpm->u.memref.size;
	param->u.tmem.buf_ptr = pa;
	param->u.tmem.shm_ref = shm_ref;

	return true;
}

uint32_t thread_rpc_cmd(uint32_t cmd, size_t num_params,
			struct thread_param *params)
{
	uint64_t data[(sizeof(struct optee_spci_std_hdr) +
		       OPTEE_MSG_GET_ARG_SIZE(4)) / sizeof(uint64_t)] = { 0 };
	struct optee_spci_std_hdr *hdr = (void *)data;
	struct optee_msg_arg *arg = (void *)(hdr + 1);
	size_t n = 0;
	unsigned long ilen = 0;
	unsigned long olen = 0;

	assert(num_params <= 4);

	hdr->a0 = OPTEE_SMC_RETURN_RPC_CMD;
	hdr->resume_info = thread_get_id();
	arg->cmd = cmd;
	arg->num_params = num_params;

	for (n = 0; n < num_params; n++) {
		switch (params[n].attr) {
		case THREAD_PARAM_ATTR_NONE:
			arg->params[n].attr = OPTEE_MSG_ATTR_TYPE_NONE;
			break;
		case THREAD_PARAM_ATTR_VALUE_IN:
		case THREAD_PARAM_ATTR_VALUE_OUT:
		case THREAD_PARAM_ATTR_VALUE_INOUT:
			arg->params[n].attr = params[n].attr -
					      THREAD_PARAM_ATTR_VALUE_IN +
					      OPTEE_MSG_ATTR_TYPE_VALUE_INPUT;
			arg->params[n].u.value.a = params[n].u.value.a;
			arg->params[n].u.value.b = params[n].u.value.b;
			arg->params[n].u.value.c = params[n].u.value.c;
			break;
		case THREAD_PARAM_ATTR_MEMREF_IN:
		case THREAD_PARAM_ATTR_MEMREF_OUT:
		case THREAD_PARAM_ATTR_MEMREF_INOUT:
			if (!params[n].u.memref.mobj ||
			    mobj_matches(params[n].u.memref.mobj,
					 CORE_MEM_NSEC_SHM)) {
				if (!set_tmem(arg->params + n, params + n))
					return TEE_ERROR_BAD_PARAMETERS;
			} else  if (mobj_matches(params[n].u.memref.mobj,
						 CORE_MEM_REG_SHM)) {
				if (!set_rmem(arg->params + n, params + n))
					return TEE_ERROR_BAD_PARAMETERS;
			} else {
				return TEE_ERROR_BAD_PARAMETERS;
			}
			break;
		default:
			return TEE_ERROR_BAD_PARAMETERS;
		}
	}

	ilen = sizeof(*hdr) + OPTEE_MSG_GET_ARG_SIZE(num_params);
	olen = thread_rpc(data, ilen);
	if (olen != ilen)
		return TEE_ERROR_COMMUNICATION;

	for (n = 0; n < num_params; n++) {
		switch (params[n].attr) {
		case THREAD_PARAM_ATTR_VALUE_OUT:
		case THREAD_PARAM_ATTR_VALUE_INOUT:
			params[n].u.value.a = arg->params[n].u.value.a;
			params[n].u.value.b = arg->params[n].u.value.b;
			params[n].u.value.c = arg->params[n].u.value.c;
			break;
		case THREAD_PARAM_ATTR_MEMREF_OUT:
		case THREAD_PARAM_ATTR_MEMREF_INOUT:
			/*
			 * rmem.size and tmem.size is the same type and
			 * location.
			 */
			params[n].u.memref.size = arg->params[n].u.rmem.size;
			break;
		default:
			break;
		}
	}

	return arg->ret;
}

/* Called from assembly function thread_rpc() only */
uint32_t thread_rpc_return_fixup(uint32_t hi, uint32_t lo, uint32_t len,
				 void *orig_msg, unsigned long orig_len);
uint32_t thread_rpc_return_fixup(uint32_t hi, uint32_t lo, uint32_t len,
				 void *orig_msg, unsigned long orig_len)
{
	void *msg = (void *)(vaddr_t)reg_pair_to_64(hi, lo);

	memcpy(orig_msg, msg, MIN(len, orig_len));
	free(msg);

	return len;
}

static void thread_rpc_free(unsigned int bt, uint64_t cookie, struct mobj *mobj)
{
	uint64_t data[(sizeof(struct optee_spci_std_hdr) +
		       OPTEE_MSG_GET_ARG_SIZE(1)) / sizeof(uint64_t)] = { 0 };
	struct optee_spci_std_hdr *hdr = (void *)data;
	struct optee_msg_arg *arg = (void *)(hdr + 1);
	unsigned long ilen = sizeof(*hdr) + OPTEE_MSG_GET_ARG_SIZE(1);

	hdr->a0 = OPTEE_SMC_RETURN_RPC_CMD;
	hdr->resume_info = thread_get_id();
	arg->cmd = OPTEE_RPC_CMD_SHM_FREE;
	arg->params[0] = (struct optee_msg_param){
		.attr = OPTEE_MSG_ATTR_TYPE_VALUE_INPUT,
		.u.value = { .a = bt, .b = cookie, },
	};
	arg->num_params = 1;

	mobj_free(mobj);
	thread_rpc(data, ilen);
}

static struct mobj *thread_rpc_alloc(size_t size, size_t align, unsigned int bt)
{
	uint64_t data[(sizeof(struct optee_spci_std_hdr) +
		       OPTEE_MSG_GET_ARG_SIZE(1)) / sizeof(uint64_t)] = { 0 };
	struct optee_spci_std_hdr *hdr = (void *)data;
	struct optee_msg_arg *arg = (void *)(hdr + 1);
	unsigned long ilen = sizeof(*hdr) + OPTEE_MSG_GET_ARG_SIZE(1);
	struct mobj *mobj = NULL;
	unsigned long olen = 0;
	uint64_t cookie = 0;

	hdr->a0 = OPTEE_SMC_RETURN_RPC_CMD;
	hdr->resume_info = thread_get_id();
	arg->cmd = OPTEE_RPC_CMD_SHM_ALLOC;
	arg->params[0] = (struct optee_msg_param){
		.attr = OPTEE_MSG_ATTR_TYPE_VALUE_INPUT,
		.u.value = { .a = bt, .b = size, .c = align },
	};
	arg->num_params = 1;

	olen = thread_rpc(data, ilen);
	if (olen != ilen || arg->ret || arg->num_params != 1)
		return NULL;

	if (arg->params[0].attr == OPTEE_MSG_ATTR_TYPE_TMEM_OUTPUT) {
		cookie = arg->params[0].u.tmem.shm_ref;
		mobj = mobj_shm_alloc(arg->params[0].u.tmem.buf_ptr,
				      arg->params[0].u.tmem.size,
				      cookie);
	} else if (arg->params[0].attr == (OPTEE_MSG_ATTR_TYPE_TMEM_OUTPUT |
					   OPTEE_MSG_ATTR_NONCONTIG)) {
		cookie = arg->params[0].u.tmem.shm_ref;
		mobj = msg_param_mobj_from_noncontig(
			arg->params[0].u.tmem.buf_ptr,
			arg->params[0].u.tmem.size,
			cookie,
			true);
	} else {
		return NULL;
	}

	if (!mobj) {
		thread_rpc_free(bt, cookie, NULL);
		return NULL;
	}

	assert(mobj_is_nonsec(mobj));

	return mobj;
}

struct mobj *thread_rpc_alloc_payload(size_t size)
{
	return thread_rpc_alloc(size, 8, OPTEE_RPC_SHM_TYPE_APPL);
}

void thread_rpc_free_payload(struct mobj *mobj)
{
	thread_rpc_free(OPTEE_RPC_SHM_TYPE_APPL, mobj_get_cookie(mobj),
			mobj);
}

struct mobj *thread_rpc_alloc_global_payload(size_t size)
{
	return thread_rpc_alloc(size, 8, OPTEE_RPC_SHM_TYPE_GLOBAL);
}

void thread_rpc_free_global_payload(struct mobj *mobj)
{
	thread_rpc_free(OPTEE_RPC_SHM_TYPE_GLOBAL, mobj_get_cookie(mobj),
			mobj);
}

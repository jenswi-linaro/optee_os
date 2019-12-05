/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2019, Arm Limited. All rights reserved.
 */

#include <assert.h>
#include <io.h>
#include <kernel/interrupt.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <mm/core_mmu.h>
#include <mm/mobj.h>
#include <optee_msg.h>
#include <optee_rpc_cmd.h>
#include <optee_spci.h>
#include <spci.h>
#include <string.h>
#include <sys/queue.h>
#include <tee/entry_std.h>

#include "thread_private.h"

struct spmc_constituent_mem_region {
	uint64_t addr;
	uint32_t page_count;
} __packed;

struct spmc_mem_region_attr {
	uint16_t recv_id;
	uint16_t mem_attrs;
};

struct spmc_mem_region_descr {
	uint32_t tag;
	uint32_t flags;
	uint32_t total_page_count;
	uint32_t constituent_mem_region_count;
	uint32_t constituent_mem_region_offs;
	uint32_t mem_region_attr_count;
	struct spmc_mem_region_attr attrs[];
};

static unsigned int rpc_arg_shm_lock = SPINLOCK_UNLOCK;
static uint32_t rpc_arg_shm_cookie;
static struct mobj *rpc_arg_shm_mobj;


static uint32_t swap_src_dst(uint32_t src_dst)
{
	return (src_dst >> 16) | (src_dst << 16);
}

static void set_args(struct thread_smc_args *args, uint32_t fid,
		     uint32_t src_dst, uint32_t w3, uint32_t w4, uint32_t w5)
{
	*args = (struct thread_smc_args){ .a0 = fid,
					  .a1 = src_dst,
					  .a3 = w3,
					  .a4 = w4,
					  .a5 = w5, };
}

static void handle_register_shm(struct thread_smc_args *args)
{
	uint32_t ret_val = SPCI_INVALID_PARAMETER;
	uint32_t ret_fid =  SPCI_ERROR;
	struct mobj *mobj = NULL;
	bool had_cookie = false;

	if (args->a5 == OPTEE_SPCI_SHM_TYPE_RPC) {
		cpu_spin_lock(&rpc_arg_shm_lock);
		if (rpc_arg_shm_cookie)
			had_cookie = true;
		else
			rpc_arg_shm_cookie = args->a4;
		cpu_spin_unlock(&rpc_arg_shm_lock);
		if (had_cookie)
			goto out;
	}

	mobj = mobj_reg_shm_add(args->a4);
	if (!mobj) {
		if (args->a5 == OPTEE_SPCI_SHM_TYPE_RPC) {
			cpu_spin_lock(&rpc_arg_shm_lock);
			rpc_arg_shm_cookie = 0;
			cpu_spin_unlock(&rpc_arg_shm_lock);
		}
		goto out;
	}

	/*
	 * If it's shared memory to be used for RPC argument passing we
	 * need to check it's large enough and map it.
	 */
	if (args->a5 == OPTEE_SPCI_SHM_TYPE_RPC) {
		size_t s = OPTEE_MSG_GET_ARG_SIZE(THREAD_RPC_MAX_NUM_PARAMS) *
			   CFG_NUM_THREADS;

		if (mobj->size < s) {
			mobj_put(mobj);
			ret_val = SPCI_NOT_SUPPORTED;
			goto out;
		}

		if (mobj_reg_shm_inc_map(mobj)) {
			mobj_put(mobj);
			ret_val = SPCI_NO_MEMORY;
			goto out;
		}
	}

	ret_fid =  SPCI_SUCCESS;
	ret_val = 0;
out:
	set_args(args, ret_fid, swap_src_dst(args->a1), ret_val, 0, 0);
}

static void handle_unregister_shm(struct thread_smc_args *args)
{
	uint32_t ret_val = SPCI_INVALID_PARAMETER;
	uint32_t ret_fid =  SPCI_ERROR;

	cpu_spin_lock(&rpc_arg_shm_lock);
	/*
	 * If it's the shared memory used for RPC argument passing we clear
	 * it out here to avoid new users of the mobj.
	 */
	if (rpc_arg_shm_cookie == args->a4) {
		rpc_arg_shm_cookie = 0;
		rpc_arg_shm_mobj = NULL;
	}
	cpu_spin_unlock(&rpc_arg_shm_lock);

	if (!mobj_reg_shm_release_by_cookie(args->a4)) {
		ret_fid =  SPCI_SUCCESS;
		ret_val = 0;
	}
	set_args(args, ret_fid, swap_src_dst(args->a1), ret_val, 0, 0);
}

static void handle_yielding_call(struct thread_smc_args *args)
{
	uint32_t ret_val = 0;

	thread_check_canaries();

	switch (args->a4) {
	case OPTEE_SPCI_YIELDING_CALL_START: /* A normal call */
		thread_alloc_and_run(args->a5, args->a1, args->a6, 0);
		ret_val = SPCI_BUSY;
		break;
	case OPTEE_SPCI_YIELDING_CALL_RESUME: /* Resuming from RPC */
		thread_resume_from_rpc(args->a5, args->a1, 0, 0, 0);
		ret_val = SPCI_INVALID_PARAMETER;
		break;
	default:
		ret_val = SPCI_DENIED;
	}

	set_args(args, SPCI_ERROR, swap_src_dst(args->a1), ret_val, 0, 0);
}

static void handle_direct_req(struct thread_smc_args *args)
{
	switch (args->a3) {
	case OPTEE_SPCI_GET_API_VERSION:
		set_args(args, SPCI_MSG_SEND_DIRECT_RESP_SMC32,
			 swap_src_dst(args->a1), OPTEE_SPCI_VERSION_MAJOR,
			 OPTEE_SPCI_VERSION_MINOR, 0);
		break;
	case OPTEE_SPCI_GET_OS_VERSION:
		set_args(args, SPCI_MSG_SEND_DIRECT_RESP_SMC32,
			 swap_src_dst(args->a1), CFG_OPTEE_REVISION_MAJOR,
			 CFG_OPTEE_REVISION_MINOR, TEE_IMPL_GIT_SHA1);
		break;
	case OPTEE_SPCI_EXCHANGE_CAPABILITIES:
		set_args(args, SPCI_MSG_SEND_DIRECT_RESP_SMC32,
			 swap_src_dst(args->a1), 0, 0, 0);
		break;
	case OPTEE_SPCI_REGISTER_SHM:
		handle_register_shm(args);
		break;
	case OPTEE_SPCI_UNREGISTER_SHM:
		handle_unregister_shm(args);
		break;
	case OPTEE_SPCI_YIELDING_CALL:
		handle_yielding_call(args);
		break;
	default:
		EMSG("Unhandled service ID %#"PRIx32, (uint32_t)args->a3);
		panic();
	}
}

static int get_attrs(struct spmc_mem_region_attr *attrs,
		     unsigned int num_attrs, uint16_t *mem_attrs)
{
	unsigned int n = 0;

	for (n = 0; n < num_attrs; n++) {
		if (attrs[n].recv_id == 0) { /* TODO */
			*mem_attrs = attrs[n].mem_attrs;
			return 0;
		}
	}

	return SPCI_INVALID_PARAMETER;
}

static int get_pages(struct spmc_constituent_mem_region *regions,
		     unsigned int num_regions, paddr_t *pages,
		     unsigned int num_pages)
{
	unsigned int tot_np = 0;
	unsigned int np = 0;
	unsigned int n = 0;

	for (n = 0; n < num_regions; n++) {
		unsigned int reg_page_count = READ_ONCE(regions[n].page_count);
		uint64_t reg_page_addr = READ_ONCE(regions[n].addr);
		size_t s = 0;

		if (MUL_OVERFLOW(reg_page_count, SMALL_PAGE_SIZE, &s) ||
		    !core_pbuf_is(CORE_MEM_NON_SEC, reg_page_addr, s))
				return TEE_ERROR_BAD_PARAMETERS;

		for (np = 0; np < reg_page_count; np++) {
			if (tot_np >= num_pages)
				return TEE_ERROR_BAD_PARAMETERS;
			pages[tot_np] = reg_page_addr + np * SMALL_PAGE_SIZE;
			tot_np++;
		}
	}

	if (tot_np != num_pages)
		return SPCI_INVALID_PARAMETER;

	return 0;
}

static int register_mem_share(struct spmc_mem_region_descr *descr,
			      size_t descr_len, uint32_t *global_handle)
{
	const uint16_t exp_mem_attrs = BIT(6) | /* Read-write not executable */
				       BIT(4) | /* Normal memory */
				       BIT(3) | /* Write-through */
				       BIT(1) | BIT(0); /* Inner shareable */
	struct mobj_reg_shm *reg_shm = NULL;
	unsigned int num_regions = 0;
	unsigned int region_offs = 0;
	unsigned int num_pages = 0;
	unsigned int num_attrs = 0;
	uint16_t mem_attrs = 0;
	paddr_t *pages = NULL;
	size_t n = 0;

	if (sizeof(*descr) < descr_len)
		return SPCI_INVALID_PARAMETER;
	num_pages = READ_ONCE(descr->total_page_count);
	num_attrs = READ_ONCE(descr->mem_region_attr_count);
	num_regions = READ_ONCE(descr->constituent_mem_region_count);
	region_offs = READ_ONCE(descr->constituent_mem_region_offs);

	/* Check that the attributes fit */
	if (ADD_OVERFLOW(num_attrs, 1, &n) ||
	    MUL_OVERFLOW(sizeof(struct spmc_mem_region_attr), n, &n) ||
	    ADD_OVERFLOW(sizeof(*descr), n, &n) || n > descr_len)
		return SPCI_INVALID_PARAMETER;

	/* Check that the attributes matches what's expected */
	if (get_attrs(descr->attrs, num_attrs, &mem_attrs) ||
	    mem_attrs != exp_mem_attrs)
		return SPCI_INVALID_PARAMETER;

	/* Check that the constituent regions fit */
	if (ADD_OVERFLOW(num_regions, 1, &n) ||
	    MUL_OVERFLOW(sizeof(struct spmc_constituent_mem_region), n, &n) ||
	    ADD_OVERFLOW(region_offs, n, &n) ||
	    ADD_OVERFLOW(sizeof(*descr), n, &n) || n > descr_len)
		return SPCI_INVALID_PARAMETER;

	reg_shm = mobj_reg_shm_new(num_pages, &pages, global_handle);
	if (!reg_shm)
		return SPCI_NO_MEMORY;

	if (get_pages((void *)((vaddr_t)descr + region_offs), num_regions,
		      pages, num_pages)) {
		mobj_reg_shm_delete(reg_shm);
		return SPCI_INVALID_PARAMETER;
	}

	return 0;
}

static void handle_mem_share(struct thread_smc_args *args)
{
	uint32_t ret_val = SPCI_INVALID_PARAMETER;
	uint32_t ret_fid =  SPCI_ERROR;
	paddr_t paddr = args->a1;
	unsigned int page_count = args->a2;
	size_t len = 0;
	uint32_t global_handle = 0;
	tee_mm_entry_t *mm = NULL;

	/* Check that it's not fragmented and that the MBZs are indeed 0 */
	if (args->a3 || args->a5 || args->a6 || args->a7)
		goto err;

	if (MUL_OVERFLOW(page_count, SMALL_PAGE_SIZE, &len))
		goto err;
	if (!core_pbuf_is(CORE_MEM_NON_SEC, paddr, len))
		goto err;
	mm = tee_mm_alloc(&tee_mm_shm, len);
	if (!mm) {
		ret_val = SPCI_NO_MEMORY;
		goto err;
	}
	if (core_mmu_map_contiguous_pages(tee_mm_get_smem(mm), paddr,
					  page_count, MEM_AREA_NSEC_SHM))
		goto err;

	ret_val = register_mem_share((void *)tee_mm_get_smem(mm), len,
				     &global_handle);
	if (!ret_val) {
		ret_fid =  SPCI_SUCCESS;
		ret_val = global_handle;
	}

	core_mmu_unmap_pages(tee_mm_get_smem(mm), page_count);

err:
	tee_mm_free(mm);
	set_args(args, ret_fid, 0, ret_val, 0, 0);
}

/* Only called from assembly */
void spmc_msg_recv(struct thread_smc_args *args);
void spmc_msg_recv(struct thread_smc_args *args)
{
	switch (args->a0) {
	case SPCI_INTERRUPT:
		itr_core_handler();
		set_args(args, SPCI_SUCCESS, args->a1, 0, 0, 0);
		break;
	case SPCI_MSG_SEND_DIRECT_REQ_SMC32:
		handle_direct_req(args);
		break;
	case SPCI_MEM_SHARE_SMC32:
		handle_mem_share(args);
		break;
	case SPCI_ERROR:
	case SPCI_SUCCESS:
	default:
		EMSG("Unhandled SPCI function ID %#"PRIx32, (uint32_t)args->a0);
		panic();
	}
}

/*
 * Helper routine for the assembly function thread_std_smc_entry()
 *
 * Note: this function is weak just to make it possible to exclude it from
 * the unpaged area.
 */
uint32_t __weak __thread_std_smc_entry(uint32_t a0, uint32_t a1, uint32_t a2,
				       uint32_t a3 __unused)
{
	struct thread_specific_data *tsd = thread_get_tsd();
	uint32_t src_dst __unused = a0;
	uint32_t rv = TEE_ERROR_BAD_PARAMETERS;
	struct optee_msg_arg *arg = NULL;
	struct mobj *mobj = NULL;
	uint32_t num_params = 0;
	size_t offs = 0;

	cpu_spin_lock(&rpc_arg_shm_lock);
	tsd->rpc_arg_mobj = mobj_get(rpc_arg_shm_mobj);
	cpu_spin_unlock(&rpc_arg_shm_lock);
	if (!tsd->rpc_arg_mobj) {
		EMSG("RPC shared memory object not found");
		return SPCI_DENIED;
	}

	mobj = mobj_reg_shm_get_by_cookie(a1);
	if (!mobj)
		goto out_put_reg_shm_mobj;

	rv = mobj_reg_shm_inc_map(mobj);
	if (rv)
		goto out_put_mobj;

	arg = mobj_get_va(mobj, a2);
	if (!arg)
		goto out_dec_map;


	if (ADD_OVERFLOW(a2, sizeof(*arg), &offs) || !mobj_get_va(mobj, offs))
		goto out_dec_map;

	num_params = READ_ONCE(arg->num_params);
	if (num_params > OPTEE_MSG_MAX_NUM_PARAMS)
		goto out_dec_map;

	if (ADD_OVERFLOW(a2, OPTEE_MSG_GET_ARG_SIZE(num_params), &offs) ||
	    !mobj_get_va(mobj, offs))
		goto out_dec_map;

	rv = tee_entry_std(arg, num_params);

out_dec_map:
	mobj_reg_shm_dec_map(mobj);
out_put_mobj:
	mobj_put(mobj);
out_put_reg_shm_mobj:
	mobj_put(tsd->rpc_arg_mobj);
	tsd->rpc_arg_mobj = NULL;
	return rv;

}

static bool set_rmem(struct optee_msg_param *param, struct thread_param *tpm)
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

static uint32_t get_rpc_arg(uint32_t cmd, size_t num_params,
			    struct thread_param *params,
			    struct optee_msg_arg **arg_ret,
			    uint32_t *carg_ret, uint32_t *shm_offs_ret)
{
	size_t sz = OPTEE_MSG_GET_ARG_SIZE(THREAD_RPC_MAX_NUM_PARAMS);
	struct thread_specific_data *tsd = NULL;
	struct optee_msg_arg *arg = NULL;
	int thr_id = thread_get_id();
	size_t shm_offs = sz * thr_id;

	if (num_params > THREAD_RPC_MAX_NUM_PARAMS)
		return TEE_ERROR_BAD_PARAMETERS;


	tsd = &threads[thr_id].tsd;
	arg = mobj_get_va(tsd->rpc_arg_mobj, shm_offs);
	assert(arg);

	memset(arg, 0, sz);
	arg->cmd = cmd;
	arg->num_params = num_params;
	arg->ret = TEE_ERROR_GENERIC; /* in case value isn't updated */

	for (size_t n = 0; n < num_params; n++) {
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
			if (!set_rmem(arg->params + n, params + n))
				return TEE_ERROR_BAD_PARAMETERS;
			break;
		default:
			return TEE_ERROR_BAD_PARAMETERS;
		}
	}

	*arg_ret = arg;
	*carg_ret = mobj_get_cookie(tsd->rpc_arg_mobj);
	*shm_offs_ret = shm_offs;

	return TEE_SUCCESS;
}

static uint32_t get_rpc_arg_res(struct optee_msg_arg *arg, size_t num_params,
				struct thread_param *params)
{
	for (size_t n = 0; n < num_params; n++) {
		switch (params[n].attr) {
		case THREAD_PARAM_ATTR_VALUE_OUT:
		case THREAD_PARAM_ATTR_VALUE_INOUT:
			params[n].u.value.a = arg->params[n].u.value.a;
			params[n].u.value.b = arg->params[n].u.value.b;
			params[n].u.value.c = arg->params[n].u.value.c;
			break;
		case THREAD_PARAM_ATTR_MEMREF_OUT:
		case THREAD_PARAM_ATTR_MEMREF_INOUT:
			params[n].u.memref.size = arg->params[n].u.rmem.size;
			break;
		default:
			break;
		}
	}

	return arg->ret;
}

uint32_t thread_rpc_cmd(uint32_t cmd, size_t num_params,
			struct thread_param *params)
{
	uint32_t shm_offs = 0;
	uint32_t carg = 0;
	struct optee_msg_arg *arg = NULL;
	uint32_t ret = 0;

	ret = get_rpc_arg(cmd, num_params, params, &arg, &carg, &shm_offs);
	if (ret)
		return ret;

	thread_spmc_rpc(carg, shm_offs);

	return get_rpc_arg_res(arg, num_params, params);
}

static void thread_rpc_free(unsigned int bt, uint32_t cookie, struct mobj *mobj)
{
	struct thread_param param = THREAD_PARAM_VALUE(IN, bt, cookie, 0);

	mobj_put(mobj);
	thread_rpc_cmd(OPTEE_RPC_CMD_SHM_FREE, 1, &param);
}

static struct mobj *thread_rpc_alloc(size_t size, unsigned int bt)
{
	struct thread_param param = THREAD_PARAM_VALUE(IN, bt, size, 0);
	struct mobj *mobj = NULL;
	uint32_t shm_offs = 0;
	uint64_t shm_ref = 0;
	uint32_t carg = 0;
	struct optee_msg_arg *arg = NULL;
	uint32_t ret = 0;

	ret = get_rpc_arg(OPTEE_RPC_CMD_SHM_ALLOC, 1, &param, &arg, &carg,
			  &shm_offs);
	if (ret)
		return NULL;

	thread_spmc_rpc(carg, shm_offs);

	if (READ_ONCE(arg->ret) || READ_ONCE(arg->num_params) != 1 ||
	    READ_ONCE(arg->params->attr) != OPTEE_MSG_ATTR_TYPE_RMEM_OUTPUT)
		return NULL;

	shm_ref = READ_ONCE(arg->params->u.rmem.shm_ref);
	mobj = mobj_reg_shm_add(shm_ref);
	if (!mobj) {
		thread_rpc_free(bt, shm_ref, NULL);
		return NULL;
	}

	assert(mobj_is_nonsec(mobj));

	return mobj;
}

struct mobj *thread_rpc_alloc_payload(size_t size)
{
	return thread_rpc_alloc(size, OPTEE_RPC_SHM_TYPE_APPL);
}

void thread_rpc_free_payload(struct mobj *mobj)
{
	thread_rpc_free(OPTEE_RPC_SHM_TYPE_APPL, mobj_get_cookie(mobj), mobj);
}

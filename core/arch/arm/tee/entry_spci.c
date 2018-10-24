// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2018, Linaro Limited
 */

#include <bench.h>
#include <io.h>
#include <kernel/tee_ta_manager.h>
#include <kernel/thread.h>
#include <mm/mobj.h>
#include <optee_spci.h>
#include <spci.h>
#include <string.h>
#include <tee/entry_spci.h>
#include <tee/tee_cryp_utl.h>
#include <tee/uuid.h>
#include <types_ext.h>

/* Sessions opened from normal world */
static struct tee_ta_session_head tee_open_sessions =
TAILQ_HEAD_INITIALIZER(tee_open_sessions);

static unsigned int session_pnum;

/*
 * This function receives all requests by value
 */
static void entry_spci_value(struct thread_smc_args *args)
{
	switch (args->a1) {
	case OPTEE_SPCI_VAL_REQ_GET_OS_REVISION:
		args->a0 = SPCI_SUCCESS;
		args->a1 = CFG_OPTEE_REVISION_MAJOR;
		args->a2 = CFG_OPTEE_REVISION_MINOR;
		args->a3 = TEE_IMPL_GIT_SHA1;
		break;
	case OPTEE_SPCI_VAL_REQ_EXCHANGE_CAPABILITIES:
		if (args->a2 || args->a3 || args->a4 || args->a5) {
			args->a0 = SPCI_INVALID_PARAMETER;
			break;
		}
		args->a0 = SPCI_SUCCESS;
		args->a1 = 0;
		args->a2 = 0;
		args->a3 = 0;
		break;
	default:
		args->a0 = SPCI_NOT_SUPPORTED;
	};
}

static void entry_shm_unregister(struct thread_smc_args *args)
{
	uint64_t cookie = READ_ONCE(args->a1);
	TEE_Result res = mobj_reg_shm_try_release_by_cookie(cookie);

	if (res) {
		EMSG("Failed to unregister cookie %#" PRIx64, cookie);
		if (res == TEE_ERROR_BUSY)
			args->a0 = SPCI_DENIED;
		else
			args->a0 = SPCI_INVALID_PARAMETER;
	} else {
		args->a0 = SPCI_SUCCESS;
	}
}

void tee_entry_spci_fast(struct thread_smc_args *args)
{
	switch (args->a0) {
	case SPCI_REQUEST_BLOCKING_BY_VAL_32:
	case SPCI_REQUEST_BLOCKING_BY_VAL_64:
		entry_spci_value(args);
		break;
	case SPCI_SHM_UNREGISTER_32:
	case SPCI_SHM_UNREGISTER_64:
		entry_shm_unregister(args);
		break;
	default:
		args->a0 = SPCI_NOT_SUPPORTED;
	}
}

static TEE_Result set_memref(const struct optee_spci_param_memref *memref,
			     struct param_mem *mem)
{
	uint64_t cookie = READ_ONCE(memref->shm_ref);

	mem->mobj = mobj_reg_shm_get_by_cookie(cookie);
	if (!mem->mobj)
		return TEE_ERROR_BAD_PARAMETERS;

	mem->offs = READ_ONCE(memref->offs);
	mem->size = READ_ONCE(memref->size);

	return TEE_SUCCESS;
}

static TEE_Result copy_in_params(const struct optee_spci_param *params,
				 uint32_t num_params,
				 struct tee_ta_param *ta_param)
{
	TEE_Result res;
	size_t n;
	uint8_t pt[TEE_NUM_PARAMS] = { 0 };

	if (num_params > TEE_NUM_PARAMS)
		return TEE_ERROR_BAD_PARAMETERS;

	for (n = 0; n < num_params; n++) {
		uint32_t attr = READ_ONCE(params[n].attr);

		if (attr & OPTEE_SPCI_ATTR_META)
			return TEE_ERROR_BAD_PARAMETERS;

		switch (attr & OPTEE_SPCI_ATTR_TYPE_MASK) {
		case OPTEE_SPCI_ATTR_TYPE_NONE:
			pt[n] = TEE_PARAM_TYPE_NONE;
			break;
		case OPTEE_SPCI_ATTR_TYPE_VALUE_INPUT:
		case OPTEE_SPCI_ATTR_TYPE_VALUE_OUTPUT:
		case OPTEE_SPCI_ATTR_TYPE_VALUE_INOUT:
			pt[n] = TEE_PARAM_TYPE_VALUE_INPUT + attr -
				OPTEE_SPCI_ATTR_TYPE_VALUE_INPUT;
			ta_param->u[n].val.a = READ_ONCE(params[n].u.value.a);
			ta_param->u[n].val.b = READ_ONCE(params[n].u.value.b);
			break;
		case OPTEE_SPCI_ATTR_TYPE_MEMREF_INPUT:
		case OPTEE_SPCI_ATTR_TYPE_MEMREF_OUTPUT:
		case OPTEE_SPCI_ATTR_TYPE_MEMREF_INOUT:
			res = set_memref(&params[n].u.memref,
					 &ta_param->u[n].mem);
			if (res)
				return res;
			pt[n] = TEE_PARAM_TYPE_MEMREF_INPUT + attr -
				OPTEE_SPCI_ATTR_TYPE_MEMREF_INPUT;
			break;
		default:
			return TEE_ERROR_BAD_PARAMETERS;
		}
	}

	ta_param->types = TEE_PARAM_TYPES(pt[0], pt[1], pt[2], pt[3]);

	return TEE_SUCCESS;
}

static void cleanup_shm_refs(struct tee_ta_param *param)
{
	for (size_t n = 0; n < TEE_NUM_PARAMS; n++) {
		switch (TEE_PARAM_TYPE_GET(param->types, n)) {
		case TEE_PARAM_TYPE_MEMREF_INPUT:
		case TEE_PARAM_TYPE_MEMREF_OUTPUT:
		case TEE_PARAM_TYPE_MEMREF_INOUT:
			mobj_reg_shm_put(param->u[n].mem.mobj);
			break;
		default:
			break;
		}
	}
}

static void copy_out_param(struct tee_ta_param *ta_param, uint32_t num_params,
			   struct optee_spci_param *params)
{
	for (size_t n = 0; n < MIN(num_params, (size_t)TEE_NUM_PARAMS); n++) {
		switch (TEE_PARAM_TYPE_GET(ta_param->types, n)) {
		case TEE_PARAM_TYPE_MEMREF_OUTPUT:
		case TEE_PARAM_TYPE_MEMREF_INOUT:
			params[n].u.memref.size = ta_param->u[n].mem.size;
			break;
		case TEE_PARAM_TYPE_VALUE_OUTPUT:
		case TEE_PARAM_TYPE_VALUE_INOUT:
			params[n].u.value.a = ta_param->u[n].val.a;
			params[n].u.value.b = ta_param->u[n].val.b;
			break;
		default:
			break;
		}
	}
}

/*
 * Extracts mandatory parameter for open session.
 *
 * Returns
 * false : mandatory parameter wasn't found or malformatted
 * true  : paramater found and OK
 */
static TEE_Result get_open_session_meta(size_t num_params,
					struct optee_spci_param *params,
					size_t *num_meta, TEE_UUID *uuid,
					TEE_Identity *clnt_id)
{
	const uint32_t req_attr = OPTEE_SPCI_ATTR_META |
				  OPTEE_SPCI_ATTR_TYPE_VALUE_INPUT;

	if (num_params < 2)
		return TEE_ERROR_BAD_PARAMETERS;

	if (READ_ONCE(params[0].attr) != req_attr ||
	    READ_ONCE(params[1].attr) != req_attr)
		return TEE_ERROR_BAD_PARAMETERS;

	tee_uuid_from_octets(uuid, (void *)&params[0].u.value);
	clnt_id->login = READ_ONCE(params[1].u.value.c);
	switch (clnt_id->login) {
	case TEE_LOGIN_PUBLIC:
		memset(&clnt_id->uuid, 0, sizeof(clnt_id->uuid));
		break;
	case TEE_LOGIN_USER:
	case TEE_LOGIN_GROUP:
	case TEE_LOGIN_APPLICATION:
	case TEE_LOGIN_APPLICATION_USER:
	case TEE_LOGIN_APPLICATION_GROUP:
		tee_uuid_from_octets(&clnt_id->uuid,
				     (void *)&params[1].u.value);
		break;
	default:
		return TEE_ERROR_BAD_PARAMETERS;
	}

	*num_meta = 2;
	return TEE_SUCCESS;
}

static TEE_Result entry_open_session(uint32_t num_params,
				     struct optee_spci_param *params,
				     uint32_t *session,
				     TEE_ErrorOrigin *err_orig_ret)
{
	TEE_Result res;
	TEE_ErrorOrigin err_orig = TEE_ORIGIN_TEE;
	struct tee_ta_session *s = NULL;
	TEE_Identity clnt_id;
	TEE_UUID uuid;
	struct tee_ta_param param = { 0 };
	size_t num_meta;

	res = get_open_session_meta(num_params, params, &num_meta, &uuid,
				    &clnt_id);
	if (res != TEE_SUCCESS)
		goto out;

	res = copy_in_params(params + num_meta, num_params - num_meta, &param);
	if (res != TEE_SUCCESS)
		goto cleanup_shm_refs;

	res = tee_ta_open_session(&err_orig, &s, &tee_open_sessions, &uuid,
				  &clnt_id, TEE_TIMEOUT_INFINITE, &param);
	if (res != TEE_SUCCESS)
		s = NULL;
	copy_out_param(&param, num_params - num_meta, params + num_meta);

	/*
	 * The occurrence of open/close session command is usually
	 * un-predictable, using this property to increase randomness
	 * of prng
	 */
	plat_prng_add_jitter_entropy(CRYPTO_RNG_SRC_JITTER_SESSION,
				     &session_pnum);

cleanup_shm_refs:
	cleanup_shm_refs(&param);

out:
	*session = (vaddr_t)s;
	*err_orig_ret = err_orig;

	return res;
}

static TEE_Result entry_close_session(uint32_t session)
{
	plat_prng_add_jitter_entropy(CRYPTO_RNG_SRC_JITTER_SESSION,
				     &session_pnum);

	return tee_ta_close_session((struct tee_ta_session *)(vaddr_t)session,
				    &tee_open_sessions, NSAPP_IDENTITY);
}

static TEE_Result entry_invoke_command(uint32_t func, uint32_t session,
				     uint32_t num_params,
				     struct optee_spci_param *params,
				     TEE_ErrorOrigin *err_orig_ret)
{
	TEE_Result res;
	TEE_ErrorOrigin err_orig = TEE_ORIGIN_TEE;
	struct tee_ta_session *s;
	struct tee_ta_param param = { 0 };

	bm_timestamp();

	res = copy_in_params(params, num_params, &param);
	if (res)
		goto out;

	s = tee_ta_get_session(session, true, &tee_open_sessions);
	if (!s) {
		res = TEE_ERROR_BAD_PARAMETERS;
		goto out;
	}

	res = tee_ta_invoke_command(&err_orig, s, NSAPP_IDENTITY,
				    TEE_TIMEOUT_INFINITE, func, &param);

	bm_timestamp();

	tee_ta_put_session(s);

	copy_out_param(&param, num_params, params);

out:
	cleanup_shm_refs(&param);

	*err_orig_ret = err_orig;

	return res;
}

static TEE_Result entry_cancel(uint32_t session, TEE_ErrorOrigin *err_orig)
{
	TEE_Result res;
	struct tee_ta_session *s;

	s = tee_ta_get_session(session, false, &tee_open_sessions);
	if (!s) {
		*err_orig = TEE_ORIGIN_TEE;
		return TEE_ERROR_BAD_PARAMETERS;
	}

	res = tee_ta_cancel_command(err_orig, s, NSAPP_IDENTITY);
	tee_ta_put_session(s);

	return res;
}


static void dispatch_cmd(uint32_t cmd, uint32_t func, uint32_t *session,
			 uint32_t num_params, struct optee_spci_param *params,
			 uint32_t *res_ret, uint32_t *eo_ret)
{
	TEE_ErrorOrigin err_origin = TEE_ORIGIN_TEE;
	TEE_Result res = TEE_ERROR_GENERIC;

	/* Enable foreign interrupts */
	thread_set_foreign_intr(true);

	switch (cmd) {
	case OPTEE_SPCI_CMD_OPEN_SESSION:
		res = entry_open_session(num_params, params, session,
					 &err_origin);
		break;
	case OPTEE_SPCI_CMD_CLOSE_SESSION:
		if (num_params)
			res = TEE_ERROR_BAD_PARAMETERS;
		else
			res = entry_close_session(READ_ONCE(*session));
		break;
	case OPTEE_SPCI_CMD_INVOKE_COMMAND:
		res = entry_invoke_command(func, READ_ONCE(*session),
					   num_params, params, &err_origin);
		break;
	case OPTEE_SPCI_CMD_CANCEL:
		if (num_params)
			res = TEE_ERROR_BAD_PARAMETERS;
		else
			res = entry_cancel(READ_ONCE(*session), &err_origin);
		break;
	default:
		EMSG("Unknown cmd 0x%x\n", cmd);
		res = TEE_ERROR_BAD_PARAMETERS;
	}

	*res_ret = res;
	*eo_ret = err_origin;
}

/*
 * This function receives all memory based requests
 *
 * Note: this function is weak just to make it possible to exclude it from
 * the unpaged area.
 */
void __weak tee_entry_spci_request(struct thread_smc_args *args)
{
	TEE_Result res = 0;
	uint64_t cookie = args->a1;
	struct mobj *mobj = mobj_reg_shm_get_by_cookie(cookie);
	struct thread_specific_data *tsd = thread_get_tsd();

	args->a0 = SPCI_INVALID_PARAMETER;
	if (!mobj) {
		EMSG("Bad shared memory cookie 0x%" PRIx64, cookie);
		return;
	}

	res = mobj_reg_shm_inc_map(mobj);
	if (res) {
		EMSG("Cannot map shared memory cookie 0x%" PRIx64, cookie);
		if (res == TEE_ERROR_OUT_OF_MEMORY)
			args->a0 = SPCI_NO_MEMORY;
		else
			args->a0 = SPCI_NOT_SUPPORTED;
		goto out_put;
	}

	size_t offset = args->a2;
	size_t end_offset = 0;
	void *va = mobj_get_va(mobj, offset);

	if (!va || !ALIGNMENT_IS_OK(va, struct optee_spci) ||
	    ADD_OVERFLOW(offset, sizeof(struct optee_spci), &end_offset) ||
	    !mobj_get_va(mobj, end_offset - 1)) {
		EMSG("Bad offset 0x%" PRIx64, offset);
		goto out_dec_map;
	}

	struct optee_spci *arg = va;
	uint32_t num_params = READ_ONCE(arg->num_params);
	uint32_t rpc_num_params = READ_ONCE(arg->rpc_num_params);
	size_t args_size = OPTEE_SPCI_GET_SIZE(num_params, rpc_num_params);

	if (ADD_OVERFLOW(offset, args_size, &end_offset) ||
	    !mobj_get_va(mobj, end_offset - 1)) {
		EMSG("Bad offset 0x%" PRIx64, offset);
		goto out_dec_map;
	}

	if (rpc_num_params < THREAD_RPC_MAX_NUM_PARAMS) {
		EMSG("Bad number of RPC params 0x%" PRIx32, rpc_num_params);
		goto out_dec_map;
	}

	tsd->rpc_spci = arg;
	tsd->rpc_params = arg->params + num_params;

	dispatch_cmd(READ_ONCE(arg->cmd), READ_ONCE(arg->func), &arg->session,
		      num_params, arg->params, &arg->ret, &arg->ret_origin);


	args->a0 = SPCI_SUCCESS;

out_dec_map:
	mobj_reg_shm_dec_map(mobj);
out_put:
	mobj_reg_shm_put(mobj);
}

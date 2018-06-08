// SPDX-License-Identifier: BSD-2-Clause
/* Copyright (c) 2018, Linaro Limited */

#include <keymaster_ta.h>
#include <sys/queue.h>
#include <tee_internal_api.h>
#include <util.h>

#include "km.h"
#include "km_key_param.h"
#include "pack.h"

static TEE_Result add_rng_entropy(uint32_t pt, TEE_Param params[TEE_NUM_PARAMS])
{
	const uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
						TEE_PARAM_TYPE_NONE,
						TEE_PARAM_TYPE_NONE,
						TEE_PARAM_TYPE_NONE);

	if (pt != exp_pt)
		return TEE_ERROR_BAD_PARAMETERS;

	return km_add_rng_entropy(params[0].memref.buffer,
				  params[0].memref.size);
}

static TEE_Result generate_key(uint32_t pt, TEE_Param params[TEE_NUM_PARAMS])
{
	const uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
						TEE_PARAM_TYPE_MEMREF_OUTPUT,
						TEE_PARAM_TYPE_MEMREF_OUTPUT,
						TEE_PARAM_TYPE_NONE);
	TEE_Result res;
	TEE_Result res2 = TEE_SUCCESS;
	struct km_key_param_head kph;
	struct pack_state pack_state;
	size_t key_blob_size;

	if (pt != exp_pt)
		return TEE_ERROR_BAD_PARAMETERS;

	TAILQ_INIT(&kph);
	pack_state_read_init(&pack_state, params[0].memref.buffer,
			     params[0].memref.size);

	res = unpack_key_param(&pack_state, &kph);
	if (res)
		goto out;

	key_blob_size = params[1].memref.size;
	res = km_gen_key(&kph, params[1].memref.buffer, &key_blob_size);
	params[1].memref.size = key_blob_size;
	if (res)
		goto out;

	pack_state_write_init(&pack_state, params[2].memref.buffer,
			      params[2].memref.size);
	res = pack_key_param(&pack_state, &kph);
	if (res)
		goto out;
	if (pack_state.offs > params[2].memref.size)
		res2 = TEE_ERROR_SHORT_BUFFER;
	params[2].memref.size = pack_state.offs;
	res = res2;
out:
	km_key_param_free_list_content(&kph);
	return res;
}

TEE_Result TA_CreateEntryPoint(void)
{
	return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void)
{
}

TEE_Result TA_OpenSessionEntryPoint(uint32_t __unused pt,
				    TEE_Param __unused params[4],
				    void __unused **session)
{
	return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void __unused *sess)
{
}

static TEE_Result translate_result(TEE_Result res)
{
	/*
	 * Translates the TEE_Result codes that maps well to an enum
	 * km_error_code. The rest are kept as is to avoid losing
	 * information.  The code should ideally be updated only emit codes
	 * defined by enum km_error_code.
	 */
	switch (res) {
	case TEE_SUCCESS:
		return KM_OK;
	case TEE_ERROR_GENERIC:
		return KM_UNKNOWN_ERROR;
	case TEE_ERROR_BAD_PARAMETERS:
		return KM_INVALID_ARGUMENT;
	case TEE_ERROR_NOT_IMPLEMENTED:
		return KM_UNIMPLEMENTED;
	case TEE_ERROR_OUT_OF_MEMORY:
		return KM_MEMORY_ALLOCATION_FAILED;
	default:
		return res;
	}
}

TEE_Result TA_InvokeCommandEntryPoint(void __unused *sess, uint32_t cmd,
				      uint32_t pt,
				      TEE_Param params[TEE_NUM_PARAMS])
{
	TEE_Result res;

	switch (cmd) {
	case KEYMASTER_CMD_ADD_RNG_ENTROPY:
		res = add_rng_entropy(pt, params);
		break;
	case KEYMASTER_CMD_GENERATE_KEY:
		res = generate_key(pt, params);
		break;
	default:
		EMSG("Command ID 0x%x is not supported", cmd);
		res = TEE_ERROR_NOT_SUPPORTED;
	}

	return translate_result(res);
}

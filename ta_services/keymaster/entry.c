/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2018, Linaro Limited */

#include <keymaster_ta.h>
#include <tee_internal_api.h>

#include "km.h"

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

TEE_Result TA_InvokeCommandEntryPoint(void __unused *sess, uint32_t cmd,
				      uint32_t pt,
				      TEE_Param params[TEE_NUM_PARAMS])
{
	switch (cmd) {
	case KEYMASTER_CMD_ADD_RNG_ENTROPY:
		return add_rng_entropy(pt, params);
	default:
		EMSG("Command ID 0x%x is not supported", cmd);
		return TEE_ERROR_NOT_SUPPORTED;
	}
}

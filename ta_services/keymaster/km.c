/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2018, Linaro Limited */

#include <pta_system.h>
#include <tee_internal_api.h>

#include "km.h"

TEE_Result km_add_rng_entropy(const void *buf, size_t blen)
{
	static const TEE_UUID system_uuid = PTA_SYSTEM_UUID;
	TEE_TASessionHandle sess = TEE_HANDLE_NULL;
	TEE_Result res;
	uint32_t pt;
	TEE_Param params[4] = { { {0} } };
	uint32_t ret_orig;

	if (!blen)
		return TEE_ERROR_BAD_PARAMETERS;

	res = TEE_OpenTASession(&system_uuid, 0, 0, NULL, &sess, &ret_orig);
	if (res != TEE_SUCCESS) {
		EMSG("TEE_OpenTASession to System Pseudo TA failed");
		return res;
	}

	pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
				TEE_PARAM_TYPE_NONE,
				TEE_PARAM_TYPE_NONE,
				TEE_PARAM_TYPE_NONE);
	params[0].memref.buffer = (void *)buf;
	params[0].memref.size = blen;

	res = TEE_InvokeTACommand(sess, 0, PTA_SYSTEM_ADD_RNG_ENTROPY,
				  pt, params, &ret_orig);
	if (res != TEE_SUCCESS) {
		EMSG("TEE_InvokeTACommand to System Pseudo TA failed");
		goto cleanup_return;
	}


cleanup_return:
	TEE_CloseTASession(sess);
	return res;
}


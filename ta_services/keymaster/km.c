/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2018, Linaro Limited */

#include <tee_internal_api.h>

#include "km.h"

TEE_Result km_add_rng_entropy(const void __unused *buf, size_t __unused blen)
{
	/* Stubbed until the system PTA is available */
	return TEE_ERROR_NOT_IMPLEMENTED;
}

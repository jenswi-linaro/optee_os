// SPDX-License-Identifier: BSD-2-Clause
/* Copyright (c) 2018, Linaro Limited */

#include <stdlib.h>
#include <sys/queue.h>
#include <tee_api_types.h>
#include <util.h>

#include "km_key_param.h"

TEE_Result km_key_param_new(uint32_t tag, uint32_t size,
			    struct km_key_param **kp)
{
	size_t sz;

	if (ADD_OVERFLOW(size, sizeof(struct km_key_param), &sz))
		return TEE_ERROR_BAD_PARAMETERS;

	*kp = malloc(sz);
	if (!*kp)
		return TEE_ERROR_OUT_OF_MEMORY;

	(*kp)->tag = tag;
	(*kp)->size = size;
	return TEE_SUCCESS;
}

void km_key_param_free_list_content(struct km_key_param_head *kph)
{
	while (true) {
		struct km_key_param *kp = TAILQ_FIRST(kph);

		if (!kp)
			return;
		km_key_param_free(kph, kp);
	}
}

struct km_key_param *km_key_param_find(struct km_key_param_head *kph,
				       uint32_t tag)
{
	struct km_key_param *kp;

	TAILQ_FOREACH(kp, kph, link)
		if (kp->tag == tag)
			return kp;

	return NULL;
}

void km_key_param_free(struct km_key_param_head *kph, struct km_key_param *kp)
{
	if (!kp)
		return;
	TAILQ_REMOVE(kph, kp, link);
	free(kp);
}

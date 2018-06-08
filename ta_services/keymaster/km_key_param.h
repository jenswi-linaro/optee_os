/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2018, Linaro Limited */

#ifndef LOCAL_KM_KEY_PARAM_H
#define LOCAL_KM_KEY_PARAM_H

#include <sys/queue.h>
#include <tee_api_types.h>

struct km_key_param {
	TAILQ_ENTRY(km_key_param) link;
	uint32_t tag;
	uint32_t size;
	uint8_t data[];
};

TAILQ_HEAD(km_key_param_head, km_key_param);

TEE_Result km_key_param_new(uint32_t tag, uint32_t size,
			    struct km_key_param **kp);
void km_key_param_free_list_content(struct km_key_param_head *kph);
void km_key_param_free(struct km_key_param_head *kph, struct km_key_param *kp);
struct km_key_param *km_key_param_find(struct km_key_param_head *kph,
				       uint32_t tag);


#endif /*LOCAL_KM_KEY_PARAM_H*/

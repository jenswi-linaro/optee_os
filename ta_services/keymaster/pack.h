/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2018, Linaro Limited */

#ifndef LOCAL_PACK_H
#define LOCAL_PACK_H

#include <tee_api_types.h>

#define PACK_TAG_ATTRS		0
#define PACK_TAG_KEY_PARAM	1

struct km_key_param_head;
struct km_key_param;

struct pack_state {
	bool reading;
	size_t offs;
	size_t blen;
	uint8_t *buf;
};

void pack_state_read_init(struct pack_state *ps, void *buf, size_t blen);
void pack_state_write_init(struct pack_state *ps, void *buf, size_t blen);

/*
 * unpack_attrs() - unpack a binary blob into array of TEE_Attribute
 * @buf:	binary blob
 * @blen:	length of blob
 * @attr:	pointer to return array or TEE_Attributes
 * @attr_count: number of elements in @attr array
 *
 * Note that attributes referencing to a buffer still points into @buf
 * so @buf cannot be freed until @attrs aren't used any more.
 *
 * It's the callers responsibility to free @attrs using TEE_Free()
 */
TEE_Result unpack_attrs(struct pack_state *ps, TEE_Attribute **attrs,
			size_t *attr_count);

/*
 * pack_attrs() - packs an array of TEE_Attribute into a binary blob
 * @attrs:	array of TEE_Attribute to pack
 * @attr_count:	number of elements in @attrs
 * @buf:	pointer to return allocated packed buffer
 * @blen:	size of @buf
 *
 * Note that it's  callers responsibility to free @buf using TEE_Free()
 */
TEE_Result pack_attrs(struct pack_state *ps, const TEE_Attribute *attrs,
		      size_t attr_count);

TEE_Result pack_key_param(struct pack_state *ps, struct km_key_param_head *kph);
TEE_Result pack_key_param_filter(struct pack_state *ps,
				 struct km_key_param_head *kph,
				 bool (*filter_cb)(struct km_key_param *kp));
TEE_Result unpack_key_param(struct pack_state *ps,
			    struct km_key_param_head *kph);

#endif /*LOCAL_PACK_H*/

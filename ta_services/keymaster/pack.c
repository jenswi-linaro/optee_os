// SPDX-License-Identifier: BSD-2-Clause
/* Copyright (c) 2018, Linaro Limited */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <tee_api_types.h>
#include <util.h>

#include "km_key_param.h"
#include "pack.h"

struct attr_packed {
	uint32_t id;
	uint32_t a;
	uint32_t b;
};

static bool check_pack_state(struct pack_state *ps, bool reading)
{
	if (reading || ps->buf) {
		uintptr_t end_ptr;

		if (!ps->buf)
			return false;
		if (ADD_OVERFLOW((uintptr_t)ps->buf, ps->blen, &end_ptr))
			return false;
	}
	if (ps->offs > ps->blen)
		return false;
	return true;
}

static bool ps_get_ptr_and_advance(struct pack_state *ps, void **ptr,
					 size_t req_ptr_alignment,
					 size_t num_bytes)
{
	size_t next_offs;
	uintptr_t next_ptr = (uintptr_t)ps->buf + ps->offs;

	if (ADD_OVERFLOW(ps->offs, num_bytes, &next_offs))
		return false;

	assert(!req_ptr_alignment || IS_POWER_OF_TWO(req_ptr_alignment));

	if (ps->buf && next_offs <= ps->blen) {
		if (req_ptr_alignment && (next_ptr & (req_ptr_alignment - 1)))
			return false;

		*ptr = (void *)next_ptr;
	} else {
		if (ps->reading)
			return false;

		if (req_ptr_alignment && (next_offs & (req_ptr_alignment - 1)))
			return false;

		*ptr = NULL;
	}
	ps->offs = next_offs;
	return true;
}

static bool ps_copy_out(struct pack_state *ps, const void *buf,
			size_t len)
{
	void *ptr;

	if (!ps_get_ptr_and_advance(ps, &ptr, 0, len))
		return false;
	if (buf && ptr)
		memcpy(ptr, buf, len);
	return true;
}

static bool ps_copy_out_u32(struct pack_state *ps, uint32_t v)
{
	return ps_copy_out(ps, &v, sizeof(v));
}

static bool ps_copy_in(struct pack_state *ps, void *buf, size_t len)
{
	void *ptr;

	if (!ps_get_ptr_and_advance(ps, &ptr, 0, len))
		return false;
	if (!ptr)
		return false;
	memcpy(buf, ptr, len);
	return true;
}

static bool ps_copy_in_u32(struct pack_state *ps, uint32_t *v)
{
	return ps_copy_in(ps, v, sizeof(*v));
}

static bool ps_align_to(struct pack_state *ps, size_t req_alignment)
{
	uintptr_t next_ptr = (uintptr_t)ps->buf + ps->offs;
	size_t next_offs;

	if (ADD_OVERFLOW(ps->offs, ROUNDUP(next_ptr, req_alignment) - next_ptr,
			 &next_offs))
		return false;
	if (ps->reading && next_offs > ps->blen)
		return false;
	ps->offs = next_offs;
	return true;
}

static bool ps_get_ptr_at(struct pack_state *ps, size_t offs, void **ptr,
			  size_t num_bytes)
{
	size_t next_offs;
	size_t tmp_offs;

	assert(ps->reading);
	if (!ps->buf)
		return false;

	if (ADD_OVERFLOW(ps->offs, offs, &tmp_offs))
		return false;
	if (ADD_OVERFLOW(tmp_offs, num_bytes, &next_offs))
		return false;
	if (next_offs > ps->blen)
		return false;

	*ptr = ps->buf + tmp_offs;
	return true;
}

TEE_Result unpack_attrs(struct pack_state *ps, TEE_Attribute **attrs,
			uint32_t *attr_count)
{
	TEE_Result res = TEE_SUCCESS;
	TEE_Attribute *a = NULL;
	size_t offs;
	void *buf;
	const struct attr_packed *ap;
	uint32_t num_attrs;

	if (!check_pack_state(ps, true /*reading*/))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!ps_copy_in_u32(ps, &num_attrs))
		return TEE_ERROR_BAD_PARAMETERS;
	if (!ps_get_ptr_and_advance(ps, &buf, __alignof__(struct attr_packed),
				    num_attrs * sizeof(*ap)))
		return TEE_ERROR_BAD_PARAMETERS;
	ap = (const struct attr_packed *)buf;

	if (num_attrs > 0) {
		size_t n;

		a = malloc(num_attrs * sizeof(TEE_Attribute));
		if (!a)
			return TEE_ERROR_OUT_OF_MEMORY;
		for (n = 0; n < num_attrs; n++) {
			a[n].attributeID = ap[n].id;
			if (ap[n].id & TEE_ATTR_BIT_VALUE) {
				a[n].content.value.a = ap[n].a;
				a[n].content.value.b = ap[n].b;
				continue;
			}

			a[n].content.ref.length = ap[n].b;
			if (!ap[n].a) {
				a[n].content.ref.buffer = NULL;
				continue;
			}
			offs = ap[n].a - num_attrs * sizeof(*ap) -
			       sizeof(uint32_t);
			if (!ps_get_ptr_at(ps, offs, &a[n].content.ref.buffer,
					  a[n].content.ref.length)) {
				res = TEE_ERROR_BAD_PARAMETERS;
				goto out;
			}
		}
	}

	res = TEE_SUCCESS;
out:
	if (res == TEE_SUCCESS) {
		*attrs = a;
		*attr_count = num_attrs;
	} else {
		free(a);
	}
	return res;
}

TEE_Result pack_attrs(struct pack_state *ps, const TEE_Attribute *attrs,
		      size_t attr_count)
{
	struct attr_packed *a;
	void *ptr;
	size_t bl;
	size_t n;
	uintptr_t base_ptr;

	if (!check_pack_state(ps, false /*!reading*/))
		return TEE_ERROR_BAD_PARAMETERS;

	bl = sizeof(uint32_t) + sizeof(struct attr_packed) * attr_count;
	for (n = 0; n < attr_count; n++) {
		if ((attrs[n].attributeID & TEE_ATTR_BIT_VALUE) != 0)
			continue; /* Only memrefs need to be updated */

		if (!attrs[n].content.ref.buffer)
			continue;

		/* Make room for padding */
		bl += ROUNDUP(attrs[n].content.ref.length, sizeof(uint32_t));
	}

	if (!ps->buf) {
		if (ADD_OVERFLOW(ps->offs, bl, &ps->offs))
			return TEE_ERROR_BAD_PARAMETERS;
		return TEE_SUCCESS;
	}

	if (!ps_get_ptr_and_advance(ps, &ptr, __alignof__(uint32_t),
				    sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;
	base_ptr = (uintptr_t)ptr;
	if (ptr)
		memcpy(ptr, &(uint32_t){attr_count}, sizeof(uint32_t));
	if (!ps_get_ptr_and_advance(ps, &ptr, __alignof__(struct attr_packed),
				    sizeof(struct attr_packed) * attr_count))
		return TEE_ERROR_BAD_PARAMETERS;

	a = ptr;

	for (n = 0; n < attr_count; n++) {
		a[n].id = attrs[n].attributeID;
		if (attrs[n].attributeID & TEE_ATTR_BIT_VALUE) {
			a[n].a = attrs[n].content.value.a;
			a[n].b = attrs[n].content.value.b;
			continue;
		}

		a[n].b = attrs[n].content.ref.length;
		if (!attrs[n].content.ref.buffer) {
			a[n].a = 0;
			continue;
		}
		if (!ps_get_ptr_and_advance(ps, &ptr, __alignof__(uint32_t),
					    attrs[n].content.ref.length))
			return TEE_ERROR_BAD_PARAMETERS;
		if (ptr && attrs[n].content.ref.buffer)
			memcpy(ptr, attrs[n].content.ref.buffer,
			       attrs[n].content.ref.length);

		/* Make buffer pointer relative to *buf */
		a[n].a = (uintptr_t)ptr - base_ptr;

		/* Round up to good alignment */
		if (!ps_align_to(ps, __alignof__(uint32_t)))
			return TEE_ERROR_BAD_PARAMETERS;
	}

	return TEE_SUCCESS;
}

static bool filter_dummy_true(struct km_key_param __unused *kp)
{
	return true;
}

TEE_Result pack_key_param(struct pack_state *ps, struct km_key_param_head *kph)
{
	return pack_key_param_filter(ps, kph, filter_dummy_true);
}

TEE_Result pack_key_param_filter(struct pack_state *ps,
				 struct km_key_param_head *kph,
				 bool (*filter_cb)(struct km_key_param *kp))
{
	struct km_key_param *kp;
	uint32_t len = 0;

	if (!check_pack_state(ps, false /*!reading*/))
		return TEE_ERROR_BAD_PARAMETERS;

	TAILQ_FOREACH(kp, kph, link) {
		if (!filter_cb(kp))
			continue;

		len += sizeof(uint32_t) * 2 + kp->size;
	}
	if (!ps_copy_out_u32(ps, len))
		return TEE_ERROR_BAD_PARAMETERS;

	TAILQ_FOREACH(kp, kph, link) {
		if (!filter_cb(kp))
			continue;

		len += sizeof(uint32_t) * 2 + kp->size;
		if (!ps_copy_out_u32(ps, kp->tag))
			return TEE_ERROR_BAD_PARAMETERS;
		if (!ps_copy_out_u32(ps, kp->size))
			return TEE_ERROR_BAD_PARAMETERS;
		if (!ps_copy_out(ps, kp->data, kp->size))
			return TEE_ERROR_BAD_PARAMETERS;
	}

	return TEE_SUCCESS;
}

TEE_Result unpack_key_param(struct pack_state *ps,
			    struct km_key_param_head *kph)
{
	TEE_Result res;
	uint32_t len;
	size_t end_offs;

	if (!check_pack_state(ps, true /*reading*/))
		return TEE_ERROR_BAD_PARAMETERS;

	if (!ps_copy_in_u32(ps, &len))
		return TEE_ERROR_BAD_PARAMETERS;
	if (ADD_OVERFLOW(ps->offs, len, &end_offs))
		return TEE_ERROR_BAD_PARAMETERS;
	if (end_offs > ps->blen)
		return TEE_ERROR_BAD_PARAMETERS;

	while (ps->offs < end_offs) {
		uint32_t l;
		uint32_t t;
		void *p;
		struct km_key_param *kp;

		if (!ps_copy_in_u32(ps, &t))
			return TEE_ERROR_BAD_PARAMETERS;
		if (!ps_copy_in_u32(ps, &l))
			return TEE_ERROR_BAD_PARAMETERS;
		if (!ps_get_ptr_and_advance(ps, &p, 0, l))
			return TEE_ERROR_BAD_PARAMETERS;

		res = km_key_param_new(t, l, &kp);
		if (res)
			goto err;
		memcpy(kp->data, p, l);
		TAILQ_INSERT_TAIL(kph, kp, link);
	}

	return TEE_SUCCESS;
err:
	km_key_param_free_list_content(kph);
	return res;
}

void pack_state_read_init(struct pack_state *ps, void *buf, size_t blen)
{
	*ps = (struct pack_state){ .offs = 0, .blen = blen, .buf = buf,
				   .reading = true, };
}

void pack_state_write_init(struct pack_state *ps, void *buf, size_t blen)
{
	*ps = (struct pack_state){ .offs = 0, .blen = blen, .buf = buf,
				   .reading = false, };
}

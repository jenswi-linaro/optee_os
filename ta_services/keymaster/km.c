// SPDX-License-Identifier: BSD-2-Clause
/* Copyright (c) 2018, Linaro Limited */

#include <assert.h>
#include <mbedtls/ecp.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/x509.h>
#include <pta_system.h>
#include <string.h>
#include <sys/queue.h>
#include <tee_internal_api.h>
#include <utee_defines.h>

#include "km.h"
#include "km_key_param.h"
#include "pack.h"

#define AE_IV_LEN	12
#define AE_TAG_LEN	16
#define AE_ALGO		TEE_ALG_AES_GCM
#define AE_KEYLEN	32


static const uint32_t storageid = TEE_STORAGE_PRIVATE;
static const char ae_key_name[] = "aekey";

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

static bool check_unexpected_duplicates(struct km_key_param_head *kph)
{
	struct km_key_param *kp0;

	TAILQ_FOREACH(kp0, kph, link) {
		enum km_tag_type tag_type = km_get_tag_type(kp0->tag);
		struct km_key_param *kp;

		if (tag_type == KM3_TAG_TYPE_ENUM_REP ||
		    tag_type == KM3_TAG_TYPE_UINT_REP ||
		    tag_type == KM3_TAG_TYPE_ULONG_REP)
			continue;

		for (kp = TAILQ_NEXT(kp0, link); kp; kp = TAILQ_NEXT(kp, link))
			if (kp->tag == kp0->tag)
				return false;
	}

	return true;
}

static TEE_Result key_param_to_value(struct km_key_param *kp, uint64_t *value)
{
	uint32_t v32;

	if (!kp)
		return KM_INVALID_ARGUMENT;

	switch (km_get_tag_type(kp->tag)) {
	case KM3_TAG_TYPE_ENUM:
	case KM3_TAG_TYPE_ENUM_REP:
	case KM3_TAG_TYPE_UINT:
	case KM3_TAG_TYPE_UINT_REP:
		if (kp->size != sizeof(uint32_t))
			return KM_INVALID_ARGUMENT;
		memcpy(&v32, kp->data, sizeof(uint32_t));
		*value = v32;
		return KM_OK;
	case KM3_TAG_TYPE_ULONG:
	case KM3_TAG_TYPE_ULONG_REP:
	case KM3_TAG_TYPE_DATE:
		if (kp->size != sizeof(uint64_t))
			return KM_INVALID_ARGUMENT;
		memcpy(value, kp->data, sizeof(uint64_t));
		return KM_OK;
	case KM3_TAG_TYPE_BOOL:
		*value = true;
		return KM_OK;
	case KM3_TAG_TYPE_BIGNUM:
	case KM3_TAG_TYPE_BYTES:
	default:
		return KM_UNSUPPORTED_TAG;
	}
}

static TEE_Result rsa_object_handle_to_raw_key(TEE_ObjectHandle h,
					       void **raw_key,
					       size_t *raw_key_size)
{
	TEE_Result res;
	TEE_Attribute attrs[] = {
		{ .attributeID = TEE_ATTR_RSA_MODULUS },
		{ .attributeID = TEE_ATTR_RSA_PUBLIC_EXPONENT },
		{ .attributeID = TEE_ATTR_RSA_PRIVATE_EXPONENT },
		{ .attributeID = TEE_ATTR_RSA_PRIME1 },
		{ .attributeID = TEE_ATTR_RSA_PRIME2 },
		{ .attributeID = TEE_ATTR_RSA_EXPONENT1 },
		{ .attributeID = TEE_ATTR_RSA_EXPONENT2 },
		{ .attributeID = TEE_ATTR_RSA_COEFFICIENT },
	};
	struct pack_state ps;
	size_t n;
	void *k;

	for (n = 0; n < ARRAY_SIZE(attrs); n++) {
		uint32_t sz;
		void *b;

		res = TEE_GetObjectBufferAttribute(h, attrs[n].attributeID,
						   NULL, &sz);
		if (res != TEE_ERROR_SHORT_BUFFER)
			goto out;
		attrs[n].content.ref.length = sz;
		b = TEE_Malloc(sz, TEE_MALLOC_FILL_ZERO);
		if (!b) {
			res = TEE_ERROR_OUT_OF_MEMORY;
			goto out;
		}
		attrs[n].content.ref.buffer = b;

		res = TEE_GetObjectBufferAttribute(h, attrs[n].attributeID,
						   b, &sz);
		if (res)
			goto out;
		assert(attrs[n].content.ref.length == sz);
	}

	pack_state_write_init(&ps, NULL, 0);
	res = pack_attrs(&ps, attrs, ARRAY_SIZE(attrs));
	if (res)
		goto out;
	k = TEE_Malloc(ps.offs, TEE_MALLOC_FILL_ZERO);
	if (!k) {
		res = TEE_ERROR_OUT_OF_MEMORY;
		goto out;
	}
	pack_state_write_init(&ps, k, ps.offs);
	res = pack_attrs(&ps, attrs, ARRAY_SIZE(attrs));
	if (res) {
		TEE_Free(k);
		goto out;
	}
	if (ps.offs > ps.blen)
		TEE_Panic(0); /* "can't happen" */
	*raw_key = k;
	*raw_key_size = ps.offs;
out:
	for (n = 0; n < ARRAY_SIZE(attrs); n++)
		TEE_Free(attrs[n].content.ref.buffer);
	return res;
}

static bool is_common_ignored_gen_key_tags(uint32_t tag)
{
	switch (tag) {
	case KM3_TAG_ALGORITHM:
	case KM3_TAG_PURPOSE:
	case KM3_TAG_DIGEST:
		return true;
	default:
		return false;
	}
}

static TEE_Result gen_rsa_key(struct km_key_param_head *kph,
			      void **raw_key, size_t *raw_key_size)
{
	TEE_Result res;
	struct km_key_param *kp;
	struct km_key_param *kp_pub_exp = NULL;
	struct km_key_param *kp_key_size = NULL;
	uint64_t key_size;
	TEE_ObjectHandle h;
	uint64_t v64;
	uint32_t be_pub_exp;
	TEE_Attribute attr;

	TAILQ_FOREACH(kp, kph, link) {
		if (is_common_ignored_gen_key_tags(kp->tag))
			continue;

		switch (kp->tag) {
		case KM3_TAG_KEY_SIZE:
			kp_key_size = kp;
			break;
		case KM3_TAG_RSA_PUBLIC_EXPONENT:
			kp_pub_exp = kp;
			break;
		default:
			EMSG("Unsupported tag %#" PRIX32, kp->tag);
			return KM_INVALID_ARGUMENT;;
		}
	}
	if (!kp_key_size) {
		EMSG("KM3_TAG_KEY_SIZE missing");
		return KM_INVALID_ARGUMENT;;
	}
	if (!kp_pub_exp) {
		EMSG("KM3_TAG_RSA_PUBLIC_EXPONENT missing");
		return KM_INVALID_ARGUMENT;
	}

	res = key_param_to_value(kp_key_size, &key_size);
	if (res)
		return res;
	res = key_param_to_value(kp_pub_exp, &v64);
	if (res)
		return res;
	if (v64 > UINT32_MAX)
		return KM_INVALID_ARGUMENT;
	be_pub_exp = TEE_U32_TO_BIG_ENDIAN((uint32_t)v64);
	TEE_InitRefAttribute(&attr, TEE_ATTR_RSA_PUBLIC_EXPONENT,
			     &be_pub_exp, sizeof(&be_pub_exp));

	res = TEE_AllocateTransientObject(TEE_TYPE_RSA_KEYPAIR, key_size, &h);
	if (res)
		return res;

	res = TEE_GenerateKey(h, key_size, &attr, 1);
	if (res) {
		res = KM_INVALID_ARGUMENT;
		goto out;
	}

	res = rsa_object_handle_to_raw_key(h, raw_key, raw_key_size);
out:
	TEE_FreeTransientObject(h);
	return res;
}

static TEE_Result add_key_param(struct km_key_param_head *kph,
				uint32_t tag, size_t size, const void *data)
{
	TEE_Result res;
	struct km_key_param *kp;

	res = km_key_param_new(tag, size, &kp);
	if (res)
		return res;
	memcpy(kp->data, data, size);
	TAILQ_INSERT_TAIL(kph, kp, link);
	return TEE_SUCCESS;
}

static TEE_Result add_key_param_u32(struct km_key_param_head *kph,
                                    uint32_t tag, uint32_t v)
{
	return add_key_param(kph, tag, sizeof(v), &v);
}

static TEE_Result set_blob_key(TEE_OperationHandle op)
{
	TEE_Result res;
	TEE_ObjectHandle key;

	res = TEE_OpenPersistentObject(storageid, ae_key_name,
				       strlen(ae_key_name), 0, &key);
	if (res == TEE_ERROR_ITEM_NOT_FOUND) {
		TEE_ObjectHandle k;

		res = TEE_AllocateTransientObject(TEE_TYPE_AES, AE_KEYLEN * 8,
						  &k);
		if (res)
			return res;

		res = TEE_GenerateKey(k, AE_KEYLEN * 8, NULL, 0);
		if (res) {
			TEE_CloseObject(k);
			return res;
		}

		res = TEE_CreatePersistentObject(storageid, ae_key_name,
						 strlen(ae_key_name), 0,
						 k, NULL, 0, &key);
		TEE_CloseObject(k);
	}
	if (res)
		return res;

	res = TEE_SetOperationKey(op, key);
	TEE_CloseObject(key);
	return res;
}

static TEE_Result make_kp_blob(struct km_key_param_head *kph, void **blob,
			       size_t *size,
			       bool (*filter_cb)(struct km_key_param *kp))
{
	TEE_Result res;
	struct pack_state ps;

	pack_state_write_init(&ps, NULL, 0);
	res = pack_key_param_filter(&ps, kph, filter_cb);
	if (res)
		return res;
	*size = ps.offs;
	*blob = TEE_Malloc(*size, TEE_MALLOC_FILL_ZERO);
	if (!*blob)
		return TEE_ERROR_OUT_OF_MEMORY;
	pack_state_write_init(&ps, *blob, *size);
	res = pack_key_param_filter(&ps, kph, filter_cb);
	if (res && ps.offs > ps.blen)
		res = TEE_ERROR_GENERIC;
	if (res)
		TEE_Free(*blob);
	return res;
}

static bool kp_filter_aad(struct km_key_param *kp)
{
	return kp->tag == (uint32_t)KM3_TAG_APPLICATION_ID ||
	       kp->tag == (uint32_t)KM3_TAG_APPLICATION_DATA;
}

static bool kp_filter_not_aad(struct km_key_param *kp)
{
	return !kp_filter_aad(kp);
}

static void kp_sort_aad_tags(struct km_key_param_head *kph)
{
	struct km_key_param *kp;

	/* Make sure the tags used for AAD are in a defined order */
	kp = km_key_param_find(kph, KM3_TAG_APPLICATION_ID);
	if (kp) {
		TAILQ_REMOVE(kph, kp, link);
		TAILQ_INSERT_TAIL(kph, kp, link);
	}
	kp = km_key_param_find(kph, KM3_TAG_APPLICATION_DATA);
	if (kp) {
		TAILQ_REMOVE(kph, kp, link);
		TAILQ_INSERT_TAIL(kph, kp, link);
	}
}

static TEE_Result make_key_blob(struct km_key_param_head *kph, void *raw_key,
				size_t raw_key_size, void *key_blob,
				size_t *key_blob_size)
{
	TEE_Result res;
	uint8_t iv[AE_IV_LEN];
	size_t req_size;
	TEE_OperationHandle op = TEE_HANDLE_NULL;
	size_t dl;
	size_t tl;
	size_t aad_size = 0;
	void *aad_data = NULL;
	size_t kp_size = 0;
	void *kp_data = NULL;

	kp_sort_aad_tags(kph);
	res = make_kp_blob(kph, &aad_data, &aad_size, kp_filter_aad);
	if (res)
		goto out;
	res = make_kp_blob(kph, &kp_data, &kp_size, kp_filter_not_aad);
	if (res)
		goto out;

	req_size = AE_IV_LEN + kp_size + raw_key_size + AE_TAG_LEN;
	if (req_size > *key_blob_size) {
		*key_blob_size = req_size;
		res = TEE_ERROR_SHORT_BUFFER;
		goto out;
	}

	res = TEE_AllocateOperation(&op, AE_ALGO, TEE_MODE_ENCRYPT,
				    AE_KEYLEN * 8);
	if (res)
		goto out;
	res = set_blob_key(op);
	if (res)
		goto out;
	TEE_GenerateRandom(iv, sizeof(iv));
	TEE_MemMove(key_blob, iv, sizeof(iv));
	res = TEE_AEInit(op, iv, sizeof(iv), AE_TAG_LEN * 8, aad_size,
			 kp_size + raw_key_size);
	if (res)
		goto out;

	TEE_AEUpdateAAD(op, aad_data, aad_size);

	dl = kp_size;
	res = TEE_AEUpdate(op, kp_data, kp_size,
			   (uint8_t *)key_blob + AE_IV_LEN, &dl);
	if (res)
		goto out;
	if (dl != kp_size) {
		res = TEE_ERROR_GENERIC;
		goto out;
	}

	dl = raw_key_size;
	tl = AE_TAG_LEN;
	res = TEE_AEEncryptFinal(op, raw_key, raw_key_size,
				 (uint8_t *)key_blob + AE_IV_LEN + kp_size, &dl,
				 (uint8_t *)key_blob + AE_IV_LEN + kp_size +
					    raw_key_size, &tl);
	if (res)
		goto out;
	if (dl != raw_key_size || tl != AE_TAG_LEN) {
		res = TEE_ERROR_GENERIC;
		goto out;
	}
	*key_blob_size = req_size;
out:
	TEE_Free(aad_data);
	TEE_Free(kp_data);
	TEE_FreeOperation(op);
	return res;

}

static TEE_Result make_key(TEE_Attribute *attrs, size_t attr_count,
			   struct km_key_param_head *kph, TEE_ObjectHandle *key)
{
	TEE_Result res;
	uint64_t v;
	TEE_ObjectHandle h;
	uint32_t obj_type;
	uint32_t key_size;

	if (key_param_to_value(km_key_param_find(kph, KM3_TAG_ALGORITHM), &v))
		return TEE_ERROR_GENERIC;
	switch (v) {
	case KM_RSA:
		obj_type = TEE_TYPE_RSA_KEYPAIR;
		break;
	default:
		EMSG("Unsupported keymaster algorithm %d", (int)v);
		return TEE_ERROR_GENERIC;
	}

	if (key_param_to_value(km_key_param_find(kph, KM3_TAG_KEY_SIZE), &v))
		return TEE_ERROR_GENERIC;
	key_size = v;

	res = TEE_AllocateTransientObject(obj_type, key_size, &h);
	if (res)
		return res;
	res = TEE_PopulateTransientObject(h, attrs, attr_count);
	if (res) {
		TEE_FreeTransientObject(h);
		return res;
	}

	*key = h;
	return TEE_SUCCESS;
}

static TEE_Result decrypt_key_blob(void *key_blob, size_t key_blob_size,
				   struct km_key_param_head *kph,
				   TEE_ObjectHandle *key)
{
	TEE_Result res;
	struct pack_state ps;
	TEE_OperationHandle op = TEE_HANDLE_NULL;
	size_t aad_size = 0;
	void *aad_data = NULL;
	uint8_t *data = NULL;
	size_t data_len;
	size_t l;

	if (SUB_OVERFLOW(key_blob_size, AE_IV_LEN, &data_len))
		return TEE_ERROR_BAD_PARAMETERS;
	if (SUB_OVERFLOW(data_len, AE_TAG_LEN, &data_len))
		return TEE_ERROR_BAD_PARAMETERS;
	data = TEE_Malloc(data_len, TEE_MALLOC_FILL_ZERO);
	if (!data)
		return TEE_ERROR_OUT_OF_MEMORY;

	kp_sort_aad_tags(kph);
	res = make_kp_blob(kph, &aad_data, &aad_size, kp_filter_aad);
	if (res)
		goto out;

	res = TEE_AllocateOperation(&op, AE_ALGO, TEE_MODE_DECRYPT,
				    AE_KEYLEN * 8);
	if (res)
		return res;
	res = set_blob_key(op);
	if (res)
		goto out;
	res = TEE_AEInit(op, key_blob, AE_IV_LEN, AE_TAG_LEN * 8, aad_size,
			 data_len);
	if (res)
		goto out;

	TEE_AEUpdateAAD(op, aad_data, aad_size);

	l = data_len;
	res = TEE_AEDecryptFinal(op, (uint8_t *)key_blob + AE_IV_LEN, data_len,
				 data, &l,
				 (uint8_t *)key_blob + AE_IV_LEN + data_len,
				 AE_TAG_LEN);
	if (res)
		goto out;
	if (l != data_len) {
		res = TEE_ERROR_GENERIC;
		goto out;
	}

	pack_state_read_init(&ps, data, data_len);
	/*
	 * Remove eventual supplied KM3_TAG_APPLICATION_ID and
	 * KM3_TAG_APPLICATION_DATA now that we're about to add what's in
	 * the key blob instead.
	 */
	km_key_param_free_list_content(kph);
	res = unpack_key_param(&ps, kph);
	if (res)
		goto out;

	if (key) {
		TEE_Attribute *attrs;
		uint32_t attr_count;

		res = unpack_attrs(&ps, &attrs, &attr_count);
		if (res)
			goto out;
		res = make_key(attrs, attr_count, kph, key);
		TEE_Free(attrs);
	}

out:
	if (res)
		km_key_param_free_list_content(kph);
	TEE_Free(data);
	TEE_Free(aad_data);
	TEE_FreeOperation(op);
	return res;
}

TEE_Result km_gen_key(struct km_key_param_head *kph, void *key_blob,
		      size_t *key_blob_size)
{
	TEE_Result res;
	uint64_t val;
	struct km_key_param *kp;
	void *raw_key = NULL;
	size_t raw_key_size = 0;

	if (!check_unexpected_duplicates(kph)) {
		EMSG("Unepected duplicate tag(s)");
		return KM_INVALID_ARGUMENT;
	}

	kp = km_key_param_find(kph, KM3_TAG_ALGORITHM);
	res = key_param_to_value(kp, &val);
	if (res)
		return TEE_SUCCESS;

	switch (val) {
	case KM_RSA:
		res = gen_rsa_key(kph, &raw_key, &raw_key_size);
		if (res)
			return res;
		break;
	default:
		EMSG("Unsupported keymaster algorithm %d", (int)val);
		return KM_UNSUPPORTED_ALGORITHM;
	}

	res = add_key_param_u32(kph, KM3_TAG_ORIGIN, KM_GENERATED);
	if (res)
		goto out;

	res = make_key_blob(kph, raw_key, raw_key_size, key_blob,
			    key_blob_size);
	km_key_param_free(kph, km_key_param_find(kph, KM3_TAG_APPLICATION_ID));
	km_key_param_free(kph,
		km_key_param_find(kph, KM3_TAG_APPLICATION_DATA));
out:
	TEE_Free(raw_key);
	return res;
}

TEE_Result km_get_key_characteristics(void *key_blob, size_t key_blob_size,
				      struct km_key_param_head *kph)
{
	return decrypt_key_blob(key_blob, key_blob_size, kph, NULL);
}


#include <ibmtss/tss.h>
#include <ibmtss/tsscryptoh.h>
#include <ibmtss/tsscrypto.h>

#include <crypto/crypto.h>
#include <trace.h>
#include <utee_defines.h>
#include <assert.h>
#include <string.h>

extern int tssVverbose;
extern int tssVerbose;

TPM_RC TSS_Crypto_Init(void)
{
	return TPM_RC_SUCCESS;
}

static uint32_t tee_algo_from_tpm_hash(TPMI_ALG_HASH tpm_alg)
{
	switch (tpm_alg) {
	case TPM_ALG_SHA1:
		return TEE_ALG_SHA1;
	case TPM_ALG_SHA256:
		return TEE_ALG_SHA256;
	case TPM_ALG_SHA384:
		return TEE_ALG_SHA384;
	case TPM_ALG_SHA512:
		return TEE_ALG_SHA512;
	default:
		return 0;
	}
}

TPM_RC TSS_Hash_Generate_valist(TPMT_HA *digest, va_list ap)
{
	TEE_Result res = 0;
	void *ctx = NULL;
	uint32_t algo = 0;
	TPM_RC rc = 0;
	int len = 0;
	void *buf = NULL;

	algo = tee_algo_from_tpm_hash(digest->hashAlg);
	if (!algo)
		return TSS_RC_HASH;
	if (rc)
		return rc;
	res = crypto_hash_alloc_ctx(&ctx, algo);
	if (res)
		return TSS_RC_HASH;
	res = crypto_hash_init(ctx);
	if (res)
		return TSS_RC_HASH;

	while (true) {
		len = va_arg(ap, int);
		buf = va_arg(ap, void *);
		if (!buf)
			break;

		res = crypto_hash_update(ctx, buf, len);
		if (res)
			goto out;
	}

	res = crypto_hash_final(ctx, (void *)&digest->digest,
				TEE_ALG_GET_DIGEST_SIZE(algo));

out:
	crypto_hash_free_ctx(ctx);
	if (res)
		return TSS_RC_HASH;
	return TPM_RC_SUCCESS;
}

TPM_RC TSS_HMAC_Generate_valist(TPMT_HA *digest, const TPM2B_KEY *hmacKey,
				va_list ap)
{
	TEE_Result res = 0;
	void *ctx = NULL;
	uint32_t algo = 0;
	int len = 0;
	void *buf = NULL;

	algo = tee_algo_from_tpm_hash(digest->hashAlg);
	assert(algo);
	if (!algo)
		return TSS_RC_HASH;
	algo = TEE_ALG_HMAC_ALGO(TEE_ALG_GET_MAIN_ALG(algo));
	res = crypto_mac_alloc_ctx(&ctx, algo);
	assert(!res);
	if (res)
		return TSS_RC_HASH;
	res = crypto_mac_init(ctx, hmacKey->b.buffer, hmacKey->b.size);
	assert(!res);
	if (res)
		return TSS_RC_HASH;

	while (true) {
		len = va_arg(ap, int);
		buf = va_arg(ap, void *);
		if (!buf)
			break;

		res = crypto_mac_update(ctx, buf, len);
		assert(!res);
		if (res)
			goto out;
	}

	res = crypto_mac_final(ctx, (void *)&digest->digest,
			       TEE_ALG_GET_DIGEST_SIZE(algo));

out:
	crypto_mac_free_ctx(ctx);
	assert(!res);
	if (res)
		return TSS_RC_HASH;
	return TPM_RC_SUCCESS;
}

TPM_RC TSS_RandBytes(unsigned char *buffer, uint32_t size)
{
	TEE_Result res = crypto_rng_read(buffer, size);

	if (res) {
		DMSG("crypto_rng_read: res %#"PRIx32, res);
		return TSS_RC_RNG_FAILURE;

	}
	return TPM_RC_SUCCESS;
}

TPM_RC TSS_AES_EncryptCFB(uint8_t *dOut, uint32_t keySizeInBits, uint8_t *key,
			  uint8_t  *iv, uint32_t dInSize, uint8_t *dIn)
{
	DMSG("TSS_RC_NOT_IMPLEMENTED");
	assert(!tssVerbose);
	return TSS_RC_NOT_IMPLEMENTED;
}

TPM_RC TSS_AES_DecryptCFB(uint8_t *dOut, uint32_t keySizeInBits, uint8_t *key,
			  uint8_t *iv, uint32_t dInSize, uint8_t *dIn)
{
	DMSG("TSS_RC_NOT_IMPLEMENTED");
	assert(!tssVerbose);
	return TSS_RC_NOT_IMPLEMENTED;
}

TPM_RC TSS_RSAPublicEncrypt(unsigned char* encrypt_data,
			    size_t encrypt_data_size,
			    const unsigned char *decrypt_data,
			    size_t decrypt_data_size, unsigned char *narr,
			    uint32_t nbytes, unsigned char *earr,
			    uint32_t ebytes, unsigned char *p,
			    int pl, TPMI_ALG_HASH halg)
{
	DMSG("TSS_RC_NOT_IMPLEMENTED");
	assert(!tssVerbose);
	return TSS_RC_NOT_IMPLEMENTED;
}

TPM_RC TSS_ECC_Salt(TPM2B_DIGEST *salt, TPM2B_ENCRYPTED_SECRET *encryptedSalt,
		    TPMT_PUBLIC *publicArea)
{
	DMSG("TSS_RC_NOT_IMPLEMENTED");
	assert(!tssVerbose);
	return TSS_RC_NOT_IMPLEMENTED;
}

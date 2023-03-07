#include <ibmtss/tss.h>
#include <ibmtss/tssresponsecode.h>
#include <ibmtss/tssutils.h>
#include <pta_invoke_tests.h>
#include <trace.h>
#include "misc.h"

static TSS_CONTEXT *tss_ctx;
static TPMI_SH_AUTH_SESSION tpm_sess = TPM_RS_PW;
static const char *nv_passwd = "hej";

extern int tssVerbose;
extern int tssVverbose;


static void print_rc_err(const char *str, TPM_RC rc)
{
	const char *msg;
	const char *submsg;
	const char *num;

	TSS_ResponseCode_toString(&msg, &submsg, &num, rc);
	EMSG("%s: rc %08x, %s%s%s", str, rc, msg, submsg, num);
}

static TPM_RC start_auth_session(void)
{
	StartAuthSession_In in = { };
	StartAuthSession_Out out = { };
	StartAuthSession_Extra extra = { };
	TPM_RC rc = 0;

	in.tpmKey = TPM_RH_NULL;
	in.bind = TPM_RH_NULL;
	in.symmetric.algorithm = TPM_ALG_XOR;
	in.symmetric.keyBits.xorr = TPM_ALG_SHA256;
	in.symmetric.mode.sym = TPM_ALG_NULL;
	in.authHash = TPM_ALG_SHA256;

	rc = TSS_Execute(tss_ctx, (void *)&out, (void *)&in, (void *)&extra,
			 TPM_CC_StartAuthSession, TPM_RH_NULL, NULL, 0);
	tpm_sess = out.sessionHandle;
	return rc;
}

static TEE_Result init_tss_ctx(void)
{
	if (!tss_ctx) {
		TPM_RC rc = 0;

		rc = TSS_Create(&tss_ctx);
		if (rc) {
			print_rc_err("TSS_Create", rc);
			return TEE_ERROR_GENERIC;
		}

		tssVerbose = 1;
		tssVverbose = 1;

		rc = start_auth_session();
		if (rc) {
			print_rc_err("TPM_CC_StartAuthSession", rc);
			return TEE_ERROR_GENERIC;
		}
	}

	return TEE_SUCCESS;
}

static TEE_Result nv_write(TPMI_RH_NV_INDEX nv_idx)
{
	NV_Write_In in = { };
	TPM_RC rc = 0;
	uint32_t sess_attrs = TPMA_SESSION_CONTINUESESSION |
			      TPMA_SESSION_DECRYPT;

	in.authHandle = nv_idx;
	in.nvIndex = nv_idx;
	in.data.t.size = 1;
	in.data.t.buffer[0] = 0xff;
	in.offset = 0;

	rc = TSS_Execute(tss_ctx, NULL, (void *)&in, NULL,
			 TPM_CC_NV_Write, tpm_sess, nv_passwd, sess_attrs,
			 TPM_RH_NULL, NULL, 0);
	if (rc) {
		print_rc_err("TPM_CC_NV_Write", rc);
		return TEE_ERROR_GENERIC;
	}

	return TEE_SUCCESS;
}

static TEE_Result nv_read_public(TPMI_RH_NV_INDEX nv_idx)
{
	NV_ReadPublic_In in = { };
	NV_ReadPublic_Out out = { };
	TPM_RC rc = 0;

	in.nvIndex = nv_idx;
	rc = TSS_Execute(tss_ctx, (void *)&out, (void *)&in, NULL,
			 TPM_CC_NV_ReadPublic, TPM_RH_NULL, NULL, 0);
	if (rc) {
		print_rc_err("TPM_CC_NV_ReadPublic", rc);
		return TEE_ERROR_GENERIC;
	}

	return TEE_SUCCESS;
}

static TEE_Result nv_read(TPMI_RH_NV_INDEX nv_idx)
{
	TEE_Result res = TEE_SUCCESS;
	NV_Read_In in = { };
	NV_Read_Out out = { };
	TPM_RC rc = 0;
	uint32_t sess_attrs = TPMA_SESSION_CONTINUESESSION;

	res = nv_read_public(nv_idx);
	if (res)
		return res;

	in.authHandle = nv_idx;
	in.nvIndex = nv_idx;
	in.size = 12;

	rc = TSS_Execute(tss_ctx, (void *)&out, (void *)&in, NULL,
			 TPM_CC_NV_Read, tpm_sess, nv_passwd, sess_attrs,
			 TPM_RH_NULL, NULL, 0,
			 TPM_RH_NULL, NULL, 0,
			 TPM_RH_NULL, NULL, 0);
	if (rc) {
		print_rc_err("TPM_CC_NV_Read", rc);
		return TEE_ERROR_GENERIC;
	}
	DHEXDUMP(out.data.b.buffer, out.data.b.size);

	return TEE_SUCCESS;
}

static TEE_Result nv_define(TPMI_RH_NV_INDEX nv_idx)
{
	NV_DefineSpace_In in = { };
	TPM_RC rc = 0;

	in.authHandle = TPM_RH_OWNER;
	in.publicInfo.nvPublic.nvIndex = nv_idx;
	in.publicInfo.nvPublic.nameAlg = TPM_ALG_SHA256;
	in.publicInfo.nvPublic.attributes.val = TPMA_NVA_NO_DA |
						TPMA_NVA_ORDINARY |
						TPMA_NVA_AUTHWRITE |
						TPMA_NVA_AUTHREAD;
	in.publicInfo.nvPublic.dataSize = 32;
	rc = TSS_TPM2B_StringCopy(&in.auth.b, nv_passwd,
				  sizeof(in.auth.t.buffer));
	if (rc) {
		print_rc_err("TSS_TPM2B_StringCopy", rc);
		return TEE_ERROR_GENERIC;
	}

	rc = TSS_Execute(tss_ctx, NULL, (void *)&in, NULL,
			 TPM_CC_NV_DefineSpace, TPM_RS_PW, NULL, 0,
			 TPM_RH_NULL, NULL, 0);
	if (rc) {
		print_rc_err("TPM_CC_NV_DefineSpace", rc);
		return TEE_ERROR_GENERIC;
	}

	return TEE_SUCCESS;
}

TEE_Result test_tpm_nv_write(uint32_t param_types,
			     TEE_Param params[TEE_NUM_PARAMS])
{
	TEE_Result res = TEE_SUCCESS;
	uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE);

	if (exp_pt != param_types) {
		DMSG("bad parameter types");
		return TEE_ERROR_BAD_PARAMETERS;
	}

	res = init_tss_ctx();
	if (res)
		return res;

	return nv_write(params[0].value.a);
}

TEE_Result test_tpm_nv_read(uint32_t param_types,
			    TEE_Param params[TEE_NUM_PARAMS])
{
	TEE_Result res = TEE_SUCCESS;
	uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE);

	if (exp_pt != param_types) {
		DMSG("bad parameter types");
		return TEE_ERROR_BAD_PARAMETERS;
	}

	res = init_tss_ctx();
	if (res)
		return res;

	return nv_read(params[0].value.a);
}

TEE_Result test_tpm_nv_define(uint32_t param_types,
			      TEE_Param params[TEE_NUM_PARAMS])
{
	TEE_Result res = TEE_SUCCESS;
	uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE);

	if (exp_pt != param_types) {
		DMSG("bad parameter types");
		return TEE_ERROR_BAD_PARAMETERS;
	}

	res = init_tss_ctx();
	if (res)
		return res;

	return nv_define(params[0].value.a);
}

// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2024, Linaro Limited
 */

#include <assert.h>
#include <io.h>
#include <string.h>
#include <tee/tee_supp_plugin_rpc.h>
#include <trace.h>

#include <ibmtss/tssresponsecode.h>
#include <ibmtss/tsserror.h>
#include <ibmtss/tssprint.h>
#include "tssproperties.h"
#include "tssdev.h"

extern int tssVerbose;
extern int tssVverbose;

static TEE_UUID plugin_uuid =
{ 0x0c4b7dc3, 0x467b, 0x4a3f,
	{ 0x89, 0x97, 0x9f, 0x7f, 0x51, 0x79, 0x62, 0xde } };

TPM_RC TSS_Dev_Transmit(TSS_CONTEXT *tssContext, uint8_t *responseBuffer,
			uint32_t *read, const uint8_t *commandBuffer,
			uint32_t written, const char *message)
{
	TPM_RC rc = 0;
	TEE_Result res = TEE_SUCCESS;
	size_t olen = 0;
	size_t rsz = 0;

	static_assert(MAX_COMMAND_SIZE <= MAX_RESPONSE_SIZE);

	if (message)
		DMSG("TSS message %s", message);

	if (tssVverbose)
		TSS_PrintAll("TSS_Dev_Transmit: Command",
			     commandBuffer, written);

	if (written > MAX_RESPONSE_SIZE) {
		EMSG("Response Overflow, TPM wrote %"PRIu32" bytes, Max response size is %u",
		     written, MAX_RESPONSE_SIZE);
		return TSS_RC_BAD_CONNECTION;
	}

	memcpy(responseBuffer, commandBuffer, written);
	memset(responseBuffer + written, 0, MAX_RESPONSE_SIZE - written);
	res = tee_invoke_supp_plugin_rpc(&plugin_uuid, 0, written,
					 responseBuffer, MAX_RESPONSE_SIZE,
					 &olen);
	if (res) {
		EMSG("Receive error %#"PRIx32, res);
		return TSS_RC_BAD_CONNECTION;
	}

	if (olen < (sizeof(TPM_ST) + (2 * sizeof(uint32_t)))) {
		EMSG("Received %zu bytes < header", olen);
		return TSS_RC_MALFORMED_RESPONSE;
	}

	rsz = get_be32(responseBuffer + sizeof(TPM_ST));
	if (rsz != olen) {
		EMSG("Bytes read (%zu) and Buffer responseSize field (%zu) don't match",
		     olen, rsz);
		return TSS_RC_MALFORMED_RESPONSE;
	}
	*read = olen;

	rc = get_be32(responseBuffer + sizeof(TPM_ST) + sizeof(uint32_t));
	DMSG("Response code: %08x", rc);
	return rc;
}

TPM_RC TSS_Dev_Close(TSS_CONTEXT *tssContext __unused)
{
	return 0;
}


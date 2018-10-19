/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2018, Linaro Limited
 */
#ifndef _OPTEE_SPCI_H
#define _OPTEE_SPCI_H

#include <compiler.h>
#include <types_ext.h>
#include <util.h>

/*
 * This file defines the OP-TEE SPCI protocol used to communicate
 * with an instance of OP-TEE running in secure world via SPCI (Secure
 * Payload Client Interface).
 */

/*****************************************************************************
 * 1 - Value based requests from normal world (using SPCI_REQUEST_VAL)
 *****************************************************************************/

/*
 * The different requests are set selected by the parameter passed in w1,
 * w2-5 are used to supply parameters. The status of the request is
 * returned in w0, w1-4 is used to carry return values and are only valid
 * if w0 is SPCI_SUCCESS. W6 is unused and w7 usage follows SPCI.
 */

/*
 * Get OS revision
 *
 * Call register usage:
 * w0	Not used, ignored
 * w1	Value request ID, OPTEE_SPCI_VAL_REQ_GET_OS_REVISION
 * w2-5 Not used, must be zero
 *
 * Normal return register usage:
 * w0	Return value, SPCI_SUCCESS
 * w1	Major version
 * w2	Minor version
 * w3	Build ID
 */
#define OPTEE_SPCI_VAL_REQ_GET_OS_REVISION		0

/*
 * Exchange capabilities
 *
 * There are currently no capabilties available to exchange. As extensions
 * are added capabilities to exchange will be added. This is how secure and
 * normal world can detect which features are available. Version numbers
 * are not used for this purpose.
 *
 * Call register usage:
 * w0	Not used, ignored
 * w1	Value request ID, OPTEE_SPCI_VAL_REQ_EXCHANGE_CAPABILITIES
 * w2-5 Not used, must be zero
 *
 * Normal return register usage:
 * w0	Return value, SPCI_SUCCESS
 * w1-3 Not used, must be zero
 */
#define OPTEE_SPCI_VAL_REQ_EXCHANGE_CAPABILITIES	1

/*****************************************************************************
 * 2 - Formatting of memory based requests
 *****************************************************************************/

#define OPTEE_SPCI_ATTR_TYPE_NONE		0
#define OPTEE_SPCI_ATTR_TYPE_VALUE_INPUT	1
#define OPTEE_SPCI_ATTR_TYPE_VALUE_OUTPUT	2
#define OPTEE_SPCI_ATTR_TYPE_VALUE_INOUT	3
#define OPTEE_SPCI_ATTR_TYPE_MEMREF_INPUT	4
#define OPTEE_SPCI_ATTR_TYPE_MEMREF_OUTPUT	5
#define OPTEE_SPCI_ATTR_TYPE_MEMREF_INOUT	6

#define OPTEE_SPCI_ATTR_TYPE_MASK		GENMASK_32(7, 0)

/*
 * Meta parameter to be absorbed by the Secure OS and not passed
 * to the Trusted Application.
 *
 * Currently only used with OPTEE_SPCI_CMD_OPEN_SESSION.
 */
#define OPTEE_SPCI_ATTR_META			BIT(8)

/*
 * Same values as TEE_LOGIN_* from TEE Internal API
 */
#define OPTEE_SPCI_LOGIN_PUBLIC			0
#define OPTEE_SPCI_LOGIN_USER			1
#define OPTEE_SPCI_LOGIN_GROUP			2
#define OPTEE_SPCI_LOGIN_APPLICATION		4
#define OPTEE_SPCI_LOGIN_APPLICATION_USER	5
#define OPTEE_SPCI_LOGIN_APPLICATION_GROUP	6

/*
 * Number of preallocated RPC paramters parameters to be supplied with
 * struct optee_spci below. This value goes into struct
 * optee_spci::rpc_num_params.
 */
#define OPTEE_SPCI_RPC_NUM_PARAMS		4

#ifndef ASM
/**
 * struct optee_spci_param_memref - registered memory reference parameter
 * @offs:	Offset into shared memory reference
 * @size:	Size of the buffer
 * @shm_ref:	Shared memory reference, pointer to a struct tee_shm
 */
struct optee_spci_param_memref {
	uint64_t offs;
	uint64_t size;
	uint64_t shm_ref;
};

/**
 * struct optee_spci_param_value - values
 * @a: first value
 * @b: second value
 * @c: third value
 */
struct optee_spci_param_value {
	uint64_t a;
	uint64_t b;
	uint64_t c;
};

/**
 * struct optee_spci_param - parameter
 * @attr: attributes
 * @mem: a memory reference
 * @value: a value
 *
 * @attr & OPTEE_SPCI_ATTR_TYPE_MASK indicates if memref or value is used in
 * the union. OPTEE_SPCI_ATTR_TYPE_VALUE_* indicates value,
 * OPTEE_SPCI_ATTR_TYPE_MEMREF_* indicates memref.
 * OPTEE_SPCI_ATTR_TYPE_NONE indicates that none of the members are used.
 */
struct optee_spci_param {
	uint64_t attr;
	union {
		struct optee_spci_param_memref memref;
		struct optee_spci_param_value value;
	} u;
};

/**
 * struct optee_spci - memory based SPCI requests
 * @cmd: Command, one of OPTEE_SPCI_CMD_*
 * @func: Trusted Application function, specific to the Trusted Application,
 *	     used if cmd == OPTEE_SPCI_CMD_INVOKE_COMMAND
 * @session: In parameter for all OPTEE_SPCI_CMD_* except
 *	     OPTEE_SPCI_CMD_OPEN_SESSION where it's an output parameter instead
 * @cancel_id: Cancellation id, a unique value to identify this request
 * @ret: return value
 * @ret_origin: origin of the return value
 * @num_params: number of parameters supplied to the OS Command
 * @params: the parameters supplied to the OS Command
 * @rpc_cmd: RPC command, one of OPTEE_SPCI_RPC_CMD_*
 * @rpc_num_params: Number of preallocated RPC parameters, follows the
 *		    params after @num_params
 * @rpc_ret: RPC return value
 *
 * Normal calls to Trusted OS uses this struct. If cmd requires further
 * information than what these fields hold it can be passed as a parameter
 * tagged as meta (setting the OPTEE_SPCI_ATTR_META bit in corresponding
 * attrs field). All parameters tagged as meta have to come first.
 *
 * When a call to secure world returns the caller first checks @rpc_cmd to
 * see if a RPC is requested. @rpc_cmd = OPTEE_RPC_CMD_NONE means that
 * there's no RPC requested and the original call is completed. If a RPC is
 * requested @rpc_cmd indicates which OPTEE_RPC_CMD_* and the number of
 * parameters are @rpc_num_params, the parameters are found at
 * @params[@num_params]. The @rpc_ret is used to return a simple return
 * value from the request.
 */
struct optee_spci {
	uint32_t cmd;
	uint32_t func;
	uint32_t session;
	uint32_t cancel_id;
	uint32_t ret;
	uint32_t ret_origin;
	uint32_t num_params;
	uint32_t rpc_cmd;
	uint32_t rpc_num_params;
	uint32_t rpc_ret;

	/*
	 * num_params + rpc_num_params tells the actual number of element
	 * in params
	 */
	struct optee_spci_param params[];
};

/**
 * OPTEE_SPCI_GET_SIZE - return size of struct optee_spci
 *
 * @num_params: Number of parameters embedded in the struct optee_spci
 *
 * Returns the size of the struct optee_spci together with the number
 * of embedded parameters.
 */
#define OPTEE_SPCI_GET_SIZE(num_params, rpc_num_params) \
	(sizeof(struct optee_spci) + \
	 sizeof(struct optee_spci_param) * ((num_params) + (rpc_num_params)))

#endif /*ASM*/

/*****************************************************************************
 * 3 - Memory based requests from normal world (using SPCI_REQUEST)
 *****************************************************************************/

/*
 * Do a secure call with struct optee_spci as argument
 * The OPTEE_SPCI_CMD_* below defines what goes in struct optee_spci::cmd
 *
 * OPTEE_SPCI_CMD_OPEN_SESSION opens a session to a Trusted Application.
 * The first two parameters are tagged as meta, holding two value
 * parameters to pass the following information:
 * param[0].u.value.a-b uuid of Trusted Application
 * param[1].u.value.a-b uuid of Client
 * param[1].u.value.c Login class of client OPTEE_SPCI_LOGIN_*
 *
 * OPTEE_SPCI_CMD_INVOKE_COMMAND invokes a command a previously opened
 * session to a Trusted Application.  struct optee_spci::func is Trusted
 * Application function, specific to the Trusted Application.
 *
 * OPTEE_SPCI_CMD_CLOSE_SESSION closes a previously opened session to
 * Trusted Application.
 *
 * OPTEE_SPCI_CMD_CANCEL cancels a currently invoked command.
 *
 */
#define OPTEE_SPCI_CMD_OPEN_SESSION	0
#define OPTEE_SPCI_CMD_INVOKE_COMMAND	1
#define OPTEE_SPCI_CMD_CLOSE_SESSION	2
#define OPTEE_SPCI_CMD_CANCEL		3

/*
 * Normal entry, initial request of a command
 *
 * Call register usage as defined for SPCI_REQUEST_START:
 * w0	0x84000067 for SMC32 version or 0xC4000067 for SMC64 version
 * a1	Memory handle holding the struct optee_spci
 * a2	Memory offset into the memory region referenced by the memory handle
 * a3	Size of the struct optee_spci
 * a4-5	Ignored, must be zero
 * w6	Ignored
 * w7	Ignored
 *
 * Return register usage, completed:
 * w0	Return value, SPCI_SUCCESS
 *
 * Return register usage, need RPC and to be resumed:
 * w0	Return value, SPCI_QUEUED
 * w1	Resume token, to be passed in resume entry below
 *
 * Return register usage busy, need to try again when one call is completed
 * w0	Return value, SPCI_BUSY
 *
 * Error register usage
 * w0	Return value
 */

/*
 * Resume entry, resume a command request
 *
 * Call register usage as defined for SPCI_REQUEST_RESUME:
 * w0	0x8400006B for SMC32 version or 0xC400006B for SMC64 version
 * w1	Resume token from a successful return from SPCI_REQUEST_START
 * a2-6	Ignored, must be zero
 * w7	Ignored
 *
 * Return register usage, completed:
 * w0	Return value, SPCI_SUCCESS
 *
 * Return register usage, need RPC and to be resumed:
 * w0	Return value, SPCI_QUEUED
 * w1	Resume token, to be passed in resume entry
 *
 * Error register usage
 * w0	Return value
 */

#endif /* _OPTEE_SPCI_H */

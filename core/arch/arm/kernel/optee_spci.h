/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2019, Linaro Limited
 */
#ifndef __OPTEE_SPCI_H
#define __OPTEE_SPCI_H

/*
 * OP-TEE SPCI protocol
 *
 * There's two kinds of messages, fast messages and standard messages.
 * Fast messages are identified by having the highest bit in the first
 * 32-bit word of the message set.
 *
 * Standard messages are conversly recognized with the highest bit in the
 * first 32-bit cleared.
 *
 * This classification correpsonds with the classification in the SMC
 * Calling Convention and is chosen for easier coexistence with the earlier
 * SMC based implementation of the driver. The SMC ids and return values
 * which are passed in the first register are reused in the first 32-bit
 * word in SPCI messages.
 *
 * Fast messages are defined by struct optee_spci_fast_arg and struct
 * optee_spci_fast_res below. struct optee_spci_fast_arg is used when
 * passing a message from Normal world whilc struct optee_spci_fast_res is
 * used in the other direction.
 *
 * Standard messages consists of a struct optee_spci_std_hdr optionally
 * followed by a struct optee_msg_arg.
 */

/**
 * struct optee_spci_std_hdr - SPCI payload header for standard messages
 * @a0:	Command if sent from Normal world to OP-TEE or
 *      return value if sent from OP-TEE to Normal world
 * @resume_info: Padding if not a RPC message else resume information.
 *		 Filled in when passed from OP-TEE to Normal world and
 *		 preserved in the other direction
 *
 * Standard messages consists of a struct optee_spci_std_hdr optionally
 * followed by a struct optee_msg_arg. The struct optee_msg_arg is only
 * omitted during a RPC while delivering a non-secure interrupt.
 *
 * When sent from Normal world @a0 is either OPTEE_SMC_CALL_WITH_ARG for
 * a request or OPTEE_SMC_CALL_RETURN_FROM_RPC if returning from a RPC.
 *
 * When sent from Secure world @a0 holds either the result of the request
 * or a RPC reqeust. A normal result has all the upper 16 bits of @a0
 * cleared while a RPC request has all the upper 16 bits set. Only two RPC
 * requests are supported, OPTEE_SMC_RPC_FUNC_FOREIGN_INTR to deliver a
 * non-secure interrupt and OPTEE_SMC_RPC_FUNC_CMD to pass a request using
 * struct optee_msg_arg.
 */
struct optee_spci_std_hdr {
	uint32_t a0;
	uint32_t resume_info;
};

/**
 * struct optee_spci_fast_arg - Fast SPCI message from Normal world
 *
 * Matches the 8 32-bit registers used in the 32-bit SMC Calling Convention.
 */
struct optee_spci_fast_arg {
	uint32_t a0;
	uint32_t a1;
	uint32_t a2;
	uint32_t a3;
	uint32_t a4;
	uint32_t a5;
	uint32_t a6;
	uint32_t a7;
};

/**
 * struct optee_spci_fast_res - Fast SPCI result message from Secure World
 *
 * Matches the 4 32-bit registers used to pass a result in the 32-bit SMC
 * Calling Convention.
 */
struct optee_spci_fast_res {
	uint32_t a0;
	uint32_t a1;
	uint32_t a2;
	uint32_t a3;
};

#endif /*__OPTEE_SPCI_H*/

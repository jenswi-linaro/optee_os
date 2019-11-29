/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2019, Linaro Limited
 */

/*
 * This file is exported by OP-TEE and is in kept in sync between secure
 * world and normal world kernel driver. We're using ARM SPCI Beta0
 * specification.
 */

#ifndef __OPTEE_SPCI_H
#define __OPTEE_SPCI_H

#include <spci.h>
#include <util.h>

/*
 * Normal world sends requests with SPCI_MSG_SEND_DIRECT_REQ and
 * responses are returned with SPCI_MSG_SEND_DIRECT_RESP for normal
 * messages.
 *
 * All requests with SPCI_MSG_SEND_DIRECT_REQ and SPCI_MSG_SEND_DIRECT_RESP
 * are using the AArch32 SMC calling convention with register usage as
 * defined in SPCI specification:
 * w0:    Function ID (0x8400006F or 0x84000070)
 * w1:    Source/Destination IDs
 * w2:    Reserved (MBZ)
 * w3-w7: Implementation defined, free to be used below
 */

#define OPTEE_SPCI_VERSION_MAJOR	SPCI_VERSION_MAJOR
#define OPTEE_SPCI_VERSION_MINOR	SPCI_VERSION_MINOR

/*
 * Returns the API version implemented, currently follows the SPCI version.
 * Call register usage:
 * w3:	  Service ID, OPTEE_SPCI_GET_API_VERSION
 * w4-w7: Not used (MBZ)
 *
 * Return register usage:
 * w3:    OPTEE_SPCI_VERSION_MAJOR
 * w4:    OPTEE_SPCI_VERSION_MINOR
 * w5-w7: Not used (MBZ)
 */
#define OPTEE_SPCI_GET_API_VERSION	0

/*
 * Returns the revision of OP-TEE.
 *
 * Used by non-secure world to figure out which version of the Trusted OS
 * is installed. Note that the returned revision is the revision of the
 * Trusted OS, not of the API.
 *
 * Call register usage:
 * w3:	  Service ID, OPTEE_SPCI_GET_OS_VERSION
 * w4-w7: Unused (MBZ)
 *
 * Return register usage:
 * w3:    CFG_OPTEE_REVISION_MAJOR
 * w4:    CFG_OPTEE_REVISION_MINOR
 * w5:	  TEE_IMPL_GIT_SHA1 (or zero if not supported)
 */
#define OPTEE_SPCI_GET_OS_VERSION	1

/*
 * Exchange capabilities between normal world and secure world.
 *
 * Currently there are no defined capabilities. When features are added new
 * capabilities may be added.
 *
 * Call register usage:
 * w3:    Service ID, OPTEE_SPCI_EXCHANGE_CAPABILITIES
 * w4-w7: Note used (MBZ)
 *
 * Return register usage:
 * w3:    Error code, 0 on success
 * w4-w7: Note used (MBZ)
 */
#define OPTEE_SPCI_EXCHANGE_CAPABILITIES 2

/*
 * Register shared memory
 *
 * Called after the memory share transaction has been started with all the
 * calls with SPCI_MEM_SHARE. OP-TEE will during this call retrieve the
 * addresses from SPM with SPCI_MEM_RETRIEVE_REQ.
 *
 * Call register usage:
 * w3:    Service ID, OPTEE_SPCI_REGISTER_SHM
 * w4:    Handle
 * w5:    Type of shared memory, OPTEE_SPCI_SHM_TYPE_*
 * w6:    Not used (MBZ)
 * w7:    Not used (MBZ)
 *
 * Return register usage:
 * w3:    Error code, 0 on success or errors as specified for
 *        SPCI_MEM_RETRIEVE_REQ
 * w4-w7: Not used (MBZ)
 */
/* Memory which is used to hold command buffer during RPC */
#define OPTEE_SPCI_SHM_TYPE_RPC		0
/* Memory that can be shared with a non-secure user space application */
#define OPTEE_SPCI_SHM_TYPE_APPL	1
/* Memory only shared with non-secure kernel */
#define OPTEE_SPCI_SHM_TYPE_KERNEL	2
/* Memory shared with non-secure kernel and user space */
#define OPTEE_SPCI_SHM_TYPE_GLOBAL	3
#define OPTEE_SPCI_REGISTER_SHM		3

/*
 * Unregister shared memory
 *
 * Called before the memory relinquish transaction has been started. This
 * call will block until OP-TEE is done using the shared memory. OP-TEE
 * will notify SPM that this peice of shared memory is not used any longer
 * with SPCI_MEM_RELINQUISH.
 *
 * Call register usage:
 * w3:    Service ID, OPTEE_SPCI_UNREGISTER_SHM
 * w4:    Global handle
 * w5:    Not used (MBZ)
 * w6:    Not used (MBZ)
 * w7:    Not used (MBZ)
 *
 * Return register usage:
 * w3:    Error code:
 *        0: success
 *        INVALID_PARAMETERS: invalid global handle
 * w4-w7: Not used (MBZ)
 */
#define OPTEE_SPCI_UNREGISTER_SHM	4

/*
 * Call with struct optee_msg_arg as argument
 *
 * Normal call register usage:
 * w3:    Service ID, OPTEE_SPCI_YIELDING_CALL
 * w4:    OPTEE_SPCI_YIELDING_CALL_START
 * w5:    Shared memory handle
 * w6:    Offset into shared memory pointing to a struct optee_msg_arg
 * w7:    Not used (MBZ)
 *
 * Resume from RPC register usage:
 * w3:    Service ID, OPTEE_SPCI_YIELDING_CALL
 * w4:    OPTEE_SPCI_YIELDING_CALL_RESUME
 * w5:    Resume info, value of w7 when OPTEE_SPCI_CALL_WITH_ARG returned
 * w6-w7: Not used (MBZ)
 *
 * Normal return register usage:
 * w3:	  Error code, 0 on success
 * w4:    OPTEE_SPCI_YIELDING_CALL_RETURN_NORMAL
 * w5-w7: Not used (MBZ)
 *
 * RPC cmd return register usage:
 * w3:	  Error code == 0
 * w4:    OPTEE_SPCI_YIELDING_CALL_RETURN_RPC_CMD
 * w5:    Resume info
 * w6:    Shared memory handle
 * w7:    Offset into shared memory pointing to the struct optee_msg_arg used
 *	  for RPC
 *
 * RPC cmd return register usage:
 * w3:	  Error code == 0
 * w4:    OPTEE_SPCI_YIELDING_CALL_RETURN_INTERRUPT
 * w5:    Resume info
 *
 * Possible error codes:
 * 0:                       Success
 * SPCI_DENIED:             w4 isn't one of OPTEE_SPCI_YIELDING_CALL_START
 *                          or OPTEE_SPCI_YIELDING_CALL_RESUME
 *
 * Possible error codes for OPTEE_SPCI_YIELDING_CALL_START
 * SPCI_BUSY:               Number of OP-TEE OS threads exceeded,
 *                          try again later
 * SPCI_DENIED:             RPC shared memory object not found
 * SPCI_INVALID_PARAMETER:  Bad shared memory handle or offset into the memory
 *
 * Possible error codes for OPTEE_SPCI_YIELDING_CALL_RESUME
 * SPCI_INVALID_PARAMETER:  Bad resume info
 *
 */
#define OPTEE_SPCI_YIELDING_CALL_START			0
#define OPTEE_SPCI_YIELDING_CALL_RESUME			1
#define OPTEE_SPCI_YIELDING_CALL_RETURN_NORMAL		0
#define OPTEE_SPCI_YIELDING_CALL_RETURN_RPC_CMD		1
#define OPTEE_SPCI_YIELDING_CALL_RETURN_INTERRUPT	2
#define OPTEE_SPCI_YIELDING_CALL	5

#endif /*__OPTEE_SPCI_H*/


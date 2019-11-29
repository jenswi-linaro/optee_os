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
 * w3:    Service ID, OPTEE_SPCI_GET_API_VERSION
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
 * w3:    Service ID, OPTEE_SPCI_GET_OS_VERSION
 * w4-w7: Unused (MBZ)
 *
 * Return register usage:
 * w3:    CFG_OPTEE_REVISION_MAJOR
 * w4:    CFG_OPTEE_REVISION_MINOR
 * w5:    TEE_IMPL_GIT_SHA1 (or zero if not supported)
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
 * w4:    Bit[1:0]:  Number of pages of shared memory to register with
 *                   OPTEE_SPCI_REGISTER_RPC_SHM to support RPC
 *        Bit[31:2]: Reserved (MBZ)
 * w5-w7: Note used (MBZ)
 */
#define OPTEE_SPCI_EXCHANGE_CAPABILITIES 2

/*
 * Call with struct optee_msg_arg as argument in the supplied shared memory
 * with a zero internal offset and normal cached memory attributes.
 * Register usage:
 * w3:    Service ID, OPTEE_SPCI_YIELDING_CALL
 * w4:    OPTEE_SPCI_YIELDING_CALL_START
 * w5:    Shared memory handle
 * w6:    Offset into shared memory pointing to a struct optee_msg_arg
 * w7:    Page count
 *
 * Call to register shared memory memory. The data is supplied in shared
 * memory with a zero internal offset and normal cached memory attributes.
 * The data is formatted as described in SPCI 1.0 Beta1 Table 137 "
 * Descriptor to retrieve a donated, lent or shared memory region".
 * Register usage:
 * w3:    Service ID, OPTEE_SPCI_YIELDING_CALL
 * w4:    OPTEE_SPCI_YIELDING_CALL_REGISTER_SHM
 * w5:    Shared memory handle
 * w6:    Offset into shared memory pointing the table
 * w7:    Page count
 *
 * Call unregister shared memory register usage:
 * w3:    Service ID, OPTEE_SPCI_YIELDING_CALL
 * w4:    OPTEE_SPCI_YIELDING_CALL_UNREGISTER_SHM
 * w5:    Shared memory handle
 * w6-w7: Not used (MBZ)
 *
 * Resume from RPC register usage:
 * w3:    Service ID, OPTEE_SPCI_YIELDING_CALL
 * w4:    OPTEE_SPCI_YIELDING_CALL_RESUME
 * w5:    Resume info
 * w6:    Global handle of shared memory allocated if returning from
 *        OPTEE_SPCI_YIELDING_CALL_RETURN_ALLOC_SHM.
 *        If allocation failed 0.
 *        If resuming from another RPC, not used (MBZ).
 * w7:    Internal offset from start address of shared memory if returning
 *        from OPTEE_SPCI_YIELDING_CALL_RETURN_ALLOC_SHM. If internal
 *        offset > 0 then one more page than requested has been allocated.
 *        If resuming from another RPC, not used (MBZ).
 *
 * Normal return (yielding call is completed) register usage:
 * w3:    Error code, 0 on success
 * w4:    OPTEE_SPCI_YIELDING_CALL_RETURN_NORMAL
 * w5-w7: Not used (MBZ)
 *
 * Alloc SHM return (RPC from secure world) register usage:
 * w3:    Error code == 0
 * w4:    OPTEE_SPCI_YIELDING_CALL_RETURN_ALLOC_SHM
 * w5:    Resume info
 * w6:    Number of pages of shared memory
 * w7:    Type of shared memory OPTEE_SPCI_SHM_TYPE_*
 *
 * Free SHM return (RPC from secure world) register usage:
 * w3:    Error code == 0
 * w4:    OPTEE_SPCI_YIELDING_CALL_RETURN_FREE_SHM
 * w5:    Resume info
 * w6:    Global handle, used to free a handle previously allocated with
 *        OPTEE_SPCI_YIELDING_CALL_RETURN_ALLOC_SHM
 * w7:    Type of shared memory OPTEE_SPCI_SHM_TYPE_*
 *
 * RPC cmd return (RPC from secure world) register usage:
 * w3:    Error code == 0
 * w4:    OPTEE_SPCI_YIELDING_CALL_RETURN_RPC_CMD
 * w5:    Resume info
 * w6:    Shared memory handle
 * w7:    Offset into shared memory pointing to the struct optee_msg_arg used
 *        for RPC
 *
 * RPC interrupt return (RPC from secure world) register usage:
 * w3:    Error code == 0
 * w4:    OPTEE_SPCI_YIELDING_CALL_RETURN_INTERRUPT
 * w5:    Resume info
 * w6-w7: Not used (MBZ)
 *
 * Possible error codes in register w3:
 * 0:                       Success
 * SPCI_DENIED:             w4 isn't one of OPTEE_SPCI_YIELDING_CALL_START
 *                          OPTEE_SPCI_YIELDING_CALL_REGISTER_SHM,
 *                          OPTEE_SPCI_YIELDING_CALL_UNREGISTER_SHM or
 *                          OPTEE_SPCI_YIELDING_CALL_RESUME
 *
 * Possible error codes for OPTEE_SPCI_YIELDING_CALL_START,
 * OPTEE_SPCI_YIELDING_CALL_REGISTER_SHM and
 * OPTEE_SPCI_YIELDING_CALL_UNREGISTER_SHM
 * SPCI_BUSY:               Number of OP-TEE OS threads exceeded,
 *                          try again later
 * SPCI_DENIED:             RPC shared memory object not found
 * SPCI_INVALID_PARAMETER:  Bad shared memory handle or offset into the memory
 *
 * Possible error codes for OPTEE_SPCI_YIELDING_CALL_RESUME
 * SPCI_INVALID_PARAMETER:  Bad resume info
 *
 */
#define OPTEE_SPCI_YIELDING_CALL_WITH_ARG		0
#define OPTEE_SPCI_YIELDING_CALL_REGISTER_SHM		1
#define OPTEE_SPCI_YIELDING_CALL_UNREGISTER_SHM		2
#define OPTEE_SPCI_YIELDING_CALL_RESUME			3
#define OPTEE_SPCI_YIELDING_CALL_RETURN_NORMAL		0
#define OPTEE_SPCI_YIELDING_CALL_RETURN_ALLOC_SHM	1
#define OPTEE_SPCI_YIELDING_CALL_RETURN_FREE_SHM	2
#define OPTEE_SPCI_YIELDING_CALL_RETURN_RPC_CMD		3
#define OPTEE_SPCI_YIELDING_CALL_RETURN_INTERRUPT	4
#define OPTEE_SPCI_SHM_TYPE_APPLICATION			0
#define OPTEE_SPCI_SHM_TYPE_KERNEL			1
#define OPTEE_SPCI_YIELDING_CALL	6

#endif /*__OPTEE_SPCI_H*/


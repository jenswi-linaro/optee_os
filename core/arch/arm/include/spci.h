/*
 * Copyright (c) 2018, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SPCI_H
#define __SPCI_H

/* This is based on the SPCI Beta0 specification */

#include <smccc.h>
#include <stdint.h>

/* SPCI Beta0 error codes. */
#define SPCI_NOT_SUPPORTED	-1
#define SPCI_INVALID_PARAMETER	-2
#define SPCI_NO_MEMORY		-3
#define SPCI_BUSY		-4
#define SPCI_INTERRUPTED	-5
#define SPCI_DENIED		-6
#define SPCI_RETRY		-7

/* The macros below are used to identify SPCI calls from the SMC function ID */
#define SPCI_FID_MASK		U(0xffff)
#define SPCI_FID_MIN_VALUE	U(0x60)
#define SPCI_FID_MAX_VALUE	U(0x7f)
#define is_spci_fid(_fid)						\
		((((_fid) & SPCI_FID_MASK) >= SPCI_FID_MIN_VALUE) &&	\
		 (((_fid) & SPCI_FID_MASK) <= SPCI_FID_MAX_VALUE))

/* SPCI_VERSION helpers */
#define SPCI_VERSION_MAJOR		U(0)
#define SPCI_VERSION_MAJOR_SHIFT	16
#define SPCI_VERSION_MAJOR_MASK		U(0x7FFF)
#define SPCI_VERSION_MINOR		U(9)
#define SPCI_VERSION_MINOR_SHIFT	0
#define SPCI_VERSION_MINOR_MASK		U(0xFFFF)
#define MAKE_SPCI_VERSION(major, minor)	((((major) & SPCI_VERSION_MAJOR_MASK)  \
						<< SPCI_VERSION_MAJOR_SHIFT) | \
					((minor) & SPCI_VERSION_MINOR_MASK))
#define SPCI_VERSION_COMPILED		SPCI_VERSION_FORM(SPCI_VERSION_MAJOR, \
							  SPCI_VERSION_MINOR)

/* SPCI_MSG_SEND helpers */
#define SPCI_MSG_SEND_ATTRS_BLK_SHIFT	U(0)
#define SPCI_MSG_SEND_ATTRS_BLK_MASK	U(0x1)
#define SPCI_MSG_SEND_ATTRS_BLK		U(0)
#define SPCI_MSG_SEND_ATTRS_BLK_NOT	U(1)
#define SPCI_MSG_SEND_ATTRS(blk)	(((blk) & SPCI_MSG_SEND_ATTRS_BLK_MASK)	\
					 << SPCI_MSG_SEND_ATTRS_BLK_SHIFT)

/* SPCI Beta0 AArch32 Function IDs */
#define SPCI_ERROR			U(0x84000060)
#define SPCI_SUCCESS			U(0x84000061)
#define SPCI_INTERRUPT			U(0x84000062)
#define SPCI_VERSION			U(0x84000063)
#define SPCI_FEATURES			U(0x84000064)
#define SPCI_RX_RELEASE			U(0x84000065)
#define SPCI_RXTX_MAP_SMC32		U(0x84000066)
#define SPCI_RXTX_UNMAP			U(0x84000067)
#define SPCI_PARTITION_INFO_GET		U(0x84000068)
#define SPCI_ID_GET			U(0x84000069)
#define SPCI_MSG_POLL			U(0x8400006A)
#define SPCI_MSG_WAIT			U(0x8400006B)
#define SPCI_MSG_YIELD			U(0x8400006C)
#define SPCI_MSG_RUN			U(0x8400006D)
#define SPCI_MSG_SEND			U(0x8400006E)
#define SPCI_MSG_SEND_DIRECT_REQ_SMC32	U(0x8400006F)
#define SPCI_MSG_SEND_DIRECT_RESP_SMC32	U(0x84000070)
#define SPCI_MEM_DONATE_SMC32		U(0x84000071)
#define SPCI_MEM_LEND_SMC32		U(0x84000072)
#define SPCI_MEM_SHARE_SMC32		U(0x84000073)
#define SPCI_MEM_RETRIEVE_REQ_SMC32	U(0x84000074)
#define SPCI_MEM_RETRIEVE_RESP_SMC32	U(0x84000075)
#define SPCI_MEM_RELINQUISH_SMC32	U(0x84000076)
#define SPCI_MEM_RECLAIM		U(0x84000077)

/* SPCI Beta0 AArch64 Function IDs */
#define SPCI_RXTX_MAP_SMC64		U(0xC4000066)
#define SPCI_MSG_SEND_DIRECT_REQ_SMC64	U(0xC400006F)
#define SPCI_MSG_SEND_DIRECT_RESP_SMC64	U(0xC4000070)
#define SPCI_MEM_DONATE_SMC64		U(0xC4000071)
#define SPCI_MEM_LEND_SMC64		U(0xC4000072)
#define SPCI_MEM_SHARE_SMC64		U(0xC4000073)
#define SPCI_MEM_RETRIEVE_REQ_SMC64	U(0xC4000074)

/* Number of SPCI calls (above) implemented */
#define SPCI_NUM_CALLS			U(24)

/*
 * Reserve a special value for traffic targeted to the Hypervisor or SPM.
 */
#define SPCI_TARGET_INFO_MBZ		U(0x0)

/*
 * Reserve a special value for MBZ parameters.
 */
#define SPCI_PARAM_MBZ			U(0x0)

#endif /* __SPCI_H */

/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2018-2019, Arm Limited. All rights reserved.
 */
#ifndef SPCI_H
#define SPCI_H

#include <smccc.h>
#include <util.h>

/* SPCI error codes. */

/* SPCI Alpha2 error codes. */

#define SPCI_SUCCESS             0
#define SPCI_NOT_SUPPORTED      -1
#define SPCI_INVALID_PARAMETER  -2
#define SPCI_NO_MEMORY          -3
#define SPCI_BUSY               -4
#define SPCI_INTERRUPTED        -5
#define SPCI_DENIED             -6
#define SPCI_RETRY              -7

/* SPCI_VERSION helpers */
#define SPCI_VERSION_MAJOR              U(0)
#define SPCI_VERSION_MAJOR_SHIFT        16
#define SPCI_VERSION_MAJOR_MASK         U(0x7FFF)
#define SPCI_VERSION_MINOR              U(9)
#define SPCI_VERSION_MINOR_SHIFT        0
#define SPCI_VERSION_MINOR_MASK         U(0xFFFF)
#define SPCI_VERSION_FORM(major, minor) ((((major) & SPCI_VERSION_MAJOR_MASK)  \
					  << SPCI_VERSION_MAJOR_SHIFT) | \
					 ((minor) & SPCI_VERSION_MINOR_MASK))
#define SPCI_VERSION_COMPILED           SPCI_VERSION_FORM(SPCI_VERSION_MAJOR, \
							  SPCI_VERSION_MINOR)

/* SPCI_MSG_RECV helpers */
#define SPCI_MSG_RECV_ATTRS_SHIFT       U(0)
#define SPCI_MSG_RECV_ATTRS_MASK        U(0x3)
#define SPCI_MSG_RECV_ATTRS_POLL        U(0)
#define SPCI_MSG_RECV_ATTRS_BLK         U(1)
#define SPCI_MSG_RECV_ATTRS(attr)       (((attr) & SPCI_MSG_RECV_ATTRS_MASK) \
					 << SPCI_MSG_RECV_ATTRS_SHIFT);

#define SPCI_MSG_RECV_STATUS_SHIFT       U(31)
#define SPCI_MSG_RECV_STATUS_MASK        U(0x1)
#define SPCI_MSG_RECV_STATUS_SUCCESS     U(0)
#define SPCI_MSG_RECV_STATUS_ERROR       U(1)

#define SPCI_MSG_RECV_MSGLOC_SHIFT       U(30)
#define SPCI_MSG_RECV_MSGLOC_MASK        U(0x1)
#define SPCI_MSG_RECV_MSGLOC_SEC         U(0)
#define SPCI_MSG_RECV_MSGLOC_NSEC        U(1)
#define SPCI_MSG_RECV_MSGLOC(loc)        (((loc) & SPCI_MSG_RECV_MSGLOC_MASK) \
					 << SPCI_MSG_RECV_MSGLOC_SHIFT);


/* SPCI_MSG_PUT helpers */
#define SPCI_MSG_PUT_MSGLOC_SHIFT       U(31)
#define SPCI_MSG_PUT_MSGLOC_MASK        U(0x1)
#define SPCI_MSG_PUT_MSGLOC_SEC         U(0)
#define SPCI_MSG_PUT_MSGLOC_NSEC        U(1)
#define SPCI_MSG_PUT_MSGLOC(loc)        (((loc) & SPCI_MSG_PUT_MSGLOC_MASK) \
					 << SPCI_MSG_PUT_MSGLOC_SHIFT);

/* SPCI_MSG_SEND helpers */
#define SPCI_MSG_SEND_ATTRS_BLK_SHIFT   U(0)
#define SPCI_MSG_SEND_ATTRS_BLK_MASK    U(0x1)
#define SPCI_MSG_SEND_ATTRS_BLK         U(0)
#define SPCI_MSG_SEND_ATTRS_BLK_NOT     U(1)

#define SPCI_MSG_SEND_ATTRS_MSGLOC_SHIFT        U(3)
#define SPCI_MSG_SEND_ATTRS_MSGLOC_MASK         U(0x1)
#define SPCI_MSG_SEND_ATTRS_MSGLOC_SEC          U(0)
#define SPCI_MSG_SEND_ATTRS_MSGLOC_NSEC         U(1)
#define SPCI_MSG_SEND_ATTRS(blk, loc)           ((((loc) & SPCI_MSG_SEND_ATTRS_MSGLOC_MASK)     \
						  << SPCI_MSG_SEND_ATTRS_MSGLOC_SHIFT) |        \
	(((blk) & SPCI_MSG_SEND_ATTRS_BLK_MASK)        \
	 << SPCI_MSG_SEND_ATTRS_BLK_SHIFT))

/* SPCI Alpha2 SMC Function IDs */
#define SPCI_VERSION                    U(0x84000060)
#define SPCI_MSG_BUF_LIST_EXCHANGE      U(0xC4000061)
#define SPCI_MSG_RECV                   U(0x84000062)
#define SPCI_MSG_PUT                    U(0x84000063)
#define SPCI_MSG_SEND                   U(0x84000064)
#define SPCI_MSG_SEND_RECV              U(0x84000065)
#define SPCI_RUN                        U(0x84000066)
#define SPCI_YIELD                      U(0x84000067)

#ifndef __ASSEMBLER__
#define MAX_SIG_LENGTH		U(4)
#define SPCI_BUF_SIGNATURE	"BUF"

enum spci_buf_state {
	SPCI_BUF_STATE_EMPTY = U(0),
	SPCI_BUF_STATE_FULL
};

typedef struct spci_buf_hdr {
	const uint8_t	signature[MAX_SIG_LENGTH];
        uint32_t        page_count;     /* Including this header */
	uint32_t	state;
        uint8_t         reserved[4];
} spci_buf_hdr_t;

typedef struct spci_buf {
	spci_buf_hdr_t	hdr;
	uint8_t		buf[];
} spci_buf_t;

#define SPCI_BUF_HDR_INIT	{SPCI_BUF_SIGNATURE, SPCI_BUF_STATE_EMPTY}

/* Type of memory region: Architectural or imp. def. */
#define SPCI_MEM_REG_TYPE_SHIFT	U(31)
#define SPCI_MEM_REG_TYPE_MASK	U(0x1)
#define SPCI_MEM_REG_TYPE_ARCH	U(0)
#define SPCI_MEM_REG_TYPE_IMP	U(1)
#define SPCI_MEM_REG_TYPE(type)	(((type) & SPCI_MEM_REG_TYPE_MASK)	\
				 << SPCI_MEM_REG_TYPE_SHIFT)

/* Type of architectural memory region: RX or TX buffer */
#define SPCI_MEM_REG_ARCH_TYPE_SHIFT	U(0)
#define SPCI_MEM_REG_ARCH_TYPE_MASK	U(0x1)
#define SPCI_MEM_REG_ARCH_TYPE_RX	U(0)
#define SPCI_MEM_REG_ARCH_TYPE_TX	U(1)
#define SPCI_MEM_REG_ARCH_TYPE(type)	(((type) & SPCI_MEM_REG_ARCH_TYPE_MASK)	\
					 << SPCI_MEM_REG_ARCH_TYPE_SHIFT)

/* Security state of architectural memory region: Secure or Non-secure */
#define SPCI_MEM_REG_ARCH_SEC_SHIFT	U(1)
#define SPCI_MEM_REG_ARCH_SEC_MASK	U(0x1)
#define SPCI_MEM_REG_ARCH_SEC_S 	U(0)
#define SPCI_MEM_REG_ARCH_SEC_NS	U(1)
#define SPCI_MEM_REG_ARCH_SEC(sec)	(((sec) & SPCI_MEM_REG_ARCH_SEC_MASK)	\
					 << SPCI_MEM_REG_ARCH_SEC_SHIFT)

/* Granularity of allocation and alignment of arch. memory region  */
#define SPCI_MEM_REG_ARCH_GRAN_SHIFT	U(2)
#define SPCI_MEM_REG_ARCH_GRAN_MASK	U(0x3)
#define SPCI_MEM_REG_ARCH_GRAN_4K	U(0)
#define SPCI_MEM_REG_ARCH_GRAN_16K	U(1)
#define SPCI_MEM_REG_ARCH_GRAN_64K	U(2)
#define SPCI_MEM_REG_ARCH_GRAN_RES	U(3)
#define SPCI_MEM_REG_ARCH_GRAN(gran)	(((gran) & SPCI_MEM_REG_ARCH_GRAN_MASK)	\
					 << SPCI_MEM_REG_ARCH_GRAN_SHIFT))

/* Macro to define attributes of an architectural memory region */
#define SPCI_MEM_REG_ARCH(type, sec, gran)			\
	(SPCI_MEM_REG_TYPE(SPCI_MEM_REG_TYPE_ARCH)	|	\
	 SPCI_MEM_REG_ARCH_TYPE(type) 			|	\
	 SPCI_MEM_REG_ARCH_SEC(sec) 			|	\
	 SPCI_MEM_REG_ARCH_GRAN(gran))

/* Transparent value in imp. def. memory region */
#define SPCI_MEM_REG_IMP_VAL_SHIFT	U(0)
#define SPCI_MEM_REG_IMP_VAL_MASK	U(0xffff)
#define SPCI_MEM_REG_IMP_VAL(val)	(((val) & SPCI_MEM_REG_IMP_VAL_MASK) \
					 << SPCI_MEM_REG_IMP_VAL_SHIFT)

/* Macro to encode value of an imp. def. memory region */
#define SPCI_MEM_REG_IMP(val)					\
	(SPCI_MEM_REG_TYPE(SPCI_MEM_REG_TYPE_IMP)	|	\
	 SPCI_MEM_REG_IMP_VAL(val))

typedef struct spci_mem_region_desc {
	uint64_t		address;
	uint16_t		page_count;
        uint8_t                 reserved[2];
	uint32_t		attributes;
} spci_mem_reg_desc_t;

/* Encoding of version field in common message header */
#define SPCI_MSG_VER_MAJ_SHIFT	U(8)
#define SPCI_MSG_VER_MAJ_MASK	U(0xff)
#define SPCI_MSG_VER_MAJ	U(1)

#define SPCI_MSG_VER_MIN_SHIFT	U(0)
#define SPCI_MSG_VER_MIN_MASK	U(0xff)
#define SPCI_MSG_VER_MIN	U(0)

#define SPCI_MSG_VER(maj, min)	((((maj) & SPCI_MSG_VER_MAJ_MASK)	\
				  << SPCI_MSG_VER_MAJ_SHIFT) |		\
				 (((min) & SPCI_MSG_VER_MIN_MASK)	\
				  << SPCI_MSG_VER_MIN_SHIFT))

/* Encoding of message type bit in flags field in common message header */
#define SPCI_MSG_TYPE_SHIFT	U(0)
#define SPCI_MSG_TYPE_MASK	U(0x1)
#define SPCI_MSG_TYPE_ARCH	U(0)
#define SPCI_MSG_TYPE_IMP	U(1)
#define SPCI_MSG_TYPE(type)	(((type) & SPCI_MSG_TYPE_MASK) \
				 << SPCI_MSG_TYPE_SHIFT)

/* Encoding of source vcpu valid bit in flags field in common message header */
#define SPCI_MSG_SRC_VCPU_VALID_SHIFT	U(1)
#define SPCI_MSG_SRC_VCPU_VALID_MASK	U(0x1)
#define SPCI_MSG_SRC_VCPU_VALID		U(1)
#define SPCI_MSG_SRC_VCPU_INVALID	U(0)

typedef struct spci_msg_hdr {
	uint16_t		version;
	uint16_t		flags;
	uint32_t		length;
	uint16_t		target_sp;
	uint16_t		target_sp_vcpu;
	uint16_t		source_sp;
	uint16_t		source_sp_vcpu;
	uint64_t		payload[];
} spci_msg_hdr_t;

/* Architectural message types */
#define SPCI_ARCH_MSG_TYPE_SHIFT	U(0)
#define SPCI_ARCH_MSG_TYPE_MASK		U(0xf)
#define SPCI_ARCH_MSG_TYPE_SHM_REQ	U(0)
#define SPCI_ARCH_MSG_TYPE_SHM_RESP	U(1)
#define SPCI_ARCH_MSG_TYPE_USHM_REQ	U(2)
#define SPCI_ARCH_MSG_TYPE_USHM_RESP	U(3)
#define SPCI_ARCH_MSG_TYPE_SEND_NOT	U(4)
#define SPCI_ARCH_MSG_TYPE_SP_INFO_REQ	U(5)
#define SPCI_ARCH_MSG_TYPE_SP_INFO_RESP	U(6)
#define SPCI_ARCH_MSG_TYPE_SP_INIT	U(7)
#define SPCI_ARCH_MSG_TYPE(type)	(((type) & SPCI_ARCH_MSG_TYPE_MASK) \
					 << SPCI_ARCH_MSG_TYPE_SHIFT)

typedef struct spci_arch_msg_hdr {
	uint16_t		type;
	uint8_t                 reserved[6];
	uint64_t		payload[];
} spci_arch_msg_hdr_t __aligned(8);

/* Encoding of version field in common message header */
#define INIT_MSG_VER_MAJ_SHIFT	U(8)
#define INIT_MSG_VER_MAJ_MASK	U(0xff)
#define INIT_MSG_VER_MAJ	U(1)

#define INIT_MSG_VER_MIN_SHIFT	U(0)
#define INIT_MSG_VER_MIN_MASK	U(0xff)
#define INIT_MSG_VER_MIN	U(0)

#define INIT_MSG_VER(maj, min)	((((maj) & INIT_MSG_VER_MAJ_MASK)	\
				  << INIT_MSG_VER_MAJ_SHIFT) |		\
				 (((min) & INIT_MSG_VER_MIN_MASK)	\
				  << INIT_MSG_VER_MIN_SHIFT))

typedef struct spci_msg_sp_init {
	uint16_t                version;
	uint16_t                mem_reg_count;
	uint8_t                 reserved[4];
	spci_mem_reg_desc_t	mem_regs[];
} spci_msg_sp_init_t;

#endif /* __ASSEMBLER__ */
#endif /* SPCI_H */

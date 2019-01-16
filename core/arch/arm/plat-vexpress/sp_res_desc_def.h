/*
 * Copyright (c) 2018, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SPM_RES_DESC_DEFS_H
#define SPM_RES_DESC_DEFS_H

/*******************************************************************************
 * Attribute Section
 ******************************************************************************/

#define RD_ATTR_TYPE_UP_MIGRATABLE	UINT32_C(0)
#define RD_ATTR_TYPE_UP_PINNED		UINT32_C(2)
#define RD_ATTR_TYPE_MP			UINT32_C(1)

#define RD_ATTR_RUNTIME_SEL0		UINT32_C(0)
#define RD_ATTR_RUNTIME_SEL1		UINT32_C(1)

#define RD_ATTR_INIT_ONLY		UINT32_C(0)
#define RD_ATTR_RUNTIME			UINT32_C(1)

#define RD_ATTR_PANIC_RESTART		UINT32_C(0)
#define RD_ATTR_PANIC_ONESHOT		UINT32_C(1)

#define RD_ATTR_XLAT_GRANULE_4KB	UINT32_C(0)
#define RD_ATTR_XLAT_GRANULE_16KB	UINT32_C(1)
#define RD_ATTR_XLAT_GRANULE_64KB	UINT32_C(2)

/*******************************************************************************
 * Memory Region Section
 ******************************************************************************/

#define RD_MEM_REGION_NAME_LEN		UINT32_C(32)

#define RD_MEM_DEVICE			UINT32_C(0)
#define RD_MEM_NORMAL_CODE		UINT32_C(1)
#define RD_MEM_NORMAL_DATA		UINT32_C(2)
#define RD_MEM_NORMAL_BSS		UINT32_C(3)
#define RD_MEM_NORMAL_RODATA		UINT32_C(4)
#define RD_MEM_NORMAL_SPM_SP_SHARED_MEM	UINT32_C(5)
#define RD_MEM_NORMAL_CLIENT_SHARED_MEM	UINT32_C(6)
#define RD_MEM_NORMAL_MISCELLANEOUS	UINT32_C(7)

#define RD_MEM_MASK			UINT32_C(15)

#define RD_MEM_IS_PIE			(UINT32_C(1) << 4)

/*******************************************************************************
 * Notification Section
 ******************************************************************************/

#define RD_NOTIF_TYPE_PLATFORM		(UINT32_C(0) << 31)
#define RD_NOTIF_TYPE_INTERRUPT		(UINT32_C(1) << 31)

#define RD_NOTIF_PLAT_ID_MASK		UINT32_C(0xFFFF)
#define RD_NOTIF_PLAT_ID_SHIFT		UINT32_C(0)

#define RD_NOTIF_PLATFORM(id)						\
	(RD_NOTIF_TYPE_PLATFORM						\
	| (((id) & RD_NOTIF_PLAT_ID_MASK) << RD_NOTIF_PLAT_ID_SHIFT))

#define RD_NOTIF_IRQ_NUM_MASK		UINT32_C(0xFFFF)
#define RD_NOTIF_IRQ_NUM_SHIFT		UINT32_C(0)
#define RD_NOTIF_IRQ_PRIO_MASK		UINT32_C(0xFF)
#define RD_NOTIF_IRQ_PRIO_SHIFT		UINT32_C(16)

#define RD_NOTIF_IRQ_EDGE_FALLING	UINT32_C(0)
#define RD_NOTIF_IRQ_EDGE_RISING	UINT32_C(2)
#define RD_NOTIF_IRQ_LEVEL_LOW		UINT32_C(1)
#define RD_NOTIF_IRQ_LEVEL_HIGH		UINT32_C(3)
#define RD_NOTIF_IRQ_TRIGGER_SHIFT	UINT32_C(24)

#define RD_NOTIF_IRQ(num, prio, trig)					\
	(RD_NOTIF_TYPE_IRQ						\
	| (((num) & RD_NOTIF_IRQ_NUM_MASK) << RD_NOTIF_IRQ_NUM_SHIFT)	\
	| (((prio) & RD_NOTIF_IRQ_PRIO_MASK) << RD_NOTIF_IRQ_PRIO_SHIFT) \
	| (((trig) << RD_NOTIF_IRQ_TRIGGER_SHIFT)))

/*******************************************************************************
 * Service Description Section
 ******************************************************************************/

#define RD_SERV_ACCESS_SECURE		(UINT32_C(1) << 0)
#define RD_SERV_ACCESS_EL3		(UINT32_C(1) << 1)
#define RD_SERV_ACCESS_NORMAL		(UINT32_C(1) << 2)

#define RD_SERV_SUPPORT_BLOCKING	(UINT32_C(1) << 0)
#define RD_SERV_SUPPORT_NON_BLOCKING	(UINT32_C(1) << 0)

#endif /* SPM_RES_DESC_DEFS_H */

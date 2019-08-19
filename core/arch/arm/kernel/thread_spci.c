/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2018-2019, Arm Limited. All rights reserved.
 */

#include <assert.h>
#include <mm/core_memprot.h>
#include <kernel/panic.h>
#include <kernel/thread.h>
#include <sm/optee_smc.h>
#include <spci_private.h>
#include <string.h>

/* One buffer for each security state */
#define SPCI_MAX_BUFS		2
#define SPCI_MAX_SEC_STATES	2
static struct spci_msg_buf_desc buf_desc[SPCI_MAX_SEC_STATES][SPCI_MAX_BUFS];

/* Special message with initialisation information */
static struct spci_msg_sp_init *sp_init_msg;

struct spci_msg_sp_init *spci_get_msg_sp_init(void)
{
	assert(sp_init_msg);
	return sp_init_msg;
}

void spci_early_init(struct spci_buf *spci_rx_buf)
{
	struct spci_arch_msg_hdr *arch_msg_hdr = NULL;
	struct spci_msg_hdr *msg_hdr = NULL;
	unsigned int ctr = 0;

	assert(memcmp(&spci_rx_buf->hdr.signature, SPCI_BUF_SIGNATURE,
		      sizeof(SPCI_BUF_SIGNATURE)) == 0);

	/* Get the common message header */
	msg_hdr = (void *) spci_rx_buf->buf;

	/* Get the arch message header */
	arch_msg_hdr = (void *)msg_hdr->payload;

	/* Get the arch. initialisation message */
	sp_init_msg = (void *)arch_msg_hdr->payload;

	/* Populate private record of SPCI RX/TX buffers */
	for (ctr = 0; ctr < sp_init_msg->mem_reg_count; ctr++) {
		struct spci_mem_region_desc *mem_reg = NULL;
		struct spci_msg_buf_desc *bdesc = NULL;
		uint32_t type = 0;
		uint32_t sec = 0;
		uint32_t attr = 0;

		mem_reg = &sp_init_msg->mem_regs[ctr];

		/* Find the type of message */
		attr = mem_reg->attributes;
		type = attr >> SPCI_MEM_REG_TYPE_SHIFT;
		type &= SPCI_MEM_REG_TYPE_MASK;

		/* Ignore imp. def. messages */
		if (type != SPCI_MEM_REG_TYPE_ARCH)
			continue;

		/* Check if secure or non-secure RX or TX buffer */
		type = attr >> SPCI_MEM_REG_ARCH_TYPE_SHIFT;
		type &= SPCI_MEM_REG_ARCH_TYPE_MASK;
		sec = attr >> SPCI_MEM_REG_ARCH_SEC_SHIFT;
		sec &= SPCI_MEM_REG_ARCH_SEC_MASK;

		/* Obtain a reference to the buffer descriptor */
		bdesc = &buf_desc[sec][type];

		/* Store available buffer information apart from buffer VA */
		bdesc->pa = mem_reg->address;
		bdesc->page_count = mem_reg->page_count;
		bdesc->attributes = attr;
	}
}

static void print_buf_desc(struct spci_msg_buf_desc *mbd)
{
	struct spci_buf *buf = (struct spci_buf *)mbd->va;

	DMSG("Buf pa: 0x%"PRIxPA, mbd->pa);
	DMSG("Buf va: 0x%"PRIxVA, mbd->va);
	DMSG("Buf pg: 0x%x", mbd->page_count);
	DMSG("Buf at: 0x%"PRIx32, mbd->attributes);
	DMSG("Buf sg: %s", buf->hdr.signature);
	DMSG("Buf st: %s", buf->hdr.state == SPCI_BUF_STATE_EMPTY ?
	     "empty" : "full");

}

/* Populate the VA of message buffers and set their state for future use */
void spci_late_init(void)
{
	unsigned int ctr0 = 0;
	unsigned int ctr1 = 0;

	for (ctr0 = 0; ctr0 < SPCI_MAX_SEC_STATES; ctr0++) {
		for (ctr1 = 0; ctr1 < SPCI_MAX_BUFS; ctr1++) {
			struct spci_msg_buf_desc *mbd = &buf_desc[ctr0][ctr1];
			struct spci_buf *buf = NULL;
			enum teecore_memtypes memtype = 0;

			/* SPM did not describe this buffer */
			if (!mbd->pa)
				continue;

			/* Secure or non-secure */
			if (ctr0 == 0)
				memtype = MEM_AREA_SPCI_SEC_SHM;
			else
				memtype = MEM_AREA_SPCI_NSEC_SHM;
			mbd->va = (vaddr_t)phys_to_virt(mbd->pa, memtype);

			/* Set the buffer state as empty */
			buf = (struct spci_buf *)mbd->va;
			buf->hdr.state = SPCI_BUF_STATE_EMPTY;

			print_buf_desc(mbd);
		}
	}
}

/* Only called from assembly */
uint32_t spci_msg_recv(int32_t status);
uint32_t __noreturn spci_msg_recv(int32_t status __unused)
{
	panic("Not implemented yet!");
}

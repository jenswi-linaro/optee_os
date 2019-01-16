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
static spci_msg_buf_desc_t buf_desc[SPCI_MAX_SEC_STATES][SPCI_MAX_BUFS];

/* Special message with initialisation information */
static spci_msg_sp_init_t *sp_init_msg;

uintptr_t spci_arch_msg_get(uint16_t msg_type)
{
	assert(msg_type == SPCI_ARCH_MSG_TYPE_SP_INIT);
	assert(sp_init_msg != NULL);
	return (uintptr_t) sp_init_msg;
}

void spci_early_init(spci_buf_t *spci_rx_buf)
{
	void *tmp;
	spci_msg_hdr_t *msg_hdr;
	spci_arch_msg_hdr_t *arch_msg_hdr;
	unsigned int ctr;

	assert (memcmp(&spci_rx_buf->hdr.signature,
		       SPCI_BUF_SIGNATURE,
		       sizeof(SPCI_BUF_SIGNATURE)) == 0);

	/* Get the common message header */
	tmp = (void *) spci_rx_buf->buf;
	msg_hdr = (spci_msg_hdr_t *) tmp;

	/* Get the arch message header */
	tmp = (void *) msg_hdr->payload;
	arch_msg_hdr = (spci_arch_msg_hdr_t *) tmp;

	/* Get the arch. initialisation message */
	tmp = (void *) arch_msg_hdr->payload;
	sp_init_msg = (spci_msg_sp_init_t *) tmp;

	/* Populate private record of SPCI RX/TX buffers */
	for (ctr = 0; ctr < sp_init_msg->mem_reg_count; ctr++) {
		spci_mem_reg_desc_t *mem_reg;
		spci_msg_buf_desc_t *bdesc;
		uint32_t type, sec, attr;

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

	return;
}

/* Populate the VA of message buffers and set their state for future use */
void spci_late_init(void)
{
	unsigned int ctr0, ctr1;

	for (ctr0 = 0; ctr0 < SPCI_MAX_SEC_STATES; ctr0++)
		for (ctr1 = 0; ctr1 < SPCI_MAX_BUFS; ctr1++) {
			paddr_t pa = buf_desc[ctr0][ctr1].pa;
			vaddr_t *va = &buf_desc[ctr0][ctr1].va;
			spci_buf_t *buf;

			/* SPM did not describe this buffer */
			if (!pa)
				continue;

			/* Secure or non-secure */
			if (ctr0 == 0)
				*va = (vaddr_t) phys_to_virt((paddr_t) pa,
							     MEM_AREA_SPCI_SEC_SHM);
			else
				*va = (vaddr_t) phys_to_virt((paddr_t) pa,
							     MEM_AREA_SPCI_NSEC_SHM);

			/* Set the buffer state as empty */
			buf = (spci_buf_t *) va;
			buf->hdr.state = SPCI_BUF_STATE_EMPTY;

			DMSG("Buf pa: 0x%lx \n", pa);
			DMSG("Buf va: 0x%lx \n", *va);
			DMSG("Buf pg: 0x%lx \n", buf_desc[ctr0][ctr1].page_count);
			DMSG("Buf at: 0x%x \n", buf_desc[ctr0][ctr1].attributes);
			DMSG("Buf sg: %s   \n", &buf->hdr.signature[0]);
			DMSG("Buf st: %s   \n",
			     (buf->hdr.state == SPCI_BUF_STATE_EMPTY) ?
			     "empty" : "full");
		}

	return;
}

void spci_msg_put(unsigned int sec_state)
{
	spci_buf_t *buf_base;

	assert ((sec_state == SPCI_MEM_REG_ARCH_SEC_S) ||
		(sec_state == SPCI_MEM_REG_ARCH_SEC_NS));

	buf_base = (spci_buf_t *)
		buf_desc[sec_state][SPCI_MEM_REG_ARCH_TYPE_RX].va;

	if (!buf_base)
		panic();

	buf_base->hdr.state = SPCI_BUF_STATE_EMPTY;
	return;
}

void *spci_msg_recv(int32_t status, struct thread_smc_args *args)
{
	uint32_t msg_loc, msg_type;
	spci_msg_hdr_t *msg_hdr;
	spci_optee_msg_t *optee_msg;
	spci_msg_buf_desc_t *rx_buf_desc;
	spci_buf_t *rx_buf;
	void *tmp;

	/* Check status */
	if (status == SPCI_INVALID_PARAMETER)
		panic();

	/* Panic for now */
	if (status == SPCI_RETRY)
		panic();

	/*
	 * There is an FIQ to handle. There is no message. Jump to
	 * the FIQ handler directly.
	 */
	if (status == SPCI_INTERRUPTED) {
		DMSG("Received FIQ\n");
		return &thread_vector_table.fiq_entry;
	}

	/* Get a reference to the RX buffer */
	msg_loc = status >> SPCI_MSG_RECV_MSGLOC_SHIFT;
	msg_loc &= SPCI_MSG_RECV_MSGLOC_MASK;
	rx_buf_desc = &buf_desc[msg_loc][SPCI_MEM_REG_ARCH_TYPE_RX];
	rx_buf = (spci_buf_t *) rx_buf_desc->va;

	/*
	 * Get the common message header.
	 * TODO: Assuming there is a single sender and receiver. Hence, sender
	 * and receiver information is not parsed.
	 */
	tmp = (void *) rx_buf->buf;
	msg_hdr = (spci_msg_hdr_t *) tmp;

	/* Get the message payload */
	msg_type = msg_hdr->flags >> SPCI_MSG_TYPE_SHIFT;
	msg_type &= SPCI_MSG_TYPE_MASK;

	/*
	 * Not expecting any architectural messages for now.
	 * TODO: Add arch. messages for power management functions.
	 */
	if (msg_type == SPCI_MSG_TYPE_ARCH)
		panic();

	/* Check if message length is same as OP-TEE message length */
	assert(msg_hdr->length == sizeof(*optee_msg));

	/* Obtain reference to OP-TEE message */
	tmp = (void *) msg_hdr->payload;
	optee_msg = (spci_optee_msg_t *) tmp;

	/* Copy the message */
	memcpy(args, optee_msg, sizeof(*optee_msg));

	/* Zero the message memory */
	memset((void *) msg_hdr, 0, sizeof(*msg_hdr) + msg_hdr->length);

	/* Release the copied message */
	rx_buf->hdr.state = SPCI_BUF_STATE_EMPTY;

	if (OPTEE_SMC_IS_FAST_CALL(args->a0))
		return &thread_vector_table.fast_smc_entry;
	else
		return &thread_vector_table.std_smc_entry;
}


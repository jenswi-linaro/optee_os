/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2019, Arm Limited
 */

#include <platform_config.h>

#include <arm64_macros.S>
#include <arm.h>
#include <asm.S>
#include <keep.h>
#include <generated/asm-defines.h>
#include <spci.h>

/* void spci_msg_recv_invoke(void); */
FUNC spci_msg_recv_invoke , :
	/* Block on MSG_RECV */
	mov	x1, #(SPCI_MSG_RECV_ATTRS_BLK & SPCI_MSG_RECV_ATTRS_MASK)
	lsl	x1, x1, #SPCI_MSG_RECV_ATTRS_SHIFT
	mov_imm	x0, SPCI_MSG_RECV
	b	.spci_msg_loop
END_FUNC spci_msg_recv_invoke

FUNC spci_msg_send_recv_invoke , :
	/* Move Attributes to x1 */
	mov	x1, x0
	mov_imm	x0, SPCI_MSG_SEND_RECV
.spci_msg_loop:
	/* Invoke SMC with caller provided parameters */
	mov	x7, xzr		/* TODO: Source and target ids are 0 */
	smc	#0

	cmp	w0, #SPCI_INTERRUPTED
	bne	1f
	bl	itr_core_handler
	b	spci_msg_recv_invoke

1:
	/*
	 * Message receive has completed. Parse message in RX and deliver
	 * it to a handler function. Once we're returning a reply has been
	 * posted in TX and with the attributes in x0.
	 */
	bl	spci_msg_recv
	/* Note that x0 now holds the attribute */
	b	spci_msg_send_recv_invoke
END_FUNC spci_msg_send_recv_invoke


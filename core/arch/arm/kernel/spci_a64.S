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

FUNC spci_msg_recv_invoke , :
	/* Block on MSG_RECV */
	mov	x1, #(SPCI_MSG_RECV_ATTRS_BLK & SPCI_MSG_RECV_ATTRS_MASK)
	lsl	x1, x1, #SPCI_MSG_RECV_ATTRS_SHIFT
	mov_imm	x0, SPCI_MSG_RECV
	b	spci_msg_loop
END_FUNC spci_msg_recv_invoke

FUNC spci_msg_send_recv_invoke , :
	/* Move Attributes to x1 */
	mov	x1, x0
	mov_imm	x0, SPCI_MSG_SEND_RECV
END_FUNC spci_msg_send_recv_invoke

FUNC spci_msg_loop , :
	/* Invoke SMC with caller provided parameters */
	mov	x7, xzr		/* TODO: Source and target ids are 0 */
	smc	#0

	/*
	 * Make space for parameters and function pointer to handle this
	 * message
	 */
	sub	sp, sp, #THREAD_SMC_ARGS_SIZE
	store_xregs sp, THREAD_SMC_ARGS_X0, 0, 7
	mov	x1, sp

	/*
	 * Message receive has completed. Parse message in RX and decide next
	 * action
	 */
	bl	spci_msg_recv

	/*
         * TODO: x8 can be used since without S-EL2, OP-TEE will always exit
	 * through a SMC in which case this is a scratch register
	 */
	mov	x8, x0
	load_xregs sp, THREAD_SMC_ARGS_X0, 0, 7
	add	sp, sp, #THREAD_SMC_ARGS_SIZE
	br	x8
END_FUNC spci_msg_loop

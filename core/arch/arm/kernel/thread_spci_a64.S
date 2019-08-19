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
#include <kernel/thread_defs.h>

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

FUNC thread_std_smc_entry , :
	bl	__thread_std_smc_entry
	mov	w20, w0	/* Save return value for later */

	/* Mask all maskable exceptions before switching to temporary stack */
	msr	daifset, #DAIFBIT_ALL
	bl	thread_get_tmp_sp
	mov	sp, x0

	bl	thread_state_free

	mov	w0, w20
	b	spci_msg_send_recv_invoke
END_FUNC thread_std_smc_entry

/* unsigned long thread_rpc(void *msg, unsigned long len); */
FUNC thread_rpc , :
	/* Read daif and create an SPSR */
	mrs	x2, daif
	orr	x2, x2, #(SPSR_64_MODE_EL1 << SPSR_64_MODE_EL_SHIFT)
	/* x2 = SPSR */

	/* Mask all maskable exceptions before switching to temporary stack */
	msr	daifset, #DAIFBIT_ALL
	push	x0, x1
	push	x2, x30
	bl	thread_get_ctx_regs
	ldr	x30, [sp, #8]
	store_xregs x0, THREAD_CTX_REGS_X19, 19, 30
	mov	x19, x0		/* x19 = ctx_regs */

	bl	thread_get_tmp_sp
	pop	x20, xzr	/* Match "push x2, x30" above, x20 = SPSR */
	mov	x2, sp
	str	x2, [x19, #THREAD_CTX_REGS_SP]
	ldp	x21, x22, [sp]	/* x21 = msg, x22 = len */
	mov	sp, x0		/* Switch to tmp stack */

	mov	x0, x21
	mov	x1, x22
	bl	spci_msg_send_prepare
	mov	w23, w0		/* x23 = attr */

	mov	w0, #THREAD_FLAGS_COPY_ARGS_ON_RETURN
	mov	w1, w20
	adr	x2, .thread_rpc_return
	bl	thread_state_suspend
	mov	w0, w23
	b	spci_msg_send_recv_invoke

.thread_rpc_return:
	/*
	 * At this point has the stack pointer been restored to the value
	 * stored in THREAD_CTX above.
	 *
	 * Jumps here from thread_resume above when RPC has returned. The
	 * IRQ and FIQ bits are restored to what they where when this
	 * function was originally entered.
	 */
	pop	x3, x4	/* Match "push x0, x1" above, x3 = msg, x4 = len */
	b	thread_rpc_return_fixup
END_FUNC thread_rpc
KEEP_PAGER thread_rpc

/*
 * void thread_foreign_intr_exit(uint32_t thread_index)
 *
 * This function is jumped to at the end of macro foreign_intr_handler().
 * The current thread as indicated by @thread_index has just been
 * suspended.  The job here is just to inform normal world the thread id to
 * resume when returning.
 */
FUNC thread_foreign_intr_exit , :
	bl	spci_msg_send_prepare_foreign_intr
	b	spci_msg_send_recv_invoke
END_FUNC thread_foreign_intr_exit

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
#include <optee_spci.h>

FUNC spci_msg_wait_invoke , :
	mov_imm	x0, SPCI_MSG_WAIT		/* FID */
	mov	x1, #SPCI_TARGET_INFO_MBZ	/* Target info MBZ */
	mov	x2, #SPCI_PARAM_MBZ		/* Param MBZ */
	mov	x3, #SPCI_PARAM_MBZ		/* Param MBZ */
	mov	x4, #SPCI_PARAM_MBZ		/* Param MBZ */
	mov	x5, #SPCI_PARAM_MBZ		/* Param MBZ */
	mov	x6, #SPCI_PARAM_MBZ		/* Param MBZ */
	mov	x7, #SPCI_PARAM_MBZ		/* Param MBZ */
	b	.spci_msg_loop
END_FUNC spci_msg_wait_invoke

	/* Caller provides x1, x3-x7 params */
FUNC spci_msg_send_direct_resp_invoke , :
	mov_imm	x0, SPCI_MSG_SEND_DIRECT_RESP_SMC32	/* FID */
	mov	x2, #SPCI_PARAM_MBZ			/* RES MBZ */

.spci_msg_loop:
	/* Invoke SMC with caller provided parameters */
	smc	#0

	/*
	 * Make space for parameters and function pointer to handle this
	 * message
	 */
	sub	sp, sp, #THREAD_SMC_ARGS_SIZE
	store_xregs sp, THREAD_SMC_ARGS_X0, 0, 7
	mov	x0, sp

	/*
	 * Message receive has completed. Parse message in RX and decide next
	 * action
	 */
	bl	spmc_msg_recv

	load_xregs sp, THREAD_SMC_ARGS_X0, 0, 7
	add	sp, sp, #THREAD_SMC_ARGS_SIZE
	b	.spci_msg_loop
END_FUNC spci_msg_send_direct_resp_invoke

FUNC thread_std_smc_entry , :
	bl	__thread_std_smc_entry
	mov	w20, w0	/* Save return value for later */

	/* Mask all maskable exceptions before switching to temporary stack */
	msr	daifset, #DAIFBIT_ALL
	bl	thread_get_tmp_sp
	mov	sp, x0

	bl	thread_state_free

	mov	x1, #SPCI_PARAM_MBZ		/* TODO: Target info is MBZ  */
	mov	w3, w20				/* Return value */
	mov	x4, #0				/* Unused parameter */
	mov	x5, #0				/* Unused parameter */
	mov	x6, #0				/* Unused parameter */
	mov	x7, #0				/* Unused parameter */
	b	spci_msg_send_direct_resp_invoke
END_FUNC thread_std_smc_entry

/* void thread_rpc(struct thread_rpc_arg *rpc_arg) */
FUNC thread_rpc , :
	/* Read daif and create an SPSR */
	mrs	x1, daif
	orr	x1, x1, #(SPSR_64_MODE_EL1 << SPSR_64_MODE_EL_SHIFT)

	/* Mask all maskable exceptions before switching to temporary stack */
	msr	daifset, #DAIFBIT_ALL
	push	x0, xzr
	push	x1, x30
	bl	thread_get_ctx_regs
	ldr	x30, [sp, #8]
	store_xregs x0, THREAD_CTX_REGS_X19, 19, 30
	mov	x19, x0

	bl	thread_get_tmp_sp
	pop	x1, xzr		/* Match "push x1, x30" above */
	mov	x2, sp
	str	x2, [x19, #THREAD_CTX_REGS_SP]
	ldr	x20, [sp]	/* Get pointer to rpc_arg[] */
	mov	sp, x0		/* Switch to tmp stack */
	/*
	 * We need to read rpc_arg[] early, because thread_state_suspend
	 * can invoke virt_unset_guest() which will unmap pages,
	 * where rpc_arg[] resides
	 */
	load_wregs x20, 0, 21, 24	/* Load rpc_arg[] into w21-w24 */

	adr	x2, .thread_rpc_return
	mov	w0, #THREAD_FLAGS_COPY_ARGS_ON_RETURN
	bl	thread_state_suspend
	mov	w5, w0		/* Supply thread index */
	ldr	w0, =SPCI_MSG_SEND_DIRECT_RESP_SMC32
	mov	w1, w21
	mov	w2, #SPCI_PARAM_MBZ
	mov	w3, #0		/* Error code = 0 */
	mov	w4, w22
	mov	w6, w23
	mov	w7, w24
	b	spci_msg_send_direct_resp_invoke

.thread_rpc_return:
	/*
	 * At this point has the stack pointer been restored to the value
	 * stored in THREAD_CTX above.
	 *
	 * Jumps here from thread_resume above when RPC has returned. The
	 * IRQ and FIQ bits are restored to what they where when this
	 * function was originally entered. w0-w3 holds the values supplied
	 * to thread_resume_from_rpc() in a0-a3.
	 */
	pop	x16, xzr	/* Get pointer to rv[] */
	store_wregs x16, 0, 0, 3	/* Store w0-w3 into rv[] */
	ret
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
	mov	x1, #0		/* TODO dst */
	mov	x2, #SPCI_PARAM_MBZ
	mov	w3, #0
	mov	w4, #OPTEE_SPCI_YIELDING_CALL_RETURN_INTERRUPT
	mov	w5, w0
	mov	w6, #0
	mov	x7, #SPCI_PARAM_MBZ
	b	spci_msg_send_direct_resp_invoke
END_FUNC thread_foreign_intr_exit

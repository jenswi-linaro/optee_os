/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <string.h>

#include <plat.h>

#include <drivers/gic.h>
#include <drivers/uart.h>
#include <sm/sm.h>
#include <sm/sm_defs.h>
#include <sm/tee_mon.h>
#include <sm/teesmc.h>
#include <sm/teesmc_st.h>

#include <kernel/kernel.h>
#include <kernel/arch_debug.h>

#include <arm32.h>
#include <kernel/thread.h>
#include <kernel/panic.h>
#include <kernel/tee_core_trace.h>
#include <kernel/misc.h>
#include <mm/tee_pager_unpg.h>
#include <mm/core_mmu.h>
#include <tee/entry.h>

#include <assert.h>

uint32_t cpu_on_handler(uint32_t a0, uint32_t a1);

#ifdef WITH_STACK_CANARIES
#define STACK_CANARY_SIZE	(4 * sizeof(uint32_t))
#define START_CANARY_VALUE	0xdededede
#define END_CANARY_VALUE	0xabababab
#define GET_START_CANARY(name, stack_num) name[stack_num][0]
#define GET_END_CANARY(name, stack_num) \
	name[stack_num][sizeof(name[stack_num]) / sizeof(uint32_t) - 1]
#else
#define STACK_CANARY_SIZE	0
#endif

#define DECLARE_STACK(name, num_stacks, stack_size) \
	static uint32_t name[num_stacks][(stack_size + STACK_CANARY_SIZE) / \
					 sizeof(uint32_t)] \
		__attribute__((section(".bss.prebss.stack"), \
			       aligned(STACK_ALIGNMENT)))

#define GET_STACK(stack) \
	((vaddr_t)(stack) + sizeof(stack) - STACK_CANARY_SIZE / 2)


DECLARE_STACK(stack_tmp,	CFG_TEE_CORE_NB_CORE,	STACK_TMP_SIZE);
DECLARE_STACK(stack_abt,	CFG_TEE_CORE_NB_CORE,	STACK_ABT_SIZE);
DECLARE_STACK(stack_sm,		CFG_TEE_CORE_NB_CORE,	SM_STACK_SIZE);
DECLARE_STACK(stack_thread,	NUM_THREADS,		STACK_THREAD_SIZE);

const vaddr_t stack_tmp_top[CFG_TEE_CORE_NB_CORE] = {
	GET_STACK(stack_tmp[0]),
#if CFG_TEE_CORE_NB_CORE > 1
	GET_STACK(stack_tmp[1]),
#endif
#if CFG_TEE_CORE_NB_CORE > 2
	GET_STACK(stack_tmp[2]),
#endif
#if CFG_TEE_CORE_NB_CORE > 3
	GET_STACK(stack_tmp[3]),
#endif
#if CFG_TEE_CORE_NB_CORE > 4
#error "Top of tmp stacks aren't defined for more than 4 CPUS"
#endif
};

/* MMU L1 table for teecore: 16kB */
#define MMU_L1_NUM_ENTRIES	(16 * 1024 / 4)
#define MMU_L1_ALIGNMENT	(1 << 14)	/* 16 KiB aligned */
uint32_t SEC_MMU_TTB_FLD[MMU_L1_NUM_ENTRIES]
        __attribute__((section(".bss.prebss.mmu"), aligned(MMU_L1_ALIGNMENT)));

/* MMU L2 table for teecore: 16 * 1kB (16MB mappeable) */
#define MMU_L2_NUM_ENTRIES	(16 * 1024 / 4)
#define MMU_L2_ALIGNMENT	(1 << 14)	/* 16 KiB aligned */
uint32_t SEC_MMU_TTB_SLD[MMU_L2_NUM_ENTRIES]
        __attribute__((section(".bss.prebss.mmu"), aligned(MMU_L2_ALIGNMENT)));

/* MMU L1 table for TAs: 16kB */
#define MMU_L1_NUM_ENTRIES	(16 * 1024 / 4)
#define MMU_L1_ALIGNMENT	(1 << 14)	/* 16 KiB aligned */
uint32_t SEC_TA_MMU_TTB_FLD[MMU_L1_NUM_ENTRIES]
        __attribute__((section(".bss.prebss.mmu"), aligned(MMU_L1_ALIGNMENT)));

/* MMU L2 table for TAs: 16 * 1kB (16MB mappeable) */
#define MMU_L2_NUM_ENTRIES	(16 * 1024 / 4)
#define MMU_L2_ALIGNMENT	(1 << 14)	/* 16 KiB aligned */
uint32_t SEC_TA_MMU_TTB_SLD[MMU_L2_NUM_ENTRIES]
        __attribute__((section(".bss.prebss.mmu"), aligned(MMU_L2_ALIGNMENT)));




extern uint32_t __text_start;
extern uint32_t __rodata_end;
extern uint32_t __data_start;
extern uint32_t __bss_start;
extern uint32_t __bss_end;
extern uint32_t _end;
extern uint32_t _end_of_ram;

static void main_fiq(void);
static void main_tee_entry(struct thread_smc_args *args);
static uint32_t main_cpu_off_handler(uint32_t a0, uint32_t a1);
static uint32_t main_cpu_suspend_handler(uint32_t a0, uint32_t a1);
static uint32_t main_cpu_resume_handler(uint32_t a0, uint32_t a1);

static void init_canaries(void)
{
	size_t n;
#define INIT_CANARY(name)						\
	for (n = 0; n < ARRAY_SIZE(name); n++) {			\
		uint32_t *start_canary = &GET_START_CANARY(name, n);	\
		uint32_t *end_canary = &GET_END_CANARY(name, n);	\
									\
		*start_canary = START_CANARY_VALUE;			\
		*end_canary = END_CANARY_VALUE;				\
		DMSG("#Stack canaries for %s[%zu] with top at %p\n",	\
			#name, n, (void *)(end_canary - 1));		\
		DMSG("watch *%p\n", (void *)end_canary);	\
	}

	INIT_CANARY(stack_tmp);
	INIT_CANARY(stack_abt);
	INIT_CANARY(stack_sm);
	INIT_CANARY(stack_thread);
}

void check_canaries(void)
{
#ifdef WITH_STACK_CANARIES
	size_t n;

#define ASSERT_STACK_CANARIES(name)					\
	for (n = 0; n < ARRAY_SIZE(name); n++) {			\
		assert(GET_START_CANARY(name, n) == START_CANARY_VALUE);\
		assert(GET_END_CANARY(name, n) == END_CANARY_VALUE);	\
	} while (0)

	ASSERT_STACK_CANARIES(stack_tmp);
	ASSERT_STACK_CANARIES(stack_abt);
	ASSERT_STACK_CANARIES(stack_sm);
	ASSERT_STACK_CANARIES(stack_thread);
#endif /*WITH_STACK_CANARIES*/
}

static const struct thread_handlers handlers = {
	.std_smc = main_tee_entry,
	.fast_smc = main_tee_entry,
	.fiq = main_fiq,
	.svc = NULL, /* XXX currently using hardcod svc handler */
	.abort = tee_pager_abort_handler,
	.cpu_on = cpu_on_handler,
	.cpu_off = main_cpu_off_handler,
	.cpu_suspend = main_cpu_suspend_handler,
	.cpu_resume = main_cpu_resume_handler,

};

uint32_t *main_init(void); /* called from assembly only */
uint32_t *main_init(void)
{
	uintptr_t bss_start = (uintptr_t)&__bss_start;
	uintptr_t bss_end = (uintptr_t)&__bss_end;
	size_t n;
	size_t pos = get_core_pos();

	/*
	 * Mask IRQ and FIQ before switch to the thread vector as the
	 * thread handler requires IRQ and FIQ to be masked while executing
	 * with the temporary stack. The thread subsystem also asserts
	 * that IRQ is blocked when using most if its functions.
	 */
	write_cpsr(read_cpsr() | CPSR_F | CPSR_I);

	/* Initialize user with physical address */
	uart_init(UART1_BASE);

	/*
	 * Zero BSS area. Note that globals that would normally
	 * would go into BSS which are used before this has to be
	 * put into .bss.prebss.* to avoid getting overwritten.
	 */
	memset((void *)bss_start, 0, bss_end - bss_start);

	DMSG("TEE initializing\n");

	/* Initialize canaries around the stacks */
	init_canaries();

	if (!thread_init_stack(THREAD_TMP_STACK, GET_STACK(stack_tmp[pos])))
		panic();
	if (!thread_init_stack(THREAD_ABT_STACK, GET_STACK(stack_abt[pos])))
		panic();

	for (n = 0; n < NUM_THREADS; n++) {
		if (!thread_init_stack(n, GET_STACK(stack_thread[n])))
			panic();
	}

	thread_init_handlers(&handlers);

#if 0
	/* Initialize GIC */
	gic_init(GIC_BASE + GICC_OFFSET, GIC_BASE + GICD_OFFSET);
	gic_it_add(IT_UART1);
	gic_it_set_cpu_mask(IT_UART1, 0x1);
	gic_it_set_prio(IT_UART1, 0xff);
	gic_it_enable(IT_UART1);
#endif

	if (init_teecore() != TEE_SUCCESS)
		panic();
	DMSG("%s: vector %p\n", __func__, (void *)thread_vector_table);

	DMSG("Switching to normal world boot\n");
	return thread_vector_table;
}

static void main_fiq(void)
{
	uint32_t iar;

	DMSG("%s\n", __func__);

	iar = gic_read_iar();

	while (uart_have_rx_data(UART1_BASE))
		DMSG("got 0x%x\n", uart_getchar(UART1_BASE));

	gic_write_eoir(iar);

	DMSG("return from %s\n", __func__);
}

static uint32_t main_cpu_off_handler(uint32_t a0, uint32_t a1)
{
	(void)&a0;
	(void)&a1;
	/* Could stop generic timer here */
	DMSG("cpu %zu: a0 0%x", get_core_pos(), a0);
	return 0;
}

static uint32_t main_cpu_suspend_handler(uint32_t a0, uint32_t a1)
{
	(void)&a0;
	(void)&a1;
	/* Could save generic timer here */
	DMSG("cpu %zu: a0 0%x", get_core_pos(), a0);
	return 0;
}

static uint32_t main_cpu_resume_handler(uint32_t a0, uint32_t a1)
{
	(void)&a0;
	(void)&a1;
	/* Could restore generic timer here */
	DMSG("cpu %zu: a0 0%x", get_core_pos(), a0);
	return 0;
}

/* called from assembly only */
uint32_t main_cpu_on_handler(uint32_t a0, uint32_t a1);
uint32_t main_cpu_on_handler(uint32_t a0, uint32_t a1)
{
	size_t pos = get_core_pos();

	write_cpsr(read_cpsr() | CPSR_F | CPSR_I);

	DMSG("cpu %d: on a0 0x%x a1 0x%x", pos, a0, a1);

	if (!thread_init_stack(THREAD_TMP_STACK, GET_STACK(stack_tmp[pos])))
		panic();
	if (!thread_init_stack(THREAD_ABT_STACK, GET_STACK(stack_abt[pos])))
		panic();

	thread_init_handlers(&handlers);

	return 0;
}

static void main_tee_entry(struct thread_smc_args *args)
{
	/*
	 * This function first catches all ST specific SMC functions
	 * if none matches, the generic tee_entry is called.
	 */

	if (args->a0 == TEESMC32_ST_FASTCALL_GET_SHM_CONFIG) {
		args->a0 = TEESMC_RETURN_OK;
		args->a1 = default_nsec_shm_paddr;
		args->a2 = default_nsec_shm_size;
		/* Should this be TEESMC cache attributes instead? */
		args->a3 = core_mmu_is_shm_cached();
		return;
	}

	if (args->a0 == TEESMC32_ST_FASTCALL_L2CC_MUTEX) {
		switch (args->a1) {
		case TEESMC_ST_L2CC_MUTEX_GET_ADDR:
		case TEESMC_ST_L2CC_MUTEX_SET_ADDR:
		case TEESMC_ST_L2CC_MUTEX_ENABLE:
		case TEESMC_ST_L2CC_MUTEX_DISABLE:
			/* TODO call the appropriate internal functions */
			args->a0 = TEESMC_RETURN_UNKNOWN_FUNCTION;
			return;
		default:
			args->a0 = TEESMC_RETURN_EBADCMD;
			return;
		}
	}

	tee_entry(args);
}



/* Override weak function in tee/entry.c */
void tee_entry_get_api_call_count(struct thread_smc_args *args)
{
	args->a0 = tee_entry_generic_get_api_call_count() + 2;
}

/* Override weak function in tee/entry.c */
void tee_entry_get_api_uuid(struct thread_smc_args *args)
{
	args->a0 = TEESMC_ST_UID_R0;
	args->a1 = TEESMC_ST_UID_R1;
	args->a2 = TEESMC_ST_UID_R2;
	args->a3 = TEESMC_ST_UID32_R3;
}

/* Override weak function in tee/entry.c */
void tee_entry_get_api_revision(struct thread_smc_args *args)
{
	args->a0 = TEESMC_ST_REVISION_MAJOR;
	args->a1 = TEESMC_ST_REVISION_MINOR;
}

/* Override weak function in tee/entry.c */
void tee_entry_get_os_uuid(struct thread_smc_args *args)
{
	args->a0 = TEESMC_OS_OPTEE_UUID_R0;
	args->a1 = TEESMC_OS_OPTEE_UUID_R1;
	args->a2 = TEESMC_OS_OPTEE_UUID_R2;
	args->a3 = TEESMC_OS_OPTEE_UUID_R3;
}

/* Override weak function in tee/entry.c */
void tee_entry_get_os_revision(struct thread_smc_args *args)
{
	args->a0 = TEESMC_OS_OPTEE_REVISION_MAJOR;
	args->a1 = TEESMC_OS_OPTEE_REVISION_MINOR;
}

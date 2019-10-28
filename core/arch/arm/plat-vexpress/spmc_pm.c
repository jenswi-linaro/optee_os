// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2019, Arm Limited
 */

#include <arm.h>
#include <arm64.h>
#include <initcall.h>
#include <keep.h>
#include <kernel/generic_boot.h>
#include <kernel/interrupt.h>
#include <kernel/misc.h>
#include <kernel/panic.h>
#include <mm/core_memprot.h>
#include <mm/core_mmu.h>
#include <platform_config.h>
#include <sm/psci.h>
#include <spmc_private.h>
#include <stdint.h>
#include <string.h>
#include <trace.h>

/*
 * Lookup table of core and cluster affinities on the FVP. In the absence of a
 * DT that provides the same information, this table is used to initialise
 * OP-TEE on secondary cores.
 */
static const uint64_t core_clus_aff_array[] = {
	0x0000,		/* Cluster 0 Cpu 0 */
	0x0001,		/* Cluster 0 Cpu 1 */
	0x0002,		/* Cluster 0 Cpu 2 */
	0x0003,		/* Cluster 0 Cpu 3 */
	0x0100,		/* Cluster 1 Cpu 0 */
	0x0101,		/* Cluster 1 Cpu 1 */
	0x0102,		/* Cluster 1 Cpu 2 */
	0x0103,		/* Cluster 1 Cpu 3 */
};

static const uint8_t aff_array_entries =
	sizeof(core_clus_aff_array)/sizeof(core_clus_aff_array[0]);

void spci_secondary_cpu_boot_req(uintptr_t secondary_ep,
				 uint64_t cookie __unused)
{
	uint64_t mpidr = read_mpidr_el1(), x1;
	uint8_t aff_shift = 0, cnt;

	if (mpidr & (1 << MPIDR_MT_SHIFT))
		aff_shift = MPIDR_CLUSTER_SHIFT;

	for (cnt = 0; cnt < aff_array_entries; cnt++) {
		int32_t ret = 0;

		/* Clear out the affinity fields until level 2 */
		x1 = mpidr & (~MPIDR_AARCH32_AFF_MASK);

		/* Create an mpidr from core_clus_aff_array */
		x1 |= core_clus_aff_array[cnt] << aff_shift;

		/* Ignore current cpu */
		if (x1 == mpidr)
			continue;

		DMSG("PSCI_CPU_ON op on mpidr 0x%lx\n", x1);

		/* Invoke the PSCI_CPU_ON function */
		asm volatile(
			"mov x0, %[fid]\n"
			"mov x1, %[x1]\n"
			"mov x2, %[secondary_ep]\n"
			"mov x3, %[cookie]\n"
			"smc #0\n"
			"mov %[ret], x0\n"
			: [ret] "=r" (ret)
			: [fid] "r" (PSCI_CPU_ON_SMC64),
			  [x1] "r" (x1),
			  [secondary_ep] "r" (secondary_ep),
			  [cookie] "r" (cookie)
			);

		if (ret != PSCI_RET_SUCCESS)
			EMSG("PSCI_CPU_ON op on mpidr 0x%lx failed (%d)\n",
			     x1, ret);
		else
			DMSG("PSCI_CPU_ON op on mpidr 0x%lx done\n", x1);
	}
	return;
}

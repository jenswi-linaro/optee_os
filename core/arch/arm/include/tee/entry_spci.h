/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2018, Linaro Limited
 */

#ifndef __TEE_ENTRY_SPCI_H
#define __TEE_ENTRY_SPCI_H


#include <kernel/thread.h>

void tee_entry_spci_fast(struct thread_smc_args *args);
void tee_entry_spci_request(struct thread_smc_args *args);

#endif /*__TEE_ENTRY_SPCI_H*/

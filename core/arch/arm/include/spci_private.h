/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2018-2019, Arm Limited. All rights reserved.
 */
#ifndef SPCI_PRIVATE_H
#define SPCI_PRIVATE_H

#include <spci.h>
#include <kernel/thread.h>

/* OP-TEE implementation defined message */
typedef struct thread_smc_args spci_optee_msg_t;

typedef struct spci_msg_buf_desc {
	paddr_t pa;
	vaddr_t va;
	uint32_t attributes;
	size_t page_count;
} spci_msg_buf_desc_t;

void spci_early_init(spci_buf_t *spci_rx_buf);
void spci_late_init(void);
uintptr_t spci_arch_msg_get(uint16_t msg_type);
void spci_msg_put(unsigned int sec_state);
void *spci_msg_recv(int32_t status, struct thread_smc_args *args);
void spci_msg_recv_invoke(void);
void spci_msg_loop(void);

#endif /* SPCI_PRIVATE_H */

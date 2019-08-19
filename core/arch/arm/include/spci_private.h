/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2018-2019, Arm Limited. All rights reserved.
 */
#ifndef __SPCI_PRIVATE_H
#define __SPCI_PRIVATE_H

#include <types_ext.h>
#include <spci.h>
#include <kernel/thread.h>

struct spci_msg_buf_desc {
	paddr_t pa;
	vaddr_t va;
	uint32_t attributes;
	unsigned int page_count;
};

void spci_early_init(struct spci_buf *spci_rx_buf);
void spci_late_init(void);
struct spci_msg_sp_init *spci_get_msg_sp_init(void);
uint32_t spci_msg_send_prepare(const void *msg, size_t msg_len);
void spci_msg_send_recv_invoke(uint32_t attributes);

#endif /* __SPCI_PRIVATE_H */

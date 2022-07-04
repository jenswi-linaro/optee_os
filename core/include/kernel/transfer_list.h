/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2022, Linaro Limited
 */

#ifndef __KERNEL_TRANSFER_LIST_H
#define __KERNEL_TRANSFER_LIST_H

#define	TRANSFER_LIST_SIGNATURE		U(0x1a6ed0ff)
#define TRANSFER_LIST_VERSION		U(0x00010000)

#define TRANSFER_LIST_GRANULE		U(16)

#ifndef __ASSEMBLER__

#include <types_ext.h>

enum transfer_list_tag_id {
	TL_TAG_EMPTY = 0,
	TL_TAG_FDT = 1,
	TL_TAG_HOB_BLOCK = 2,
	TL_TAG_HOB_LIST = 3,
	TL_TAG_ACPI_TABLE = 4,
	TL_TAG_ACPI_TABLE_AGGREGATE = 5,
	TL_TAG_FDT_OVERLAY = 6,

	TL_TAG_OPTEE_PAGABLE_PART = 0x1000,
	TL_TAG_TOS_FW_CONFIG = 0x1001,
};

struct transfer_list {
	uint32_t	signature;
	uint8_t		checksum;
	uint8_t		reserved0[3];
	uint32_t	version;
	uint32_t	length;
	uint32_t	max_length;
	uint8_t		reserved1[12];
	/*
	 * Commented out element used to visualze dynamic part of the
	 * data structure.
	 *
	 * Note that struct transfer_entry also is dynamic in size
	 * so the elements can't be indexed directly but instead must be
	 * traversed in order
	 *
	 * struct transfer_entry entries[];
	 */
};

struct transfer_entry {
	uint32_t	tag_id;
	uint32_t	hdr_size;
	uint32_t	data_size;
	uint32_t	reserved;
	/*
	 * Commented out element used to visualze dynamic part of the
	 * data structure.
	 *
	 * Note that padding is added at the end of @data to make to reach
	 * a 16-byte boundary.
	 *
	 * uint8_t	data[ROUNDUP(data_size, 16)];
	 */
};

struct transfer_list *transfer_list_map(paddr_t pa);
void transfer_list_unmap_sync(struct transfer_list *tl);
void transfer_list_unmap_nosync(struct transfer_list *tl);
struct transfer_list *transfer_list_check_header(struct transfer_list *tl);

struct transfer_entry *transfer_list_next(struct transfer_list *tl,
					  struct transfer_entry *last);

void transfer_list_update_checksum(struct transfer_list *tl);
bool transfer_list_verify_checksum(struct transfer_list *tl);

bool transfer_list_set_data_size(struct transfer_list *tl,
				 struct transfer_entry *entry,
				 uint32_t new_data_size);

void transfer_list_grow_to_max_data_size(struct transfer_list *tl,
					 struct transfer_entry *entry);

static inline void *transfer_list_data(struct transfer_entry *entry)
{
	return (uint8_t *)entry + entry->hdr_size;
}

void transfer_list_rem(struct transfer_list *tl, struct transfer_entry *entry);

struct transfer_entry *transfer_list_add(struct transfer_list *tl,
					 uint32_t tag_id, uint32_t data_size);

struct transfer_entry *transfer_list_find(struct transfer_list *tl,
					  uint32_t tag_id);

#endif /*__ASSEMBLER__*/
#endif /*__KERNEL_TRANSFER_LIST_H*/

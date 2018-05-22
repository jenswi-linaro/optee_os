/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2018, Linaro Limited */

#ifndef __USER_TA_HEADER_DEFINES_H
#define __USER_TA_HEADER_DEFINES_H

#include <keymaster_ta.h>

#define TA_UUID				TA_KEYMASTER_UUID

#define TA_FLAGS			(TA_FLAG_SINGLE_INSTANCE | \
					 TA_FLAG_MULTI_SESSION)

#define TA_STACK_SIZE			(16 * 1024)
#define TA_DATA_SIZE			(16 * 1024)

#define TA_CURRENT_TA_EXT_PROPERTIES \
    { "gp.ta.description", USER_TA_PROP_TYPE_STRING, \
        "AOSP Keymaster services Trusted Application" }, \
    { "gp.ta.version", USER_TA_PROP_TYPE_U32, &(const uint32_t){ 0 } }

#endif /*__USER_TA_HEADER_DEFINES_H*/

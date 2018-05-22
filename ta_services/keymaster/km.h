/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2018, Linaro Limited */

#ifndef LOCAL_KM_H
#define LOCAL_KM_H

#include <tee_internal_api.h>

TEE_Result km_add_rng_entropy(const void *buf, size_t blen);

#endif /*LOCAL_KM_H*/

/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2018, Linaro Limited */

#ifndef __KEYMASTER_TA_H
#define __KEYMASTER_TA_H

#define TA_KEYMASTER_UUID { 0xd36b30c7, 0x5a5f, 0x472c, \
			    {0xaf, 0x97, 0x7f, 0x38, 0xa2, 0xed, 0xab, 0x7d } }

/*
 * Add (re-seed) caller-provided entropy to the RNG pool. Keymaster
 * implementations need to securely mix the provided entropy into their
 * pool, which also must contain internally-generated entropy from a
 * hardware random number generator.
 *
 * in	params[0].memref: entropy input data
 */
#define KEYMASTER_CMD_ADD_RNG_ENTROPY	0

#endif /*__KEYMASTER_TA_H*/

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

/*
 * See description of genereateKey at [0] for the different elements.
 * [0] Link: https://source.android.com/reference/hidl/android/hardware/keymaster/3.0/types#keyparameter
 *
 * in	params[0].memref  = serialized array of struct key_param
 * out	params[1].memref  = keyBlob
 * out	params[2].memref  = serialized array of struct key_param representing
 *			    the teeEnforced array of keyCharacteristics.
 *
 * struct key_param - holds a key parameter
 * @tag		opaque value when serializing
 * @size	size of data below
 * @data	data of the key parameter @size bytes large
 *
 * struct key_param {
 *	uint32_t tag;	@tag is an opaque value when serializing
 *	uint32_t size;
 *	uint8_t data[]; @data is @size large
 * };
 */
#define KEYMASTER_CMD_GENERATE_KEY	1

/*
 * Returns parameters and authorizations associated with the provided key,
 * see description of getKeyCharacteristics at [0] for the different elemenets.
 * [0] Link: https://source.android.com/reference/hidl/android/hardware/keymaster/3.0/IKeymasterDevice#getkeycharacteristics
 *
 * in   params[0].memref  = keyBlob
 * in   params[1].memref  = serialized array of struct key_param holding
 *			    APPLICATION_ID and APPLICATION_DATA
 * out	params[2].memref  = serialized array of struct key_param representing
 *			    the teeEnforced array of keyCharacteristics.
 */
#define KEYMASTER_CMD_GET_KEY_CHARACTERISTICS 2

#endif /*__KEYMASTER_TA_H*/

/*
 * Support for ePass2003 smart cards
 *
 * Copyright (C) 2008, Weitao Sun <weitao@ftsafe.com>
 * Copyright (C) 2011, Xiaoshuo Wu <xiaoshuo@ftsafe.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef ENABLE_SM		/* empty file without SM enabled */
#ifdef ENABLE_OPENSSL		/* empty file without openssl */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/sha.h>

#include "internal.h"
#include "asn1.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/sha.h>

#include "internal.h"
#include "asn1.h"
#include "cardctl.h"

static const struct sc_atr_table epass2003_atrs[] = {
	/* This is a FIPS certified card using SCP01 security messaging. */
	{"3B:9F:95:81:31:FE:9F:00:66:46:53:05:10:00:11:71:df:00:00:00:6a:82:5e",
	 "FF:FF:FF:FF:FF:00:FF:FF:FF:FF:FF:FF:00:00:00:ff:00:ff:ff:00:00:00:00",
	 "FTCOS/ePass2003", SC_CARD_TYPE_ENTERSAFE_FTCOS_EPASS2003, 0, NULL },
	{NULL, NULL, NULL, 0, 0, NULL}
};

static struct sc_card_operations *iso_ops = NULL;
static struct sc_card_operations epass2003_ops;

static struct sc_card_driver epass2003_drv = {
	"epass2003",
	"epass2003",
	&epass2003_ops,
	NULL, 0, NULL
};

#define KEY_TYPE_AES	0x01	/* FIPS mode */
#define KEY_TYPE_DES	0x02	/* Non-FIPS mode */

#define KEY_LEN_AES	16
#define KEY_LEN_DES	8
#define KEY_LEN_DES3	24
#define HASH_LEN	24

static unsigned char PIN_ID[2] = { ENTERSAFE_USER_PIN_ID, ENTERSAFE_SO_PIN_ID };

/*0x00:plain; 0x01:scp01 sm*/
#define SM_PLAIN				0x00
#define SM_SCP01				0x01

static unsigned char g_init_key_enc[16] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
	0x0D, 0x0E, 0x0F, 0x10
};

static unsigned char g_init_key_mac[16] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
	0x0D, 0x0E, 0x0F, 0x10
};

static unsigned char g_random[8] = {
	0xBF, 0xC3, 0x29, 0x11, 0xC7, 0x18, 0xC3, 0x40
};

typedef struct epass2003_exdata_st {
	unsigned char sm;		/* SM_PLAIN or SM_SCP01 */
	unsigned char smtype;		/* KEY_TYPE_AES or KEY_TYPE_DES */
	unsigned char sk_enc[16];	/* encrypt session key */
	unsigned char sk_mac[16];	/* mac session key */
	unsigned char icv_mac[16];	/* instruction counter vector(for sm) */
	unsigned char currAlg;		/* current Alg */
	unsigned int  ecAlgFlags; 	/* Ec Alg mechanism type*/
} epass2003_exdata;

#define REVERSE_ORDER4(x)	(			  \
		((unsigned long)x & 0xFF000000)>> 24	| \
		((unsigned long)x & 0x00FF0000)>>  8 	| \
		((unsigned long)x & 0x0000FF00)<<  8	| \
		((unsigned long)x & 0x000000FF)<< 24)


static const struct sc_card_error epass2003_errors[] = {
	{ 0x6200, SC_ERROR_CARD_CMD_FAILED,	"Warning: no information given, non-volatile memory is unchanged" },
	{ 0x6281, SC_ERROR_CORRUPTED_DATA,	"Part of returned data may be corrupted" },
	{ 0x6282, SC_ERROR_FILE_END_REACHED,	"End of file/record reached before reading Le bytes" },
	{ 0x6283, SC_ERROR_CARD_CMD_FAILED,	"Selected file invalidated" },
	{ 0x6284, SC_ERROR_CARD_CMD_FAILED,	"FCI not formatted according to ISO 7816-4" },

	{ 0x6300, SC_ERROR_PIN_CODE_INCORRECT,  "Authentication failed"}, 
	{ 0x63C1, SC_ERROR_PIN_CODE_INCORRECT,  "Authentication failed. One tries left"}, 
	{ 0x63C2, SC_ERROR_PIN_CODE_INCORRECT,  "Authentication failed. Two tries left"}, 
	{ 0x63C3, SC_ERROR_PIN_CODE_INCORRECT,  "Authentication failed"}, 
	{ 0x63C4, SC_ERROR_PIN_CODE_INCORRECT,  "Authentication failed"}, 
	{ 0x63C5, SC_ERROR_PIN_CODE_INCORRECT,  "Authentication failed"}, 
	{ 0x63C6, SC_ERROR_PIN_CODE_INCORRECT,  "Authentication failed"}, 
	{ 0x63C7, SC_ERROR_PIN_CODE_INCORRECT,  "Authentication failed"}, 
	{ 0x63C8, SC_ERROR_PIN_CODE_INCORRECT,  "Authentication failed"}, 
	{ 0x63C9, SC_ERROR_PIN_CODE_INCORRECT,  "Authentication failed"}, 
	{ 0x63CA, SC_ERROR_PIN_CODE_INCORRECT,  "Authentication failed"}, 

	{ 0x6381, SC_ERROR_CARD_CMD_FAILED,	"Warning: file filled up by last write" },

	{ 0x6581, SC_ERROR_MEMORY_FAILURE,	"Memory failure" },

	{ 0x6700, SC_ERROR_WRONG_LENGTH,	"Wrong length" },

	{ 0x6800, SC_ERROR_NO_CARD_SUPPORT,	"Functions in CLA not supported" },
	{ 0x6881, SC_ERROR_NO_CARD_SUPPORT,	"Logical channel not supported" },
	{ 0x6882, SC_ERROR_NO_CARD_SUPPORT,	"Secure messaging not supported" },

	{ 0x6900, SC_ERROR_NOT_ALLOWED,		"Command not allowed" },
	{ 0x6981, SC_ERROR_CARD_CMD_FAILED,	"Command incompatible with file structure" },
	{ 0x6982, SC_ERROR_SECURITY_STATUS_NOT_SATISFIED, "Security status not satisfied" },
	{ 0x6983, SC_ERROR_AUTH_METHOD_BLOCKED,	"Authentication method blocked" },
	{ 0x6984, SC_ERROR_REF_DATA_NOT_USABLE,	"Referenced data not usable" },
	{ 0x6985, SC_ERROR_NOT_ALLOWED,		"Conditions of use not satisfied" },
	{ 0x6986, SC_ERROR_NOT_ALLOWED,		"Command not allowed (no current EF)" },
	{ 0x6987, SC_ERROR_INCORRECT_PARAMETERS,"Expected SM data objects missing" },
	{ 0x6988, SC_ERROR_INCORRECT_PARAMETERS,"SM data objects incorrect" },

	{ 0x6A00, SC_ERROR_INCORRECT_PARAMETERS,"Wrong parameter(s) P1-P2" },
	{ 0x6A80, SC_ERROR_INCORRECT_PARAMETERS,"Incorrect parameters in the data field" },
	{ 0x6A81, SC_ERROR_NO_CARD_SUPPORT,	"Function not supported" },
	{ 0x6A82, SC_ERROR_FILE_NOT_FOUND,	"File not found" },
	{ 0x6A83, SC_ERROR_RECORD_NOT_FOUND,	"Record not found" },
	{ 0x6A84, SC_ERROR_NOT_ENOUGH_MEMORY,	"Not enough memory space in the file" },
	{ 0x6A85, SC_ERROR_INCORRECT_PARAMETERS,"Lc inconsistent with TLV structure" },
	{ 0x6A86, SC_ERROR_INCORRECT_PARAMETERS,"Incorrect parameters P1-P2" },
	{ 0x6A87, SC_ERROR_INCORRECT_PARAMETERS,"Lc inconsistent with P1-P2" },
	{ 0x6A88, SC_ERROR_DATA_OBJECT_NOT_FOUND,"Referenced data not found" },
	{ 0x6A89, SC_ERROR_FILE_ALREADY_EXISTS,  "File already exists"},
	{ 0x6A8A, SC_ERROR_FILE_ALREADY_EXISTS,  "DF name already exists"},

	{ 0x6B00, SC_ERROR_INCORRECT_PARAMETERS,"Wrong parameter(s) P1-P2" },
	{ 0x6D00, SC_ERROR_INS_NOT_SUPPORTED,	"Instruction code not supported or invalid" },
	{ 0x6E00, SC_ERROR_CLASS_NOT_SUPPORTED,	"Class not supported" },
	{ 0x6F00, SC_ERROR_CARD_CMD_FAILED,	"No precise diagnosis" },

	{ 0x9000,SC_SUCCESS,                       NULL }
};

static int epass2003_transmit_apdu(struct sc_card *card, struct sc_apdu *apdu);
static int epass2003_select_file(struct sc_card *card, const sc_path_t * in_path, sc_file_t ** file_out);
int epass2003_refresh(struct sc_card *card);
static int hash_data(const unsigned char *data, size_t datalen, unsigned char *hash, unsigned int mechanismType);

static int
epass2003_check_sw(struct sc_card *card, unsigned int sw1, unsigned int sw2)
{
	const int err_count = sizeof(epass2003_errors)/sizeof(epass2003_errors[0]);
	int i;

	/* Handle special cases here */
	if (sw1 == 0x6C) {
		sc_log(card->ctx, "Wrong length; correct length is %d", sw2);
		return SC_ERROR_WRONG_LENGTH;
	}


	for (i = 0; i < err_count; i++)   {
		if (epass2003_errors[i].SWs == ((sw1 << 8) | sw2)) {
			sc_log(card->ctx, "%s", epass2003_errors[i].errorstr);
			return epass2003_errors[i].errorno;
		}
	}

	sc_log(card->ctx, "Unknown SWs; SW1=%02X, SW2=%02X", sw1, sw2);
	return SC_ERROR_CARD_CMD_FAILED;
}

static int
sc_transmit_apdu_t(sc_card_t *card, sc_apdu_t *apdu)
{
	int r = sc_transmit_apdu(card, apdu);
	if ( ((0x69 == apdu->sw1) && (0x85 == apdu->sw2)) || ((0x69 == apdu->sw1) && (0x88 == apdu->sw2)))
	{
		epass2003_refresh(card);
		r = sc_transmit_apdu(card, apdu);
	}
	return r;
}

static int
openssl_enc(const EVP_CIPHER * cipher, const unsigned char *key, const unsigned char *iv,
		const unsigned char *input, size_t length, unsigned char *output)
{
	int r = SC_ERROR_INTERNAL;
	EVP_CIPHER_CTX * ctx = NULL;
	int outl = 0;
	int outl_tmp = 0;
	unsigned char iv_tmp[EVP_MAX_IV_LENGTH] = { 0 };

	memcpy(iv_tmp, iv, EVP_MAX_IV_LENGTH);
	ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL)
		goto out;
	EVP_EncryptInit_ex(ctx, cipher, NULL, key, iv_tmp);
	EVP_CIPHER_CTX_set_padding(ctx, 0);

	if (!EVP_EncryptUpdate(ctx, output, &outl, input, length))
		goto out;

	if (!EVP_EncryptFinal_ex(ctx, output + outl, &outl_tmp))
		goto out;

	r = SC_SUCCESS;
out:
	if (ctx)
	    EVP_CIPHER_CTX_free(ctx);
	return r;
}

static int
openssl_dec(const EVP_CIPHER * cipher, const unsigned char *key, const unsigned char *iv,
		const unsigned char *input, size_t length, unsigned char *output)
{
	int r = SC_ERROR_INTERNAL;
	EVP_CIPHER_CTX * ctx = NULL;
	int outl = 0;
	int outl_tmp = 0;
	unsigned char iv_tmp[EVP_MAX_IV_LENGTH] = { 0 };

	memcpy(iv_tmp, iv, EVP_MAX_IV_LENGTH);
	ctx = EVP_CIPHER_CTX_new();
	if (ctx == NULL)
		goto out;
	EVP_DecryptInit_ex(ctx, cipher, NULL, key, iv_tmp);
	EVP_CIPHER_CTX_set_padding(ctx, 0);

	if (!EVP_DecryptUpdate(ctx, output, &outl, input, length))
		goto out;

	if (!EVP_DecryptFinal_ex(ctx, output + outl, &outl_tmp))
		goto out;

	r = SC_SUCCESS;
out:
	if (ctx)
		EVP_CIPHER_CTX_free(ctx);
	return r;
}


static int
aes128_encrypt_ecb(const unsigned char *key, int keysize,
		const unsigned char *input, size_t length, unsigned char *output)
{
	unsigned char iv[EVP_MAX_IV_LENGTH] = { 0 };
	return openssl_enc(EVP_aes_128_ecb(), key, iv, input, length, output);
}


static int
aes128_encrypt_cbc(const unsigned char *key, int keysize, unsigned char iv[16],
		const unsigned char *input, size_t length, unsigned char *output)
{
	return openssl_enc(EVP_aes_128_cbc(), key, iv, input, length, output);
}


static int
aes128_decrypt_cbc(const unsigned char *key, int keysize, unsigned char iv[16],
		const unsigned char *input, size_t length, unsigned char *output)
{
	return openssl_dec(EVP_aes_128_cbc(), key, iv, input, length, output);
}


static int
des3_encrypt_ecb(const unsigned char *key, int keysize,
		const unsigned char *input, int length, unsigned char *output)
{
	unsigned char iv[EVP_MAX_IV_LENGTH] = { 0 };
	unsigned char bKey[24] = { 0 };

	if (keysize == 16) {
		memcpy(&bKey[0], key, 16);
		memcpy(&bKey[16], key, 8);
	}
	else {
		memcpy(&bKey[0], key, 24);
	}

	return openssl_enc(EVP_des_ede3(), bKey, iv, input, length, output);
}


static int
des3_encrypt_cbc(const unsigned char *key, int keysize, unsigned char iv[EVP_MAX_IV_LENGTH],
		const unsigned char *input, size_t length, unsigned char *output)
{
	unsigned char bKey[24] = { 0 };

	if (keysize == 16) {
		memcpy(&bKey[0], key, 16);
		memcpy(&bKey[16], key, 8);
	}
	else {
		memcpy(&bKey[0], key, 24);
	}

	return openssl_enc(EVP_des_ede3_cbc(), bKey, iv, input, length, output);
}


static int
des3_decrypt_cbc(const unsigned char *key, int keysize, unsigned char iv[EVP_MAX_IV_LENGTH],
		const unsigned char *input, size_t length, unsigned char *output)
{
	unsigned char bKey[24] = { 0 };
	if (keysize == 16) {
		memcpy(&bKey[0], key, 16);
		memcpy(&bKey[16], key, 8);
	}
	else {
		memcpy(&bKey[0], key, 24);
	}

	return openssl_dec(EVP_des_ede3_cbc(), bKey, iv, input, length, output);
}


static int
des_encrypt_cbc(const unsigned char *key, int keysize, unsigned char iv[EVP_MAX_IV_LENGTH],
		const unsigned char *input, size_t length, unsigned char *output)
{
	return openssl_enc(EVP_des_cbc(), key, iv, input, length, output);
}


static int
des_decrypt_cbc(const unsigned char *key, int keysize, unsigned char iv[EVP_MAX_IV_LENGTH],
		const unsigned char *input, size_t length, unsigned char *output)
{
	return openssl_dec(EVP_des_cbc(), key, iv, input, length, output);
}


static int
openssl_dig(const EVP_MD * digest, const unsigned char *input, size_t length,
		unsigned char *output)
{
	int r = 0;
	EVP_MD_CTX *ctx = NULL;
	unsigned outl = 0;

	ctx = EVP_MD_CTX_create();
	if (ctx == NULL) {
	    r = SC_ERROR_OUT_OF_MEMORY;
	    goto err;
	}
	    
	EVP_MD_CTX_init(ctx);
	EVP_DigestInit_ex(ctx, digest, NULL);
	if (!EVP_DigestUpdate(ctx, input, length)) {
		r = SC_ERROR_INTERNAL;
		goto err;
	}

	if (!EVP_DigestFinal_ex(ctx, output, &outl)) {
		r = SC_ERROR_INTERNAL;
		goto err;
	}
	r = SC_SUCCESS;
err:
	if (ctx)
		EVP_MD_CTX_destroy(ctx);

	return r;
}


static int
sha1_digest(const unsigned char *input, size_t length, unsigned char *output)
{
	return openssl_dig(EVP_sha1(), input, length, output);
}

static int
sha256_digest(const unsigned char *input, size_t length, unsigned char *output)
{
	return openssl_dig(EVP_sha256(), input, length, output);
}


static int
gen_init_key(struct sc_card *card, unsigned char *key_enc, unsigned char *key_mac,
		unsigned char *result, unsigned char key_type)
{
	int r;
	struct sc_apdu apdu;
	unsigned char data[256] = { 0 };
	unsigned char tmp_sm;
	unsigned long blocksize = 0;
	unsigned char cryptogram[256] = { 0 };	/* host cryptogram */
	unsigned char iv[16] = { 0 };
	epass2003_exdata *exdata = NULL;
	
	if (!card->drv_data) 
		return SC_ERROR_INVALID_ARGUMENTS;
	

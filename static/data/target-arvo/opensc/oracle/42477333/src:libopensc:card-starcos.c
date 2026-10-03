/*
 * card-starcos.c: Support for STARCOS SPK 2.3 cards
 *
 * Copyright (C) 2003  Jörn Zukowski <zukowski@trustcenter.de> and 
 *                     Nils Larsch   <larsch@trustcenter.de>, TrustCenter AG
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

#include <stdlib.h>
#include <string.h>

#include "asn1.h"
#include "cardctl.h"
#include "internal.h"
#include "iso7816.h"

static const struct sc_atr_table starcos_atrs[] = {
	{ "3B:B7:94:00:c0:24:31:fe:65:53:50:4b:32:33:90:00:b4", NULL, NULL, SC_CARD_TYPE_STARCOS_GENERIC, 0, NULL },
	{ "3B:B7:94:00:81:31:fe:65:53:50:4b:32:33:90:00:d1", NULL, NULL, SC_CARD_TYPE_STARCOS_GENERIC, 0, NULL },
	{ "3b:b7:18:00:c0:3e:31:fe:65:53:50:4b:32:34:90:00:25", NULL, NULL, SC_CARD_TYPE_STARCOS_GENERIC, 0, NULL },
	{ "3b:d8:18:ff:81:b1:fe:45:1f:03:80:64:04:1a:b4:03:81:05:61", NULL, NULL, SC_CARD_TYPE_STARCOS_V3_4, 0, NULL },
	{ "3b:d3:96:ff:81:b1:fe:45:1f:07:80:81:05:2d", NULL, NULL, SC_CARD_TYPE_STARCOS_V3_4, 0, NULL },
	{ "3B:9B:96:C0:0A:31:FE:45:80:67:04:1E:B5:01:00:89:4C:81:05:45", NULL, NULL, SC_CARD_TYPE_STARCOS_V3_5, 0, NULL },
	{ "3B:DB:96:FF:81:31:FE:45:80:67:05:34:B5:02:01:C0:A1:81:05:3C", NULL, NULL, SC_CARD_TYPE_STARCOS_V3_5, 0, NULL },
	{ "3B:D9:96:FF:81:31:FE:45:80:31:B8:73:86:01:C0:81:05:02", NULL, NULL, SC_CARD_TYPE_STARCOS_V3_5, 0, NULL },
	{ "3B:DF:96:FF:81:31:FE:45:80:5B:44:45:2E:42:4E:4F:54:4B:31:31:31:81:05:A0", NULL, NULL, SC_CARD_TYPE_STARCOS_V3_5, 0, NULL },
	{ "3B:DF:96:FF:81:31:FE:45:80:5B:44:45:2E:42:4E:4F:54:4B:31:30:30:81:05:A0", NULL, NULL, SC_CARD_TYPE_STARCOS_V3_5, 0, NULL },
	{ "3B:D9:96:FF:81:31:FE:45:80:31:B8:73:86:01:E0:81:05:22", NULL, NULL, SC_CARD_TYPE_STARCOS_V3_5, 0, NULL },
	{ "3B:D0:97:FF:81:B1:FE:45:1F:07:2B", NULL, NULL, SC_CARD_TYPE_STARCOS_V3_4, 0, NULL },
	{ NULL, NULL, NULL, 0, 0, NULL }
};

static struct sc_card_operations starcos_ops;
static struct sc_card_operations *iso_ops = NULL;

static struct sc_card_driver starcos_drv = {
	"STARCOS",
	"starcos",
	&starcos_ops,
	NULL, 0, NULL
};

static const struct sc_card_error starcos_errors[] = 
{
	{ 0x6600, SC_ERROR_INCORRECT_PARAMETERS, "Error setting the security env"},
	{ 0x66F0, SC_ERROR_INCORRECT_PARAMETERS, "No space left for padding"},
	{ 0x69F0, SC_ERROR_NOT_ALLOWED,          "Command not allowed"},
	{ 0x6A89, SC_ERROR_FILE_ALREADY_EXISTS,  "Files exists"},
	{ 0x6A8A, SC_ERROR_FILE_ALREADY_EXISTS,  "Application exists"},
	{ 0x6F01, SC_ERROR_CARD_CMD_FAILED, "public key not complete"},
	{ 0x6F02, SC_ERROR_CARD_CMD_FAILED, "data overflow"},
	{ 0x6F03, SC_ERROR_CARD_CMD_FAILED, "invalid command sequence"},
	{ 0x6F05, SC_ERROR_CARD_CMD_FAILED, "security environment invalid"},
	{ 0x6F07, SC_ERROR_FILE_NOT_FOUND, "key part not found"},
	{ 0x6F08, SC_ERROR_CARD_CMD_FAILED, "signature failed"},
	{ 0x6F0A, SC_ERROR_INCORRECT_PARAMETERS, "key format does not match key length"},
	{ 0x6F0B, SC_ERROR_INCORRECT_PARAMETERS, "length of key component inconsistent with algorithm"},
	{ 0x6F81, SC_ERROR_CARD_CMD_FAILED, "system error"}
};

/* internal structure to save the current security environment */
typedef struct starcos_ex_data_st {
	int    sec_ops;	/* the currently selected security operation,
			 * i.e. SC_SEC_OPERATION_AUTHENTICATE etc. */
	unsigned int    fix_digestInfo;
} starcos_ex_data;

#define CHECK_NOT_SUPPORTED_V3_4(card) \
	do { \
		if ((card)->type == SC_CARD_TYPE_STARCOS_V3_4) { \
			sc_log((card)->ctx,  \
				"not supported for STARCOS 3.4 cards"); \
			return SC_ERROR_NOT_SUPPORTED; \
		} \
	} while (0);

/* the starcos part */
static int starcos_match_card(sc_card_t *card)
{
	int i;

	i = _sc_match_atr(card, starcos_atrs, &card->type);
	if (i < 0)
		return 0;
	return 1;
}

static int starcos_init(sc_card_t *card)
{
	unsigned int flags;
	starcos_ex_data *ex_data;

	ex_data = calloc(1, sizeof(starcos_ex_data));
	if (ex_data == NULL)
		return SC_ERROR_OUT_OF_MEMORY;

	card->name = "STARCOS";
	card->cla  = 0x00;
	card->drv_data = (void *)ex_data;

	flags = SC_ALGORITHM_RSA_PAD_PKCS1 
		| SC_ALGORITHM_ONBOARD_KEY_GEN
		| SC_ALGORITHM_RSA_PAD_ISO9796
		| SC_ALGORITHM_RSA_HASH_NONE
		| SC_ALGORITHM_RSA_HASH_SHA1
		| SC_ALGORITHM_RSA_HASH_MD5
		| SC_ALGORITHM_RSA_HASH_RIPEMD160
		| SC_ALGORITHM_RSA_HASH_MD5_SHA1;

	card->caps = SC_CARD_CAP_RNG; 

	if (card->type == SC_CARD_TYPE_STARCOS_V3_4
			|| card->type == SC_CARD_TYPE_STARCOS_V3_5) {
		if (card->type == SC_CARD_TYPE_STARCOS_V3_4)
			card->name = "STARCOS 3.4";
		else
			card->name = "STARCOS 3.5";
		card->caps |= SC_CARD_CAP_ISO7816_PIN_INFO;

		flags |= SC_CARD_FLAG_RNG
			| SC_ALGORITHM_RSA_HASH_SHA224
			| SC_ALGORITHM_RSA_HASH_SHA256
			| SC_ALGORITHM_RSA_HASH_SHA384
			| SC_ALGORITHM_RSA_HASH_SHA512;

		_sc_card_add_rsa_alg(card, 512, flags, 0x10001);
		_sc_card_add_rsa_alg(card, 768, flags, 0x10001);
		_sc_card_add_rsa_alg(card,1024, flags, 0x10001);
		_sc_card_add_rsa_alg(card,1728, flags, 0x10001);
		_sc_card_add_rsa_alg(card,1976, flags, 0x10001);
		_sc_card_add_rsa_alg(card,2048, flags, 0x10001);
	} else {
		_sc_card_add_rsa_alg(card, 512, flags, 0x10001);
		_sc_card_add_rsa_alg(card, 768, flags, 0x10001);
		_sc_card_add_rsa_alg(card,1024, flags, 0x10001);

		/* we need read_binary&friends with max 128 bytes per read */
		card->max_send_size = 128;
		card->max_recv_size = 128;
	}

	if (sc_parse_ef_atr(card) == SC_SUCCESS) {
		if (card->ef_atr->card_capabilities & ISO7816_CAP_EXTENDED_LENGTH) {
			card->caps |= SC_CARD_CAP_APDU_EXT;
		}
		if (card->ef_atr->max_response_apdu > 0) {
			card->max_recv_size = card->ef_atr->max_response_apdu;
		}
		if (card->ef_atr->max_command_apdu > 0) {
			card->max_send_size = card->ef_atr->max_command_apdu;
		}
	}

	return 0;
}

static int starcos_finish(sc_card_t *card)
{
	if (card->drv_data)
		free((starcos_ex_data *)card->drv_data);
	return 0;
}

static int process_fci(sc_context_t *ctx, sc_file_t *file,
		       const u8 *buf, size_t buflen)
{
	/* NOTE: According to the Starcos S 2.1 manual it's possible
	 *       that a SELECT DF returns as a FCI arbitrary data which
	 *       is stored in a object file (in the corresponding DF)
	 *       with the tag 0x6f.
	 */

	size_t taglen, len = buflen;
	const u8 *tag = NULL, *p;
  
	sc_log(ctx,  "processing FCI bytes\n");

	if (buflen < 2)
		return SC_ERROR_INTERNAL;
	if (buf[0] != 0x6f)
		return SC_ERROR_INVALID_DATA;
	len = (size_t)buf[1];
	if (buflen - 2 < len)
		return SC_ERROR_INVALID_DATA;
	p = buf + 2;

	/* defaults */
	file->type = SC_FILE_TYPE_WORKING_EF;
	file->ef_structure = SC_FILE_EF_UNKNOWN;
	file->shareable = 0;
	file->record_length = 0;
	file->size = 0;
  
	tag = sc_asn1_find_tag(ctx, p, len, 0x80, &taglen);
	if (tag != NULL && taglen >= 2) {
		int bytes = (tag[0] << 8) + tag[1];
		sc_log(ctx, 
			"  bytes in file: %d\n", bytes);
		file->size = bytes;
	}

  	tag = sc_asn1_find_tag(ctx, p, len, 0x82, &taglen);
	if (tag != NULL) {
		const char *type = "unknown";
		const char *structure = "unknown";

		if (taglen == 1 && tag[0] == 0x01) {
			/* transparent EF */
			type = "working EF";
			structure = "transparent";
			file->type = SC_FILE_TYPE_WORKING_EF;
			file->ef_structure = SC_FILE_EF_TRANSPARENT;
		} else if (taglen == 1 && tag[0] == 0x11) {
			/* object EF */
			type = "working EF";
			structure = "object";
			file->type = SC_FILE_TYPE_WORKING_EF;
			file->ef_structure = SC_FILE_EF_TRANSPARENT; /* TODO */
		} else if (taglen == 3 && tag[1] == 0x21) {
			type = "working EF";
			file->record_length = tag[2];
			file->type = SC_FILE_TYPE_WORKING_EF;
			/* linear fixed, cyclic or compute */
			switch ( tag[0] )
			{
				case 0x02:
					structure = "linear fixed";
					file->ef_structure = SC_FILE_EF_LINEAR_FIXED;
					break;
				case 0x07:
					structure = "cyclic";
					file->ef_structure = SC_FILE_EF_CYCLIC;
					break;
				case 0x17:
					structure = "compute";
					file->ef_structure = SC_FILE_EF_UNKNOWN;
					break;
				default:
					structure = "unknown";
					file->ef_structure = SC_FILE_EF_UNKNOWN;
					file->record_length = 0;
					break;
			}
		}

 		sc_log(ctx, 
			"  type: %s\n", type);
		sc_log(ctx, 
			"  EF structure: %s\n", structure);
	}
	file->magic = SC_FILE_MAGIC;

	return SC_SUCCESS;
}

static int process_fci_v3_4(sc_context_t *ctx, sc_file_t *file,
		       const u8 *buf, size_t buflen)
{
	size_t taglen, len = buflen;
	const u8 *tag = NULL, *p;

	sc_log(ctx, 
		 "processing %"SC_FORMAT_LEN_SIZE_T"u FCI bytes\n", buflen);

	if (buflen < 2)
		return SC_ERROR_INTERNAL;
	if (buf[0] != 0x6f)
		return SC_ERROR_INVALID_DATA;
	len = (size_t)buf[1];
	if (buflen - 2 < len)
		return SC_ERROR_INVALID_DATA;

	/* defaults */
	file->type = SC_FILE_TYPE_WORKING_EF;
	if (len == 0) {
		SC_FUNC_RETURN(ctx, 2, SC_SUCCESS);
	}

	p = buf + 2;
	file->ef_structure = SC_FILE_TYPE_DF;
	file->shareable = 1;
	tag = sc_asn1_find_tag(ctx, p, len, 0x84, &taglen);
	if (tag != NULL && taglen > 0 && taglen <= 16) {
		memcpy(file->name, tag, taglen);
		file->namelen = taglen;
		sc_log(ctx,  "filename %s",
			sc_dump_hex(file->name, file->namelen));
	}
	return SC_SUCCESS;
}

static int process_fcp_v3_4(sc_context_t *ctx, sc_file_t *file,
		       const u8 *buf, size_t buflen)
{
	size_t taglen, len = buflen;
	const u8 *tag = NULL, *p;

	sc_log(ctx, 
		 "processing %"SC_FORMAT_LEN_SIZE_T"u FCP bytes\n", buflen);

	if (buflen < 2)
		return SC_ERROR_INTERNAL;
	if (buf[0] != 0x62)
		return SC_ERROR_INVALID_DATA;
	len = (size_t)buf[1];
	if (buflen - 2 < len)
		return SC_ERROR_INVALID_DATA;
	p = buf + 2;

	tag = sc_asn1_find_tag(ctx, p, len, 0x80, &taglen);
	if (tag != NULL && taglen >= 2) {
		int bytes = (tag[0] << 8) + tag[1];
		sc_log(ctx, 
			"  bytes in file: %d\n", bytes);
		file->size = bytes;
	}

	tag = sc_asn1_find_tag(ctx, p, len, 0xc5, &taglen);
	if (tag != NULL && taglen >= 2) {
		int bytes = (tag[0] << 8) + tag[1];
		sc_log(ctx, 
			"  bytes in file 2: %d\n", bytes);
		file->size = bytes;
	}

	tag = sc_asn1_find_tag(ctx, p, len, 0x82, &taglen);
	if (tag != NULL) {
		const char *type = "unknown";
		const char *structure = "unknown";

		if (taglen >= 1) {
			unsigned char byte = tag[0];
			if (byte & 0x40) {
				file->shareable = 1;
			}
			if (byte == 0x38) {
				type = "DF";
				file->type = SC_FILE_TYPE_DF;
				file->shareable = 1;
			}
			switch (byte & 7) {
			case 1:
				/* transparent EF */
				type = "working EF";
				structure = "transparent";
				file->type = SC_FILE_TYPE_WORKING_EF;
				file->ef_structure = SC_FILE_EF_TRANSPARENT;
				break;
			case 2:
				/* linear fixed EF */
				type = "working EF";
				structure = "linear fixed";
				file->type = SC_FILE_TYPE_WORKING_EF;
				file->ef_structure = SC_FILE_EF_LINEAR_FIXED;
				break;
			case 4:
				/* linear variable EF */
				type = "working EF";
				structure = "linear variable";
				file->type = SC_FILE_TYPE_WORKING_EF;
				file->ef_structure = SC_FILE_EF_LINEAR_VARIABLE;
				break;
			case 6:
				/* cyclic EF */
				type = "working EF";
				structure = "cyclic";
				file->type = SC_FILE_TYPE_WORKING_EF;
				file->ef_structure = SC_FILE_EF_CYCLIC;
				break;
			default:
				/* use defaults from above */
				break;
			}
		}
		sc_log(ctx, 
			"  type: %s\n", type);
		sc_log(ctx, 
			"  EF structure: %s\n", structure);
		if (taglen >= 2) {
			if (tag[1] != 0x41 || taglen != 5) {
				SC_FUNC_RETURN(ctx, 2,SC_ERROR_INVALID_DATA);
			}
			/* formatted EF */
			file->record_length = (tag[2] << 8) + tag[3];
			file->record_count = tag[4];
			sc_log(ctx, 
				"  rec_len: %"SC_FORMAT_LEN_SIZE_T"u  rec_cnt: %"SC_FORMAT_LEN_SIZE_T"u\n\n",
				file->record_length, file->record_count);
		}
	}

	tag = sc_asn1_find_tag(ctx, p, len, 0x83, &taglen);
	if (tag != NULL && taglen >= 2) {
		file->id = (tag[0] << 8) | tag[1];
		sc_log(ctx,  "  file identifier: 0x%02X%02X\n",
			tag[0], tag[1]);
	}

	tag = sc_asn1_find_tag(ctx, p, len, 0x84, &taglen);
	if (tag != NULL && taglen > 0 && taglen <= 16) {
		memcpy(file->name, tag, taglen);
		file->namelen = taglen;
		sc_log(ctx,  "  filename %s",
			sc_dump_hex(file->name, file->namelen));
	}

	tag = sc_asn1_find_tag(ctx, p, len, 0x8a, &taglen);
	if (tag != NULL && taglen == 1) {
		char* status = "unknown";
		switch (tag[0]) {
		case 1:
			status = "creation";
			file->status = SC_FILE_STATUS_CREATION;
			break;
		case 5:
			status = "operational active";
			file->status = SC_FILE_STATUS_ACTIVATED;
			break;
		case 12:
		case 13:
			status = "creation";
			file->status = SC_FILE_STATUS_INVALIDATED;
			break;
		default:
			break;
		}
		sc_log(ctx,  "  file status: %s\n", status);
	}

	file->magic = SC_FILE_MAGIC;
	return SC_SUCCESS;
}

static int starcos_select_aid(sc_card_t *card,
			      u8 aid[16], size_t len,
			      sc_file_t **file_out)
{

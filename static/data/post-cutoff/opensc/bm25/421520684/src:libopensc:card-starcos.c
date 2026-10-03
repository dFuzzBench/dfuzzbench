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
	{ "3B:D0:96:FF:81:B1:FE:45:1F:07:2A", NULL, NULL, SC_CARD_TYPE_STARCOS_V3_4, 0, NULL },
	{ "3b:df:96:ff:81:31:fe:45:80:5b:44:45:2e:42:41:5f:53:43:33:35:32:81:05:b5", NULL, NULL, SC_CARD_TYPE_STARCOS_V3_5_ESIGN, 0, NULL },
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
	unsigned int    pin_encoding;
} starcos_ex_data;

/*
   This constant allows signing or
   decrypting with RSA keys up to 4096 bits.
*/
#define STARCOS3X_PROBE_APDU_LENGTH	512

#define PIN_ENCODING_DETERMINE	0
#define PIN_ENCODING_DEFAULT	SC_PIN_ENCODING_GLP

// known pin formats for StarCOS 3.x cards
#define PIN_FORMAT_F1			0x11
#define PIN_FORMAT_F2			0x12
#define PIN_FORMAT_RSA			0x1230
#define PIN_FORMAT_BCD			0x13
#define PIN_FORMAT_ASCII		0x14
#define PIN_FORMAT_PW_ASCII		0x21
// default is the Format 2 PIN Block which is GLP in OpenSC
#define PIN_FORMAT_DEFAULT		PIN_FORMAT_F2

#define CHECK_NOT_SUPPORTED_V3_4(card) \
	do { \
		if ((card)->type == SC_CARD_TYPE_STARCOS_V3_4) { \
			sc_log((card)->ctx,  \
				"not supported for STARCOS 3.4 cards"); \
			return SC_ERROR_NOT_SUPPORTED; \
		} \
	} while (0);

/* card type helpers */
#define IS_V34(card) card->type == SC_CARD_TYPE_STARCOS_V3_4 || card->type == SC_CARD_TYPE_STARCOS_V3_4_ESIGN
#define IS_V35(card) card->type == SC_CARD_TYPE_STARCOS_V3_5 || card->type == SC_CARD_TYPE_STARCOS_V3_5_ESIGN
#define IS_V3x(card) IS_V34(card) || IS_V35(card)

/* the starcos part */
static int starcos_match_card(sc_card_t *card)
{
	int i;

	i = _sc_match_atr(card, starcos_atrs, &card->type);
	if (i < 0)
		return 0;
	return 1;
}


typedef struct starcos_ctrl_ref_template_st {
	unsigned int 	transmission_format;
#if 0
	// not relevant values for now
	unsigned int	se_reference;
	unsigned int	ssec_initial_value;
#endif
} starcos_ctrl_ref_template;

// tags
#define TAG_STARCOS35_PIN_REFERENCE					0x88
#define TAG_STARCOS3X_SUPPORTED_SEC_MECHANISMS_tag		0x7B
#define TAG_STARCOS3X_CTRL_REF_TEMPLATE				0xA4
#define TAG_STARCOS3X_TRANSMISSION_FORMAT			0x89

static const char * starcos_ef_pwdd = "3F000015";
static const char * starcos_ef_keyd = "3F000013";

/**
 * Parses supported security mechanisms record data.
 * It returns SC_SUCCESS and the ctrl_ref_template structure data on success
 */
static int starcos_parse_supported_sec_mechanisms(struct sc_card *card, const unsigned char * buf, size_t buflen, starcos_ctrl_ref_template * ctrl_ref_template)
{
	struct sc_context *ctx = card->ctx;
	const unsigned char *supported_sec_mechanisms_tag = NULL;
	size_t taglen;

	LOG_FUNC_CALLED(ctx);

	supported_sec_mechanisms_tag = sc_asn1_find_tag(ctx, buf, buflen, TAG_STARCOS3X_SUPPORTED_SEC_MECHANISMS_tag, &taglen);
	if (supported_sec_mechanisms_tag != NULL && taglen >= 1)   {
		const unsigned char *tx_fmt_tag = NULL;
		const unsigned char *ctrl_ref_template_tag = NULL;
		size_t supported_sec_mechanisms_taglen = taglen;

		// control-reference template is either included in the supported security mechanisms tag or it can be the CRT tag itself (EF.PWDD)
		ctrl_ref_template_tag = sc_asn1_find_tag(ctx, supported_sec_mechanisms_tag, taglen, TAG_STARCOS3X_CTRL_REF_TEMPLATE, &taglen);
		if ( ctrl_ref_template_tag == NULL || taglen == 0 ) {
			ctrl_ref_template_tag = supported_sec_mechanisms_tag;
			taglen = supported_sec_mechanisms_taglen;
		}

		tx_fmt_tag = sc_asn1_find_tag(ctx, ctrl_ref_template_tag, taglen, TAG_STARCOS3X_TRANSMISSION_FORMAT, &taglen);
		if ( tx_fmt_tag != NULL && taglen >= 1 ) {
			ctrl_ref_template->transmission_format = *(tx_fmt_tag + 0);
			LOG_FUNC_RETURN(ctx, SC_SUCCESS);
		}
	}

	LOG_FUNC_RETURN(ctx, SC_ERROR_TEMPLATE_NOT_FOUND);
}

static int starcos_determine_pin_format34(sc_card_t *card, unsigned int * pin_format)
{
	struct sc_context *ctx = card->ctx;
	struct sc_path path;
	struct sc_file *file;
	unsigned char buf[256];
	int rv;
	int retval = SC_SUCCESS;
	int rec_no=1;

	LOG_FUNC_CALLED(ctx);

	sc_format_path(starcos_ef_pwdd, &path);
	rv = sc_select_file(card, &path, &file);
	LOG_TEST_RET(ctx, rv, "Cannot select EF.PWDD file");

	if ( (rv = sc_read_record(card, rec_no, 0, buf, sizeof(buf), SC_RECORD_BY_REC_NR)) > 0 ) {
		starcos_ctrl_ref_template ctrl_ref_template;
		memset((void*)&ctrl_ref_template, 0, sizeof(ctrl_ref_template));
		rv = starcos_parse_supported_sec_mechanisms(card, buf, rv, &ctrl_ref_template);
		if ( rv == SC_SUCCESS ) {
			*pin_format = ctrl_ref_template.transmission_format;
			sc_log(ctx, "Determined StarCOS 3.4 PIN format: 0x%x", *pin_format);
		} else {
			sc_log(ctx, "Failed to parse record %d of EF.PWD, err=%d", rec_no, rv);
			retval = rv;
		}
	} else {
		sc_log(ctx, "Failed to read record %d of EF.PWDD, err=%d", rec_no, rv);
		retval = rv;
	}

	sc_file_free(file);
	LOG_FUNC_RETURN(ctx, retval);
}

static int starcos_determine_pin_format35(sc_card_t *card, unsigned int * pin_format)
{
	struct sc_context *ctx = card->ctx;
	struct sc_path path;
	struct sc_file *file;
	unsigned char buf[256];
	int rv;
	int retval = SC_ERROR_RECORD_NOT_FOUND;
	int rec_no=1;
	starcos_ctrl_ref_template ctrl_ref_template;

	LOG_FUNC_CALLED(ctx);

	sc_format_path(starcos_ef_keyd, &path);
	rv = sc_select_file(card, &path, &file);
	LOG_TEST_RET(ctx, rv, "Cannot select EF.KEYD file");

	while ( (rv = sc_read_record(card, rec_no++, 0, buf, sizeof(buf), SC_RECORD_BY_REC_NR)) > 0 ) {
		if ( buf[0] != TAG_STARCOS35_PIN_REFERENCE ) continue;

		memset((void*)&ctrl_ref_template, 0, sizeof(ctrl_ref_template));
		rv = starcos_parse_supported_sec_mechanisms(card, buf, rv, &ctrl_ref_template);
		if ( rv == SC_SUCCESS ) {
			*pin_format = ctrl_ref_template.transmission_format;
			sc_log(ctx, "Determined StarCOS 3.5 PIN format: 0x%x", *pin_format);
			retval = rv;
			// assuming that all PINs and PUKs have the same transmission format
			break;
		} else {
			sc_log(ctx, "Failed to parse record %d of EF.KEYD, err=%d", rec_no-1, rv);
			retval = rv;
		}
	}

	sc_file_free(file);
	LOG_FUNC_RETURN(ctx, retval);
}

/**
 * Determine v3.x PIN encoding by parsing either
 * EF.PWDD (for v3.4) or EF.KEYD (for v3.5)
 *
 * It returns an OpenSC PIN encoding, using the default value on failure
 */
static unsigned int starcos_determine_pin_encoding(sc_card_t *card)
{
	unsigned int pin_format = PIN_FORMAT_DEFAULT;
	unsigned int encoding = PIN_ENCODING_DETERMINE;

	if ( IS_V34(card) ) {
		starcos_determine_pin_format34(card, &pin_format);
	} else if ( IS_V35(card) ) {
		starcos_determine_pin_format35(card, &pin_format);
	}

	switch (pin_format) {
	case PIN_FORMAT_PW_ASCII:
	case PIN_FORMAT_ASCII:
		encoding = SC_PIN_ENCODING_ASCII;
		break;
	case PIN_FORMAT_BCD:
		encoding = SC_PIN_ENCODING_BCD;
		break;
	case PIN_FORMAT_F1:
	case PIN_FORMAT_F2:
		encoding = SC_PIN_ENCODING_GLP;
		break;
	}

	sc_log(card->ctx, "Determined PIN encoding: %d", encoding);
	return encoding;
}

/**
 * Returns 1 if an extended APDU can be sent to the card
 * with the given card reader. Otherwise returns 0.
 */
static int starcos_probe_reader_for_ext_apdu(sc_card_t * card) {
	sc_apdu_t apdu;
	int rv;
	/* try to read STARCOS3X_PROBE_APDU_LENGTH bytes */
	u8 data[STARCOS3X_PROBE_APDU_LENGTH];

	/* Get Data: Get Chip Serial Number */
	sc_format_apdu(card, &apdu, SC_APDU_CASE_2_EXT, 0xCA, 0x9F, 0x6C);
	apdu.cla = 0xA0;
	apdu.resp = data;
	apdu.resplen = sizeof(data);
	apdu.le = apdu.resplen;
	rv = sc_transmit_apdu(card, &apdu);
	LOG_TEST_RET(card->ctx, rv, "Failed to send Get Data ext. APDU");
	return (apdu.sw1 == 0x90 && apdu.sw2 == 0x00);
}

static int starcos_select_mf(sc_card_t * card) {
	sc_apdu_t apdu;
	const u8 mf_buf[2] = {0x3f, 0x00};

	sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0xA4, 0x00, 0x0C);
	apdu.le = 0;
	apdu.lc = 2;
	apdu.data    = mf_buf;
	apdu.datalen = 2;
	apdu.resplen = 0;

	return sc_transmit_apdu(card, &apdu);
}

static int starcos_select_aid(sc_card_t *card,
			      const u8 aid[16], size_t len,
			      sc_file_t **file_out);

/* returns 1 if the card has the eSign app with AID A0:00:00:02:45:53:69:67:6E
   otherwise returns 0
 */
static int starcos_has_esign_app(sc_card_t * card) {
	static const char * starcos_esign_aid = "A0:00:00:02:45:53:69:67:6E";
	int rv;

	rv = starcos_select_mf(card);
	if ( rv == SC_SUCCESS ) {
		u8 aid[SC_MAX_PATH_SIZE];
		size_t len = sizeof(aid);

		rv = sc_hex_to_bin(starcos_esign_aid, aid, &len);
		LOG_TEST_RET(card->ctx, rv, "Failed to convert eSing AID");
		rv = starcos_select_aid(card, aid, len, NULL);
		if ( rv == SC_SUCCESS ) {
			starcos_select_mf(card);
		}
	}
	return ( rv == SC_SUCCESS );
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
	ex_data->pin_encoding = PIN_ENCODING_DETERMINE;

	flags = SC_ALGORITHM_RSA_PAD_PKCS1
		| SC_ALGORITHM_ONBOARD_KEY_GEN
		| SC_ALGORITHM_RSA_PAD_ISO9796
		| SC_ALGORITHM_RSA_HASH_NONE
		| SC_ALGORITHM_RSA_HASH_SHA1
		| SC_ALGORITHM_RSA_HASH_MD5
		| SC_ALGORITHM_RSA_HASH_RIPEMD160
		| SC_ALGORITHM_RSA_HASH_MD5_SHA1;

	card->caps = SC_CARD_CAP_RNG;

	if ( IS_V3x(card) ) {

		flags |= SC_CARD_FLAG_RNG
			| SC_ALGORITHM_RSA_HASH_SHA224
			| SC_ALGORITHM_RSA_HASH_SHA256
			| SC_ALGORITHM_RSA_HASH_SHA384
			| SC_ALGORITHM_RSA_HASH_SHA512
			| SC_ALGORITHM_RSA_PAD_PSS;

		_sc_card_add_rsa_alg(card, 512, flags, 0x10001);
		_sc_card_add_rsa_alg(card, 768, flags, 0x10001);
		_sc_card_add_rsa_alg(card,1024, flags, 0x10001);
		_sc_card_add_rsa_alg(card,1728, flags, 0x10001);
		_sc_card_add_rsa_alg(card,1976, flags, 0x10001);
		_sc_card_add_rsa_alg(card,2048, flags, 0x10001);
		if ( IS_V34(card) ) {
			card->name = "STARCOS 3.4";
			card->caps |= SC_CARD_CAP_ISO7816_PIN_INFO;
		} else {
			card->name = "STARCOS 3.5";
			_sc_card_add_rsa_alg(card,3072, flags, 0x10001);
		}
		card->max_send_size = 255;
		card->max_recv_size = 256;
	} else {
		_sc_card_add_rsa_alg(card, 512, flags, 0x10001);
		_sc_card_add_rsa_alg(card, 768, flags, 0x10001);
		_sc_card_add_rsa_alg(card,1024, flags, 0x10001);

		/* we need read_binary&friends with max 128 bytes per read */
		card->max_send_size = 128;
		card->max_recv_size = 128;
	}

	if (sc_parse_ef_atr(card) == SC_SUCCESS) {
		size_t max_recv_size = 0;
		size_t max_send_size = 0;

		/* Add max. length values from IAS/ECC specific issuer data */
		if ( card->ef_atr->issuer_data_len >= 4 ) {
			max_recv_size = bebytes2ushort(card->ef_atr->issuer_data);
			max_send_size = bebytes2ushort(card->ef_atr->issuer_data + 2);
		}
		/* which could be overridden with ISO7816 EF.ATR options, if present */
		if (card->ef_atr->max_response_apdu > 0) {
			max_recv_size = card->ef_atr->max_response_apdu;
		}
		if (card->ef_atr->max_command_apdu > 0) {
			max_send_size = card->ef_atr->max_command_apdu;
		}

		if ( max_send_size > 256 && max_recv_size > 256 ) {
			size_t max_recv_size_prev = card->max_recv_size;
			size_t max_send_size_prev = card->max_send_size;
			/* allow SC_CARD_CAP_APDU_EXT independent of ef_atr->caps, see IAS/ECC issuer data above */
			card->caps |= SC_CARD_CAP_APDU_EXT;
			/* the received data should not exceed max_recv_size including the sw1/sw2 */
			card->max_recv_size = max_recv_size - 2;
			/* the sent APDU should not exceed max_send_size including the 4 bytes of the APDU and 2 * 3 bytes Lc/Le */
			card->max_send_size = max_send_size - 10;
			/* probe reader for extended APDU support */
			if ( starcos_probe_reader_for_ext_apdu(card) ) {
				sc_log(card->ctx, "Successfully probed extended APDU, enabling extended APDU with max send/recv %d/%d",
					(int)card->max_send_size, (int)card->max_recv_size);
			} else {
				card->caps &= ~(SC_CARD_CAP_APDU_EXT);
				card->max_recv_size = max_recv_size_prev;
				card->max_send_size = max_send_size_prev;
				sc_log(card->ctx, "Ext APDU probing failed, the actual reader does not support ext APDU");
			}
		}
	}

	if ( ex_data->pin_encoding == PIN_ENCODING_DETERMINE ) {
		// about to determine PIN encoding
		ex_data->pin_encoding = starcos_determine_pin_encoding(card);
	}

	if ( card->type == SC_CARD_TYPE_STARCOS_V3_4 && starcos_has_esign_app(card) ) {
		card->type = SC_CARD_TYPE_STARCOS_V3_4_ESIGN;
		sc_log(card->ctx, "Card has eSign app, card type changed to %d", card->type);
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
			      const u8 aid[16], size_t len,
			      sc_file_t **file_out)
{
	sc_apdu_t apdu;
	int r;
	size_t i = 0;

	sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0xA4, 0x04, 0x0C);
	apdu.lc = len;
	apdu.data = (u8*)aid;
	apdu.datalen = len;
	apdu.resplen = 0;
	apdu.le = 0;
	r = sc_transmit_apdu(card, &apdu);
	LOG_TEST_RET(card->ctx, r, "APDU transmit failed");

	/* check return value */
	if (!(apdu.sw1 == 0x90 && apdu.sw2 == 0x00) && apdu.sw1 != 0x61 )
		SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, sc_check_sw(card, apdu.sw1, apdu.sw2));

	/* update cache */
	card->cache.current_path.type = SC_PATH_TYPE_DF_NAME;
	card->cache.current_path.len = len;
	memcpy(card->cache.current_path.value, aid, len);

	if (file_out) {
		sc_file_t *file = sc_file_new();
		if (!file)
			LOG_FUNC_RETURN(card->ctx, SC_ERROR_OUT_OF_MEMORY);
		file->type = SC_FILE_TYPE_DF;
		file->ef_structure = SC_FILE_EF_UNKNOWN;
		file->path.len = 0;
		file->size = 0;
		/* AID */
		for (i = 0; i < len; i++)
			file->name[i] = aid[i];
		file->namelen = len;
		file->id = 0x0000;
		file->magic = SC_FILE_MAGIC;

		*file_out = file;
	}
	SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, SC_SUCCESS);
}

static int starcos_select_fid(sc_card_t *card,
			      unsigned int id_hi, unsigned int id_lo,
			      sc_file_t **file_out, int is_file)
{
	sc_apdu_t apdu;
	u8 data[] = {id_hi & 0xff, id_lo & 0xff};
	u8 resp[SC_MAX_APDU_BUFFER_SIZE];
	int bIsDF = 0, r;
	int isFCP = 0;
	int isMF = 0;

	/* request FCI to distinguish between EFs and DFs */
	sc_format_apdu(card, &apdu, SC_APDU_CASE_4_SHORT, 0xA4, 0x00, 0x00);
	apdu.p2   = 0x00;
	apdu.resp = (u8*)resp;
	apdu.resplen = SC_MAX_APDU_BUFFER_SIZE;
	apdu.le = 256;
	apdu.lc = 2;
	apdu.data = (u8*)data;
	apdu.datalen = 2;

	if ( IS_V3x(card) ) {
		if (id_hi == 0x3f && id_lo == 0x0) {
			apdu.p1 = 0x0;
			apdu.p2 = 0x0;
			isMF = 1;
		} else if (file_out || is_file) {
			// last component (i.e. file or path)
			apdu.p1 = 0x2;
			apdu.p2 = 0x4;
		} else {
			// path component
			apdu.p1 = 0x1;
			apdu.p2 = 0x0;
		}
	}

	r = sc_transmit_apdu(card, &apdu);
	LOG_TEST_RET(card->ctx, r, "APDU transmit failed");

	if (apdu.p2 == 0x00 && apdu.sw1 == 0x62 && apdu.sw2 == 0x84 ) {
		/* no FCI => we have a DF (see comment in process_fci()) */
		bIsDF = 1;
		apdu.p2 = 0x0C;
		apdu.cse = SC_APDU_CASE_3_SHORT;
		apdu.resplen = 0;
		apdu.le = 0;
		r = sc_transmit_apdu(card, &apdu);
		LOG_TEST_RET(card->ctx, r, "APDU re-transmit failed");
	} else if ((IS_V3x(card))
			&& apdu.p2 == 0x4 && apdu.sw1 == 0x6a && apdu.sw2 == 0x82) {
		/* not a file, could be a path */
		bIsDF = 1;
		apdu.p1 = 0x1;
		apdu.p2 = 0x0;
		apdu.resplen = sizeof(resp);
		apdu.le = 256;
		apdu.lc = 2;
		r = sc_transmit_apdu(card, &apdu);
		LOG_TEST_RET(card->ctx, r, "APDU re-transmit failed");
	} else if (apdu.sw1 == 0x61 || (apdu.sw1 == 0x90 && apdu.sw2 == 0x00 && !isMF)) {
		/* SELECT returned some data (possible FCI) =>
		 * try a READ BINARY to see if a EF is selected */
		sc_apdu_t apdu2;
		u8 resp2[2];
		sc_format_apdu(card, &apdu2, SC_APDU_CASE_2_SHORT, 0xB0, 0, 0);
		apdu2.resp = (u8*)resp2;
		apdu2.resplen = 2;
		apdu2.le = 1;
		apdu2.lc = 0;
		r = sc_transmit_apdu(card, &apdu2);
		LOG_TEST_RET(card->ctx, r, "APDU transmit failed");
		if (apdu2.sw1 == 0x69 && apdu2.sw2 == 0x86) {
			/* no current EF is selected => we have a DF */
			bIsDF = 1;
		} else {
			isFCP = 1;
		}
	}

	if (apdu.sw1 != 0x61 && (apdu.sw1 != 0x90 || apdu.sw2 != 0x00))
		SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, sc_check_sw(card, apdu.sw1, apdu.sw2));

	/* update cache */
	if (bIsDF || isMF) {
		card->cache.current_path.type = SC_PATH_TYPE_PATH;
		card->cache.current_path.value[0] = 0x3f;
		card->cache.current_path.value[1] = 0x00;
		if (id_hi == 0x3f && id_lo == 0x00)
			card->cache.current_path.len = 2;
		else {
			card->cache.current_path.len = 4;
			card->cache.current_path.value[2] = id_hi;
			card->cache.current_path.value[3] = id_lo;
		}
	}

	if (file_out) {
		sc_file_t *file = sc_file_new();
		if (!file)
			LOG_FUNC_RETURN(card->ctx, SC_ERROR_OUT_OF_MEMORY);
		file->id   = (id_hi << 8) + id_lo;
		file->path = card->cache.current_path;

		if (bIsDF) {
			/* we have a DF */
			file->type = SC_FILE_TYPE_DF;
			file->ef_structure = SC_FILE_EF_UNKNOWN;
			file->size = 0;
			file->namelen = 0;
			file->magic = SC_FILE_MAGIC;
			*file_out = file;
		} else {
			/* ok, assume we have a EF */
			if ( IS_V3x(card) ) {
				if (isFCP) {
					r = process_fcp_v3_4(card->ctx, file, apdu.resp,
							apdu.resplen);
				} else {
					r = process_fci_v3_4(card->ctx, file, apdu.resp,
							apdu.resplen);
				}
			} else {
				r = process_fci(card->ctx, file, apdu.resp,
						apdu.resplen);
			}
			if (r != SC_SUCCESS) {
				sc_file_free(file);
				return r;
			}

			*file_out = file;
		}
	}

	SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, SC_SUCCESS);
}

static int starcos_select_file(sc_card_t *card,
			       const sc_path_t *in_path,
			       sc_file_t **file_out)
{
	u8 pathbuf[SC_MAX_PATH_SIZE], *path = pathbuf;
	int    r, pathtype;
	size_t i, pathlen;
	char pbuf[SC_MAX_PATH_STRING_SIZE];
	/* option for path caching, it is deactivated by default,
	   but it can be enabled by setting cache_valid to option: card->cache.valid */
	int    cache_valid = 0;

	SC_FUNC_CALLED(card->ctx, SC_LOG_DEBUG_VERBOSE);

	r = sc_path_print(pbuf, sizeof(pbuf), &card->cache.current_path);
	if (r != SC_SUCCESS)
		pbuf[0] = '\0';

	sc_log(card->ctx,
		 "current path (%s, %s): %s (len: %"SC_FORMAT_LEN_SIZE_T"u)\n",
		 card->cache.current_path.type == SC_PATH_TYPE_DF_NAME ?
		 "aid" : "path",
		 cache_valid ? "valid" : "invalid", pbuf,
		 card->cache.current_path.len);

	if ( in_path->len > sizeof(pathbuf) ) {
		SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, SC_ERROR_BUFFER_TOO_SMALL);
	}
	memcpy(path, in_path->value, in_path->len);
	pathlen = in_path->len;
	pathtype = in_path->type;

	if (in_path->aid.len) {
		if (!pathlen) {
			if ( in_path->aid.len > sizeof(pathbuf) ) {
				SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, SC_ERROR_BUFFER_TOO_SMALL);
			}
			memcpy(path, in_path->aid.value, in_path->aid.len);
			pathlen = in_path->aid.len;
			pathtype = SC_PATH_TYPE_DF_NAME;
		} else {
			if (!cache_valid
				|| card->cache.current_path.type != SC_PATH_TYPE_DF_NAME
				|| card->cache.current_path.len != pathlen
				|| memcmp(card->cache.current_path.value, in_path->aid.value, in_path->aid.len) != 0 ) {
				r = starcos_select_aid(card, in_path->aid.value, in_path->aid.len, NULL);
				LOG_TEST_RET(card->ctx, r, "Could not select AID!");
			}

			if (pathtype == SC_PATH_TYPE_DF_NAME) {
				pathtype = SC_PATH_TYPE_FILE_ID;
			}
		}
	}

	if (pathtype == SC_PATH_TYPE_FILE_ID)
	{	/* SELECT EF/DF with ID */
		/* Select with 2byte File-ID */
		if (pathlen != 2)
			SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE,SC_ERROR_INVALID_ARGUMENTS);
		r = starcos_select_fid(card, path[0], path[1], file_out, 1);
		SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, r);
	}
	else if (pathtype == SC_PATH_TYPE_DF_NAME)
	{	/* SELECT DF with AID */
		/* Select with 1-16byte Application-ID */
		if (cache_valid
		    && card->cache.current_path.type == SC_PATH_TYPE_DF_NAME
		    && card->cache.current_path.len == pathlen
		    && memcmp(card->cache.current_path.value, pathbuf, pathlen) == 0 )
		{
			sc_log(card->ctx,  "cache hit\n");
			SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, SC_SUCCESS);
		}
		else
		{
			r = starcos_select_aid(card, pathbuf, pathlen, file_out);
			SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, r);
		}
	}
	else if (pathtype == SC_PATH_TYPE_PATH)
	{
		u8 n_pathbuf[SC_MAX_PATH_SIZE];
		int bMatch = -1;

		/* Select with path (sequence of File-IDs) */
		/* Starcos (S 2.1 and SPK 2.3) only supports one
		 * level of subdirectories, therefore a path is
		 * at most 3 FID long (the last one being the FID
		 * of a EF) => pathlen must be even and less than 6
		 */
		if (pathlen%2 != 0 || pathlen > 6 || pathlen <= 0)
			SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, SC_ERROR_INVALID_ARGUMENTS);

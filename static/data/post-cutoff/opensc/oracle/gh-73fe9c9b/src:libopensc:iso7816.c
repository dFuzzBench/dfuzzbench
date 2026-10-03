/*
 * iso7816.c: Functions specified by the ISO 7816 standard
 *
 * Copyright (C) 2001, 2002  Juha Yrjölä <juha.yrjola@iki.fi>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "asn1.h"
#include "iso7816.h"
#include "sm/sm-iso.h"

void
iso7816_fixup_transceive_length(const struct sc_card *card,
		struct sc_apdu *apdu)
{
	if (card == NULL || apdu == NULL) {
		return;
	}

	if (apdu->lc > sc_get_max_send_size(card)) {
		/* The lower layers will automatically do chaining */
		apdu->flags |= SC_APDU_FLAGS_CHAINING;
	}

	if (apdu->le > sc_get_max_recv_size(card)) {
		/* The lower layers will automatically do a GET RESPONSE, if possible.
		 * All other workarounds must be carried out by the upper layers. */
		apdu->le = sc_get_max_recv_size(card);
	}
}


static const struct sc_card_error iso7816_errors[] = {
	{ 0x6200, SC_ERROR_CARD_CMD_FAILED,	"Warning: no information given, non-volatile memory is unchanged" },
	{ 0x6281, SC_ERROR_CORRUPTED_DATA,	"Part of returned data may be corrupted" },
	{ 0x6282, SC_ERROR_FILE_END_REACHED,	"End of file/record reached before reading Le bytes" },
	{ 0x6283, SC_ERROR_CARD_CMD_FAILED,	"Selected file invalidated" },
	{ 0x6284, SC_ERROR_CARD_CMD_FAILED,	"FCI not formatted according to ISO 7816-4" },
	{ 0x6285, SC_ERROR_CARD_CMD_FAILED,	"Selected file in termination state" },
	{ 0x6286, SC_ERROR_CARD_CMD_FAILED,	"No input data available from a sensor on the card" },

	{ 0x6300, SC_ERROR_CARD_CMD_FAILED,	"Warning: no information given, non-volatile memory has changed" },
	{ 0x6381, SC_ERROR_CARD_CMD_FAILED,	"Warning: file filled up by last write" },

	{ 0x6400, SC_ERROR_CARD_CMD_FAILED,	"Execution error" },
	{ 0x6401, SC_ERROR_CARD_CMD_FAILED,	"Immediate response required by the card" },

	{ 0x6581, SC_ERROR_MEMORY_FAILURE,	"Memory failure" },

	{ 0x6700, SC_ERROR_WRONG_LENGTH,	"Wrong length" },

	{ 0x6800, SC_ERROR_NO_CARD_SUPPORT,	"Functions in CLA not supported" },
	{ 0x6881, SC_ERROR_NO_CARD_SUPPORT,	"Logical channel not supported" },
	{ 0x6882, SC_ERROR_NO_CARD_SUPPORT,	"Secure messaging not supported" },
	{ 0x6883, SC_ERROR_CARD_CMD_FAILED,	"Last command of the chain expected" },
	{ 0x6884, SC_ERROR_NO_CARD_SUPPORT,	"Command chaining not supported" },

	{ 0x6900, SC_ERROR_NOT_ALLOWED,		"Command not allowed" },
	{ 0x6981, SC_ERROR_CARD_CMD_FAILED,	"Command incompatible with file structure" },
	{ 0x6982, SC_ERROR_SECURITY_STATUS_NOT_SATISFIED,"Security status not satisfied" },
	{ 0x6983, SC_ERROR_AUTH_METHOD_BLOCKED,	"Authentication method blocked" },
	{ 0x6984, SC_ERROR_REF_DATA_NOT_USABLE,	"Referenced data not usable" },
	{ 0x6985, SC_ERROR_NOT_ALLOWED,		"Conditions of use not satisfied" },
	{ 0x6986, SC_ERROR_NOT_ALLOWED,		"Command not allowed (no current EF)" },
	{ 0x6987, SC_ERROR_INCORRECT_PARAMETERS,"Expected SM data objects missing" },
	{ 0x6988, SC_ERROR_INCORRECT_PARAMETERS,"Incorrect SM data objects" },

	{ 0x6A00, SC_ERROR_INCORRECT_PARAMETERS,"Wrong parameter(s) P1-P2" },
	{ 0x6A80, SC_ERROR_INCORRECT_PARAMETERS,"Incorrect parameters in the data field" },
	{ 0x6A81, SC_ERROR_NO_CARD_SUPPORT,	"Function not supported" },
	{ 0x6A82, SC_ERROR_FILE_NOT_FOUND,	"File or application not found" },
	{ 0x6A83, SC_ERROR_RECORD_NOT_FOUND,	"Record not found" },
	{ 0x6A84, SC_ERROR_NOT_ENOUGH_MEMORY,	"Not enough memory space in the file" },
	{ 0x6A85, SC_ERROR_INCORRECT_PARAMETERS,"Lc inconsistent with TLV structure" },
	{ 0x6A86, SC_ERROR_INCORRECT_PARAMETERS,"Incorrect parameters P1-P2" },
	{ 0x6A87, SC_ERROR_INCORRECT_PARAMETERS,"Lc inconsistent with P1-P2" },
	{ 0x6A88, SC_ERROR_DATA_OBJECT_NOT_FOUND,"Referenced data not found" },
	{ 0x6A89, SC_ERROR_FILE_ALREADY_EXISTS,	"File already exists"},
	{ 0x6A8A, SC_ERROR_FILE_ALREADY_EXISTS,	"DF name already exists"},

	{ 0x6B00, SC_ERROR_INCORRECT_PARAMETERS,"Wrong parameter(s) P1-P2" },
	{ 0x6D00, SC_ERROR_INS_NOT_SUPPORTED,	"Instruction code not supported or invalid" },
	{ 0x6E00, SC_ERROR_CLASS_NOT_SUPPORTED,	"Class not supported" },
	{ 0x6F00, SC_ERROR_CARD_CMD_FAILED,	"No precise diagnosis" },
};


static int
iso7816_check_sw(struct sc_card *card, unsigned int sw1, unsigned int sw2)
{
	const int err_count = sizeof(iso7816_errors)/sizeof(iso7816_errors[0]);
	int i;

	/* Handle special cases here */
	if (sw1 == 0x6C) {
		sc_log(card->ctx, "Wrong length; correct length is %d", sw2);
		return SC_ERROR_WRONG_LENGTH;
	}
	if (sw1 == 0x90 && sw2 == 0x00)
		return SC_SUCCESS;
	if (sw1 == 0x63U && (sw2 & ~0x0fU) == 0xc0U) {
		sc_log(card->ctx, "PIN not verified (remaining tries: %d)", (sw2 & 0x0f));
		return SC_ERROR_PIN_CODE_INCORRECT;
	}
	for (i = 0; i < err_count; i++)   {
		if (iso7816_errors[i].SWs == ((sw1 << 8) | sw2)) {
			sc_log(card->ctx, "%s", iso7816_errors[i].errorstr);
			return iso7816_errors[i].errorno;
		}
	}

	sc_log(card->ctx, "Unknown SWs; SW1=%02X, SW2=%02X", sw1, sw2);
	return SC_ERROR_CARD_CMD_FAILED;
}


static int
iso7816_read_binary(struct sc_card *card, unsigned int idx, u8 *buf, size_t count, unsigned long *flags)
{
	struct sc_context *ctx = card->ctx;
	struct sc_apdu apdu;
	int r;

	if (idx > 0x7FFF) {
		sc_log(ctx, "invalid EF offset: 0x%X > 0x7FFF", idx);
		return SC_ERROR_OFFSET_TOO_LARGE;
	}

	sc_format_apdu(card, &apdu, SC_APDU_CASE_2, 0xB0, (idx >> 8) & 0x7F, idx & 0xFF);
	apdu.le = count;
	apdu.resplen = count;
	apdu.resp = buf;

	iso7816_fixup_transceive_length(card, &apdu);
	r = sc_transmit_apdu(card, &apdu);
	LOG_TEST_RET(ctx, r, "APDU transmit failed");

	r = sc_check_sw(card, apdu.sw1, apdu.sw2);
	if (r == SC_ERROR_FILE_END_REACHED || r == SC_ERROR_INCORRECT_PARAMETERS)
		LOG_FUNC_RETURN(ctx, (int)apdu.resplen);
	LOG_TEST_RET(ctx, r, "Check SW error");

	LOG_FUNC_RETURN(ctx, (int)apdu.resplen);
}


const struct sc_asn1_entry c_asn1_do_data[] = {
	{ "Offset Data Object", SC_ASN1_OCTET_STRING, SC_ASN1_APP|0x14, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "Discretionary Data Object", SC_ASN1_OCTET_STRING, SC_ASN1_APP|0x13, SC_ASN1_ALLOC|SC_ASN1_UNSIGNED|SC_ASN1_OPTIONAL, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL }
};

int encode_do_data(struct sc_context *ctx,
		unsigned int idx, const unsigned char *data, size_t data_len,
		u8 **out, size_t *outlen)
{
	unsigned char offset_buffer[2];
	size_t offset_buffer_len = sizeof offset_buffer;
	struct sc_asn1_entry asn1_do_data[sizeof c_asn1_do_data / sizeof *c_asn1_do_data];
	sc_copy_asn1_entry(c_asn1_do_data, asn1_do_data);

	if (idx > 0xFFFF)
		LOG_TEST_RET(ctx, SC_ERROR_INTERNAL, "Offset beyond 0xFFFF not supported");
	offset_buffer[0] = (u8) (idx >> 8);
	offset_buffer[1] = (u8) (idx & 0x00FF);
	sc_format_asn1_entry(asn1_do_data + 0, offset_buffer, &offset_buffer_len, 1);

	if (data && data_len) {
		sc_format_asn1_entry(asn1_do_data + 1, (void *) &data, &data_len, 1);
	} else {
		sc_format_asn1_entry(asn1_do_data + 1, NULL, NULL, 0);
	}

	LOG_TEST_RET(ctx,
			sc_asn1_encode(ctx, asn1_do_data, out, outlen),
			"sc_asn1_encode() failed");

	return SC_SUCCESS;
}

int decode_do_data(struct sc_context *ctx,
		const unsigned char *encoded_data, size_t encoded_data_len,
		u8 **out, size_t *outlen)
{
	struct sc_asn1_entry asn1_do_data[sizeof c_asn1_do_data / sizeof *c_asn1_do_data];
	sc_copy_asn1_entry(c_asn1_do_data, asn1_do_data);

	sc_format_asn1_entry(asn1_do_data + 0, NULL, NULL, 0);
	sc_format_asn1_entry(asn1_do_data + 1, out, outlen, 0);

	LOG_TEST_RET(ctx,
			sc_asn1_decode(ctx, asn1_do_data, encoded_data, encoded_data_len, NULL, NULL),
			"sc_asn1_decode() failed");

	if (!(asn1_do_data[1].flags & SC_ASN1_PRESENT))
		return SC_ERROR_INVALID_ASN1_OBJECT;

	return SC_SUCCESS;
}

static int
iso7816_read_record(struct sc_card *card, unsigned int rec_nr, unsigned int idx,
		u8 *buf, size_t count, unsigned long flags)
{
	struct sc_apdu apdu;
	int r;
	/* XXX maybe use some bigger buffer */
	unsigned char resp[SC_MAX_APDU_RESP_SIZE];
	unsigned char *encoded_data = NULL, *decoded_data = NULL;
	size_t encoded_data_len = 0, decoded_data_len = 0;

	if (rec_nr > 0xFF)
		LOG_FUNC_RETURN(card->ctx, SC_ERROR_INVALID_ARGUMENTS);

	if (idx == 0) {
		sc_format_apdu(card, &apdu, SC_APDU_CASE_2, 0xB2, rec_nr, 0);
		apdu.le = count;
		apdu.resplen = count;
		apdu.resp = buf;
	} else {
		r = encode_do_data(card->ctx, idx, NULL, 0, &encoded_data, &encoded_data_len);
		LOG_TEST_GOTO_ERR(card->ctx, r, "Could not encode data objects");

		sc_format_apdu(card, &apdu, SC_APDU_CASE_4, 0xB3, rec_nr, 0);
		apdu.lc = encoded_data_len;
		apdu.datalen = encoded_data_len;
		apdu.data = encoded_data;
		apdu.le = sizeof resp;
		apdu.resplen = sizeof resp;
		apdu.resp = resp;
	}
	apdu.p2 = (flags & SC_RECORD_EF_ID_MASK) << 3;
	if (flags & SC_RECORD_BY_REC_NR)
		apdu.p2 |= 0x04;

	iso7816_fixup_transceive_length(card, &apdu);
	r = sc_transmit_apdu(card, &apdu);
	LOG_TEST_GOTO_ERR(card->ctx, r, "APDU transmit failed");
	r = sc_check_sw(card, apdu.sw1, apdu.sw2);
	LOG_TEST_GOTO_ERR(card->ctx, r, "Card returned error");

	if (idx == 0) {
		r = (int)apdu.resplen;
	} else {
		r = decode_do_data(card->ctx, apdu.resp, apdu.resplen,
				&decoded_data, &decoded_data_len);
		LOG_TEST_GOTO_ERR(card->ctx, r, "Could not decode data objects");
		if (decoded_data_len <= count) {
			count = decoded_data_len;
		}
		memcpy(buf, decoded_data, count);
		r = (int)count;
	}

err:
	free(encoded_data);
	free(decoded_data);
	LOG_FUNC_RETURN(card->ctx, r);
}


static int
iso7816_write_record(struct sc_card *card, unsigned int rec_nr,
		const u8 *buf, size_t count, unsigned long flags)
{
	struct sc_apdu apdu;
	int r;

	sc_format_apdu(card, &apdu, SC_APDU_CASE_3, 0xD2, rec_nr, 0);
	apdu.lc = count;
	apdu.datalen = count;
	apdu.data = buf;
	apdu.p2 = (flags & SC_RECORD_EF_ID_MASK) << 3;
	if (flags & SC_RECORD_BY_REC_NR)
		apdu.p2 |= 0x04;

	iso7816_fixup_transceive_length(card, &apdu);
	r = sc_transmit_apdu(card, &apdu);
	LOG_TEST_RET(card->ctx, r, "APDU transmit failed");
	r = sc_check_sw(card, apdu.sw1, apdu.sw2);
	LOG_TEST_RET(card->ctx, r, "Card returned error");

	LOG_FUNC_RETURN(card->ctx, (int)count);
}


static int
iso7816_append_record(struct sc_card *card,
		const u8 *buf, size_t count, unsigned long flags)
{
	struct sc_apdu apdu;
	int r;

	sc_format_apdu(card, &apdu, SC_APDU_CASE_3, 0xE2, 0, 0);
	apdu.lc = count;
	apdu.datalen = count;
	apdu.data = buf;
	apdu.p2 = (flags & SC_RECORD_EF_ID_MASK) << 3;

	iso7816_fixup_transceive_length(card, &apdu);
	r = sc_transmit_apdu(card, &apdu);
	LOG_TEST_RET(card->ctx, r, "APDU transmit failed");
	r = sc_check_sw(card, apdu.sw1, apdu.sw2);
	LOG_TEST_RET(card->ctx, r, "Card returned error");

	LOG_FUNC_RETURN(card->ctx, (int)count);
}


static int
iso7816_update_record(struct sc_card *card, unsigned int rec_nr, unsigned int idx,
		const u8 *buf, size_t count, unsigned long flags)
{
	struct sc_apdu apdu;
	int r;
	unsigned char *encoded_data = NULL;
	size_t encoded_data_len = 0;

	if (rec_nr > 0xFF)
		LOG_FUNC_RETURN(card->ctx, SC_ERROR_INVALID_ARGUMENTS);

	if (idx == 0) {
		sc_format_apdu(card, &apdu, SC_APDU_CASE_3, 0xDC, rec_nr, 0);
		apdu.lc = count;
		apdu.datalen = count;
		apdu.data = buf;
	} else {
		r = encode_do_data(card->ctx, idx, buf, count, &encoded_data, &encoded_data_len);
		LOG_TEST_GOTO_ERR(card->ctx, r, "Could not encode data objects");

		sc_format_apdu(card, &apdu, SC_APDU_CASE_3, 0xDD, rec_nr, 0);
		apdu.lc = encoded_data_len;
		apdu.datalen = encoded_data_len;
		apdu.data = encoded_data;
	}
	apdu.p2 = (flags & SC_RECORD_EF_ID_MASK) << 3;
	if (flags & SC_RECORD_BY_REC_NR)
		apdu.p2 |= 0x04;

	iso7816_fixup_transceive_length(card, &apdu);
	r = sc_transmit_apdu(card, &apdu);
	LOG_TEST_GOTO_ERR(card->ctx, r, "APDU transmit failed");
	r = sc_check_sw(card, apdu.sw1, apdu.sw2);
	LOG_TEST_GOTO_ERR(card->ctx, r, "Card returned error");
	r = (int)count;

err:
	free(encoded_data);
	LOG_FUNC_RETURN(card->ctx, r);
}

static int
iso7816_write_binary(struct sc_card *card,
		unsigned int idx, const u8 *buf, size_t count, unsigned long flags)
{
	struct sc_apdu apdu;
	int r;

	if (idx > 0x7fff) {
		sc_log(card->ctx, "invalid EF offset: 0x%X > 0x7FFF", idx);
		return SC_ERROR_OFFSET_TOO_LARGE;
	}

	sc_format_apdu(card, &apdu, SC_APDU_CASE_3, 0xD0,
		       (idx >> 8) & 0x7F, idx & 0xFF);
	apdu.lc = count;
	apdu.datalen = count;
	apdu.data = buf;

	iso7816_fixup_transceive_length(card, &apdu);
	r = sc_transmit_apdu(card, &apdu);
	LOG_TEST_RET(card->ctx, r, "APDU transmit failed");
	r = sc_check_sw(card, apdu.sw1, apdu.sw2);
	LOG_TEST_RET(card->ctx, r, "Card returned error");

	LOG_FUNC_RETURN(card->ctx, (int)count);
}


static int
iso7816_update_binary(struct sc_card *card,
		unsigned int idx, const u8 *buf, size_t count, unsigned long flags)
{
	struct sc_apdu apdu;
	int r;

	if (idx > 0x7fff) {
		sc_log(card->ctx, "invalid EF offset: 0x%X > 0x7FFF", idx);
		return SC_ERROR_OFFSET_TOO_LARGE;
	}

	sc_format_apdu(card, &apdu, SC_APDU_CASE_3, 0xD6, (idx >> 8) & 0x7F, idx & 0xFF);
	apdu.lc = count;
	apdu.datalen = count;
	apdu.data = buf;

	iso7816_fixup_transceive_length(card, &apdu);
	r = sc_transmit_apdu(card, &apdu);
	LOG_TEST_RET(card->ctx, r, "APDU transmit failed");
	r = sc_check_sw(card, apdu.sw1, apdu.sw2);
	LOG_TEST_RET(card->ctx, r, "Card returned error");

	LOG_FUNC_RETURN(card->ctx, (int)count);
}


static int
iso7816_process_fci(struct sc_card *card, struct sc_file *file,
		const unsigned char *buf, size_t buflen)
{
	struct sc_context *ctx = card->ctx;
	const unsigned char *p, *end;
	unsigned int cla = 0, tag = 0;
	size_t length;

	file->status = SC_FILE_STATUS_UNKNOWN;

	for (p = buf, length = buflen, end = buf + buflen;
			p < end;
			p += length, length = end - p) {

		if (SC_SUCCESS != sc_asn1_read_tag(&p, length, &cla, &tag, &length)
				|| p == NULL) {
			break;
		}
		switch (cla | tag) {
			case 0x81:
				if (file->size != 0) {
					/* don't overwrite existing file size excluding structural information */
					break;
				}
				/* fall through */
			case 0x80:
				/* determine the file size */
				file->size = 0;
				if (p && length <= sizeof(size_t)) {
					size_t size = 0, i;
					for (i = 0; i < length; i++) {
						size <<= 8;
						size |= p[i];
					}
					if (size > MAX_FILE_SIZE) {
						file->size = MAX_FILE_SIZE;
						sc_log(ctx, "  file size truncated, encoded length: %"SC_FORMAT_LEN_SIZE_T"u", size);
					} else {
						file->size = size;
					}
				}

				sc_log(ctx, "  bytes in file: %"SC_FORMAT_LEN_SIZE_T"u", file->size);
				break;

			case 0x82:
				if (length > 0) {
					unsigned char byte = p[0];
					const char *type;

					file->shareable = byte & 0x40 ? 1 : 0;
					sc_log(ctx, "  shareable: %s", (byte & 0x40) ? "yes" : "no");
					file->ef_structure = byte & 0x07;
					switch ((byte >> 3) & 7) {
						case 0:
							type = "working EF";
							file->type = SC_FILE_TYPE_WORKING_EF;
							break;
						case 1:
							type = "internal EF";
							file->type = SC_FILE_TYPE_INTERNAL_EF;
							break;
						case 7:
							type = "DF";
							file->type = SC_FILE_TYPE_DF;
							break;
						default:
							file->type = SC_FILE_TYPE_UNKNOWN;
							type = "unknown";
							break;
					}
					sc_log(ctx, "  type: %s", type);
					sc_log(ctx, "  EF structure: %d", byte & 0x07);
					sc_log(ctx, "  tag 0x82: 0x%02x", byte);

					/* if possible, get additional information for non-DFs */
					if (file->type != SC_FILE_TYPE_DF) {
						/* max. record length for fixed- & variable-sized records */
						if (length > 2 && byte & 0x06) {
							file->record_length = (length > 3)
								? bebytes2ushort(p+2)
								: p[2];
							sc_log(ctx, "  record length: %"SC_FORMAT_LEN_SIZE_T"u",
								file->record_length);
						}

						/* number of records */
						if (length > 4) {
							file->record_count = (length > 5)
								? bebytes2ushort(p+4)
								: p[4];
							sc_log(ctx, "  records: %"SC_FORMAT_LEN_SIZE_T"u",
								file->record_count);
						}
					}


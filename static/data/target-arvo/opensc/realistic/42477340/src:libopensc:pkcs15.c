/*
 * pkcs15.c: PKCS #15 general functions
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>

#include "cardctl.h"
#include "internal.h"
#include "pkcs15.h"
#include "asn1.h"
#include "common/libscdl.h"

#ifdef ENABLE_OPENSSL
#include <openssl/sha.h>
#endif

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

static const struct sc_asn1_entry c_asn1_twlabel[] = {
	{ "twlabel", SC_ASN1_UTF8STRING, SC_ASN1_TAG_UTF8STRING, 0, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL }
};

static const struct sc_asn1_entry c_asn1_algorithm_info[7] = {
	{ "reference",		SC_ASN1_INTEGER,	SC_ASN1_TAG_INTEGER,	0, NULL, NULL },
	{ "algorithmPKCS#11",	SC_ASN1_INTEGER,	SC_ASN1_TAG_INTEGER,	0, NULL, NULL },
	{ "parameters",		SC_ASN1_CHOICE,		0,			0, NULL, NULL },
	{ "supportedOperations",SC_ASN1_BIT_FIELD,	SC_ASN1_TAG_BIT_STRING,	0, NULL, NULL },
	{ "objId",		SC_ASN1_OBJECT,		SC_ASN1_TAG_OBJECT,	SC_ASN1_OPTIONAL, NULL, NULL },
	{ "algRef",		SC_ASN1_INTEGER,	SC_ASN1_TAG_INTEGER,	SC_ASN1_OPTIONAL, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL }
};

static const struct sc_asn1_entry c_asn1_algorithm_info_parameters[3] = {
	{ "PKCS15RSAParameters",SC_ASN1_NULL,		SC_ASN1_TAG_NULL,	0, NULL, NULL },
	{ "PKCS15ECParameters",	SC_ASN1_OBJECT,		SC_ASN1_TAG_OBJECT,	0, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL }
};

/*
 * in src/libopensc/types.h SC_MAX_SUPPORTED_ALGORITHMS  defined as 8
 */
static const struct sc_asn1_entry c_asn1_supported_algorithms[SC_MAX_SUPPORTED_ALGORITHMS + 1] = {
	{ "algorithmInfo", SC_ASN1_STRUCT, SC_ASN1_TAG_SEQUENCE | SC_ASN1_CONS, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "algorithmInfo", SC_ASN1_STRUCT, SC_ASN1_TAG_SEQUENCE | SC_ASN1_CONS, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "algorithmInfo", SC_ASN1_STRUCT, SC_ASN1_TAG_SEQUENCE | SC_ASN1_CONS, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "algorithmInfo", SC_ASN1_STRUCT, SC_ASN1_TAG_SEQUENCE | SC_ASN1_CONS, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "algorithmInfo", SC_ASN1_STRUCT, SC_ASN1_TAG_SEQUENCE | SC_ASN1_CONS, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "algorithmInfo", SC_ASN1_STRUCT, SC_ASN1_TAG_SEQUENCE | SC_ASN1_CONS, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "algorithmInfo", SC_ASN1_STRUCT, SC_ASN1_TAG_SEQUENCE | SC_ASN1_CONS, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "algorithmInfo", SC_ASN1_STRUCT, SC_ASN1_TAG_SEQUENCE | SC_ASN1_CONS, SC_ASN1_OPTIONAL, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL }
};

#define C_ASN1_LAST_UPDATE_SIZE 3
static const struct sc_asn1_entry c_asn1_last_update[C_ASN1_LAST_UPDATE_SIZE] = {
	{ "generalizedTime",	SC_ASN1_GENERALIZEDTIME, SC_ASN1_TAG_GENERALIZEDTIME,	SC_ASN1_OPTIONAL, NULL, NULL },
	{ "referencedTime",	SC_ASN1_PATH, SC_ASN1_TAG_SEQUENCE | SC_ASN1_CONS,	SC_ASN1_OPTIONAL, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL }
};

#define C_ASN1_PROFILE_INDICATION_SIZE 3
static const struct sc_asn1_entry c_asn1_profile_indication[C_ASN1_PROFILE_INDICATION_SIZE] = {
	{ "profileOID",		SC_ASN1_OBJECT,		SC_ASN1_TAG_OBJECT,     SC_ASN1_OPTIONAL, NULL, NULL },
	{ "profileName",	SC_ASN1_UTF8STRING,	SC_ASN1_TAG_UTF8STRING, SC_ASN1_OPTIONAL, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL }
};

#define C_ASN1_TOKI_ATTRS_SIZE 15
static const struct sc_asn1_entry c_asn1_toki_attrs[C_ASN1_TOKI_ATTRS_SIZE] = {
	{ "version",	    SC_ASN1_INTEGER,		SC_ASN1_TAG_INTEGER, 0, NULL, NULL },
	{ "serialNumber",   SC_ASN1_OCTET_STRING,	SC_ASN1_TAG_OCTET_STRING, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "manufacturerID", SC_ASN1_UTF8STRING,		SC_ASN1_TAG_UTF8STRING, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "label",	    SC_ASN1_UTF8STRING,		SC_ASN1_CTX | 0, SC_ASN1_OPTIONAL, NULL, NULL },
	/* XXX the Taiwanese ID card erroneously uses explicit tagging */
	{ "label-tw",       SC_ASN1_STRUCT,		SC_ASN1_CTX | 0 | SC_ASN1_CONS, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "tokenflags",	    SC_ASN1_BIT_FIELD,		SC_ASN1_TAG_BIT_STRING, 0, NULL, NULL },
	{ "seInfo",	    SC_ASN1_SE_INFO,		SC_ASN1_CONS | SC_ASN1_TAG_SEQUENCE, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "recordInfo",	    SC_ASN1_STRUCT,		SC_ASN1_CONS | SC_ASN1_CTX | 1, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "supportedAlgorithms", SC_ASN1_STRUCT,	SC_ASN1_CONS | SC_ASN1_CTX | 2, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "issuerId",       SC_ASN1_UTF8STRING,		SC_ASN1_CTX | 3, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "holderId",       SC_ASN1_UTF8STRING,		SC_ASN1_CTX | 4, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "lastUpdate",     SC_ASN1_STRUCT,		SC_ASN1_CONS | SC_ASN1_CTX | 5, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "preferredLanguage", SC_ASN1_PRINTABLESTRING,	SC_ASN1_TAG_PRINTABLESTRING, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "profileIndication", SC_ASN1_STRUCT,		SC_ASN1_CONS | SC_ASN1_CTX | 6, SC_ASN1_OPTIONAL, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL }
};

static const struct sc_asn1_entry c_asn1_tokeninfo[] = {
	{ "TokenInfo", SC_ASN1_STRUCT, SC_ASN1_CONS | SC_ASN1_TAG_SEQUENCE, 0, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL }
};

static void sc_pkcs15_free_unusedspace(struct sc_pkcs15_card *);
static void sc_pkcs15_remove_dfs(struct sc_pkcs15_card *);
static void sc_pkcs15_remove_objects(struct sc_pkcs15_card *);
static int sc_pkcs15_aux_get_md_guid(struct sc_pkcs15_card *, const struct sc_pkcs15_object *,
		unsigned, unsigned char *, size_t *);

int sc_pkcs15_parse_tokeninfo(sc_context_t *ctx,
	sc_pkcs15_tokeninfo_t *ti, const u8 *buf, size_t blen)
{
	int r;
	size_t ii;
	u8 serial[128];
	size_t serial_len = sizeof(serial);
	u8 mnfid[SC_PKCS15_MAX_LABEL_SIZE];
	size_t mnfid_len  = sizeof(mnfid) - 1;
	u8 label[SC_PKCS15_MAX_LABEL_SIZE];
	size_t label_len = sizeof(label) - 1;
	u8 last_update[32], profile_indication[SC_PKCS15_MAX_LABEL_SIZE];
	size_t lupdate_len = sizeof(last_update) - 1, pi_len = sizeof(profile_indication) - 1;
	size_t flags_len   = sizeof(ti->flags);
	u8 preferred_language[3];
	size_t lang_length = sizeof(preferred_language);
	struct sc_asn1_entry asn1_supported_algorithms[SC_MAX_SUPPORTED_ALGORITHMS + 1],
			asn1_algo_infos[SC_MAX_SUPPORTED_ALGORITHMS][7],
			asn1_algo_infos_parameters[SC_MAX_SUPPORTED_ALGORITHMS][3];
	size_t reference_len = sizeof(ti->supported_algos[0].reference);
	size_t mechanism_len = sizeof(ti->supported_algos[0].mechanism);
	size_t parameter_len = sizeof(ti->supported_algos[0].parameters);
	size_t operations_len = sizeof(ti->supported_algos[0].operations);
	size_t algo_ref_len = sizeof(ti->supported_algos[0].algo_ref);

	struct sc_asn1_entry asn1_last_update[C_ASN1_LAST_UPDATE_SIZE];
	struct sc_asn1_entry asn1_profile_indication[C_ASN1_PROFILE_INDICATION_SIZE];
	struct sc_asn1_entry asn1_toki_attrs[C_ASN1_TOKI_ATTRS_SIZE], asn1_tokeninfo[3], asn1_twlabel[3];

	memset(last_update, 0, sizeof(last_update));
	memset(label, 0, sizeof(label));
	memset(profile_indication, 0, sizeof(profile_indication));
	memset(mnfid, 0, sizeof(mnfid));

	sc_copy_asn1_entry(c_asn1_twlabel, asn1_twlabel);
	sc_copy_asn1_entry(c_asn1_toki_attrs, asn1_toki_attrs);
	sc_copy_asn1_entry(c_asn1_tokeninfo, asn1_tokeninfo);
	sc_copy_asn1_entry(c_asn1_last_update, asn1_last_update);
	sc_format_asn1_entry(asn1_twlabel, label, &label_len, 0);
	sc_copy_asn1_entry(c_asn1_profile_indication, asn1_profile_indication);

	for (ii=0; ii<SC_MAX_SUPPORTED_ALGORITHMS; ii++) {
		sc_copy_asn1_entry(c_asn1_algorithm_info, asn1_algo_infos[ii]);
		sc_copy_asn1_entry(c_asn1_algorithm_info_parameters,
			asn1_algo_infos_parameters[ii]);
	}
	sc_copy_asn1_entry(c_asn1_supported_algorithms, asn1_supported_algorithms);

	for (ii=0; ii<SC_MAX_SUPPORTED_ALGORITHMS; ii++)   {
		sc_format_asn1_entry(asn1_algo_infos[ii] + 0, &ti->supported_algos[ii].reference, &reference_len, 0);
		sc_format_asn1_entry(asn1_algo_infos[ii] + 1, &ti->supported_algos[ii].mechanism, &mechanism_len, 0);
		sc_format_asn1_entry(asn1_algo_infos[ii] + 2,
			asn1_algo_infos_parameters[ii], NULL, 0);
		sc_format_asn1_entry(asn1_algo_infos_parameters[ii] + 0,
			NULL, NULL, 0);
		sc_format_asn1_entry(asn1_algo_infos_parameters[ii] + 1,
			&ti->supported_algos[ii].parameters, &parameter_len, 0);
		sc_format_asn1_entry(asn1_algo_infos[ii] + 3, &ti->supported_algos[ii].operations, &operations_len, 0);
		sc_format_asn1_entry(asn1_algo_infos[ii] + 4, &ti->supported_algos[ii].algo_id, NULL, 1);
		sc_format_asn1_entry(asn1_algo_infos[ii] + 5, &ti->supported_algos[ii].algo_ref, &algo_ref_len, 0);
		sc_format_asn1_entry(asn1_supported_algorithms + ii, asn1_algo_infos[ii], NULL, 0);
	}

	sc_format_asn1_entry(asn1_last_update + 0, last_update, &lupdate_len, 0);
	sc_format_asn1_entry(asn1_last_update + 1, &ti->last_update.path, NULL, 0);

	sc_format_asn1_entry(asn1_profile_indication + 0, &ti->profile_indication.oid, NULL, 0);
	sc_format_asn1_entry(asn1_profile_indication + 1, profile_indication, &pi_len, 0);

	sc_format_asn1_entry(asn1_toki_attrs + 0, &ti->version, NULL, 0);
	sc_format_asn1_entry(asn1_toki_attrs + 1, serial, &serial_len, 0);
	sc_format_asn1_entry(asn1_toki_attrs + 2, mnfid, &mnfid_len, 0);
	sc_format_asn1_entry(asn1_toki_attrs + 3, label, &label_len, 0);
	sc_format_asn1_entry(asn1_toki_attrs + 4, asn1_twlabel, NULL, 0);
	sc_format_asn1_entry(asn1_toki_attrs + 5, &ti->flags, &flags_len, 0);
	sc_format_asn1_entry(asn1_toki_attrs + 6, &ti->seInfo, &ti->num_seInfo, 0);
	sc_format_asn1_entry(asn1_toki_attrs + 7, NULL, NULL, 0);
	sc_format_asn1_entry(asn1_toki_attrs + 8, asn1_supported_algorithms, NULL, 0);
	sc_format_asn1_entry(asn1_toki_attrs + 9, NULL, NULL, 0);
	sc_format_asn1_entry(asn1_toki_attrs + 10, NULL, NULL, 0);
	sc_format_asn1_entry(asn1_toki_attrs + 11, asn1_last_update, NULL, 0);
	sc_format_asn1_entry(asn1_toki_attrs + 12, preferred_language, &lang_length, 0);
	sc_format_asn1_entry(asn1_toki_attrs + 13, asn1_profile_indication, NULL, 0);
	sc_format_asn1_entry(asn1_tokeninfo, asn1_toki_attrs, NULL, 0);

	r = sc_asn1_decode(ctx, asn1_tokeninfo, buf, blen, NULL, NULL);
	LOG_TEST_RET(ctx, r, "ASN.1 parsing of EF(TokenInfo) failed");

	if (asn1_toki_attrs[1].flags & SC_ASN1_PRESENT && serial_len > 0)   {
		ti->serial_number = malloc(serial_len * 2 + 1);
		if (ti->serial_number == NULL)
			return SC_ERROR_OUT_OF_MEMORY;
		sc_bin_to_hex(serial, serial_len, ti->serial_number, serial_len * 2 + 1, 0);
		sc_log(ctx, "TokenInfo.serialNunmber '%s'", ti->serial_number);
	}

	if (ti->manufacturer_id == NULL) {
		if (asn1_toki_attrs[2].flags & SC_ASN1_PRESENT)
			ti->manufacturer_id = strdup((char *) mnfid);
		else
			ti->manufacturer_id = strdup("(unknown)");
		if (ti->manufacturer_id == NULL)
			return SC_ERROR_OUT_OF_MEMORY;
	}
	if (ti->label == NULL) {
		if (asn1_toki_attrs[3].flags & SC_ASN1_PRESENT ||
		    asn1_toki_attrs[4].flags & SC_ASN1_PRESENT)
			ti->label = strdup((char *) label);
		else
			ti->label = strdup("(unknown)");
		if (ti->label == NULL)
			return SC_ERROR_OUT_OF_MEMORY;
	}
	if (asn1_toki_attrs[11].flags & SC_ASN1_PRESENT) {
		if (asn1_last_update[0].flags & SC_ASN1_PRESENT)   {
			sc_log(ctx, "LastUpdate.generalizedTime present");
			ti->last_update.gtime = strdup((char *)last_update);
			if (ti->last_update.gtime == NULL)
				return SC_ERROR_OUT_OF_MEMORY;
		}
		else if (asn1_last_update[1].flags & SC_ASN1_PRESENT)  {
			sc_log(ctx, "LastUpdate.referencedTime present");
		}
	}
	if (asn1_toki_attrs[12].flags & SC_ASN1_PRESENT) {
		preferred_language[2] = 0;
		ti->preferred_language = strdup((char *)preferred_language);
		if (ti->preferred_language == NULL)
			return SC_ERROR_OUT_OF_MEMORY;
	}

	sc_init_oid(&ti->profile_indication.oid);
	if (asn1_toki_attrs[13].flags & SC_ASN1_PRESENT) {
		if (asn1_profile_indication[0].flags & SC_ASN1_PRESENT)   {
			sc_log(ctx, "ProfileIndication.oid present");
		}
		else if (asn1_profile_indication[1].flags & SC_ASN1_PRESENT)  {
			sc_log(ctx, "ProfileIndication.name present");
			ti->profile_indication.name = strdup((char *)profile_indication);
			if (ti->profile_indication.name == NULL)
				return SC_ERROR_OUT_OF_MEMORY;
		}
	}

	sc_log(ctx, "LastUpdate.path '%s'", sc_print_path(&ti->last_update.path));
	sc_log(ctx, "ProfileIndication.name '%s'",  ti->profile_indication.name);
	return SC_SUCCESS;
}


int
sc_pkcs15_encode_tokeninfo(sc_context_t *ctx, sc_pkcs15_tokeninfo_t *ti,
		u8 **buf, size_t *buflen)
{
	int r, ii;
	size_t serial_len, mnfid_len, label_len, flags_len, last_upd_len, pi_len;

	struct sc_asn1_entry asn1_toki_attrs[C_ASN1_TOKI_ATTRS_SIZE];
	struct sc_asn1_entry asn1_tokeninfo[2];
	struct sc_asn1_entry asn1_supported_algorithms[SC_MAX_SUPPORTED_ALGORITHMS + 1],
			asn1_algo_infos[SC_MAX_SUPPORTED_ALGORITHMS][7],
			asn1_algo_infos_parameters[SC_MAX_SUPPORTED_ALGORITHMS][3];
	size_t reference_len = sizeof(ti->supported_algos[0].reference);
	size_t mechanism_len = sizeof(ti->supported_algos[0].mechanism);
	size_t parameter_len = sizeof(ti->supported_algos[0].parameters);
	size_t operations_len = sizeof(ti->supported_algos[0].operations);
	size_t algo_ref_len = sizeof(ti->supported_algos[0].algo_ref);
	struct sc_asn1_entry asn1_last_update[C_ASN1_LAST_UPDATE_SIZE];
	struct sc_asn1_entry asn1_profile_indication[C_ASN1_PROFILE_INDICATION_SIZE];

	sc_copy_asn1_entry(c_asn1_toki_attrs, asn1_toki_attrs);
	sc_copy_asn1_entry(c_asn1_tokeninfo, asn1_tokeninfo);
	sc_copy_asn1_entry(c_asn1_last_update, asn1_last_update);
	sc_copy_asn1_entry(c_asn1_profile_indication, asn1_profile_indication);

	for (ii=0; ii<SC_MAX_SUPPORTED_ALGORITHMS && ti->supported_algos[ii].reference; ii++) {
		sc_copy_asn1_entry(c_asn1_algorithm_info, asn1_algo_infos[ii]);
		sc_copy_asn1_entry(c_asn1_algorithm_info_parameters,
			asn1_algo_infos_parameters[ii]);
	}
	sc_copy_asn1_entry(c_asn1_supported_algorithms, asn1_supported_algorithms);

	for (ii=0; ii<SC_MAX_SUPPORTED_ALGORITHMS && ti->supported_algos[ii].reference; ii++)   {
		sc_format_asn1_entry(asn1_algo_infos[ii] + 0, &ti->supported_algos[ii].reference, &reference_len, 1);
		sc_format_asn1_entry(asn1_algo_infos[ii] + 1, &ti->supported_algos[ii].mechanism, &mechanism_len, 1);
		sc_format_asn1_entry(asn1_algo_infos[ii] + 2,
			asn1_algo_infos_parameters[ii], NULL, 1);
		if (!ti->supported_algos[ii].parameters)	{
			sc_format_asn1_entry(asn1_algo_infos_parameters[ii] + 0,
				NULL, NULL, 1);
		}
		else {
			sc_format_asn1_entry(asn1_algo_infos_parameters[ii] + 1,
				&ti->supported_algos[ii].parameters, &parameter_len, 0);
		}
		sc_format_asn1_entry(asn1_algo_infos[ii] + 3, &ti->supported_algos[ii].operations, &operations_len, 1);
		sc_format_asn1_entry(asn1_algo_infos[ii] + 4, &ti->supported_algos[ii].algo_id, NULL, 1);
		sc_format_asn1_entry(asn1_algo_infos[ii] + 5, &ti->supported_algos[ii].algo_ref, &algo_ref_len, 1);
		sc_format_asn1_entry(asn1_supported_algorithms + ii, asn1_algo_infos[ii], NULL, 1);
	}

	sc_format_asn1_entry(asn1_toki_attrs + 0, &ti->version, NULL, 1);
	if (ti->serial_number != NULL) {
		u8 serial[128];
		serial_len = 0;
		if (strlen(ti->serial_number)/2 > sizeof(serial))
			return SC_ERROR_BUFFER_TOO_SMALL;
		serial_len = sizeof(serial);
		if (sc_hex_to_bin(ti->serial_number, serial, &serial_len) < 0)
			return SC_ERROR_INVALID_ARGUMENTS;
		sc_format_asn1_entry(asn1_toki_attrs + 1, serial, &serial_len, 1);
	}
	else   {
		sc_format_asn1_entry(asn1_toki_attrs + 1, NULL, NULL, 0);
	}

	if (ti->manufacturer_id != NULL) {
		mnfid_len = strlen(ti->manufacturer_id);
		sc_format_asn1_entry(asn1_toki_attrs + 2, ti->manufacturer_id, &mnfid_len, 1);
	}
	else    {
		sc_format_asn1_entry(asn1_toki_attrs + 2, NULL, NULL, 0);
	}

	if (ti->label != NULL) {
		label_len = strlen(ti->label);
		sc_format_asn1_entry(asn1_toki_attrs + 3, ti->label, &label_len, 1);
	}
	else   {
		sc_format_asn1_entry(asn1_toki_attrs + 3, NULL, NULL, 0);
	}

	if (ti->flags) {
		flags_len = sizeof(ti->flags);
		sc_format_asn1_entry(asn1_toki_attrs + 5, &ti->flags, &flags_len, 1);
	}
	else   {
		sc_format_asn1_entry(asn1_toki_attrs + 5, NULL, NULL, 0);
	}

	if (ti->num_seInfo)
		sc_format_asn1_entry(asn1_toki_attrs + 6, ti->seInfo, &ti->num_seInfo, 1);
	else
		sc_format_asn1_entry(asn1_toki_attrs + 6, NULL, NULL, 0);

	sc_format_asn1_entry(asn1_toki_attrs + 7, NULL, NULL, 0);

	if (ti->supported_algos[0].reference)
		sc_format_asn1_entry(asn1_toki_attrs + 8, asn1_supported_algorithms, NULL, 1);
	else
		sc_format_asn1_entry(asn1_toki_attrs + 8, NULL, NULL, 0);

	sc_format_asn1_entry(asn1_toki_attrs + 9, NULL, NULL, 0);
	sc_format_asn1_entry(asn1_toki_attrs + 10, NULL, NULL, 0);

	if (ti->last_update.path.len) {
		sc_format_asn1_entry(asn1_last_update + 0, &ti->last_update.path, NULL, 1);
		sc_format_asn1_entry(asn1_toki_attrs + 11, asn1_last_update, NULL, 1);
	}
	else if (ti->last_update.gtime != NULL) {
		last_upd_len = strlen(ti->last_update.gtime);
		sc_format_asn1_entry(asn1_last_update + 0, ti->last_update.gtime, &last_upd_len, 1);
		sc_format_asn1_entry(asn1_toki_attrs + 11, asn1_last_update, NULL, 1);
	}
	else   {
		sc_format_asn1_entry(asn1_toki_attrs + 11, NULL, NULL, 0);
	}
	sc_format_asn1_entry(asn1_toki_attrs + 12, NULL, NULL, 0);

	if (sc_valid_oid(&ti->profile_indication.oid))   {
		sc_format_asn1_entry(asn1_profile_indication + 0, &ti->profile_indication.oid, NULL, 1);
		sc_format_asn1_entry(asn1_toki_attrs + 13, asn1_profile_indication, NULL, 1);
	}
	else if (ti->profile_indication.name)   {
		pi_len = strlen(ti->profile_indication.name);
		sc_format_asn1_entry(asn1_profile_indication + 1, ti->profile_indication.name, &pi_len, 1);
		sc_format_asn1_entry(asn1_toki_attrs + 13, asn1_profile_indication, NULL, 1);
	}
	else    {
		sc_format_asn1_entry(asn1_toki_attrs + 13, NULL, NULL, 0);
	}

	sc_format_asn1_entry(asn1_tokeninfo, asn1_toki_attrs, NULL, 1);

	r = sc_asn1_encode(ctx, asn1_tokeninfo, buf, buflen);
	LOG_TEST_RET(ctx, r, "sc_asn1_encode() failed");

	return SC_SUCCESS;
}

static const struct sc_asn1_entry c_asn1_ddo[] = {
	{ "oid",	   SC_ASN1_OBJECT, SC_ASN1_TAG_OBJECT, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "odfPath",	   SC_ASN1_PATH, SC_ASN1_CONS | SC_ASN1_TAG_SEQUENCE, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "tokenInfoPath", SC_ASN1_PATH, SC_ASN1_CONS | SC_ASN1_CTX | 0, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "unusedPath",    SC_ASN1_PATH, SC_ASN1_CONS | SC_ASN1_CTX | 1, SC_ASN1_OPTIONAL, NULL, NULL },
/* According to PKCS#15 v1.1 here is the place for the future extensions.
 * The following data are used when ODF record points to the xDF files in a different application.
 */
	{ "ddoIIN",	   SC_ASN1_OCTET_STRING, SC_ASN1_APP | 0x02, SC_ASN1_OPTIONAL, NULL, NULL },
	{ "ddoAID",	   SC_ASN1_OCTET_STRING, SC_ASN1_APP | 0x0F, SC_ASN1_OPTIONAL, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL }
};

static void
fix_authentic_ddo(struct sc_pkcs15_card *p15card)
{
	/* AuthentIC v3.2 card has invalid ODF and tokenInfo paths encoded into DDO.
	 * Cleanup this attributes -- default values must be OK.
	 */
	if (p15card->card->type == SC_CARD_TYPE_OBERTHUR_AUTHENTIC_3_2)   {
		sc_file_free(p15card->file_odf);
		p15card->file_odf = NULL;
		sc_file_free(p15card->file_tokeninfo);
		p15card->file_tokeninfo = NULL;
	}
}

static int
parse_ddo(struct sc_pkcs15_card *p15card, const u8 * buf, size_t buflen)
{
	struct sc_context *ctx = p15card->card->ctx;
	struct sc_asn1_entry asn1_ddo[7];
	sc_path_t odf_path, ti_path, us_path;
	struct sc_iid iid;
	struct sc_aid aid;
	int r;

	LOG_FUNC_CALLED(ctx);

	iid.len = sizeof(iid.value);
	aid.len = sizeof(aid.value);

	sc_copy_asn1_entry(c_asn1_ddo, asn1_ddo);
	sc_format_asn1_entry(asn1_ddo + 1, &odf_path, NULL, 0);
	sc_format_asn1_entry(asn1_ddo + 2, &ti_path, NULL, 0);
	sc_format_asn1_entry(asn1_ddo + 3, &us_path, NULL, 0);
	sc_format_asn1_entry(asn1_ddo + 4, iid.value, &iid.len, 0);
	sc_format_asn1_entry(asn1_ddo + 5, aid.value, &aid.len, 0);

	r = sc_asn1_decode(ctx, asn1_ddo, buf, buflen, NULL, NULL);
	LOG_TEST_RET(ctx, r, "DDO parsing failed");

	if (asn1_ddo[1].flags & SC_ASN1_PRESENT) {
		p15card->file_odf = sc_file_new();
		if (p15card->file_odf == NULL)
			goto mem_err;
		p15card->file_odf->path = odf_path;
	}
	if (asn1_ddo[2].flags & SC_ASN1_PRESENT) {
		if (p15card->file_tokeninfo)
			sc_file_free(p15card->file_tokeninfo);
		p15card->file_tokeninfo = sc_file_new();
		if (p15card->file_tokeninfo == NULL)
			goto mem_err;
		p15card->file_tokeninfo->path = ti_path;
	}
	if (asn1_ddo[3].flags & SC_ASN1_PRESENT) {
		p15card->file_unusedspace = sc_file_new();
		if (p15card->file_unusedspace == NULL)
			goto mem_err;
		p15card->file_unusedspace->path = us_path;
	}
	if (asn1_ddo[4].flags & SC_ASN1_PRESENT) {
		sc_debug(ctx, SC_LOG_DEBUG_ASN1, "DDO.IID '%s'", sc_dump_hex(iid.value, iid.len));
		memcpy(&p15card->app->ddo.iid, &iid, sizeof(struct sc_iid));
	}
	if (asn1_ddo[5].flags & SC_ASN1_PRESENT) {
		sc_debug(ctx, SC_LOG_DEBUG_ASN1, "DDO.AID '%s'", sc_dump_hex(aid.value, aid.len));
		memcpy(&p15card->app->ddo.aid, &aid, sizeof(struct sc_aid));
	}

	fix_authentic_ddo(p15card);
	LOG_FUNC_RETURN(ctx, SC_SUCCESS);
mem_err:
	sc_file_free(p15card->file_odf);
	p15card->file_odf = NULL;
	sc_file_free(p15card->file_tokeninfo);
	p15card->file_tokeninfo = NULL;
	sc_file_free(p15card->file_unusedspace);
	p15card->file_unusedspace = NULL;
	LOG_FUNC_RETURN(ctx, SC_ERROR_OUT_OF_MEMORY);
}


char *
sc_pkcs15_get_lastupdate(struct sc_pkcs15_card *p15card)
{
	struct sc_context *ctx  = p15card->card->ctx;
	struct sc_file *file = NULL;
	struct sc_asn1_entry asn1_last_update[C_ASN1_LAST_UPDATE_SIZE];
	unsigned char *content, last_update[32];
	size_t lupdate_len = sizeof(last_update) - 1;
	int r, content_len;
	size_t size;

	if (p15card->tokeninfo->last_update.gtime)
		goto done;

	if (!p15card->tokeninfo->last_update.path.len)
		return NULL;

	r = sc_select_file(p15card->card, &p15card->tokeninfo->last_update.path, &file);
	if (r < 0)
		return NULL;

	size = file->size ? file->size : 1024;

	content = calloc(size, 1);
	if (!content)
		return NULL;

	r = sc_read_binary(p15card->card, 0, content, size, 0);
	if (r < 0)
		return NULL;
	content_len = r;

	sc_file_free(file);

	sc_copy_asn1_entry(c_asn1_last_update, asn1_last_update);
	sc_format_asn1_entry(asn1_last_update + 0, last_update, &lupdate_len, 0);

	r = sc_asn1_decode(ctx, asn1_last_update, content, content_len, NULL, NULL);
	free(content);
	if (r < 0)
		return NULL;

	p15card->tokeninfo->last_update.gtime = strdup((char *)last_update);
	if (!p15card->tokeninfo->last_update.gtime)
		return NULL;
done:
	sc_log(ctx, "lastUpdate.gtime '%s'", p15card->tokeninfo->last_update.gtime);
	return p15card->tokeninfo->last_update.gtime;
}


static const struct sc_asn1_entry c_asn1_odf[] = {
	{ "privateKeys",	 SC_ASN1_STRUCT, SC_ASN1_CTX | 0 | SC_ASN1_CONS, 0, NULL, NULL },
	{ "publicKeys",		 SC_ASN1_STRUCT, SC_ASN1_CTX | 1 | SC_ASN1_CONS, 0, NULL, NULL },
	{ "trustedPublicKeys",	 SC_ASN1_STRUCT, SC_ASN1_CTX | 2 | SC_ASN1_CONS, 0, NULL, NULL },
	{ "secretKeys",		 SC_ASN1_STRUCT, SC_ASN1_CTX | 3 | SC_ASN1_CONS, 0, NULL, NULL },
	{ "certificates",	 SC_ASN1_STRUCT, SC_ASN1_CTX | 4 | SC_ASN1_CONS, 0, NULL, NULL },
	{ "trustedCertificates", SC_ASN1_STRUCT, SC_ASN1_CTX | 5 | SC_ASN1_CONS, 0, NULL, NULL },
	{ "usefulCertificates",  SC_ASN1_STRUCT, SC_ASN1_CTX | 6 | SC_ASN1_CONS, 0, NULL, NULL },
	{ "dataObjects",	 SC_ASN1_STRUCT, SC_ASN1_CTX | 7 | SC_ASN1_CONS, 0, NULL, NULL },
	{ "authObjects",	 SC_ASN1_STRUCT, SC_ASN1_CTX | 8 | SC_ASN1_CONS, 0, NULL, NULL },
	{ NULL, 0, 0, 0, NULL, NULL }
};

static const unsigned int odf_indexes[] = {
	SC_PKCS15_PRKDF,
	SC_PKCS15_PUKDF,
	SC_PKCS15_PUKDF_TRUSTED,
	SC_PKCS15_SKDF,
	SC_PKCS15_CDF,
	SC_PKCS15_CDF_TRUSTED,
	SC_PKCS15_CDF_USEFUL,
	SC_PKCS15_DODF,
	SC_PKCS15_AODF,
};


static int
parse_odf(const unsigned char * buf, size_t buflen, struct sc_pkcs15_card *p15card)
{
	const unsigned char *p = buf;
	size_t left = buflen;
	int r, i, type;
	struct sc_path path;
	struct sc_asn1_entry asn1_obj_or_path[] = {
		{ "path", SC_ASN1_PATH, SC_ASN1_CONS | SC_ASN1_SEQUENCE, 0, &path, NULL },
		{ NULL, 0, 0, 0, NULL, NULL }
	};
	struct sc_asn1_entry asn1_odf[10];

	sc_copy_asn1_entry(c_asn1_odf, asn1_odf);
	for (i = 0; asn1_odf[i].name != NULL; i++)
		sc_format_asn1_entry(asn1_odf + i, asn1_obj_or_path, NULL, 0);
	while (left > 0) {
		r = sc_asn1_decode_choice(p15card->card->ctx, asn1_odf, p, left, &p, &left);
		if (r == SC_ERROR_ASN1_END_OF_CONTENTS)
			break;
		if (r < 0)
			return r;
		type = r;
		r = sc_pkcs15_make_absolute_path(&p15card->file_app->path, &path);
		if (r < 0)
			return r;
		r = sc_pkcs15_add_df(p15card, odf_indexes[type], &path);
		if (r)
			return r;
	}
	return 0;
}


int
sc_pkcs15_encode_odf(struct sc_context *ctx, struct sc_pkcs15_card *p15card,
			 unsigned char **buf, size_t *buflen)
{
	struct sc_path path;
	struct sc_asn1_entry asn1_obj_or_path[] = {
		{ "path", SC_ASN1_PATH, SC_ASN1_CONS | SC_ASN1_SEQUENCE, 0, &path, NULL },
		{ NULL, 0, 0, 0, NULL, NULL }
	};
	struct sc_asn1_entry *asn1_paths = NULL;
	struct sc_asn1_entry *asn1_odf = NULL;
	int df_count = 0, r, c = 0;
	const int nr_indexes = sizeof(odf_indexes)/sizeof(odf_indexes[0]);
	struct sc_pkcs15_df *df;

	df = p15card->df_list;
	while (df != NULL) {
		df_count++;
		df = df->next;
	};
	if (df_count == 0)
		LOG_TEST_RET(ctx, SC_ERROR_OBJECT_NOT_FOUND, "No DF's found.");

	asn1_odf = malloc(sizeof(struct sc_asn1_entry) * (df_count + 1));
	if (asn1_odf == NULL) {
		r = SC_ERROR_OUT_OF_MEMORY;
		goto err;
	}
	asn1_paths = malloc(sizeof(struct sc_asn1_entry) * (df_count * 2));
	if (asn1_paths == NULL) {
		r = SC_ERROR_OUT_OF_MEMORY;
		goto err;
	}
	for (df = p15card->df_list; df != NULL; df = df->next) {
		int j, type = -1;

		for (j = 0; j < nr_indexes; j++)
			if (odf_indexes[j] == df->type) {
				type = j;
				break;
			}
		if (type == -1) {
			sc_log(ctx, "Unsupported DF type.");
			continue;
		}
		asn1_odf[c] = c_asn1_odf[type];
		sc_format_asn1_entry(asn1_odf + c, asn1_paths + 2*c, NULL, 1);
		sc_copy_asn1_entry(asn1_obj_or_path, asn1_paths + 2*c);
		sc_format_asn1_entry(asn1_paths + 2*c, &df->path, NULL, 1);
		c++;
	}
	asn1_odf[c].name = NULL;
	r = sc_asn1_encode(ctx, asn1_odf, buf, buflen);
err:
	if (asn1_paths != NULL)
		free(asn1_paths);
	if (asn1_odf != NULL)
		free(asn1_odf);
	return r;
}


struct sc_pkcs15_card *
sc_pkcs15_card_new(void)
{
	struct sc_pkcs15_card *p15card;

	p15card = calloc(1, sizeof(struct sc_pkcs15_card));
	if (p15card == NULL)
		return NULL;

	p15card->tokeninfo = calloc(1, sizeof(struct sc_pkcs15_tokeninfo));
	if (p15card->tokeninfo == NULL) {
		free(p15card);
		return NULL;
	}

	p15card->magic = SC_PKCS15_CARD_MAGIC;
	return p15card;
}


struct sc_pkcs15_tokeninfo *
sc_pkcs15_tokeninfo_new(void)
{
	struct sc_pkcs15_tokeninfo *tokeninfo;

	tokeninfo = calloc(1, sizeof(struct sc_pkcs15_tokeninfo));
	if (tokeninfo == NULL) {
		return NULL;
	}

	sc_init_oid(&tokeninfo->profile_indication.oid);

	return tokeninfo;
}


void
sc_pkcs15_free_tokeninfo(struct sc_pkcs15_tokeninfo *tokeninfo)
{
	if (!tokeninfo)
		return;

	if (tokeninfo->label != NULL)
		free(tokeninfo->label);
	if (tokeninfo->serial_number != NULL)
		free(tokeninfo->serial_number);
	if (tokeninfo->manufacturer_id != NULL)
		free(tokeninfo->manufacturer_id);
	if (tokeninfo->last_update.gtime != NULL)
		free(tokeninfo->last_update.gtime);
	if (tokeninfo->preferred_language != NULL)
		free(tokeninfo->preferred_language);
	if (tokeninfo->profile_indication.name != NULL)
		free(tokeninfo->profile_indication.name);
	if (tokeninfo->seInfo != NULL) {
		unsigned i;
		for (i = 0; i < tokeninfo->num_seInfo; i++)
			free(tokeninfo->seInfo[i]);
		free(tokeninfo->seInfo);
	}
	free(tokeninfo);
}


void
sc_pkcs15_free_app(struct sc_pkcs15_card *p15card)
{
	if (p15card && p15card->app) {
		free(p15card->app->label);
		free(p15card->app->ddo.value);
		free(p15card->app);
		p15card->app = NULL;
	}
}


void
sc_pkcs15_card_free(struct sc_pkcs15_card *p15card)
{
	if (p15card == NULL || p15card->magic != SC_PKCS15_CARD_MAGIC)
		return;

	if (p15card->ops.clear)
		p15card->ops.clear(p15card);

	/* For more complicated MD data a dedicated release procedure
	 * has to be implemented. */
	if (p15card->md_data)
		free(p15card->md_data);

	sc_pkcs15_remove_objects(p15card);
	sc_pkcs15_remove_dfs(p15card);
	sc_pkcs15_free_unusedspace(p15card);
	p15card->unusedspace_read = 0;

	sc_file_free(p15card->file_app);
	sc_file_free(p15card->file_tokeninfo);
	sc_file_free(p15card->file_odf);
	sc_file_free(p15card->file_unusedspace);

	p15card->magic = 0;
	sc_pkcs15_free_tokeninfo(p15card->tokeninfo);
	sc_pkcs15_free_app(p15card);
	free(p15card);
}


void
sc_pkcs15_card_clear(struct sc_pkcs15_card *p15card)
{
	if (p15card == NULL)
		return;

	if (p15card->ops.clear)
		p15card->ops.clear(p15card);

	p15card->flags = 0;
	p15card->tokeninfo->version = 0;
	p15card->tokeninfo->flags   = 0;

	sc_pkcs15_remove_objects(p15card);
	sc_pkcs15_remove_dfs(p15card);

	p15card->df_list = NULL;
	sc_file_free(p15card->file_app);
	p15card->file_app = NULL;
	sc_file_free(p15card->file_tokeninfo);
	p15card->file_tokeninfo = NULL;
	sc_file_free(p15card->file_odf);
	p15card->file_odf = NULL;
	sc_file_free(p15card->file_unusedspace);
	p15card->file_unusedspace = NULL;
	if (p15card->tokeninfo->label != NULL) {
		free(p15card->tokeninfo->label);
		p15card->tokeninfo->label = NULL;
	}
	if (p15card->tokeninfo->serial_number != NULL) {
		free(p15card->tokeninfo->serial_number);
		p15card->tokeninfo->serial_number = NULL;
	}
	if (p15card->tokeninfo->manufacturer_id != NULL) {
		free(p15card->tokeninfo->manufacturer_id);
		p15card->tokeninfo->manufacturer_id = NULL;
	}
	if (p15card->tokeninfo->last_update.gtime != NULL) {
		free(p15card->tokeninfo->last_update.gtime);
		p15card->tokeninfo->last_update.gtime = NULL;
	}
	if (p15card->tokeninfo->preferred_language != NULL) {
		free(p15card->tokeninfo->preferred_language);
		p15card->tokeninfo->preferred_language = NULL;
	}
	if (p15card->tokeninfo->profile_indication.name != NULL)   {
		free(p15card->tokeninfo->profile_indication.name);
		p15card->tokeninfo->profile_indication.name = NULL;
	}
	if (p15card->tokeninfo->seInfo != NULL) {
		size_t i;
		for (i = 0; i < p15card->tokeninfo->num_seInfo; i++)
			free(p15card->tokeninfo->seInfo[i]);
		free(p15card->tokeninfo->seInfo);
		p15card->tokeninfo->seInfo     = NULL;
		p15card->tokeninfo->num_seInfo = 0;
	}
}


struct sc_app_info *
sc_find_app(struct sc_card *card, struct sc_aid *aid)
{
	int ii;

	if (card->app_count <= 0)
		return NULL;

	if (!aid || !aid->len)
		return card->app[0];

	for (ii=0; ii < card->app_count; ii++) {
		if (card->app[ii]->aid.len != aid->len)
			continue;
		if (memcmp(card->app[ii]->aid.value, aid->value, aid->len))
			continue;
		return card->app[ii];
	}
	return NULL;
}


static struct sc_app_info *
sc_dup_app_info(const struct sc_app_info *info)
{
	struct sc_app_info *out = calloc(1, sizeof(struct sc_app_info));

	if (!out)
		return NULL;

	memcpy(out, info, sizeof(struct sc_app_info));

	if (info->label) {
		out->label = strdup(info->label);
		if (!out->label) {
			free(out);
			return NULL;
		}
	} else
		out->label = NULL;

	out->ddo.value = malloc(info->ddo.len);
	if (!out->ddo.value) {
		free(out->label);
		free(out);
		return NULL;
	}
	memcpy(out->ddo.value, info->ddo.value, info->ddo.len);

	return out;
}


struct sc_app_info *
sc_pkcs15_get_application_by_type(struct sc_card * card, char *app_type)
{
	struct sc_app_info *out = NULL;
	scconf_block *conf_block = NULL;
	int i, rv;

	if (!card)
		return NULL;

	if (card->app_count < 0)   {
		rv = sc_enum_apps(card);
		if (rv < 0 && rv != SC_ERROR_FILE_NOT_FOUND)
			return NULL;
	}

	conf_block = sc_get_conf_block(card->ctx, "framework", "pkcs15", 1);
	if (!conf_block)
		return NULL;

	for (i = 0; i < card->app_count; i++)   {
		struct sc_app_info *app_info = card->app[i];
		scconf_block **blocks = NULL;
		char str_path[SC_MAX_AID_STRING_SIZE];

		sc_bin_to_hex(app_info->aid.value, app_info->aid.len, str_path, sizeof(str_path), 0);
		blocks = scconf_find_blocks(card->ctx->conf, conf_block, "application", str_path);
		if (blocks)   {
			if (blocks[0])   {
				char *type = (char *)scconf_get_str(blocks[0], "type", app_type);
				if (!strcmp(type, app_type))   {
					out = app_info;
					free(blocks);
					break;
				}
			}
			free(blocks);
		}
	}

	return out;
}


int
sc_pkcs15_bind_internal(struct sc_pkcs15_card *p15card, struct sc_aid *aid)
{
	struct sc_path tmppath;
	struct sc_card    *card = p15card->card;
	struct sc_context *ctx  = card->ctx;
	struct sc_pkcs15_tokeninfo tokeninfo;
	struct sc_pkcs15_df *df;
	const struct sc_app_info *info = NULL;
	unsigned char *buf = NULL;
	size_t len;
	int    err, ok = 0;

	LOG_FUNC_CALLED(ctx);
	/* Enumerate apps now */
	if (card->app_count < 0) {
		err = sc_enum_apps(card);
		if (err != SC_SUCCESS)
			sc_log(ctx, "unable to enumerate apps: %s", sc_strerror(err));
	}
	p15card->file_app = sc_file_new();
	if (p15card->file_app == NULL) {
		err = SC_ERROR_OUT_OF_MEMORY;
		goto end;
	}

	sc_format_path("3F005015", &p15card->file_app->path);

	info = sc_find_app(card, aid);
	if (info)   {
		sc_log(ctx, "bind to application('%s',aid:'%s')", info->label, sc_dump_hex(info->aid.value, info->aid.len));
		p15card->app = sc_dup_app_info(info);
		if (!p15card->app)   {
			err = SC_ERROR_OUT_OF_MEMORY;
			goto end;
		}

		if (info->path.len)
			p15card->file_app->path = info->path;

		if (info->ddo.value && info->ddo.len)
			parse_ddo(p15card, info->ddo.value, info->ddo.len);

	}
	else if (aid)   {
		sc_log(ctx, "Application '%s' not found", sc_dump_hex(aid->value, aid->len));
		err = SC_ERROR_INVALID_ARGUMENTS;
		goto end;
	}
	sc_log(ctx, "application path '%s'", sc_print_path(&p15card->file_app->path));

	/* Check if pkcs15 directory exists */
	err = sc_select_file(card, &p15card->file_app->path, NULL);

	/* If the above test failed on cards without EF(DIR),
	 * try to continue read ODF from 3F005031. -aet
	 */
	if ((err != SC_SUCCESS) && (card->app_count < 1)) {
		sc_format_path("3F00", &p15card->file_app->path);
		err = SC_SUCCESS;
	}

	if (err < 0)   {
		sc_log (ctx, "Cannot select application path");
		goto end;
	}

	if (p15card->file_odf == NULL) {
		/* check if an ODF is present; we don't know yet whether we have a pkcs15 card */
		sc_format_path("5031", &tmppath);
		err = sc_pkcs15_make_absolute_path(&p15card->file_app->path, &tmppath);
		if (err != SC_SUCCESS)   {
			sc_log(ctx, "Cannot make absolute path to EF(ODF); error:%i", err);
			goto end;
		}
		sc_log(ctx, "absolute path to EF(ODF) %s", sc_print_path(&tmppath));
		err = sc_select_file(card, &tmppath, &p15card->file_odf);
	}
	else {
		tmppath = p15card->file_odf->path;
		sc_file_free(p15card->file_odf);
		p15card->file_odf = NULL;
		err = sc_select_file(card, &tmppath, &p15card->file_odf);
	}

	if (err != SC_SUCCESS) {
		sc_log(ctx, "EF(ODF) not found in '%s'", sc_print_path(&tmppath));
		goto end;
	}

	len = p15card->file_odf->size;
	if (!len) {
		sc_log(ctx, "EF(ODF) is empty");
		goto end;
	}
	buf = malloc(len);
	if(buf == NULL)
		LOG_FUNC_RETURN(ctx, SC_ERROR_OUT_OF_MEMORY);

	err = -1; /* file state: not in cache */
	if (p15card->opts.use_file_cache) {
		err = sc_pkcs15_read_cached_file(p15card, &tmppath, &buf, &len);
		if (err == SC_SUCCESS)
			err = len;
	}
	if (err < 0) {
		err = sc_read_binary(card, 0, buf, len, 0);
		if (err < 2) {
			if (err < 0) {
				sc_log(ctx, "read EF(ODF) file error: %s", sc_strerror(err));
			} else {
				err = SC_ERROR_PKCS15_APP_NOT_FOUND;
				sc_log(ctx, "Invalid content of EF(ODF): %s", sc_strerror(err));
			}
			goto end;
		}
		/* sc_read_binary may return less than requested */
		len = err;

		if (p15card->opts.use_file_cache) {
			sc_pkcs15_cache_file(p15card, &tmppath, buf, len);
		}
	}

	if (parse_odf(buf, len, p15card)) {
		err = SC_ERROR_PKCS15_APP_NOT_FOUND;
		sc_log(ctx, "Unable to parse ODF");
		goto end;
	}
	free(buf);
	buf = NULL;

	sc_log(ctx, "The following DFs were found:");
	for (df = p15card->df_list; df; df = df->next)
		sc_log(ctx, "  DF type %u, path %s, index %u, count %d", df->type,
				sc_print_path(&df->path), df->path.index, df->path.count);

	if (p15card->file_tokeninfo == NULL) {
		sc_format_path("5032", &tmppath);
		err = sc_pkcs15_make_absolute_path(&p15card->file_app->path, &tmppath);
		if (err != SC_SUCCESS)   {
			sc_log(ctx, "Cannot make absolute path to EF(TokenInfo); error:%i", err);
			goto end;
		}
		sc_log(ctx, "absolute path to EF(TokenInfo) %s", sc_print_path(&tmppath));
	}
	else {
		tmppath = p15card->file_tokeninfo->path;
		sc_file_free(p15card->file_tokeninfo);
		p15card->file_tokeninfo = NULL;
	}

	err = sc_select_file(card, &tmppath, &p15card->file_tokeninfo);
	if (err)   {
		sc_log(ctx, "cannot select EF(TokenInfo) file: %s", sc_strerror(err));
		goto end;
	}

	len = p15card->file_tokeninfo->size;
	if (!len) {

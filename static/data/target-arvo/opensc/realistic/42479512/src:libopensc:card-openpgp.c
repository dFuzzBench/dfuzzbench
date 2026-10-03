/*
 * card-openpgp.c: Support for OpenPGP card
 *
 * Copyright (C) 2003  Olaf Kirch <okir@suse.de>
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

/*
 * Specifications:
 * (all available from: https://gnupg.org/ftp/specs/)
 * https://gnupg.org/ftp/specs/openpgp-card-1.0.pdf (obsolete)
 * https://gnupg.org/ftp/specs/openpgp-card-1.1.pdf
 * https://gnupg.org/ftp/specs/OpenPGP-smart-card-application-2.0.pdf
 * https://gnupg.org/ftp/specs/OpenPGP-smart-card-application-2.1.pdf
 * https://gnupg.org/ftp/specs/OpenPGP-smart-card-application-2.2.pdf
 * https://gnupg.org/ftp/specs/OpenPGP-smart-card-application-3.0.pdf
 * https://gnupg.org/ftp/specs/OpenPGP-smart-card-application-3.1.pdf
 * https://gnupg.org/ftp/specs/OpenPGP-smart-card-application-3.2.pdf
 * https://gnupg.org/ftp/specs/OpenPGP-smart-card-application-3.3.pdf
 * https://gnupg.org/ftp/specs/OpenPGP-smart-card-application-3.3.0.pdf
 * https://gnupg.org/ftp/specs/OpenPGP-smart-card-application-3.3.1.pdf
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "internal.h"
#include "asn1.h"
#include "cardctl.h"
#include "errors.h"
#ifdef ENABLE_OPENSSL
#include <openssl/sha.h>
#endif /* ENABLE_OPENSSL */

static const char default_cardname[]    = "OpenPGP card";
static const char default_cardname_v1[] = "OpenPGP card v1.x";
static const char default_cardname_v2[] = "OpenPGP card v2.x";
static const char default_cardname_v3[] = "OpenPGP card v3.x";

static const struct sc_atr_table pgp_atrs[] = {
	{ "3b:fa:13:00:ff:81:31:80:45:00:31:c1:73:c0:01:00:00:90:00:b1", NULL, default_cardname_v1, SC_CARD_TYPE_OPENPGP_V1, 0, NULL },
	{ "3b:da:18:ff:81:b1:fe:75:1f:03:00:31:c5:73:c0:01:40:00:90:00:0c", NULL, default_cardname_v2, SC_CARD_TYPE_OPENPGP_V2, 0, NULL },
	{
		"3b:da:11:ff:81:b1:fe:55:1f:03:00:31:84:73:80:01:80:00:90:00:e4",
		"ff:ff:ff:ff:ff:ff:ff:ff:ff:ff:ff:ff:ff:ff:ff:ff:ff:00:ff:ff:00",
		"Gnuk v1.0.x (OpenPGP v2.0)", SC_CARD_TYPE_OPENPGP_GNUK, 0, NULL
	},
	{ "3b:fc:13:00:00:81:31:fe:15:59:75:62:69:6b:65:79:4e:45:4f:72:33:e1", NULL, "Yubikey NEO (OpenPGP v2.0)", SC_CARD_TYPE_OPENPGP_V2, 0, NULL },
	{ "3b:f8:13:00:00:81:31:fe:15:59:75:62:69:6b:65:79:34:d4", NULL, "Yubikey 4 (OpenPGP v2.1)", SC_CARD_TYPE_OPENPGP_V2, 0, NULL },
	{ "3b:da:18:ff:81:b1:fe:75:1f:03:00:31:f5:73:c0:01:60:00:90:00:1c", NULL, default_cardname_v3, SC_CARD_TYPE_OPENPGP_V3, 0, NULL },
	{ NULL, NULL, NULL, 0, 0, NULL }
};

static struct sc_card_operations *iso_ops;
static struct sc_card_operations pgp_ops;
static struct sc_card_driver pgp_drv = {
	"OpenPGP card",
	"openpgp",
	&pgp_ops,
	NULL, 0, NULL
};

/*
 * The OpenPGP card doesn't have a file system, instead everything
 * is stored in data objects that are accessed through GET/PUT.
 *
 * However, much inside OpenSC's pkcs15 implementation is based on
 * the assumption that we have a file system. So we fake one here.
 *
 * Selecting the MF causes us to select the OpenPGP AID.
 *
 * Everything else is mapped to "file" IDs.
 */

enum _type {		/* DO type */
	SIMPLE      = SC_FILE_TYPE_WORKING_EF,
	CONSTRUCTED = SC_FILE_TYPE_DF
};

enum _version {		/* 2-byte BCD-alike encoded version number */
	OPENPGP_CARD_1_0 = 0x0100,
	OPENPGP_CARD_1_1 = 0x0101,
	OPENPGP_CARD_2_0 = 0x0200,
	OPENPGP_CARD_2_1 = 0x0201,
	OPENPGP_CARD_2_2 = 0x0202,
	OPENPGP_CARD_3_0 = 0x0300,
	OPENPGP_CARD_3_1 = 0x0301,
	OPENPGP_CARD_3_2 = 0x0302,
	OPENPGP_CARD_3_3 = 0x0303,
};

enum _access {		/* access flags for the respective DO/file */
	READ_NEVER   = 0x0010,
	READ_PIN1    = 0x0011,
	READ_PIN2    = 0x0012,
	READ_PIN3    = 0x0014,
	READ_ALWAYS  = 0x0018,
	READ_MASK    = 0x00FF,
	WRITE_NEVER  = 0x1000,
	WRITE_PIN1   = 0x1100,
	WRITE_PIN2   = 0x1200,
	WRITE_PIN3   = 0x1400,
	WRITE_ALWAYS = 0x1800,
	WRITE_MASK   = 0x1F00
};

enum _ext_caps {	/* extended capabilities/features: bit flags */
	EXT_CAP_ALG_ATTR_CHANGEABLE = 0x0004,
	EXT_CAP_PRIVATE_DO          = 0x0008,
	EXT_CAP_C4_CHANGEABLE       = 0x0010,
	EXT_CAP_KEY_IMPORT          = 0x0020,
	EXT_CAP_GET_CHALLENGE       = 0x0040,
	EXT_CAP_SM                  = 0x0080,
	EXT_CAP_LCS                 = 0x0100,
	EXT_CAP_CHAINING            = 0x1000,
	EXT_CAP_APDU_EXT            = 0x2000,
	EXT_CAP_MSE                 = 0x4000
};

enum _card_state {
	CARD_STATE_UNKNOWN        = 0x00,
	CARD_STATE_INITIALIZATION = 0x03,
	CARD_STATE_ACTIVATED      = 0x05
};

enum _sm_algo {
	SM_ALGO_NONE    = 0,	/* SM not supported */
	SM_ALGO_AES128  = 1,
	SM_ALGO_AES256  = 2,
	SM_ALGO_SCP11b  = 3,
	SM_ALGO_3DES    = 256,	/* 2.x: coded as 0 in DO C0 */
	SM_ALGO_UNKNOWN = 257	/* 3.x: coded as 0 in DO C0 */
};

static struct pgp_supported_ec_curves {
		struct sc_object_id oid;
		size_t size;
		struct sc_object_id oid_binary;
} ec_curves[] = {
	{{{1, 2, 840, 10045, 3, 1, 7, -1}}, 256,
		{{0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, -1}}}, /* ansiX9p256r1 */
	{{{1, 3, 132, 0, 34, -1}}, 384,
		{{0x2b, 0x81, 0x04, 0x00, 0x22, -1}}}, /* ansiX9p384r1 */
	{{{1, 3, 132, 0, 35, -1}}, 521,
		{{0x2b, 0x81, 0x04, 0x00, 0x23, -1}}}, /* ansiX9p521r1 */
	{{{1, 3, 36, 3, 3, 2, 8, 1, 1, 7, -1}}, 256,
		{{0x2b, 0x24, 0x03, 0x03, 0x02, 0x08, 0x01, 0x01, 0x07, -1}}}, /* brainpoolP256r1 */
	{{{1, 3, 36, 3, 3, 2, 8, 1, 1, 11, -1}}, 384,
		{{0x2b, 0x24, 0x03, 0x03, 0x02, 0x08, 0x01, 0x01, 0x0b, -1}}}, /* brainpoolP384r1 */
	{{{1, 3, 36, 3, 3, 2, 8, 1, 1, 13, -1}}, 512,
		{{0x2b, 0x24, 0x03, 0x03, 0x02, 0x08, 0x01, 0x01, 0x0d, -1}}}, /* brainpoolP512r1 */
	{{{-1}}, 0, {{0x0}}} /* This entry must not be touched. */
};

typedef struct pgp_blob {
	struct pgp_blob *	next;	/* pointer to next sibling */
	struct pgp_blob *	parent;	/* pointer to parent */
	struct do_info *info;

	sc_file_t *	file;
	unsigned int	id;
	int		status;

	unsigned char *	data;
	unsigned int	len;
	struct pgp_blob *	files;	/* pointer to 1st child */
} pgp_blob_t;

struct do_info {
	unsigned int	id;		/* ID of the DO in question */

	enum _type	type;		/* constructed DO or not */
	enum _access	access;		/* R/W access levels for the DO */

	/* function to get the DO from the card:
	 * only != NULL is DO if readable and not only a part of a constructed DO */
	int		(*get_fn)(sc_card_t *, unsigned int, u8 *, size_t);
	/* function to write the DO to the card:
	 * only != NULL if DO is writeable under some conditions */
	int		(*put_fn)(sc_card_t *, unsigned int, const u8 *, size_t);
};

static int		pgp_get_card_features(sc_card_t *card);
static int		pgp_finish(sc_card_t *card);
static void		pgp_iterate_blobs(pgp_blob_t *, int, void (*func)());

static int		pgp_get_blob(sc_card_t *card, pgp_blob_t *blob,
				 unsigned int id, pgp_blob_t **ret);
static pgp_blob_t *	pgp_new_blob(sc_card_t *, pgp_blob_t *, unsigned int, sc_file_t *);
static void		pgp_free_blob(pgp_blob_t *);
static int		pgp_get_pubkey(sc_card_t *, unsigned int,
				u8 *, size_t);
static int		pgp_get_pubkey_pem(sc_card_t *, unsigned int,
				u8 *, size_t);

/* The DO holding X.509 certificate is constructed but does not contain a child DO.
 * We should notice this when building fake file system later. */
#define DO_CERT                  0x7f21

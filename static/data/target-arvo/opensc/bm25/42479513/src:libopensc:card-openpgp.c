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
/* Control Reference Template of private keys. Ref: Section 4.3.3.7 of OpenPGP card v2 spec.
 * Here we treat them as DOs just for convenience */
#define DO_SIGN                  0xb600
#define DO_ENCR                  0xb800
#define DO_AUTH                  0xa400
/* These DOs do not exist. They are defined and used just for ease of implementation */
#define DO_SIGN_SYM              0xb601
#define DO_ENCR_SYM              0xb801
#define DO_AUTH_SYM              0xa401
/* Private DOs */
#define DO_PRIV1                 0x0101
#define DO_PRIV2                 0x0102
#define DO_PRIV3                 0x0103
#define DO_PRIV4                 0x0104
/* Cardholder information DOs */
#define DO_CARDHOLDER            0x65
#define DO_NAME                  0x5b
#define DO_LANG_PREF             0x5f2d
#define DO_SEX                   0x5f35


/* Maximum length for response buffer when reading pubkey.
 * This value is calculated with 4096-bit key length */
#define MAXLEN_RESP_PUBKEY       527
/* Gnuk only supports 1 key length (2048 bit) */
#define MAXLEN_RESP_PUBKEY_GNUK  271

/* Maximal size of a DO:
 * v2.0+: max. certificate size it at bytes 5-6 of Extended Capabilities DO 00C0
 * v3.0+: max. special DO size is at bytes 7-8 of Extended Capabilities DO 00C0
 * Theoretically we should have the 64k, but we currently limit to 8k. */
#define	MAX_OPENPGP_DO_SIZE	8192

static struct do_info		pgp1x_objects[] = {	/* OpenPGP card spec 1.1 */
	{ 0x004f, SIMPLE,      READ_ALWAYS | WRITE_NEVER, NULL,               NULL        },
	{ 0x005b, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x005e, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  sc_get_data,        sc_put_data },
	{ 0x0065, CONSTRUCTED, READ_ALWAYS | WRITE_NEVER, sc_get_data,        NULL        },
	{ 0x006e, CONSTRUCTED, READ_ALWAYS | WRITE_NEVER, sc_get_data,        NULL        },
	{ 0x0073, CONSTRUCTED, READ_ALWAYS | WRITE_NEVER, NULL,               NULL        },
	{ 0x007a, CONSTRUCTED, READ_ALWAYS | WRITE_NEVER, sc_get_data,        NULL        },
	{ 0x0081, SIMPLE,      READ_ALWAYS | WRITE_NEVER, NULL,               NULL        },
	{ 0x0082, SIMPLE,      READ_ALWAYS | WRITE_NEVER, NULL,               NULL        },
	{ 0x0093, SIMPLE,      READ_ALWAYS | WRITE_NEVER, NULL,               NULL        },
	{ 0x00c0, SIMPLE,      READ_ALWAYS | WRITE_NEVER, NULL,               NULL        },
	{ 0x00c1, SIMPLE,      READ_ALWAYS | WRITE_NEVER, NULL,               NULL        },
	{ 0x00c2, SIMPLE,      READ_ALWAYS | WRITE_NEVER, NULL,               NULL        },
	{ 0x00c3, SIMPLE,      READ_ALWAYS | WRITE_NEVER, NULL,               NULL        },
	{ 0x00c4, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00c5, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00c6, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00c7, SIMPLE,      READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00c8, SIMPLE,      READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00c9, SIMPLE,      READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00ca, SIMPLE,      READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00cb, SIMPLE,      READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00cc, SIMPLE,      READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00cd, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00ce, SIMPLE,      READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00cf, SIMPLE,      READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00d0, SIMPLE,      READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00e0, CONSTRUCTED, READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00e1, CONSTRUCTED, READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00e2, CONSTRUCTED, READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x0101, SIMPLE,      READ_ALWAYS | WRITE_PIN2,  sc_get_data,        sc_put_data },
	{ 0x0102, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  sc_get_data,        sc_put_data },
	{ 0x0103, SIMPLE,      READ_PIN2   | WRITE_PIN2,  sc_get_data,        sc_put_data },
	{ 0x0104, SIMPLE,      READ_PIN3   | WRITE_PIN3,  sc_get_data,        sc_put_data },
	{ 0x3f00, CONSTRUCTED, READ_ALWAYS | WRITE_NEVER, NULL,               NULL        },
	{ 0x5f2d, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x5f35, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x5f50, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  sc_get_data,        sc_put_data },
	{ 0x7f49, CONSTRUCTED, READ_ALWAYS | WRITE_NEVER, NULL,               NULL        },
	{ DO_AUTH,     CONSTRUCTED, READ_ALWAYS | WRITE_NEVER, pgp_get_pubkey,     NULL   },
	{ DO_AUTH_SYM, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  pgp_get_pubkey_pem, NULL   },
	{ DO_SIGN,     CONSTRUCTED, READ_ALWAYS | WRITE_NEVER, pgp_get_pubkey,     NULL   },
	{ DO_SIGN_SYM, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  pgp_get_pubkey_pem, NULL   },
	{ DO_ENCR,     CONSTRUCTED, READ_ALWAYS | WRITE_NEVER, pgp_get_pubkey,     NULL   },
	{ DO_ENCR_SYM, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  pgp_get_pubkey_pem, NULL   },
	{ 0, 0, 0, NULL, NULL },
};

static struct do_info		pgp33_objects[] = {	/* OpenPGP card spec 3.3 */
	{ 0x00f9, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  NULL,               sc_put_data },
	/* OpenPGP card spec 3.0 - 3.2 */
	{ 0x00d6, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00d7, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00d8, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  NULL,               sc_put_data },
	/* DO 7F66 is CONSTRUCTED in spec; we treat it as SIMPLE: no need to parse TLV */
	{ 0x7f66, SIMPLE,      READ_ALWAYS | WRITE_NEVER, NULL,               sc_put_data },
	/* DO 7F74 is CONSTRUCTED in spec; we treat it as SIMPLE for the time being */
	{ 0x7f74, SIMPLE,      READ_ALWAYS | WRITE_NEVER, NULL,               sc_put_data },
	/* OpenPGP card spec 2.1 & 2.2 */
	{ 0x00d5, SIMPLE,      READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	/* OpenPGP card spec 2.0 */
	{ 0x004d, CONSTRUCTED, READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x004f, SIMPLE,      READ_ALWAYS | WRITE_NEVER, sc_get_data,        NULL        },
	{ 0x005b, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x005e, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  sc_get_data,        sc_put_data },
	{ 0x0065, CONSTRUCTED, READ_ALWAYS | WRITE_NEVER, sc_get_data,        NULL        },
	{ 0x006e, CONSTRUCTED, READ_ALWAYS | WRITE_NEVER, sc_get_data,        NULL        },
	{ 0x0073, CONSTRUCTED, READ_ALWAYS | WRITE_NEVER, NULL,               NULL        },
	{ 0x007a, CONSTRUCTED, READ_ALWAYS | WRITE_NEVER, sc_get_data,        NULL        },
	{ 0x0081, SIMPLE,      READ_ALWAYS | WRITE_NEVER, NULL,               NULL        },
	{ 0x0082, SIMPLE,      READ_ALWAYS | WRITE_NEVER, NULL,               NULL        },
	{ 0x0093, SIMPLE,      READ_ALWAYS | WRITE_NEVER, NULL,               NULL        },
	{ 0x00c0, SIMPLE,      READ_ALWAYS | WRITE_NEVER, NULL,               NULL        },
	{ 0x00c1, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00c2, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00c3, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00c4, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  sc_get_data,        sc_put_data },
	{ 0x00c5, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00c6, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00c7, SIMPLE,      READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00c8, SIMPLE,      READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00c9, SIMPLE,      READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00ca, SIMPLE,      READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00cb, SIMPLE,      READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00cc, SIMPLE,      READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00cd, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00ce, SIMPLE,      READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00cf, SIMPLE,      READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00d0, SIMPLE,      READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00d1, SIMPLE,      READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00d2, SIMPLE,      READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00d3, SIMPLE,      READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x00f4, CONSTRUCTED, READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x0101, SIMPLE,      READ_ALWAYS | WRITE_PIN2,  sc_get_data,        sc_put_data },
	{ 0x0102, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  sc_get_data,        sc_put_data },
	{ 0x0103, SIMPLE,      READ_PIN2   | WRITE_PIN2,  sc_get_data,        sc_put_data },
	{ 0x0104, SIMPLE,      READ_PIN3   | WRITE_PIN3,  sc_get_data,        sc_put_data },
	{ 0x3f00, CONSTRUCTED, READ_ALWAYS | WRITE_NEVER, NULL,               NULL        },
	{ 0x5f2d, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x5f35, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x5f48, CONSTRUCTED, READ_NEVER  | WRITE_PIN3,  NULL,               sc_put_data },
	{ 0x5f50, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  sc_get_data,        sc_put_data },
	{ 0x5f52, SIMPLE,      READ_ALWAYS | WRITE_NEVER, sc_get_data,        NULL        },
	/* DO 7F21 is CONSTRUCTED in spec; we treat it as SIMPLE: no need to parse TLV */
	{ DO_CERT, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  sc_get_data,        sc_put_data },
	{ 0x7f48, CONSTRUCTED, READ_NEVER  | WRITE_NEVER, NULL,               NULL        },
	{ 0x7f49, CONSTRUCTED, READ_ALWAYS | WRITE_NEVER, NULL,               NULL        },
	{ DO_AUTH,     CONSTRUCTED, READ_ALWAYS | WRITE_NEVER, pgp_get_pubkey,     NULL   },
	/* The DOs 0xA401, 0xB601, 0xB801 are virtual DOs, they do not represent any real DO.
	 * However, their R/W access condition may block the process of importing key in pkcs15init.
	 * So we set their accesses condition as WRITE_PIN3 (writable). */
	{ DO_AUTH_SYM, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  pgp_get_pubkey_pem, NULL   },
	{ DO_SIGN,     CONSTRUCTED, READ_ALWAYS | WRITE_NEVER, pgp_get_pubkey,     NULL   },
	{ DO_SIGN_SYM, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  pgp_get_pubkey_pem, NULL   },
	{ DO_ENCR,     CONSTRUCTED, READ_ALWAYS | WRITE_NEVER, pgp_get_pubkey,     NULL   },
	{ DO_ENCR_SYM, SIMPLE,      READ_ALWAYS | WRITE_PIN3,  pgp_get_pubkey_pem, NULL   },
	{ 0, 0, 0, NULL, NULL },
};

static struct do_info		*pgp30_objects = pgp33_objects + 1;
static struct do_info		*pgp21_objects = pgp33_objects + 6;
static struct do_info		*pgp20_objects = pgp33_objects + 7;


#define DRVDATA(card)        ((struct pgp_priv_data *) ((card)->drv_data))

struct pgp_priv_data {
	pgp_blob_t		*mf;
	pgp_blob_t		*current;	/* currently selected file */

	enum _version		bcd_version;
	struct do_info		*pgp_objects;

	enum _card_state	state;		/* card state */
	enum _ext_caps		ext_caps;	/* extended capabilities */

	enum _sm_algo		sm_algo;	/* Secure Messaging algorithm */

	size_t			max_challenge_size;
	size_t			max_cert_size;

	sc_security_env_t	sec_env;
};


/**
 * Internal: get OpenPGP application identifier from AID DO 004F
 */
static int
get_full_pgp_aid(sc_card_t *card, sc_file_t *file)
{
	int r = SC_ERROR_INVALID_ARGUMENTS;

	if (file != NULL) {
		/* explicitly get the full aid */
		r = sc_get_data(card, 0x004F, file->name, sizeof file->name);
		file->namelen = MAX(r, 0);
	}

	return r;
}


#define BCD2UCHAR(x) (((((x) & 0xF0) >> 4) * 10) + ((x) & 0x0F))

/**
 * ABI: check if card's ATR matches one of driver's
 * or if the OpenPGP application is present on the card.
 */
static int
pgp_match_card(sc_card_t *card)
{
	int i;

	LOG_FUNC_CALLED(card->ctx);

	i = _sc_match_atr(card, pgp_atrs, &card->type);
	if (i >= 0) {
		card->name = pgp_atrs[i].name;
		LOG_FUNC_RETURN(card->ctx, 1);
	}
	else {
		sc_path_t	partial_aid;
		sc_file_t *file = NULL;

		/* select application "OpenPGP" */
		sc_format_path("D276:0001:2401", &partial_aid);
		partial_aid.type = SC_PATH_TYPE_DF_NAME;
		/* OpenPGP card only supports selection *with* requested FCI */
		i = iso_ops->select_file(card, &partial_aid, &file);
		if (SC_SUCCESS == i) {
			card->type = SC_CARD_TYPE_OPENPGP_BASE;
			card->name = default_cardname;

			if (file->namelen != 16)
				(void) get_full_pgp_aid(card, file);
			if (file->namelen == 16) {
				unsigned char major = BCD2UCHAR(file->name[6]);

				switch (major) {
					case 1:
						card->type = SC_CARD_TYPE_OPENPGP_V1;
						card->name = default_cardname_v1;
						break;
					case 2:
						card->type = SC_CARD_TYPE_OPENPGP_V2;
						card->name = default_cardname_v2;
						break;
					case 3:
						card->type = SC_CARD_TYPE_OPENPGP_V3;
						card->name = default_cardname_v3;
						break;
					default:
						break;
				}
			}
			sc_file_free(file);
			LOG_FUNC_RETURN(card->ctx, 1);
		}
	}
	LOG_FUNC_RETURN(card->ctx, 0);
}


/**
 * ABI: initialize driver & allocate private data.
 */
static int
pgp_init(sc_card_t *card)
{
	struct pgp_priv_data *priv;
	sc_path_t	path;
	sc_file_t	*file = NULL;
	struct do_info	*info;
	int		r, i;

	LOG_FUNC_CALLED(card->ctx);

	priv = calloc (1, sizeof *priv);
	if (!priv)
		LOG_FUNC_RETURN(card->ctx, SC_ERROR_OUT_OF_MEMORY);
	card->drv_data = priv;

	card->cla = 0x00;

	/* select application "OpenPGP" */
	sc_format_path("D276:0001:2401", &path);
	path.type = SC_PATH_TYPE_DF_NAME;
	if ((r = iso_ops->select_file(card, &path, &file)) < 0) {
		sc_file_free(file);
		pgp_finish(card);
		LOG_FUNC_RETURN(card->ctx, SC_ERROR_INVALID_CARD);
	}

	/* defensive programming check */
	if (!file)   {
		pgp_finish(card);
		LOG_FUNC_RETURN(card->ctx, SC_ERROR_INVALID_CARD);
	}

	if (file->namelen != 16) {
		/* explicitly get the full aid */
		r = get_full_pgp_aid(card, file);
		if (r < 0) {
			sc_file_free(file);
			pgp_finish(card);
			LOG_FUNC_RETURN(card->ctx, SC_ERROR_INVALID_CARD);
		}
	}

	/* read information from AID */
	if (file->namelen == 16) {
		static char card_name[SC_MAX_APDU_BUFFER_SIZE] = "OpenPGP card";

		/* OpenPGP card spec 1.1, 2.x & 3.x, section 4.2.1 & 4.1.2.1 */
		priv->bcd_version = bebytes2ushort(file->name + 6);
		card->version.fw_major = card->version.hw_major = BCD2UCHAR(file->name[6]);
		card->version.fw_minor = card->version.hw_minor = BCD2UCHAR(file->name[7]);

		/* for "standard" cards, include detailed card version & serial no. in card name */
		if (card->name == default_cardname_v1 ||
		    card->name == default_cardname_v2 ||
		    card->name == default_cardname_v3) {
			snprintf(card_name, sizeof(card_name),
				 "OpenPGP card v%u.%u (%04X %08lX)",
				 card->version.hw_major, card->version.hw_minor,
				 bebytes2ushort(file->name + 8),
				 bebytes2ulong(file->name + 10));
		}
		else if (card->name != NULL) {
			/* for other cards, append serial number to the card name */
			snprintf(card_name, sizeof(card_name),
				 "%s (%04X %08lX)",
				 card->name,
				 bebytes2ushort(file->name + 8),
				 bebytes2ulong(file->name + 10));
		}
		card->name = card_name;

		/* GPG compatibility: set card's serial number to manufacturer ID + serial number */
		memcpy(card->serialnr.value, file->name + 8, 6);
		card->serialnr.len = 6;
	} else {
		/* set detailed card version */
		switch (card->type) {
			case SC_CARD_TYPE_OPENPGP_V3:
				priv->bcd_version = OPENPGP_CARD_3_0;
				break;
			case SC_CARD_TYPE_OPENPGP_GNUK:
			case SC_CARD_TYPE_OPENPGP_V2:
				priv->bcd_version = OPENPGP_CARD_2_0;
				break;
			default:
				priv->bcd_version = OPENPGP_CARD_1_1;
				break;
		}
	}

	/* set pointer to correct list of card objects */
	priv->pgp_objects = (priv->bcd_version < OPENPGP_CARD_2_0) ? pgp1x_objects
			  : (priv->bcd_version < OPENPGP_CARD_2_1) ? pgp20_objects
			  : (priv->bcd_version < OPENPGP_CARD_3_0) ? pgp21_objects
			  : (priv->bcd_version < OPENPGP_CARD_3_3) ? pgp30_objects
			  :					     pgp33_objects;

	/* change file path to MF for re-use in MF */
	sc_format_path("3f00", &file->path);

	/* set up the root of our fake file tree */
	/* Transfers ownership of the file to the priv->mf structure */
	priv->mf = pgp_new_blob(card, NULL, 0x3f00, file);
	if (!priv->mf) {
		sc_file_free(file);
		pgp_finish(card);
		LOG_FUNC_RETURN(card->ctx, SC_ERROR_OUT_OF_MEMORY);
	}

	/* select MF */
	priv->current = priv->mf;

	/* populate MF - add matching blobs listed in the pgp_objects table */
	for (info = priv->pgp_objects; (info != NULL) && (info->id > 0); info++) {
		if (((info->access & READ_MASK) != READ_NEVER) && (info->get_fn != NULL)) {
			pgp_blob_t *child = NULL;

			child = pgp_new_blob(card, priv->mf, info->id, sc_file_new());

			/* catch out of memory condition */
			if (child == NULL) {
				pgp_finish(card);
				LOG_FUNC_RETURN(card->ctx, SC_ERROR_OUT_OF_MEMORY);
			}
		}
	}

	/* get card_features from ATR & DOs */
	pgp_get_card_features(card);

	/* if algorithm attributes can be changed,
	 * add supported algorithms based on specification for pkcs15-init */
	if ((priv->ext_caps & EXT_CAP_ALG_ATTR_CHANGEABLE) &&

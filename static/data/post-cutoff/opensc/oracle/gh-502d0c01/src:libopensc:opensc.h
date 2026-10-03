/*
 * opensc.h: OpenSC library header file
 *
 * Copyright (C) 2001, 2002  Juha Yrjölä <juha.yrjola@iki.fi>
 *               2005        The OpenSC project
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

/**
 * @file src/libopensc/opensc.h
 * OpenSC library core header file
 */

#ifndef _OPENSC_H
#define _OPENSC_H

#include <stdio.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include "common/simclist.h"
#include "scconf/scconf.h"
#include "libopensc/errors.h"
#include "libopensc/types.h"
#ifdef ENABLE_SM
#include "libopensc/sm.h"
#endif

#if defined(_WIN32) && !(defined(__MINGW32__) && defined (__MINGW_PRINTF_FORMAT))
#define SC_FORMAT_LEN_SIZE_T "I"
#define SC_FORMAT_LEN_PTRDIFF_T "I"
#else
/* hope SUSv3 ones work */
#define SC_FORMAT_LEN_SIZE_T "z"
#define SC_FORMAT_LEN_PTRDIFF_T "t"
#endif

#define SC_SEC_OPERATION_DECIPHER	0x0001
#define SC_SEC_OPERATION_SIGN		0x0002
#define SC_SEC_OPERATION_AUTHENTICATE	0x0003
#define SC_SEC_OPERATION_DERIVE         0x0004
#define SC_SEC_OPERATION_WRAP		0x0005
#define SC_SEC_OPERATION_UNWRAP		0x0006
#define SC_SEC_OPERATION_ENCRYPT_SYM	0x0007
#define SC_SEC_OPERATION_DECRYPT_SYM	0x0008
/* sc_security_env flags */
#define SC_SEC_ENV_ALG_REF_PRESENT	0x0001
#define SC_SEC_ENV_FILE_REF_PRESENT	0x0002
#define SC_SEC_ENV_KEY_REF_PRESENT	0x0004
#define SC_SEC_ENV_KEY_REF_SYMMETRIC	0x0008
#define SC_SEC_ENV_ALG_PRESENT		0x0010
#define SC_SEC_ENV_TARGET_FILE_REF_PRESENT 0x0020

/* sc_security_env additional parameters */
#define SC_SEC_ENV_MAX_PARAMS		10
#define SC_SEC_ENV_PARAM_IV		1
#define SC_SEC_ENV_PARAM_TARGET_FILE	2

/* PK algorithms */
#define SC_ALGORITHM_RSA		0
#define SC_ALGORITHM_EC		2
#define SC_ALGORITHM_GOSTR3410		3
#define SC_ALGORITHM_EDDSA		4
#define SC_ALGORITHM_XEDDSA		5

/* Symmetric algorithms */
#define SC_ALGORITHM_DES		64
#define SC_ALGORITHM_3DES		65
#define SC_ALGORITHM_GOST		66
#define SC_ALGORITHM_AES		67
#define SC_ALGORITHM_UNDEFINED		68	/* used with CKK_GENERIC_SECRET type keys */

/* Hash algorithms */
#define SC_ALGORITHM_MD5		128
#define SC_ALGORITHM_SHA1		129
#define SC_ALGORITHM_GOSTR3411		130

/* Key derivation algorithms */
#define SC_ALGORITHM_PBKDF2		192

/* Key encryption algorithms */
#define SC_ALGORITHM_PBES2		256

#define SC_ALGORITHM_ONBOARD_KEY_GEN	0x80000000
/* need usage = either sign or decrypt. keys with both? decrypt, emulate sign */
#define SC_ALGORITHM_NEED_USAGE		0x40000000
#define SC_ALGORITHM_SPECIFIC_FLAGS	0x001FFFFF

/* If the card is willing to produce a cryptogram padded with the following
 * methods, set these flags accordingly.  These flags are exclusive: an RSA card
 * must support at least one of them, and exactly one of them must be selected
 * for a given operation. */
#define SC_ALGORITHM_RSA_RAW		0x00000001
#define SC_ALGORITHM_RSA_PADS		0x000000FF
#define SC_ALGORITHM_RSA_PAD_NONE	0x00000001
#define SC_ALGORITHM_RSA_PAD_ANSI	0x00000004
#define SC_ALGORITHM_RSA_PAD_ISO9796	0x00000008
#define SC_ALGORITHM_RSA_PAD_PSS	0x00000010 /* PKCS#1 v2.0 PSS */
#define SC_ALGORITHM_RSA_PAD_OAEP	0x00000020 /* PKCS#1 v2.0 OAEP */
#define SC_ALGORITHM_RSA_PAD_PKCS1_TYPE_01	0x00000040 /* PKCS#1 v1.5 padding type 1 */
#define SC_ALGORITHM_RSA_PAD_PKCS1_TYPE_02	0x00000080 /* PKCS#1 v1.5 padding type 2 */
#define SC_ALGORITHM_RSA_PAD_PKCS1	(SC_ALGORITHM_RSA_PAD_PKCS1_TYPE_01 | SC_ALGORITHM_RSA_PAD_PKCS1_TYPE_02) /* PKCS#1 v1.5 (type 1 or 2) */

/* If the card is willing to produce a cryptogram with the following
 * hash values, set these flags accordingly.  The interpretation of the hash
 * flags depends on the algorithm and padding chosen: for RSA, the hash flags
 * determine how the padding is constructed and do not describe the first
 * hash applied to the document before padding begins.
 *
 *   - For PAD_NONE, ANSI X9.31, (and ISO9796?), the hash value is therefore
 *     ignored.  For ANSI X9.31, the input data must already have the hash
 *     identifier byte appended (eg 0x33 for SHA-1).
 *   - For PKCS1 (v1.5) the hash is recorded in the padding, and HASH_NONE is a
 *     valid value, meaning that the hash's DigestInfo has already been
 *     prepended to the data, otherwise the hash id is put on the front.
 *   - For PSS (PKCS#1 v2.0) the hash is used to derive the padding from the
 *     already-hashed message.
 *
 * In no case is the hash actually applied to the entire document.
 *
 * It's possible that the card may support different hashes for PKCS1 and PSS
 * signatures; in this case the card driver has to pick the lowest-denominator
 * when it sets these flags to indicate its capabilities. */
#define SC_ALGORITHM_RSA_HASH_NONE	0x00000100 /* only applies to PKCS1 padding */
#define SC_ALGORITHM_RSA_HASH_SHA1	0x00000200
#define SC_ALGORITHM_RSA_HASH_MD5	0x00000400
#define SC_ALGORITHM_RSA_HASH_MD5_SHA1	0x00000800
#define SC_ALGORITHM_RSA_HASH_RIPEMD160	0x00001000
#define SC_ALGORITHM_RSA_HASH_SHA256	0x00002000
#define SC_ALGORITHM_RSA_HASH_SHA384	0x00004000
#define SC_ALGORITHM_RSA_HASH_SHA512	0x00008000
#define SC_ALGORITHM_RSA_HASH_SHA224	0x00010000
#define SC_ALGORITHM_RSA_HASHES		0x0001FF00

/* This defines the hashes to be used with MGF1 in PSS padding */
#define SC_ALGORITHM_MGF1_SHA1		0x00100000
#define SC_ALGORITHM_MGF1_SHA256	0x00200000
#define SC_ALGORITHM_MGF1_SHA384	0x00400000
#define SC_ALGORITHM_MGF1_SHA512	0x00800000
#define SC_ALGORITHM_MGF1_SHA224	0x01000000
#define SC_ALGORITHM_MGF1_HASHES	0x01F00000

/* These flags are exclusive: a GOST R34.10 card must support at least one or the
 * other of the methods, and exactly one of them applies to any given operation.
 * Note that the GOST R34.11 hash is actually applied to the data (ie if this
 * algorithm is chosen the entire unhashed document is passed in). */
#define SC_ALGORITHM_GOSTR3410_RAW		0x00020000
#define SC_ALGORITHM_GOSTR3410_HASH_NONE	SC_ALGORITHM_GOSTR3410_RAW /*XXX*/
#define SC_ALGORITHM_GOSTR3410_HASH_GOSTR3411	0x00080000
#define SC_ALGORITHM_GOSTR3410_HASHES		0x000A0000
/*TODO: -DEE Should the above be 0x000E0000 */
/* Or should the HASH_NONE be 0x00000100  and HASHES be 0x00080010 */

/* The ECDSA flags are exclusive, and exactly one of them applies to any given
 * operation.  If ECDSA with a hash is specified, then the data passed in is
 * the entire document, unhashed, and the hash is applied once to it before
 * truncating and signing.  These flags are distinct from the RSA hash flags,
 * which determine the hash ids the card is willing to put in RSA message
 * padding. */
/* May need more bits if card can do more hashes */
/* TODO: -DEE Will overload RSA_HASHES with EC_HASHES */
/* Not clear if these need their own bits or not */
/* The PIV card does not support and hashes */
#define SC_ALGORITHM_ECDH_CDH_RAW	0x00200000
#define SC_ALGORITHM_ECDSA_RAW		0x00100000
#define SC_ALGORITHM_ECDSA_HASH_NONE		SC_ALGORITHM_RSA_HASH_NONE
#define SC_ALGORITHM_ECDSA_HASH_SHA1		SC_ALGORITHM_RSA_HASH_SHA1
#define SC_ALGORITHM_ECDSA_HASH_SHA224		SC_ALGORITHM_RSA_HASH_SHA224
#define SC_ALGORITHM_ECDSA_HASH_SHA256		SC_ALGORITHM_RSA_HASH_SHA256
#define SC_ALGORITHM_ECDSA_HASH_SHA384		SC_ALGORITHM_RSA_HASH_SHA384
#define SC_ALGORITHM_ECDSA_HASH_SHA512		SC_ALGORITHM_RSA_HASH_SHA512
#define SC_ALGORITHM_ECDSA_HASHES		(SC_ALGORITHM_ECDSA_HASH_SHA1 | \
							SC_ALGORITHM_ECDSA_HASH_SHA224 | \
							SC_ALGORITHM_ECDSA_HASH_SHA256 | \
							SC_ALGORITHM_ECDSA_HASH_SHA384 | \
							SC_ALGORITHM_ECDSA_HASH_SHA512)

/* EdDSA algorithms */
#define SC_ALGORITHM_EDDSA_RAW		0x00400000
#define SC_ALGORITHM_XEDDSA_RAW		0x00800000

/* define mask of all algorithms that can do raw */
#define SC_ALGORITHM_RAW_MASK (SC_ALGORITHM_RSA_RAW | \
                               SC_ALGORITHM_GOSTR3410_RAW | \
                               SC_ALGORITHM_ECDH_CDH_RAW | \
                               SC_ALGORITHM_ECDSA_RAW)

/* extended algorithm bits for selected mechs */
#define SC_ALGORITHM_EXT_EC_F_P          0x00000001
#define SC_ALGORITHM_EXT_EC_F_2M         0x00000002
#define SC_ALGORITHM_EXT_EC_ECPARAMETERS 0x00000004
#define SC_ALGORITHM_EXT_EC_NAMEDCURVE   0x00000008
#define SC_ALGORITHM_EXT_EC_UNCOMPRESES  0x00000010
#define SC_ALGORITHM_EXT_EC_COMPRESS     0x00000020

/* symmetric algorithm flags. More algorithms to be added when implemented. */
#define SC_ALGORITHM_AES_ECB		 0x01000000
#define SC_ALGORITHM_AES_CBC		 0x02000000
#define SC_ALGORITHM_AES_CBC_PAD	 0x04000000
#define SC_ALGORITHM_AES_FLAGS		 0x0F000000


/* Event masks for sc_wait_for_event() */
#define SC_EVENT_CARD_INSERTED		0x0001
#define SC_EVENT_CARD_REMOVED		0x0002
#define SC_EVENT_CARD_EVENTS		(SC_EVENT_CARD_INSERTED|SC_EVENT_CARD_REMOVED)
#define SC_EVENT_READER_ATTACHED	0x0004
#define SC_EVENT_READER_DETACHED	0x0008
#define SC_EVENT_READER_EVENTS		(SC_EVENT_READER_ATTACHED|SC_EVENT_READER_DETACHED)

#define MAX_FILE_SIZE 65535

#define ED448_KEY_SIZE_BYTES 57U

struct sc_supported_algo_info {
	unsigned int reference;
	unsigned int mechanism;
	struct sc_object_id parameters; /* OID for ECC */
	unsigned int operations;
	struct sc_object_id algo_id;
	unsigned int algo_ref;
};

typedef struct sc_sec_env_param {
	unsigned int param_type;
	void* value;
	size_t value_len;
} sc_sec_env_param_t;


typedef struct sc_security_env {
	unsigned long flags;
	int operation;
	unsigned long algorithm, algorithm_flags;
	size_t key_size_bits;

	unsigned long algorithm_ref;
	struct sc_path file_ref;
	unsigned char key_ref[SC_MAX_KEYREF_SIZE];
	size_t key_ref_len;
	struct sc_path target_file_ref; /* target key file in unwrap operation */

	struct sc_supported_algo_info supported_algos[SC_MAX_SUPPORTED_ALGORITHMS];
	/* optional parameters */
	struct sc_sec_env_param params[SC_SEC_ENV_MAX_PARAMS];
} sc_security_env_t;

struct sc_algorithm_id {
	unsigned long algorithm;
	struct sc_object_id oid;
	void *params;
};

struct sc_pbkdf2_params {
	u8 salt[16];
	size_t salt_len;
	int iterations;
	size_t key_length;
	struct sc_algorithm_id hash_alg;
};

struct sc_pbes2_params {
	struct sc_algorithm_id derivation_alg;
	struct sc_algorithm_id key_encr_alg;
};

/*
 * PKCS11 2.3 Elliptic Curve lists mechanisms that use CKA_EC_PARAMS
 * which implies the type of key and size needed in the OID
 * The ecParameters can be presented as
 * - name of curve;
 * - OID of named curve;
 * - implicit parameters.
 * - printable string for non standard OIDS - added in pkcs11 3.0
 *
 * type - type(choice) of 'EC domain parameters' as it present in CKA_EC_PARAMS (PKCS#11).
 *	Recommended value '1' -- namedCurve.
 * field_length - EC key size in bits.
 * key_type - 0 implies SC_ALGORITHM_EC, SC_ALGORITHM_EDDSA or SC_ALGORITHM_XEDDSA
 *	Not actually part of CKA_EC_PARAMS - used in OpenSC to differentiate key types that use ec_params
 *	will be set by sc_pkcs15_fix_ec_parameters
 */
struct sc_ec_parameters {
	char *named_curve;
	struct sc_object_id id;
	struct sc_lv_data der;

	int type;
	size_t field_length;
	unsigned int key_type;
};

typedef struct sc_algorithm_info {
	unsigned int algorithm;
	size_t key_length;
	unsigned long flags;

	union {
		struct sc_rsa_info {
			unsigned long exponent;
		} _rsa;
		struct sc_ec_info {
			unsigned long ext_flags;
			struct sc_ec_parameters params;
		} _ec;
	} u;
} sc_algorithm_info_t;

typedef struct sc_app_info {
	char *label;

	struct sc_aid aid;
	struct sc_ddo ddo;

	struct sc_path path;

	int rec_nr;		/* -1, if EF(DIR) is transparent */
} sc_app_info_t;

struct sc_ef_atr {
	unsigned char card_service;
	unsigned char df_selection;
	size_t unit_size;
	unsigned char card_capabilities;
	size_t max_command_apdu;
	size_t max_response_apdu;

	struct sc_aid aid;

	unsigned char pre_issuing[6];
	size_t pre_issuing_len;

	unsigned char issuer_data[16];
	size_t issuer_data_len;

	struct sc_object_id allocation_oid;

	unsigned status;
};

#define SC_PROTO_T0		0x00000001
#define SC_PROTO_T1		0x00000002
#define SC_PROTO_RAW		0x00001000
#define SC_PROTO_ANY		0xFFFFFFFF

struct sc_reader_driver {
	const char *name;
	const char *short_name;
	struct sc_reader_operations *ops;

	void *dll;
};

/* reader flags */
#define SC_READER_CARD_PRESENT		0x00000001
#define SC_READER_CARD_CHANGED		0x00000002
#define SC_READER_CARD_INUSE		0x00000004
#define SC_READER_CARD_EXCLUSIVE	0x00000008
#define SC_READER_HAS_WAITING_AREA	0x00000010
#define SC_READER_REMOVED			0x00000020
#define SC_READER_ENABLE_ESCAPE		0x00000040

/* reader capabilities */
#define SC_READER_CAP_DISPLAY	0x00000001
#define SC_READER_CAP_PIN_PAD	0x00000002
#define SC_READER_CAP_PACE_EID             0x00000004
#define SC_READER_CAP_PACE_ESIGN           0x00000008
#define SC_READER_CAP_PACE_DESTROY_CHANNEL 0x00000010
#define SC_READER_CAP_PACE_GENERIC         0x00000020

/* reader send/receive length of short APDU */
#define SC_READER_SHORT_APDU_MAX_SEND_SIZE 255
#define SC_READER_SHORT_APDU_MAX_RECV_SIZE 256

typedef struct sc_reader {
	struct sc_context *ctx;
	const struct sc_reader_driver *driver;
	const struct sc_reader_operations *ops;
	void *drv_data;
	char *name;
	char *vendor;
	unsigned char version_major;
	unsigned char version_minor;

	unsigned long flags, capabilities;
	unsigned int supported_protocols, active_protocol;
	size_t max_send_size; /* Max Lc supported by the reader layer */
	size_t max_recv_size; /* Max Le supported by the reader layer */

	struct sc_atr atr;
	struct sc_uid uid;
	struct _atr_info {
		u8 *hist_bytes;
		size_t hist_bytes_len;
		int Fi, f, Di, N;
		u8 FI, DI;
	} atr_info;
} sc_reader_t;

/* This will be the new interface for handling PIN commands.
 * It is supposed to support pin pads (with or without display)
 * attached to the reader.
 */
#define SC_PIN_CMD_VERIFY	0
#define SC_PIN_CMD_CHANGE	1
#define SC_PIN_CMD_UNBLOCK	2
#define SC_PIN_CMD_GET_INFO	3
#define SC_PIN_CMD_GET_SESSION_PIN	4

#define SC_PIN_CMD_USE_PINPAD		0x0001
#define SC_PIN_CMD_NEED_PADDING		0x0002
#define SC_PIN_CMD_IMPLICIT_CHANGE	0x0004

#define SC_PIN_ENCODING_ASCII	0
#define SC_PIN_ENCODING_BCD	1
#define SC_PIN_ENCODING_GLP	2 /* Global Platform - Card Specification v2.0.1 */

/** Values for sc_pin_cmd_pin.logged_in, can be bitmapped together */
#define SC_PIN_STATE_UNKNOWN	0
#define SC_PIN_STATE_LOGGED_OUT 1
#define SC_PIN_STATE_LOGGED_IN  2
#define SC_PIN_STATE_NEEDS_CHANGE 4

/* A card driver receives the sc_pin_cmd_data and sc_pin_cmd_pin structures filled in by the
 * caller, with the exception of the fields returned by the driver for SC_PIN_CMD_GET_INFO.
 * It may use and update any of the fields before passing the structure to the ISO 7816 layer for
 * processing.
 */
struct sc_pin_cmd_pin {
	const char *prompt;	/* Prompt to display */

	const unsigned char *data; /* PIN, set to NULL when using pin pad */
	size_t len;		/* set to 0 when using pin pad */

	size_t min_length;	/* min length of PIN */
	size_t max_length;	/* max length of PIN */

	unsigned int encoding;	/* ASCII-numeric, BCD, etc */

	size_t pad_length;	/* PIN padding options, used with SC_PIN_CMD_NEED_PADDING */
	unsigned char pad_char;

	size_t offset;		/* PIN offset in the APDU when using pin pad */

	int max_tries;		/* Used for signaling back from SC_PIN_CMD_GET_INFO */
	int tries_left;		/* Used for signaling back from SC_PIN_CMD_GET_INFO */
	int logged_in;		/* Used for signaling back from SC_PIN_CMD_GET_INFO */
};

/* A NULL in apdu means that the APDU is prepared by the ISO 7816 layer, which also handles PIN
 * padding and setting offset fields for the PINs (for PIN-pad use). A non-NULL in APDU means that
 * the card driver has prepared the APDU (including padding) and set the PIN offset fields.
 *
 * Note that flags apply to both PINs for multi-PIN operations.
 */
struct sc_pin_cmd_data {
	unsigned int cmd;
	unsigned int flags;

	unsigned int pin_type;		/* usually SC_AC_CHV */
	int pin_reference;
	int puk_reference;		/* non-zero means that reference is available */

	struct sc_pin_cmd_pin pin1, pin2;

	struct sc_apdu *apdu;		/* APDU of the PIN command */
};

struct sc_reader_operations {
	/* Called during sc_establish_context(), when the driver
	 * is loaded */
	int (*init)(struct sc_context *ctx);
	/* Called when the driver is being unloaded.  finish() has to
	 * release any resources. */
	int (*finish)(struct sc_context *ctx);
	/* Called when library wish to detect new readers
	 * should add only new readers. */
	int (*detect_readers)(struct sc_context *ctx);
	int (*cancel)(struct sc_context *ctx);
	/* Called when releasing a reader.  release() has to
	 * deallocate the private data.  Other fields will be
	 * freed by OpenSC. */
	int (*release)(struct sc_reader *reader);

	int (*detect_card_presence)(struct sc_reader *reader);
	int (*connect)(struct sc_reader *reader);
	int (*disconnect)(struct sc_reader *reader);
	int (*transmit)(struct sc_reader *reader, sc_apdu_t *apdu);
	int (*lock)(struct sc_reader *reader);
	int (*unlock)(struct sc_reader *reader);
	int (*set_protocol)(struct sc_reader *reader, unsigned int proto);
	/* Pin pad functions */
	int (*display_message)(struct sc_reader *, const char *);
	int (*perform_verify)(struct sc_reader *, struct sc_pin_cmd_data *);
	int (*perform_pace)(struct sc_reader *reader,
			void *establish_pace_channel_input,
			void *establish_pace_channel_output);

	/* Wait for an event */
	int (*wait_for_event)(struct sc_context *ctx, unsigned int event_mask,
			sc_reader_t **event_reader, unsigned int *event,
			int timeout, void **reader_states);
	/* Reset a reader */
	int (*reset)(struct sc_reader *, int);
	/* Used to pass in PC/SC handles to minidriver */
	int (*use_reader)(struct sc_context *ctx, void *pcsc_context_handle, void *pcsc_card_handle);
};

/*
 * Card flags
 *
 * Used to hint about card specific capabilities and algorithms
 * supported to the card driver. Used in sc_atr_table and
 * card_atr block structures in the configuration file.
 *
 * Unknown, card vendor specific values may exists, but must
 * not conflict with values defined here. All actions defined
 * by the flags must be handled by the card driver themselves.
 */

/* Mask for card vendor specific values */
#define SC_CARD_FLAG_VENDOR_MASK	0xFFFF0000

/* Hint SC_CARD_CAP_RNG */
#define SC_CARD_FLAG_RNG		0x00000002
#define SC_CARD_FLAG_KEEP_ALIVE	0x00000004

/*
 * Card capabilities
 */

/* Card can handle large (> 256 bytes) buffers in calls to
 * read_binary, write_binary and update_binary; if not,
 * several successive calls to the corresponding function
 * is made. */
#define SC_CARD_CAP_APDU_EXT		0x00000001

/* Card has on-board random number source. */
#define SC_CARD_CAP_RNG			0x00000004

/* Card supports ISO7816 PIN status queries using an empty VERIFY */
#define SC_CARD_CAP_ISO7816_PIN_INFO	0x00000008

/* Use the card's ACs in sc_pkcs15init_authenticate(),
 * instead of relying on the ACL info in the profile files. */
#define SC_CARD_CAP_USE_FCI_AC		0x00000010

/* Card (or card driver) supports an protected authentication mechanism */
#define SC_CARD_CAP_PROTECTED_AUTHENTICATION_PATH	0x00000100

/* Card (or card driver) supports generating a session PIN */
#define SC_CARD_CAP_SESSION_PIN	0x00000200

/* Card and driver supports handling on card session objects.
 * If a driver has this capability, the driver handles storage and operations
 * with objects that CKA_TOKEN set to FALSE. If a driver doesn't support this,
 * OpenSC handles them as in memory objects.*/
#define SC_CARD_CAP_ONCARD_SESSION_OBJECTS	0x00000400

/* Card (or card driver) supports key wrapping operations */
#define SC_CARD_CAP_WRAP_KEY			0x00000800
/* Card (or card driver) supports key unwrapping operations */
#define SC_CARD_CAP_UNWRAP_KEY			0x00001000

typedef struct sc_card {
	struct sc_context *ctx;
	struct sc_reader *reader;

	struct sc_atr atr;
	struct sc_uid uid;

	int type;			/* Card type, for card driver internal use */
	unsigned long caps, flags;
	int cla;
	size_t max_send_size; /* Max Lc supported by the card */
	size_t max_recv_size; /* Max Le supported by the card */

	struct sc_app_info *app[SC_MAX_CARD_APPS];
	int app_count;

	struct sc_ef_atr *ef_atr;

	struct sc_algorithm_info *algorithms;
	int algorithm_count;

	int lock_count;

	struct sc_card_driver *driver;
	struct sc_card_operations *ops;
	const char *name;
	void *drv_data;
	int max_pin_len;

	struct sc_serial_number serialnr;
	struct sc_version version;

	void *mutex;
#ifdef ENABLE_SM
	struct sm_context sm_ctx;
#endif

	unsigned int magic;
} sc_card_t;

struct sc_card_operations {
	/** @brief Match a card with the given card driver.
	 *
	 * Called in sc_connect_card().  Must return 1, if the current
	 * card can be handled with this driver, or 0 otherwise.  ATR
	 * field of the sc_card struct is filled in before calling
	 * this function. It is recommended not to modify `card` during this call.
	 * */
	int (*match_card)(struct sc_card *card);

	/** @brief Initialize a card.
	 *
	 * Called when ATR of the inserted card matches an entry in ATR
	 * table.  May return SC_ERROR_INVALID_CARD to indicate that
	 * the card cannot be handled with this driver. drv_data may be used to
	 * store card driver's (allocated) private data. */
	int (*init)(struct sc_card *card);
	/** @brief Deinitialize a card.
	 *
	 * Called when the `card` object is being freed.  finish() has to
	 * deallocate all possible private data. */
	int (*finish)(struct sc_card *card);

	/* ISO 7816-4 functions */

	/**
	 * @brief Read data from a binary EF with a single command
	 *
	 * Implementation of this call back is optional and may be NULL.
	 *
	 * @param  card   struct sc_card object on which to issue the command
	 * @param  idx    index within the file with the data to read
	 * @param  buf    buffer to the read data
	 * @param  count  number of bytes to read
	 * @param  flags  flags for the READ BINARY command (optional)
	 * @return number of bytes read or an error code
	 *
	 * @see sc_read_binary()
	 */
	int (*read_binary)(struct sc_card *card, unsigned int idx,
			u8 * buf, size_t count, unsigned long *flags);
	/**
	 * @brief Write data to a binary EF with a single command
	 *
	 * Implementation of this call back is optional and may be NULL.
	 *
	 * @param  card   struct sc_card object on which to issue the command
	 * @param  idx    index within the file for the data to be written
	 * @param  buf    buffer with the data
	 * @param  count  number of bytes to write
	 * @param  flags  flags for the WRITE BINARY command (currently not used)
	 * @return number of bytes written or an error code
	 *
	 * @see sc_write_binary()
	 */
	int (*write_binary)(struct sc_card *card, unsigned int idx,
				const u8 * buf, size_t count, unsigned long flags);
	/** @brief Updates the content of a binary EF
	 *
	 * Implementation of this call back is optional and may be NULL.
	 *
	 * @param  card   struct sc_card object on which to issue the command
	 * @param  idx    index within the file for the data to be updated
	 * @param  buf    buffer with the new data
	 * @param  count  number of bytes to update
	 * @param  flags  flags for the UPDATE BINARY command (currently not used)
	 * @return number of bytes written or an error code
	 *
	 * @see sc_update_binary()
	 */
	int (*update_binary)(struct sc_card *card, unsigned int idx,
			     const u8 * buf, size_t count, unsigned long flags);
	/**

/*
 * pkcs11-tool.c: Tool for poking around pkcs11 modules/tokens
 *
 * Copyright (C) 2002  Olaf Kirch <okir@suse.de>
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

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <windows.h>
#include <io.h>
#endif

#ifdef ENABLE_OPENSSL
#include <openssl/opensslv.h>
#if OPENSSL_VERSION_NUMBER >= 0x10000000L
#include <openssl/opensslconf.h>
#include <openssl/crypto.h>
#endif
#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/asn1t.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#if !defined(OPENSSL_NO_EC) && !defined(OPENSSL_NO_ECDSA)
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#endif
#include <openssl/bn.h>
#include <openssl/err.h>
#endif

#include "pkcs11/pkcs11.h"
#include "pkcs11/pkcs11-opensc.h"
#include "libopensc/asn1.h"
#include "common/compat_strlcat.h"
#include "common/compat_strlcpy.h"
#include "common/libpkcs11.h"
#include "util.h"
#include "libopensc/sc-ossl-compat.h"

#ifdef _WIN32
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#endif

#ifndef ENABLE_SHARED
extern CK_FUNCTION_LIST pkcs11_function_list;
#endif

#define NEED_SESSION_RO	0x01
#define NEED_SESSION_RW	0x02

static struct ec_curve_info {
	const char *name;
	const char *oid;
	const char *oid_encoded;
	size_t size;
} ec_curve_infos[] = {
	{"secp192r1",    "1.2.840.10045.3.1.1", "06082A8648CE3D030101", 192},
	{"prime192v1",   "1.2.840.10045.3.1.1", "06082A8648CE3D030101", 192},
	{"prime192v2",   "1.2.840.10045.3.1.2", "06082A8648CE3D030102", 192},
	{"prime192v3",   "1.2.840.10045.3.1.3", "06082A8648CE3D030103", 192},
	{"nistp192",     "1.2.840.10045.3.1.1", "06082A8648CE3D030101", 192},
	{"ansiX9p192r1", "1.2.840.10045.3.1.1", "06082A8648CE3D030101", 192},

	{"secp224r1", "1.3.132.0.33", "06052b81040021", 224},
	{"nistp224",  "1.3.132.0.33", "06052b81040021", 224},

	{"prime256v1",   "1.2.840.10045.3.1.7", "06082A8648CE3D030107", 256},
	{"secp256r1",    "1.2.840.10045.3.1.7", "06082A8648CE3D030107", 256},
	{"ansiX9p256r1", "1.2.840.10045.3.1.7", "06082A8648CE3D030107", 256},
	{"frp256v1",	 "1.2.250.1.223.101.256.1", "060a2a817a01815f65820001", 256},

	{"secp384r1",		"1.3.132.0.34", "06052B81040022", 384},
	{"prime384v1",		"1.3.132.0.34", "06052B81040022", 384},
	{"ansiX9p384r1",	"1.3.132.0.34", "06052B81040022", 384},

	{"secp521r1", "1.3.132.0.35", "06052B81040023", 521},
	{"nistp521",  "1.3.132.0.35", "06052B81040023", 521},

	{"brainpoolP192r1", "1.3.36.3.3.2.8.1.1.3", "06092B2403030208010103", 192},
	{"brainpoolP224r1", "1.3.36.3.3.2.8.1.1.5", "06092B2403030208010105", 224},
	{"brainpoolP256r1", "1.3.36.3.3.2.8.1.1.7", "06092B2403030208010107", 256},
	{"brainpoolP320r1", "1.3.36.3.3.2.8.1.1.9", "06092B2403030208010109", 320},
	{"brainpoolP384r1", "1.3.36.3.3.2.8.1.1.11", "06092B240303020801010B", 384},
	{"brainpoolP512r1", "1.3.36.3.3.2.8.1.1.13", "06092B240303020801010D", 512},

	{"secp192k1",		"1.3.132.0.31", "06052B8104001F", 192},
	{"secp256k1",		"1.3.132.0.10", "06052B8104000A", 256},
	{NULL, NULL, NULL, 0},
};

static const struct sc_aid GOST_HASH2001_PARAMSET_OID = { { 0x06, 0x07, 0x2a, 0x85, 0x03, 0x02, 0x02, 0x1e, 0x01 }, 9 };
static const struct sc_aid GOST_HASH2012_256_PARAMSET_OID = { { 0x06, 0x08, 0x2A, 0x85, 0x03, 0x07, 0x01, 0x01, 0x02, 0x02 }, 10 };
static const struct sc_aid GOST_HASH2012_512_PARAMSET_OID = { { 0x06, 0x08, 0x2A, 0x85, 0x03, 0x07, 0x01, 0x01, 0x02, 0x03 }, 10 };

enum {
	OPT_MODULE = 0x100,
	OPT_SLOT,
	OPT_SLOT_DESCRIPTION,
	OPT_SLOT_INDEX,
	OPT_TOKEN_LABEL,
	OPT_APPLICATION_LABEL,
	OPT_APPLICATION_ID,
	OPT_ISSUER,
	OPT_SUBJECT,
	OPT_SO_PIN,
	OPT_INIT_TOKEN,
	OPT_INIT_PIN,
	OPT_ATTR_FROM,
	OPT_KEY_TYPE,
	OPT_KEY_USAGE_SIGN,
	OPT_KEY_USAGE_DECRYPT,
	OPT_KEY_USAGE_DERIVE,
	OPT_PRIVATE,
	OPT_SENSITIVE,
	OPT_EXTRACTABLE,
	OPT_TEST_HOTPLUG,
	OPT_UNLOCK_PIN,
	OPT_PUK,
	OPT_NEW_PIN,
	OPT_LOGIN_TYPE,
	OPT_TEST_EC,
	OPT_DERIVE,
	OPT_DERIVE_PASS_DER,
	OPT_DECRYPT,
	OPT_TEST_FORK,
	OPT_GENERATE_KEY,
	OPT_GENERATE_RANDOM,
	OPT_HASH_ALGORITHM,
	OPT_MGF,
	OPT_SALT,
	OPT_VERIFY,
	OPT_SIGNATURE_FILE,
	OPT_ALWAYS_AUTH,
	OPT_ALLOWED_MECHANISMS,
	OPT_OBJECT_INDEX
};

static const struct option options[] = {
	{ "module",		1, NULL,		OPT_MODULE },
	{ "show-info",		0, NULL,		'I' },
	{ "list-slots",		0, NULL,		'L' },
	{ "list-token-slots",	0, NULL,		'T' },
	{ "list-mechanisms",	0, NULL,		'M' },
	{ "list-objects",	0, NULL,		'O' },

	{ "sign",		0, NULL,		's' },
	{ "verify",		0, NULL,		OPT_VERIFY },
	{ "decrypt",		0, NULL,		OPT_DECRYPT },
	{ "hash",		0, NULL,		'h' },
	{ "derive",		0, NULL,		OPT_DERIVE },
	{ "derive-pass-der",	0, NULL,		OPT_DERIVE_PASS_DER },
	{ "mechanism",		1, NULL,		'm' },
	{ "hash-algorithm",	1, NULL,		OPT_HASH_ALGORITHM },
	{ "mgf",		1, NULL,		OPT_MGF },
	{ "salt-len",		1, NULL,		OPT_SALT },

	{ "login",		0, NULL,		'l' },
	{ "login-type",		1, NULL,		OPT_LOGIN_TYPE },
	{ "pin",		1, NULL,		'p' },
	{ "puk",		1, NULL,		OPT_PUK },
	{ "new-pin",		1, NULL,		OPT_NEW_PIN },
	{ "so-pin",		1, NULL,		OPT_SO_PIN },
	{ "init-token",		0, NULL,		OPT_INIT_TOKEN },
	{ "init-pin",		0, NULL,		OPT_INIT_PIN },
	{ "change-pin",		0, NULL,		'c' },
	{ "unlock-pin",		0, NULL,		OPT_UNLOCK_PIN },
	{ "keypairgen",		0, NULL,		'k' },
	{ "keygen",		0, NULL,		OPT_GENERATE_KEY },
	{ "key-type",		1, NULL,		OPT_KEY_TYPE },
	{ "usage-sign",		0, NULL,		OPT_KEY_USAGE_SIGN },
	{ "usage-decrypt",	0, NULL,		OPT_KEY_USAGE_DECRYPT },
	{ "usage-derive",	0, NULL,		OPT_KEY_USAGE_DERIVE },
	{ "write-object",	1, NULL,		'w' },
	{ "read-object",	0, NULL,		'r' },
	{ "delete-object",	0, NULL,		'b' },
	{ "application-label",	1, NULL,		OPT_APPLICATION_LABEL },
	{ "application-id",	1, NULL,		OPT_APPLICATION_ID },
	{ "issuer",		1, NULL,		OPT_ISSUER },
	{ "subject",		1, NULL,		OPT_SUBJECT },
	{ "type",		1, NULL,		'y' },
	{ "id",			1, NULL,		'd' },
	{ "label",		1, NULL,		'a' },
	{ "slot",		1, NULL,		OPT_SLOT },
	{ "slot-description",	1, NULL,		OPT_SLOT_DESCRIPTION },
	{ "slot-index",		1, NULL,		OPT_SLOT_INDEX },
	{ "object-index",		1, NULL,		OPT_OBJECT_INDEX },
	{ "token-label",	1, NULL,		OPT_TOKEN_LABEL },
	{ "set-id",		1, NULL,		'e' },
	{ "attr-from",		1, NULL,		OPT_ATTR_FROM },
	{ "input-file",		1, NULL,		'i' },
	{ "signature-file",	1, NULL,		OPT_SIGNATURE_FILE },
	{ "output-file",	1, NULL,		'o' },
	{ "signature-format",	1, NULL,		'f' },
	{ "allowed-mechanisms",	1, NULL,		OPT_ALLOWED_MECHANISMS },

	{ "test",		0, NULL,		't' },
	{ "test-hotplug",	0, NULL,		OPT_TEST_HOTPLUG },
	{ "moz-cert",		1, NULL,		'z' },
	{ "verbose",		0, NULL,		'v' },
	{ "private",		0, NULL,		OPT_PRIVATE },
	{ "sensitive",		0, NULL,		OPT_SENSITIVE },
	{ "extractable",	0, NULL,		OPT_EXTRACTABLE },
	{ "always-auth",	0, NULL,		OPT_ALWAYS_AUTH },
	{ "test-ec",		0, NULL,		OPT_TEST_EC },
#ifndef _WIN32
	{ "test-fork",		0, NULL,		OPT_TEST_FORK },
#endif
	{ "generate-random",	1, NULL,		OPT_GENERATE_RANDOM },

	{ NULL, 0, NULL, 0 }
};

static const char *option_help[] = {
	"Specify the module to load (default:" DEFAULT_PKCS11_PROVIDER ")",
	"Show global token information",
	"List available slots",
	"List slots with tokens",
	"List mechanisms supported by the token",
	"Show objects on token",

	"Sign some data",
	"Verify a signature of some data",
	"Decrypt some data",
	"Hash some data",
	"Derive a secret key using another key and some data",
	"Derive ECDHpass DER encoded pubkey for compatibility with some PKCS#11 implementations",
	"Specify mechanism (use -M for a list of supported mechanisms), or by hexadecimal, e.g., 0x80001234",
	"Specify hash algorithm used with RSA-PKCS-PSS signature and RSA-PKCS-OAEP decryption",
	"Specify MGF (Message Generation Function) used for RSA-PSS signature and RSA-OAEP decryption (possible values are MGF1-SHA1 to MGF1-SHA512)",
	"Specify how many bytes should be used for salt in RSA-PSS signatures (default is digest size)",

	"Log into the token first",
	"Specify login type ('so', 'user', 'context-specific'; default:'user')",
	"Supply User PIN on the command line (if used in scripts: careful!)",
	"Supply User PUK on the command line",
	"Supply new User PIN on the command line",
	"Supply SO PIN on the command line (if used in scripts: careful!)",
	"Initialize the token, its label and its SO PIN (use with --label and --so-pin)",
	"Initialize the User PIN (use with --pin and --login)",
	"Change User PIN",
	"Unlock User PIN (without '--login' unlock in logged in session; otherwise '--login-type' has to be 'context-specific')",
	"Key pair generation",
	"Key generation",
	"Specify the type and length of the key to create, for example rsa:1024 or EC:prime256v1 or GOSTR3410-2012-256:B",
	"Specify 'sign' key usage flag (sets SIGN in privkey, sets VERIFY in pubkey)",
	"Specify 'decrypt' key usage flag (RSA only, set DECRYPT privkey, ENCRYPT in pubkey)",
	"Specify 'derive' key usage flag (EC only)",
	"Write an object (key, cert, data) to the card",
	"Get object's CKA_VALUE attribute (use with --type)",
	"Delete an object (use with --type cert/data/privkey/pubkey/secrkey)",
	"Specify the application label of the data object (use with --type data)",
	"Specify the application ID of the data object (use with --type data)",
	"Specify the issuer in hexadecimal format (use with --type cert)",
	"Specify the subject in hexadecimal format (use with --type cert/privkey/pubkey)",
	"Specify the type of object (e.g. cert, privkey, pubkey, secrkey, data)",
	"Specify the ID of the object",
	"Specify the label of the object",
	"Specify the ID of the slot to use",
	"Specify the description of the slot to use",
	"Specify the index of the slot to use",
	"Specify the index of the object to use",
	"Specify the token label of the slot to use",
	"Set the CKA_ID of an object, <args>= the (new) CKA_ID",
	"Use <arg> to create some attributes when writing an object",
	"Specify the input file",
	"Specify the file with signature for verification",
	"Specify the output file",
	"Format for ECDSA signature <arg>: 'rs' (default), 'sequence', 'openssl'",
	"Specify the comma-separated list of allowed mechanisms when creating an object.",

	"Test (best used with the --login or --pin option)",
	"Test hotplug capabilities (C_GetSlotList + C_WaitForSlotEvent)",
	"Test Mozilla-like keypair gen and cert req, <arg>=certfile",
	"Verbose operation. (Set OPENSC_DEBUG to enable OpenSC specific debugging)",
	"Set the CKA_PRIVATE attribute (object is only viewable after a login)",
	"Set the CKA_SENSITIVE attribute (object cannot be revealed in plaintext)",
	"Set the CKA_EXTRACTABLE attribute (object can be extracted)",
	"Set the CKA_ALWAYS_AUTHENTICATE attribute to a key object (require PIN verification for each use)",
	"Test EC (best used with the --login or --pin option)",
#ifndef _WIN32
	"Test forking and calling C_Initialize() in the child",
#endif
	"Generate given amount of random data"
};

static const char *	app_name = "pkcs11-tool"; /* for utils.c */

static int		verbose = 0;
static const char *	opt_input = NULL;
static const char *	opt_output = NULL;
static const char *	opt_signature_file = NULL;
static const char *	opt_module = DEFAULT_PKCS11_PROVIDER;
static int		opt_slot_set = 0;
static CK_SLOT_ID	opt_slot = 0;
static const char *	opt_slot_description = NULL;
static const char *	opt_token_label = NULL;
static CK_ULONG		opt_slot_index = 0;
static int		opt_slot_index_set = 0;
static CK_ULONG		opt_object_index = 0;
static int		opt_object_index_set = 0;
static CK_MECHANISM_TYPE opt_mechanism = 0;
static int		opt_mechanism_used = 0;
static const char *	opt_file_to_write = NULL;
static const char *	opt_object_class_str = NULL;
static CK_OBJECT_CLASS	opt_object_class = -1;
static CK_BYTE		opt_object_id[100], new_object_id[100];
static const char *	opt_attr_from_file = NULL;
static size_t		opt_object_id_len = 0, new_object_id_len = 0;
static char *		opt_object_label = NULL;
static const char *	opt_pin = NULL;
static const char *	opt_so_pin = NULL;
static const char *	opt_puk = NULL;
static const char *	opt_new_pin = NULL;
static char *		opt_application_label = NULL;
static char *		opt_application_id = NULL;
static char *		opt_issuer = NULL;
static char *		opt_subject = NULL;
static char *		opt_key_type = NULL;
static char *		opt_sig_format = NULL;
#define MAX_ALLOWED_MECHANISMS 20
static CK_MECHANISM_TYPE opt_allowed_mechanisms[MAX_ALLOWED_MECHANISMS];
static size_t		opt_allowed_mechanisms_len = 0;
static int		opt_is_private = 0;
static int		opt_is_sensitive = 0;
static int		opt_is_extractable = 0;
static int		opt_test_hotplug = 0;
static int		opt_login_type = -1;
static int		opt_key_usage_sign = 0;
static int		opt_key_usage_decrypt = 0;
static int		opt_key_usage_derive = 0;
static int		opt_key_usage_default = 1; /* uses defaults if no opt_key_usage options */
static int		opt_derive_pass_der = 0;
static unsigned long	opt_random_bytes = 0;
static CK_MECHANISM_TYPE opt_hash_alg = 0;
static unsigned long	opt_mgf = 0;
static long	        opt_salt_len = 0;
static int		opt_salt_len_given = 0; /* 0 - not given, 1 - given with input parameters */
static int		opt_always_auth = 0;

static void *module = NULL;
static CK_FUNCTION_LIST_PTR p11 = NULL;
static CK_SLOT_ID_PTR p11_slots = NULL;
static CK_ULONG p11_num_slots = 0;
static int suppress_warn = 0;

struct flag_info {
	CK_FLAGS	value;
	const char *	name;
};
struct mech_info {
	CK_MECHANISM_TYPE mech;
	const char *	name;
	const char *	short_name;
};
struct x509cert_info {
	unsigned char	subject[512];
	int		subject_len;
	unsigned char	issuer[512];
	int		issuer_len;
	unsigned char	serialnum[128];
	int		serialnum_len;
};
struct rsakey_info {
	unsigned char	*modulus;
	int		modulus_len;
	unsigned char	*public_exponent;
	int		public_exponent_len;
	unsigned char	*private_exponent;
	int		private_exponent_len;
	unsigned char	*prime_1;
	int		prime_1_len;
	unsigned char	*prime_2;
	int		prime_2_len;
	unsigned char	*exponent_1;
	int		exponent_1_len;
	unsigned char	*exponent_2;
	int		exponent_2_len;
	unsigned char	*coefficient;
	int		coefficient_len;
};
struct gostkey_info {
	struct sc_lv_data param_oid;
	struct sc_lv_data public;
	struct sc_lv_data private;
};

static void		show_cryptoki_info(void);
static void		list_slots(int, int, int);
static void		show_token(CK_SLOT_ID);
static void		list_mechs(CK_SLOT_ID);
static void		list_objects(CK_SESSION_HANDLE, CK_OBJECT_CLASS);
static int		login(CK_SESSION_HANDLE, int);
static void		init_token(CK_SLOT_ID);
static void		init_pin(CK_SLOT_ID, CK_SESSION_HANDLE);
static int		change_pin(CK_SLOT_ID, CK_SESSION_HANDLE);
static int		unlock_pin(CK_SLOT_ID slot, CK_SESSION_HANDLE sess, int login_type);
static void		show_object(CK_SESSION_HANDLE, CK_OBJECT_HANDLE);
static void		show_key(CK_SESSION_HANDLE, CK_OBJECT_HANDLE);
static void		show_cert(CK_SESSION_HANDLE, CK_OBJECT_HANDLE);
static void		show_dobj(CK_SESSION_HANDLE sess, CK_OBJECT_HANDLE obj);
static void		sign_data(CK_SLOT_ID, CK_SESSION_HANDLE, CK_OBJECT_HANDLE);
static void		verify_signature(CK_SLOT_ID, CK_SESSION_HANDLE, CK_OBJECT_HANDLE);
static void		decrypt_data(CK_SLOT_ID, CK_SESSION_HANDLE, CK_OBJECT_HANDLE);
static void		hash_data(CK_SLOT_ID, CK_SESSION_HANDLE);
static void		derive_key(CK_SLOT_ID, CK_SESSION_HANDLE, CK_OBJECT_HANDLE);
static int		gen_keypair(CK_SLOT_ID slot, CK_SESSION_HANDLE,
				CK_OBJECT_HANDLE *, CK_OBJECT_HANDLE *, const char *);
static int		gen_key(CK_SLOT_ID slot, CK_SESSION_HANDLE, CK_OBJECT_HANDLE *, const char *, char *);
static int		write_object(CK_SESSION_HANDLE session);
static int		read_object(CK_SESSION_HANDLE session);
static int		delete_object(CK_SESSION_HANDLE session);
static void		set_id_attr(CK_SESSION_HANDLE session);
static int		find_object(CK_SESSION_HANDLE, CK_OBJECT_CLASS,
				CK_OBJECT_HANDLE_PTR,
				const unsigned char *, size_t id_len, int obj_index);
static int		find_mechanism(CK_SLOT_ID, CK_FLAGS, CK_MECHANISM_TYPE_PTR, size_t, CK_MECHANISM_TYPE_PTR);
static int		find_slot_by_description(const char *, CK_SLOT_ID_PTR);
static int		find_slot_by_token_label(const char *, CK_SLOT_ID_PTR);
static void		get_token_info(CK_SLOT_ID, CK_TOKEN_INFO_PTR);
static CK_ULONG		get_mechanisms(CK_SLOT_ID,
				CK_MECHANISM_TYPE_PTR *, CK_FLAGS);
static void		p11_fatal(const char *, CK_RV);
static void		p11_warn(const char *, CK_RV);
static const char *	p11_slot_info_flags(CK_FLAGS);
static const char *	p11_token_info_flags(CK_FLAGS);
static const char *	p11_utf8_to_local(CK_UTF8CHAR *, size_t);
static const char *	p11_flag_names(struct flag_info *, CK_FLAGS);
static const char *	p11_mechanism_to_name(CK_MECHANISM_TYPE);
static CK_MECHANISM_TYPE p11_name_to_mechanism(const char *);
static const char *	p11_mgf_to_name(CK_RSA_PKCS_MGF_TYPE);
static CK_MECHANISM_TYPE p11_name_to_mgf(const char *);
static void		p11_perror(const char *, CK_RV);
static const char *	CKR2Str(CK_ULONG res);
static int		p11_test(CK_SESSION_HANDLE session);
static int test_card_detection(int);
static int		hex_to_bin(const char *in, CK_BYTE *out, size_t *outlen);
static void		pseudo_randomize(unsigned char *data, size_t dataLen);
static CK_SESSION_HANDLE test_kpgen_certwrite(CK_SLOT_ID slot, CK_SESSION_HANDLE session);
static void		test_ec(CK_SLOT_ID slot, CK_SESSION_HANDLE session);
#ifndef _WIN32
static void		test_fork(void);
#endif
static void		generate_random(CK_SESSION_HANDLE session);
static CK_RV		find_object_with_attributes(CK_SESSION_HANDLE session, CK_OBJECT_HANDLE *out,
				CK_ATTRIBUTE *attrs, CK_ULONG attrsLen, CK_ULONG obj_index);
static CK_ULONG		get_private_key_length(CK_SESSION_HANDLE sess, CK_OBJECT_HANDLE prkey);

/* win32 needs this in open(2) */
#ifndef O_BINARY
# define O_BINARY 0
#endif

#define ATTR_METHOD(ATTR, TYPE) \
static TYPE \
get##ATTR(CK_SESSION_HANDLE sess, CK_OBJECT_HANDLE obj) \
{ \
	TYPE		type = 0; \
	CK_ATTRIBUTE	attr = { CKA_##ATTR, &type, sizeof(type) }; \
	CK_RV		rv; \
 \
	rv = p11->C_GetAttributeValue(sess, obj, &attr, 1); \
	if (rv != CKR_OK) \
		p11_warn("C_GetAttributeValue(" #ATTR ")", rv); \
	return type; \
}

#define VARATTR_METHOD(ATTR, TYPE) \
static TYPE * \
get##ATTR(CK_SESSION_HANDLE sess, CK_OBJECT_HANDLE obj, CK_ULONG_PTR pulCount) \
{ \
	CK_ATTRIBUTE	attr = { CKA_##ATTR, NULL, 0 };		\
	CK_RV		rv;					\
	if (pulCount)						\
		*pulCount = 0;					\
	rv = p11->C_GetAttributeValue(sess, obj, &attr, 1);	\
	if (rv == CKR_OK) {					\
		if (attr.ulValueLen == (CK_ULONG)(-1))		\
			return NULL;				\
		if (!(attr.pValue = calloc(1, attr.ulValueLen + 1)))		\
			util_fatal("out of memory in get" #ATTR ": %m");	\
		rv = p11->C_GetAttributeValue(sess, obj, &attr, 1);		\
		if (attr.ulValueLen == (CK_ULONG)(-1)) {	\
			free(attr.pValue);			\
			return NULL;				\
		}						\
		if (pulCount)					\
			*pulCount = attr.ulValueLen / sizeof(TYPE);	\
	} else if (rv != CKR_ATTRIBUTE_TYPE_INVALID) {		\
		p11_warn("C_GetAttributeValue(" #ATTR ")", rv);	\
	}							\
	return (TYPE *) attr.pValue;				\
}

/*
 * Define attribute accessors
 */
ATTR_METHOD(CLASS, CK_OBJECT_CLASS);			/* getCLASS */
ATTR_METHOD(ALWAYS_AUTHENTICATE, CK_BBOOL); 		/* getALWAYS_AUTHENTICATE */
ATTR_METHOD(PRIVATE, CK_BBOOL); 			/* getPRIVATE */
ATTR_METHOD(MODIFIABLE, CK_BBOOL);			/* getMODIFIABLE */
ATTR_METHOD(ENCRYPT, CK_BBOOL);				/* getENCRYPT */
ATTR_METHOD(DECRYPT, CK_BBOOL);				/* getDECRYPT */
ATTR_METHOD(SIGN, CK_BBOOL);				/* getSIGN */
ATTR_METHOD(VERIFY, CK_BBOOL);				/* getVERIFY */
ATTR_METHOD(WRAP, CK_BBOOL);				/* getWRAP */
ATTR_METHOD(UNWRAP, CK_BBOOL);				/* getUNWRAP */
ATTR_METHOD(DERIVE, CK_BBOOL);				/* getDERIVE */
ATTR_METHOD(SENSITIVE, CK_BBOOL);			/* getSENSITIVE */
ATTR_METHOD(ALWAYS_SENSITIVE, CK_BBOOL);		/* getALWAYS_SENSITIVE */
ATTR_METHOD(EXTRACTABLE, CK_BBOOL);			/* getEXTRACTABLE */
ATTR_METHOD(NEVER_EXTRACTABLE, CK_BBOOL);		/* getNEVER_EXTRACTABLE */
ATTR_METHOD(LOCAL, CK_BBOOL);				/* getLOCAL */
ATTR_METHOD(OPENSC_NON_REPUDIATION, CK_BBOOL);		/* getOPENSC_NON_REPUDIATION */
ATTR_METHOD(KEY_TYPE, CK_KEY_TYPE);			/* getKEY_TYPE */
ATTR_METHOD(CERTIFICATE_TYPE, CK_CERTIFICATE_TYPE);	/* getCERTIFICATE_TYPE */
ATTR_METHOD(MODULUS_BITS, CK_ULONG);			/* getMODULUS_BITS */
ATTR_METHOD(VALUE_LEN, CK_ULONG);			/* getVALUE_LEN */
VARATTR_METHOD(LABEL, char);				/* getLABEL */
VARATTR_METHOD(APPLICATION, char);			/* getAPPLICATION */
VARATTR_METHOD(ID, unsigned char);			/* getID */
VARATTR_METHOD(OBJECT_ID, unsigned char);		/* getOBJECT_ID */
VARATTR_METHOD(MODULUS, CK_BYTE);			/* getMODULUS */
#ifdef ENABLE_OPENSSL
VARATTR_METHOD(SUBJECT, unsigned char);			/* getSUBJECT */
VARATTR_METHOD(PUBLIC_EXPONENT, CK_BYTE);		/* getPUBLIC_EXPONENT */
#endif
VARATTR_METHOD(VALUE, unsigned char);			/* getVALUE */
VARATTR_METHOD(GOSTR3410_PARAMS, unsigned char);	/* getGOSTR3410_PARAMS */
VARATTR_METHOD(GOSTR3411_PARAMS, unsigned char);	/* getGOSTR3411_PARAMS */
VARATTR_METHOD(EC_POINT, unsigned char);		/* getEC_POINT */
VARATTR_METHOD(EC_PARAMS, unsigned char);		/* getEC_PARAMS */
VARATTR_METHOD(ALLOWED_MECHANISMS, CK_MECHANISM_TYPE);	/* getALLOWED_MECHANISMS */


int main(int argc, char * argv[])
{
	CK_SESSION_HANDLE session = CK_INVALID_HANDLE;
	CK_OBJECT_HANDLE object = CK_INVALID_HANDLE;
	int err = 0, c, long_optind = 0;
	int do_show_info = 0;
	int do_list_slots = 0;
	int list_token_slots = 0;
	int do_list_mechs = 0;
	int do_list_objects = 0;
	int do_sign = 0;
	int do_verify = 0;
	int do_decrypt = 0;
	int do_hash = 0;
	int do_derive = 0;
	int do_gen_keypair = 0;
	int do_gen_key = 0;
	int do_write_object = 0;
	int do_read_object = 0;
	int do_delete_object = 0;
	int do_set_id = 0;
	int do_test = 0;
	int do_test_kpgen_certwrite = 0;
	int do_test_ec = 0;
#ifndef _WIN32
	int do_test_fork = 0;
#endif
	int need_session = 0;
	int opt_login = 0;
	int do_init_token = 0;
	int do_init_pin = 0;
	int do_change_pin = 0;
	int do_unlock_pin = 0;
	int action_count = 0;
	int do_generate_random = 0;
	char *s = NULL;
	CK_RV rv;

#ifdef _WIN32
	char expanded_val[PATH_MAX];
	DWORD expanded_len;

	if(_setmode(_fileno(stdout), _O_BINARY ) == -1)
		util_fatal("Cannot set FMODE to O_BINARY");
	if(_setmode(_fileno(stdin), _O_BINARY ) == -1)
		util_fatal("Cannot set FMODE to O_BINARY");
#endif

#ifdef ENABLE_OPENSSL
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
	OPENSSL_config(NULL);

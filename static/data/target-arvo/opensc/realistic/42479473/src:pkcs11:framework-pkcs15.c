/*
 * framework-pkcs15.c: PKCS#15 framework and related objects
 *
 * Copyright (C) 2002  Timo Teräs <timo.teras@iki.fi>
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
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "libopensc/log.h"
#include "libopensc/internal.h"
#include "libopensc/asn1.h"
#include "libopensc/cardctl.h"
#include "ui/notify.h"
#include "common/compat_strnlen.h"
#ifdef ENABLE_OPENSSL
#include <openssl/sha.h>
#else
#define SHA_DIGEST_LENGTH	20
#endif

#include "sc-pkcs11.h"
#ifdef USE_PKCS15_INIT
#include "pkcs15init/pkcs15-init.h"
#endif

struct pkcs15_slot_data {
	struct sc_pkcs15_object *auth_obj;
};
#define slot_data(p)		((struct pkcs15_slot_data *) (p))
#define slot_data_auth(p)	(((p) && slot_data(p)) ? slot_data(p)->auth_obj : NULL)
#define slot_data_auth_info(p)	(((p) && slot_data_auth(p))? \
		(struct sc_pkcs15_auth_info *) slot_data_auth(p)->data : NULL)

#define check_attribute_buffer(attr,size)	\
	if (attr->pValue == NULL_PTR) {         \
		attr->ulValueLen = size;        \
		return CKR_OK;                  \
	}                                       \
	if (attr->ulValueLen < size) {		\
		attr->ulValueLen = size;	\
		return CKR_BUFFER_TOO_SMALL;    \
	}                                       \
	attr->ulValueLen = size;

#define MAX_OBJECTS	128
struct pkcs15_fw_data {
	struct sc_pkcs15_card *		p15_card;
	struct pkcs15_any_object *	objects[MAX_OBJECTS];
	unsigned int			num_objects;
	unsigned int			locked;
	unsigned char user_puk[64];
	unsigned int user_puk_len;
};

struct pkcs15_any_object {
	struct sc_pkcs11_object		base;
	unsigned int			refcount;
	size_t				size;
	struct sc_pkcs15_object *	p15_object;
	struct pkcs15_pubkey_object *	related_pubkey;
	struct pkcs15_cert_object *	related_cert;
	struct pkcs15_prkey_object *	related_privkey;
};

struct pkcs15_cert_object {
	struct pkcs15_any_object	base;

	struct sc_pkcs15_cert_info *	cert_info;
	struct sc_pkcs15_cert *		cert_data;
};
#define cert_flags		base.base.flags
#define cert_p15obj		base.p15_object
#define cert_pubkey		base.related_pubkey
#define cert_issuer		base.related_cert
#define cert_prvkey		base.related_privkey

struct pkcs15_prkey_object {
	struct pkcs15_any_object	base;

	struct sc_pkcs15_prkey_info *	prv_info;
	struct sc_pkcs15_pubkey *	pub_data;
};
#define prv_flags		base.base.flags
#define prv_p15obj		base.p15_object
#define prv_pubkey		base.related_pubkey
#define prv_next		base.related_privkey

struct pkcs15_pubkey_object {
	struct pkcs15_any_object	base;

	struct sc_pkcs15_pubkey_info *	pub_info;	/* NULL for key extracted from cert */
	struct sc_pkcs15_pubkey *	pub_data;
};
#define pub_flags		base.base.flags
#define pub_p15obj		base.p15_object
#define pub_genfrom		base.related_cert

#define __p15_type(obj)		(((obj) && (obj)->p15_object)? ((obj)->p15_object->type) : (unsigned int)-1)
#define is_privkey(obj)		((__p15_type(obj) & SC_PKCS15_TYPE_CLASS_MASK) == SC_PKCS15_TYPE_PRKEY)
#define is_pubkey(obj)		((__p15_type(obj) & SC_PKCS15_TYPE_CLASS_MASK) == SC_PKCS15_TYPE_PUBKEY)
#define is_cert(obj)		(__p15_type(obj) == SC_PKCS15_TYPE_CERT_X509)

struct pkcs15_data_object {
	struct pkcs15_any_object	base;

	struct sc_pkcs15_data_info *info;
	struct sc_pkcs15_data *value;
};
#define data_flags		base.base.flags
#define data_p15obj		base.p15_object
#define is_data(obj) (__p15_type(obj) == SC_PKCS15_TYPE_DATA_OBJECT)

struct pkcs15_skey_object {
	struct pkcs15_any_object    base;

	struct sc_pkcs15_skey_info *info;
	struct sc_pkcs15_skey *valueXXXX;
};

#define is_skey(obj) ((__p15_type(obj) & SC_PKCS15_TYPE_CLASS_MASK) == SC_PKCS15_TYPE_SKEY)

#define skey_flags	base.base.flags
#define skey_p15obj	base.p15_object

extern struct sc_pkcs11_object_ops pkcs15_cert_ops;
extern struct sc_pkcs11_object_ops pkcs15_prkey_ops;
extern struct sc_pkcs11_object_ops pkcs15_pubkey_ops;
extern struct sc_pkcs11_object_ops pkcs15_dobj_ops;
extern struct sc_pkcs11_object_ops pkcs15_skey_ops;

const CK_BYTE gostr3410_paramset_A_encoded_oid[] = { 0x06, 0x07, 0x2a, 0x85, 0x03, 0x02, 0x02, 0x23, 0x01 };
const unsigned int gostr3410_paramset_A_oid[] = {1, 2, 643, 2, 2, 35, 1, (unsigned int)-1};
const CK_BYTE gostr3410_paramset_B_encoded_oid[] = { 0x06, 0x07, 0x2a, 0x85, 0x03, 0x02, 0x02, 0x23, 0x02 };
const unsigned int gostr3410_paramset_B_oid[] = {1, 2, 643, 2, 2, 35, 2, (unsigned int)-1};
const CK_BYTE gostr3410_paramset_C_encoded_oid[] = { 0x06, 0x07, 0x2a, 0x85, 0x03, 0x02, 0x02, 0x23, 0x03 };
const unsigned int gostr3410_paramset_C_oid[] = {1, 2, 643, 2, 2, 35, 3, (unsigned int)-1};

static const struct {
	const CK_BYTE *encoded_oid;
	const unsigned int encoded_oid_size;
	const unsigned int *oid;
	const unsigned int oid_size;
	unsigned char oid_id;
} gostr3410_param_oid [] = {
	{ &gostr3410_paramset_A_encoded_oid[0],
		sizeof(gostr3410_paramset_A_encoded_oid),
		&gostr3410_paramset_A_oid[0],
		sizeof(gostr3410_paramset_A_oid),
		SC_PKCS15_PARAMSET_GOSTR3410_A },
	{ &gostr3410_paramset_B_encoded_oid[0],
		sizeof(gostr3410_paramset_B_encoded_oid),
		&gostr3410_paramset_B_oid[0],
		sizeof(gostr3410_paramset_B_oid),
		SC_PKCS15_PARAMSET_GOSTR3410_B },
	{ &gostr3410_paramset_C_encoded_oid[0],
		sizeof(gostr3410_paramset_C_encoded_oid),
		&gostr3410_paramset_C_oid[0],
		sizeof(gostr3410_paramset_C_oid),
		SC_PKCS15_PARAMSET_GOSTR3410_C }
};

const CK_BYTE gostr3411_94_cryptopro_paramset_encoded_oid[] = { 0x06, 0x07, 0x2a, 0x85, 0x03, 0x02, 0x02, 0x1e, 0x01 };
const unsigned int gostr3411_94_cryptopro_paramset_oid[] = {1, 2, 643, 2, 2, 30, 1, (unsigned int)-1};

#ifdef USE_PKCS15_INIT
static const struct {
	const CK_BYTE *encoded_oid;
	const unsigned int encoded_oid_size;
	const unsigned int *oid;
	const unsigned int oid_size;
} gostr3410_hash_param_oid [] = {
	{ &gostr3411_94_cryptopro_paramset_encoded_oid[0],
		sizeof(gostr3411_94_cryptopro_paramset_encoded_oid),
		&gostr3411_94_cryptopro_paramset_oid[0],
		sizeof(gostr3411_94_cryptopro_paramset_oid)}
};
#endif

static int	__pkcs15_release_object(struct pkcs15_any_object *);
static CK_RV	register_mechanisms(struct sc_pkcs11_card *p11card);
static CK_RV	get_public_exponent(struct sc_pkcs15_pubkey *,
					CK_ATTRIBUTE_PTR);
static CK_RV	get_modulus(struct sc_pkcs15_pubkey *,
					CK_ATTRIBUTE_PTR);
static CK_RV	get_modulus_bits(struct sc_pkcs15_pubkey *,
					CK_ATTRIBUTE_PTR);
static CK_RV	get_usage_bit(unsigned int usage, CK_ATTRIBUTE_PTR attr);
static CK_RV	get_gostr3410_params(const u8 *, size_t, CK_ATTRIBUTE_PTR);
static CK_RV	get_ec_pubkey_point(struct sc_pkcs15_pubkey *, CK_ATTRIBUTE_PTR);
static CK_RV	get_ec_pubkey_params(struct sc_pkcs15_pubkey *, CK_ATTRIBUTE_PTR);
static int	lock_card(struct pkcs15_fw_data *);
static int	unlock_card(struct pkcs15_fw_data *);
static int	reselect_app_df(sc_pkcs15_card_t *p15card);

#ifdef USE_PKCS15_INIT
static CK_RV	set_gost3410_params(struct sc_pkcs15init_prkeyargs *,
			struct sc_pkcs15init_pubkeyargs *,
			CK_ATTRIBUTE_PTR, CK_ULONG, CK_ATTRIBUTE_PTR, CK_ULONG);
static CK_RV	pkcs15_create_slot(struct sc_pkcs11_card *p11card, struct pkcs15_fw_data *fw_data,
			struct sc_pkcs15_object *auth, struct sc_app_info *app,
			struct sc_pkcs11_slot **out);
static int pkcs11_get_pin_callback(struct sc_profile *profile, int id,
		const struct sc_pkcs15_auth_info *info, const char *label,
		unsigned char *pinbuf, size_t *pinsize);

static struct sc_pkcs15init_callbacks pkcs15init_callbacks = {
	pkcs11_get_pin_callback,       /* get_pin() */
	NULL
};
static char *pkcs15init_sopin = NULL;
static size_t pkcs15init_sopin_len = 0;

static int pkcs11_get_pin_callback(struct sc_profile *profile, int id,
		const struct sc_pkcs15_auth_info *info, const char *label,
		unsigned char *pinbuf, size_t *pinsize)
{
	char	*secret = NULL;
	size_t	len = 0;

	if (info->auth_type != SC_PKCS15_PIN_AUTH_TYPE_PIN)
		return SC_ERROR_NOT_SUPPORTED;

	sc_log(context, "pkcs11_get_pin_callback() auth-method %X", info->auth_method);
	if (info->auth_method == SC_AC_CHV)   {
		unsigned int flags = info->attrs.pin.flags;

		sc_log(context, "pkcs11_get_pin_callback() PIN flags %X", flags);
		if ((flags & SC_PKCS15_PIN_FLAG_SO_PIN) && !(flags & SC_PKCS15_PIN_FLAG_UNBLOCKING_PIN))    {
			if (pkcs15init_sopin_len)
				secret = pkcs15init_sopin;
		}
	}
	if (secret)
		len = strlen(secret);

	sc_log(context, "pkcs11_get_pin_callback() secret '%s'", secret ? secret : "<null>");

	if (!secret)
		return SC_ERROR_OBJECT_NOT_FOUND;
	if (len > *pinsize)
		return SC_ERROR_BUFFER_TOO_SMALL;
	memcpy(pinbuf, secret, len + 1);
	*pinsize = len;
	return 0;
}
#endif

/* Returns WF data corresponding to the given application or,
 * if application info is not supplied, returns first available WF data. */
static struct pkcs15_fw_data *
get_fw_data(struct sc_pkcs11_card *p11card, struct sc_app_info *app_info, int *out_idx)
{
	struct pkcs15_fw_data *out = NULL;
	int idx;

	if (!p11card)
		return NULL;
	for (idx=0; p11card && idx < SC_PKCS11_FRAMEWORK_DATA_MAX_NUM; idx++)   {
		struct pkcs15_fw_data *fw_data = (struct pkcs15_fw_data *) p11card->fws_data[idx];
		struct sc_file *file_app = NULL;

		if (!fw_data || !fw_data->p15_card)
			continue;

		file_app = fw_data->p15_card->file_app;
		if (app_info && file_app)   {
			if (file_app->path.len != app_info->path.len)
				continue;
			if (file_app->path.aid.len != app_info->path.aid.len)
				continue;
			if (memcmp(file_app->path.aid.value, app_info->path.aid.value, app_info->path.aid.len))
				continue;
			if (memcmp(file_app->path.value, app_info->path.value, app_info->path.len))
				continue;
		}

		out = fw_data;
		if (out_idx)
			*out_idx = idx;
		break;
	}

	return out;
}

/* PKCS#15 Framework */
static CK_RV
pkcs15_bind(struct sc_pkcs11_card *p11card, struct sc_app_info *app_info)
{
	struct pkcs15_fw_data *fw_data = NULL;
	struct sc_aid *aid = app_info ? &app_info->aid : NULL;
	int rc, idx;
	CK_RV ck_rv;

	sc_log(context, "Bind PKCS#15 '%s' application", app_info ? app_info->label : "<anonymous>");
	if (!p11card)
		return CKR_TOKEN_NOT_RECOGNIZED;
	for (idx=0; idx<SC_PKCS11_FRAMEWORK_DATA_MAX_NUM; idx++)
		if (!p11card->fws_data[idx])
			break;
	if (idx == SC_PKCS11_FRAMEWORK_DATA_MAX_NUM)
		return CKR_USER_TOO_MANY_TYPES;

	if (!(fw_data = calloc(1, sizeof(*fw_data))))
		return CKR_HOST_MEMORY;
	p11card->fws_data[idx] = fw_data;

	rc = sc_pkcs15_bind(p11card->card, aid, &fw_data->p15_card);
	if (rc != SC_SUCCESS) {
		sc_log(context, "sc_pkcs15_bind failed: %d", rc);
		return sc_to_cryptoki_error(rc, NULL);
	}

	/* Mechanisms are registered globally per card. Checking
	 * p11card->nmechanisms avoids registering the same mechanisms twice for a
	 * card with multiple slots. */
	if (!p11card->nmechanisms) {
		ck_rv = register_mechanisms(p11card);
		if (ck_rv != CKR_OK) {
			sc_log(context, "cannot register mechanisms; CKR 0x%lX",
			       ck_rv);
			return ck_rv;
		}
	}

	if (idx == 0) {
		/* send a notification only for the first application that's bound */
		sc_notify_id(p11card->card->ctx, &p11card->reader->atr, fw_data->p15_card,
				NOTIFY_CARD_INSERTED);
	}

	return CKR_OK;
}


static CK_RV
pkcs15_unbind(struct sc_pkcs11_card *p11card)
{
	unsigned int i, idx;
	int rv = SC_SUCCESS;

	if (!p11card)
		return CKR_TOKEN_NOT_RECOGNIZED;
	for (idx=0; p11card && idx<SC_PKCS11_FRAMEWORK_DATA_MAX_NUM; idx++)   {
		struct pkcs15_fw_data *fw_data = (struct pkcs15_fw_data *) p11card->fws_data[idx];

		if (!fw_data)
			break;
		for (i = 0; i < fw_data->num_objects; i++) {
			struct pkcs15_any_object *obj = fw_data->objects[i];

			/* use object specific release method if existing */
			if (obj->base.ops && obj->base.ops->release)
				obj->base.ops->release(obj);
			else
				__pkcs15_release_object(obj);
		}

		unlock_card(fw_data);

		if (fw_data->p15_card && fw_data->p15_card->card) {
			if (idx == 0) {
				int rc = sc_detect_card_presence(fw_data->p15_card->card->reader);
				if (rc <= 0 || rc & SC_READER_CARD_CHANGED) {
					/* send a notification only if the card was removed/changed
					 * and only for the first application that's unbound */
					sc_notify_id(fw_data->p15_card->card->ctx,
							&fw_data->p15_card->card->reader->atr,
							fw_data->p15_card,
							NOTIFY_CARD_REMOVED);
				}
			}
			rv = sc_pkcs15_unbind(fw_data->p15_card);
		}
		fw_data->p15_card = NULL;

		free(fw_data);
		p11card->fws_data[idx] = NULL;
	}

	return sc_to_cryptoki_error(rv, NULL);
}


static void
pkcs15_init_token_info(struct sc_pkcs15_card *p15card, CK_TOKEN_INFO_PTR pToken)
{
	scconf_block *conf_block = NULL;
	char *model = NULL;

	conf_block = sc_get_conf_block(p15card->card->ctx, "framework", "pkcs15", 1);
	if (conf_block && p15card->file_app)   {
		scconf_block **blocks = NULL;
		char str_path[SC_MAX_AID_STRING_SIZE];

		memset(str_path, 0, sizeof(str_path));
		sc_bin_to_hex(p15card->file_app->path.value, p15card->file_app->path.len, str_path, sizeof(str_path), 0);
		blocks = scconf_find_blocks(p15card->card->ctx->conf, conf_block, "application", str_path);
		if (blocks)   {
			if (blocks[0])
				model = (char *)scconf_get_str(blocks[0], "model", NULL);
			free(blocks);
		}
	}

	if (model)
		strcpy_bp(pToken->model, model, sizeof(pToken->model));
	else if (p15card->flags & SC_PKCS15_CARD_FLAG_EMULATED)
		strcpy_bp(pToken->model, "PKCS#15 emulated", sizeof(pToken->model));
	else
		strcpy_bp(pToken->model, "PKCS#15", sizeof(pToken->model));

	if (p15card->tokeninfo) {
		strcpy_bp(pToken->manufacturerID, p15card->tokeninfo->manufacturer_id, 32);

		/* Take the last 16 chars of the serial number (if the are more than 16).
		 * _Assuming_ that the serial number is a Big Endian counter, this
		 * will assure that the serial within each type of card will be
		 * unique in pkcs11 (at least for the first 8^16 cards :-) */
		if (p15card->tokeninfo->serial_number != NULL) {
			size_t sn_start = strlen(p15card->tokeninfo->serial_number);

			if (sn_start <= 16)
				sn_start = 0;
			else
				sn_start -= 16;

			strcpy_bp(pToken->serialNumber, p15card->tokeninfo->serial_number + sn_start, 16);
		}
	}

	pToken->ulMaxSessionCount = CK_EFFECTIVELY_INFINITE;
	pToken->ulSessionCount = 0; /* FIXME */
	pToken->ulMaxRwSessionCount = CK_EFFECTIVELY_INFINITE;
	pToken->ulRwSessionCount = 0; /* FIXME */
	pToken->ulTotalPublicMemory = CK_UNAVAILABLE_INFORMATION;
	pToken->ulFreePublicMemory = CK_UNAVAILABLE_INFORMATION;
	pToken->ulTotalPrivateMemory = CK_UNAVAILABLE_INFORMATION;
	pToken->ulFreePrivateMemory = CK_UNAVAILABLE_INFORMATION;
	pToken->hardwareVersion.major = p15card->card->version.hw_major;
	pToken->hardwareVersion.minor = p15card->card->version.hw_minor;
	pToken->firmwareVersion.major = p15card->card->version.fw_major;
	pToken->firmwareVersion.minor = p15card->card->version.fw_minor;
}

#ifdef USE_PKCS15_INIT
static char *
set_cka_label(CK_ATTRIBUTE_PTR attr, char *label)
{
	char *l = (char *)attr->pValue;
	unsigned long len = attr->ulValueLen;

	if (len >= SC_PKCS15_MAX_LABEL_SIZE)
		len = SC_PKCS15_MAX_LABEL_SIZE-1;
	memcpy(label, l, len);
	label[len] = '\0';
	return label;
}
#endif

static int
__pkcs15_create_object(struct pkcs15_fw_data *fw_data,
		       struct pkcs15_any_object **result,
		       struct sc_pkcs15_object *p15_object,
		       struct sc_pkcs11_object_ops *ops,
		       size_t size)
{
	struct pkcs15_any_object *obj;

	if (fw_data->num_objects >= MAX_OBJECTS)
		return SC_ERROR_TOO_MANY_OBJECTS;

	if (!(obj = calloc(1, size)))
		return SC_ERROR_OUT_OF_MEMORY;

	fw_data->objects[fw_data->num_objects++] = obj;

	obj->base.ops = ops;
	obj->p15_object = p15_object;
	obj->refcount = 1;
	obj->size = size;

	*result = obj;
	return 0;
}

static int
__pkcs15_release_object(struct pkcs15_any_object *obj)
{
	if (--(obj->refcount) != 0)
		return obj->refcount;

	sc_mem_clear(obj, obj->size);
	free(obj);

	return 0;
}

#ifdef USE_PKCS15_INIT
static int
__pkcs15_delete_object(struct pkcs15_fw_data *fw_data, struct pkcs15_any_object *obj)
{
	unsigned int i;

	if (fw_data->num_objects == 0)
		return SC_ERROR_INTERNAL;

	for (i = 0; i < fw_data->num_objects; ++i)   {
		if (fw_data->objects[i] == obj) {
			fw_data->objects[i] = fw_data->objects[--fw_data->num_objects];
			if (__pkcs15_release_object(obj) > 0)
				return SC_ERROR_INTERNAL;
			return SC_SUCCESS;
		}
	}
	return SC_ERROR_OBJECT_NOT_FOUND;
}
#endif

CK_RV C_GetTokenInfo(CK_SLOT_ID slotID, CK_TOKEN_INFO_PTR pInfo)
{
	struct sc_pkcs11_slot *slot;
	struct pkcs15_fw_data *fw_data = NULL;
	struct sc_pkcs15_card *p15card = NULL;
	struct sc_pkcs15_object *auth;
	struct sc_pkcs15_auth_info *pin_info;
	CK_RV rv;

	sc_log(context, "C_GetTokenInfo(%lx)", slotID);
	if (pInfo == NULL_PTR)
		return CKR_ARGUMENTS_BAD;

	rv = sc_pkcs11_lock();
	if (rv != CKR_OK)
		return rv;

	rv = slot_get_token(slotID, &slot);
	if (rv != CKR_OK)   {
		sc_log(context, "C_GetTokenInfo() get token: rv 0x%lX", rv);
		goto out;
	}

	if (slot->p11card == NULL) {
		rv = CKR_TOKEN_NOT_PRESENT;
		goto out;
	}

	fw_data = (struct pkcs15_fw_data *) slot->p11card->fws_data[slot->fw_data_idx];
	if (!fw_data) {
		rv = sc_to_cryptoki_error(SC_ERROR_INTERNAL, "C_GetTokenInfo");
		goto out;
	}
	p15card = fw_data->p15_card;

	/* User PIN flags are cleared before re-calculation */
	slot->token_info.flags &= ~(CKF_USER_PIN_COUNT_LOW|CKF_USER_PIN_FINAL_TRY|CKF_USER_PIN_LOCKED);
	auth = slot_data_auth(slot->fw_data);
	sc_log(context,
		"C_GetTokenInfo() auth. object %p, token-info flags 0x%lX", auth,
		slot->token_info.flags);
	if (auth) {
		pin_info = (struct sc_pkcs15_auth_info*) auth->data;

		sc_pkcs15_get_pin_info(p15card, auth);

		if (pin_info->tries_left >= 0) {
			if (pin_info->tries_left == 1 || pin_info->max_tries == 1)
				slot->token_info.flags |= CKF_USER_PIN_FINAL_TRY;
			else if (pin_info->tries_left == 0)
				slot->token_info.flags |= CKF_USER_PIN_LOCKED;
			else if (pin_info->max_tries > 1 && pin_info->tries_left < pin_info->max_tries)
				slot->token_info.flags |= CKF_USER_PIN_COUNT_LOW;
		}
	}
	memcpy(pInfo, &slot->token_info, sizeof(CK_TOKEN_INFO));
out:
	sc_pkcs11_unlock();
	sc_log(context, "C_GetTokenInfo(%lx) returns 0x%lX", slotID, rv);
	return rv;
}


static int
public_key_created(struct pkcs15_fw_data *fw_data, const struct sc_pkcs15_id *id,
		struct pkcs15_any_object **obj2)
{
	size_t ii;

	for(ii=0; ii<fw_data->num_objects; ii++) {
		struct pkcs15_any_object *any_object = fw_data->objects[ii];
		struct sc_pkcs15_object *p15_object = any_object->p15_object;

		if (!p15_object)
			continue;

		if ((p15_object->type & SC_PKCS15_TYPE_CLASS_MASK) != SC_PKCS15_TYPE_PUBKEY)
			continue;

		if (sc_pkcs15_compare_id(id, &((struct sc_pkcs15_pubkey_info *)p15_object->data)->id))   {
			if (obj2)
				*obj2 = any_object;
			return SC_SUCCESS;
		}
	}

	return SC_ERROR_OBJECT_NOT_FOUND;
}

static void
pkcs15_cert_extract_label(struct pkcs15_cert_object *cert)
{
	if (!cert || !cert->cert_p15obj || !cert->cert_data)
		return;

	sc_log(context, "pkcs15_cert_extract_label() called. Current label: %s", cert->cert_p15obj->label);

	/* if we didn't get a label, set one based on the CN */
	if (*cert->cert_p15obj->label == '\0') { /* can't be NULL -- static array */
		static const struct sc_object_id cn_oid = {{ 2, 5, 4, 3, -1 }};
		u8 *cn_name = NULL;
		size_t cn_len = 0;
		int rv = sc_pkcs15_get_name_from_dn(context,
			cert->cert_data->subject, cert->cert_data->subject_len,
			&cn_oid, &cn_name, &cn_len);
		if (rv == SC_SUCCESS) {
			sc_log(context, "pkcs15_cert_extract_label(): Name from DN is %.*s",
					(unsigned int) cn_len, cn_name);
			cn_len = MIN(cn_len, SC_PKCS15_MAX_LABEL_SIZE-1);
			memcpy(cert->cert_p15obj->label, cn_name, cn_len);
			cert->cert_p15obj->label[cn_len] = '\0';
		}
		free(cn_name);
	}
}

static int
__pkcs15_create_cert_object(struct pkcs15_fw_data *fw_data, struct sc_pkcs15_object *cert,
		struct pkcs15_any_object **cert_object)
{
	struct sc_pkcs15_cert_info *p15_info = NULL;
	struct sc_pkcs15_cert *p15_cert = NULL;
	struct pkcs15_cert_object *object = NULL;
	struct pkcs15_pubkey_object *obj2 = NULL;
	int rv;

	p15_info = (struct sc_pkcs15_cert_info *) cert->data;

	if (cert->flags & SC_PKCS15_CO_FLAG_PRIVATE)  {	/* is the cert private? */
		p15_cert = NULL;			/* will read cert when needed */
	}
	else    {
		rv = sc_pkcs15_read_certificate(fw_data->p15_card, p15_info, &p15_cert);
		if (rv < 0)
			return rv;
	}

	/* Certificate object */
	rv = __pkcs15_create_object(fw_data, (struct pkcs15_any_object **) &object,
			cert, &pkcs15_cert_ops, sizeof(struct pkcs15_cert_object));
	if (rv < 0) {
		if (p15_cert != NULL)
			sc_pkcs15_free_certificate(p15_cert);
		return rv;
	}

	object->cert_info = p15_info;
	object->cert_data = p15_cert;

	/* Corresponding public key */
	rv = public_key_created(fw_data, &p15_info->id, (struct pkcs15_any_object **) &obj2);
	if (rv != SC_SUCCESS)
		rv = __pkcs15_create_object(fw_data, (struct pkcs15_any_object **) &obj2,
				NULL, &pkcs15_pubkey_ops, sizeof(struct pkcs15_pubkey_object));
	if (rv < 0)
		return rv;

	if (p15_cert) {
		 /* make a copy of public key from the cert */
		if (!obj2->pub_data)
			rv = sc_pkcs15_pubkey_from_cert(context, &p15_cert->data, &obj2->pub_data);
		if (rv < 0)
			return rv;
	}

	obj2->pub_genfrom = object;
	object->cert_pubkey = obj2;

	/* Find missing labels for certificate */
	pkcs15_cert_extract_label(object);

	if (cert_object != NULL)
		*cert_object = (struct pkcs15_any_object *) object;

	return 0;
}


static int
__pkcs15_create_pubkey_object(struct pkcs15_fw_data *fw_data,
	struct sc_pkcs15_object *pubkey, struct pkcs15_any_object **pubkey_object)
{
	struct pkcs15_pubkey_object *object = NULL;
	struct sc_pkcs15_pubkey *p15_key = NULL;
	int rv;

	/* Read public key from card */
	/* Attempt to read pubkey from card or file.
	 * During initialization process, the key may have been created
	 * and saved as a file before the certificate has been created.
	 */
	if (pubkey->flags & SC_PKCS15_CO_FLAG_PRIVATE)   {	/* is the key private? */
		sc_log(context, "No pubkey");
		p15_key = NULL;					/* will read key when needed */
	}
	else {
		/* if emulation already created pubkey use it */
		if (pubkey->emulated && (fw_data->p15_card->flags & SC_PKCS15_CARD_FLAG_EMULATED)) {
			sc_log(context, "Use emulated pubkey");
			p15_key = (struct sc_pkcs15_pubkey *) pubkey->emulated;
		}
		else {
			sc_log(context, "Get pubkey from PKCS#15 object");
			rv = sc_pkcs15_read_pubkey(fw_data->p15_card, pubkey, &p15_key);
			if (rv < 0)
				 p15_key = NULL;
		}
	}

	/* Public key object */
	rv = __pkcs15_create_object(fw_data, (struct pkcs15_any_object **) &object,
			pubkey, &pkcs15_pubkey_ops, sizeof(struct pkcs15_pubkey_object));
	if (rv >= 0) {
		object->pub_info = (struct sc_pkcs15_pubkey_info *) pubkey->data;
		object->pub_data = p15_key;
		if (p15_key && object->pub_info->modulus_length == 0 && p15_key->algorithm == SC_ALGORITHM_RSA)
			object->pub_info->modulus_length = 8 * p15_key->u.rsa.modulus.len;
	} else if (!(pubkey->emulated && (fw_data->p15_card->flags & SC_PKCS15_CARD_FLAG_EMULATED))) {
		sc_pkcs15_free_pubkey(p15_key);
	}
	if (object && object->pub_data) {
		if ((object->pub_data->alg_id)&&(object->pub_data->algorithm == SC_ALGORITHM_GOSTR3410))
			object->pub_data->alg_id->params = &((object->pub_data->u).gostr3410.params);
	}
	if (pubkey_object != NULL)
		*pubkey_object = (struct pkcs15_any_object *) object;

	return rv;
}


static int
__pkcs15_create_prkey_object(struct pkcs15_fw_data *fw_data,
	struct sc_pkcs15_object *prkey, struct pkcs15_any_object **prkey_object)
{
	struct pkcs15_prkey_object *object = NULL;
	int rv;

	rv = __pkcs15_create_object(fw_data, (struct pkcs15_any_object **) &object,
			prkey, &pkcs15_prkey_ops, sizeof(struct pkcs15_prkey_object));
	if (rv >= 0)
		object->prv_info = (struct sc_pkcs15_prkey_info *) prkey->data;

	if (prkey_object != NULL)
		*prkey_object = (struct pkcs15_any_object *) object;

	return rv;
}


static int
__pkcs15_create_data_object(struct pkcs15_fw_data *fw_data,
		struct sc_pkcs15_object *object, struct pkcs15_any_object **data_object)
{
	struct pkcs15_data_object *dobj = NULL;
	int rv;

	rv = __pkcs15_create_object(fw_data, (struct pkcs15_any_object **) &dobj,
			object, &pkcs15_dobj_ops, sizeof(struct pkcs15_data_object));
	if (rv >= 0)   {
		dobj->info = (struct sc_pkcs15_data_info *) object->data;
		dobj->value = NULL;
	}

	if (data_object != NULL)
		*data_object = (struct pkcs15_any_object *) dobj;

	return rv;
}


static int
__pkcs15_create_secret_key_object(struct pkcs15_fw_data *fw_data,
		struct sc_pkcs15_object *object, struct pkcs15_any_object **skey_object)
{
	struct pkcs15_skey_object *skey = NULL;
	int rv;

	rv = __pkcs15_create_object(fw_data, (struct pkcs15_any_object **) &skey,
			object, &pkcs15_skey_ops, sizeof(struct pkcs15_skey_object));
	if (rv >= 0)
		skey->info = (struct sc_pkcs15_skey_info *) object->data;

	if (skey_object != NULL)
		*skey_object = (struct pkcs15_any_object *) skey;

	return rv;
}


static int
pkcs15_create_pkcs11_objects(struct pkcs15_fw_data *fw_data, int p15_type, const char *name,
		int (*create)(struct pkcs15_fw_data *, struct sc_pkcs15_object *,
			struct pkcs15_any_object **any_object))
{
	struct sc_pkcs15_object *p15_object[MAX_OBJECTS];
	int i, count, rv;

	rv = count = sc_pkcs15_get_objects(fw_data->p15_card, p15_type, p15_object, MAX_OBJECTS);
	if (rv >= 0)
		sc_log(context, "Found %d %s%s", count, name, (count == 1)? "" : "s");

	for (i = 0; rv >= 0 && i < count; i++)
		rv = create(fw_data, p15_object[i], NULL);

	return count;
}


static void
__pkcs15_prkey_bind_related(struct pkcs15_fw_data *fw_data, struct pkcs15_prkey_object *pk)
{
	struct sc_pkcs15_id *id = &pk->prv_info->id;
	unsigned int i;

	sc_log(context, "Object is a private key and has id %s", sc_pkcs15_print_id(id));

	for (i = 0; i < fw_data->num_objects; i++) {
		struct pkcs15_any_object *obj = fw_data->objects[i];

		if (obj->base.flags & SC_PKCS11_OBJECT_HIDDEN)
			continue;
		if (is_privkey(obj) && obj != (struct pkcs15_any_object *) pk) {
			/* merge private keys with the same ID and
			 * different usage bits */
			struct pkcs15_prkey_object *other, **pp;

			other = (struct pkcs15_prkey_object *) obj;
			if (sc_pkcs15_compare_id(&other->prv_info->id, id)) {
				obj->base.flags |= SC_PKCS11_OBJECT_HIDDEN;
				for (pp = &pk->prv_next; *pp; pp = &(*pp)->prv_next)
					;
				*pp = (struct pkcs15_prkey_object *) obj;
			}
		}
		else if (is_pubkey(obj) && !pk->prv_pubkey) {
			struct pkcs15_pubkey_object *pubkey;

			pubkey = (struct pkcs15_pubkey_object *) obj;
			if (sc_pkcs15_compare_id(&pubkey->pub_info->id, id)) {
				sc_log(context, "Associating object %d as public key", i);
				pk->prv_pubkey = pubkey;
				if (pubkey->pub_data) {
					sc_pkcs15_dup_pubkey(context, pubkey->pub_data, &pk->pub_data);
					if (pk->prv_info->modulus_length == 0)
						pk->prv_info->modulus_length = pubkey->pub_info->modulus_length;
				}
			}
		}
	}
}


static void
__pkcs15_cert_bind_related(struct pkcs15_fw_data *fw_data, struct pkcs15_cert_object *cert)
{
	struct sc_pkcs15_cert *c1 = cert->cert_data;
	struct sc_pkcs15_id *id = &cert->cert_info->id;
	unsigned int i;

	sc_log(context, "Object is a certificate and has id %s", sc_pkcs15_print_id(id));

	/* Loop over all objects to see if we find the certificate of
	 * the issuer and the associated private key */
	for (i = 0; i < fw_data->num_objects; i++) {
		struct pkcs15_any_object *obj = fw_data->objects[i];

		if (is_cert(obj) && obj != (struct pkcs15_any_object *) cert) {
			struct pkcs15_cert_object *cert2;
			struct sc_pkcs15_cert *c2;

			cert2 = (struct pkcs15_cert_object *) obj;
			c2 = cert2->cert_data;

			if (!c1 || !c2 || !c1->issuer_len || !c2->subject_len)
				continue;
			if (c1->issuer_len == c2->subject_len
			 && !memcmp(c1->issuer, c2->subject, c1->issuer_len)) {
				sc_log(context, "Associating object %d (id %s) as issuer",
						i, sc_pkcs15_print_id(&cert2->cert_info->id));
				cert->cert_issuer = (struct pkcs15_cert_object *) obj;
				return;
			}
		} else
		if (is_privkey(obj) && !cert->cert_prvkey) {
			struct pkcs15_prkey_object *pk;

			pk = (struct pkcs15_prkey_object *) obj;
			if (sc_pkcs15_compare_id(&pk->prv_info->id, id)) {
				sc_log(context, "Associating object %d as private key", i);
				cert->cert_prvkey = pk;
			}
		}
	}
}

static void
pkcs15_bind_related_objects(struct pkcs15_fw_data *fw_data)
{
	unsigned int i;

	/* Loop over all private keys and attached related certificate
	 * and/or public key
	 */
	for (i = 0; i < fw_data->num_objects; i++) {
		struct pkcs15_any_object *obj = fw_data->objects[i];

		if (obj->base.flags & SC_PKCS11_OBJECT_HIDDEN)
			continue;

		sc_log(context, "Looking for objects related to object %d", i);
		if (is_privkey(obj))
			__pkcs15_prkey_bind_related(fw_data, (struct pkcs15_prkey_object *) obj);
		else if (is_cert(obj))
			__pkcs15_cert_bind_related(fw_data, (struct pkcs15_cert_object *) obj);
	}
}


/* We deferred reading of the cert until needed, as it may be
 * a private object, so we must wait till login to read  */
static int
check_cert_data_read(struct pkcs15_fw_data *fw_data, struct pkcs15_cert_object *cert)
{
	struct pkcs15_pubkey_object *obj2;
	int rv;

	if (!cert)
		return SC_ERROR_OBJECT_NOT_FOUND;

	if (cert->cert_data)
		return 0;
	rv = sc_pkcs15_read_certificate(fw_data->p15_card, cert->cert_info, &cert->cert_data);
	if (rv < 0)
		return rv;

	obj2 = cert->cert_pubkey;
	/* make a copy of public key from the cert data */
	if (!obj2->pub_data)
		rv = sc_pkcs15_pubkey_from_cert(context, &cert->cert_data->data, &obj2->pub_data);

	/* Find missing labels for certificate */
	pkcs15_cert_extract_label(cert);

	/* now that we have the cert and pub key, lets see if we can bind anything else */
	pkcs15_bind_related_objects(fw_data);

	return rv;
}


static void
pkcs15_add_object(struct sc_pkcs11_slot *slot, struct pkcs15_any_object *obj,
		  CK_OBJECT_HANDLE_PTR pHandle)
{
	unsigned int i;
	struct pkcs15_fw_data *card_fw_data;
	CK_OBJECT_HANDLE handle =
		(CK_OBJECT_HANDLE)(uintptr_t)obj; /* cast pointer to long, will truncate on Win64 */

	if (obj == NULL || slot == NULL)
		return;
	if (obj->base.flags & (SC_PKCS11_OBJECT_HIDDEN | SC_PKCS11_OBJECT_RECURS))
		return;

	if (list_contains(&slot->objects, obj))
		return;

	if (pHandle != NULL)
		*pHandle = handle;

	list_append(&slot->objects, obj);
	sc_log(context, "Slot:%lX Setting object handle of 0x%lx to 0x%lx",
		   slot->id, obj->base.handle, handle);
	obj->base.handle = handle;
	obj->base.flags |= SC_PKCS11_OBJECT_SEEN;
	obj->refcount++;

	/* Add related objects
	 * XXX prevent infinite recursion when a card specifies two certificates
	 * referring to each other.
	 */
	obj->base.flags |= SC_PKCS11_OBJECT_RECURS;

	switch (__p15_type(obj)) {
	case SC_PKCS15_TYPE_PRKEY_RSA:
	case SC_PKCS15_TYPE_PRKEY_GOSTR3410:
	case SC_PKCS15_TYPE_PRKEY_EC:
		if (slot->p11card != NULL) {
			pkcs15_add_object(slot, (struct pkcs15_any_object *) obj->related_pubkey, NULL);
			if (!slot->p11card)
				return;
			card_fw_data = (struct pkcs15_fw_data *) slot->p11card->fws_data[slot->fw_data_idx];
			for (i = 0; i < card_fw_data->num_objects; i++) {
				struct pkcs15_any_object *obj2 = card_fw_data->objects[i];
				struct pkcs15_cert_object *cert;

				if (!is_cert(obj2))
					continue;

				cert = (struct pkcs15_cert_object*) obj2;

				if ((struct pkcs15_any_object*)(cert->cert_prvkey) != obj)
					continue;

				pkcs15_add_object(slot, obj2, NULL);
			}
		}
		break;
	case SC_PKCS15_TYPE_CERT_X509:
		pkcs15_add_object(slot, (struct pkcs15_any_object *) obj->related_pubkey, NULL);
		pkcs15_add_object(slot, (struct pkcs15_any_object *) obj->related_cert, NULL);
		break;
	}

	obj->base.flags &= ~SC_PKCS11_OBJECT_RECURS;
}


static void
pkcs15_init_slot(struct sc_pkcs15_card *p15card, struct sc_pkcs11_slot *slot,
		struct sc_pkcs15_object *auth, struct sc_app_info *app_info)
{
	struct pkcs15_slot_data *fw_data;
	struct sc_pkcs15_auth_info *pin_info = NULL;
	int write_protected;
	scconf_block *atrblock;

	sc_log(context, "Called");
	pkcs15_init_token_info(p15card, &slot->token_info);
	slot->token_info.flags |= CKF_TOKEN_INITIALIZED;
	if (auth != NULL)
		slot->token_info.flags |= CKF_USER_PIN_INITIALIZED;

	if ((p15card->card->reader->capabilities & SC_READER_CAP_PIN_PAD) || (p15card->card->caps & SC_CARD_CAP_PROTECTED_AUTHENTICATION_PATH))
		slot->token_info.flags |= CKF_PROTECTED_AUTHENTICATION_PATH;

	if (p15card->card->caps & SC_CARD_CAP_RNG && p15card->card->ops->get_challenge != NULL)
		slot->token_info.flags |= CKF_RNG;

	if (p15card->tokeninfo && p15card->tokeninfo->flags & SC_PKCS15_TOKEN_READONLY) {
		write_protected = 1;
	} else {
		write_protected = 0;
	}
	atrblock = _sc_match_atr_block(p15card->card->ctx, NULL, &p15card->card->atr);
	if (atrblock) {
		write_protected = scconf_get_bool(atrblock, "read_only", write_protected);
	}
	if (write_protected) {
		slot->token_info.flags |= CKF_WRITE_PROTECTED;
	}

	slot->fw_data = fw_data = calloc(1, sizeof(*fw_data));
	if (!fw_data) {
		return;
	}
	fw_data->auth_obj = auth;

	if (auth != NULL) {
		pin_info = (struct sc_pkcs15_auth_info*) auth->data;

		if (pin_info->auth_type != SC_PKCS15_PIN_AUTH_TYPE_PIN)   {
			pin_info = NULL;
		}
		else   {
			size_t pin_len = 0;
			if (auth->label[0] && strncmp(auth->label, "PIN", 4) != 0)
				pin_len = strlen(auth->label);

			if (pin_len) {
				size_t tokeninfo_len = 0;
				if (p15card->tokeninfo)
					tokeninfo_len = strlen(p15card->tokeninfo->label);
				/* Print the possibly truncated token label with at least 4
				 * characters followed by the PIN label in paranthesis */
				if (tokeninfo_len == 0
						|| pin_len + strlen("L... ()") > 32) {
					/* There is no token label or it doesn't fit,
					 * print only PIN label */
					strcpy_bp(slot->token_info.label, auth->label, 32);
				} else {
					size_t max_tokeninfo_len = MIN(tokeninfo_len,
							32 - pin_len - strlen(" ()"));
					strcpy_bp(slot->token_info.label,
							p15card->tokeninfo->label,
							max_tokeninfo_len);
					slot->token_info.label[max_tokeninfo_len]           = ' ';
					slot->token_info.label[max_tokeninfo_len+1]         = '(';
					slot->token_info.label[max_tokeninfo_len+2+pin_len] = ')';
					strcpy_bp(slot->token_info.label+max_tokeninfo_len+2,
							auth->label, pin_len);
				}
			} else {
				/* PIN label is empty or just says non-useful "PIN",
				 * print only token label */
				strcpy_bp(slot->token_info.label,
						p15card->tokeninfo ? p15card->tokeninfo->label : "",
						32);
			}
			slot->token_info.flags |= CKF_LOGIN_REQUIRED;
		}
	}

	if (pin_info) {
		slot->token_info.ulMaxPinLen = pin_info->attrs.pin.max_length;
		slot->token_info.ulMinPinLen = pin_info->attrs.pin.min_length;
	}
	else {
		/* choose reasonable defaults */
		slot->token_info.ulMaxPinLen = 8;
		slot->token_info.ulMinPinLen = 4;
		strcpy_bp(slot->token_info.label,
				p15card->tokeninfo ? p15card->tokeninfo->label : "",
				32);
	}

	slot->app_info = app_info;
	sc_log(context, "Initialized slot 0x%lx with token %*s", slot->id,
			(int)sizeof(slot->token_info.label), slot->token_info.label);
}


static CK_RV
pkcs15_create_slot(struct sc_pkcs11_card *p11card, struct pkcs15_fw_data *fw_data,
		struct sc_pkcs15_object *auth, struct sc_app_info *app_info,
		struct sc_pkcs11_slot **out)
{
	struct sc_pkcs11_slot *slot = NULL;
	CK_RV rv;

	rv = slot_allocate(&slot, p11card);
	if (rv != CKR_OK)
		return rv;

	/* There's a token in this slot */
	slot->slot_info.flags |= CKF_TOKEN_PRESENT;

	/* Fill in the slot/token info from pkcs15 data */
	if (fw_data)
		pkcs15_init_slot(fw_data->p15_card, slot, auth, app_info);
	else {
		/* Token is not initialized, announce pinpad capability nevertheless */
		if (slot->reader->capabilities & SC_READER_CAP_PIN_PAD)
			slot->token_info.flags |= CKF_PROTECTED_AUTHENTICATION_PATH;
	}

	*out = slot;
	return CKR_OK;
}


static int
_pkcs15_create_typed_objects(struct pkcs15_fw_data *fw_data)
{
	int rv;

	rv = pkcs15_create_pkcs11_objects(fw_data, SC_PKCS15_TYPE_PRKEY_RSA, "RSA private key",
			__pkcs15_create_prkey_object);
	if (rv < 0)
		return rv;

	rv = pkcs15_create_pkcs11_objects(fw_data, SC_PKCS15_TYPE_PUBKEY_RSA, "RSA public key",
			__pkcs15_create_pubkey_object);
	if (rv < 0)
		return rv;

	rv = pkcs15_create_pkcs11_objects(fw_data, SC_PKCS15_TYPE_PRKEY_EC, "EC private key",
			__pkcs15_create_prkey_object);
	if (rv < 0)
		return rv;

	rv = pkcs15_create_pkcs11_objects(fw_data, SC_PKCS15_TYPE_PUBKEY_EC, "EC public key",
			__pkcs15_create_pubkey_object);
	if (rv < 0)
		return rv;

	rv = pkcs15_create_pkcs11_objects(fw_data, SC_PKCS15_TYPE_PRKEY_GOSTR3410, "GOSTR3410 private key",
			__pkcs15_create_prkey_object);
	if (rv < 0)
		return rv;

	rv = pkcs15_create_pkcs11_objects(fw_data, SC_PKCS15_TYPE_PUBKEY_GOSTR3410, "GOSTR3410 public key",
			__pkcs15_create_pubkey_object);
	if (rv < 0)
		return rv;

	rv = pkcs15_create_pkcs11_objects(fw_data, SC_PKCS15_TYPE_CERT_X509, "certificate",
			__pkcs15_create_cert_object);
	if (rv < 0)
		return rv;

	rv = pkcs15_create_pkcs11_objects(fw_data, SC_PKCS15_TYPE_DATA_OBJECT, "data object",
			__pkcs15_create_data_object);
	if (rv < 0)
		return rv;

	rv = pkcs15_create_pkcs11_objects(fw_data, SC_PKCS15_TYPE_SKEY_GENERIC, "Generic secret key",
			__pkcs15_create_secret_key_object);
	if (rv < 0)
		return rv;

	/* Match up related keys and certificates */
	pkcs15_bind_related_objects(fw_data);
	sc_log(context, "found %i FW objects", fw_data->num_objects);

	return rv;
}


int
_is_slot_auth_object(struct sc_pkcs15_auth_info *pin_info)
{

	/* Ignore all but PIN authentication objects */
	if (pin_info->auth_type != SC_PKCS15_PIN_AUTH_TYPE_PIN)
		return 0;

	/* Ignore any non-authentication PINs */
	if ((pin_info->attrs.pin.flags & SC_PKCS15_PIN_FLAG_SO_PIN) != 0)
		return 0;

	/* Ignore unblocking pins */
	if (!sc_pkcs11_conf.create_puk_slot)
		if (pin_info->attrs.pin.flags & SC_PKCS15_PIN_FLAG_UNBLOCKING_PIN)
			return 0;

	return 1;
}

int slot_get_logged_in_state(struct sc_pkcs11_slot *slot)
{
	int logged_in = SC_PIN_STATE_UNKNOWN;
	struct pkcs15_fw_data *fw_data = NULL;
	struct sc_pkcs15_card *p15card = NULL;
	struct sc_pkcs15_object *pin_obj = NULL;
	struct sc_pkcs15_auth_info *pin_info;

	if (slot->p11card == NULL) {
		goto out;
	}

	fw_data = (struct pkcs15_fw_data *) slot->p11card->fws_data[slot->fw_data_idx];
	if (!fw_data)
		goto out;
	p15card = fw_data->p15_card;

	if (slot->login_user == CKU_SO) {
		sc_pkcs15_find_so_pin(p15card, &pin_obj);
	} else {
		pin_obj = slot_data_auth(slot->fw_data);
	}

	if (!pin_obj)
		goto out;

	pin_info = (struct sc_pkcs15_auth_info *)pin_obj->data;
	if (!pin_info)
		goto out;
	sc_pkcs15_get_pin_info(p15card, pin_obj);
	logged_in = pin_info->logged_in;
out:
	return logged_in;
}


struct sc_pkcs15_object *
_get_auth_object_by_name(struct sc_pkcs15_card *p15card, char *name)
{
	struct sc_pkcs15_object *out = NULL;
	int rv = SC_ERROR_OBJECT_NOT_FOUND;

	/* please keep me in sync with md_get_pin_by_role() in minidriver */
	if (!strcmp(name, "UserPIN"))   {
		/* Try to get 'global' PIN; if no, get the 'local' one */
		rv = sc_pkcs15_find_pin_by_flags(p15card, SC_PKCS15_PIN_TYPE_FLAGS_PIN_GLOBAL,
				SC_PKCS15_PIN_TYPE_FLAGS_MASK, NULL, &out);
		if (rv)

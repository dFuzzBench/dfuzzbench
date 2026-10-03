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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "libopensc/log.h"
#include "libopensc/opensc.h"
#include "libopensc/cardctl.h"
#include "libopensc/cards.h"
#include "pkcs15-init.h"
#include "profile.h"
static int epass2003_pkcs15_erase_card(struct sc_profile *profile,
				       struct sc_pkcs15_card *p15card)
{
	SC_FUNC_CALLED(p15card->card->ctx, SC_LOG_DEBUG_VERBOSE);

	if (sc_select_file(p15card->card, sc_get_mf_path(), NULL) < 0)
		return SC_SUCCESS;

	return sc_card_ctl(p15card->card, SC_CARDCTL_ERASE_CARD, 0);
}

static int epass2003_pkcs15_init_card(struct sc_profile *profile,
				      struct sc_pkcs15_card *p15card)
{
	struct sc_card *card = p15card->card;
	int ret;

	SC_FUNC_CALLED(card->ctx, SC_LOG_DEBUG_VERBOSE);
	sc_do_log(card->ctx, SC_LOG_DEBUG_VERBOSE_TOOL,NULL,0,NULL,
			"ePass2003 doesn't support SO-PIN and SO-PUK. You can unblock key with PUK. \n");
	{			/* MF */
		struct sc_file *mf_file;
		struct sc_file *skey_file;

		ret = sc_profile_get_file(profile, "MF", &mf_file);
		LOG_TEST_RET(card->ctx, ret,
			    "Get MF info failed");
		ret = sc_create_file(card, mf_file);
		sc_file_free(mf_file);
		LOG_TEST_RET(card->ctx, ret,
			    "Create MF failed");

		ret = sc_profile_get_file(profile, "SKey-MF", &skey_file);
		LOG_TEST_RET(card->ctx, ret,
			    "Get SKey info failed");
		ret = sc_create_file(card, skey_file);
		sc_file_free(skey_file);
		LOG_TEST_RET(card->ctx, ret,
			    "Create SKey failed");

	}

	{			/* EF(DIR) */
		struct sc_file *dir_file;

		/* get dir profile */
		ret = sc_profile_get_file(profile, "DIR", &dir_file);
		LOG_TEST_RET(card->ctx, ret,
			    "Get EF(DIR) info failed");
		ret = sc_create_file(card, dir_file);
		sc_file_free(dir_file);
		LOG_TEST_RET(card->ctx, ret,
			    "Create EF(DIR) failed");

		sc_free_apps(card);
	}
	SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, SC_SUCCESS);
}

static int epass2003_pkcs15_create_dir(struct sc_profile *profile,
				       struct sc_pkcs15_card *p15card,
				       struct sc_file *df)
{
	struct sc_card *card = p15card->card;
	int ret;

	SC_FUNC_CALLED(card->ctx, SC_LOG_DEBUG_VERBOSE);

	{			/* p15 DF */
		struct sc_file *df_file;
		struct sc_file *skey_file;
		struct sc_file *ef_file;
		u8 max_counter[2] = { 0 };
		int id;
		u8 user_maxtries = 0;
		u8 so_maxtries = 0;

		ret = sc_profile_get_file(profile, "PKCS15-AppDF", &df_file);
		LOG_TEST_RET(card->ctx, ret,
			    "Get PKCS15-AppDF info failed");
		ret = sc_create_file(card, df_file);
		sc_file_free(df_file);
		LOG_TEST_RET(card->ctx, ret,
			    "Create PKCS15-AppDF failed");

		ret = sc_profile_get_file(profile, "SKey-AppDF", &skey_file);
		LOG_TEST_RET(card->ctx, ret,
			    "Get SKey info failed");
		ret = sc_create_file(card, skey_file);
		sc_file_free(skey_file);
		LOG_TEST_RET(card->ctx, ret,
			    "Create SKey info failed");

		ret = sc_profile_get_file(profile, "MAXPIN", &ef_file);
		LOG_TEST_RET(card->ctx, ret,
			    "Get MAXPIN info failed");
		ret = sc_create_file(card, ef_file);
		LOG_TEST_RET(card->ctx, ret,
			    "Create MAXPIN failed");
		ret = sc_select_file(card, &(ef_file->path), &ef_file);
		LOG_TEST_RET(card->ctx, ret,
			    "Select MAXPIN failed");

		ret = sc_profile_get_pin_id(profile, 2, &id);
		LOG_TEST_RET(card->ctx, ret,
			    "Get User PIN id error!");
		user_maxtries = (u8) sc_profile_get_pin_retries(profile, id);

		ret = sc_profile_get_pin_id(profile, 1, &id);
		LOG_TEST_RET(card->ctx, ret,
			    "Get User PIN id error!");
		so_maxtries = (u8) sc_profile_get_pin_retries(profile, id);

		max_counter[0] = user_maxtries;
		max_counter[1] = so_maxtries;

		ret = sc_update_binary(card, 0, max_counter, 2, 0);

		LOG_TEST_RET(card->ctx, ret,
			    "Update MAXPIN failed");
		sc_file_free(ef_file);
	}

	{			/* p15 efs */
		char *create_efs[] = {
			"PKCS15-ODF",
			"PKCS15-TokenInfo",
			"PKCS15-UnusedSpace",
			"PKCS15-AODF",
			"PKCS15-PrKDF",
			"PKCS15-PuKDF",
			"PKCS15-CDF",
			"PKCS15-DODF",
			NULL,
		};
		int i;
		struct sc_file *file = 0;

		for (i = 0; create_efs[i]; ++i) {
			if (sc_profile_get_file(profile, create_efs[i], &file)) {
				sc_log(card->ctx,
					 "Inconsistent profile: cannot find %s",
					 create_efs[i]);
				SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE,
					       SC_ERROR_INCONSISTENT_PROFILE);
			}
			ret = sc_create_file(card, file);
			sc_file_free(file);
			LOG_TEST_RET(card->ctx, ret,
				    "Create pkcs15 file failed");
		}
	}

	SC_FUNC_RETURN(card->ctx, SC_LOG_DEBUG_VERBOSE, ret);
}

static int epass2003_pkcs15_pin_reference(struct sc_profile *profile,
					  struct sc_pkcs15_card *p15card,
					  struct sc_pkcs15_auth_info *auth_info)
{
	SC_FUNC_CALLED(p15card->card->ctx, SC_LOG_DEBUG_VERBOSE);

	if (auth_info->auth_type != SC_PKCS15_PIN_AUTH_TYPE_PIN)
		return SC_ERROR_OBJECT_NOT_VALID;

	if (auth_info->attrs.pin.reference < ENTERSAFE_USER_PIN_ID
	    || auth_info->attrs.pin.reference > ENTERSAFE_SO_PIN_ID)
		return SC_ERROR_INVALID_PIN_REFERENCE;

	SC_FUNC_RETURN(p15card->card->ctx, SC_LOG_DEBUG_VERBOSE, SC_SUCCESS);
}

static int epass2003_pkcs15_create_pin(struct sc_profile *profile,
				       struct sc_pkcs15_card *p15card,
				       struct sc_file *df,
				       struct sc_pkcs15_object *pin_obj,
				       const unsigned char *pin, size_t pin_len,
				       const unsigned char *puk, size_t puk_len)
{
	struct sc_card *card = p15card->card;
	int r;
	struct sc_pkcs15_auth_info *auth_info;

	if (NULL == pin_obj)
		return SC_ERROR_INVALID_ARGUMENTS;

	auth_info = (struct sc_pkcs15_auth_info *)pin_obj->data;

	SC_FUNC_CALLED(card->ctx, SC_LOG_DEBUG_VERBOSE);

	if (auth_info->auth_type != SC_PKCS15_PIN_AUTH_TYPE_PIN)
		return SC_ERROR_OBJECT_NOT_VALID;

	{			/*pin */
		sc_epass2003_wkey_data data;
		int id;

		if (!pin || !pin_len || pin_len > 16)
			return SC_ERROR_INVALID_ARGUMENTS;

		data.type = SC_EPASS2003_SECRET_PIN;
		data.key_data.es_secret.kid = auth_info->attrs.pin.reference;
		data.key_data.es_secret.ac[0] =
		    EPASS2003_AC_MAC_NOLESS | EPASS2003_AC_EVERYONE;
		data.key_data.es_secret.ac[1] =
		    EPASS2003_AC_MAC_NOLESS | EPASS2003_AC_USER;


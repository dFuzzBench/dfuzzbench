/*
 * Initialize Cards according to PKCS#15
 *
 * Copyright (C) 2002 Olaf Kirch <okir@suse.de>
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
 *
 * Random notes
 *  -	the "key" command should go away, it's obsolete
 */

#include "config.h"

#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <assert.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#include <winreg.h>
#endif

#include "common/compat_strlcpy.h"
#include "scconf/scconf.h"
#include "libopensc/log.h"
#include "libopensc/pkcs15.h"
#include "pkcs15-init.h"
#include "profile.h"

#define DEF_PRKEY_RSA_ACCESS	0x1D
#define DEF_PRKEY_DSA_ACCESS	0x12
#define DEF_PUBKEY_ACCESS	0x12

#define TEMPLATE_FILEID_MIN_DIFF	0x20

/*
#define DEBUG_PROFILE
*/

/*
 * Parser state
 */
struct state {
	struct state *		frame;
	const char *		filename;
	struct sc_profile *	profile;
	struct file_info *	file;
	struct pin_info *	pin;
	struct auth_info *	key;
};


struct command {
	const char *		name;
	int			min_args, max_args;
	int			(*func)(struct state *, int, char **);
};

struct block {
	const char *		name;
	int			(*handler)(struct state *,
					struct block *,
					const char *,
					scconf_block *);
	struct command *	cmd_info;
	struct block *		blk_info;
};

struct map {
	const char *		name;
	unsigned int		val;
};

static struct map		aclNames[] = {
	{ "NONE",	SC_AC_NONE	},
	{ "NEVER",	SC_AC_NEVER	},
	{ "CHV",	SC_AC_CHV	},
	{ "TERM",	SC_AC_TERM	},
	{ "PRO",	SC_AC_PRO	},
	{ "AUT",	SC_AC_AUT	},
	{ "KEY",	SC_AC_AUT	},
	{ "SEN",	SC_AC_SEN	},
	{ "IDA",	SC_AC_IDA	},
	{ "SCB",	SC_AC_SCB	},
	{ NULL, 0 }
};
static struct map		fileOpNames[] = {
	{ "SELECT",	SC_AC_OP_SELECT	},
	{ "LOCK",	SC_AC_OP_LOCK	},
	{ "DELETE",	SC_AC_OP_DELETE	},
	{ "DELETE-SELF",SC_AC_OP_DELETE_SELF },
	{ "CREATE",	SC_AC_OP_CREATE	},
	{ "CREATE-EF",	SC_AC_OP_CREATE_EF	},
	{ "CREATE-DF",	SC_AC_OP_CREATE_DF	},
	{ "REHABILITATE",SC_AC_OP_REHABILITATE	},
	{ "INVALIDATE",	SC_AC_OP_INVALIDATE	},
	{ "FILES",	SC_AC_OP_LIST_FILES	},
	{ "READ",	SC_AC_OP_READ	},
	{ "UPDATE",	SC_AC_OP_UPDATE	},
	{ "WRITE",	SC_AC_OP_WRITE	},
	{ "ERASE",	SC_AC_OP_ERASE	},
	{ "CRYPTO",     SC_AC_OP_CRYPTO },
        { "PIN-DEFINE", SC_AC_OP_PIN_DEFINE },
        { "PIN-CHANGE", SC_AC_OP_PIN_CHANGE },
        { "PIN-RESET",  SC_AC_OP_PIN_RESET },
        { "PIN-USE",	SC_AC_OP_PIN_USE },
	{ "GENERATE",	SC_AC_OP_GENERATE },
	{ "PSO-COMPUTE-SIGNATURE",	SC_AC_OP_PSO_COMPUTE_SIGNATURE },
	{ "INTERNAL-AUTHENTICATE",	SC_AC_OP_INTERNAL_AUTHENTICATE },
	{ "PSO-DECRYPT",		SC_AC_OP_PSO_DECRYPT },
	{ "RESIZE",	SC_AC_OP_RESIZE },
	{ "ADMIN",	SC_AC_OP_ADMIN	},
	{ "ACTIVATE",	SC_AC_OP_ACTIVATE },
	{ "DEACTIVATE",	SC_AC_OP_DEACTIVATE },
	{ NULL, 0 }
};
static struct map		fileTypeNames[] = {
	{ "EF",		SC_FILE_TYPE_WORKING_EF		},
	{ "INTERNAL-EF",SC_FILE_TYPE_INTERNAL_EF	},
	{ "DF",		SC_FILE_TYPE_DF			},
	{ "BSO",	SC_FILE_TYPE_BSO		},
	{ NULL, 0 }
};
static struct map		fileStructureNames[] = {
	{ "TRANSPARENT",	SC_FILE_EF_TRANSPARENT	},
	{ "LINEAR-FIXED",	SC_FILE_EF_LINEAR_FIXED	},
	{ "LINEAR-FIXED-TLV",	SC_FILE_EF_LINEAR_FIXED_TLV	},
	{ "LINEAR-VARIABLE",	SC_FILE_EF_LINEAR_VARIABLE	},
	{ "LINEAR-VARIABLE-TLV",SC_FILE_EF_LINEAR_VARIABLE_TLV	},
	{ "CYCLIC",		SC_FILE_EF_CYCLIC	},
	{ "CYCLIC-TLV",		SC_FILE_EF_CYCLIC_TLV	},
	{ NULL, 0 }
};
static struct map		pkcs15DfNames[] = {
	{ "PRKDF",		SC_PKCS15_PRKDF		},
	{ "PUKDF",		SC_PKCS15_PUKDF		},
	{ "PUKDF-TRUSTED",	SC_PKCS15_PUKDF_TRUSTED	},
	{ "SKDF",		SC_PKCS15_SKDF		},
	{ "CDF",		SC_PKCS15_CDF		},
	{ "CDF-TRUSTED",	SC_PKCS15_CDF_TRUSTED	},
	{ "CDF-USEFUL",		SC_PKCS15_CDF_USEFUL	},
	{ "DODF",		SC_PKCS15_DODF		},
	{ "AODF",		SC_PKCS15_AODF		},
	{ NULL, 0 }
};
static struct map		pinTypeNames[] = {
	{ "BCD",		SC_PKCS15_PIN_TYPE_BCD	},
	{ "ascii-numeric",	SC_PKCS15_PIN_TYPE_ASCII_NUMERIC	},
	{ "utf8",		SC_PKCS15_PIN_TYPE_UTF8	},
	{ "half-nibble-bcd",	SC_PKCS15_PIN_TYPE_HALFNIBBLE_BCD	},
	{ "iso9564-1",		SC_PKCS15_PIN_TYPE_ISO9564_1	},
	{ NULL, 0 }
};
static struct map		pinIdNames[] = {
	{ "pin",		SC_PKCS15INIT_USER_PIN	},
	{ "puk",		SC_PKCS15INIT_USER_PUK	},
	{ "user-pin",		SC_PKCS15INIT_USER_PIN	},
	{ "user-puk",		SC_PKCS15INIT_USER_PUK	},
	{ "sopin",		SC_PKCS15INIT_SO_PIN	},
	{ "sopuk",		SC_PKCS15INIT_SO_PUK	},
	{ "so-pin",		SC_PKCS15INIT_SO_PIN	},
	{ "so-puk",		SC_PKCS15INIT_SO_PUK	},
	{ NULL, 0 }
};
static struct map		pinFlagNames[] = {
	{ "case-sensitive",		SC_PKCS15_PIN_FLAG_CASE_SENSITIVE		},
	{ "local",			SC_PKCS15_PIN_FLAG_LOCAL			},
	{ "change-disabled",		SC_PKCS15_PIN_FLAG_CHANGE_DISABLED		},
	{ "unblock-disabled",		SC_PKCS15_PIN_FLAG_UNBLOCK_DISABLED		},
	{ "initialized",		SC_PKCS15_PIN_FLAG_INITIALIZED			},
	{ "needs-padding",		SC_PKCS15_PIN_FLAG_NEEDS_PADDING		},
	{ "unblockingPin",		SC_PKCS15_PIN_FLAG_UNBLOCKING_PIN		},
	{ "soPin",			SC_PKCS15_PIN_FLAG_SO_PIN			},
	{ "disable-allowed",		SC_PKCS15_PIN_FLAG_DISABLE_ALLOW		},
	{ "integrity-protected",	SC_PKCS15_PIN_FLAG_INTEGRITY_PROTECTED		},
	{ "confidentiality-protected",	SC_PKCS15_PIN_FLAG_CONFIDENTIALITY_PROTECTED	},
	{ "exchangeRefData",		SC_PKCS15_PIN_FLAG_EXCHANGE_REF_DATA		},
	{ NULL, 0 }
};
static struct map		idStyleNames[] = {
	{ "native",		SC_PKCS15INIT_ID_STYLE_NATIVE },
	{ "mozilla",		SC_PKCS15INIT_ID_STYLE_MOZILLA },
	{ "rfc2459",		SC_PKCS15INIT_ID_STYLE_RFC2459 },
	{ NULL, 0 }
};
static struct map              mdStyleNames[] = {
	{ "none",               SC_PKCS15INIT_MD_STYLE_NONE },
	{ "gemalto",            SC_PKCS15INIT_MD_STYLE_GEMALTO },
	{ NULL, 0 }
};
static struct {
	const char *		name;
	struct map *		addr;
} mapNames[] = {
	{ "file ACL",		aclNames	},
	{ "file operation",	fileOpNames	},
	{ "file type",		fileTypeNames	},
	{ "file structure",	fileStructureNames},
	{ "PKCS#15 file name",	pkcs15DfNames	},
	{ "pin encoding",	pinTypeNames	},
	{ "pin name",		pinIdNames	},
	{ "pin flag",		pinFlagNames	},
	{ NULL, NULL }
};

static int		process_conf(struct sc_profile *, scconf_context *);
static int		process_block(struct state *, struct block *,
				const char *, scconf_block *);
static void		init_state(struct state *, struct state *);
static int		get_authid(struct state *, const char *,
				unsigned int *, unsigned int *);
static int		get_uint(struct state *, const char *, unsigned int *);
static int		get_bool(struct state *, const char *, unsigned int *);
static int		get_uint_eval(struct state *, int, char **,
				unsigned int *);
static int		map_str2int(struct state *, const char *,
				unsigned int *, struct map *);
static int		setstr(char **strp, const char *value);
static void		parse_error(struct state *, const char *, ...);

static struct file_info *	sc_profile_instantiate_file(sc_profile_t *,
				struct file_info *, struct file_info *,
				unsigned int);
static struct file_info *	sc_profile_find_file(struct sc_profile *,
				const sc_path_t *, const char *);
static struct file_info *	sc_profile_find_file_by_path(
				struct sc_profile *,
				const sc_path_t *);

static struct pin_info *	new_pin(struct sc_profile *, int);
static struct file_info *	new_file(struct state *, const char *,
				unsigned int);
static struct file_info *	add_file(sc_profile_t *, const char *,
				sc_file_t *, struct file_info *);
static void		free_file_list(struct file_info **);
static void		append_file(sc_profile_t *, struct file_info *);
static struct auth_info *	new_key(struct sc_profile *,
				unsigned int, unsigned int);
static void		set_pin_defaults(struct sc_profile *,
				struct pin_info *);
static void		new_macro(sc_profile_t *, const char *, scconf_list *);
static sc_macro_t *	find_macro(sc_profile_t *, const char *);

static sc_file_t *
init_file(unsigned int type)
{
	struct sc_file	*file;
	unsigned int	op;

	file = sc_file_new();
	for (op = 0; op < SC_MAX_AC_OPS; op++) {
		sc_file_add_acl_entry(file, op, SC_AC_NONE, 0);
	}
	file->type = type;
	file->status = SC_FILE_STATUS_ACTIVATED;
	if (file->type != SC_FILE_TYPE_DF && file->type != SC_FILE_TYPE_BSO)
		file->ef_structure = SC_FILE_EF_TRANSPARENT;
	return file;
}

/*
 * Initialize profile
 */
struct sc_profile *
sc_profile_new(void)
{
	struct sc_pkcs15_card *p15card;
	struct sc_profile *pro;

	pro = calloc(1, sizeof(*pro));
	if (pro == NULL)
		return NULL;
	pro->p15_spec = p15card = sc_pkcs15_card_new();

	pro->pkcs15.do_last_update = 1;

	if (p15card) {
		p15card->tokeninfo->label = strdup("OpenSC Card");
		p15card->tokeninfo->manufacturer_id = strdup("OpenSC Project");
		p15card->tokeninfo->serial_number = strdup("0000");
		p15card->tokeninfo->flags = SC_PKCS15_TOKEN_EID_COMPLIANT;
		p15card->tokeninfo->version = 0;

		/* Set up EF(TokenInfo) and EF(ODF) */
		p15card->file_tokeninfo = init_file(SC_FILE_TYPE_WORKING_EF);
		p15card->file_odf = init_file(SC_FILE_TYPE_WORKING_EF);
		p15card->file_unusedspace = init_file(SC_FILE_TYPE_WORKING_EF);
	}

	/* Assume card does RSA natively, but no DSA */
	pro->rsa_access_flags = DEF_PRKEY_RSA_ACCESS;
	pro->dsa_access_flags = DEF_PRKEY_DSA_ACCESS;
	pro->pin_encoding = SC_PKCS15_PIN_TYPE_ASCII_NUMERIC;
	pro->pin_minlen = 4;
	pro->pin_maxlen = 8;
	pro->id_style = SC_PKCS15INIT_ID_STYLE_NATIVE;

	return pro;
}

int
sc_profile_load(struct sc_profile *profile, const char *filename)
{
	struct sc_context *ctx = profile->card->ctx;
	scconf_context	*conf;
	const char *profile_dir = NULL;
	char path[PATH_MAX];
	int res = 0, i;
#ifdef _WIN32
	char temp_path[PATH_MAX];
	size_t temp_len;
#endif

	LOG_FUNC_CALLED(ctx);
	for (i = 0; ctx->conf_blocks[i]; i++) {
		profile_dir = scconf_get_str(ctx->conf_blocks[i], "profile_dir", NULL);
		if (profile_dir)
			break;
	}

	if (!profile_dir) {
#ifdef _WIN32
		temp_len = PATH_MAX - 1;
		res = sc_ctx_win32_get_config_value(NULL, "ProfileDir", "Software\\OpenSC Project\\OpenSC",
				temp_path, &temp_len);
		if (res)
			LOG_FUNC_RETURN(ctx, res);
		temp_path[temp_len] = '\0';
		profile_dir = temp_path;
#else
		profile_dir = SC_PKCS15_PROFILE_DIRECTORY;
#endif
	}
	sc_log(ctx, "Using profile directory '%s'.", profile_dir);

#ifdef _WIN32
	snprintf(path, sizeof(path), "%s\\%s.%s", profile_dir, filename, SC_PKCS15_PROFILE_SUFFIX);
#else /* _WIN32 */
	snprintf(path, sizeof(path), "%s/%s.%s", profile_dir, filename, SC_PKCS15_PROFILE_SUFFIX);
#endif /* _WIN32 */

	sc_log(ctx, "Trying profile file %s", path);

	conf = scconf_new(path);
	res = scconf_parse(conf);

	sc_log(ctx, "profile %s loaded ok", path);

	if (res < 0) {
		scconf_free(conf);
		LOG_FUNC_RETURN(ctx, SC_ERROR_FILE_NOT_FOUND);
	}

	if (res == 0) {
		scconf_free(conf);
		LOG_FUNC_RETURN(ctx, SC_ERROR_SYNTAX_ERROR);
	}

	res = process_conf(profile, conf);
	scconf_free(conf);
	LOG_FUNC_RETURN(ctx, res);
}


int
sc_profile_finish(struct sc_profile *profile, const struct sc_app_info *app_info)
{
	struct sc_context *ctx = profile->card->ctx;
	struct file_info *fi;
	struct pin_info	*pi;
	char		reason[64];

	LOG_FUNC_CALLED(ctx);
	profile->mf_info = sc_profile_find_file(profile, NULL, "MF");
	if (!profile->mf_info)
		LOG_TEST_RET(ctx, SC_ERROR_INCONSISTENT_PROFILE, "Profile doesn't define a MF");

	if (app_info && app_info->aid.len)   {
		struct sc_path path;

		sc_log(ctx, "finish profile with '%s' application profile", app_info->label);
		memset(&path, 0, sizeof(struct sc_path));
		path.type = SC_PATH_TYPE_DF_NAME;
		path.aid = app_info->aid;

		sc_log(ctx, "Look for file by path '%s'", sc_print_path(&path));
		profile->df_info = sc_profile_find_file_by_path(profile, &path);
		sc_log(ctx, "returned DF info %p", profile->df_info);
		if (profile->df_info && profile->df_info->profile_extension)   {
			sc_log(ctx, "application profile extension '%s'", profile->df_info->profile_extension);
			if (sc_profile_load(profile, profile->df_info->profile_extension))
				LOG_TEST_RET(ctx, SC_ERROR_INCONSISTENT_PROFILE, "Cannot load application profile extension");
		}
	}

	profile->df_info = sc_profile_find_file(profile, NULL, "PKCS15-AppDF");
	if (!profile->df_info)
		LOG_TEST_RET(ctx, SC_ERROR_INCONSISTENT_PROFILE, "Profile doesn't define a PKCS15-AppDF");

	profile->p15_spec->file_app = profile->df_info->file;
	profile->df_info->dont_free = 1;

	for (pi = profile->pin_list; pi; pi = pi->next) {
		const char	*name;

		set_pin_defaults(profile, pi);
		if (!(name = pi->file_name))
			continue;
		if (!(fi = sc_profile_find_file(profile, NULL, name))) {
			snprintf(reason, sizeof(reason), "unknown PIN file \"%s\"\n", name);
			goto whine;
		}

		pi->file = fi;
	}
	LOG_FUNC_RETURN(ctx, SC_SUCCESS);

whine:
	sc_log(ctx, "%s", reason);
	LOG_FUNC_RETURN(ctx, SC_ERROR_INCONSISTENT_PROFILE);
}

void
sc_profile_free(struct sc_profile *profile)
{
	struct auth_info *ai;
	struct pin_info *pi;
	sc_macro_t	*mi;
	sc_template_t	*ti;

	if (profile->name)
		free(profile->name);

	free_file_list(&profile->ef_list);

	while ((ai = profile->auth_list) != NULL) {
		profile->auth_list = ai->next;
		free(ai);
	}

	while ((ti = profile->template_list) != NULL) {
		profile->template_list = ti->next;
		if (ti->data)
			sc_profile_free(ti->data);
		if (ti->name)
			free(ti->name);
		free(ti);
	}

	while ((mi = profile->macro_list) != NULL) {
		profile->macro_list = mi->next;
		if (mi->name)
			free(mi->name);
		free(mi);
	}

	while ((pi = profile->pin_list) != NULL) {
		profile->pin_list = pi->next;
		if (pi->file_name)
			free(pi->file_name);
		free(pi);
	}

	if (profile->p15_spec)
		sc_pkcs15_card_free(profile->p15_spec);
	memset(profile, 0, sizeof(*profile));
	free(profile);
}

void
sc_profile_get_pin_info(struct sc_profile *profile,
		int id, struct sc_pkcs15_auth_info *info)
{
	struct pin_info	*pi;

	pi = new_pin(profile, id);
	if (pi == NULL)
		return;

	pi->pin.max_tries = pi->pin.tries_left;
	*info = pi->pin;
}

int
sc_profile_get_pin_retries(sc_profile_t *profile, int id)
{
	struct pin_info	*pi;

	pi = new_pin(profile, id);
	if (pi == NULL)
		return SC_ERROR_OUT_OF_MEMORY;
	return pi->pin.tries_left;
}

int
sc_profile_get_pin_id(struct sc_profile *profile,
		unsigned int reference, int *id)
{
	struct pin_info	*pi;

	for (pi = profile->pin_list; pi; pi = pi->next) {
		if (pi->pin.auth_type != SC_PKCS15_PIN_AUTH_TYPE_PIN)
			continue;
		if (pi->pin.attrs.pin.reference == (int)reference) {
			*id = pi->id;
			return 0;
		}

	}
	return SC_ERROR_OBJECT_NOT_FOUND;
}

int
sc_profile_get_file_in(sc_profile_t *profile,
		const sc_path_t *path, const char *name, sc_file_t **ret)
{
	struct file_info *fi;

	if ((fi = sc_profile_find_file(profile, path, name)) == NULL)
		return SC_ERROR_FILE_NOT_FOUND;
	sc_file_dup(ret, fi->file);
	if (*ret == NULL)
		return SC_ERROR_OUT_OF_MEMORY;
	return 0;
}

int
sc_profile_get_file(struct sc_profile *profile,
		const char *name, sc_file_t **ret)
{
	struct file_info *fi;

	if ((fi = sc_profile_find_file(profile, NULL, name)) == NULL)
		return SC_ERROR_FILE_NOT_FOUND;
	sc_file_dup(ret, fi->file);
	if (*ret == NULL)
		return SC_ERROR_OUT_OF_MEMORY;
	return 0;
}

int
sc_profile_get_file_instance(struct sc_profile *profile, const char *name,
		int index, sc_file_t **ret)
{
	struct sc_context *ctx = profile->card->ctx;
	struct file_info *fi;
	struct sc_file *file;
	int r;

	LOG_FUNC_CALLED(ctx);
	sc_log(ctx, "try to get '%s' file instance", name);

	if ((fi = sc_profile_find_file(profile, NULL, name)) == NULL)
		LOG_FUNC_RETURN(ctx, SC_ERROR_FILE_NOT_FOUND);
	sc_file_dup(&file, fi->file);
	sc_log(ctx, "ident '%s'; parent '%s'", fi->ident, fi->parent->ident);
	if (file == NULL)
		LOG_FUNC_RETURN(ctx, SC_ERROR_OUT_OF_MEMORY);
	sc_log(ctx, "file (type:%X, path:'%s')", file->type, sc_print_path(&file->path));

	file->id += index;
        if(file->type == SC_FILE_TYPE_BSO) {
		r = sc_profile_add_file(profile, name, file);
		if (r < 0)
			sc_file_free(file);
		LOG_TEST_RET(ctx, r, "Profile error: cannot add BSO file");
	}
	else if (file->path.len)   {
		file->path.value[file->path.len - 2] = (file->id >> 8) & 0xFF;
		file->path.value[file->path.len - 1] = file->id & 0xFF;

		r = sc_profile_add_file(profile, name, file);
		if (r < 0)
			sc_file_free(file);
		LOG_TEST_RET(ctx, r, "Profile error: cannot add file");
	}

	if (ret)
		*ret = file;
	else
		sc_file_free(file);

	LOG_FUNC_RETURN(ctx, SC_SUCCESS);
}

int
sc_profile_get_path(struct sc_profile *profile,
		const char *name, sc_path_t *ret)
{
	struct file_info *fi;

	if ((fi = sc_profile_find_file(profile, NULL, name)) == NULL)
		return SC_ERROR_FILE_NOT_FOUND;
	*ret = fi->file->path;
	return 0;
}

int
sc_profile_get_file_by_path(struct sc_profile *profile,
		const sc_path_t *path, sc_file_t **ret)
{
	struct sc_context *ctx = profile->card->ctx;
	struct file_info *fi;

	LOG_FUNC_CALLED(ctx);
	if ((fi = sc_profile_find_file_by_path(profile, path)) == NULL)
		LOG_FUNC_RETURN(ctx, SC_ERROR_FILE_NOT_FOUND);
	sc_file_dup(ret, fi->file);
	LOG_FUNC_RETURN(ctx, *ret ? SC_SUCCESS : SC_ERROR_OUT_OF_MEMORY);
}

int
sc_profile_add_file(sc_profile_t *profile, const char *name, sc_file_t *file)
{
	struct sc_context *ctx = profile->card->ctx;
	sc_path_t	path = file->path;
	struct file_info	*parent;

	LOG_FUNC_CALLED(ctx);
	if (!path.len)   {
		parent = profile->df_info;
		        }
        else   {
		path.len -= 2;
		parent = sc_profile_find_file_by_path(profile, &path);
	}
	if (!parent)
		LOG_FUNC_RETURN(ctx, SC_ERROR_FILE_NOT_FOUND);
	sc_log(ctx, "Parent path:%s", sc_print_path(&parent->file->path));

	sc_file_dup(&file, file);
	if (file == NULL)
		LOG_FUNC_RETURN(ctx, SC_ERROR_OUT_OF_MEMORY);

	add_file(profile, name, file, parent);
	LOG_FUNC_RETURN(ctx, SC_SUCCESS);
}

/*
 * Instantiate template
 */
int
sc_profile_instantiate_template(sc_profile_t *profile,
		const char *template_name, const sc_path_t *base_path,
		const char *file_name, const sc_pkcs15_id_t *id,
		sc_file_t **ret)
{
	struct sc_context *ctx = profile->card->ctx;
	struct sc_profile	*tmpl;
	struct sc_template	*info;
	unsigned int	idx;
	struct file_info *fi, *base_file, *match = NULL;

#ifdef DEBUG_PROFILE
	printf("Instantiate %s in template %s\n", file_name, template_name);
	sc_profile_find_file_by_path(profile, base_path);
#endif
	for (info = profile->template_list; info; info = info->next)
		if (!strcmp(info->name, template_name))
			break;
	if (info == NULL)   {
		sc_log(ctx, "Template %s not found", template_name);
		return SC_ERROR_TEMPLATE_NOT_FOUND;
	}

	tmpl = info->data;
	idx = id->value[id->len-1];
	for (fi = profile->ef_list; fi; fi = fi->next) {
		if (fi->base_template == tmpl
		 && fi->inst_index == idx
		 && sc_compare_path(&fi->inst_path, base_path)
		 && !strcmp(fi->ident, file_name)) {
			sc_file_dup(ret, fi->file);
			if (*ret == NULL)
				return SC_ERROR_OUT_OF_MEMORY;
			return 0;
		}
	}

	sc_log(ctx, "Instantiating template %s at %s", template_name, sc_print_path(base_path));

	base_file = sc_profile_find_file_by_path(profile, base_path);
	if (base_file == NULL) {
		sc_log(ctx, "Directory %s not defined in profile", sc_print_path(base_path));
		return SC_ERROR_OBJECT_NOT_FOUND;
	}

	/* This loop relies on the fact that new files are always
	 * appended to the list, after the parent files they refer to
	 */
	assert(base_file->instance);
	for (fi = tmpl->ef_list; fi; fi = fi->next) {
		struct file_info	*parent, *instance;
		unsigned int	skew = 0;

		fi->instance = NULL;
		if ((parent = fi->parent) == NULL) {
			parent = base_file;
			skew = idx;
		}
		parent = parent->instance;

		instance = sc_profile_instantiate_file(profile, fi, parent, skew);
		if (instance == NULL)
			return SC_ERROR_OUT_OF_MEMORY;

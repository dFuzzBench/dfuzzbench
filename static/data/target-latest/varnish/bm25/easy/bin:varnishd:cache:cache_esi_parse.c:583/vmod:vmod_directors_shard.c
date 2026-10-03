/*-
 * Copyright 2009-2018 UPLEX - Nils Goroll Systemoptimierung
 * All rights reserved.
 *
 * Authors: Julian Wiesener <jw@uplex.de>
 *	    Nils Goroll <slink@uplex.de>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "cache/cache.h"
#include "vcl.h"

#include "vend.h"

#include "vcc_directors_if.h"
#include "vmod_directors_shard_dir.h"
#include "vmod_directors_shard_cfg.h"
#include "vsb.h"

/* -------------------------------------------------------------------------
 *  shard director: LAZY mode (vdi resolve function), parameter objects
 *
 *  By associating a parameter object with a shard director, we enable LAZY
 *  lookups as with the other directors. Parameter objects are defined with VCL
 *  scope (normal vmod objects), but can be overridden per backend request using
 *  a task priv.
 *
 *  We use the same concept to carry shard.backend() parameters to vdi resolve
 *  for LAZY mode: They get saved in a per-director task scope parameter object.
 *
 *  Each object points to another object providing defaults for values which are
 *  not defined.
 *
 *  Actual resolution of the various parameter objects does not happen before
 *  they are used, which enables changing them independently (ie, shard
 *  .backend() parameters have precedence over an associated parameter object,
 *  which by itself can be overridden).
 *
 *  Overview of parameter objects (pointers are alternatives)
 *
 *  shard() director        shard_param() object    default param
 *
 *               --------------------------------->   vmod static
 *    VCL obj   /                                ->
 *    .param  -+--------->    VCL obj           /  _
 *                            .default  --------   /|
 *                                                /
 *                               ^               /
 *                               |              /
 *                                             /
 *                            .default        /
 *          ------------->    TASK priv      /
 *         /                                /
 *    .default -----------------------------
 *    TASK priv
 */

/* -------------------------------------------------------------------------
 * method arguments and set parameters bitmask in vmod_directors_shard_param
 */

#define arg_by		((uint32_t)1)
#define arg_key		((uint32_t)1 << 1)
#define arg_key_blob	((uint32_t)1 << 2)
#define arg_alt		((uint32_t)1 << 3)
#define arg_warmup	((uint32_t)1 << 4)
#define arg_rampup	((uint32_t)1 << 5)
#define arg_healthy	((uint32_t)1 << 6)
#define arg_param	((uint32_t)1 << 7)
#define arg_resolve	((uint32_t)1 << 8)
#define arg_mask_	((arg_resolve << 1) - 1)
/* allowed in shard_param.set */
#define arg_mask_set_	(arg_param - 1)
/* allowed in shard_param */
#define arg_mask_param_ ( arg_mask_set_		\
			  & ~arg_key			\
			  & ~arg_key_blob )

/* -------------------------------------------------------------------------
 * shard parameters - declaration & defaults
 */
enum vmod_directors_shard_param_scope {
	_SCOPE_INVALID = 0,
	SCOPE_VMOD,
	SCOPE_VCL,
	SCOPE_TASK,
	SCOPE_STACK
};

struct vmod_directors_shard_param;

#define VMOD_SHARD_SHARD_PARAM_BLOB		0xdf5ca116

struct vmod_directors_shard_param {
	unsigned				magic;
#define VMOD_SHARD_SHARD_PARAM_MAGIC		0xdf5ca117

	/* internals */
	uint32_t				key;
	const char				*vcl_name;
	const struct vmod_directors_shard_param *defaults;
	enum vmod_directors_shard_param_scope	scope;

	/* parameters */
	VCL_ENUM				by;
	VCL_ENUM				healthy;
	uint32_t				mask;
	VCL_BOOL				rampup;
	VCL_INT					alt;
	VCL_REAL				warmup;
};

static const struct vmod_directors_shard_param shard_param_default = {
	.magic		= VMOD_SHARD_SHARD_PARAM_MAGIC,

	.key		= 0,
	.vcl_name	= "builtin defaults",
	.defaults	= NULL,
	.scope		= SCOPE_VMOD,

	.mask		= arg_mask_param_,
	.rampup	= 1,
	.alt		= 0,
	.warmup		= -1,
};

#define default_by(ptr) (ptr == NULL ? VENUM(HASH) : ptr)
#define default_healthy(ptr) (ptr == NULL ? VENUM(CHOSEN) : ptr)

static struct vmod_directors_shard_param *
shard_param_stack(struct vmod_directors_shard_param *p,
    const struct vmod_directors_shard_param *pa, const char *who);

static const struct vmod_directors_shard_param *
shard_param_task_r(VRT_CTX, const void *id, const char *who,
    const struct vmod_directors_shard_param *pa);

static struct vmod_directors_shard_param *
shard_param_task_l(VRT_CTX, const void *id, const char *who,
    const struct vmod_directors_shard_param *pa);

static const struct vmod_directors_shard_param *
shard_param_blob(VCL_BLOB blob);

static const struct vmod_directors_shard_param *
vmod_shard_param_read(VRT_CTX, const void *id, const char *who,
    const struct vmod_directors_shard_param *p,
    struct vmod_directors_shard_param *pstk);

// XXX #3329 #3330 revisit - for now, treat pipe like backend
#define SHARD_VCL_TASK_REQ (VCL_MET_TASK_C & ~VCL_MET_PIPE)
#define SHARD_VCL_TASK_BEREQ (VCL_MET_TASK_B | VCL_MET_PIPE)
/* -------------------------------------------------------------------------
 * shard vmod interface
 */
static vdi_healthy_f vmod_shard_healthy;
static vdi_resolve_f vmod_shard_resolve;
static vdi_list_f vmod_shard_list;

struct vmod_directors_shard {
	unsigned				magic;
#define VMOD_SHARD_SHARD_MAGIC			0x6e63e1bf
	struct sharddir				*shardd;
	VCL_BACKEND				dir;
};

static void
shard__assert(void)
{
	VCL_INT t1;
	uint32_t t2a, t2b;

	/* we put our uint32 key in a VCL_INT container */
	assert(sizeof(VCL_INT) >= sizeof(uint32_t));
	t2a = UINT32_MAX;
	t1 = (VCL_INT)t2a;
	t2b = (uint32_t)t1;
	assert(t2a == t2b);
}

static void v_matchproto_(vdi_release_f)
vmod_shard_release(VCL_BACKEND dir)
{
	struct sharddir *shardd;

	CAST_OBJ_NOTNULL(shardd, dir->priv, SHARDDIR_MAGIC);
	sharddir_release(shardd);
}

static void v_matchproto_(vdi_destroy_f)
vmod_shard_destroy(VCL_BACKEND dir)
{
	struct sharddir *shardd;

	CAST_OBJ_NOTNULL(shardd, dir->priv, SHARDDIR_MAGIC);
	sharddir_delete(&shardd);
}

static const struct vdi_methods vmod_shard_methods[1] = {{
	.magic =	VDI_METHODS_MAGIC,
	.type =		"shard",
	.resolve =	vmod_shard_resolve,
	.healthy =	vmod_shard_healthy,
	.release =	vmod_shard_release,
	.destroy =	vmod_shard_destroy,
	.list =		vmod_shard_list
}};


VCL_VOID v_matchproto_(td_directors_shard__init)
vmod_shard__init(VRT_CTX, struct vmod_directors_shard **vshardp,
    const char *vcl_name)
{
	struct vmod_directors_shard *vshard;

	shard__assert();

	AN(vshardp);
	AZ(*vshardp);
	ALLOC_OBJ(vshard, VMOD_SHARD_SHARD_MAGIC);
	AN(vshard);

	*vshardp = vshard;
	sharddir_new(&vshard->shardd, vcl_name, &shard_param_default);

	vshard->dir = VRT_AddDirector(ctx, vmod_shard_methods, vshard->shardd,
	    "%s", vcl_name);
}

VCL_VOID v_matchproto_(td_directors_shard__fini)
vmod_shard__fini(struct vmod_directors_shard **vshardp)
{
	struct vmod_directors_shard *vshard;

	TAKE_OBJ_NOTNULL(vshard, vshardp, VMOD_SHARD_SHARD_MAGIC);
	VRT_DelDirector(&vshard->dir);
	FREE_OBJ(vshard);
}

VCL_INT v_matchproto_(td_directors_shard_key)
vmod_shard_key(VRT_CTX, struct vmod_directors_shard *vshard, VCL_STRANDS s)
{

	(void)ctx;
	(void)vshard;

	return ((VCL_INT)VRT_HashStrands32(s));
}

VCL_VOID v_matchproto_(td_directors_set_warmup)
vmod_shard_set_warmup(VRT_CTX, struct vmod_directors_shard *vshard,
    VCL_REAL probability)
{
	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);
	if (probability < 0 || probability >= 1) {
		shard_notice(ctx->vsl, vshard->shardd->name,
		    ".set_warmup(%f) ignored", probability);
		return;
	}
	shardcfg_set_warmup(vshard->shardd, probability);
}

VCL_VOID v_matchproto_(td_directors_set_rampup)
vmod_shard_set_rampup(VRT_CTX, struct vmod_directors_shard *vshard,
    VCL_DURATION duration)
{
	(void)ctx;
	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);
	shardcfg_set_rampup(vshard->shardd, duration);
}

VCL_VOID v_matchproto_(td_directors_shard_associate)
vmod_shard_associate(VRT_CTX,
    struct vmod_directors_shard *vshard, VCL_BLOB b)
{
	const struct vmod_directors_shard_param *ppt;
	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);

	if (b == NULL) {
		sharddir_set_param(vshard->shardd, &shard_param_default);
		return;
	}

	ppt = shard_param_blob(b);

	if (ppt == NULL) {
		shard_fail(ctx, vshard->shardd->name, "%s",
		    "shard .associate param invalid");
		return;
	}

	sharddir_set_param(vshard->shardd, ppt);
}

VCL_BOOL v_matchproto_(td_directors_shard_add_backend)
vmod_shard_add_backend(VRT_CTX, struct vmod_directors_shard *vshard,
    struct VARGS(shard_add_backend) *args)
{
	VCL_REAL weight = 1;

	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);

	if (args->backend == NULL) {
		shard_fail(ctx, vshard->shardd->name, "%s",
		    "None backend cannot be added");
		return (0);
	}

	if (args->valid_weight) {
		if (args->weight >= 1)
			weight = args->weight;
		else
			shard_notice(ctx->vsl, vshard->shardd->name,
			    ".add_backend(weight=%f) ignored", args->weight);
	}

	return (shardcfg_add_backend(ctx, vshard->shardd, args->backend,
	    args->valid_ident ? args->ident : NULL,
	    args->valid_rampup ? args->rampup : nan(""),
	    weight));
}

VCL_BOOL v_matchproto_(td_directors_shard_remove_backend)
vmod_shard_remove_backend(VRT_CTX, struct vmod_directors_shard *vshard,
    struct VARGS(shard_remove_backend) *args)
{
	VCL_BACKEND be = args->valid_backend ? args->backend : NULL;
	VCL_STRING ident = args->valid_ident ? args->ident : NULL;

	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);

	if (be == NULL && ident == NULL) {
		shard_fail(ctx, vshard->shardd->name, "%s",
		    ".remove_backend(): either backend or ident are required");
		return (0);
	}

	return (shardcfg_remove_backend(ctx, vshard->shardd, be, ident));
}

VCL_BOOL v_matchproto_(td_directors_shard_clear)
vmod_shard_clear(VRT_CTX, struct vmod_directors_shard *vshard)
{
	CHECK_OBJ_NOTNULL(vshard, VMOD_SHARD_SHARD_MAGIC);
	return (shardcfg_clear(ctx, vshard->shardd));
}

VCL_BOOL v_matchproto_(td_directors_shard_reconfigure)
vmod_shard_reconfigure(VRT_CTX, struct vmod_directors_shard *vshard,
    VCL_INT replicas)
{
	return (shardcfg_reconfigure(ctx, vshard->shardd, replicas));
}

static inline uint32_t
shard_get_key(VRT_CTX, const struct vmod_directors_shard_param *p)
{
	struct http *http;
	VCL_ENUM by = default_by(p->by);

	if (by == VENUM(KEY) || by == VENUM(BLOB))
		return (p->key);
	if (by == VENUM(HASH) && ctx->bo != NULL) {
		CHECK_OBJ(ctx->bo, BUSYOBJ_MAGIC);
		return (vbe32dec(ctx->bo->digest));
	}
	if (by == VENUM(HASH) || by == VENUM(URL)) {
		if (ctx->http_req) {
			AN(http = ctx->http_req);
		} else {
			AN(ctx->http_bereq);
			AN(http = ctx->http_bereq);
		}
		return (VRT_HashStrands32(TOSTRAND(http->hd[HTTP_HDR_URL].b)));
	}
	WRONG("by enum");
}

/*
 * merge parameters to resolve all undef values
 * key is to be calculated after merging
 */
static void
shard_param_merge(struct vmod_directors_shard_param *to,
		  const struct vmod_directors_shard_param *from)
{
	CHECK_OBJ_NOTNULL(to, VMOD_SHARD_SHARD_PARAM_MAGIC);
	assert((to->mask & ~arg_mask_param_) == 0);

	if (to->mask == arg_mask_param_)
		return;

	CHECK_OBJ_NOTNULL(from, VMOD_SHARD_SHARD_PARAM_MAGIC);
	assert((from->mask & ~arg_mask_param_) == 
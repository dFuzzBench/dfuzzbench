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
    const struct vmod_directors_shard_param *pa, const
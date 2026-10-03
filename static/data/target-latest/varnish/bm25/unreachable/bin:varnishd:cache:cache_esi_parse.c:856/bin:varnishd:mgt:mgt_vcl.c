/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 *
 * VCL management stuff
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mgt/mgt.h"
#include "mgt/mgt_vcl.h"
#include "common/heritage.h"

#include "vcli_serve.h"
#include "vct.h"
#include "vev.h"
#include "vte.h"
#include "vtim.h"

struct vclstate {
	const char		*name;
};

#define VCL_STATE(sym, str)						\
	static const struct vclstate VCL_STATE_ ## sym[1] = {{ str }};
#include "tbl/vcl_states.h"

static const struct vclstate VCL_STATE_LABEL[1] = {{ "label" }};

static unsigned vcl_count;

struct vclproghead vclhead = VTAILQ_HEAD_INITIALIZER(vclhead);
static struct vclproghead discardhead = VTAILQ_HEAD_INITIALIZER(discardhead);
struct vmodfilehead vmodhead = VTAILQ_HEAD_INITIALIZER(vmodhead);
static struct vclprog *mgt_vcl_active;
static struct vev *e_poker;

static int mgt_vcl_setstate(struct cli *, struct vclprog *,
    const struct vclstate *);
static int mgt_vcl_settemp(struct cli *, struct vclprog *, unsigned);
static int mgt_vcl_askchild(struct cli *, struct vclprog *, unsigned);
static void mgt_vcl_set_cooldown(struct vclprog *, vtim_mono);

/*--------------------------------------------------------------------*/

static const struct vclstate *
mcf_vcl_parse_state(struct cli *cli, const char *s)
{
	if (s != NULL) {
#define VCL_STATE(sym, str)				\
		if (!strcmp(s, str))			\
			return (VCL_STATE_ ## sym);
#include "tbl/vcl_states.h"
	}
	VCLI_Out(cli, "State must be one of auto, cold or warm.");
	VCLI_SetResult(cli, CLIS_PARAM);
	return (NULL);
}

struct vclprog *
mcf_vcl_byname(const char *name)
{
	struct vclprog *vp;

	VTAILQ_FOREACH(vp, &vclhead, list)
		if (!strcmp(name, vp->name))
			return (vp);
	return (NULL);
}

static int
mcf_invalid_vclname(struct cli *cli, const char *name)
{
	const char *bad;

	AN(name);
	bad = VCT_invalid_name(name, NULL);

	if (bad != NULL) {
		VCLI_SetResult(cli, CLIS_PARAM);
		VCLI_Out(cli, "Illegal character in VCL name ");
		if (*bad > 0x20 && *bad < 0x7f)
			VCLI_Out(cli, "('%c')", *bad);
		else
			VCLI_Out(cli, "(0x%02x)", *bad & 0xff);
		return (-1);
	}
	return (0);
}

static struct vclprog *
mcf_find_vcl(struct cli *cli, const char *name)
{
	struct vclprog *vp;

	if (mcf_invalid_vclname(cli, name))
		return (NULL);

	vp = mcf_vcl_byname(name);
	if (vp == NULL) {
		VCLI_SetResult(cli, CLIS_PARAM);
		VCLI_Out(cli, "No VCL named %s known\n", name);
	}
	return (vp);
}

static int
mcf_find_no_vcl(struct cli *cli, const char *name)
{

	if (mcf_invalid_vclname(cli, name))
		return (0);

	if (mcf_vcl_byname(name) != NULL) {
		VCLI_SetResult(cli, CLIS_PARAM);
		VCLI_Out(cli, "Already a VCL named %s", name);
		return (0);
	}
	return (1);
}

int
mcf_is_label(const struct vclprog *vp)
{
	return (vp->state == VCL_STATE_LABEL);
}

/*--------------------------------------------------------------------*/

struct vcldep *
mgt_vcl_dep_add(struct vclprog *vp_from, struct vclprog *vp_to)
{
	struct vcldep *vd;

	CHECK_OBJ_NOTNULL(vp_from, VCLPROG_MAGIC);
	CHECK_OBJ_NOTNULL(vp_to, VCLPROG_MAGIC);
	assert(vp_to->state != VCL_STATE_COLD);

	ALLOC_OBJ(vd, VCLDEP_MAGIC);
	AN(vd);

	mgt_vcl_set_cooldown(vp_from, -1);
	mgt_vcl_set_cooldown(vp_to, -1);

	vd->from = vp_from;
	VTAILQ_INSERT_TAIL(&vp_from->dfrom, vd, lfrom);
	vd->to = vp_to;
	VTAILQ_INSERT_TAIL(&vp_to->dto, vd, lto);
	vp_to->nto++;
	return (vd);
}

static void
mgt_vcl_dep_del(struct vcldep *vd)
{

	CHECK_OBJ_NOTNULL(vd, VCLDEP_MAGIC);
	VTAILQ_REMOVE(&vd->from->dfrom, vd, lfrom);
	VTAILQ_REMOVE(&vd->to->dto, vd, lto);
	vd->to->nto--;
	if (vd->to->nto == 0)
		mgt_vcl_set_cooldown(vd->to, VTIM_mono());
	FREE_OBJ(vd);
}

/*--------------------------------------------------------------------*/

static struct vclprog *
mgt_vcl_add(const char *name, const struct vclstate *state)
{
	struct vclprog *vp;

	assert(state == VCL_STATE_WARM ||
	       state == VCL_STATE_COLD ||
	       state == VCL_STATE_AUTO ||
	       state == VCL_STATE_LABEL);
	ALLOC_OBJ(vp, VCLPROG_MAGIC);
	XXXAN(vp);
	REPLACE(vp->name, name);
	VTAILQ_INIT(&vp->dfrom);
	VTAILQ_INIT(&vp->dto);
	VTAILQ_INIT(&vp->vmods);
	vp->state = state;

	if (vp->state != VCL_STATE_COLD)
		vp->warm = 1;

	VTAILQ_INSERT_TAIL(&vclhead, vp, list);
	if (vp->state != VCL_STATE_LABEL)
		vcl_count++;
	return (vp);
}

static void
mgt_vcl_del(struct vclprog *vp)
{
	char *p;
	struct vmoddep *vd;
	struct vmodfile *vf;
	struct vcldep *dep;

	CHECK_OBJ_NOTNULL(vp, VCLPROG_MAGIC);
	assert(VTAILQ_EMPTY(&vp->dto));

	mgt_vcl_symtab_clean(vp);

	while ((dep = VTAILQ_FIRST(&vp->dfrom)) != NULL) {
		assert(dep->from == vp);
		mgt_vcl_dep_del(dep);
	}

	VTAILQ_REMOVE(&vclhead, vp, list);
	if (vp->state != VCL_STATE_LABEL)
		vcl_count--;
	if (vp->fname != NULL) {
		if (!MGT_DO_DEBUG(DBG_VCL_KEEP))
			AZ(unlink(vp->fname));
		p = strrchr(vp->fname, '/');
		AN(p);
		*p = '\0';
		VJ_master(JAIL_MASTER_FILE);
		/*
		 * This will fail if any files are dropped next to the library
		 * without us knowing.  This happens for instance with GCOV.
		 * Assume developers know how to clean up after themselves
		 * (or alternatively:  How to run out of disk space).
		 */
		(void)rmdir(vp->fname);
		VJ_master(JAIL_MASTER_LOW);
		free(vp->fname);
	}
	while (!VTAILQ_EMPTY(&vp->vmods)) {
		vd = VTAILQ_FIRST(&vp->vmods);
		CHECK_OBJ(vd, VMODDEP_MAGIC);
		vf = vd->to;
		CHECK_OBJ(vf, VMODFILE_MAGIC);
		VTAILQ_REMOVE(&vp->vmods, vd, lfrom);
		VTAILQ_REMOVE(&vf->vcls, vd, lto);
		FREE_OBJ(vd);

		if (VTAILQ_EMPTY(&vf->vcls)) {
			if (!MGT_DO_DEBUG(DBG_VMOD_SO_KEEP))
				AZ(unlink(vf->fname));
			VTAILQ_REMOVE(&vmodhead, vf, list);
			free(vf->fname);
			FREE_OBJ(vf);
		}
	}
	free(vp->name);
	FREE_OBJ(vp);
}

const char *
mgt_has_vcl(void)
{
	if (VTAILQ_EMPTY(&vclhead))
		return ("No VCL loaded");
	if (mgt_vcl_active == NULL)
		return ("No active VCL");
	CHECK_OBJ_NOTNULL(mgt_vcl_active, VCLPROG_MAGIC);
	AN(mgt_vcl_active->warm);
	return (NULL);
}

/*
 * go_cold
 *
 * -1: leave alone
 *  0: timer not started - not currently used
 * >0: when timer started
 */
static void
mgt_vcl_set_cooldown(struct vclprog *vp, vtim_mono now)
{
	CHECK_OBJ_NOTNULL(vp, VCLPROG_MAGIC);

	if (vp == mgt_vcl_active ||
	    vp->state != VCL_STATE_AUTO ||
	    vp->warm == 0 ||
	    !VTAILQ_EMPTY(&vp->dto) ||
	    !VTAILQ_EMPTY(&vp->dfrom))
		vp->go_cold = -1;
	else
		vp->go_cold = now;
}

static int
mgt_vcl_settemp(struct cli *cli, struct vclprog *vp, unsigned warm)
{
	int i;

	CHECK_OBJ_NOTNULL(vp, VCLPROG_MAGIC);

	if (warm == vp->warm)
		return (0);

	if (vp->state == VCL_STATE_AUTO || vp->state == VCL_STATE_LABEL) {
		mgt_vcl_set_cooldown(vp, -1);
		i = mgt_vcl_askchild(cli, vp, warm);
		mgt_vcl_set_cooldown(vp, VTIM_mono());
	} else {
		i = mgt_vcl_setstate(cli, vp,
		    warm ? VCL_STATE_WARM : VCL_STATE_COLD);
	}

	return (i);
}

static int
mgt_vcl_requirewarm(struct cli *cli, struct vclprog *vp)
{
	if (vp->state == VCL_STATE_COLD) {
		VCLI_SetResult(cli, CLIS_CANT);
		VCLI_Out(cli, "VCL '%s' is cold - set to auto or warm first",
		    vp->name);
		return (1);
	}
	return (mgt_vcl_settemp(cli, vp, 1));
}

static int
mgt_vcl_askchild(struct cli *cli, struct vclprog *vp, unsigned warm)
{
	unsigned status;
	char *p;
	int i;

	CHECK_OBJ_NOTNULL(vp, VCLPROG_MAGIC);

	if (!MCH_Running()) {
		vp->warm = warm;
		return (0);
	}

	i = mgt_cli_askchild(&status, &p, "vcl.state %s %d%s\n",
	    vp->name, warm, vp->state->name);
	if (i && cli != NULL) {
		VCLI_SetResult(cli, status);
		VCLI_Out(cli, "%s", p);
	} else if (i) {
		MGT_Complain(C_ERR,
		    "Please file ticket: VCL poker problem: "
		    "'vcl.state %s %d%s' -> %03d '%s'",
		    vp->name, warm, vp->state->name, i, p);
	} else {
		/* Success, update mgt's VCL state to reflect child's
		   state */
		vp->warm = warm;
	}

	free(p);
	return (i);
}

static int
mgt_vcl_setstate(struct cli *cli, struct vclprog *vp, const struct vclstate *vs)
{
	unsigned warm;
	int i;
	const struct vclstate *os;

	CHECK_OBJ_NOTNULL(vp, VCLPROG_MAGIC);

	assert(vs != VCL_STATE_LABEL);

	if (mcf_is_label(vp)) {
		AN(vp->warm);
		/* do not touch labels */
		return (0);
	}

	if (vp->state == vs)
		return (0);

	os = vp->state;
	vp->state = vs;

	if (vp == mgt_vcl_active) {
		assert (vs == VCL_STATE_WARM || vs == VCL_STATE_AUTO);
		AN(vp->warm);
		warm = 1;
	} else if (vs == VCL_STATE_AUTO) {
		warm = vp->warm;
	} else {
		warm = (vs == VCL_STATE_WARM ? 1 : 0);
	}

	i = mgt_vcl_askchild(cli, vp, warm);
	if (i == 0)
		mgt_vcl_set_cooldown(vp, VTIM_mono());
	else
		vp->state = os;
	return (i);
}

/*--------------------------------------------------------------------*/

static struct vclprog *
mgt_new_vcl(struct cli *cli, const char *vclname, const char *vclsrc,
    const char *vclsrcfile, const char *state, int C_flag)
{
	unsigned status;
	char *lib, *p;
	struct vclprog *vp;
	const struct vclstate *vs;

	AN(cli);

	if (vcl_count >= mgt_param.max_vcl &&
	    mgt_param.max_vcl_handling == 2) {
		VCLI_Out(cli, "Too many (%d) VCLs already loaded\n", vcl_count);
		VCLI_Out(cli, "(See max_vcl and max_vcl_handling parameters)");
		VCLI_SetResult(cli, CLIS_CANT);
		return (NULL);
	}

	if (state == NULL)
		vs = VCL_STATE_AUTO;
	else
		vs = mcf_vcl_parse_state(cli, state);

	if (vs == NULL)
		return (NULL);

	vp = mgt_vcl_add(vclname, vs);
	lib = mgt_VccCompile(cli, vp, vclname, vclsrc, vclsrcfile, C_flag);
	if
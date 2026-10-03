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
 */

#include "config.h"

#include "cache_varnishd.h"
#include "cache_filter.h"
#include "cache_objhead.h"
#include "storage/storage.h"
#include "vcl.h"
#include "vtim.h"
#include "vcc_interface.h"

#define FETCH_STEPS \
	FETCH_STEP(mkbereq,           MKBEREQ) \
	FETCH_STEP(retry,             RETRY) \
	FETCH_STEP(startfetch,        STARTFETCH) \
	FETCH_STEP(condfetch,         CONDFETCH) \
	FETCH_STEP(fetch,             FETCH) \
	FETCH_STEP(fetchbody,         FETCHBODY) \
	FETCH_STEP(fetchend,          FETCHEND) \
	FETCH_STEP(error,             ERROR) \
	FETCH_STEP(fail,              FAIL) \
	FETCH_STEP(done,              DONE)

typedef const struct fetch_step *vbf_state_f(struct worker *, struct busyobj *);

struct fetch_step {
	const char	*name;
	vbf_state_f	*func;
};

#define FETCH_STEP(l, U) \
    static vbf_state_f vbf_stp_##l; \
    static const struct fetch_step F_STP_##U[1] = {{ .name = "Fetch Step " #l, .func = vbf_stp_##l, }};
FETCH_STEPS
#undef FETCH_STEP

static hdr_t const H_X_Varnish = HDR("X-Varnish");

/*--------------------------------------------------------------------
 * Allocate an object, with fall-back to Transient.
 * XXX: This somewhat overlaps the stuff in stevedore.c
 * XXX: Should this be merged over there ?
 */

static int
vbf_allocobj(struct busyobj *bo, unsigned l)
{
	struct objcore *oc;
	const struct stevedore *stv;
	vtim_dur lifetime;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	oc = bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	lifetime = oc->ttl + oc->grace + oc->keep;

	if (bo->uncacheable) {
		stv = stv_transient;
		bo->wrk->stats->beresp_uncacheable++;
	}
	else if (lifetime < cache_param->shortlived) {
		stv = stv_transient;
		bo->wrk->stats->beresp_shortlived++;
	}
	else
		stv = bo->storage;

	bo->storage = NULL;

	if (stv == NULL)
		return (0);

	if (STV_NewObject(bo->wrk, oc, stv, l))
		return (1);

	if (stv == stv_transient)
		return (0);

	/*
	 * Try to salvage the transaction by allocating a shortlived object
	 * on Transient storage.
	 */

	oc->ttl = vmin_t(float, oc->ttl, cache_param->shortlived);
	oc->grace = 0.0;
	oc->keep = 0.0;
	return (STV_NewObject(bo->wrk, oc, stv_transient, l));
}

static void
vbf_cleanup(struct busyobj *bo)
{
	struct vfp_ctx *vfc;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	vfc = bo->vfc;
	CHECK_OBJ_NOTNULL(vfc, VFP_CTX_MAGIC);

	bo->acct.beresp_bodybytes += VFP_Close(vfc);
	bo->vfp_filter_list = NULL;

	if (bo->director_state != DIR_S_NULL)
		VDI_Finish(bo);
}

void
Bereq_Rollback(VRT_CTX)
{
	struct busyobj *bo;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	bo = ctx->bo;
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	if (bo->htc != NULL) {
		assert(bo->htc->body_status != BS_TAKEN);
		if (bo->htc->body_status != BS_NONE)
			bo->htc->doclose = SC_RESP_CLOSE;
	}

	vbf_cleanup(bo);
	VCL_TaskLeave(ctx, bo->privs);
	VCL_TaskEnter(bo->privs);
	HTTP_Clone(bo->bereq, bo->bereq0);
	bo->vfp_filter_list = NULL;
	bo->err_reason = NULL;
	AN(bo->ws_bo);
	WS_Rollback(bo->ws, bo->ws_bo);
}

/*--------------------------------------------------------------------
 * Turn the beresp into a obj
 */

static int
vbf_beresp2obj(struct busyobj *bo)
{
	unsigned l, l2;
	const char *b;
	uint8_t *bp;
	struct vsb *vary = NULL;
	int varyl = 0;
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	oc = bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	l = 0;

	/* Create Vary instructions */
	if (!(oc->flags & OC_F_PRIVATE)) {
		varyl = VRY_Create(bo, &vary);
		if (varyl > 0) {
			AN(vary);
			assert(varyl == VSB_len(vary));
			l += PRNDUP((intptr_t)varyl);
		} else if (varyl < 0) {
			/*
			 * Vary parse error
			 * Complain about it, and make this a pass.
			 */
			VSLb(bo->vsl, SLT_Error,
			    "Illegal 'Vary' header from backend, "
			    "making this a pass.");
			bo->uncacheable = 1;
			AZ(vary);
		} else
			/* No vary */
			AZ(vary);
	}

	l2 = http_EstimateWS(bo->beresp,
	    bo->uncacheable ? HTTPH_A_PASS : HTTPH_A_INS);
	l += l2;

	if (bo->uncacheable)
		oc->flags |= OC_F_HFM;

	if (!vbf_allocobj(bo, l)) {
		if (vary != NULL)
			VSB_destroy(&vary);
		AZ(vary);
		return (VFP_Error(bo->vfc, "Could not get storage"));
	}

	if (vary != NULL) {
		AN(ObjSetAttr(bo->wrk, oc, OA_VARY, varyl, VSB_data(vary)));
		VSB_destroy(&vary);
	}

	AZ(ObjSetXID(bo->wrk, oc, bo->vsl->wid));

	/* for HTTP_Encode() VSLH call */
	bo->beresp->logtag = SLT_ObjMethod;

	/* Filter into object */
	bp = ObjSetAttr(bo->wrk, oc, OA_HEADERS, l2, NULL);
	AN(bp);
	HTTP_Encode(bo->beresp, bp, l2,
	    bo->uncacheable ? HTTPH_A_PASS : HTTPH_A_INS);

	if (http_GetHdr(bo->beresp, H_Last_Modified, &b))
		AZ(ObjSetDouble(bo->wrk, oc, OA_LASTMODIFIED, VTIM_parse(b)));
	else
		AZ(ObjSetDouble(bo->wrk, oc, OA_LASTMODIFIED,
		    floor(oc->t_origin)));

	return (0);
}

/*--------------------------------------------------------------------
 * Copy req->bereq and release req if no body
 */

static const struct fetch_step * v_matchproto_(vbf_state_f)
vbf_stp_mkbereq(struct worker *wrk, struct busyobj *bo)
{
	const char *q;
	struct objcore *oc;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
	CHECK_OBJ_NOTNULL(bo->req, REQ_MAGIC);
	oc = bo->fetch_objcore;
	CHECK_OBJ_NOTNULL(oc, OBJCORE_MAGIC);

	assert(oc->boc->state == BOS_INVALID);
	AZ(bo->storage);

	HTTP_Setup(bo->bereq0, bo->ws, bo->vsl, SLT_BereqMethod);
	http_FilterReq(bo->bereq0, bo->req->http,
	    bo->uncacheable ? HTTPH_R_PASS : HTTPH_R_FETCH);

	if (bo->uncacheable)
		AZ(bo->stale_oc);
	else {
		http_ForceField(bo->bereq0, HTTP_HDR_METHOD, "GET");
		if (cache_param->http_gzip_support)
			http_ForceHeader(bo->bereq0, H_Accept_Encoding, "gzip");
	}
	http_ForceField(bo->bereq0, HTTP_HDR_PROTO, "HTTP/1.1");

	if (bo->stale_oc != NULL &&
	    ObjCheckFlag(bo->wrk, bo->stale_oc, OF_IMSCAND) &&
	    (bo->stale_oc->boc != NULL || ObjGetLen(wrk, bo->stale_oc) != 0)) {
		AZ(bo->stale_oc->flags & (OC_F_HFM|OC_F_PRIVATE));
		q = RFC2616_Strong_LM(NULL, wrk, bo->stale_oc);
		if (q != NULL)
			http_PrintfHeader(bo->bereq0,
			    "If-Modified-Since: %s", q);
		q = HTTP_GetHdrPack(bo->wrk, bo->stale_oc, H_ETag);
		if (q != NULL)
			http_PrintfHeader(bo->bereq0,
			    "If-None-Match: %s", q);
	}

	http_CopyHome(bo->bereq0);
	HTTP_Setup(bo->bereq, bo->ws, bo->vsl, SLT_BereqMethod);
	bo->ws_bo = WS_Snapshot(bo->ws);
	HTTP_Clone(bo->bereq, bo->bereq0);

	if (bo->req->req_body_status->avail == 0) {
		VBO_SetState(bo->wrk, bo, BOS_REQ_DONE);
	} else if (bo->req->req_body_status == BS_CACHED) {
		AN(bo->req->body_oc);
		bo->bereq_body = bo->req->body_oc;
		HSH_Ref(bo->bereq_body);
		VBO_SetState(bo->wrk, bo, BOS_REQ_DONE);
	}
	return (F_STP_STARTFETCH);
}

/*--------------------------------------------------------------------
 * Start a new VSL transaction and try again
 * Prepare the busyobj and fetch processors
 */

static const struct fetch_step * v_matchproto_(vbf_state_f)
v
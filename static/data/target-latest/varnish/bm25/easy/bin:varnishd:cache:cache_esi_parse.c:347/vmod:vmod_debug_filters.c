/*-
 * Copyright (c) 2012-2019 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@FreeBSD.org>
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
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cache/cache_varnishd.h"
#include "cache/cache_filter.h"

#include "vgz.h"
#include "vsha256.h"
#include "vtim.h"
#include "vcc_debug_if.h"

#include "vmod_debug.h"

/**********************************************************************/

static enum vfp_status v_matchproto_(vfp_pull_f)
xyzzy_vfp_rot13_pull(struct vfp_ctx *vc, struct vfp_entry *vfe, void *p,
    ssize_t *lp)
{
	enum vfp_status vp;
	char *q;
	ssize_t l;

	(void)vfe;
	vp = VFP_Suck(vc, p, lp);
	if (vp == VFP_ERROR)
		return (vp);
	q = p;
	for (l = 0; l < *lp; l++, q++) {
		if (*q >= 'A' && *q <= 'Z')
			*q = (((*q - 'A') + 13) % 26) + 'A';
		if (*q >= 'a' && *q <= 'z')
			*q = (((*q - 'a') + 13) % 26) + 'a';
	}
	return (vp);
}

static const struct vfp xyzzy_vfp_rot13 = {
	.name = "rot13",
	.pull = xyzzy_vfp_rot13_pull,
};

/**********************************************************************/

// deliberately fragmenting the stream to make testing more interesting
#define ROT13_BUFSZ 8

static int v_matchproto_(vdp_init_f)
xyzzy_vdp_rot13_init(VRT_CTX, struct vdp_ctx *vdc, void **priv)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	CHECK_OBJ_ORNULL(vdc->oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(vdc->hp, HTTP_MAGIC);
	AN(vdc->clen);

	AN(priv);

	*priv = malloc(ROT13_BUFSZ);
	if (*priv == NULL)
		return (-1);

	return (0);
}

static int v_matchproto_(vdp_bytes_f)
xyzzy_vdp_rot13_bytes(struct vdp_ctx *vdc, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	char *q;
	const char *pp;
	int i, j, retval = 0;

	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	AN(priv);
	AN(*priv);
	if (len <= 0)
		return (VDP_bytes(vdc, act, ptr, len));
	AN(ptr);
	if (act != VDP_END)
		act = VDP_FLUSH;
	q = *priv;
	pp = ptr;

	for (i = 0, j = 0; j < len; i++, j++) {
		if (pp[j] >= 'A' && pp[j] <= 'Z')
			q[i] = (((pp[j] - 'A') + 13) % 26) + 'A';
		else if (pp[j] >= 'a' && pp[j] <= 'z')
			q[i] = (((pp[j] - 'a') + 13) % 26) + 'a';
		else
			q[i] = pp[j];
		if (i == ROT13_BUFSZ - 1 && j < len - 1) {
			retval = VDP_bytes(vdc, VDP_FLUSH, q, ROT13_BUFSZ);
			if (retval != 0)
				return (retval);
			i = -1;
		}
	}
	if (i >= 0)
		retval = VDP_bytes(vdc, act, q, i);
	return (retval);
}

static int v_matchproto_(vdp_fini_f)
xyzzy_vdp_rot13_fini(struct vdp_ctx *vdc, void **priv)
{
	(void)vdc;
	AN(priv);
	free(*priv);
	*priv = NULL;
	return (0);
}

static const struct vdp xyzzy_vdp_rot13 = {
	.name  = "rot13",
	.init  = xyzzy_vdp_rot13_init,
	.bytes = xyzzy_vdp_rot13_bytes,
	.fini  = xyzzy_vdp_rot13_fini,
};

VCL_VOID v_matchproto_(td_debug_rot104)
xyzzy_rot104(VRT_CTX)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	// This should fail
	AN(VRT_AddFilter(ctx, &xyzzy_vfp_rot13, &xyzzy_vdp_rot13));
}

/**********************************************************************
 * vdp debug_chunked: force http1 chunked encoding by removing the
 * Content-Length header
 *
 * this happens in a VDP because cnt_transmit() runs after VCL and
 * restores it
 */

static int v_matchproto_(vdp_init_f)
xyzzy_vdp_chunked_init(VRT_CTX, struct vdp_ctx *vdc, void **priv)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	CHECK_OBJ_ORNULL(vdc->oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(vdc->hp, HTTP_MAGIC);
	AN(vdc->clen);
	AN(priv);

	http_Unset(vdc->hp, H_Content_Length);
	*vdc->clen = -1;

	return (1);
}

static const struct vdp xyzzy_vdp_chunked = {
	.name  = "debug.chunked",
	.init  = xyzzy_vdp_chunked_init,
};

/**********************************************************************
 * pedantic tests of the VDP API:
 * - assert that we see a VDP_END
 * - assert that _fini gets called before the task ends
 *
 * note:
 * we could lookup our own vdpe in _fini and check for vdpe->end == VDP_END
 * yet that would cross the API
 */

enum vdp_state_e {
	VDPS_NULL = 0,
	VDPS_INIT,	// _init called
	VDPS_BYTES,	// _bytes called act != VDP_END
	VDPS_END,	// _bytes called act == VDP_END
	VDPS_FINI	// _fini called
};

struct vdp_state_s {
	unsigned		magic;
#define VDP_STATE_MAGIC	0x57c8d309
	enum vdp_state_e	state;
};

static void v_matchproto_(vmod_priv_fini_f)
priv_pedantic_fini(VRT_CTX, void *priv)
{
	struct vdp_state_s *vdps;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CAST_OBJ_NOTNULL(vdps, priv, VDP_STATE_MAGIC);

	assert(vdps->state == VDPS_FINI);
}

static const struct vmod_priv_methods priv_pedantic_methods[1] = {{
	.magic = VMOD_PRIV_METHODS_MAGIC,
	.type = "debug_vdp_pedantic",
	.fini = priv_pedantic_fini
}};

static int v_matchproto_(vdp_init_f)
xyzzy_pedantic_init(VRT_CTX, struct vdp_ctx *vdc, void **priv)
{
	struct vdp_state_s *vdps;
	struct vmod_priv *p;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	CHECK_OBJ_ORNULL(vdc->oc, OBJCORE_MAGIC);
	CHECK_OBJ_NOTNULL(vdc->hp, HTTP_MAGIC);
	AN(vdc->clen);
	AN(priv);

	WS_TASK_ALLOC_OBJ(ctx, vdps, VDP_STATE_MAGIC);
	if (vdps == NULL)
		return (-1);
	assert(vdps->state == VDPS_NULL);

	p = VRT_priv_task(ctx, (void *)vdc);
	if (p == NULL)
		return (-1);
	p->priv = vdps;
	p->methods = priv_pedantic_methods;

	*priv = vdps;

	vdps->state = VDPS_INIT;

	return (0);
}

static int v_matchproto_(vdp_bytes_f)
xyzzy_pedantic_bytes(struct vdp_ctx *vdc, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{

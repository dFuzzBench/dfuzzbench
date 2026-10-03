/*-
 * Copyright (c) 2013-2015 Varnish Software AS
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
 * Interaction with the libvgz (zlib) library.
 *
 * The zlib library pollutes namespace a LOT when you include the "vgz.h"
 * (aka "zlib.h") file so we contain the damage by vectoring all access
 * to libz through this source file.
 *
 * The API defined by this file, will also insulate the rest of the code,
 * should we find a better gzip library at a later date.
 *
 */

#include "config.h"

#include <stdlib.h>

#include "cache_varnishd.h"
#include "cache_filter.h"
#include "cache_objhead.h"
#include "cache_vgz.h"

#include "storage/storage.h"

#include "vend.h"

#include "vgz.h"

struct vgz {
	unsigned		magic;
#define VGZ_MAGIC		0x162df0cb
	enum {VGZ_GZ,VGZ_UN}	dir;
	struct vsl_log		*vsl;
	const char		*id;
	int			last_i;
	enum vgz_flag		flag;

	struct stv_buffer	*stvbuf;
	char			*m_buf;
	ssize_t			m_sz;
	ssize_t			m_len;

	intmax_t		bits;

	z_stream		vz;
};

static const char *
vgz_msg(const struct vgz *vg)
{
	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	return (vg->vz.msg ? vg->vz.msg : "(null)");
}

/*--------------------------------------------------------------------
 * Set up a gunzip instance
 */

static struct vgz *
vgz_gunzip(struct vsl_log *vsl, const char *id)
{
	struct vgz *vg;

	ALLOC_OBJ(vg, VGZ_MAGIC);
	AN(vg);
	vg->vsl = vsl;
	vg->id = id;
	vg->dir = VGZ_UN;

	/*
	 * Max memory usage according to zonf.h:
	 *	mem_needed = "a few kb" + (1 << (windowBits))
	 * Since we don't control windowBits, we have to assume
	 * it is 15, so 34-35KB or so.
	 */
	assert(Z_OK == inflateInit2(&vg->vz, 31));
	return (vg);
}

static struct vgz *
VGZ_NewGunzip(struct vsl_log *vsl, const char *id)
{
	VSC_C_main->n_gunzip++;
	return (vgz_gunzip(vsl, id));
}

static struct vgz *
VGZ_NewTestGunzip(struct vsl_log *vsl, const char *id)
{
	VSC_C_main->n_test_gunzip++;
	return (vgz_gunzip(vsl, id));
}

struct vgz *
VGZ_NewGzip(struct vsl_log *vsl, const char *id)
{
	struct vgz *vg;
	int i;

	VSC_C_main->n_gzip++;
	ALLOC_OBJ(vg, VGZ_MAGIC);
	AN(vg);
	vg->vsl = vsl;
	vg->id = id;
	vg->dir = VGZ_GZ;

	/*
	 * From zconf.h:
	 *
	 *	mem_needed = "a few kb"
	 *		+ (1 << (windowBits+2))
	 *		+  (1 << (memLevel+9))
	 *
	 * windowBits [8..15] (-> 1K..128K)
	 * memLevel [1..9] (-> 1K->256K)
	 */
	i = deflateInit2(&vg->vz,
	    cache_param->gzip_level,		/* Level */
	    Z_DEFLATED,				/* Method */
	    16 + 15,				/* Window bits (16=gzip) */
	    cache_param->gzip_memlevel,		/* memLevel */
	    Z_DEFAULT_STRATEGY);
	assert(Z_OK == i);
	return (vg);
}

/*--------------------------------------------------------------------
 */

static int
vgz_getmbuf(struct worker *wrk, struct vgz *vg)
{
	size_t sz;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	AZ(vg->m_sz);
	AZ(vg->m_len);
	AZ(vg->m_buf);
	AZ(vg->stvbuf);

	vg->stvbuf = STV_AllocBuf(wrk, stv_transient, cache_param->gzip_buffer);
	if (vg->stvbuf == NULL)
		return (-1);
	vg->m_buf = STV_GetBufPtr(vg->stvbuf, &sz);
	vg->m_sz = sz;
	AN(vg->m_buf);
	assert(vg->m_sz > 0);
	return (0);
}

/*--------------------------------------------------------------------*/

void
VGZ_Ibuf(struct vgz *vg, const void *ptr, ssize_t len)
{

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	AZ(vg->vz.avail_in);
	assert(len >= 0);
	if (len > 0)
		AN(ptr);

	vg->vz.next_in = TRUST_ME(ptr);
	vg->vz.avail_in = len;
}

int
VGZ_IbufEmpty(const struct vgz *vg)
{

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	return (vg->vz.avail_in == 0);
}

/*--------------------------------------------------------------------*/

void
VGZ_Obuf(struct vgz *vg, void *ptr, ssize_t len)
{

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	AN(ptr);
	assert(len > 0);

	vg->vz.next_out = ptr;
	vg->vz.avail_out = len;
}

int
VGZ_ObufFull(const struct vgz *vg)
{

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	return (vg->vz.avail_out == 0);
}

/*--------------------------------------------------------------------*/

static enum vgzret_e
VGZ_Gunzip(struct vgz *vg, const void **pptr, ssize_t *plen)
{
	int i;
	ssize_t l;
	const uint8_t *before;

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);

	*pptr = NULL;
	*plen = 0;
	AN(vg->vz.next_out);
	AN(vg->vz.avail_out);
	before = vg->vz.next_out;
	i = inflate(&vg->vz, 0);
	if (i == Z_OK || i == Z_STREAM_END) {
		*pptr = before;
		l = (const uint8_t *)vg->vz.next_out - before;
		*plen = l;
	}
	vg->last_i = i;
	if (i == Z_OK)
		return (VGZ_OK);
	if (i == Z_STREAM_END)
		return (VGZ_END);
	if (i == Z_BUF_ERROR)
		return (VGZ_STUCK);
	VSLb(vg->vsl, SLT_Gzip, "Gunzip error: %d (%s)", i, vgz_msg(vg));
	return (VGZ_ERROR);
}

/* set vz pointers for in and out iovecs */
static inline void
vgz_iovec_update(struct vgz *vg, const struct iovec *in, const struct iovec *buf)
{
	/* in: either fully consumed or the same */
	assert(vg->vz.avail_in == 0 || vg->vz.next_in == TRUST_ME(in->iov_base));
	vg->vz.next_in = TRUST_ME(in->iov_base);
	vg->vz.avail_in = in->iov_len;
	vg->vz.next_out = TRUST_ME(buf->iov_base);
	vg->vz.avail_out = buf->iov_len;
}

static enum vgzret_e
vgz_gunzip_iovec(struct vgz *vg, struct iovec *in, struct iovec *buf, struct iovec *out)
{
	int i;

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);
	AN(in && buf && out);
	vgz_iovec_update(vg, in, buf);

	i = inflate(&vg->vz, 0);
	if (i == Z_OK || i == Z_STREAM_END) {
		iovec_collect(buf, out, pdiff(buf->iov_base, vg->vz.next_out));
		in->iov_base = TRUST_ME(vg->vz.next_in);
		in->iov_len = vg->vz.avail_in;
	}
	vg->last_i = i;
	if (i == Z_OK)
		return (VGZ_OK);
	if (i == Z_STREAM_END)
		return (VGZ_END);
	if (i == Z_BUF_ERROR)
		return (VGZ_STUCK);
	VSLb(vg->vsl, SLT_Gzip, "Gunzip error: %d (%s)", i, vgz_msg(vg));
	return (VGZ_ERROR);
}

/*--------------------------------------------------------------------*/

enum vgzret_e
VGZ_Gzip(struct vgz *vg, const void **pptr, ssize_t *plen, enum vgz_flag flags)
{
	int i;
	int zflg;
	ssize_t l;
	const uint8_t *before;

	CHECK_OBJ_NOTNULL(vg, VGZ_MAGIC);

	*pptr = NULL;
	*plen = 0;
	AN(vg->vz.next_out);
	AN(vg->vz.avail_out);
	before = vg->vz.next_out;
	switch (flags) {
	case VGZ_NORMAL:	zflg = Z_NO_FLUSH; break;
	case VGZ_ALIGN:		zflg = Z_SYNC_FLUSH; break;
	case VGZ_RESET:		zflg = Z_FULL_FLUSH; break;
	case VGZ_FINISH:	zflg = Z_FINISH; break;
	default:		WRONG("Invalid VGZ flag");
	}
	i = deflate(&vg->vz, zflg);
	if (i == Z_OK || i == Z_STREAM_END) {
		*pptr = before;
		l = (const uint8_t *)vg->vz.next_out - before;
		*plen = l;
	}
	vg->last_i = i;
	if (i == Z_OK)
		return (VGZ_OK);
	if (i == Z_STREAM_END)
		return (VGZ_END);
	if (i == Z_BUF_ERROR)
		return (VGZ_STUCK);
	VSLb(vg->vsl, SLT_Gzip, "Gzip error: %d (%s)", i, vgz_msg(vg));
	return (VGZ_ERROR);
}

/*--------------------------------------------------------------------
 * VDP for gunzip'ing
 */

// common for traditional interface and vdpio
static int vdp_gunzip_init_common(struct vdp_ctx *vdc);

static
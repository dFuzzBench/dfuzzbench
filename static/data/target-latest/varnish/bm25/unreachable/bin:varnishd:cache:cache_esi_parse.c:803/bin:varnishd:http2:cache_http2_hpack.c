/*-
 * Copyright (c) 2016 Varnish Software AS
 * All rights reserved.
 *
 * Author: Martin Blix Grydeland <martin@varnish-software.com>
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
 */

#include "config.h"

#include "cache/cache_varnishd.h"

#include <ctype.h>
#include <stdio.h>

#include "http2/cache_http2.h"
#include "vct.h"

static void
h2h_assert_ready(const struct h2h_decode *d)
{

	CHECK_OBJ_NOTNULL(d, H2H_DECODE_MAGIC);
	AN(d->out);
	assert(d->namelen >= 2); /* 2 chars from the ": " that we added */
	assert(d->namelen <= d->out_u);
	assert(d->out[d->namelen - 2] == ':');
	assert(d->out[d->namelen - 1] == ' ');
}

// rfc9113,l,2493,2528
static h2_error
h2h_checkhdr(struct vsl_log *vsl, txt nm, txt val)
{
	const char *p;
	int l;
	enum {
		FLD_NAME_FIRST,
		FLD_NAME,
		FLD_VALUE_FIRST,
		FLD_VALUE
	} state;

	if (Tlen(nm) == 0) {
		VSLb(vsl, SLT_BogoHeader, "Empty name");
		return (H2SE_PROTOCOL_ERROR);
	}

	// VSLb(vsl, SLT_Debug, "CHDR [%.*s] [%.*s]",
	//     (int)Tlen(nm), nm.b, (int)Tlen(val), val.b);

	l = vmin_t(int, Tlen(nm) + 2 + Tlen(val), 20);
	state = FLD_NAME_FIRST;
	Tforeach(p, nm) {
		switch(state) {
		case FLD_NAME_FIRST:
			state = FLD_NAME;
			if (*p == ':')
				break;
			/* FALLTHROUGH */
		case FLD_NAME:
			if (isupper(*p)) {
				VSLb(vsl, SLT_BogoHeader,
				    "Illegal field header name (upper-case): %.*s",
				    l, nm.b);
				return (H2SE_PROTOCOL_ERROR);
			}
			if (!vct_istchar(*p) || *p == ':') {
				VSLb(vsl, SLT_BogoHeader,
				    "Illegal field header name (non-token): %.*s",
				    l, nm.b);
				return (H2SE_PROTOCOL_ERROR);
			}
			break;
		default:
			WRONG("http2 field name validation state");
		}
	}

	state = FLD_VALUE_FIRST;
	Tforeach(p, val) {
		switch(state) {
		case FLD_VALUE_FIRST:
			if (vct_issp(*p)) {
				VSLb(vsl, SLT_BogoHeader,
				    "Illegal field value 0x%02x start %.*s",
				    *p, l, nm.b);
				return (H2SE_PROTOCOL_ERROR);
			}
			state = FLD_VALUE;
			/* FALLTHROUGH */
		case FLD_VALUE:
			if (!vct_ishdrval(*p)) {
				VSLb(vsl, SLT_BogoHeader,
				    "Illegal field value 0x%02x %.*s",
				    *p, l, nm.b);
				return (H2SE_PROTOCOL_ERROR);
			}
			break;
		default:
			WRONG("http2 field value validation state");
		}
	}
	if (state == FLD_VALUE && vct_issp(val.e[-1])) {
		VSLb(vsl, SLT_BogoHeader,
		    "Illegal field value 0x%02x (end) at %.*s",
		    val.e[-1], l, nm.b);
		return (H2SE_PROTOCOL_ERROR);
	}
	return (0);
}

static h2_error
h2h_addhdr(struct http *hp, struct h2h_decode *d)
{
	/* XXX: This might belong in cache/cache_http.c */
	txt hdr, nm, val;
	int disallow_empty;
	const char *p;
	unsigned n, has_dup;
	h2_error err;

	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	h2h_assert_ready(d);

	/* Assume hdr is by default a regular header from what we decoded. */
	hdr.b = d->out;
	hdr.e = hdr.b + d->out_u;
	n = hp->nhd;

	/* nm and val are separated by ": " */
	nm.b = hdr.b;
	nm.e = nm.b + d->namelen - 2;
	val.b = nm.e + 2;
	val.e = hdr.e;

	err = h2h_checkhdr(hp->vsl, nm, val);
	if (err != NULL)
		return (err);

	disallow_empty = 0;
	has_dup = 0;

	if (Tlen(hdr) > cache_param->http_req_hdr_len) {
		VSLb(hp->vsl, SLT_BogoHeader, "Header too large: %.20s", hdr.b);
		return (H2SE_ENHANCE_YOUR_CALM);
	}

	/* Match H/2 pseudo headers */
	/* XXX: Should probably have some include tbl for pseudo-headers */
	if (!Tstrcmp(nm, ":method")) {
		hdr.b = val.b;
		n = HTTP_HDR_METHOD;
		disallow_empty = 1;

		/* Check HTTP token */
		Tforeach(p, hdr) {
			if (!vct_istchar(*p))
				return (H2SE_PROTOCOL_ERROR);
		}
	} else if (!Tstrcmp(nm, ":path")) {
		hdr.b = val.b;
		n = HTTP_HDR_URL;
		disallow_empty = 1;

		// rfc9113,l,2693,2705
		if (Tlen(val) > 0 && val.b[0] != '/' && Tstrcmp(val, "*")) {
			VSLb(hp->vsl, SLT_BogoHeader,
			    "Illegal :path pseudo-header %.*s",
			    (int)Tlen(val), val.b);
			return (H2SE_PROTOCOL_ERROR);
		}

		/* Path cannot contain LWS or CTL */
		Tforeach(p, hdr) {
			if (vct_islws(*p) || vct_isctl(*p))
				return (H2SE_PROTOCOL_ERROR);
		}
	} else if (!Tstrcmp(nm, ":scheme")) {
		/* XXX: What to do about this one? (typically
		   "http" or "https"). For now set it as a normal
		   header, stripping the first ':'. */
		hdr.b++;
		has_dup = d->has_scheme;
		d->has_scheme = 1;
		disallow_empty = 1;

		/* Check HTTP token */
		Tforeach(p, val) {
			if (!vct_istchar(*p))
				return (H2SE_PROTOCOL_ERROR);
		}
	} else if (!Tstrcmp(nm, ":authority")) {
		/* NB: we inject "host" in place of "rity" for
		 * the ":authority" pseudo-header.
		 */
		memcpy(d->out + 6, "host", 4);
		hdr.b += 6;
		nm = Tstr(":authority"); /* preserve original */
		has_dup = d->has_authority;
		d->has_authority = 1;
	} else if (nm.b[0] == ':') {
		VSLb(hp->vsl, SLT_BogoHeader,
		    "Unknown pseudo-header: %.*s",
		    vmin_t(int, Tlen(hdr), 20), hdr.b);
		return (H2SE_PROTOCOL_ERROR);	// rfc7540,l,2990,2992
	}

	if (disallow_empty && Tlen(val) == 0) {
		VSLb(hp->vsl, SLT_BogoHeader,
		    "Empty pseudo-header %.*s",
		    (int)Tlen(nm), nm.b);
		return (H2SE_PROTOCOL_ERROR);
	}

	if (n >= HTTP_HDR_FIRST) {
		/* Check for space in struct http */
		if (n >= hp->shd) {
			VSLb(hp->vsl, SLT_LostHeader,
			    "Too many headers: %.*s",
			    vmin_t(int, Tlen(hdr), 20), hdr.b);
			return (H2SE_ENHANCE_YOUR_CALM);
		}
		hp->nhd++;
		AZ(hp->hd[n].b);
	}

	if (has_dup || hp->hd[n].b != NULL) {
		assert(nm.b[0] == ':');
		VSLb(hp->vsl, SLT_BogoHeader,
		    "Duplicate pseudo-header %.*s",
		    (int)Tlen(nm), nm.b);
		return (H2SE_PROTOCOL_ERROR);	// rfc7540,l,3158,3162
	}

	hp->hd[n] = hdr;
	return (0);
}

static void
h2h_decode_init(const struct h2_sess *h2, struct ws *ws)
{
	struct h2h_decode *d;

	CHECK_OBJ_NOTNULL(h2, H2_SESS_MAGIC);
	CHECK_OBJ_NOTNULL(ws, WS_MAGIC);

	AN(h2->decode);
	d = h2->decode;
	INIT_OBJ(d, H2H_DECODE_MAGIC);
	VHD_Init(d->vhd);
	d->out_l = WS_ReserveSize(ws, cache_param->http_req_size);
	/*
	 * Can't do any work without any buffer
	 * space. Require non-zero size.
	 */
	XXXAN(d->out_l);
	d->out = WS_Reservation(ws);

	if (cache_param->h2_max_header_list_size == 0)
		d->limit =
		    (long)(h2->local_settings.max_header_list_size * 1.5);
	else
		d->limit = cache_param->h2_max_header_list_size;

	if (d->limit <
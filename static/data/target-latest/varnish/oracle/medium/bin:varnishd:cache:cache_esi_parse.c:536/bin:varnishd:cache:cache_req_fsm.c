/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2017 Varnish Software AS
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
 * This file contains the request-handling state engine, which is intended to
 * (over time) be(come) protocol agnostic.
 * We already use this now with ESI:includes, which are for all relevant
 * purposes a different "protocol"
 *
 * A special complication is the fact that we can suspend processing of
 * a request when hash-lookup finds a busy objhdr.
 *
 */

#include "config.h"

#include "cache_varnishd.h"
#include "cache_filter.h"
#include "cache_objhead.h"
#include "cache_transport.h"
#include "vcc_interface.h"

#include "http1/cache_http1.h"
#include "storage/storage.h"
#include "vcl.h"
#include "vct.h"
#include "vsha256.h"
#include "vtim.h"

#define REQ_STEPS \
  REQ_STEP(transport,           TRANSPORT,	) \
  REQ_STEP(restart,             RESTART,	static) \
  REQ_STEP(recv,                RECV,		) \
  REQ_STEP(pipe,                PIPE,		static) \
  REQ_STEP(pass,                PASS,		static) \
  REQ_STEP(lookup,              LOOKUP,		static) \
  REQ_STEP(purge,               PURGE,		static) \
  REQ_STEP(miss,                MISS,		static) \
  REQ_STEP(fetch,               FETCH,		static) \
  REQ_STEP(deliver,             DELIVER,	static) \
  REQ_STEP(vclfail,             VCLFAIL,	static) \
  REQ_STEP(synth,               SYNTH,		static) \
  REQ_STEP(transmit,            TRANSMIT,	static) \
  REQ_STEP(finish,              FINISH,		static)

#define REQ_STEP(l, U, priv) \
    static req_state_f cnt_##l; \
    priv const struct req_step R_STP_##U[1] = {{ \
	.name = "Req Step " #l, \
	.func = cnt_##l, \
    }};
REQ_STEPS
#undef REQ_STEP

/*--------------------------------------------------------------------
 * Handle "Expect:" and "Connection:" on incoming request
 */

static enum req_fsm_nxt v_matchproto_(req_state_f)
cnt_transport(struct worker *wrk, struct req *req)
{
	const char *p;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->http, HTTP_MAGIC);
	CHECK_OBJ_NOTNULL(req->transport, TRANSPORT_MAGIC);
	AN(req->req_body_status);

	if (http_GetHdr(req->http, H_Expect, &p)) {
		if (!http_expect_eq(p, 100-continue)) {
			req->doclose = SC_RX_JUNK;
			(void)req->transport->minimal_response(req, 417);
			wrk->stats->client_req_417++;
			return (REQ_FSM_DONE);
		}
		if (req->http->protover >= 11 &&
		    req->htc->pipeline_b == NULL)	// XXX: HTTP1 vs 2 ?
			req->want100cont = 1;
		http_Unset(req->http, H_Expect);
	}

	AZ(req->err_code);

	req->doclose = http_DoConnection(req->http, SC_REQ_CLOSE);
	if (req->doclose == SC_RX_BAD) {
		wrk->stats->client_req_400++;
		(void)req->transport->minimal_response(req, 400);
		return (REQ_FSM_DONE);
	}

	if (req->req_body_status->avail == 1) {
		AN(req->transport->req_body != NULL);
		VFP_Setup(req->vfc, wrk);
		req->vfc->resp = req->http;		// XXX
		req->transport->req_body(req);
	}

	req->ws_req = WS_Snapshot(req->ws);
	HTTP
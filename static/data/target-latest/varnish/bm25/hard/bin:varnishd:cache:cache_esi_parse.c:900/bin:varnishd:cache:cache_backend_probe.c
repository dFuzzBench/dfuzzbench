/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 * Poll backends for collection of health statistics
 *
 * We co-opt threads from the worker pool for probing the backends,
 * but we want to avoid a potentially messy cleanup operation when we
 * retire the backend, so the thread owns the health information, which
 * the backend references, rather than the other way around.
 *
 */

#include "config.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>

#include "cache_varnishd.h"

#include "vbh.h"
#include "vsa.h"
#include "vtcp.h"
#include "vtim.h"

#include "cache_backend.h"
#include "cache_conn_pool.h"

#include "VSC_vbe.h"

struct vbp_state {
	const char			*name;
};

#define VBP_STATE(n) static const struct vbp_state vbp_state_ ## n [1] = {{ .name = #n }}
VBP_STATE(scheduled);
VBP_STATE(running);
VBP_STATE(cold);
VBP_STATE(cooling);
VBP_STATE(deleted);
#undef VBP_STATE

/* Default averaging rate, we want something pretty responsive */
#define AVG_RATE			4

struct vbp_target {
	unsigned			magic;
#define VBP_TARGET_MAGIC		0x6b7cb656

	VRT_BACKEND_PROBE_FIELDS()

	struct backend			*backend;
	struct conn_pool		*conn_pool;

	char				*req;
	int				req_len;

	char				resp_buf[128];
	unsigned			good;

	/* Collected statistics */
#define BITMAP(n, c, t, b)	uintmax_t	n;
#include "tbl/backend_poll.h"

	vtim_dur			last;
	vtim_dur			avg;
	double				rate;

	vtim_real			due;
	const struct vbp_state		*state;
	int				heap_idx;
	struct pool_task		task[1];
};

static struct lock			vbp_mtx;
static pthread_cond_t			vbp_cond;
static struct vbh			*vbp_heap;

static const unsigned char vbp_proxy_local[] = {
	0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d, 0x0a, 0x51,
	0x55, 0x49, 0x54, 0x0a, 0x20, 0x00, 0x00, 0x00,
};

/*--------------------------------------------------------------------*/

static void
vbp_delete(struct vbp_target *vt)
{
	CHECK_OBJ_NOTNULL(vt, VBP_TARGET_MAGIC);

	assert(vt->heap_idx == VBH_NOIDX);

#define DN(x)	/**/
	VRT_BACKEND_PROBE_HANDLE();
#undef DN
	VCP_Rel(&vt->conn_pool);
	free(vt->req);
	FREE_OBJ(vt);
}


/*--------------------------------------------------------------------
 * Record pokings...
 */

static void
vbp_start_poke(struct vbp_target *vt)
{
	CHECK_OBJ_NOTNULL(vt, VBP_TARGET_MAGIC);

#define BITMAP(n, c, t, b) \
	vt->n <<= 1;
#include "tbl/backend_poll.h"

	vt->last = 0;
	vt->resp_buf[0] = '\0';
}

static void
vbp_has_poked(struct vbp_target *vt)
{
	unsigned i, j;
	uint64_t u;

	CHECK_OBJ_NOTNULL(vt, VBP_TARGET_MAGIC);

	/* Calculate exponential average */
	if (vt->happy & 1) {
		if (vt->rate < AVG_RATE)
			vt->rate += 1.0;
		vt->avg += (vt->last - vt->avg) / vt->rate;
	}

	u = vt->happy;
	for (i = j = 0; i < vt->window; i++) {
		if (u & 1)
			j++;
		u >>= 1;
	}
	vt->good = j;
}

void
VBP_Update_Backend(struct vbp_target *vt)
{
	unsigned i = 0, chg;
	char bits[10];

	CHECK_OBJ_NOTNULL(vt, VBP_TARGET_MAGIC);

#define BITMAP(n, c, t, b)			\
	bits[i++] = (vt->n & 1) ? c : '-';
#include "tbl/backend_poll.h"
	bits[i] = '\0';
	assert(i < sizeof bits);

	Lck_Lock(&vbp_mtx);
	if (vt->backend == NULL) {
		Lck_Unlock(&vbp_mtx);
		return;
	}

	i = (vt->good < vt->threshold);
	chg = (i != vt->backend->sick);
	vt->backend->sick = i;
	if (i && chg && vt->backend->director != NULL)
		VDI_Event(vt->backend->director, VDI_EVENT_SICK);

	AN(vt->backend->vcl_name);
	VSL(SLT_Backend_health, NO_VXID,
	    "%s %s %s %s %u %u %u %.6f %.6f \"%s\"",
	    vt->backend->vcl_name, chg ? "Went" : "Still",
	    i ? "sick" : "healthy", bits,
	    vt->good, vt->threshold, vt->window,
	    vt->last, vt->avg, vt->resp_buf);
	vt->backend->vsc->happy = vt->happy;
	if (chg)
		vt->backend->changed = VTIM_real();
	Lck_Unlock(&vbp_mtx);
}

static void
vbp_reset(struct vbp_target *vt)
{
	unsigned u;

	CHECK_OBJ_NOTNULL(vt, VBP_TARGET_MAGIC);
	vt->avg = 0.0;
	vt->rate = 0.0;
#define BITMAP(n, c, t, b) \
	vt->n = 0;
#include "tbl/backend_poll.h"

	for (u = 0; u < vt->initial; u++) {
		vbp_start_poke(vt);
		vt->happy |= 1;
		vbp_has_poked(vt);
	}
}

/*--------------------------------------------------------------------
 * Poke one backend, once, but possibly at both IPv4 and IPv6 addresses.
 *
 * We do deliberately not use the stuff in cache_backend.c, because we
 * want to measure the backends response without local distractions.
 */

static int
vbp_write(struct vbp_target *vt, int *sock, const void *buf, size_t len)
{
	int i;

	i = write(*sock, buf, len);
	VTCP_Assert(i);
	if (i != len) {
		if (i < 0) {
			vt->err_xmit |= 1;
			bprintf(vt->resp_buf, "Write error %d (%s)",
				errno, VAS_errtxt(errno));
		} else {
			bprintf(vt->resp_buf,
				"Short write (%d/%zu) error %d (%s)",
				i, len, errno, VAS_errtxt(errno));
		}
		VTCP_close(sock);
		return (-1);
	}
	return (0);
}

static int
vbp_write_proxy_v1(struct vbp_target *vt, int *sock)
{
	char buf[105]; /* maximum size for a TCP6 PROXY line with null char */
	char addr[VTCP_ADDRBUFSIZE];
	char port[VTCP_PORTBUFSIZE];
	char vsabuf[vsa_suckaddr_len];
	const struct suckaddr *sua;
	int proto;
	struct vsb vsb;

	sua = VSA_getsockname(*sock, vsabuf, sizeof vsabuf);
	AN(sua);
	AN(VSB_init(&vsb, buf, sizeof buf));

	proto = VSA_Get_Proto(sua);
	if (proto == AF_INET || proto == AF_INET6) {
		VTCP_name(sua, addr, sizeof addr, port, sizeof port);
		VSB_printf(&vsb, "PROXY %s %s %s %s %s\r\n",
		    proto == AF_INET ? "TCP4" : "TCP6",
		    addr, addr, port, port);
	} else {
		VSB_cat(&vsb, "PROXY UNKNOWN\r\n");
	}
	AZ(VSB_finish(&vsb));

	VSB_fini(&vsb);
	return (vbp_write(vt, sock, buf, strlen(buf)));
}

static void
vbp_poke(struct vbp_target *vt)
{
	int s, i, proxy_header, err;
	vtim_real t_start, t_now, t_end;
	vtim_dur tmo;
	unsigned rlen, resp;
	char buf[8192], *p;
	struct pollfd pfda[1], *pfd = pfda;
	const struct suckaddr *sa;

	t_start = t_now = VTIM_real();
	t_end = t_start + vt->timeout;

	s = VCP_Open(vt->conn_pool, t_end - t_now, &sa, &err);
	if (s < 0) {
		bprintf(vt->resp_buf, "Open error %d (%s)", err, VAS_errtxt(err));
		Lck_Lock(&vbp_mtx);
		if (vt->backend)
			VBE_Connect_Error(vt->backend->vsc, err);
		Lck_Unlock(&vbp_mtx);
		return;
	}

	i = VSA_Get_Proto(sa);
	if (VSA_Compare(sa, bogo_ip) == 0)
		vt->good_unix |= 1;
	else if (i == AF_INET)
		vt->good_ipv4 |= 1;
	else if (i == AF_INET6)
		vt->good_ipv6 |= 1;
	else
		WRONG("Wrong probe protocol family");

	t_now = VTIM_real();
	tmo = t_end - t_now;
	if (tmo <= 0) {
		bprintf(vt->resp_buf,
			"Open timeout %.3fs exceeded by %.3fs",
			vt->timeout, -tmo);
		VTCP_close(&s);
		return;
	}

	Lck_Lock(&vbp_mtx);
	if (vt->backend != NULL)
		proxy_header = vt->backend->proxy_header;
	else
		proxy_header = -1;
	Lck_Unlock(&vbp_mtx);

	if (proxy_header < 0) {
		bprintf(vt->resp_buf, "%s", "No backend");
		VTCP_close(&s);
		return;
	}

	/* Send the PROXY header */
	assert(proxy_header <= 2);
	if (proxy_header == 1) {
		if (vbp_write_proxy_v1(vt, &s) != 0)
			return;
	} else if (proxy_header == 2 &&
	    vbp_write(vt, &s, vbp_proxy_local, sizeof vbp_proxy_local) != 0)
		return;

	/* Send the request */
	if (vbp_write(vt, &s, vt->req, vt->req_len) != 0)
		return;

	vt->good_xmit |= 1;

	pfd->fd = s;
	rlen = 0;
	while (1) {
		pfd->events = POLLIN;
		pfd->revents = 0;
		t_now = VTIM_real();
		tmo = t_end - t_now;
		if (tmo <= 0) {
			bprintf(vt->resp_buf,
			    "Poll timeout %.3fs exceeded by %.3fs",
			    vt->timeout, -tmo);
			i = -1;
			break;
		}
		i = poll(pfd, 1, VTIM_poll_tmo(tmo));
		if (i <= 0) {
			if (!i) {
				if (!vt->exp_close)
					break;
				errno = ETIMEDOUT;
			}
			bprintf(vt->resp_buf, "Poll error %d (%s)",
			    errno, VAS_errtxt(errno));
			i = -1;
			break;
		}
		if (rlen < sizeof vt->resp_buf)
			i = read(s, vt->resp_buf + rlen,
			    sizeof vt->resp_buf - rlen);
		else
			i = read(s, buf, sizeof buf);
		VTCP_Assert(i);
		if (i <= 0) {
			if (i < 0)
				bprintf(vt->resp_buf, "Read error %d (%s)",
					errno, VAS_errtxt(errno));
			break;
		}
		rlen += i;
	}

	VTCP_close(&s);

	if (i < 0) {
		/* errno reported above */
		vt->err_recv |= 1;
		return;
	}

	if (rlen == 0) {
		bprintf(vt->resp_buf, "%s", "Empty response");
		return;
	}

	/* So we have a good receive ... */
	t_now = VTIM_real();
	vt->last = t_now - t_start;
	vt->good_recv |= 1;

	/* Now find out if we like the response */
	vt->resp_buf[sizeof vt->resp_buf - 1] = '\0';
	p = strchr(vt->resp_buf, '\r');
	if (p != NULL)
		*p = '\0';
	p = strchr(vt->resp_buf, '\n');
	if (p != NULL)
		*p = '\0';

	i = sscanf(vt->resp_buf, "HTTP/%*f %u ", &resp);

	if (i == 1 && resp == vt->exp_status)
		vt->happy |= 1;
}

/*--------------------------------------------------------------------
 */
static void
vbp_heap_insert(struct vbp_target *vt)
{
	// Lck_AssertHeld(&vbp_mtx);
	VBH_insert(vbp_heap, vt);
	if (VBH_root(vbp_heap) == vt)
		PTOK(pthread_cond_signal(&vbp_cond));
}

/*--------------------------------------------------------------------
 */

/*
 * called when a task was successful or could not get scheduled
 * returns non-NULL if target is to be deleted (
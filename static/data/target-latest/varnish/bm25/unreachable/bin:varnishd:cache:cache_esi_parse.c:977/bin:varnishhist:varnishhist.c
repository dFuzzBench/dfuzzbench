/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 * Author: Dag-Erling Smørgrav <des@des.no>
 * Author: Guillaume Quintard <guillaume.quintard@gmail.com>
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
 * Log tailer for Varnish
 */

#include "config.h"

#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VOPT_DEFINITION
#define VOPT_INC "varnishhist_options.h"

#include "vdef.h"
#include "vcurses.h"
#include "vapi/vsl.h"
#include "vapi/vsm.h"
#include "vapi/voptget.h"
#include "vapi/vsig.h"
#include "vas.h"
#include "vut.h"
#include "vtim.h"

#define HIST_N 2000		/* how far back we remember */
#define HIST_RES 100		/* bucket resolution */

static struct VUT *vut;

static int hist_low;
static int hist_high;
static int hist_range;
static unsigned hist_buckets;

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

static int end_of_file = 0;
static unsigned ms_delay = 1000;
static unsigned rr_hist[HIST_N];
static unsigned nhist;
static unsigned next_hist;
static unsigned *bucket_miss;
static unsigned *bucket_hit;
static char *format;
static int match_tag;
static double timebend = 0, t0;
static double vsl_t0 = 0, vsl_to, vsl_ts = 0;
static pthread_cond_t timebend_cv;
static double log_ten;
static char *ident;

static const unsigned scales[] = {
	1,
	2,
	3,
	4,
	5,
	10,
	15,
	20,
	25,
	50,
	100,
	250,
	500,
	1000,
	2500,
	5000,
	10000,
	25000,
	50000,
	100000,
	UINT_MAX
};

struct profile {
	const char *name;
	char VSL_arg;
	enum VSL_tag_e tag;
	const char *prefix;
	int field;
	int hist_low;
	int hist_high;
};

#define HIS_PROF(name,vsl_arg,tag,prefix,field,hist_low,high_high,doc)	\
	{name,vsl_arg,tag,prefix,field,hist_low,high_high},
#define HIS_NO_PREFIX	NULL
#define HIS_CLIENT	'c'
#define HIS_BACKEND	'b'
static const struct profile profiles[] = {
#include "varnishhist_profiles.h"
	{ NULL }
};
#undef HIS_NO_PREFIX
#undef HIS_BACKEND
#undef HIS_CLIENT
#undef HIS_PROF

static const struct profile *active_profile;

static void
update(void)
{
	char t[VTIM_FORMAT_SIZE];
	const unsigned w = COLS / hist_range;
	const unsigned n = w * hist_range;
	unsigned bm[n], bh[n];
	unsigned max;
	unsigned scale;
	int i, j;
	unsigned k, l;

	/* Draw horizontal axis */
	for (k = 0; k < n; ++k)
		(void)mvaddch(LINES - 2, k, '-');
	for (i = 0, j = hist_low; i < hist_range; ++i, ++j) {
		(void)mvaddch(LINES - 2, w * i, '+');
		IC(mvprintw(LINES - 1, w * i, "|1e%d", j));
	}

	if (end_of_file)
		IC(mvprintw(0, 0, "%*s", COLS - 1, "EOF"));
	else
		IC(mvprintw(0, 0, "%*s", COLS - 1, ident));

	/* count our flock */
	memset(bm, 0, sizeof bm);
	memset(bh, 0, sizeof bh);
	for (k = 0, max = 1; k < hist_buckets; ++k) {
		l = k * n / hist_buckets;
		assert(l < n);
		bm[l] += bucket_miss[k];
		bh[l] += bucket_hit[k];
		max = vmax(max, bm[l] + bh[l]);
	}

	/* scale,time */
	assert(LINES - 3 >= 0);
	for (i = 0; max / scales[i] > (unsigned)(LINES - 3); ++i)
		/* nothing */ ;
	scale = scales[i];

	if (vsl_t0 > 0) {
		VTIM_format(vsl_ts, t);

		IC(mvprintw(0, 0, "1:%u, n = %u, d = %g @ %s x %g",
		    scale, nhist, 1e-3 * ms_delay, t, timebend));
	} else {
		IC(mvprintw(0, 0, "1:%u, n = %u, d = %g",
		    scale, nhist, 1e-3 * ms_delay));
	}

	for (j = 5; j < LINES - 2; j += 5)
		IC(mvprintw((LINES - 2) - j, 0, "%u_", j * scale));

	/* show them */
	for (k = 0; k < n; ++k) {
		for (l = 0; l < bm[k] / scale; ++l)
			(void)mvaddch((LINES - 3) - l, k, '#');
		for (; l < (bm[k] + bh[k]) / scale; ++l)
			(void)mvaddch((LINES - 3) - l, k, '|');
	}
}

inline static void
upd_vsl_ts(const char *p)
{

	if (timebend == 0)
		return;

	p = strchr(p, ' ');

	if (p == NULL)
		return;

	vsl_ts = vmax_t(double, vsl_ts, strtod(p + 1,
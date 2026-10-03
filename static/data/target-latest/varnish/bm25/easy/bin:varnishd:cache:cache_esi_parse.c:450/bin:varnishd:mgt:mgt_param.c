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
 */

#include "config.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mgt/mgt.h"
#include "common/heritage.h"

#include "mgt/mgt_param.h"
#include "vav.h"
#include "vcli_serve.h"

struct plist {
	unsigned			magic;
#define PLIST_MAGIC			0xbfc3ea16
	VTAILQ_ENTRY(plist)		list;
	struct parspec			*spec;
};

static VTAILQ_HEAD(, plist)		phead = VTAILQ_HEAD_INITIALIZER(phead);

struct params mgt_param;
static const int margin1 = 8;
static int margin2 = 0;
static const int wrap_at = 72;
static const int tab0 = 3;

/*--------------------------------------------------------------------*/

static const char TYPE_TIMEOUT_TEXT[] =
	"\n\n"
	"NB: This parameter can be disabled with the value \"never\".";

static const char OBJ_STICKY_TEXT[] =
	"\n\n"
	"NB: This parameter is evaluated only when objects are created. "
	"To change it for all objects, restart or ban everything.";

static const char DELAYED_EFFECT_TEXT[] =
	"\n\n"
	"NB: This parameter may take quite some time to take (full) effect.";

static const char MUST_RESTART_TEXT[] =
	"\n\n"
	"NB: This parameter will not take any effect until the "
	"child process has been restarted.";

static const char MUST_RELOAD_TEXT[] =
	"\n\n"
	"NB: This parameter will not take any effect until the "
	"VCL programs have been reloaded.";

static const char EXPERIMENTAL_TEXT[] =
	"\n\n"
	"NB: We do not know yet if it is a good idea to change "
	"this parameter, or if the default value is even sensible. "
	"Caution is advised, and feedback is most welcome.";

static const char WIZARD_TEXT[] =
	"\n\n"
	"NB: Do not change this parameter, unless a developer tell "
	"you to do so.";

static const char PROTECTED_TEXT[] =
	"\n\n"
	"NB: This parameter is protected and cannot be changed.";


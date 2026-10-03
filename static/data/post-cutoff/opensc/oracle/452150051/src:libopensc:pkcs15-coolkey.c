/*
 * partial PKCS15 emulation for Coolkey cards
 *
 * Copyright (C) 2005,2006,2007,2008,2009,2010
 *               Douglas E. Engert <deengert@anl.gov>
 *               2004, Nils Larsch <larsch@trustcenter.de>
 * Copyright (C) 2006, Identity Alliance,
 *               Thomas Harning <thomas.harning@identityalliance.com>
 * Copyright (C) 2007, EMC, Russell Larner <rlarner@rsa.com>
 * Copyright (C) 2016, Red Hat, Inc.
 *
 * Coolkey driver author: Robert Relyea <rrelyea@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "internal.h"
#include "cardctl.h"
#include "asn1.h"
#include "pkcs15.h"
#include "../pkcs11/pkcs11.h"

typedef struct pdata_st {
	const char *id;
	const char *label;
	const char *path;
	int         ref;
	int         type;
	unsigned int maxlen;
	unsigned int minlen;
	unsigned int storedlen;
	int         flags;
	int         tries_left;
	const unsigned char  pad_char;
	int         obj_flags;
} pindata;

static int coolkey_detect_card(sc_pkcs15_card_t *p15card)
{
	sc_card_t *card = p15card->card;

	SC_FUNC_CALLED(card->ctx, SC_LOG_DEBUG_VERBOSE);
	if (card->type < SC_CARD_TYPE_COOLKEY_GENERIC
		|| card->type >= SC_CARD_TYPE_COOLKEY_GENERIC+1000)
		return SC_ERROR_INVALID_CARD;
	return SC_SUCCESS;
}

static int

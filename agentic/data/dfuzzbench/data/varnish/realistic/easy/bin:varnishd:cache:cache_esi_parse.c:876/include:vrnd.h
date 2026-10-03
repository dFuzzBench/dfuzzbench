/*-
 * Copyright (c) 2013 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modific
 *
 * Random functions
 */

typedef void vrnd_lock_f(void);

extern vrnd_lock_f *VRND_Lock;
extern vrnd_lock_f *VRND_Unlock;

int VRND_RandomCrypto(void *, size_t);

long VRND_RandomTestable(void);
double VRND_RandomTestableDouble(void);
void VRND_SeedTestable(unsigned int);
void VRND_SeedAll(void);		/* Seed random(3) properly */


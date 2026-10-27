/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (C) 2026 Vasily Kazantsev (as IBXM's port: LICENSE-ibxm.txt's terms) */
/* The FM-1's IBXM port (not part of upstream): no C library and no heap on the device. calloc takes
 * from a fixed arena (IBXM allocates only when a song's replay is built: the module, instrument and
 * pattern tables, the channels); free does nothing. */
#ifndef IBXM_PORT_H
#define IBXM_PORT_H
#include <string.h>
#include "../../plat.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef IBXM_ARENA
#define IBXM_ARENA 6400                  /* bytes: a song's tables and its replay; 16 tracks and 256 positions,
                                          * the most, take 6,248 (fw/tests/sound/transport_test.c builds one) */
#endif
/* two arenas: the player's first replay's (a song, the main deck, the loops while nothing else plays), and
 * DJ's second deck's, memory lent while DJ plays (ibxm_arena_mem1); a replay is built in either
 * (ibxm_arena_use) */
static long ib_arena0[IBXM_ARENA / sizeof(long)] PLAT_POOL;
static long *ib_arena1;
static long *ib_arena = ib_arena0;
static unsigned int ib_used[2], ib_k;
#define ib_arena_used ib_used[ib_k]

static void *ib_calloc(unsigned int count, unsigned int size)
{
    unsigned int bytes = (count * size + 7u) & ~7u, i;
    char *p;
    if (bytes > IBXM_ARENA - ib_arena_used)
        return 0;
    p = (char *)ib_arena + ib_arena_used;
    ib_arena_used += bytes;
    for (i = 0; i < bytes; i++)
        p[i] = 0;
    return p;
}

#define calloc(n, s) ib_calloc((unsigned int)(n), (unsigned int)(s))
#define free(p) ((void)(p))

#endif

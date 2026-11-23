/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The voice pool's links and lists checked after a half (included after player.c, built with POOL_CHECK: it reads its
 * statics). A voice in use is one of three: in an engine (the engine's voice links back, and it plays what the engine
 * holds), a ghost (in the ghosts' list once, never a TB-303's; one whose return is under way named by exactly one
 * engine, which has no voice and no fade, its state the return's), or a start waiting for a fade (named by exactly one
 * engine with a fade note); a free voice is none of them. An engine's voice links back to it; an engine fading has no
 * voice of its own, its fade note names it, and the voice waiting for it (if any) waits. The voices in use are the
 * engines' plus the waiting plus the ghosts. The ghosts' list holds 24 at most, each once, in the order they were made
 * (player.c stamps each as made). After a half's voices pass: no waiting start is left, and a fade note only past the
 * count (core 2's job lost in the pass), none a held ghost's return into a release tail's engine; no ghost is
 * listed past its end, and the ghosts' soonest end is no later than any ghost's; a held ghost has no let-go, one let go
 * ends 10 s after its let-go at the latest; no engine past the count holds a held note (another count makes them
 * ghosts); no engine holds a note a mute makes a ghost (the voices pass made it one, a muted track's new note starts as
 * one, and none comes back while muted). A free engine (no voice, no fade, no return) is silent if a DX7's, Juno-106's
 * or sampler's: a note leaves its engine ended, cut or faded out, a return dropped included (a CHIP cut rings on
 * through its band-limited tail; a TB-303 never returns). A return under way holds an engine of the count,
 * fresh (its note's start to come) or of its own kind. A broken rule ends the test at once, saying where. */
#ifndef POOL_CHECK_H
#define POOL_CHECK_H
#include <stdio.h>
#include <stdlib.h>

static void pool_fail(const char *where, const char *what, unsigned a, unsigned b)
{
    fprintf(stderr, "%s: the pool broken: %s (%u, %u)\n", where, what, a, b);
    exit(1);
}

static void pool_check(const char *where)
{
    uint32_t i, k, used = 0, bound = 0, waiting = 0;
    if (pl_gn > PL_GHOSTS)
        pool_fail(where, "ghosts past the list's room", pl_gn, PL_GHOSTS);
    if (pl_nfd > PL_FADES)
        pool_fail(where, "fade notes past their room", pl_nfd, PL_FADES);
    for (i = 0; i < PL_NV; i++) {
        const pl_voice_t *v = &pl_v[i];
        uint32_t in_list = 0, waits = 0;
        for (k = 0; k < pl_gn; k++)
            in_list += pl_gl[k] == i;
        for (k = 0; k < PL_NE; k++)
            waits += pl_e[k].wait == i;
        if (v->use == PV_FREE) {
            if (v->place != PL_NO_ENG || v->gh != GH_NO || in_list || waits)
                pool_fail(where, "a free voice in an engine, the list or waiting", i, v->place);
            continue;
        }
        used++;
        if (v->place != PL_NO_ENG) {
            if (v->place >= PL_NE || pl_e[v->place].voice != i || v->engine != pl_e[v->place].kind)
                pool_fail(where, "a voice's engine not its (voice, engine)", i, v->place);
            if (v->gh != GH_NO || in_list || waits)
                pool_fail(where, "a voice in an engine is a ghost or waits too", i, v->gh);
            if (pl_v_muted(v) && v->engine != EN_TB)
                pool_fail(where, "a muted note in an engine after the voices pass (voice, its track)", i, v->who);
            bound++;
        } else if (v->gh == GH_GHOST || v->gh == GH_BACK) {
            if (in_list != 1u || waits != (v->gh == GH_BACK))
                pool_fail(where, "a ghost not in the list once, or its return under way not one engine's", i, waits);
            if (v->engine == EN_TB)
                pool_fail(where, "a TB-303 ghost", i, v->who);
        } else if (v->gh == GH_WAIT) {
            if (waits != 1u || in_list)
                pool_fail(where, "a waiting voice not named by one engine", i, waits);
            waiting++;
        } else {
            pool_fail(where, "a voice in use with no engine, neither a ghost nor waiting", i, v->engine);
        }
    }
    for (i = 0; i < PL_NE; i++) {
        const pl_eng_t *e = &pl_e[i];
        if (e->voice != PL_NO_VOICE && (e->voice >= PL_NV || pl_v[e->voice].place != i))
            pool_fail(where, "an engine's voice not in it (engine, voice)", i, e->voice);
        if (e->fade != PL_NO_FADE && (e->fade >= pl_nfd || pl_fd[e->fade].eng != i || e->voice != PL_NO_VOICE))
            pool_fail(where, "an engine's fade note not its, or a voice in it while it fades", i, e->fade);
        if (e->wait != PL_NO_VOICE && (e->wait >= PL_NV || (e->fade == PL_NO_FADE) != (pl_v[e->wait].gh == GH_BACK) ||
                                       (pl_v[e->wait].gh != GH_WAIT && pl_v[e->wait].gh != GH_BACK) ||
                                       e->voice != PL_NO_VOICE))
            pool_fail(where, "a voice waiting for an engine with no fade, or a return's engine fading or voiced", i,
                      e->wait);
        if (i >= pl_nv && e->voice != PL_NO_VOICE && pl_v[e->voice].held && !pl_v[e->voice].cut)
            pool_fail(where, "an engine past the count holds a held note (engine, voice)", i, e->voice);
        if (e->voice == PL_NO_VOICE && e->wait == PL_NO_VOICE && e->fade == PL_NO_FADE &&
            (e->kind == EN_DX7 || e->kind == EN_JUNO || e->kind == EN_SAMP) && pl_e_sounds(e))
            pool_fail(where, "a free engine still sounding (engine, kind)", i, e->kind);
        if (e->wait != PL_NO_VOICE && e->wait < PL_NV && pl_v[e->wait].gh == GH_BACK &&
            (i >= pl_nv || (e->kind != 255 && e->kind != pl_v[e->wait].engine)))
            pool_fail(where, "a return under way past the count or in another kind's engine (engine, kind)", i,
                      e->kind);
    }
    for (i = 0; i < pl_nfd; i++)
        if (pl_fd[i].eng >= PL_NE || pl_e[pl_fd[i].eng].fade != i)
            pool_fail(where, "a fade note's engine not fading by it", i, pl_fd[i].eng);
    if (used != bound + waiting + pl_gn)
        pool_fail(where, "the voices in use are not the engines', the waiting and the ghosts", used, bound + waiting + pl_gn);
    if (waiting)
        pool_fail(where, "a waiting start left after the voices pass", waiting, 0);
    for (i = 0; i < pl_nfd; i++) {       /* (a fade left only past the count: the count fell inside the voices pass, core
                                          * 2's job lost; it renders in the next block) */
        if (pl_fd[i].eng < pl_nv)
            pool_fail(where, "a fade note left after the voices pass in an engine of the count", i, pl_fd[i].eng);
        if (pl_e[pl_fd[i].eng].wait != PL_NO_VOICE && pl_v[pl_e[pl_fd[i].eng].wait].gh == GH_BACK)
            pool_fail(where, "a ghost's return into a tail's engine left after the voices pass", i, pl_fd[i].eng);
    }
    for (i = 0; i < pl_gn; i++) {
        const pl_voice_t *g = &pl_v[pl_gl[i]];
        if (i && pl_gh_made[pl_gl[i - 1u]] >= pl_gh_made[pl_gl[i]])
            pool_fail(where, "the ghosts out of the order they were made in (place, voice)", i, pl_gl[i]);
        if (g->t_end != PL_NEVER && (int32_t)(g->t_end - pl_blk) < 0)
            pool_fail(where, "a ghost listed past its end (voice, blocks past)", pl_gl[i], pl_blk - g->t_end);
        if (g->t_end != PL_NEVER && (pl_gh_soon == PL_NEVER || (int32_t)(g->t_end - pl_gh_soon) < 0))
            pool_fail(where, "a ghost's end before the soonest end (voice, blocks on)", pl_gl[i], g->t_end - pl_blk);
        if (g->held ? g->t_off != FWD_HELD
                    : g->t_end == PL_NEVER || (int32_t)(g->t_end - g->t_off) > (int32_t)PL_GH_KEEP)
            pool_fail(where, "a ghost's let-go and end not as held or let go (voice, held)", pl_gl[i], g->held);
    }
}
#endif

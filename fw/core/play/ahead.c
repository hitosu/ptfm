/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The patterns about to play, found ahead by the main loop (ahead.h): for each replay, its song's first pattern
 * (where every start's seek reads first: a DJ deck's start made ready finds it there), the one playing and the
 * next as the song's flow goes on (the order's next, or its LOOP FROM's), or the EDITOR's loop's pattern; rec_ahead
 * keeps rec_pattern's answer for each, so the audio context, reaching one, copies where its tracks start instead of
 * walking every track's cells (the store's lookups and the walk were most of a pattern change's cost, and of a deck's start).
 * Work is done only when a replay has moved on, or the store changed. */
#include "ahead.h"
#include "player.h"
#include "rec.h"
#include "../store/store.h"

static struct { uint8_t what, song, pos, pat, s[2], p[2], valid; uint32_t gen; } last;

/* a song's first pattern, the one at pos and the next, into w from n; the new n */
static int song_pats(uint8_t song, uint8_t pos, uint8_t *w, int n)
{
    rec_song_t s;
    uint32_t p, next;
    if (song == 0xFFu || rec_song(song, &s) || !s.len || n + 3 > REC_AHEAD)
        return n;
    p = pos < s.len ? pos : 0u;
    next = p + 1u;
    if (next >= s.len)
        next = (s.flags & RS_STOP) ? p : (s.loop_from < s.len ? s.loop_from : 0u);
    w[n++] = s.order[0];
    w[n++] = s.order[p];
    w[n++] = s.order[next];
    return n;
}

void ahead_poll(void)
{
    player_state_t st;
    uint8_t w[REC_AHEAD];
    int n = 0;
    player_state(&st, 0);
    if (last.valid && last.gen == store_gen() && last.what == st.what && last.song == st.song && last.pos == st.pos &&
        last.pat == st.pat && last.s[0] == st.deck[0].song && last.p[0] == st.deck[0].pos &&
        last.s[1] == st.deck[1].song && last.p[1] == st.deck[1].pos)
        return;
    if (st.what == PL_LOOP) {
        w[n++] = st.pat;
    } else if (st.what == PL_DJ) {
        n = song_pats(st.deck[0].song, st.deck[0].pos, w, n);
        n = song_pats(st.deck[1].song, st.deck[1].pos, w, n);
    } else if (st.what == PL_SONG || st.what == PL_NONE) {
        n = song_pats(st.song, st.pos, w, n);
    }                                   /* (the loops, a preview: patterns of their own source, not the store's) */
    rec_ahead(w, n);
    last.what = st.what;
    last.song = st.song;
    last.pos = st.pos;
    last.pat = st.pat;
    last.s[0] = st.deck[0].song;
    last.p[0] = st.deck[0].pos;
    last.s[1] = st.deck[1].song;
    last.p[1] = st.deck[1].pos;
    last.gen = store_gen();
    last.valid = 1;
}

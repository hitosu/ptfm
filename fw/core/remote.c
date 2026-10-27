/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The state a page asks for (specs/connections.md#the-state), and PT/FM's messages packed 7 in 8: remote.h. */
#include <string.h>
#include "remote.h"
#include "plat.h"
#include "dbg.h"
#include "fx/fx.h"
#include "midi/midi.h"
#include "play/player.h"
#include "play/dj.h"
#include "play/rec.h"
#include "store/store.h"
#include "ui/ui_core.h"

static uint8_t asked;

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

int remote_send(uint8_t cmd, const uint8_t *b, uint32_t len)
{
    uint8_t out[640];                   /* (USB's largest message; packed on the main loop's stack) */
    uint32_t k = 0;
    if (8u + (len * 8u + 6u) / 7u > sizeof out)
        return 0;
    out[k++] = 0xF0; out[k++] = 0x7D; out[k++] = 0x50; out[k++] = 0x54; out[k++] = cmd;
    for (uint32_t i = 0; i < len; i += 7) {
        uint32_t top = k++;
        out[top] = 0;
        for (uint32_t j = 0; j < 7 && i + j < len; j++) {
            out[top] |= (uint8_t)((b[i + j] >> 7) << j);
            out[k++] = b[i + j] & 0x7F;
        }
    }
    out[k++] = 0xF7;
    return !plat_sysex_send(out, k);
}

void remote_dj_put(uint8_t *p)
{
    memset(p, 0, REMOTE_DJ_LEN);
    p[0] = (uint8_t)(dj_live() ? 1u : 0u);
    for (int d = 0; d < 2; d++) {
        uint8_t *q = p + 1 + 24 * d;
        dj_deck_state_t s;
        rec_song_t so;
        dj_deck_state(d, &s);
        q[0] = (uint8_t)((s.playing ? 1u : 0u) | (s.armed ? 2u : 0u) | (s.ended ? 4u : 0u) | (s.stopped ? 8u : 0u) |
                         (s.roll ? 16u : 0u));
        p[0] |= (uint8_t)(s.playing ? 2u << d : 0u);
        q[1] = s.pos;
        q[2] = s.row;
        put16(q + 3, s.bpm10);
        q[5] = s.stop;
        q[6] = s.loop;
        q[7] = (uint8_t)(s.loop_rows > 255u ? 255u : s.loop_rows);
        if (s.song != DJ_NO_SONG && !rec_song(s.song, &so))
            memcpy(q + 8, so.name, 16);
    }
    memcpy(p + 49, DJ_REC, DJ_REC_LEN);
}

int remote_state(uint8_t *f)
{
    player_state_t st;
    rec_song_t so;
    int mch = 0, mir = midi_mirror(&mch);
    const ui_screen_t *sc = ui_screen_now();
    ui_overlay_t *o = ui_top();
    memset(f, 0, REMOTE_STATE_LEN);
    player_state(&st, 0);
    f[0] = REMOTE_STATE_VERSION;
    f[1] = (uint8_t)((mir ? 2u : 0u) | (ui_arp_on() ? 4u : 0u) | (ui_set[S_ARP_LATCH] ? 8u : 0u) |
                     (st.playing ? 16u : 0u));
#ifdef PTFM_DEBUG
    f[1] |= (uint8_t)(1u | (dbg_usb_tone ? 32u : 0u));
#endif
    f[2] = (uint8_t)mch;
    f[3] = st.what;
    f[4] = st.song;
    f[5] = st.pos;
    f[6] = st.row;
    put16(f + 7, st.bpm10);
    put16(f + 9, fx_active());
    f[11] = st.voice_count;
    f[12] = st.voices;
    put16(f + 13, ui_unsaved > 65535u ? 65535u : ui_unsaved);
    put16(f + 15, (uint32_t)ui_set[S_HOLD_DELAY]);
    put16(f + 17, (uint32_t)ui_set[S_COMBO_GRACE]);
    f[19] = (uint8_t)(ui_cur.inst >> 8);
    f[20] = (uint8_t)ui_cur.inst;
    f[21] = (uint8_t)(int8_t)ui_set[S_KEYS_OCTAVE];
    f[22] = (uint8_t)ui_midi_set[S_SYNC];
    f[23] = (uint8_t)ui_set[S_COUNT_IN];
    f[24] = (uint8_t)(int8_t)ui_set[S_USB_GAIN];
    if (sc && sc->name) strncpy((char *)f + 26, T(sc->name), 15);
    if (o) {
        const char *w = ui_overlay_words(o);
        f[42] = o->kind;
        f[43] = (uint8_t)ui_overlay_count();
        if (w) strncpy((char *)f + 44, w, 23);
    }
    if (sc && sc->cursor) sc->cursor(f + 68);
    if (!rec_song(ui_cur.song, &so)) memcpy(f + 76, so.name, 16);   /* (the current song's: SONGS' SELECT) */
#ifdef PTFM_VERSION
    strncpy((char *)f + 92, PTFM_VERSION, 23);
#endif
    remote_dj_put(f + 116);
    const char *toast = ui_toast_now();
    if (toast) strncpy((char *)f + 301, toast, 23);
    put16(f + 325, store_edit_used() / 4u);             /* the edit memory (u16 in 4-byte units: each a multiple of 4) */
    put16(f + 327, store_edit_top() / 4u);
    put16(f + 329, store_edit_limit() / 4u);
    return REMOTE_STATE_LEN;
}

int remote_sysex(const uint8_t *msg, uint32_t n)
{
    if (n != 6 || msg[0] != 0xF0 || msg[1] != 0x7D || msg[2] != 0x50 || msg[3] != 0x54 || msg[4] != 0x40 ||
        msg[5] != 0xF7)
        return 0;
    asked = 1;
    return 1;
}

void remote_poll(void)
{
    static uint8_t num;                 /* the answers sent, numbered 1-255 then 1 again (a page counts what it lost) */
    if (!asked) return;
    uint8_t f[REMOTE_STATE_LEN];
    remote_state(f);
    f[25] = (uint8_t)(num % 255u + 1u);
    if (remote_send(0x41, f, REMOTE_STATE_LEN)) {   /* (USB busy: again next pass, the same number) */
        asked = 0;
        num = f[25];
    }
}

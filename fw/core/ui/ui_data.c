/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The records as the screens show them (ui_data.h; specs/files/README.md, songs.md, patterns.md,
 * instruments.md). */
#include <string.h>
#include "ui_data.h"
#include "gfx.h"
#include "fxdef.h"
#include "../store/store.h"
#include "../play/rec.h"
#include "../play/player.h"

const txt_t ENG_SHORT[ENG_COUNT] = {TX(""), TX("DX7"), TX("JUNO"), TX("TB-303"), TX("SAMPLE"), TX("FUSION"), TX("CHIP")};
const txt_t ENG_TYPE[ENG_COUNT] = {TX(""), TX("DX7"), TX("JUNO-106"), TX("TB-303"), TX("SAMPLER"), TX("FUSION"), TX("CHIP")};
const uint8_t ENG_COL[ENG_COUNT] = {C_DGREY, C_LRED, C_LCYAN, C_LGREEN, C_YELLOW, C_LMAGENTA, C_LBLUE};
const uint8_t ENG_SLOTS[ENG_COUNT] = {0, 32, 128, 128, 64, 64, 128};

/* the names' characters by their 6-bit codes: 0 space, 1-26 A-Z, 27-36 0-9, 37-48 signs */
static const char NAME6[49] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:.-?|/\\*#$@%";

uint32_t ui_bits(const uint8_t *p, uint32_t bit, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++, bit++) v |= (uint32_t)(p[bit >> 3] >> (bit & 7) & 1) << i;
    return v;
}
void ui_put_bits(uint8_t *p, uint32_t bit, int n, uint32_t v)
{
    for (int i = 0; i < n; i++, bit++) {
        uint8_t m = (uint8_t)(1u << (bit & 7));
        if (v >> i & 1) p[bit >> 3] |= m; else p[bit >> 3] &= (uint8_t)~m;
    }
}
void ui_put_name6(uint8_t *p, uint32_t bit, int n, const char *name)
{
    int end = 0;
    for (int i = 0; i < n; i++) {
        char c = end ? ' ' : name[i];
        if (!c) { end = 1; c = ' '; }
        if (c >= 'a' && c <= 'z') c -= 32;
        uint32_t code = 0;
        for (uint32_t k = 0; k < sizeof NAME6 - 1; k++) if (NAME6[k] == c) code = k;
        ui_put_bits(p, bit + 6u * (uint32_t)i, 6, code);
    }
}
int ui_free_pattern(void)
{
    uint8_t nums[256];
    int n = store_list(REC_PATTERN, nums, 256), k = 0;
    for (int f = 0; f < 0xF0; f++) {
        while (k < n && nums[k] < f) k++;
        if (k == n || nums[k] != f) return f;
    }
    return -1;
}
int ui_name6(const uint8_t *p, uint32_t bit, int n, char *dst)
{
    int k = 0, empty = ui_bits(p, bit, 6) == 63;
    for (int i = 0; i < n; i++) {
        uint32_t c = ui_bits(p, bit + 6u * (uint32_t)i, 6);
        dst[k++] = c < sizeof NAME6 - 1 ? NAME6[c] : ' ';
    }
    while (k && dst[k - 1] == ' ') k--;
    dst[k] = 0;
    return !empty;
}

/* ---- songs ---- */
int ui_song(uint8_t n, ui_song_t *s)
{
    uint32_t len;
    uint8_t ver;
    const uint8_t *p = store_get(REC_SONG, n, 0, &len, &ver);
    if (!p || len < 22) return 0;
    ui_name6(p, 0, 16, s->name);
    s->tempo = (uint16_t)(p[12] | p[13] << 8);
    s->ticks = p[14] ? p[14] : 6;
    s->rpb = p[15] ? p[15] : 4;
    s->swing = p[16];
    s->flags = p[17];
    s->loop_from = p[18];
    s->gvol = p[19];
    s->npos = (uint16_t)(p[20] + 1);
    if (21u + s->npos > len) s->npos = (uint16_t)(len - 21);
    s->order = p + 21;
    return 1;
}
int ui_songs(uint8_t *nums, int max)
{
    uint8_t all[256];
    int n = store_list(REC_SONG, all, 256), k = 0;
    for (int i = 0; i < n && k < max; i++)
        if (all[i] != UI_KEYS_SONG) nums[k++] = all[i];
    return k;
}
static uint8_t keys_song[21 + 1 + 1 + 64 + 8];          /* (the keys' song's record: one position, no mixer, its FX
                                                         * setup, the spare bytes) */
void ui_keys_song_init(void)
{
    memset(keys_song, 0, sizeof keys_song);
    ui_put_name6(keys_song, 0, 16, T("THE KEYS"));
    keys_song[12] = 120;                                /* TEMPO 120 */
    keys_song[14] = 6; keys_song[15] = 4; keys_song[16] = 50; keys_song[19] = 64;
    keys_song[21] = 0xFF;                               /* one position: no pattern there */
    fxd_default(keys_song + 23);                        /* (after the mixer's count, 0) */
    fxd_set_on(keys_song + 23, FE_REVERB, 1);           /* and the REVERB at its own settings: the keys sound round at once */
    builtin_rec_t r = {REC_SONG, UI_KEYS_SONG, 0, 1, sizeof keys_song, keys_song};
    store_builtin_extra(&r);
}
int ui_song_bpm(const ui_song_t *s)
{
    int d = s->ticks * s->rpb;
    return d ? (s->tempo * 24 + d / 2) / d : 0;
}
void ui_tempo_range(const ui_song_t *s, int *lo10, int *hi10)
{
    int d = (s->ticks ? s->ticks : 6) * (s->rpb ? s->rpb : 4), lo = ((int)PL_TEMPO_MIN * 240 + d - 1) / d;
    int hi = (int)PL_TEMPO_MAX * 240 / d;
    *lo10 = lo > (int)PL_TEMPO_MIN * 10 ? lo : (int)PL_TEMPO_MIN * 10;
    *hi10 = hi < (int)PL_TEMPO_MAX * 10 ? hi : (int)PL_TEMPO_MAX * 10;
}
uint32_t ui_song_secs(const ui_song_t *s)
{
    /* a row lasts TICKS A ROW * 2.5 / TEMPO s; the time rounded */
    uint32_t rows = 0;
    for (int i = 0; i < s->npos; i++) rows += (uint32_t)ui_pattern_rows(s->order[i]);
    uint64_t num = (uint64_t)rows * s->ticks * 5, den = (uint64_t)s->tempo * 2;
    return den ? (uint32_t)((num + den / 2) / den) : 0;
}

/* ---- patterns (rec.c: the flash's parts with the edits' pieces over them) ---- */
typedef char editor_pattern_is_the_stores[UI_EDITOR_PATTERN == STORE_EDITOR_PAT ? 1 : -1];   /* (its room's number) */
static const uint8_t editor_pattern[5] = {8, 63, 1, 0, 0};     /* 8 tracks x 64 rows, EDIT STEP 1, no track */
void ui_editor_pattern_init(void)
{
    builtin_rec_t r = {REC_PATTERN, UI_EDITOR_PATTERN, 0, 1, sizeof editor_pattern, editor_pattern};
    store_builtin_extra(&r);
}
int ui_patterns(uint8_t *nums, int max)
{
    uint8_t all[256];
    int n = store_list(REC_PATTERN, all, 256), k = 0;
    for (int i = 0; i < n && k < max; i++)
        if (all[i] != UI_EDITOR_PATTERN) nums[k++] = all[i];
    return k;
}
int ui_pattern_rows(uint8_t n)
{
    uint8_t ntrk;
    uint16_t rows;
    return rec_pattern_size(n, &ntrk, &rows) ? 0 : rows;
}
int ui_pattern_tracks(uint8_t n)
{
    uint8_t ntrk;
    uint16_t rows;
    return rec_pattern_size(n, &ntrk, &rows) ? 0 : ntrk;
}

/* ---- cartridges (rec.c, as the edits make them; ENG_* are the record kinds' numbers) ---- */
int ui_cart_kind(uint8_t cart)
{
    rec_cart_t c;
    return rec_cart(cart, &c) ? ENG_NONE : c.kind;
}
int ui_cart_name(uint8_t cart, char *dst)
{
    rec_cart_t c;
    if (rec_cart(cart, &c)) { dst[0] = 0; return 0; }
    ui_name6(c.head, 0, 12, dst);
    return 1;
}
int ui_cart_slots(uint8_t cart)
{
    rec_cart_t c;
    return rec_cart(cart, &c) ? 0 : c.nslots;
}
int ui_carts(uint8_t *nums, int max)
{
    /* the six kinds share one number space: merged in numbers' order */
    int n = 0;
    for (int c = 0; c < 256 && n < max; c++) if (ui_cart_kind((uint8_t)c)) nums[n++] = (uint8_t)c;
    return n;
}
/* a slot's bytes (NULL: empty or not stored) and where its name and LEVEL are (instruments.md: each type's slot) */
static const uint16_t NAME_BIT[ENG_COUNT] = {0, 784, 123, 42, 174, 8, 121};
static const uint8_t *slot_of(uint16_t ref, int *eng)
{
    rec_cart_t c;
    *eng = ENG_NONE;
    if (rec_cart((uint8_t)(ref >> 8), &c)) return 0;
    *eng = c.kind;
    return rec_slot(&c, (uint8_t)(ref & 255));
}
int ui_inst_name(uint16_t ref, char *dst)
{
    int e;
    const uint8_t *s = slot_of(ref, &e);
    if (!s) { dst[0] = 0; return 0; }
    return ui_name6(s, NAME_BIT[e], 10, dst);
}
int ui_inst_level(uint16_t ref)
{
    int e;
    const uint8_t *s = slot_of(ref, &e);
    return s ? (int8_t)ui_bits(s, NAME_BIT[e] + 60u, 8) : 0;
}

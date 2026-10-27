/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* VISUAL (specs/ui/screens/visual.md): the voices' scopes, the output as a scope, its peaks scrolling by,
 * its spectrum (an integer FFT of 256 points), the tracks' levels. Everything is computed in the main loop
 * from a copy of the latest audio (player_master, player_scope, player_levels), a frame at a time (tick),
 * never in the audio's context; what draw needs is kept small (each scope already as its 240 or 56 points),
 * the copy and the FFT in the store's scratch, which only a mount or a seeding uses. */
#include <string.h>
#include "ui_core.h"
#include "ui_data.h"
#include "fxm.h"
#include "../play/player.h"
#include "../store/store.h"
#include "../store/seed.h"

enum { V_VOICES, V_SCOPE, V_WAVE, V_SPECTRUM, V_LEVELS, V_MASCOT, V_N };
static const txt_t VIEWS[V_N] = {TX("VOICES"), TX("SCOPE"), TX("WAVE"), TX("SPECTRUM"), TX("LEVELS"), TX("FOMA")};
static const int16_t ZOOM[6] = {128, 256, 384, 512, 768, 1024};
#define FFT_N 256
#define MASTER_N 1024
#define VS_N 56                         /* a voice's scope: points */

static struct {
    uint8_t view, zoom, gain, stereo, freeze;
    uint32_t last;                      /* the last grab */
    int16_t sc[2][240];                 /* SCOPE: each column's sample, left and right */
    uint8_t wave[240];                  /* WAVE: a frame's peak, 0-255; the newest last */
    uint8_t nwave;
    int8_t spec[FFT_N / 2];             /* SPECTRUM: each bin, dB + 128 (the floor -128 dB) */
    uint16_t lv[16];                    /* LEVELS */
    pl_voice_info_t vi[PL_VOICES_MOST]; /* (the voices heard: the pool's first 16; past them only notes fading out
                                         * of their places or parked, unheard) */
    int8_t vs[PL_VOICES_MOST][VS_N];    /* VOICES: each voice's scope, / 256 */
} vis = {.zoom = 2, .gain = 3, .stereo = 1};

/* The mascot, FOMA (specs/ui/screens/visual.md#foma; its name only in the texts): a little FM-1 with a cat's ears,
 * flying as Nyan Cat does, facing right, but bouncing round the body as a DVD player's logo does (a new colour at
 * each wall, sparkles in a corner); its tail a rainbow of six stripes straight back from it to the left edge, its
 * upper edge the left channel's wave and its lower edge the right's, reshaped once a step (a row); stars going
 * by, a star on each beat, then a cross, then a dot, half of them half a beat later */
#define MAS_W 16                        /* its picture: 16 x 12 cells of MAS_S x MAS_S pixels */
#define MAS_H 12
#define MAS_S 3
#define MAS_SC 6                        /* the stars: 6 columns x 4 bands, one in each, where in it by chance (so */
#define MAS_SR 4                        /* spread evenly), going by MAS_SV px a frame */
#define MAS_SV 3
#define MAS_LAPS 64u                    /* (the stars' run: MAS_LAPS widths, then round, the same places) */
static const char *const MAS_PIC[MAS_H] = {
    "......B.....B...",                 /* B its colour, E the ears' insides, K its screen (the face), W white (the */
    ".....BEB...BEB..",                 /* eyes, the white keys), k the black keys, P its mouth, L its legs; it */
    "BBBBBBBBBBBBBBB.",                 /* faces right, always */
    "BKKKKKKKKKKKKKB.",
    "BKKKKKKWKKKWKKB.",
    "BKKKKKKWKKKWKKB.",
    "BKKKKKKKKPKKKKB.",
    "BBBBBBBBBBBBBBB.",
    "BWkWkWWkWkWkWWB.",
    "BBBBBBBBBBBBBBB.",
    "................",
    "................"};
static const uint8_t MAS_COLS[6] = {C_LRED, C_YELLOW, C_LGREEN, C_LCYAN, C_LBLUE, C_LMAGENTA};
static const uint8_t MAS_BOW[6] = {C_LRED, C_BROWN, C_YELLOW, C_LGREEN, C_LBLUE, C_LMAGENTA};   /* the tail, top down */
static struct {
    int32_t x, y, vx, vy;               /* its top-left, Q4 (the body's pixels), and its speed a frame */
    uint8_t col, frame, hop, step;      /* its colour; frames counted; hopping on the beat; the step (stars, tail) */
    uint8_t shaped;                     /* the step SCOPE's wave was last taken at (the tail's shape: once a step) */
    uint8_t ouch;                       /* frames its eyes stay shut after hitting a wall */
    uint32_t sx;                        /* how far the stars went by, px (0 .. MAS_LAPS widths) */
    uint32_t corner_at;                 /* the last corner (ms), its sparkles for 1.5 s; 0 none yet */
    uint16_t corners;
} mas = {.x = 40 << 4, .y = (BODY_Y + 30) << 4, .vx = 32, .vy = 20};

static void mas_move(void)              /* a frame on (tick, ~30 a second): onward, off the walls, the stars by */
{
    const int32_t x1 = (240 - MAS_S * (MAS_W - 1)) << 4, y0 = BODY_Y << 4, y1 = (BODY_Y + BODY_H - MAS_S * (MAS_H - 1)) << 4;
    int wall = 0;
    mas.x += mas.vx;
    mas.y += mas.vy;
    if (mas.x <= 0 || mas.x >= x1) { mas.x = mas.x <= 0 ? 0 : x1; mas.vx = -mas.vx; wall |= 1; }
    if (mas.y <= y0 || mas.y >= y1) { mas.y = mas.y <= y0 ? y0 : y1; mas.vy = -mas.vy; wall |= 2; }
    if (mas.ouch) mas.ouch--;
    if (wall) { mas.col = (uint8_t)((mas.col + 1) % sizeof MAS_COLS); mas.ouch = 8; }   /* (a bump: its eyes shut) */
    if (wall == 3) { mas.corner_at = ui_now() | 1u; mas.corners++; }
    mas.frame++;
    mas.sx = (mas.sx + MAS_SV) % (240u * MAS_LAPS);
    player_state_t st;                  /* while something plays: a hop on the beat, the stars a step a row */
    ui_player_state(&st);
    mas.hop = (uint8_t)(st.playing && st.row % 4 == 0 && st.tick < 3);
    mas.step = st.playing ? st.row : (uint8_t)(mas.frame >> 2);
}

static void mas_star(int x, int y, int shape)   /* 0 a dot, 1 a cross, 2 a star: cells of 2 x 2 px round (x, y) */
{
    static const int8_t D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    g_rect(x - 1, y - 1, 2, 2, shape ? C_WHITE : C_LGREY);
    for (int k = 0; shape && k < 4; k++) {
        g_rect(x - 1 + 2 * D[k][0], y - 1 + 2 * D[k][1], 2, 2, shape == 2 ? C_WHITE : C_LGREY);
        if (shape == 2) g_rect(x - 1 + 4 * D[k][0], y - 1 + 4 * D[k][1], 2, 2, C_LGREY);
    }
}

static void mas_draw(void)
{
    const int bw = 240 / MAS_SC, bh = BODY_H / MAS_SR;
    for (int i = 0; i < MAS_SC * MAS_SR; i++) {         /* the night: the stars going by to the left */
        uint32_t jx = ((uint32_t)i * 2654435761u >> 16) % (uint32_t)(bw - 8);   /* (its place across: kept, so */
        uint32_t pos = (uint32_t)(i % MAS_SC * bw) + jx + 240u * MAS_LAPS - mas.sx;   /* they stay spread) */
        uint32_t h = (pos / 240u % MAS_LAPS + 1u) * 0x9E3779B1u ^ (uint32_t)(i + 1) * 0x85EBCA6Bu;   /* (each time */
        h ^= h >> 15;                                                                       /* round: anew) */
        h *= 0x2C1B3C6Du;
        h ^= h >> 12;
        int y = BODY_Y + i / MAS_SC * bh + 6 + (int)(h % (uint32_t)(bh - 12));
        static const uint8_t SHAPE[4] = {2, 1, 0, 0};   /* on its beat a star, then a cross, then a dot */
        int p = (int)((mas.step + (uint32_t)((i % MAS_SC + i / MAS_SC) & 1) * 2u) & 3u);   /* (half of them, a */
        mas_star((int)(pos % 240u), y, SHAPE[p]);                                         /* checkerboard, half a beat on) */
    }
    int x0 = mas.x >> 4, y0 = (mas.y >> 4) - (mas.hop ? 3 : 0), blink = mas.frame % 90 < 3 || mas.ouch;
    int legs = (mas.frame >> 2) & 1;
    /* the tail, from its back to the left edge, as Nyan Cat's waving rainbow: its upper edge the left channel's wave
     * and its lower edge the right's, as SCOPE has them from its back on (taken anew once a step: grab), both
     * moving the same way (a mono sound: the whole rainbow snakes, as thick as the box), each +-9 px at the louder
     * one's peak (near silence: straight); the six stripes share what lies between (6-42 px) */
    /* the edges smoothed so a busy wave (hats, noise) never tears it: each column the mean of SCOPE's 17 points
     * round it, then 1-2-1 with its neighbours (the kick's and the bass's swing, hundreds of points long, stays) */
    int16_t e[2][240 / MAS_S + 2], f[2][240 / MAS_S + 2];
    int nc = (x0 + MAS_S - 1) / MAS_S + 1, pk = 1024;   /* (16-bit: near silence stays straight) */
    for (int i = 0; i < nc; i++) {
        int k = i * MAS_S, a = k < 8 ? 0 : k - 8, b = k + 9 > 240 ? 240 : k + 9;
        for (int c = 0; c < 2; c++) {
            int32_t sum = 0;
            for (int j = a; j < b; j++) sum += vis.sc[c][j];
            e[c][i] = (int16_t)(sum / (b - a));
        }
    }
    for (int i = 0; i < nc; i++)
        for (int c = 0; c < 2; c++) {
            int v = (e[c][i > 0 ? i - 1 : 0] + 2 * e[c][i] + e[c][i + 1 < nc ? i + 1 : i]) / 4;
            f[c][i] = (int16_t)v;
            v = v < 0 ? -v : v;
            pk = v > pk ? v : pk;
        }
    for (int i = 0, x = x0; i < nc; i++, x -= MAS_S) {  /* (held at its back: the swing grows over 8 columns, 24 px) */
        int g = i < 8 ? i : 8, up = y0 + 6 - f[0][i] * 9 * g / (8 * pk), dn = y0 + 30 - f[1][i] * 9 * g / (8 * pk);
        for (int s = 0; s < 6; s++) {
            int a = up + (dn - up) * s / 6, b = up + (dn - up) * (s + 1) / 6;
            g_rect(x, a, MAS_S, b - a, MAS_BOW[s]);
        }
    }
    for (int r = 0; r < MAS_H; r++)                     /* the body */
        for (int c = 0; c < MAS_W; c++) {
            char p = MAS_PIC[r][c];
            if (r == 10) p = (c == 1 + legs || c == 4 + legs || c == 10 - legs || c == 13 - legs) ? 'L' : '.';
            if (p == '.') continue;
            if (p == 'W' && r == 4 && blink) p = 'K';   /* (a blink: the eyes' upper half shut) */
            int col = p == 'B' ? MAS_COLS[mas.col] : p == 'E' ? C_LMAGENTA : p == 'K' ? C_DGREY : p == 'W' ? C_WHITE
                    : p == 'k' ? C_BLACK : p == 'P' ? C_LMAGENTA : C_LGREY;
            g_rect(x0 + MAS_S * c, y0 + MAS_S * r, MAS_S, MAS_S, col);
        }
    int32_t since = (int32_t)(ui_now() - mas.corner_at);   /* (signed: the stamp may be a ms ahead) */
    if (mas.corner_at && since < 1500) {                /* a corner: sparkles round it, and a word */
        int t = (since < 0 ? 0 : since) / 60;
        for (int i = 0; i < 8; i++) {
            int dx = ((i & 1) ? 1 : -1) * (12 + t + (i >> 1) * 5), dy = ((i & 2) ? 1 : -1) * (8 + t + (i >> 2) * 4);
            int cx = x0 + MAS_S * MAS_W / 2 + dx, cy = y0 + MAS_S * MAS_H / 2 + dy, col = MAS_COLS[(i + t) % sizeof MAS_COLS];
            g_hline(cx - 2, cy, 5, col);
            g_vline(cx, cy - 2, 5, col);
        }
        g_text((240 - g_tw(&FONT_S, T("CORNER!"))) / 2, BODY_Y + BODY_H / 2 - 8, &FONT_S, T("CORNER!"), C_YELLOW);
    }
}

/* ------------------------------------------------------------------------ the FFT --- */
static void fft(int32_t *re, int32_t *im)       /* in place, radix 2, Q15 twiddles; halved a stage: |X| / N */
{
    for (int i = 1, j = 0; i < FFT_N; i++) {
        int bit = FFT_N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { int32_t t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= FFT_N; len <<= 1) {
        for (int k = 0; k < len / 2; k++) {
            uint32_t ph = (uint32_t)((uint64_t)k * 0x100000000ULL / (uint64_t)len);
            int32_t wr = fxm_cos(ph), wi = -fxm_sin(ph);
            for (int i = k; i < FFT_N; i += len) {
                int j = i + len / 2;
                int32_t tr = (int32_t)(((int64_t)re[j] * wr - (int64_t)im[j] * wi) >> 15);
                int32_t ti = (int32_t)(((int64_t)re[j] * wi + (int64_t)im[j] * wr) >> 15);
                re[j] = (re[i] - tr) >> 1; im[j] = (im[i] - ti) >> 1;
                re[i] = (re[i] + tr) >> 1; im[i] = (im[i] + ti) >> 1;
            }
        }
    }
}
/* the newest FFT_N frames of lr (MASTER_N), mono, a Hann window: each bin's level (dB re full scale) */
static void spectrum(const int16_t *lr, int32_t *re, int32_t *im)
{
    const int16_t *s = lr + 2 * (MASTER_N - FFT_N);
    for (int i = 0; i < FFT_N; i++) {
        int32_t w = (32768 - fxm_cos((uint32_t)((uint64_t)i * 0x100000000ULL / FFT_N))) >> 1;     /* Hann, Q15 */
        re[i] = (int32_t)(((int64_t)(s[2 * i] + s[2 * i + 1]) * w) >> 16);                     /* (L + R) / 2 x w */
        im[i] = 0;
    }
    fft(re, im);
    for (int k = 0; k < FFT_N / 2; k++) {
        uint64_t m = fxm_sqrt((uint64_t)((int64_t)re[k] * re[k] + (int64_t)im[k] * im[k]));
        int db = m ? fxm_db(m, 32768) / 256 : -128;
        vis.spec[k] = (int8_t)ui_clamp(db, -128, 0);
    }
}
static void grab(void)
{
    if (vis.freeze || seed_active()) return;
    uint8_t *scratch = store_scratch_claim(1);          /* 4 KB: the frames, then (over the oldest) the FFT */
    if (!scratch) return;                               /* (busy: a save, seeding; this frame keeps the last) */
    int16_t *lr = (int16_t *)scratch;
    /* the newest frames the player keeps (fewer than MASTER_N): at the end, the older part silent, so the
     * spectrum and the scope work on what really played */
    int got = player_master(lr, MASTER_N), base;
    got = got < 1 ? 0 : got > MASTER_N ? MASTER_N : got;
    if (got < MASTER_N) {
        memmove(lr + 2 * (MASTER_N - got), lr, (size_t)got * 4);
        memset(lr, 0, (size_t)(MASTER_N - got) * 4);
    }
    base = MASTER_N - got;
    int pk = 0;
    for (int i = 0; i < 2 * MASTER_N; i++) { int a = lr[i] < 0 ? -lr[i] : lr[i]; if (a > pk) pk = a; }
    if (vis.nwave == 240) memmove(vis.wave, vis.wave + 1, 239);
    else vis.nwave++;
    vis.wave[vis.nwave - 1] = (uint8_t)(pk >> 7);
    /* SCOPE: from the left channel's first rising zero crossing, ZOOM's window over 240 columns (within what
     * the player kept). On FOMA's view (its tail's edges) taken anew once a step only, a row while playing */
    int len = ZOOM[vis.zoom] < got ? ZOOM[vis.zoom] : got > 0 ? got : 1, z = base;
    if (vis.view != V_MASCOT || mas.shaped != mas.step) {
        mas.shaped = mas.step;
        for (int i = base + 1; i < MASTER_N - len; i++) if (lr[2 * (i - 1)] <= 0 && lr[2 * i] > 0) { z = i; break; }
        for (int x = 0; x < 240; x++) {
            int i = z + x * len / 240;
            if (i >= MASTER_N) i = MASTER_N - 1;
            vis.sc[0][x] = lr[2 * i];
            vis.sc[1][x] = lr[2 * i + 1];
        }
    }
    spectrum(lr, (int32_t *)scratch, (int32_t *)(scratch + 4 * FFT_N));     /* (the oldest frames' bytes) */
    store_scratch_claim(0);
    player_levels(vis.lv);
    int n = player_voices_now(vis.vi, PL_VOICES_MOST);
    for (int i = n; i < PL_VOICES_MOST; i++) vis.vi[i].on = 0;
    /* a voice's scope: every frame the player keeps of it (its last block, 64), across the VS_N points; only what
     * it gave is read */
    int16_t s[64];
    for (int i = 0; i < n; i++) {
        if (!vis.vi[i].on) continue;
        int got = player_scope(i, s, (int)(sizeof s / sizeof s[0]));
        for (int k = 0; k < VS_N; k++) vis.vs[i][k] = got > 0 ? (int8_t)(s[k * got / VS_N] >> 8) : 0;
    }
}
static void tick(void)
{
    uint32_t now = ui_now();
    if (now - vis.last < 33) return;    /* about 30 times a second */
    vis.last = now;
    grab();
    if (vis.view == V_MASCOT && !vis.freeze) mas_move();
    ui_dirty();
}

/* --------------------------------------------------------------------- drawing --- */
static int64_t gain_q8(void) { return vis.gain >= 3 ? (int64_t)256 << (vis.gain - 3) : 256 >> (3 - vis.gain); }
static void draw(void)
{
    const int Y0 = BODY_Y, H = BODY_H;
    int64_t g8 = gain_q8();
    if (vis.view == V_VOICES) {
        static const uint8_t ENG_OF[6] = {ENG_NONE, ENG_DX7, ENG_JUNO, ENG_TB303, ENG_SAMPLER, ENG_CHIP};   /* (PL_ENG_*) */
        player_state_t st;
        ui_player_state(&st);                           /* (a tile a voice of the limit in force: 8, 12 or 16) */
        int n = st.voice_count < 8 ? 8 : st.voice_count > PL_VOICES_MOST ? PL_VOICES_MOST : st.voice_count;
        int rows = n / 4, tw = 60, th = H / rows;
        for (int i = 0; i < n; i++) {
            int x = (i % 4) * tw, y = Y0 + i / 4 * th;
            const pl_voice_info_t *v = &vis.vi[i];
            g_box(x, y, tw - 1, th - 1, C_DGREY);
            if (i >= PL_VOICES_MOST || !v->on) { g_text(x + 3, y + 2, &FONT_T, "--", C_DGREY); continue; }
            int col = ENG_COL[ENG_OF[v->engine < 6 ? v->engine : 0]];
            char who[8], note[6];
            if (v->track == 0xFF) strcpy(who, T("KEY"));
            else if (v->track >= 16) ui_fmt(who, sizeof who, T("L%s"), ui_hex2(v->track - 16));
            else ui_fmt(who, sizeof who, T("T%s"), ui_hex2(v->track));
            g_text(x + 3, y + 2, &FONT_T, who, col);
            ui_note_name(note, v->note);
            g_text(x + tw - 22, y + 2, &FONT_T, note, C_LGREY);
            int mid2 = 2 * (y + 14) + (th - 16), amp2 = th - 18, py = 0;   /* x 2: halves */
            for (int k = 0; k < VS_N; k++) {
                int64_t s = (int64_t)vis.vs[i][k] * 256 * g8 * 6 / 256;   /* the sample x gain x 6, 32768 = 1 */
                if (s > 32768) s = 32768;
                if (s < -32768) s = -32768;
                int yy = fxm_round((int64_t)mid2 * 32768 - s * amp2, 65536);
                if (k) g_line(x + 2 + k - 1, py, x + 2 + k, yy, col);
                py = yy;
            }
        }
        return;
    }
    if (vis.view == V_MASCOT) {
        mas_draw();
        return;
    }
    if (vis.view == V_SCOPE) {
        int nt = vis.stereo ? 2 : 1;
        for (int k = 0; k < nt; k++) {
            int ch = vis.stereo ? k : -1, col = k ? C_LCYAN : C_LGREEN;
            int64_t h2 = 2 * H / nt, mid2 = 2 * Y0 + h2 * k + h2 / 2;  /* the half's height and middle, x 2 */
            g_hline(0, fxm_round(mid2, 2), 240, C_DEEP + C_DGREY);
            int py = 0;
            for (int x = 0; x < 240; x++) {
                int64_t s = ch < 0 ? (vis.sc[0][x] + vis.sc[1][x]) / 2 : vis.sc[ch][x];
                s = s * g8 * 2 / 256;
                if (s > 32768) s = 32768;
                if (s < -32768) s = -32768;
                int yy = fxm_round(mid2 * 32768 - s * (h2 / 2 - 4), 65536);   /* mid - s (h / 2 - 2), x 2 */
                if (x) g_line(x - 1, py, x, yy, col);
                py = yy;
            }
            if (vis.stereo) g_text(2, Y0 + (int)(h2 * k / 2) + 2, &FONT_T, ch ? T("R") : T("L"), C_DGREY);
        }
        return;
    }
    if (vis.view == V_WAVE) {
        int mid = Y0 + H / 2, n = vis.nwave;
        for (int x = 0; x < n; x++) {
            int64_t a = (int64_t)vis.wave[x] * 128 * g8 * 3 / (256 * 2);    /* peak x gain x 1.5, 32768 = 1 */
            if (a > 32768) a = 32768;
            int64_t h2 = a * (H - 8) / 32768;                               /* h x 2 */
            int top = fxm_round((int64_t)2 * mid - h2, 2), hh = fxm_round(h2 * 2, 2);
            g_vline(240 - n + x, top, hh < 1 ? 1 : hh, x == n - 1 ? C_WHITE : C_BROWN);
        }
        g_hline(0, mid, 240, C_DEEP + C_DGREY);
        return;
    }
    if (vis.view == V_SPECTRUM) {
        for (int b = 0; b < 40; b++) {
            /* 30 x 600^(b / 40) Hz to 30 x 600^((b + 1) / 40): log2(600) = 9.2288 */
            int64_t f0 = (int64_t)30 * (int64_t)fxm_exp2((int32_t)((int64_t)604815 * b / 40));
            int64_t f1 = (int64_t)30 * (int64_t)fxm_exp2((int32_t)((int64_t)604815 * (b + 1) / 40));
            int i0 = (int)(f0 * (FFT_N / 2) / (22050LL << 16)), i1 = (int)(f1 * (FFT_N / 2) / (22050LL << 16));
            if (i1 < i0 + 1) i1 = i0 + 1;
            int m = -128;
            for (int i = i0; i < i1 && i < FFT_N / 2; i++) if (vis.spec[i] > m) m = vis.spec[i];
            int v = m + 90 + vis.gain * 4;                                  /* dB */
            int h = ui_clamp(v, 0, 80) * (H - 16) / 80;
            g_rect(b * 6, Y0 + H - 12 - h, 5, h, b < 12 ? C_LRED : b < 26 ? C_YELLOW : C_LCYAN);
        }
        g_text(0, Y0 + H - 11, &FONT_T, "30", C_DGREY);
        g_text(108, Y0 + H - 11, &FONT_T, "700", C_DGREY);
        g_text(210, Y0 + H - 11, &FONT_T, "18K", C_DGREY);
        return;
    }
    for (int t = 0; t < 16; t++) {                      /* LEVELS: a meter for each track */
        int x = t * 15, full = H - 20;
        int64_t a = (int64_t)vis.lv[t] * g8 * 4 / 256;
        if (a > 32768) a = 32768;
        int h = (int)(a * full / 32768);
        g_rect(x + 2, Y0 + 2, 11, full, C_DEEP + C_DGREY);
        g_rect(x + 2, Y0 + H - 18 - h, 11, h, ui_muted(t) ? C_DGREY : h * 100 > full * 85 ? C_RED : C_GREEN);
        g_text(x + 1, Y0 + H - 13, &FONT_T, ui_hex2(t), ui_muted(t) ? C_DGREY : C_CYAN);
    }
}

/* ----------------------------------------------------------------- the controls --- */
static const txt_t GAIN[8] = {TX("X0.13"), TX("X0.25"), TX("X0.5"), TX("X1"), TX("X2"), TX("X4"), TX("X8"), TX("X16")};
static void knob(int e, int s, int coarse)
{
    (void)coarse;
    int d = s > 0 ? 1 : -1;
    if (e == K_SELECT) vis.view = (uint8_t)ui_clamp(vis.view + d, 0, V_N - 1);     /* the views, as pages */
    if (e == K_K1) { vis.zoom = (uint8_t)ui_clamp(vis.zoom + d, 0, 5); ui_show_knob(T("K1 ZOOM"), ui_dec(vis.zoom + 1)); }
    if (e == K_K2) { vis.gain = (uint8_t)ui_clamp(vis.gain + d, 0, 7); ui_show_knob(T("K2 GAIN"), T(GAIN[vis.gain])); }
    if (e == K_K4 && vis.view == V_MASCOT) {            /* (a touch the help keeps quiet: K4 lifts FOMA or drops it, a
                                                         * turn (24 clicks) the body's height, within it; it flies on) */
        const int32_t y0 = BODY_Y << 4, y1 = (BODY_Y + BODY_H - MAS_S * (MAS_H - 1)) << 4;
        mas.y = ui_clamp(mas.y - d * ((BODY_H << 4) / 24), y0, y1);
        ui_dirty();
    }
}
static const char *p_view(void) { return vis.stereo ? T("STEREO") : T("MONO"); }
static void p_view_change(int d, int c) { (void)d; (void)c; vis.stereo = !vis.stereo; }
static const char *p_freeze(void) { return vis.freeze ? T("ON") : T("OFF"); }
static void p_freeze_change(int d, int c) { (void)d; (void)c; vis.freeze = !vis.freeze; }
static int tap(int b)
{
    if (b == B_SEQ) { vis.view = (uint8_t)((vis.view + 1) % V_N); return 1; }
    if (b == B_GLO) {
        const ui_prop_t items[2] = {{TX("VIEW"), p_view, p_view_change, 0, 0, 0},
                                    {TX("FREEZE"), p_freeze, p_freeze_change, 0, 0, 0}};
        ui_props(T("VISUAL OPTIONS"), items, 2, B_GLO);
        return 1;
    }
    return 0;
}
static const char *sub(void) { return T(VIEWS[vis.view]); }
static const char *seq(void)
{
    static char b[8];
    ui_page_of(b, vis.view, V_N);
    return b;
}
static const char *footer(uint8_t *col)
{
    static char b[24];
    *col = C_LGREY;
    if (vis.view == V_MASCOT) {                         /* (the mascot's score: the corners hit; none yet: nothing) */
        if (mas.corners) ui_fmt(b, sizeof b, T("SCORE: %d%s"), mas.corners, vis.freeze ? T(" FROZEN") : "");
        else ui_fmt(b, sizeof b, "%s", vis.freeze ? T("FROZEN") : "");
    } else
        ui_fmt(b, sizeof b, "%s%s", T(VIEWS[vis.view]), vis.freeze ? T(" FROZEN") : "");
    return b;
}
static void enter(const ui_screen_t *from) { (void)from; vis.last = 0; }

const ui_screen_t SCR_VISUAL = {
    .name = TX("VISUAL"), .group = G_HEAR,
    .does = BIT(B_SEQ) | BIT(B_GLO),
    .enter = enter, .draw = draw, .sub = sub, .seq = seq, .footer = footer, .knob = knob, .tap = tap, .tick = tick,
};

/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The editor page's pictures (specs/editor.md, The tabs): the FM-1's own drawing code, in
 * the page's WebAssembly (fw/core built with -DPTFM_EDITOR: editor/build.py), draws an item's pictures from its bytes
 * as the FM-1's screen shows them: the same pixels, palette (the DEFAULT theme) and fonts, at the same places. Each
 * picture is the screen's own drawing run for both halves of a frame (the strip, g_band 0 and 1), its rows taken as
 * g_push sends them to the panel, then the virtual FM-1's screen put back as it was (and its next frame sent whole).
 *
 * Asked of a WebAssembly powered on (web_init: before it the texts are not there yet), the one whose library holds what
 * the picture reads beside the bytes given (below): the virtual FM-1's for its own items, the preview's (its flash image
 * mounted) for the page's others. The page writes its bytes into ed_pic_buf() (512 B), then asks for a picture. The
 * answer is a pointer to
 *   int32 x, y, w, h        the picture's rectangle on the screen (w = h = 0: no such picture, nothing drawn)
 *   uint16 px[240 * 240]    the screen's pixels, RGB565, row by row: the picture in its rectangle, black (0) elsewhere
 * (one buffer for every answer: read it before the next call).
 *
 * ed_picture(what, kind, len, inst, page, group, octave): an instrument's picture from its slot's bytes, len of them
 * at ed_pic_buf() as the cartridge's record holds a slot (kind: 1 DX7 112 B, 2 JUNO-106 28, 3 TB-303 24, 4 SAMPLER 40,
 * 5 FUSION 16, 6 CHIP 28; fewer are taken as followed by zeros); inst = the cartridge << 8 | the slot they stand for;
 * octave the keys' (-4..4, FUSION's band of the rows the keys play). what:
 *   0 DETAILS   INSTRUMENTS while browsing, the current instrument's details under the list (every kind: the DX7's
 *               summary and operators, the Juno-106's sliders and switches, the TB-303's knobs, CHIP's steps, tables
 *               and settings, a sample instrument's sample, waveform and settings, a FUSION slot's pattern and map):
 *               0, 133, 240 x 93
 *   1 EDIT      edit mode's picture with the cursor on page `page` (ALGORITHM's, from 0) and group `group` (SELECT's,
 *               from 0), as the FM-1 keeps them (clamped to the instrument's pages and groups): the DX7's algorithm, its
 *               cursor group's operator filled and that operator's envelope (OP1's where the group has none); the
 *               Juno-106's envelope and filter; the TB-303's envelope, filter and wave; CHIP's page: 0 TONE (its steps,
 *               the volume), 1 VOLUME (the envelope, the volume), 2 TABLES (the cursor row's table: group 0 VOL, 1 ARP,
 *               2 PITCH, 3 VIBRATO, 4 DUTY, 5 NOISE), 3 WAVE; a FUSION slot's pattern and map: 0, 17, 240 x 100.
 *               A sample instrument's (both pages): its waveform with the markers, its envelope over it, the key:
 *               0, 100, 240 x 127
 *   2 CHIP_ROW  CHIP's TABLES page, row `group`'s table drawn small at the row's right (rows as above):
 *               172, 147 + 12 x group, 66 x 10
 *   3 WAVE      a sample instrument's waveform as its edit picture draws it: START and END white, LOOP START and LOOP
 *               END green with a loop (an instrument made up for a sample alone shows that sample with its cut):
 *               0, 100, 240 x 72
 *   4 SAMPLES   the samples' list (ENV on SAMPLE), its cursor on the instrument's sample: that sample's waveform,
 *               176, 37 + 13 x its row, 56 x 10 (14 rows show: the row is the sample's index less the list's top,
 *               max(0, min(index - 6, samples - 14)))
 * What the store holds is read as on the FM-1: a sample instrument's samples are its cartridge's (inst's), a FUSION
 * slot's pattern and its instruments' colours the library's, so those pictures need the page's flash image holding
 * them, mounted (ed_glue.c's ed_remount); a deleted or missing sample or pattern is drawn as the FM-1 says it. The
 * yellow marks where voices play a sample are this WebAssembly's player's (none while it plays nothing).
 *
 * ed_fx_picture(effect): FX EDIT's picture of an effect (fx_pics.h; specs/ui/screens/fx.md, The pictures): 0 GATE,
 * 1 FILTER, 2 DRIVE, 3 LO-FI, 4 CHORUS, 5 DELAY, 6 REVERB, 7 LIMITER, 8 DUCK, 9 METRONOME, 10 SENDS, from its fields at
 * ed_pic_buf() as int32s (fxpic_t's):
 *   0 x, 1 y, 2 w, 3 h        where it is drawn (the rectangle answered); FX EDIT's: 2, 30 + y0, 236 x (194 - y0), y0 =
 *                             20 + 11 x the list's items (the chain as the routing shows it, SENDS' three under it with
 *                             SENDS, then DUCK and the METRONOME)
 *   4 col                     the effect's colour (a palette role, C_*), dark grey (8) while it is off
 *   5-12 k[0..7]              its knobs' values as FX EDIT shows them (a list's: its index)
 *   13 open                   FILTER: how far its envelope has it open now, Q16 of the way from CUT to fully open
 *   14 hit                    FILTER: its envelope's source hit within the last 0.1 s
 *   15 editing                GATE: its steps being edited
 *   16 step, 17 beat          GATE's playing step, the METRONOME's beat; -1 none
 *   18 bpm10                  the tempo, BPM x 10 (a song at rest: its TEMPO x 240 / (TICKS A ROW x ROWS A BEAT))
 *   19-34 dck[0..15]          DUCK: each track's DCK amount (its mixer's)
 *   35 ntrk                   DUCK: the song's tracks (its patterns' most)
 *   36 dj                     the SENDS item of DJ's setup
 *   byte 148                  DUCK: the source track's instrument's name, up to 15 characters and a 0 ("" none)
 * The FM-1 draws its knob page (`ALG 1/2`) over FILTER's and DELAY's picture, at x 194-239 of the picture's first 12
 * rows; the picture here is without it. GATE's steps are the player's patterns (the user's from the settings).
 * ed_fx_setup(effect): those fields made from an FX setup's 64 bytes at ed_pic_buf() + 256 (songs.md, the FX setup) as
 * FX EDIT makes them for a song at rest: the rectangle, the colour, the knobs; FILTER's envelope still, GATE not edited,
 * no step, no beat, bpm10 1200, DUCK's fields none, dj 0: the page puts the song's tempo and DUCK's own over them.
 * Answers the effect's knobs' count, -1 no such effect. */
#include <stdint.h>
#include <string.h>
#include "plat.h"
#include "ui/ui_core.h"
#include "ui/fx_pics.h"
#include "ui/fxdef.h"

#ifndef PTFM_EDITOR
#error "the editor's WebAssembly is built with -DPTFM_EDITOR (editor/build.py: emulator/build.py --define PTFM_EDITOR)"
#endif

#define ED_EXPORT(name) __attribute__((export_name(name)))
/* (the C names are edp_*: the firmware's own ed_* are its EDITOR's; the page knows them by their export names) */

uint16_t *web_fb_buf(void);             /* the emulator's platform: the panel's memory, where plat_lcd_rows puts rows */
/* scr_instr.c's pictures (its PTFM_EDITOR part): drawn into the band now; 0 drawn, -1 no such picture */
int scr_instr_picture(int what, int k, const uint8_t *bytes, int inst, int pg, int grp, int octave, int fresh,
                      int32_t *rect);

#define EDP_IN 512
#define EDP_SETUP 256                   /* ed_fx_setup's FX setup in the buffer */
enum { F_X, F_Y, F_W, F_H, F_COL, F_K, F_OPEN = F_K + 8, F_HIT, F_EDITING, F_STEP, F_BEAT, F_BPM10, F_DCK,
       F_NTRK = F_DCK + 16, F_DJ, F_N };   /* the FX fields, int32s; the source's name after them (byte 148) */
#define EDP_NAME (4 * F_N)

static uint8_t edp_in[EDP_IN];
static struct { int32_t r[4]; uint16_t px[G_W * G_H]; } edp_out;
static uint16_t edp_keep[G_W * G_H];    /* the virtual FM-1's screen while a picture is drawn */

ED_EXPORT("ed_pic_buf") uint8_t *edp_buf(void) { return edp_in; }

/* a picture: draw(half, rect) for both halves (rect from the first), the rows g_push sends taken from the panel's
 * memory, the rectangle's into the answer; then the screen and the theme as they were */
static uint8_t *edp_capture(int (*draw)(int half, int32_t *rect))
{
    uint16_t *fb = web_fb_buf();
    int32_t r[4] = {0, 0, 0, 0};
    int theme = g_theme_now(), ok = 1;
    memcpy(edp_keep, fb, sizeof edp_keep);
    g_theme(0);                         /* DEFAULT (and the whole frame sent) */
    g_invalidate();
    for (int half = 0; half < 2 && ok; half++) {
        g_band(half);
        g_clear(C_BLACK);
        ok = !draw(half, r);
        g_push();
    }
    memset(&edp_out, 0, sizeof edp_out);
    if (ok) {
        int x0 = r[0] < 0 ? 0 : r[0], y0 = r[1] < 0 ? 0 : r[1];
        int x1 = r[0] + r[2] > G_W ? G_W : r[0] + r[2], y1 = r[1] + r[3] > G_H ? G_H : r[1] + r[3];
        if (x1 > x0 && y1 > y0) {
            memcpy(edp_out.r, r, sizeof r);
            for (int y = y0; y < y1; y++)
                memcpy(edp_out.px + y * G_W + x0, fb + y * G_W + x0, (size_t)(x1 - x0) * sizeof *fb);
        }
    }
    memcpy(fb, edp_keep, sizeof edp_keep);
    g_theme(theme);
    g_invalidate();                     /* (the screen's next frame goes whole: the strip and its hashes were ours) */
    return (uint8_t *)&edp_out;
}

/* ---------------------------------------------------------------- an instrument's --- */
static struct { int what, kind, inst, page, group, octave; } edp_ip;
static uint8_t edp_slot[128];           /* the slot's bytes, zeros after the page's */
static int edp_ip_draw(int half, int32_t *rect)
{
    return scr_instr_picture(edp_ip.what, edp_ip.kind, edp_slot, edp_ip.inst, edp_ip.page, edp_ip.group, edp_ip.octave,
                             half == 0, rect);
}
ED_EXPORT("ed_picture") uint8_t *edp_picture(uint32_t what, uint32_t kind, uint32_t len, uint32_t inst, int32_t page,
                                             int32_t group, int32_t octave)
{
    memset(edp_slot, 0, sizeof edp_slot);
    memcpy(edp_slot, edp_in, len < sizeof edp_slot ? len : sizeof edp_slot);
    edp_ip.what = (int)what;
    edp_ip.kind = (int)kind;
    edp_ip.inst = (int)(inst & 0xFFFFu);
    edp_ip.page = page;
    edp_ip.group = group;
    edp_ip.octave = octave;
    return edp_capture(edp_ip_draw);
}

/* -------------------------------------------------------------------- FX EDIT's --- */
static fxpic_t edp_fx;
static int edp_fx_e;
static char edp_src[16];
static int32_t edp_rd(int i) { int32_t v; memcpy(&v, edp_in + 4 * i, 4); return v; }
static void edp_wr(int i, int32_t v) { memcpy(edp_in + 4 * i, &v, 4); }
static int edp_fx_draw(int half, int32_t *rect)
{
    (void)half;
    rect[0] = edp_fx.x; rect[1] = edp_fx.y; rect[2] = edp_fx.w; rect[3] = edp_fx.h;
    g_clip(0, BODY_Y, G_W, BODY_H);     /* (the body, as the FM-1's frame draws a screen) */
    fx_picture(edp_fx_e, &edp_fx);
    g_noclip();
    return 0;
}
ED_EXPORT("ed_fx_picture") uint8_t *edp_fx_picture(uint32_t e)
{
    memset(&edp_fx, 0, sizeof edp_fx);
    if (e > FE_SENDS) { memset(&edp_out, 0, sizeof edp_out); return (uint8_t *)&edp_out; }
    edp_fx.x = edp_rd(F_X); edp_fx.y = edp_rd(F_Y); edp_fx.w = edp_rd(F_W); edp_fx.h = edp_rd(F_H);
    edp_fx.col = edp_rd(F_COL) & 31;
    for (int k = 0; k < 8; k++) edp_fx.k[k] = edp_rd(F_K + k);
    edp_fx.open = (uint32_t)edp_rd(F_OPEN);
    edp_fx.hit = edp_rd(F_HIT);
    edp_fx.editing = edp_rd(F_EDITING);
    edp_fx.step = edp_rd(F_STEP);
    edp_fx.beat = edp_rd(F_BEAT);
    edp_fx.bpm10 = edp_rd(F_BPM10);
    for (int t = 0; t < 16; t++) edp_fx.dck[t] = (uint8_t)edp_rd(F_DCK + t);
    edp_fx.ntrk = edp_rd(F_NTRK);
    edp_fx.dj = edp_rd(F_DJ);
    memcpy(edp_src, edp_in + EDP_NAME, sizeof edp_src - 1);
    edp_src[sizeof edp_src - 1] = 0;
    edp_fx.src_name = edp_src;
    edp_fx_e = (int)e;
    return edp_capture(edp_fx_draw);
}

/* the fields as FX EDIT makes them from a song's setup, at rest (scr_fx.c's draw_edit and picture) */
ED_EXPORT("ed_fx_setup") int32_t edp_fx_setup(uint32_t e)
{
    const uint8_t *s = edp_in + EDP_SETUP;
    uint8_t c[12];
    int nc, sends, n = 0, nk;
    if (e > FE_SENDS) return -1;
    nc = fxd_chain(s, c);
    sends = fxd_sends(s);
    for (int i = 0; i < nc; i++) {      /* the list: the chain as the routing shows it, then DUCK and the METRONOME */
        int send = c[i] == FE_CHORUS || c[i] == FE_DELAY || c[i] == FE_REVERB;
        if (sends ? send : c[i] == FE_SENDS) continue;
        n += c[i] == FE_SENDS ? 4 : 1;
    }
    n += 2;
    int y = BODY_Y + 1 + n * 11 + 2;
    memset(edp_in, 0, EDP_SETUP);
    edp_wr(F_X, 2); edp_wr(F_Y, y + 30); edp_wr(F_W, 236); edp_wr(F_H, 226 - y - 32);
    edp_wr(F_STEP, -1); edp_wr(F_BEAT, -1); edp_wr(F_BPM10, 1200);
    if (e == FE_SENDS) return 0;
    nk = FX_EFF[e].nk;
    edp_wr(F_COL, fxd_on(s, (int)e) ? FX_EFF[e].col : C_DGREY);
    for (int k = 0; k < nk && k < 8; k++) edp_wr(F_K + k, fxd_get(s, (int)e, k));
    return nk;
}

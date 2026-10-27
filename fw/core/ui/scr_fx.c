/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* FX (specs/ui/screens/fx.md): LIVE (the punch-ins on the white keys, the effects on the black keys, the
 * picked effect's knobs), EDIT (the chain as a list, the knobs, the picture; MOVE, routing, GATE's steps and
 * patterns) and MIXER (each track's level, pan, duck amount and sends). SEQ turns the pages. What it edits:
 * fx_target.h. A knob turned takes over at once (an FX command to the player) until the song's next command
 * for it; a value a command moved off the setup's is drawn in the effect's colour. */
#include <string.h>
#include "ui_core.h"
#include "ui_data.h"
#include "ui_punch.h"
#include "fxdef.h"
#include "fx_target.h"
#include "fx_pics.h"
#include "fxm.h"
#include "../play/player.h"
#include "../play/rec.h"
#include "../play/loops.h"
#include "../fx/fx.h"

enum { LIVE, EDIT, MIXER };
static const txt_t PAGES[3] = {TX("LIVE"), TX("EDIT"), TX("MIXER")};
/* LIVE: the effects on the black keys in the FX codes' order, then BYPASS (F#5) */
static const uint8_t LIVE_X[11] = {2, 41, 86, 163, 202, 2, 41, 86, 163, 202, 2};
static const uint8_t LIVE_ROW[11] = {0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2};
#define BYPASS_KEY 10
static const uint8_t DJ_FX[5] = {FE_CHORUS, FE_DELAY, FE_REVERB, FE_LIMITER, FE_METRONOME};

static struct {
    int page, sel, kpage, last;
    int move;                           /* MOVE: the chain as it was, for HOME */
    uint8_t move_chain[12];
    int move_n, move_sel;
    int gate_edit;                      /* GATE's steps edited: what HOME puts back */
    uint8_t gate_was[16][16];           /* the user's patterns as they were */
    uint8_t gate_knob_was[4];
    int gate_changed;
    int mix_t, mix_r;
    int bypass;
    struct { uint8_t down, was, turned; } held[16];
} fx = {.page = LIVE, .last = -1};

/* ------------------------------------------------------------------- what it edits --- */
static fxt_t tg;
static uint8_t set_s[FX_SETUP_LEN], now_s[FX_SETUP_LEN];
static int now_plays;
static void refresh(void)
{
    if (!fxt_get(&tg)) memset(&tg, 0, sizeof tg);
    fxt_setup(&tg, set_s);
    now_plays = fxt_now(&tg, now_s);
}
static int is_dj(void) { return tg.what == FT_DJ; }
static int keys_song(void) { return tg.what == FT_SONG && tg.num == UI_KEYS_SONG; }
static int page_count(void)              /* (DJ's: no MIXER; the keys' song's: the KEYS column alone) */
{
    return is_dj() ? 2 : 3;
}
static int is_send(int e) { return e == FE_CHORUS || e == FE_DELAY || e == FE_REVERB; }
static int dj_has(int e) { for (int i = 0; i < 5; i++) if (DJ_FX[i] == e) return 1; return 0; }
static int live_has(int e) { return e < FE_N && (!is_dj() || dj_has(e)); }
/* the list: the chain in its order (SENDS: the SENDS item, its three indented after it; GLOBAL: the SENDS item
 * kept but not shown), then DUCK and the METRONOME, apart; DJ's: SENDS, its three, LIMITER, METRONOME */
static int items(uint8_t *out)
{
    int n = 0;
    if (is_dj()) {
        static const uint8_t D[6] = {FE_SENDS, FE_CHORUS, FE_DELAY, FE_REVERB, FE_LIMITER, FE_METRONOME};
        memcpy(out, D, 6);
        return 6;
    }
    uint8_t c[12];
    int nc = fxd_chain(set_s, c), sends = fxd_sends(set_s);
    for (int i = 0; i < nc; i++) {
        if (sends ? is_send(c[i]) : c[i] == FE_SENDS) continue;
        out[n++] = c[i];
        if (c[i] == FE_SENDS) { out[n++] = FE_CHORUS; out[n++] = FE_DELAY; out[n++] = FE_REVERB; }
    }
    out[n++] = FE_DUCK;
    out[n++] = FE_METRONOME;
    return n;
}
static int sel_e(void)
{
    uint8_t it[16];
    int n = items(it);
    fx.sel = ui_clamp(fx.sel, 0, n - 1);
    return it[fx.sel];
}
static void pick(int e)
{
    uint8_t it[16];
    int n = items(it);
    for (int i = 0; i < n; i++)
        if (it[i] == e && fx.sel != i) { fx.sel = i; fx.kpage = 0; fx.last = -1; }
}
static int nknobs(int e) { return e < FE_N ? FX_EFF[e].nk : 0; }
static int col_of(int e) { return e < FE_N ? FX_EFF[e].col : C_LGREY; }
/* a knob / switch as it plays: the value and whether a command moved it off the setup's */
static int knob_now(int e, int k, int *cmd)
{
    int v = fxd_get(now_s, e, k), s = fxd_get(set_s, e, k);
    if (cmd) *cmd = now_plays && v != s;
    return v;
}
static int on_now(int e, int *cmd)
{
    int v = fxd_on(now_s, e), s = fxd_on(set_s, e);
    if (cmd) *cmd = now_plays && v != s;
    return v;
}

/* the changes: into the record (the edit memory), and to the sound when it is what plays */
typedef struct { int e, k, v, on; } chg_t;
static void ch_knob(uint8_t *s, void *a) { chg_t *c = a; fxd_set(s, c->e, c->k, c->v); }
static void ch_on(uint8_t *s, void *a) { chg_t *c = a; fxd_set_on(s, c->e, c->on); }
static void ch_reset(uint8_t *s, void *a) { chg_t *c = a; fxd_reset(s, c->e); }
static void ch_routing(uint8_t *s, void *a) { chg_t *c = a; fxd_set_routing(s, c->v); }
static void ch_chain(uint8_t *s, void *a) { (void)a; fxd_set_chain(s, fx.move_chain, fx.move_n); }
static uint8_t chain_new[12];
static int chain_new_n;
static void ch_chain_new(uint8_t *s, void *a) { (void)a; fxd_set_chain(s, chain_new, chain_new_n); }
static int edit(void (*fn)(uint8_t *, void *), void *arg, int button)
{
    int r = fxt_edit(&tg, fn, arg, button);
    if (r == -2) ui_toast(T("MEMORY FOR EDITS IS FULL"));
    if (!r) refresh();
    return r;
}
static void sound_knob(int e, int k)
{
    if (!tg.plays) return;
    uint8_t code = fxd_code(e, k);
    if (code) player_fx_cmd(code, set_s[fxd_byte(e, k)]);
    else player_fx_reload();
}

/* ------------------------------------------------------------------------ knobs --- */
/* a knob's value in words (fxd_text); in LOOPS FILTER's SOURCE names the loop on its track (loop t plays on track t:
 * `LOOP 03`) */
static void ktext(int e, int k, int v, char *out)
{
    fxd_text(e, k, v, out);
    if (e == FE_FILTER && FX_EFF[e].k[k].fmt == KF_ESRC && tg.what == FT_LOOPS && v >= 1 && v <= loops_n())
        ui_fmt(out, 12, T("LOOP %s"), ui_hex2(v - 1));
}
static void turn_knob(int e, int ki, int s, int coarse, int button)
{
    const fx_knob_t *kd = &FX_EFF[e].k[ki];
    fx.last = ki;
    int from = knob_now(e, ki, 0);                      /* from what plays: no jump off a command's value */
    chg_t c = {e, ki, 0, 0};
    if (kd->list) c.v = ui_clamp(from + (s > 0 ? 1 : -1), 0, kd->n - 1);
    else c.v = ui_clamp(from + s * kd->step * (coarse ? 10 : 1), kd->min, kd->max);
    if (edit(ch_knob, &c, button)) return;
    sound_knob(e, ki);
    char nm[32], v[16];
    ui_fmt(nm, sizeof nm, "%s %s", T(FX_EFF[e].name), T(kd->name));
    ktext(e, ki, fxd_get(set_s, e, ki), v);
    ui_show_knob(nm, v);
}
static void gate_steps(int pat, uint8_t *steps) { fx_gate_steps(pat, steps); }

/* GATE's steps (fx.md#gate): the user's patterns (16-31) kept in the settings record */
static void gate_put(int pat, const uint8_t *steps)
{
    if (pat < 16) return;
    ui_gate_set(pat - 16, steps);
    fx.gate_changed = 1;
}

/* ------------------------------------------------------------------- the MIXER --- */
static const txt_t MIX_NAME[6] = {TX("LVL"), TX("PAN"), TX("DCK"), TX("CHO"), TX("DLY"), TX("REV")};
static const uint8_t MIX_DEF[6] = {0x7F, 0x80, 0, 0, 0, 0};
static int mix_rows(uint8_t *rows)
{
    int n = 0;
    rows[n++] = 0;
    rows[n++] = 1;
    if (fxd_on(set_s, FE_DUCK)) rows[n++] = 2;
    if (fxd_sends(set_s)) { rows[n++] = 3; rows[n++] = 4; rows[n++] = 5; }
    return n;
}
/* the columns: the tracks (the loops; none for the keys' song), then KEYS, the keys' own, one for every setup
 * (fx.md#mixer): its values ui_keys_mix's, its LVL KEYS GAIN as dB + 24 (0-36) */
static int mix_ntrk(void) { return keys_song() ? 0 : fxt_mix_cols(&tg); }
static int mix_keys(int c) { return c == mix_ntrk(); }
static void mix_get(int c, uint8_t *six)
{
    if (!mix_keys(c)) { fxt_mix_get(&tg, c, six); return; }
    memcpy(six, ui_keys_mix, 6);
    six[RM_LEVEL] = (uint8_t)(ui_set[S_KEYS_GAIN] + 24);
}
static int mix_dark(int c, int k) { return mix_keys(c) && ui_set[S_KEYS_FX] && k >= 2; }   /* (DRY: no DUCK, no sends) */
static void mix_words(char *dst, int c, int k, int v)
{
    int db = v - 24;
    if (mix_keys(c) && k == 0) ui_fmt(dst, 24, "%s%d DB", db > 0 ? "+" : "", db);
    else if (k == 1) ui_fmt(dst, 24, "%s%s", ui_hex2(v), v == 128 ? T(" MIDDLE") : v < 128 ? T(" LEFT") : T(" RIGHT"));
    else ui_fmt(dst, 24, "%s %d%%", ui_hex2(v), (v * 100 + 63) / 127);
}

/* --------------------------------------------------------------------- drawing --- */
static void draw_knob_page(int e, int y, int dark_bg)
{
    int kn = nknobs(e);
    if (kn <= 4) return;
    char pg[8], s[16];
    int pages = (kn + 3) / 4;
    ui_page_of(pg, ui_clamp(fx.kpage, 0, pages - 1), pages);
    ui_fmt(s, sizeof s, T("ALG %s"), pg);
    if (dark_bg) g_rect(194, y, 46, 12, C_BLACK);
    g_run_t r[2] = {{T("ALG"), C_YELLOW}, {0, dark_bg ? C_DGREY : C_LGREY}};
    char sp[10];
    ui_fmt(sp, sizeof sp, " %s", pg);
    r[1].s = sp;
    g_runs(238 - g_tw(&FONT_T, s), y, &FONT_T, r, 2);
}
static void draw_live(void)
{
    const int Y0 = BODY_Y;
    g_text(0, Y0 + 1, &FONT_T, ui_punch_kept() ? T("HELD ON: ITS KEY OR ANOTHER STOPS IT") : T("PUNCH-INS: HOLD A KEY; ARP HOLDS IT ON"),
           C_CYAN);
    for (int k = 0; k < 16; k++) {                      /* on chips, a row a group of four */
        int x = (k % 4) * 60, y = Y0 + 15 + k / 4 * 13, held = ui_punch_now() == k;
        g_rect(x + 1, y - 1, 58, 12, held ? C_RED : C_DEEP + C_LGREY);
        g_text(x + 3, y, &FONT_T, T(UI_PUNCH_CHIP[k]), held ? g_ink(C_RED) : C_LGREY);   /* (white, or black on a red
                                                                                          * nearer white: one-colour) */
    }
    g_hline(0, Y0 + 69, 240, C_DGREY);
    g_text(0, Y0 + 72, &FONT_T, T("EFFECTS: BLACK KEYS"), C_CYAN);
    int picked = sel_e();
    for (int k = 0; k < 11; k++) {
        if (k < 10 && !live_has(k)) continue;           /* (DJ: its own five only) */
        int y = Y0 + 86 + LIVE_ROW[k] * 15;
        const char *word = k < 10 ? T(FX_SHORT_NAME[k]) : T("BYPASS");
        int lit = k < 10 ? on_now(k, 0) && !fx.bypass : fx.bypass;
        if (k == picked) g_box(LIVE_X[k] - 3, y - 2, g_tw(&FONT_T, word) + 5, 14, C_WHITE);
        g_text(LIVE_X[k], y, &FONT_T, word, k == BYPASS_KEY ? (fx.bypass ? C_RED : C_LGREY) : lit ? col_of(k) : C_LGREY);
    }
    g_hline(0, Y0 + 131, 240, C_DGREY);
    int e = live_has(picked) ? picked : is_dj() ? FE_CHORUS : FE_GATE, kn = nknobs(e), pages = (kn + 3) / 4;
    int kp = ui_clamp(fx.kpage, 0, pages - 1), lit = on_now(e, 0) && !fx.bypass, col = col_of(e);
    g_run_t r[2] = {{T(FX_EFF[e].name), (uint8_t)(lit ? col : C_DGREY)}, {lit ? T("  ON") : T("  OFF"), (uint8_t)(lit ? C_LGREY : C_DGREY)}};
    g_runs(0, Y0 + 135, &FONT_S, r, 2);
    if (pages > 1) draw_knob_page(e, Y0 + 137, 0);
    for (int i = 0; i < 4; i++) {
        int ki = kp * 4 + i;
        if (ki >= kn) break;
        const fx_knob_t *kd = &FX_EFF[e].k[ki];
        int x = i * 60, y = Y0 + 155, v = knob_now(e, ki, 0);
        int lo = kd->list ? 0 : kd->min, hi = kd->list ? kd->n - 1 : kd->max;
        char kk[4], val[16];
        ui_fmt(kk, sizeof kk, T("K%d "), i + 1);
        g_run_t kr[2] = {{kk, C_YELLOW}, {fxd_knob_short(e, ki), C_LGREY}};
        g_runs(x + 2, y, &FONT_T, kr, 2);
        ktext(e, ki, v, val);
        val[9] = 0;
        g_text(x + 2, y + 13, &FONT_T, val, ki == fx.last ? C_WHITE : col);
        g_rect(x + 2, y + 28, 54, 3, C_DEEP + C_DGREY);
        int fill = hi > lo ? ((v - lo) * 54 * 2 + (hi - lo)) / (2 * (hi - lo)) : 0;
        g_rect(x + 2, y + 28, fill < 1 ? 1 : fill, 3, col);
    }
    g_run_t h[6] = {{T("SELECT"), C_YELLOW}, {T(" FX  "), C_LGREY}, {T("OCT"), C_YELLOW}, {T(" OFF/ON  "), C_LGREY},
                    {T("EDIT"), C_YELLOW}, {T(" ITS PICTURE"), C_LGREY}};
    g_runs(0, Y0 + 197, &FONT_T, h, 6);
}

static void picture(int e, int y, int h)
{
    fxpic_t c;
    memset(&c, 0, sizeof c);
    c.x = 2; c.y = y; c.w = 236; c.h = h;
    c.dj = is_dj();
    c.step = c.beat = -1;
    if (e < FE_N) {
        int on = on_now(e, 0);
        c.col = on ? col_of(e) : C_DGREY;
        for (int k = 0; k < nknobs(e); k++) c.k[k] = knob_now(e, k, 0);
        if (e == FE_GATE && fx.gate_edit) c.k[0] = fxd_get(set_s, FE_GATE, 0);   /* the pattern being edited */
        c.editing = fx.gate_edit;
        player_state_t st;
        ui_player_state(&st);
        c.bpm10 = st.bpm10 ? st.bpm10 : 1200;
        if (st.what == PL_NONE) {                       /* nothing played yet: the setup's song's own tempo (the keys') */
            ui_song_t s;
            if (ui_song(tg.what == FT_SONG ? tg.num : UI_KEYS_SONG, &s) && s.ticks && s.rpb)
                c.bpm10 = (int)((uint32_t)s.tempo * 240u / (s.ticks * s.rpb));
        }
        if (st.playing && on && e == FE_GATE) c.step = fx_gate_step();
        if (e == FE_FILTER && on && tg.plays && c.k[2]) {   /* its envelope as it moves (the mix's FILTER) */
            static uint32_t hits, hit_at;
            uint32_t h = fx_filter_hits();
            if (h != hits) { hits = h; hit_at = ui_now() | 1u; }
            c.open = fx_filter_open();
            c.hit = hit_at && ui_now() - hit_at < 100u;
        }
        if (e == FE_METRONOME && on && c.k[1]) {   /* the beat it clicked last: its own TEMPO's, with nothing playing too */
            c.beat = fx_met_beat();
            if (c.beat >= c.k[1]) c.beat = -1;
        }
        if (e == FE_DUCK) {
            c.ntrk = fxt_tracks_used(&tg);
            for (int t = 0; t < 16; t++) { uint8_t six[6]; fxt_mix_get(&tg, t, six); c.dck[t] = six[2]; }
            static char src[12];
            src[0] = 0;
            if (c.k[0] >= 16)                   /* (the keys: the instrument they play) */
                ui_inst_name(ui_cur.inst, src);
            else if (tg.what == FT_SONG) {      /* the source track's instrument: the first its cells name */
                ui_song_t s;
                if (ui_song(tg.num, &s)) {
                    for (int i = 0; i < s.npos && !src[0]; i++) {
                        rec_pat_t pt;
                        if (rec_pattern(s.order[i], &pt) || c.k[0] >= pt.ntrk || !pt.trk[c.k[0]]) continue;
                        rec_cur_t cu;
                        rec_cell_t cell;
                        rec_cur_set(&cu, pt.trk[c.k[0]]);
                        for (int r = 0; r < pt.rows && !src[0]; r++)
                            if (rec_cur_at(&cu, r, &cell) && cell.has & RC_INS)
                                ui_inst_name((uint16_t)(cell.cart << 8 | cell.slot), src);
                    }
                }
            }
            c.src_name = src;
        }
    }
    fx_picture(e, &c);
}
/* an activity meter's fill (fx.md#edit): q (Q15 of full scale) in dB, a pixel a dB from -60 (none) to 0 (full), 60 +
 * 20 log10(q / 32768): its log2 from the top bit and a table of the next 5 */
static const uint16_t LOG2_32[32] = {0, 182, 358, 530, 696, 858, 1016, 1169, 1319, 1465, 1607, 1746, 1882, 2015, 2145,
    2272, 2396, 2518, 2637, 2754, 2869, 2982, 3092, 3200, 3307, 3412, 3514, 3615, 3715, 3812, 3908, 4003};   /* 4096
                                                         * log2(1 + i / 32) */
static int meter_px(int32_t q)
{
    if (q <= 0) return 0;
    if (q >= 32768) return 60;
    int m = 14;
    while (!(q >> m)) m--;                              /* (q from 2^m to 2^(m + 1)) */
    int i = m >= 5 ? (q >> (m - 5)) & 31 : (q << (5 - m)) & 31;
    int32_t l2 = (m - 15) * 4096 + LOG2_32[i];          /* log2(q / 32768), Q12 */
    int px = 60 + ((l2 * 1541 + (1 << 19)) >> 20);      /* (20 log10 is 6.02 log2: 1541 / 2^20 of a Q12) */
    return px < 0 ? 0 : px;
}
static void draw_edit(void)
{
    const int Y0 = BODY_Y;
    uint8_t it[16];
    int n = items(it), sel = ui_clamp(fx.sel, 0, n - 1), sends = fxd_sends(set_s);
    for (int i = 0; i < n; i++) {
        int e = it[i], y = Y0 + 1 + i * 11, on = i == sel;
        int indent = sends && is_send(e) ? 12 : 0;
        if (e == FE_DUCK) g_hline(0, y - 1, 240, C_DGREY);
        if (on) g_box(0, y - 1, 240, 12, fx.move ? C_YELLOW : C_WHITE);
        if (e == FE_SENDS) { g_diamond(4, y - 1, C_LBLUE); g_text(14, y, &FONT_T, T("SENDS"), C_LBLUE); continue; }
        int cmd, sw = on_now(e, &cmd), col = col_of(e);
        g_text(4 + indent, y, &FONT_T, T(FX_EFF[e].name), sw ? col : C_DGREY);
        g_text(100, y, &FONT_T, sw ? T("ON") : T("OFF"), cmd ? col : sw ? C_GREEN : C_DGREY);
        g_rect(128, y + 3, 60, 5, C_DEEP + C_DGREY);   /* the activity meter: how hard it works now, in dB */
        int fill = meter_px(e == FE_DUCK ? player_duck_env() : fx_meter_q15(e));
        if (sw && fill > 0) g_rect(128, y + 3, fill, 5, col);
        if (on) {                                       /* the switch that changes it */
            g_run_t r[2] = {{sw ? T("OCT-") : T("OCT+"), C_YELLOW}, {sw ? T(" OFF") : T(" ON"), C_DGREY}};
            g_runs(190, y, &FONT_T, r, 2);
        }
    }
    int e = it[sel], y = Y0 + 1 + n * 11 + 2;
    g_hline(0, y, 240, C_DGREY);
    int kn = nknobs(e);
    if (e != FE_SENDS && kn) {
        int kp = ui_clamp(fx.kpage, 0, (kn + 3) / 4 - 1);
        for (int i = 0; i < 4; i++) {
            int ki = kp * 4 + i;
            if (ki >= kn) break;
            int x = i * 60, cmd, v = knob_now(e, ki, &cmd);
            char kk[4], sh[10], val[16];
            ui_fmt(kk, sizeof kk, T("K%d"), i + 1);
            ui_fmt(sh, sizeof sh, " %s", fxd_knob_short(e, ki));
            g_run_t r[2] = {{kk, C_YELLOW}, {sh, C_CYAN}};
            g_runs(x, y + 3, &FONT_T, r, 2);
            ktext(e, ki, v, val);
            val[9] = 0;
            g_text(x, y + 15, &FONT_T, val, cmd ? col_of(e) : ki == fx.last ? C_WHITE : C_LGREY);
        }
    }
    picture(e, y + 30, 226 - y - 32);
    if (kn > 4) draw_knob_page(e, y + 30, 1);
}
static void draw_mixer(void)
{
    const int Y0 = BODY_Y;
    uint8_t rows[6];
    int nr = mix_rows(rows), ntrk = mix_ntrk(), ncol = ntrk + 1, loops = tg.what == FT_LOOPS;
    fx.mix_r = ui_clamp(fx.mix_r, 0, nr - 1);
    fx.mix_t = ui_clamp(fx.mix_t, 0, ncol - 1);
    int used = loops ? ntrk : fxt_tracks_used(&tg), left = fx.mix_t / 8 * 8, src = fxd_get(set_s, FE_DUCK, 0);
    g_text(2, Y0 + 2, &FONT_T, loops ? T("MIXER: THE LOOPS") : T("MIXER"), C_CYAN);
    for (int k = 0; k < 8 && left + k < ncol; k++) {
        int c = left + k, x = 30 + k * 26, keys = mix_keys(c), dim = !keys && (c >= used || fxt_mix_muted(&tg, c));
        uint8_t six[6];
        mix_get(c, six);
        if (keys) g_text(x - 2, Y0 + 16, &FONT_T, T("KEYS"), c == fx.mix_t ? C_WHITE : C_CYAN);
        else g_text(x + 3, Y0 + 16, &FONT_T, ui_hex2(c), c == fx.mix_t ? C_WHITE : dim ? C_DGREY : C_CYAN);
        for (int j = 0; j < nr; j++) {
            int key = rows[j], v = six[key], y = Y0 + 30 + j * 26;
            int col = dim || mix_dark(c, key) ? C_DGREY : key == 1 ? C_LGREY : key == 0 ? C_GREEN : key == 2 ? C_BROWN : C_LBLUE;
            if (c == fx.mix_t && j == fx.mix_r) g_box(x - 2, y - 2, 24, 24, C_WHITE);
            if (key == 1) {
                g_hline(x + 1, y + 10, 18, C_DGREY);
                g_rect(x + 9 + fxm_round((int64_t)(v - 128) * 9, 128), y + 5, 2, 11, col);
            } else if (key == 2 && (keys ? src == FX_DUCK_KEYS : fxt_mix_track(&tg, c) == src)) {   /* (DUCK's source) */
                g_text(x + 7, y + 5, &FONT_T, T("S"), C_BROWN);
            } else {
                int hgt = keys && key == 0 ? (v * 18 * 2 + 36) / 72 : (v * 18 * 2 + 127) / 254;   /* (KEYS' LVL: -24 to +12 dB) */
                g_rect(x + 6, y + 2, 8, 18, C_DEEP + C_DGREY);
                g_rect(x + 6, y + 20 - hgt, 8, hgt, col);
            }
        }
    }
    for (int j = 0; j < nr; j++) g_text(2, Y0 + 35 + j * 26, &FONT_T, T(MIX_NAME[rows[j]]), j == fx.mix_r ? C_WHITE : C_CYAN);
    uint8_t six[6];
    mix_get(fx.mix_t, six);
    char w[24], line[64];
    mix_words(w, fx.mix_t, rows[fx.mix_r], six[rows[fx.mix_r]]);
    if (mix_keys(fx.mix_t))
        ui_fmt(line, sizeof line, "%s  %s %s%s", T("KEYS, EVERYWHERE"), T(MIX_NAME[rows[fx.mix_r]]), w,
               ui_set[S_KEYS_FX] ? T("  DRY") : "");
    else
        ui_fmt(line, sizeof line, "%s%s  %s %s%s", loops ? T("LOOP ") : T("TRACK "), ui_hex2(fx.mix_t), T(MIX_NAME[rows[fx.mix_r]]),
               w, fxt_mix_muted(&tg, fx.mix_t) ? T("  MUTED") : "");
    g_text(2, Y0 + 196, &FONT_T, line, C_LGREY);
}
static void draw(void)
{
    refresh();
    if (fx.page >= page_count()) fx.page = LIVE;
    if (fx.page == LIVE) draw_live();
    else if (fx.page == EDIT) draw_edit();
    else draw_mixer();
}

/* ----------------------------------------------------------------- the controls --- */
static void move_sel(int d)
{
    uint8_t c[12], vis[12];
    int nc = fxd_chain(set_s, c), nv = 0, e = sel_e(), sends = fxd_sends(set_s);
    for (int i = 0; i < nc; i++) if (!(sends ? is_send(c[i]) : c[i] == FE_SENDS)) vis[nv++] = c[i];
    int i = -1;
    for (int k = 0; k < nv; k++) if (vis[k] == e) i = k;
    int j = i + d;
    if (i < 0 || j < 0 || j >= nv || vis[j] == FE_LIMITER) return;
    int a = -1, b = -1;
    for (int k = 0; k < nc; k++) { if (c[k] == e) a = k; if (c[k] == vis[j]) b = k; }
    memcpy(chain_new, c, (size_t)nc);
    chain_new_n = nc;
    chain_new[a] = vis[j];
    chain_new[b] = (uint8_t)e;
    if (edit(ch_chain_new, 0, B_SEL)) return;
    if (tg.plays) player_fx_reload();
    pick(e);
}
static int fixed_fx(int e) { return e == FE_DUCK || e == FE_METRONOME || e == FE_LIMITER || (fxd_sends(set_s) && is_send(e)); }

static void knob(int e, int s, int coarse)
{
    refresh();
    int picked = sel_e();
    if (fx.page == LIVE && !live_has(picked)) pick(is_dj() ? FE_CHORUS : FE_GATE);     /* (EDIT's SENDS item picked) */
    if (fx.page == LIVE && e == K_SELECT) {             /* the effects in the black keys' order */
        int order[10], n = 0, at = 0;
        for (int k = 0; k < 10; k++) if (live_has(k)) order[n++] = k;
        for (int k = 0; k < n; k++) if (order[k] == sel_e()) at = k;
        pick(order[ui_clamp(at + (s > 0 ? 1 : -1), 0, n - 1)]);
        return;
    }
    if (fx.page == MIXER) {
        uint8_t rows[6];
        int nr = mix_rows(rows), ncol = mix_ntrk() + 1;
        if (e == K_ALG) fx.mix_t = ui_clamp(fx.mix_t + (s > 0 ? 1 : -1), 0, ncol - 1);
        else if (e == K_SELECT) fx.mix_r = ui_clamp(fx.mix_r + (s > 0 ? 1 : -1), 0, nr - 1);
        else if (e == K_K1) {
            uint8_t six[6];
            int key = rows[ui_clamp(fx.mix_r, 0, nr - 1)], keys = mix_keys(fx.mix_t);
            mix_get(fx.mix_t, six);
            int v = ui_clamp(six[key] + s * (coarse ? keys && key == 0 ? 6 : 16 : 1), 0, keys && key == 0 ? 36 : key == 1 ? 255 : 127);
            if (keys) ui_keys_mix_set(key, key == 0 ? v - 24 : v);   /* (a setting: every setup's) */
            else {
                int r = fxt_mix_set(&tg, fx.mix_t, key, v, -1);
                if (r == -2) ui_toast(T("MEMORY FOR EDITS IS FULL"));
                if (r) return;
            }
            char nm[16], w[24];
            ui_fmt(nm, sizeof nm, "%s %s", T(MIX_NAME[key]), keys ? T("KEYS") : ui_hex2(fx.mix_t));
            mix_words(w, fx.mix_t, key, v);
            ui_show_knob(nm, w);
        }
        return;
    }
    /* a GATE step's key held: ALGORITHM its ratchets, ON, X2, X3, X4 (every held step alike) */
    if (e == K_ALG && fx.gate_edit && fx.page == EDIT) {
        int any = 0, pat = fxd_get(set_s, FE_GATE, 0), first = -1;
        uint8_t st[16];
        gate_steps(pat, st);
        for (int k = 0; k < 16; k++) if (fx.held[k].down) {
            any = 1;
            if (first < 0) first = k;
            fx.held[k].turned = 1;
            st[k] = (uint8_t)ui_clamp(st[k] + (s > 0 ? 1 : -1), 1, 4);
        }
        if (any) {
            gate_put(pat, st);
            static const txt_t W[5] = {TX("OFF"), TX("ON"), TX("X2"), TX("X3"), TX("X4")};
            char nm[12];
            ui_fmt(nm, sizeof nm, T("STEP %d"), first + 1);
            ui_show_knob(nm, T(W[st[first]]));
            return;
        }
    }
    if (e == K_SELECT) {
        if (fx.move) { move_sel(s > 0 ? 1 : -1); return; }
        uint8_t it[16];
        int n = items(it);
        fx.sel = ui_clamp(fx.sel + (s > 0 ? 1 : -1), 0, n - 1);
        fx.kpage = 0; fx.last = -1; fx.gate_edit = 0;
        return;
    }
    int x = sel_e(), kn = nknobs(x);
    if (e == K_ALG) { fx.kpage = ui_clamp(fx.kpage + (s > 0 ? 1 : -1), 0, kn > 4 ? (kn + 3) / 4 - 1 : 0); return; }
    if (e < K_K1 || e > K_K4) return;
    int ki = fx.kpage * 4 + (e - K_K1);
    if (ki >= kn) return;
    turn_knob(x, ki, s, coarse, -1);
}

static void routing_change(int d, int c)
{
    (void)d; (void)c;
    if (is_dj()) return;
    chg_t ch = {0, 0, !fxd_sends(set_s), 0};
    if (edit(ch_routing, &ch, B_GLO)) return;
    if (tg.plays) player_fx_reload();
    ui_toast(fxd_sends(set_s) ? T("ROUTING SENDS") : T("ROUTING GLOBAL"));
}
static const char *routing_get(void) { refresh(); return fxd_sends(set_s) ? T("SENDS") : T("GLOBAL"); }
/* the keys' song's TEMPO and SWING (fx.md#fx-properties-glo): its own, as SONGS' properties have a song's (SONGS
 * never shows it); heard at once */
static const char *tempo_get(void)
{
    static char b[12];
    int t = 120, sw = 50;
    refresh();
    fxt_time(&tg, &t, &sw);
    ui_fmt(b, sizeof b, "%d BPM", t);
    return b;
}
static void tempo_change(int d, int c)
{
    int t = 120, sw = 50;
    refresh();
    if (!fxt_time(&tg, &t, &sw)) return;
    t = ui_clamp(t + (d > 0 ? 1 : -1) * (c ? 10 : 1), (int)REC_TEMPO_MIN, (int)REC_TEMPO_MAX);
    if (fxt_set_time(&tg, t, sw, B_GLO) == -2) ui_toast(T("MEMORY FOR EDITS IS FULL"));
}
static const char *swing_get(void)
{
    static char b[8];
    int t = 120, sw = 50;
    refresh();
    fxt_time(&tg, &t, &sw);
    ui_fmt(b, sizeof b, "%d %%", sw);
    return b;
}
static void swing_change(int d, int c)
{
    int t = 120, sw = 50;
    refresh();
    if (!fxt_time(&tg, &t, &sw)) return;
    sw = ui_clamp(sw + (d > 0 ? 1 : -1) * (c ? 5 : 1), 50, 75);
    if (fxt_set_time(&tg, t, sw, B_GLO) == -2) ui_toast(T("MEMORY FOR EDITS IS FULL"));
}
/* KEYS, EVERYWHERE: SETTINGS' KEYS, for every setup (fx.md#the-keys-dry): the keys heard through the effects, or dry */
static const char *keys_get(void) { return T(UI_KEYS_FX[ui_set[S_KEYS_FX] ? 1 : 0]); }
static void keys_change(int d, int c)
{
    (void)d; (void)c;
    ui_set[S_KEYS_FX] = (int16_t)!ui_set[S_KEYS_FX];
    ui_settings_changed();
    ui_settings_apply(S_KEYS_FX);
    ui_toast(ui_set[S_KEYS_FX] ? T("KEYS DRY") : T("KEYS THROUGH FX"));
}
static int menu_e;
static void revert_go(void)                             /* (specs/ui/saving.md#revert; what plays: from its next block) */
{
    refresh();
    if (fxt_revert(&tg) == -2) return;
    refresh();
    if (tg.plays) player_fx_reload();
}
static void menu_pick(int id)
{
    refresh();
    int e = menu_e;
    if (id == 1) {                                      /* MOVE */
        if (fixed_fx(e)) { ui_refuse(B_SEL); ui_toast(ui_txt_v(T("# DOES NOT MOVE"), T(FX_EFF[e].name))); return; }
        fx.move = 1;
        fx.move_n = fxd_chain(set_s, fx.move_chain);
        fx.move_sel = fx.sel;
        ui_toast(T("SELECT MOVES IT, SEL PUTS IT DOWN"));
    } else if (id == 2 && e < FE_N) {                   /* RESET */
        chg_t c = {e, 0, 0, 0};
        if (!edit(ch_reset, &c, B_SEL) && tg.plays) player_fx_reload();
    } else if (id == 3) {                               /* REVERT: every effect as saved, after a question */
        static char l1[32];
        ui_fmt(l1, sizeof l1, T("FOR %s"), tg.name);
        ui_ask_revert(T("REVERT THE EFFECTS TO SAVED?"), l1, revert_go);
    }
}

/* GATE's patterns (ENV on GATE): SELECT picks, ENV takes it */
static int gl_sel;
static void gl_knob(ui_overlay_t *o, int e, int s, int coarse)
{
    (void)o; (void)coarse;
    if (e == K_SELECT) gl_sel = ui_clamp(gl_sel + s, 0, 31);
}
static int gl_tap(ui_overlay_t *o, int b)
{
    if (b != B_ENV) return 0;
    ui_close(o);
    chg_t c = {FE_GATE, 0, gl_sel, 0};
    if (!edit(ch_knob, &c, B_ENV)) sound_knob(FE_GATE, 0);
    return 1;
}
static void gl_draw(ui_overlay_t *o)
{
    (void)o;
    const int Y0 = BODY_Y;
    ui_panel(2, Y0 + 2, 236, 206, T("GATE PATTERNS"), C_CYAN);
    int top = ui_clamp(gl_sel - 6, 0, 32 - 14);
    for (int i = top; i < top + 14; i++) {
        int y = Y0 + 18 + (i - top) * 13;
        uint8_t st[16];
        char nb[16], line[24];
        gate_steps(i, st);
        if (i == gl_sel) g_box(4, y - 1, 232, 13, C_WHITE);
        ui_fmt(line, sizeof line, "%s %s", ui_hex2(i), fxd_gate_name(i, nb));
        g_text(8, y, &FONT_T, line, i < 16 ? C_LGREY : C_LBLUE);
        for (int k = 0; k < 16; k++) g_rect(130 + k * 6, y + 2, 4, 7, st[k] ? C_LBLUE : C_DEEP + C_DGREY);
    }
}

static int tap(int b)
{
    refresh();
    int e = sel_e();
    if (b == B_SEQ) {
        fx.page = (fx.page + 1) % page_count();
        fx.gate_edit = 0; fx.move = 0;
        return 1;
    }
    if (b == B_ARP) return ui_punch_arp();              /* a punch-in's key held: held on (else the arpeggiator) */
    if (b == B_GLO) {                                   /* (titled whose setup it is: FX PROPERTIES FOR, its name) */
        ui_prop_t items_[4];
        int n = 0;
        if (keys_song()) {
            items_[n++] = (ui_prop_t){TX("TEMPO"), tempo_get, tempo_change, 0, 0, 0};
            items_[n++] = (ui_prop_t){TX("SWING"), swing_get, swing_change, 0, 0, 0};
        }
        items_[n++] = (ui_prop_t){TX("ROUTING"), routing_get, is_dj() ? 0 : routing_change, 0, 0, 0};
        if (!is_dj()) items_[n++] = (ui_prop_t){TX("KEYS, EVERYWHERE"), keys_get, keys_change, 0, 0, 0};   /* (DJ: no notes) */
        ui_props(T("FX PROPERTIES FOR"), items_, n, B_GLO);
        ui_props_for(tg.name);
        return 1;
    }
    if (fx.page == MIXER) {
        if (b == B_LFO) {
            uint8_t rows[6];
            mix_rows(rows);
            int key = rows[ui_clamp(fx.mix_r, 0, 5)];
            if (mix_keys(fx.mix_t)) {                   /* (KEYS: 0 DB, 80, 00, the sends 7F) */
                ui_keys_mix_set(key, key == 0 ? 0 : UI_KEYS_MIX_DEF[key]);
                return 1;
            }
            int r = fxt_mix_set(&tg, fx.mix_t, key, MIX_DEF[key], B_LFO);
            if (r == -2) ui_toast(T("MEMORY FOR EDITS IS FULL"));
            return 1;
        }
        if (b == B_OCTM || b == B_OCTP) return 0;
        return b == B_SEL || b == B_EDIT || b == B_ENV;
    }
    if ((b == B_OCTP || b == B_OCTM) && e < FE_N) {
        chg_t c = {e, 0, 0, b == B_OCTP};
        if (edit(ch_on, &c, b)) return 1;
        if (tg.plays) player_fx_cmd(b == B_OCTP ? 0xAE : 0xAF, (uint8_t)e);
        ui_toast(ui_txt_v(b == B_OCTP ? T("# ON") : T("# OFF"), T(FX_EFF[e].name)));
        return 1;
    }
    if (b == B_LFO && fx.last >= 0 && e < FE_N && fx.last < nknobs(e)) {
        chg_t c = {e, fx.last, FX_EFF[e].k[fx.last].def, 0};
        if (edit(ch_knob, &c, B_LFO)) return 1;
        sound_knob(e, fx.last);
        char nm[32], v[16];
        ui_fmt(nm, sizeof nm, "%s %s", T(FX_EFF[e].name), T(FX_EFF[e].k[fx.last].name));
        ktext(e, fx.last, fxd_get(set_s, e, fx.last), v);
        ui_show_knob(nm, v);
        return 1;
    }
    if (b == B_SEL) {
        if (fx.move) { fx.move = 0; ui_toast(T("MOVED")); return 1; }
        static ui_mitem_t m[3];
        int n = 0;
        if (!is_dj() && fx.page == EDIT) m[n++] = (ui_mitem_t){TX("MOVE"), 1, 0, 0};
        m[n++] = (ui_mitem_t){TX("RESET"), 2, 0, 0};
        if (fxt_unsaved(&tg)) m[n++] = (ui_mitem_t){TX("REVERT"), 3, 0, 0};
        static char title[24];
        ui_fmt(title, sizeof title, "%s", e == FE_SENDS ? T("SENDS") : T(FX_EFF[e].name));
        menu_e = e;
        ui_menu("fx", title, m, n, menu_pick, B_SEL);
        return 1;
    }
    if (b == B_EDIT && fx.page == LIVE) { fx.page = EDIT; fx.gate_edit = 0; fx.move = 0; return 1; }
    if (b == B_EDIT && e == FE_GATE && fx.page == EDIT) {
        if (fx.gate_edit) { fx.gate_edit = 0; return 1; }                   /* keeping the changes */
        /* the session: HOME puts back the user's patterns and GATE's knobs as they were */
        for (int i = 0; i < 16; i++) gate_steps(16 + i, fx.gate_was[i]);
        for (int k = 0; k < 4; k++) fx.gate_knob_was[k] = (uint8_t)fxd_get(set_s, FE_GATE, k);
        fx.gate_changed = 0;
        int pat = fxd_get(set_s, FE_GATE, 0);
        if (pat < 16) {                                 /* a built-in pattern: edit a copy among yours */
            uint8_t st[16], g[16];
            gate_steps(pat, st);
            int to = 16;
            for (int i = 16; i < 32; i++) {
                int on = 0;
                gate_steps(i, g);
                for (int k = 0; k < 16; k++) on += g[k] != 0;
                if (on <= 1) { to = i; break; }
            }
            gate_put(to, st);
            chg_t c = {FE_GATE, 0, to, 0};
            if (!edit(ch_knob, &c, B_EDIT)) sound_knob(FE_GATE, 0);
            char a[16], bb[16], line[48];
            ui_fmt(line, sizeof line, T("%s COPIED TO %s"), fxd_gate_name(pat, a), fxd_gate_name(to, bb));
            ui_toast(line);
        }
        fx.gate_edit = 1;
        memset(fx.held, 0, sizeof fx.held);
        return 1;
    }
    if (b == B_ENV && e == FE_GATE && fx.page == EDIT) {
        gl_sel = fxd_get(set_s, FE_GATE, 0);
        ui_overlay_t o = {.kind = OV_LIST, .opener = B_ENV, .knob = gl_knob, .tap = gl_tap, .draw = gl_draw};
        ui_open(&o);
        return 1;
    }
    return 0;
}
static int key(int n, int down)
{
    refresh();
    if (fx.page == LIVE) {                              /* LIVE: the keys play no notes */
        int wi = ui_white_index(n), bi = ui_black_index(n);
        if (wi >= 0) { if (down) ui_punch_down(wi); else ui_punch_up(wi); }
        else if (bi == BYPASS_KEY) { fx.bypass = down; player_fx_bypass(down); }
        else if (bi >= 0 && down && live_has(bi)) pick(bi);
        return 1;
    }
    /* GATE's steps: a white key switches its step; an OFF step comes on as the key goes down, an ON one goes
     * off as it comes up, unless ALGORITHM turned meanwhile */
    int wi = ui_white_index(n);
    if (fx.gate_edit && fx.page == EDIT && wi >= 0) {
        int pat = fxd_get(set_s, FE_GATE, 0);
        uint8_t st[16];
        gate_steps(pat, st);
        if (down) {
            fx.held[wi].down = 1;
            fx.held[wi].was = st[wi];
            fx.held[wi].turned = 0;
            if (!st[wi]) { st[wi] = 1; gate_put(pat, st); }
        } else {
            int was = fx.held[wi].was, turned = fx.held[wi].turned;
            fx.held[wi].down = 0;
            if (was && !turned) { st[wi] = 0; gate_put(pat, st); }
        }
        return 1;
    }
    return 0;
}
static int key_leds(uint8_t *keys)
{
    refresh();
    if (fx.page == LIVE) {
        ui_punch_leds(keys);
        int picked = sel_e();
        for (int i = 0; i < 11; i++)
            keys[UI_BLACK[i]] = (uint8_t)(i == BYPASS_KEY ? (fx.bypass ? LV_FULL : 0) : !live_has(i) ? 0
                                          : i == picked ? LV_FULL : on_now(i, 0) ? LV_DIM : 0);
        return 1;
    }
    if (!fx.gate_edit) return 0;
    /* GATE's steps: OFF dark, ON half, a ratchet blinks its count (X2 twice ...), then a pause, over and over;
     * the playing step's key dark as the playhead passes */
    uint8_t st[16];
    gate_steps(fxd_get(set_s, FE_GATE, 0), st);
    uint32_t cyc = ui_now() % 1400u;
    player_state_t ps;
    ui_player_state(&ps);
    int play = ps.playing && on_now(FE_GATE, 0) ? fx_gate_step() : -1;
    for (int i = 0; i < 16; i++) {
        int v = st[i];
        keys[UI_WHITE[i]] = (uint8_t)(i == play ? 0 : v == 0 ? 0 : v == 1 ? 128
                                      : cyc < (uint32_t)v * 240u && cyc % 240u < 140u ? LV_FULL : LV_DIM);
    }
    return 1;
}
static int lit(int b)
{
    refresh();
    int e = sel_e();
    if (b == B_EDIT && fx.gate_edit) return LV_FULL;
    if (b == B_SEL && fx.move) return LV_FULL;
    if (b == B_OCTM || b == B_OCTP) {
        if (fx.page == MIXER || e >= FE_N) return 0;
        return fxd_on(set_s, e) == (b == B_OCTP) ? LV_FULL : LV_DIM;
    }
    if (b == B_EDIT && fx.page == LIVE) return LV_DIM;
    if (b == B_EDIT || b == B_ENV) return fx.page == EDIT && e == FE_GATE ? LV_DIM : 0;
    return LV_NONE;
}
static int back(void)
{
    if (fx.move) {                                      /* HOME: the chain as it was */
        refresh();
        fx.move = 0;
        uint8_t c[12];
        int n = fxd_chain(set_s, c);
        if (n != fx.move_n || memcmp(c, fx.move_chain, (size_t)n)) {
            if (!edit(ch_chain, 0, B_HOME) && tg.plays) player_fx_reload();
        }
        fx.sel = fx.move_sel;
        ui_toast(T("NOT MOVED"));
        return 1;
    }
    if (fx.gate_edit) {                                 /* HOME: dropping the changes */
        fx.gate_edit = 0;
        if (fx.gate_changed) {
            for (int i = 0; i < 16; i++) gate_put(16 + i, fx.gate_was[i]);
            for (int k = 0; k < 4; k++) {
                if (fxd_get(set_s, FE_GATE, k) == fx.gate_knob_was[k]) continue;
                chg_t c = {FE_GATE, k, fx.gate_knob_was[k], 0};
                if (!edit(ch_knob, &c, B_HOME)) sound_knob(FE_GATE, k);
            }
            ui_toast(T("CHANGES DROPPED"));
        }
        return 1;
    }
    return 0;
}
static void enter(const ui_screen_t *from)
{
    fxt_from(from ? T(from->name) : 0);
    refresh();
    if (fx.page >= page_count()) fx.page = LIVE;
    if (is_dj() && !dj_has(sel_e())) { pick(FE_CHORUS); fx.kpage = 0; fx.last = -1; }
}
/* the effect under the cursor is the hand's (specs/ui/screens/fx.md#the-fx-setup): on LIVE and EDIT, while what it
 * edits plays, the player told which (its knobs' codes); nothing else turns it, the other effects follow their
 * commands. Told again on a change, and every half second (a request lost to a full queue) */
static void mine_send(int e)
{
    static int sent = -2;
    static uint32_t at;
    if (e == sent && (int32_t)(ui_now() - at) < 500) return;
    uint64_t codes = 0;
    for (int k = 0; e >= 0 && k < nknobs(e); k++) {
        uint8_t c = fxd_code(e, k);
        if (c >= 0x90 && c <= 0xBF) codes |= 1ull << (c - 0x90);
    }
    player_fx_mine(e >= 0 ? (uint8_t)e : 0xFF, codes);
    sent = e;
    at = ui_now();
}
static void tick(void)
{
    int e = fx.page != MIXER && tg.plays ? sel_e() : -1;
    mine_send(e < FE_N ? e : -1);
    fx_watch(fx.page == EDIT);                          /* (the activity meters: measured only while EDIT shows) */
}
static void editing(uint8_t *what, uint32_t *key)       /* the MIXER: the mixer shown, the song's or the loops' */
{
    if (fx.page != MIXER || !tg.plays) return;
    *what = PL_ED_MIX;
    *key = tg.what == FT_LOOPS ? 0xFFFF0000u : 0x0000FFFFu;
}
static void leave(const ui_screen_t *to)
{
    (void)to;
    mine_send(-1);
    fx_watch(0);
    fx.gate_edit = 0;
    if (fx.bypass) { fx.bypass = 0; player_fx_bypass(0); }
    if (fx.page == LIVE && !ui_punch_kept()) ui_punch_off();   /* (one ARP holds on plays on) */
    fxt_from(0);
}
static const char *sub(void) { return T(PAGES[fx.page]); }
static const char *seq(void)
{
    static char b[8];
    ui_page_of(b, fx.page, page_count());
    return b;
}
static const char *footer(uint8_t *col)
{
    static char b[24];
    refresh();
    *col = C_LGREY;
    if (fx.page == MIXER && mix_keys(fx.mix_t)) return T("KEYS");
    if (fx.page == MIXER) { ui_fmt(b, sizeof b, "%s%s", tg.what == FT_LOOPS ? T("LOOP ") : T("TRACK "), ui_hex2(fx.mix_t)); return b; }
    int e = sel_e();
    if (e >= FE_N) return 0;
    ui_fmt(b, sizeof b, T("%s %d.%d%% CPU"), T(FX_EFF[e].name), FX_EFF[e].cpu10 / 10, FX_EFF[e].cpu10 % 10);
    return b;
}
static int sel_lines(const char **lines, int max)
{
    if (max < 1) return 0;
    uint8_t rows[6];
    int keys_lvl = fx.page == MIXER && mix_keys(fx.mix_t) && rows[ui_clamp(fx.mix_r, 0, mix_rows(rows) - 1)] == 0;
    lines[0] = keys_lvl ? T("K1: BY 6 DB") : fx.page == MIXER ? T("K1: BY 16") : T("K1-K4: BIG STEPS");
    if (fx.page != EDIT || max < 2) return 1;
    lines[1] = T("OCT: THE OCTAVE");                    /* (SEL + OCT: has_combo) */
    return 2;
}

/* SEL + OCT on EDIT, where the keys play notes: the keys' octave (OCT alone switches the picked effect there) */
static int has_combo(int held, int b) { return held == B_SEL && (b == B_OCTM || b == B_OCTP) && fx.page == EDIT; }
static int combo(int held, int b)
{
    if (!has_combo(held, b)) return 0;
    ui_octave(b == B_OCTP ? 1 : -1);
    return 1;
}

/* the state's cursor (specs/connections.md#the-state): the page (LIVE, EDIT, MIXER), the picked effect's number, the
 * effects switched on in the setup it edits (as it shows ON / OFF, a bit each by number) */
static void cursor(uint8_t *c)
{
    uint32_t on = 0;
    for (int e = 0; e < FE_N; e++)
        if (on_now(e, 0)) on |= 1u << e;
    c[0] = (uint8_t)fx.page;
    c[1] = (uint8_t)sel_e();
    c[2] = (uint8_t)on;
    c[3] = (uint8_t)(on >> 8);
}

const ui_screen_t SCR_FX = {
    .name = TX("FX"), .group = G_PLAY,
    .does = BIT(B_SEL) | BIT(B_GLO) | BIT(B_SEQ) | BIT(B_LFO) | BIT(B_EDIT) | BIT(B_ENV) | BIT(B_ARP) | BIT(B_OCTM) | BIT(B_OCTP),
    .combines = BIT(B_ARP),
    .enter = enter, .leave = leave, .draw = draw, .sub = sub, .seq = seq, .footer = footer, .knob = knob, .tap = tap,
    .key = key, .back = back, .lit = lit, .key_leds = key_leds, .sel_lines = sel_lines, .cursor = cursor, .tick = tick,
    .editing = editing, .combo = combo, .has_combo = has_combo,
};

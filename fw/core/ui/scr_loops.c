/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* LOOPS (specs/ui/screens/loops.md), a loop station; the playground's is playground/scr-loops.js. Up to 16 loops over one
 * length, always set (play/loops.h: the store's pieces), each one instrument and colour, MONO or POLY, its notes once
 * (its base), a repeat count and a SHIFT. THE STAGE: one loop of the length belonging to no loop (the store's piece of
 * its own: store_stage), heard in the keys' instrument and mode through the mix REC would put it in; while the loops play
 * the keys overdub it (each note landed by QUANTIZE, written into it and asked of the player's stage keys' lanes, its
 * span replacing what the stage had as the line passes; LFO's tap switches to OVERWRITE and back), and REC
 * sends it where the selection is (a new loop on + NEW LOOP, into the loop on a loop); stopped and empty, REC arms.
 * EDIT copies the selected loop onto the stage, held or on + NEW LOOP it clears it; ALGORITHM is the play position
 * (playing: a jump); K1-K4 their jobs from GLO; SEQ: the keys PLAY or CONTROL (mutes and solos, now or at the bar line;
 * QUANTIZE; MONO / POLY); ARP's tap the stage off and on (off: kept, dimmed, not heard, the selected loop heard, the keys
 * heard at once in its sound and written nowhere), ARP held + REC (or SEL's RECORD PATTERN) the pattern recorder
 * (lp_rec.c); GLO the properties; SEL the menu (ADJUST: a SHIFT); ENV the cartridges. A screen of the round;
 * the loops and the stage play on outside it, and what plays plays on into it until the loops start
 * (specs/ui/navigation.md). */
#include <string.h>
#include "ui_core.h"
#include "ui_priv.h"
#include "ui_data.h"
#include "ui_lp.h"
#include "ui_punch.h"
#include "ui_ed.h"
#include "ed_pat.h"
#include "ed_inst.h"
#include "../play/player.h"
#include "../play/loops.h"
#include "../play/live.h"
#include "../play/rec.h"
#include "../store/store.h"

const uint8_t LP_COL[12] = {C_LRED, C_YELLOW, C_LGREEN, C_LCYAN, C_LBLUE, C_LMAGENTA, C_BROWN, C_GREEN, C_CYAN, C_RED,
                            C_MAGENTA, C_LGREY};
static const txt_t QUANTS[5] = {TX("ROW"), TX("2 ROWS"), TX("3 ROWS"), TX("4 ROWS"), TX("REAL TIME")};   /* LQ_* */
static const txt_t QTOP[5] = {TX("Q ROW"), TX("Q 2 ROWS"), TX("Q 3 ROWS"), TX("Q 4 ROWS"), TX("Q REAL")};
static const uint8_t QROWS[5] = {1, 2, 3, 4, 0};        /* QUANTIZE's lines (REAL TIME: none) */
static const uint8_t QKEY[5] = {LQ_2_ROWS, LQ_3_ROWS, LQ_4_ROWS, LQ_ROW, LQ_REAL};   /* OP1-OP5's, and K1's order */
#define REC_QUIET_MS 500               /* REC's wait after a job (loops.md#rec-edit-and-the-arm) */
#define STAGE_MOST STORE_LOOP_STREAM_MAX   /* the stage's stream at most, its 0x00 counted (3,800 B) */

/* arm: stopped, the stage empty, REC armed (a key or PLAY starts the loops), counting in (PLAY started them).
 * RECORD LIVE (play/live.h): armed by its panel (SEL's menu); counting in (PLAY started it); recording */
enum { ARM_NONE, ARM_WAIT, ARM_COUNT };
enum { SG_NONE, SG_ARMED, SG_COUNT, SG_ON };
/* key[n]: a key's note, its lane + 1 (0 none), bit 3 its note in the pass after the line's, its note delay << 4; krow[n]
 * its row; kpass[n] the rows its span has replaced to. cur: MONO, the key whose note the lane holds (-1 none).
 * The stage's line (the loops' pass it was handled for) and its run (1 playing, 2 started here, not yet seen playing:
 * run_h the halves then); own: the cells the keys wrote in the line's pass (a bit a stream row), own_nx in the next
 * (their own spans never replace them); ers: OVERWRITE's eraser (1 on, 2 in the next pass), its row and the rows it has
 * replaced to; over: OVERWRITE on (LFO's tap; EDIT's and the pattern recorder's fills end it; 0 at every start, never
 * saved); told: each lane's hold told the player (row | 128 the next pass; 0xFF free, 0xFE to tell again); tell: the
 * stage's setup told. wm / ws: loops whose mute / solo toggles at the next bar line (told to the player: wsent; its wait
 * seen: wseen); held: CONTROL's white keys down, a bit a key (done: held past the hold delay). quiet: REC's wait, its start
 * (ms, all 32 bits: no wrap; 0 none). kept: the play position, where PLAY starts (the row playing; paused, where they were;
 * ALGORITHM's while stopped); pl_seen: the loops seen playing; start_row: where they started; plan: a shorter LENGTH
 * planned while GLO is open (rows); st_rows: the length the stage is made for; adj: ADJUST open (adj_was: the SHIFT
 * before); off: the stage switched off (ARP's tap; 0 at every start, never saved); ol: the keys while it is off, the key
 * each of its lanes holds (PL_LP_STAGE_KEYS' 4 + lane), | 0x80 the MONO one, 0xFF free; onext: the lane taken next
 * when all are held */
static struct {
    uint32_t quiet;
    uint8_t control, solo, top, arm, adj, adj_was, plan, st_rows, kept, pl_seen, start_row, run, run_h, jumps, over;
    uint8_t ers, ers_row, ers_to, off, onext;
    uint8_t push;                       /* OVERDUB: a bit a lane, a note from before moved on with the key holding the lane,
                                         * at its kpass (dub_on) */
    uint8_t ol[PL_LP_OFF_LANES];
    int8_t cur;
    uint16_t line;
    uint8_t told[4];
    pl_lp_stage_t tell;
    uint8_t own[64];
    uint16_t own_nx[4];
    uint8_t key[27], krow[27], kpass[27];
    uint16_t wm, ws, held, done;
    uint8_t wsent, wseen, wsent_h;      /* (wsent_h: the halves' count, low byte, when told) */
    uint16_t held_at[16];
    uint8_t song;                       /* RECORD LIVE: SG_* */
    uint8_t splay;                      /* the loops played as it was armed (PLAY does not start it then) */
    uint8_t pdefer, phold;              /* PLAY pressed while a song records: acted on as it is let go; its hold came */
    int16_t sat;                        /* the song's row heard last */
    int16_t hrow;                       /* the row HOME was pressed in while a song records, as a tap would end it (-1 none) */
    uint8_t cart;                       /* ENV's list: the cartridge highlighted */
    uint8_t pat;                        /* SAVE TO / LOAD FROM PATTERN: the pattern picked */
    uint8_t spp_on, spp_row;            /* SYNC EXTERNAL: a song position while they stood, where CONTINUE starts them */
} lp;
void lp_init(void)                      /* (set at the start, not by an initialiser: lp stays out of the image) */
{
    lp.cur = -1;
    lp.hrow = -1;
    memset(lp.told, 0xFF, sizeof lp.told);
    memset(&lp.tell, 0xFF, sizeof lp.tell);   /* (pl_lp_stage_t: 6 bytes, all of them 0xFF) */
    memset(lp.own_nx, 0xFF, sizeof lp.own_nx);
    memset(lp.ol, 0xFF, sizeof lp.ol);
}

/* ------------------------------------------------------------------------ time --- */
static void state(player_state_t *st) { ui_player_state(st); }
static int speed(const player_state_t *st) { return st->speed ? st->speed : loops_ticks(); }
static int fdiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static int on_loop(void) { return loops_sel() < loops_n(); }
static int top_sel(void) { return loops_n() < LP_MAX - 1 ? loops_n() : LP_MAX - 1; }   /* (16 loops: no + NEW LOOP) */
static int slots(void) { return loops_n() + (loops_n() < LP_MAX ? 1 : 0); }
static int target(void) { return on_loop() ? loops_sel() : loops_n(); }    /* where REC sends the stage */
int lp_playing_here(void) { player_state_t st; state(&st); return st.what == PL_LOOPS && st.playing; }
static int counting(void) { player_state_t st; state(&st); return st.count_beats != 0; }
/* the loops heard now: their row, pass, tick, a row's ticks, the place in ticks; 0 while they do not play (counting in,
 * held for the keys, kept silent by a song: the stage's line stands) */
typedef struct { int row, pass, tick, sp, pos, halves; } here_t;
static int here(here_t *h)
{
    player_state_t st;
    state(&st);
    h->sp = speed(&st);
    h->row = st.row;
    h->pass = st.lp_pass;
    h->tick = st.tick;
    h->pos = st.row * h->sp + st.tick;
    h->halves = (int)st.halves;
    return st.what == PL_LOOPS && st.playing && !st.lcue && !st.count_beats && !st.lp_rest;
}
static int rec_any(void) { return lp.arm || lp.song == SG_ON; }      /* the header's red ● */
void lp_loop_name(char *dst, int i) { ui_fmt(dst, 8, T("LOOP %s"), ui_hex2(i)); }
/* the instrument's name; an unused slot's EMPTY */
static void inst_name(uint16_t ref, char *dst) { if (!ui_inst_name(ref, dst)) strcpy(dst, T("EMPTY")); }
int lp_next_colour(void) { int n = loops_n(); return n ? (loops_colour(n - 1) + 1) % 12 : 0; }
static const char *bars_text(int b) { return b == 1 ? T("1 BAR") : ui_txt_v(T("# BARS"), ui_dec(b)); }
/* the length as shown: whole bars (`4 BARS`, `1 BAR`); a part bar (BEATS A BAR changed under it) in its beats, whole as
 * the length's rows always are (`21 BEATS`); the pattern recorder's REPEATS too */
const char *lp_len_text(int rows)
{
    int br = loops_bar_rows(), rpb = loops_rpb();
    if (rows % br == 0) return bars_text(rows / br);
    if (rows % rpb == 0) return rows == rpb ? T("1 BEAT") : ui_txt_v(T("# BEATS"), ui_dec(rows / rpb));
    return ui_txt_v(T("# ROWS"), ui_dec(rows));
}
static void bar_beat(char *b, int r)    /* a row as BAR.BEAT (.STEP within the beat when not on it): `2.4.3` */
{
    int br = loops_bar_rows(), rpb = loops_rpb(), s = r % rpb;
    if (s) ui_fmt(b, 12, "%d.%d.%d", r / br + 1, r % br / rpb + 1, s + 1);
    else ui_fmt(b, 12, "%d.%d", r / br + 1, r % br / rpb + 1);
}
/* a SHIFT signed the nearer way round, and as shown: `+17 ROWS`, `-1 ROW` */
static int shift_signed(int v) { int n = loops_rows(); v = (v % n + n) % n; return v > n / 2 ? v - n : v; }
static void shift_text(char *b, int v)
{
    int s = shift_signed(v);
    ui_fmt(b, 16, "%s%d%s", s > 0 ? "+" : "", s, s == 1 || s == -1 ? T(" ROW") : T(" ROWS"));
}
/* a change told as an edit's (MEMORY FOR EDITS IS FULL ...), and to the player when it plays them: 0 done */
static int edited(int r, uint32_t was, int rows)
{
    if (ui_edit_loops(r, was)) return -1;
    loops_changed(rows);
    return 0;
}
static int set(int r) { return edited(r, ui_edit_used(), 0); }
int lp_busy(int b)
{
    if (counting()) { ui_refuse(b); return 1; }
    return 0;
}
int lp_song_on(void) { return lp.song == SG_ON; }
/* REC's wait (loops.md#rec-edit-and-the-arm): for 500 ms after REC does a job on LOOPS it does nothing */
void lp_rec_done(void) { uint32_t t = ui_now(); lp.quiet = t + !t; }   /* (0 is none: ms 0 stamps 1) */
int lp_rec_quiet(void)
{
    if (lp.quiet && (int32_t)(ui_now() - lp.quiet) >= REC_QUIET_MS) lp.quiet = 0;   /* (signed: 1 may be ahead) */
    return lp.quiet != 0;
}

/* ------------------------------------------------------------------ the stage --- */
/* Its stream is the store's piece (store_stage), there only while it holds notes, `lp.st_rows` rows long. A change is the
 * stream built again into ui_scratch, row by row (st_write), and put; refused, nothing changed and the header says why */
static int stage_has(void) { return store_stage(0) != 0; }
static int cmd_at(const rec_cell_t *c, int code)
{
    for (int k = 0; k < c->n; k++) if (c->cmd[k][0] == code) return k;
    return -1;
}
/* a lane's note still sounds after cell c (was: before it): a note starts one, a note off or a key off ends it */
static int sounding(int was, const rec_cell_t *c)
{
    if (c->has & RC_NOTE) was = c->note != REC_NOTE_OFF;
    return cmd_at(c, 0x14) >= 0 ? 0 : was;
}
static void off_cell(rec_cell_t *c) { memset(c, 0, sizeof *c); c->has = RC_NOTE | LP_PLAYED; c->note = REC_NOTE_OFF; }
static int stage_lanes(void)            /* the lanes its cells use, 1-4 */
{
    uint32_t len;
    const uint8_t *s = store_stage(&len);
    rec_cur_t cu;
    rec_cell_t c;
    int n = 1;
    if (!s) return 1;
    rec_cur_set_end(&cu, s, s + len);
    while (cu.row < 4 * lp.st_rows) { int r = cu.row; if (rec_cur_at(&cu, r, &c) && !loops_bridge(&c) && (r & 3) + 1 > n) n = (r & 3) + 1; }
    return n;
}
static int stage_cell(int row, int lane, rec_cell_t *c)    /* 1: a cell there */
{
    uint32_t len;
    const uint8_t *s = store_stage(&len);
    rec_cur_t cu;
    if (!s) return 0;
    rec_cur_set_end(&cu, s, s + len);
    return rec_cur_at(&cu, 4 * row + lane, c) && !loops_bridge(c);
}
/* the cells the keys wrote in the line's pass (nx 0) or the next (1): never replaced by their own pass's spans */
static int own_is(int srow, int nx)
{
    if (!nx) return lp.own[srow >> 3] >> (srow & 7) & 1;
    for (int k = 0; k < 4; k++) if (lp.own_nx[k] == srow) return 1;
    return 0;
}
static void own_set(int srow, int nx)
{
    if (!nx) { lp.own[srow >> 3] |= (uint8_t)(1u << (srow & 7)); return; }
    int k = 0;
    while (k < 3 && lp.own_nx[k] != 0xFFFF && lp.own_nx[k] != srow) k++;
    lp.own_nx[k] = (uint16_t)srow;
}
static void own_clear(void) { memset(lp.own, 0, sizeof lp.own); memset(lp.own_nx, 0xFF, sizeof lp.own_nx); }
/* lane l's note from before row r still sounding there, not one the keys wrote in pass nx */
static int sounds_at(int l, int r, int nx)
{
    uint32_t len;
    const uint8_t *s = store_stage(&len);
    rec_cur_t cu;
    rec_cell_t c;
    int on = 0;
    if (!s) return 0;
    rec_cur_set_end(&cu, s, s + len);
    while (cu.row < 4 * r) {
        int sr = cu.row;
        if (!rec_cur_at(&cu, sr, &c) || loops_bridge(&c) || (sr & 3) != l) continue;
        on = own_is(sr, nx) ? 0 : sounding(on, &c);
    }
    return on;
}
/* a stage's stream put (buf, n bytes with its 0x00): dropped when it holds no note; STORE_* */
static int stage_put(const uint8_t *buf, uint32_t n)
{
    rec_cur_t cu;
    rec_cell_t c;
    int notes = 0;
    rec_cur_set_end(&cu, buf, buf + n);
    while (cu.row != 0x7FFF && !notes) { int r = cu.row; if (rec_cur_at(&cu, r, &c)) notes = loops_is_note(&c); }
    return notes ? store_stage_put(buf, n) : store_stage_drop();
}
/* a change of the stage: rows [a, b) cleared on `lanes` (the cells the keys wrote in pass nx kept), cells put (in stream
 * rows' order), MONO merged onto lane 0 first (mono); extra: the bytes its room must keep (a note's end) */
typedef struct { uint8_t a, b, lanes, nx; } st_clr_t;
typedef struct { int nclr, nput, mono, extra; st_clr_t clr[5]; uint16_t at[5]; rec_cell_t c[5]; } st_ed_t;
static void ed_put(st_ed_t *ed, int row, int lane, const rec_cell_t *c)
{
    int k = ed->nput, srow = 4 * row + lane;
    if (k >= 5) return;
    while (k && ed->at[k - 1] > srow) { ed->at[k] = ed->at[k - 1]; ed->c[k] = ed->c[k - 1]; k--; }
    ed->at[k] = (uint16_t)srow;
    ed->c[k] = *c;
    ed->nput++;
}
static void ed_clr(st_ed_t *ed, int a, int b, int lanes, int nx)
{
    if (ed->nclr < 5 && a < b) ed->clr[ed->nclr++] = (st_clr_t){(uint8_t)a, (uint8_t)b, (uint8_t)lanes, (uint8_t)nx};
}
/* OVERDUB (specs/ui/screens/loops.md#the-stage): a key holds its own lane alone and what was there stays: a note sounding
 * where it lands is cut there, a note starting while it is held has its start moved on with the key, put down where it
 * is let go (one it covers whole gone). OVERWRITE: a key clears every lane over its span, as ever */
static int overwrite_on(void);
static int key_lane(int n);
static int dub(void) { return !overwrite_on(); }
static int key_lanes(int n, int mono) { return mono ? 1 : dub() ? 1 << key_lane(n) : 15; }
/* the row of the next cell on lane l after row r (not one the keys wrote in pass nx), the length if none */
static int lane_next(int l, int r, int nx)
{
    uint32_t len;
    const uint8_t *s = store_stage(&len);
    rec_cur_t cu;
    rec_cell_t c;
    int rows = loops_rows();
    if (!s) return rows;
    rec_cur_set_end(&cu, s, s + len);
    while (cu.row < 4 * rows) {
        int sr = cu.row;
        if (rec_cur_at(&cu, sr, &c) && !loops_bridge(&c) && (sr & 3) == l && sr >> 2 > r && !own_is(sr, nx)) return sr >> 2;
    }
    return rows;
}
/* key n's lane (OVERDUB) from its kpass to `to`: what was there cleared (the keys' own cells of the line's pass kept), a
 * note starting there moved on to `to` (a later one starting there: the earlier covered whole; a note's end there: it
 * covered whole), unless a cell is at `to` already or `to` is the length's end (covered whole) */
static void dub_on(st_ed_t *ed, int n, int to)
{
    int lane = key_lane(n), from = lp.kpass[n], rows = loops_rows(), has = 0;
    rec_cell_t c, p;
    if (to > rows) to = rows;
    if (to <= from) return;
    for (int r = from; r < to; r++) {
        if (!stage_cell(r, lane, &c) || own_is(4 * r + lane, 0)) continue;
        if (loops_is_note(&c)) { p = c; has = 1; }
        else if ((c.has & RC_NOTE) || cmd_at(&c, 0x14) >= 0) has = 0;   /* (its end) */
    }
    ed_clr(ed, from, to, 1 << lane, 0);
    lp.push &= (uint8_t)~(1u << lane);
    if (has && to < rows && !stage_cell(to, lane, &c)) { ed_put(ed, to, lane, &p); lp.push |= (uint8_t)(1u << lane); }
}
static int st_write(const st_ed_t *ed)  /* STORE_OK, or why not (nothing changed) */
{
    uint32_t len;
    const uint8_t *s = store_stage(&len);
    lp_st_rd_t d = {.mono = ed->mono, .from = -1};      /* (MONO: merged as it plays, how REC writes it) */
    rec_cell_t c4[LP_LANES];
    lp_enc_t e;
    int rows = loops_rows(), k = 0;
    if (s) rec_cur_set_end(&d.cu, s, s + len);
    loops_enc_stage(&e, ui_scratch, STAGE_MOST);
    for (int r = 0; r < rows; r++) {
        int has = s ? loops_stage_row(&d, r, c4) : 0;
        for (int j = 0; j < ed->nclr; j++)
            if (r >= ed->clr[j].a && r < ed->clr[j].b)
                for (int l = 0; l < LP_LANES; l++)
                    if ((ed->clr[j].lanes >> l & 1) && !own_is(4 * r + l, ed->clr[j].nx)) has &= ~(1 << l);
        for (; k < ed->nput && ed->at[k] >> 2 == r; k++) { c4[ed->at[k] & 3] = ed->c[k]; has |= 1 << (ed->at[k] & 3); }
        for (int l = 0; l < LP_LANES; l++) if (has >> l & 1) loops_enc_cell(&e, 4 * r + l, &c4[l]);
    }
    uint32_t n = loops_enc_end(&e);
    if (!n) return STORE_ERR_BIG;
    if (ed->extra) { int r = store_stage_check(n + (uint32_t)ed->extra); if (r) return r; }
    return stage_put(ui_scratch, n);
}
static void refusal(int r)              /* a change of the stage refused: why, in the header */
{
    ui_toast(r == STORE_ERR_STATE ? T("STAGE NOT WRITTEN: STORAGE BUSY") : r == STORE_ERR_BIG ? T("THE STAGE IS FULL")
                                                                                              : T("MEMORY FOR EDITS IS FULL"));
}
/* the stage fitted to the length (a change of it: LENGTH, GRID, LOAD, REVERT): cut there (a note sounding over the cut
 * ending at the line); longer, its stream as it is (the rows added empty). A cut refused (the store busy, memory full,
 * past 3,800 B) leaves it as it was, cut again the next pass */
static void fit(void)
{
    uint32_t len, n;
    const uint8_t *s = store_stage(&len);
    int rows = loops_rows();
    if (s && rows < lp.st_rows && (!(n = loops_stage_length(s, len, lp.st_rows, rows, ui_scratch, STAGE_MOST)) ||
                                   stage_put(ui_scratch, n) != STORE_OK)) return;
    lp.st_rows = (uint8_t)rows;
}

/* the player told: the stage's setup (the keys' instrument and mode, the mix it sounds through, the loop silent, its
 * rest while it is switched off: not heard, the selected loop heard); a new instrument or mode stops the keys down
 * writing first */
static void stop_writing(void);
static void stage_tell(void)
{
    pl_lp_stage_t s;
    memset(&s, 0, sizeof s);
    s.inst.cart = (uint8_t)(ui_cur.inst >> 8);
    s.inst.slot = (uint8_t)ui_cur.inst;
    s.mono = (uint8_t)loops_keys_mono();
    s.track = (uint8_t)target();
    s.silent = (uint8_t)(stage_has() && !lp.off && on_loop() ? loops_sel() : 0xFF);
    s.rest = lp.off;
    if (s.inst.cart != lp.tell.inst.cart || s.inst.slot != lp.tell.inst.slot || s.mono != lp.tell.mono) stop_writing();
    if (memcmp(&s, &lp.tell, sizeof s)) { lp.tell = s; player_lp_stage(&s); }
}
/* the lanes held told the player: each lane its earliest held place (a POLY key its own lane in OVERDUB, all four in
 * OVERWRITE, from its row; a MONO key lane 0; OVERWRITE's eraser all four), the lanes no longer held free */
static int key_lane(int n) { return (lp.key[n] & 7) - 1; }
static int key_nx(int n) { return lp.key[n] >> 3 & 1; }
static void holds_tell(void)
{
    uint8_t want[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    int mono = loops_keys_mono();
#define HOLD(lanes, v) for (int l_ = 0; l_ < LP_LANES; l_++) if ((lanes) >> l_ & 1 && (v) < want[l_]) want[l_] = (uint8_t)(v)
    for (int n = 0; n < 27; n++)
        if (lp.key[n] && (!mono || lp.cur == n)) HOLD(key_lanes(n, mono), lp.krow[n] | key_nx(n) << 7);
    if (lp.ers) HOLD(15, lp.ers_row | (lp.ers & 2) << 6);
#undef HOLD
    for (int l = 0; l < LP_LANES; l++) {
        if (want[l] == lp.told[l]) continue;
        unsigned m = 0;
        for (int k = l; k < LP_LANES; k++) if (want[k] == want[l] && want[k] != lp.told[k]) m |= 1u << k;
        if (want[l] == 0xFF) player_lp_hold(m, 0, -1);
        else player_lp_hold(m, lp.line + (want[l] >> 7), want[l] & 127);
        for (int k = 0; k < LP_LANES; k++) if (m >> k & 1) lp.told[k] = want[k];
    }
}

/* a song recording takes a key on the stage as it lands: what is heard now on the lane is written at (row, tick) of the
 * pass heard (a note heard at once, a note off let go after its line) */
static void song_land(int lane, int row, int tick)
{
    player_state_t st;
    if (lp.song != SG_ON) return;
    state(&st);
    live_land(lane, st.lp_ticks - (uint32_t)((st.row - row) * speed(&st) + st.tick - tick));
}
/* where a note played at pos (ticks, sp a row) lands: its row; *wait 1 when its line is ahead (heard there); *delay
 * REAL TIME's tick, as a note delay */
static int land(int pos, int sp, int *wait, int *delay)
{
    int q = QROWS[loops_quant()];
    *wait = *delay = 0;
    if (!q) {
        int r = fdiv(pos, sp);
        *delay = pos - r * sp > 15 ? 15 : pos - r * sp;
        return r;
    }
    int at = fdiv(2 * pos + q * sp, 2 * q * sp) * q;
    *wait = at * sp > pos;
    return at;
}
/* key n's note m onto the stage at `row` of the line's pass (nx: the next), heard at that row (at; -1 at once): the row
 * replaced on its lanes, a note from before sounding into it cut there; MONO legato as MONO LEGATO says (a stage of
 * chords first made one lane for good); a fifth POLY note, or one the stage has no room for: neither heard nor written
 * (the header says why). 1 written */
static int write_note(int n, int m, int row, int delay, int nx, int at)
{
    st_ed_t ed;
    rec_cell_t c, h;
    int mono = loops_keys_mono(), lane = 0, busy = 0, glide = 0, r, dubbing = dub();
    memset(&ed, 0, sizeof ed);
    for (int k = 0; k < 27; k++) if (lp.key[k] && k != n && !mono) busy |= 1 << key_lane(k);
    if (!mono && dubbing) {                             /* OVERDUB: a lane free there, the one free longest; none free, the
                                                         * one whose note ends soonest (cut there) */
        int best = -1;
        lane = LP_LANES;
        for (int l = 0; l < LP_LANES; l++) {
            if (busy >> l & 1) continue;
            int nxt = lane_next(l, row, nx), here_ = stage_cell(row, l, &h) && !own_is(4 * row + l, nx) && loops_is_note(&h);
            int v = !here_ && !sounds_at(l, row, nx) ? 512 + nxt : 256 - nxt;
            if (v > best) { best = v; lane = l; }
        }
    } else
        while (!mono && lane < LP_LANES && busy >> lane & 1) lane++;
    if (lane >= LP_LANES) return 0;
    ed.mono = mono && stage_lanes() > 1;
    ed.extra = 3;
    memset(&c, 0, sizeof c);
    c.has = RC_NOTE | RC_VOL | LP_PLAYED;
    c.note = (uint8_t)m;
    c.vol = (uint8_t)ui_set[S_KEYS_VOLUME];
    if (delay) loops_cmd_add(&c, 0x0E, 0xD0 | delay);
    if (mono) {
        int k = lp.cur;
        if (k >= 0 && lp.key[k] && (lp.krow[k] != row || key_nx(k) != nx) && loops_legato() != LG_JUMP) {
            if (ui_cart_kind((uint8_t)(ui_cur.inst >> 8)) == ENG_TB303) {   /* a TB-303 slides: SLD on the note held */
                if (key_nx(k) == nx && stage_cell(lp.krow[k], 0, &h) && loops_is_note(&h) && cmd_at(&h, 0xC1) < 0 && cmd_at(&h, 0xC2) < 0) {
                    int a = cmd_at(&h, 0xC0);
                    if (a >= 0) h.cmd[a][0] = 0xC2;
                    else loops_cmd_add(&h, 0xC1, 0);
                    ed_put(&ed, lp.krow[k], 0, &h);
                }
                glide = 1;
            } else if (loops_legato() == LG_LEGATO) {   /* elsewhere LEGATO: LEG; or a glide, 2 semitones a tick */
                loops_cmd_add(&c, 0xD3, 0);
                glide = (int)PL_LP_LEG;
            } else {
                loops_cmd_add(&c, 0x03, 0x20);
                glide = 0x20;
            }
        }
    }
    int pushed = -1;                                    /* (OVERDUB: a note from before starting there, moved on a row) */
    if (dubbing) {
        rec_cell_t p;
        if (stage_cell(row, lane, &p) && !own_is(4 * row + lane, nx) && loops_is_note(&p) && row + 1 < loops_rows() &&
            !stage_cell(row + 1, lane, &h)) { ed_put(&ed, row + 1, lane, &p); pushed = 4 * (row + 1) + lane; }
        ed_clr(&ed, row, row + 1, 1 << lane, nx);
    } else {
        ed_clr(&ed, row, row + 1, mono ? 1 : 15, nx);
        for (int l = 0; l < LP_LANES && !mono; l++)
            if (l != lane && !(busy >> l & 1) && sounds_at(l, row, nx)) { rec_cell_t o; off_cell(&o); ed_put(&ed, row, l, &o); }
    }
    ed_put(&ed, row, lane, &c);
    if ((r = st_write(&ed)) != STORE_OK) { refusal(r); return 0; }
    for (int k = 0; k < ed.nput; k++) if (ed.at[k] != pushed) own_set(ed.at[k], nx);   /* (the note moved on: not the key's) */
    lp.key[n] = (uint8_t)((lane + 1) | nx << 3 | delay << 4);
    lp.krow[n] = (uint8_t)row;
    lp.kpass[n] = (uint8_t)(row + 1);
    lp.push = (uint8_t)(pushed >= 0 ? lp.push | 1u << lane : lp.push & ~(1u << lane));
    if (mono) lp.cur = (int8_t)n;
    holds_tell();
    if (at < 0) song_land(lane, row, delay);
    player_lp_note(PL_LP_STAGE_KEYS, lane, (uint8_t)m, c.vol, at, delay, (unsigned)glide);
    return 1;
}
/* a key let go: its span replaced up to its end, its note off on the nearest row, at least a row after the note (REAL
 * TIME a key off at its tick); past the length nothing is written (the note ends at the line: a note off at row 0 where
 * that lane is free). A MONO key ends its note only while it has the lane. The loops not playing: its sound alone ends */
static void stage_key_up(int n)
{
    int info = lp.key[n], lane = (info & 7) - 1, nx = info >> 3 & 1, delay = info >> 4, nrow = lp.krow[n], ers = lp.kpass[n];
    int mono = loops_keys_mono(), len = loops_rows(), row, keyoff = -1;
    here_t h;
    st_ed_t ed;
    rec_cell_t c, there;
    lp.key[n] = 0;
    if (lane < 0) return;
    if (mono && lp.cur != n) { holds_tell(); return; }
    if (mono) lp.cur = -1;
    holds_tell();
    if (!here(&h) || !stage_has() || h.pass != lp.line) { player_lp_off(PL_LP_STAGE_KEYS, lane, -1); return; }
    int pos = h.pos - (nx ? len * h.sp : 0);
    memset(&ed, 0, sizeof ed);
    if (loops_quant() == LQ_REAL) {
        row = fdiv(pos, h.sp);
        if (row < nrow || (row == nrow && h.tick <= delay)) row = nrow + 1;
        keyoff = h.tick;
    } else {
        row = fdiv(2 * pos + h.sp, 2 * h.sp);
        if (row < nrow + 1) row = nrow + 1;
    }
    if (dub() && !nx) {                                 /* OVERDUB: its lane to the end, a note moved on with it put down at
                                                         * its end (let go in the row before: moved back to it) */
        rec_cell_t p;
        int moved = lp.push >> lane & 1;
        lp.key[n] = (uint8_t)info;                      /* (dub_on reads its lane) */
        if (row >= ers) dub_on(&ed, n, row);
        else if (moved && row > nrow && stage_cell(ers, lane, &p)) { ed_clr(&ed, ers, ers + 1, 1 << lane, 0); ed_put(&ed, row, lane, &p); }
        else moved = 0;
        lp.key[n] = 0;
        if (row >= ers) moved = lp.push >> lane & 1;
        lp.push &= (uint8_t)~(1u << lane);
        if (moved && row < len) {                       /* (the note moved there ends this one) */
            st_write(&ed);
            if (row * h.sp <= pos) song_land(lane, row, 0);
            player_lp_off(PL_LP_STAGE_KEYS, lane, row * h.sp > pos ? row : -1);
            return;
        }
    } else {
        lp.push &= (uint8_t)~(1u << lane);
        ed_clr(&ed, ers, row < len ? row : len, mono ? 1 : dub() ? 1 << lane : 15, nx);
    }
    if (row >= len) {                                   /* (past the length: a note off at row 0 where the lane is free) */
        if (!stage_cell(0, lane, &there)) { off_cell(&c); ed_put(&ed, 0, lane, &c); }
        if (!st_write(&ed) && ed.nput) own_set(lane, nx);
        player_lp_off(PL_LP_STAGE_KEYS, lane, 0);
        return;
    }
    int has = stage_cell(row, lane, &there);
    if (keyoff >= 0 && row == nrow && has && cmd_at(&there, 0x14) < 0) {   /* (let go in its note's row: its key off) */
        loops_cmd_add(&there, 0x14, keyoff);
        ed_put(&ed, row, lane, &there);
        st_write(&ed);
        player_lp_off(PL_LP_STAGE_KEYS, lane, -1);
        return;
    }
    if (!has || !loops_is_note(&there)) {                     /* (a note there ends it) */
        if (keyoff >= 0) {
            if (has) c = there;
            else { memset(&c, 0, sizeof c); c.has = LP_PLAYED; }
            loops_cmd_add(&c, 0x14, keyoff);
        } else if (has) { c = there; c.has |= RC_NOTE; c.note = REC_NOTE_OFF; }
        else off_cell(&c);
        ed_put(&ed, row, lane, &c);
    }
    if (!st_write(&ed) && ed.nput) own_set(4 * row + lane, nx);
    if (row * h.sp <= pos) song_land(lane, row, keyoff >= 0 ? keyoff : 0);
    player_lp_off(PL_LP_STAGE_KEYS, lane, row * h.sp > pos ? row : -1);
}
/* the keys down stop writing (each note ends as when its key is let go, its span what the line passed; the loops not
 * playing: its sound alone ends) whenever the stage's rows or sound change under them; their keys let go later do
 * nothing more */
static void stop_writing(void)
{
    for (int n = 0; n < 27; n++) if (lp.key[n]) stage_key_up(n);
    lp.cur = -1;
}
/* a key down while the loops play: onto the stage where it lands (past the length: the next pass's first rows); the
 * selected loop silent once the stage holds notes */
static void stage_tick(void);
static void stage_key_down(int n, int m)
{
    here_t h;
    int on, len = loops_rows(), wait, delay, nx = 0;
    stage_tick();
    on = here(&h);
    if (!on) { h.pos = lp.start_row * h.sp; h.pass = lp.line; }   /* (just started: not heard yet, from the play position) */
    int row = land(h.pos, h.sp, &wait, &delay);
    if (row >= len) { row -= len; nx = 1; }
    if (row < 0) row = 0;
    write_note(n, m, row, delay, nx, wait || nx ? row : -1);
    stage_tell();
}
/* OVERWRITE at work, in place of OVERDUB (lp.over): on LOOPS itself, not with the stage off, in ADJUST or the pattern
 * recorder */
extern const ui_screen_t SCR_LOOPS;
static int overwrite_on(void) { return lp.over && ui_screen_now() == &SCR_LOOPS && !lp.off && !lp.adj && !lp_rec_on(); }
/* its eraser from the line: the stage's row under it erased at once on every lane (a note from before cut there; what of
 * the stage sounds there stopped), then everything the line passes */
static void start_eraser(const here_t *h)
{
    st_ed_t ed;
    if (!stage_has()) return;
    memset(&ed, 0, sizeof ed);
    ed_clr(&ed, h->row, h->row + 1, 15, 0);
    for (int l = 0; l < LP_LANES; l++) {                /* (empty there once cleared: a note from before cut) */
        rec_cell_t c;
        if (stage_cell(h->row, l, &c) && own_is(4 * h->row + l, 0)) continue;
        if (sounds_at(l, h->row, 0)) { off_cell(&c); ed_put(&ed, h->row, l, &c); }
    }
    if (st_write(&ed)) return;
    for (int k = 0; k < ed.nput; k++) own_set(ed.at[k], 0);
    lp.ers = 1;
    lp.ers_row = (uint8_t)h->row;
    lp.ers_to = (uint8_t)(h->row + 1);
    holds_tell();
}
/* the line of pass p heard (the loops' row 0): a key still held stops writing there: its note ends at the line (its span
 * replaced to the end, a note off at row 0 where its lane is free, its sound let go) and the key is forgotten (nothing
 * written for it in the next pass, its release does nothing), so its note plays again where it was written; the eraser
 * goes on from there */
static void line_at(int p)
{
    st_ed_t ed;
    int len = loops_rows(), mono = loops_keys_mono();
    uint8_t ends[27];
    memset(&ed, 0, sizeof ed);
    memset(ends, 0, sizeof ends);
    for (int n = 0; n < 27; n++)
        if (lp.key[n] && !key_nx(n) && (!mono || lp.cur == n)) {
            ed_clr(&ed, lp.kpass[n], len, key_lanes(n, mono), 0);   /* (a note moved on with it: covered whole) */
            lp.push &= (uint8_t)~(1u << (mono ? 0 : key_lane(n)));
        }
    if (lp.ers && !(lp.ers & 2)) ed_clr(&ed, lp.ers_to, len, 15, 0);
    if (ed.nclr) st_write(&ed);
    lp.line = (uint16_t)p;                              /* (the next pass's own cells the line's now) */
    memset(lp.own, 0, sizeof lp.own);
    for (int k = 0; k < 4; k++) { if (lp.own_nx[k] != 0xFFFF) own_set(lp.own_nx[k], 0); lp.own_nx[k] = 0xFFFF; }
    for (int k = 0; k < 4; k++) lp.told[k] = 0xFE;      /* (told again, from the new line) */
    for (int n = 0; n < 27; n++) {
        if (!lp.key[n]) continue;
        if (key_nx(n)) { lp.key[n] &= (uint8_t)~8; continue; }
        if (mono && lp.cur != n) { lp.key[n] = 0; continue; }
        ends[n] = 1;
    }
    if (lp.ers) {
        if (lp.ers & 2) lp.ers = 1;
        else { lp.ers_row = 0; lp.ers_to = 0; }
    }
    for (int n = 0; n < 27; n++) {
        if (!ends[n]) continue;
        int lane = key_lane(n);
        rec_cell_t c;
        memset(&ed, 0, sizeof ed);
        if (!stage_cell(0, lane, &c)) { off_cell(&c); ed_put(&ed, 0, lane, &c); if (!st_write(&ed)) own_set(lane, 0); }
        player_lp_off(PL_LP_STAGE_KEYS, lane, -1);
        lp.key[n] = 0;                                  /* (forgotten: its release does nothing) */
        if (mono) lp.cur = -1;
    }
    holds_tell();
}
/* the loops' run: begun (seen playing: their passes from there), ended (the keys stop writing, OVERWRITE's eraser gone:
 * a run after starts it again at its line); the line; a jump landed (OVERWRITE goes on from where the loops land); the
 * held keys' and OVERWRITE's spans grown to the line */
static void stage_tick(void)
{
    here_t h;
    player_state_t st;
    int on = here(&h);
    st_ed_t ed;
    if (!on) {
        if (lp.run == 2 && (uint8_t)((uint8_t)h.halves - lp.run_h) < 6) return;   /* (started here, not yet heard) */
        if (lp.run) { lp.run = 0; stop_writing(); lp.ers = 0; holds_tell(); }
        return;
    }
    state(&st);
    if (lp.run == 0) {                                  /* (playing, not started here: from where they are) */
        lp.line = (uint16_t)h.pass;
        own_clear();
        lp.jumps = st.lp_jumps;
        memset(lp.told, 0xFF, sizeof lp.told);
    }
    lp.run = 1;
    if (st.lp_jumps != lp.jumps) {                      /* (landed from a jump: the eraser from there) */
        lp.jumps = st.lp_jumps;
        if (lp.ers) {
            int lpass = h.row < st.lp_land_row ? h.pass - 1 : h.pass;
            lp.ers = (uint8_t)(lpass > (int)lp.line ? 3 : 1);
            lp.ers_row = lp.ers_to = st.lp_land_row;
            lp.told[0] = lp.told[1] = lp.told[2] = lp.told[3] = 0xFE;
            holds_tell();
        }
    }
    if (h.pass != (int)lp.line) {
        if (h.pass == (int)lp.line + 1) line_at(h.pass);
        else { stop_writing(); lp.line = (uint16_t)h.pass; own_clear(); if (lp.ers) lp.ers = 1; holds_tell(); }
    }
    if (!overwrite_on()) { if (lp.ers) { lp.ers = 0; holds_tell(); } }
    else if (!lp.ers) start_eraser(&h);
    if (!stage_has()) return;
    memset(&ed, 0, sizeof ed);
    int to = h.row + 1, mono = loops_keys_mono();
    for (int n = 0; n < 27; n++)
        if (lp.key[n] && !key_nx(n) && (!mono || lp.cur == n) && to > lp.kpass[n]) {
            if (dub()) dub_on(&ed, n, to);              /* (OVERDUB: its lane, a note there moved on with it) */
            else ed_clr(&ed, lp.kpass[n], to, mono ? 1 : 15, 0);
            lp.kpass[n] = (uint8_t)to;
        }
    if (lp.ers == 1 && to > lp.ers_to) { ed_clr(&ed, lp.ers_to, to, 15, 0); lp.ers_to = (uint8_t)to; }
    if (ed.nclr) st_write(&ed);
}
/* the stage emptied (what of it sounds let go); the selected loop heard again */
static void clear_stage(void)
{
    stop_writing();
    player_lp_stage_end(-1);
    store_stage_drop();
    lp.ers = 0;
    holds_tell();
    stage_tell();
}
/* everything the stage follows, before a control acts and on every pass */
static void sync(void)
{
    fit();
    stage_tell();
    stage_tick();
}
/* ARP's tap: the stage off and on (STAGE OFF, STAGE ON), on the screen whatever else is going on (REC with it off:
 * stage_on_rec, below). Off, the stage keeps
 * its notes, drawn dimmed, not heard, and the selected loop is heard in its place (the player told: its rest); the keys
 * down stop writing; an arm
 * (the stage's: its first key would start the loops writing) ends, a count-in with it (CANCELLED). On, the stage is
 * heard again from the next row, the selected loop silent again while it holds notes */
static void stage_switch(void)
{
    if (lp.off) {
        lp.off = 0;
        stage_tell();
        ui_toast(T("STAGE ON"));
    } else {
        stop_writing();
        lp.off = 1;
        lp.ers = 0;
        holds_tell();
        stage_tell();
        if (lp.arm) {
            if (counting() && lp.song == SG_NONE) player_count_stop();
            lp.arm = ARM_NONE;
            ui_toast(T("CANCELLED"));
        } else ui_toast(T("STAGE OFF"));
    }
    ui_dirty();
}
/* the stage's clear (EDIT held, EDIT's tap on + NEW LOOP, SEL's CLEAR STAGE): emptied, the selected loop heard again, and
 * on, as EDIT on a loop leaves it. Already empty: off, it just turns on; on, b blinks. Refused during a count-in */
static void stage_clear(int b)
{
    if (lp_busy(b)) return;
    if (stage_has()) {
        clear_stage();
        lp.off = 0;
        stage_tell();
        ui_toast(T("STAGE CLEARED"));
    } else if (lp.off) stage_switch();
    else ui_refuse(b);
}
/* the keys while the stage is off and the loops play: each sounds at once in the stage's sound (the keys' instrument and
 * MONO / POLY, through the stage's column and LOOPS' effects), no QUANTIZE wait, written nowhere, on a lane of its own
 * (player.h: PL_LP_STAGE_KEYS' 4 on): POLY every key a note (a free lane, as many as the pool's voices can sound; all held,
 * the lane taken next round), MONO one note on one lane, legato as MONO LEGATO says (a TB-303 slides); a key let go ends
 * its note at once (MONO: only the key whose note sounds). The stage-on keys' lanes stay theirs */
static void off_key_down(int n, int m)
{
    int mono = loops_keys_mono(), l = -1;
    unsigned glide = 0;
    for (int k = 0; k < PL_LP_OFF_LANES && mono && l < 0; k++) if (lp.ol[k] != 0xFF && lp.ol[k] & 0x80) l = k;
    if (l >= 0 && loops_legato() != LG_JUMP)            /* (TB-303: a slide; LEGATO: LEG; GLIDE: 2 semitones a tick) */
        glide = ui_cart_kind((uint8_t)(ui_cur.inst >> 8)) == ENG_TB303 ? 1u : loops_legato() == LG_LEGATO ? PL_LP_LEG : 0x20u;
    for (int k = 0; k < PL_LP_OFF_LANES && l < 0; k++) if (lp.ol[k] == 0xFF) l = k;
    if (l < 0) { l = lp.onext; lp.onext = (uint8_t)((l + 1) % PL_LP_OFF_LANES); }
    lp.ol[l] = (uint8_t)(n | (mono ? 0x80 : 0));
    player_lp_note(PL_LP_STAGE_KEYS, 4 + l, (uint8_t)m, (uint8_t)ui_set[S_KEYS_VOLUME], -1, 0, glide);
}
static int off_key_up(int n)            /* 1: key n held a lane of the stage-off keys (its note ended) */
{
    for (int k = 0; k < PL_LP_OFF_LANES; k++)
        if ((lp.ol[k] & 0x7F) == n) {   /* (a free lane's 0xFF: 0x7F, no key) */
            lp.ol[k] = 0xFF;
            player_lp_off(PL_LP_STAGE_KEYS, 4 + k, -1);
            return 1;
        }
    return 0;
}
static void off_keys_end(void)          /* the keys down while the stage is off, ended at once (leaving LOOPS, FX) */
{
    for (int k = 0; k < PL_LP_OFF_LANES; k++)
        if (lp.ol[k] != 0xFF) { lp.ol[k] = 0xFF; player_lp_off(PL_LP_STAGE_KEYS, 4 + k, -1); }
}

/* --------------------------------------------------------------- REC and EDIT --- */
/* the loops from the play position (or the count-in's end): their passes from 0, nothing held */
static void start_play(void)
{
    player_state_t st;
    int n = loops_rows(), row = lp.kept % n;
    state(&st);
    if (st.lp_alone) player_lp_preview_alone(0);        /* (the recorder's preview alone stops) */
    loops_play((uint8_t)row);
    lp.start_row = (uint8_t)row;
    lp.arm = ARM_NONE;
    lp.run = 2;
    lp.run_h = (uint8_t)st.halves;
    lp.line = 0;
    lp.jumps = st.lp_jumps;
    lp.ers = 0;
    own_clear();
    memset(lp.told, 0xFF, sizeof lp.told);
}
static void count_then(void)            /* armed, PLAY: after COUNT-IN (OFF: at once) */
{
    player_state_t st;
    int ci = loops_countin(), d = loops_ticks() * loops_rpb();
    if (!ci) { start_play(); return; }
    state(&st);
    if (st.playing) player_pause();                     /* (what played on into LOOPS pauses as the count starts) */
    player_count_in(ci, d ? loops_tempo() * 240 / d : 1200);
    loops_play((uint8_t)(lp.kept % loops_rows()));      /* (it waits for the count's end) */
    lp.start_row = (uint8_t)(lp.kept % loops_rows());
    lp.arm = ARM_COUNT;
}
/* REC with notes on the stage: sent where the selection is (+ NEW LOOP: a new loop in the keys' instrument and mode, the
 * next colour, the next loop's mix, the selection on + NEW LOOP again; a loop: its notes replaced, the instrument and
 * mode written, SHIFT 0); the stage emptied, what of it sounds going on as the loop's (loops_stage_write), and off till
 * REC or ARP turns it on; an arm ends */
static int send(void)
{
    char nm[8];
    uint32_t len, was = ui_edit_used();
    if (store_stage_check(1) == STORE_ERR_STATE) { ui_refuse(B_REC); ui_toast(T("STAGE NOT WRITTEN: STORAGE BUSY")); return 0; }
    stop_writing();
    fit();
    int i = target(), newl = !on_loop();
    const uint8_t *s = store_stage(&len);
    if (!s) return 0;
    int r = loops_stage_write(i, s, len, (uint8_t)(ui_cur.inst >> 8), (uint8_t)ui_cur.inst, loops_keys_mono());
    if (r == LP_BIG || r == LP_NO) { ui_refuse(B_REC); if (r == LP_BIG) ui_toast(T("THE STAGE IS FULL")); return 0; }
    if (edited(r, was, 0)) { ui_refuse(B_REC); return 0; }
    if (newl) loops_set_sel(top_sel());                 /* (+ NEW LOOP again) */
    lp.ers = 0;
    lp.arm = ARM_NONE;
    lp.off = 1;
    holds_tell();
    lp_loop_name(nm, i);
    ui_toast(ui_txt_v(newl ? T("STAGE AS #") : T("STAGE INTO #"), nm));
    stage_tell();
    return 1;
}
/* REC on the screen: the stage sent; empty: while the loops play refused (THE STAGE IS EMPTY), stopped it arms (again:
 * disarms). In ADJUST nothing (REC blinks). 1: a job done */
static int rec_press(void)
{
    if (lp.adj) { ui_refuse(B_REC); return 0; }
    if (stage_has()) return send();
    if (lp_playing_here()) { ui_refuse(B_REC); ui_toast(T("THE STAGE IS EMPTY")); return 0; }
    if (lp.arm) { lp.arm = ARM_NONE; ui_toast(T("CANCELLED")); return 1; }
    lp.arm = ARM_WAIT;
    return 1;
}
/* EDIT: the selected loop onto the stage as it is heard (its repeats written out, a note off where it rests or a repeat
 * is cut, its SHIFT applied), in place of what the stage held; its instrument and MONO / POLY the keys'; the stage on, if
 * it was off; OVERDUB, so the line keeps them; the loop silent at once. On + NEW LOOP the stage's clear; a stage it would
 * not fit: refused, saying why */
static void edit_fill(void)
{
    char nm[8];
    int i = loops_sel(), r;
    if (!on_loop()) { stage_clear(B_EDIT); return; }
    if (lp_busy(B_EDIT)) return;
    uint32_t n = loops_heard(i, 0, STAGE_MOST);
    if (!n) r = STORE_ERR_BIG;
    else r = store_stage_check(n);
    if (r) { ui_refuse(B_EDIT); refusal(r); return; }
    stop_writing();
    player_lp_stage_end(-1);                            /* (what of the stage sounds let go at once) */
    ui_set_inst(loops_inst(i));
    if (loops_keys_mono() != loops_mono(i)) set(loops_set_keys_mono(loops_mono(i)));
    n = loops_heard(i, ui_scratch, STAGE_MOST);
    r = n ? stage_put(ui_scratch, n) : STORE_ERR_BIG;
    if (r) { refusal(r); return; }
    lp.st_rows = (uint8_t)loops_rows();
    lp.ers = 0;
    lp.off = 0;                                         /* (an explicit fill of the stage turns it on) */
    lp.over = 0;
    holds_tell();
    stage_tell();                                       /* (the stage holds its notes: the loop silent at once) */
    lp_loop_name(nm, i);
    ui_toast(ui_txt_v(T("# ON THE STAGE"), nm));
}
/* the pattern recorder's REC (lp_rec.c): its stream onto the stage in place of what it held, checked whole first; the
 * keys down stop writing before it is made (their ends are written through ui_scratch); its instrument and POLY the
 * keys' then, the stage on if it was off, OVERDUB as EDIT's fill. The stage's own sounding notes are the player's to let go
 * (player_lp_preview_keep, the recorder's). One
 * window: a put refused after the check passed (the store turned busy within this call) leaves the keys down stopped,
 * their ends written; nothing else changed (the stream is built after their ends, so the order stays) */
int lp_stage_from(uint32_t (*make)(uint8_t *buf, uint32_t max), uint16_t inst, int poly)
{
    uint32_t n = make(0, STAGE_MOST);
    int r = n ? store_stage_check(n) : STORE_ERR_BIG;
    if (!r) {
        stop_writing();
        n = make(ui_scratch, STAGE_MOST);
        r = n ? stage_put(ui_scratch, n) : STORE_ERR_BIG;
    }
    if (r) { ui_refuse(B_REC); refusal(r); return r; }
    if (inst != ui_cur.inst) ui_set_inst(inst);
    if (poly && loops_keys_mono()) set(loops_set_keys_mono(0));
    lp.st_rows = (uint8_t)loops_rows();
    lp.ers = 0;
    lp.off = 0;                                         /* (an explicit fill of the stage turns it on) */
    lp.over = 0;
    holds_tell();
    stage_tell();
    return STORE_OK;
}

/* ------------------------------------------------------------------- RECORD LIVE --- */
/* RECORD LIVE (specs/ui/screens/loops.md#record-live): its panel (SEL's menu: the name, LOOPS' tempo) arms it, the
 * player's tap on from there (play/live.c); it starts with REC, a note, or PLAY (the loops not playing as it was armed:
 * after COUNT-IN); HOME ends it, leaving LOOPS or the loops' replay pausing anywhere too. Its rows are its own, its time
 * the replay's ticks from its row 0: while it records the replay never stops, PLAY silencing the loops or playing them on
 * again on the next row, held taking them to their start (player_lp_rest). play/live.c writes it from what sounds, a
 * pattern every 128 rows once they are heard (lp_live_poll, on every screen: FX keeps it); LOOPS goes on as ever */
static char sl_name[17];                /* the panel's NAME (16 characters at most) */
/* it begins at its row 0, the replay's tick t0 (known 0: a replay started for it, known at its first row, lp_live_poll):
 * 1 begun */
static int song_begin(uint32_t t0, int known)
{
    player_state_t st;
    uint8_t name[12], km[RM_N];
    int n = ui_free_song();
    if (n < 0) { lp.song = SG_NONE; live_disarm(); ui_toast(T("NO SONG NUMBER FREE")); return 0; }
    memset(name, 0, sizeof name);
    ui_put_name6(name, 0, 16, sl_name);
    memcpy(km, ui_keys_mix, RM_N);
    km[RM_LEVEL] = 0x7F;                                /* (the keys' on another screen: the KEYS column's mix, full) */
    state(&st);
    live_begin((uint8_t)n, name, km, st.halves, t0, known, ui_punch_now());
    lp.song = SG_ON;
    lp.pdefer = lp.phold = 0;
    lp.sat = 0;
    lp.hrow = -1;
    ui_dirty();
    return 1;
}
/* its start with the loops not playing (REC, a key): their replay from row 0, silent from its first row, their place
 * kept for PLAY to take them on from */
static int song_begin_silent(void)
{
    loops_play_silent((uint8_t)(lp.kept < loops_rows() ? lp.kept : 0));
    return song_begin(0, 0);
}
/* the song's row heard now (row 0 until the replay's first row is known) */
static int song_now(const player_state_t *st)
{
    int here_ = st->what == PL_LOOPS && st->playing && live_known();
    int32_t d = here_ ? (int32_t)(st->lp_ticks - live_t0()) : 0;
    if (d < 0) d = 0;
    return d / live_ticks();
}
enum { SF_DONE, SF_FULL = LIVE_FULL, SF_PATTERNS = LIVE_PATTERNS, SF_256 };
/* it is over: the toast says how it went (why: SF_*) */
static void song_finish(int why)
{
    char t[12];
    const char *m, *s;
    player_state_t st;
    int n = live_done(), left = live_left();
    state(&st);
    int secs = (int)((uint64_t)(st.halves - live_h0()) * 256u / 44100u);
    s = ui_hex2(live_song());
    lp.song = SG_NONE;
    live_disarm();
    if (st.what == PL_LOOPS && st.lp_rest) player_pause();   /* (the loops silent: their replay stops, at their place) */
    ui_fmt(t, sizeof t, "%d:%02d", secs / 60, secs % 60);
    if (why == SF_FULL) m = ui_txt_v(T("SONG # STOPPED: MEMORY FOR EDITS IS FULL"), s);
    else if (why == SF_PATTERNS) m = ui_txt_v(T("SONG # STOPPED: ALL 240 PATTERNS USED"), s);
    else if (why == SF_256) m = ui_txt_v(T("SONG # STOPPED: 256 POSITIONS"), s);
    else if (!n) m = T("CANCELLED");
    else if (left == 1) m = ui_txt_v(T("SONG #: 1 NOTE LEFT OUT"), s);
    else if (left) m = ui_txt_v(ui_txt_v(T("SONG #: # NOTES LEFT OUT"), s), ui_dec(left));
    else if (n == 1) m = ui_txt_v(ui_txt_v(T("SONG # RECORDED: #, 1 PATTERN"), s), t);
    else m = ui_txt_v(ui_txt_v(ui_txt_v(T("SONG # RECORDED: #, # PATTERNS"), s), t), ui_dec(n));
    ui_toast(m);
    ui_dirty();
}
/* the patterns heard to their end written (each once its rows are heard): 1 it stopped (256 positions, no room) */
static int song_chunks(int now)
{
    int r;
    while (live_done() < now / LIVE_ROWS && live_done() < 256)
        if ((r = live_chunk(LIVE_ROWS)) != LIVE_OK) { song_finish(r); return 1; }
    if (live_done() >= 256) { song_finish(SF_256); return 1; }
    return 0;
}
/* it ends at once at the song's row `at`: the rows heard written, that row the last; nothing heard (at < 0): no song */
static void song_end(int at)
{
    int r;
    if (lp.song != SG_ON) { lp.song = SG_NONE; live_disarm(); return; }
    if (at >= 0) live_poll();
    if (at >= 0 && song_chunks(at)) return;
    live_punch_end(at + 1);
    if (at >= live_done() * LIVE_ROWS && live_done() < 256 && (r = live_chunk(at % LIVE_ROWS + 1)) != LIVE_OK) {
        song_finish(r);
        return;
    }
    song_finish(SF_DONE);
}
static void song_end_here(int at)       /* ... at row at (HOME's press), or (< 0) the row heard now (leaving LOOPS) */
{
    player_state_t st;
    state(&st);
    song_end(!live_known() ? -1 : at >= 0 ? at : st.what == PL_LOOPS && st.playing ? song_now(&st) : lp.sat);
}
void lp_song_stop(void) { if (lp.song == SG_ON) song_end_here(-1); }
/* armed, REC or a key: the loops playing, it begins at the row playing (they play on as they were); not playing,
 * their replay starts silent for it */
static int song_start(void)
{
    player_state_t st;
    state(&st);
    if (st.what == PL_LOOPS && st.playing) return song_begin(st.lp_ticks - st.tick, 1);
    return song_begin_silent();
}
static void song_count(void)            /* armed, PLAY (the loops not playing as it was armed): after COUNT-IN */
{
    player_state_t st;
    int ci = loops_countin(), d = loops_ticks() * loops_rpb();
    state(&st);
    if (st.playing) player_pause();                     /* (what played on into LOOPS pauses as the count starts) */
    loops_play(0);                                      /* (with COUNT-IN: it waits for the count's end) */
    if (!ci) { song_begin(0, 0); return; }              /* (a replay from row 0, known two halves on: lp_live_poll) */
    player_count_in(ci, d ? loops_tempo() * 240 / d : 1200);
    lp.song = SG_COUNT;
}
static void song_cancel(void)           /* armed or counting in: given up */
{
    if (lp.song == SG_COUNT) player_count_stop();
    if (lp.song) live_disarm();
    lp.song = SG_NONE;
    ui_dirty();
}
/* the RECORD LIVE panel (specs/ui/screens/loops.md#the-record-live-panel): NAME (EDIT: the text entry), LOOPS' tempo on
 * K1 (and ALGORITHM, as on the screen); REC arms it, HOME cancels; the keys play as on the screen */
static void sl_default(void)            /* LOOPS 01, or one past the highest LOOPS ## a song has */
{
    ui_song_t s;
    char pre[12];
    int hi = 0, n;
    ui_fmt(pre, sizeof pre, "%s ", T("LOOPS"));
    n = (int)strlen(pre);
    for (int k = 0; k <= UI_SONG_LAST; k++) {
        if (!ui_song((uint8_t)k, &s) || strncmp(s.name, pre, (size_t)n)) continue;
        const char *d = s.name + n;
        if (d[0] >= '0' && d[0] <= '9' && d[1] >= '0' && d[1] <= '9' && !d[2] && (d[0] - '0') * 10 + d[1] - '0' > hi)
            hi = (d[0] - '0') * 10 + d[1] - '0';
    }
    ui_fmt(sl_name, sizeof sl_name, "%s%02d", pre, hi < 99 ? hi + 1 : 99);
}
static void sl_named(const char *name)
{
    if (name && name[0]) { strncpy(sl_name, name, sizeof sl_name - 1); sl_name[sizeof sl_name - 1] = 0; }
    ui_dirty();
}
static void sl_knob(ui_overlay_t *o, int e, int s, int coarse)
{
    (void)o;
    if (e == K_K1 || e == K_ALG) { lp_tempo_turn(s, coarse); ui_dirty(); }
}
static int sl_tap(ui_overlay_t *o, int b)
{
    (void)o;
    if (b == B_EDIT) ui_text_entry(T("NAME"), sl_name, 16, B_EDIT, sl_named);
    return 1;
}
static void sl_rec(ui_overlay_t *o)     /* REC (ARM): armed, the panel closed, the tap on */
{
    player_state_t st;
    if (lp_rec_quiet()) return;
    ui_close(o);
    state(&st);
    lp.song = SG_ARMED;
    lp.splay = st.what == PL_LOOPS && st.playing;       /* (playing: PLAY does not start it) */
    live_arm();
    lp_rec_done();
    ui_dirty();
}
static int sl_lit(ui_overlay_t *o, int b) { (void)o; return b == B_REC ? (ui_blink(250) ? LV_FULL : 0) : LV_NONE; }
static void sl_value(int y, const char *v)
{
    g_run_t r = {v, C_WHITE};
    g_runs(226 - g_runs_w(&FONT_T, &r, 1), y, &FONT_T, &r, 1);
}
static void sl_draw(ui_overlay_t *o)
{
    (void)o;
    int h = 108, y = BODY_Y + (BODY_H - h) / 2;
    char b[16];
    ui_panel(8, y, 224, h, 0, C_LGREY);
    g_text(g_centre(&FONT_T, T("RECORD LIVE"), 8, 224), y + 6, &FONT_T, T("RECORD LIVE"), C_WHITE);
    g_text(14, y + 24, &FONT_T, T("NAME"), C_LGREY);
    sl_value(y + 24, sl_name);
    g_run_t k[2] = {{T("K1"), C_YELLOW}, {T(" TEMPO"), C_LGREY}};
    g_runs(14, y + 36, &FONT_T, k, 2);
    ui_fmt(b, sizeof b, "%d BPM", loops_bpm());
    sl_value(y + 36, b);
    g_run_t e[2] = {{T("EDIT"), C_YELLOW}, {T(": NAME"), C_LGREY}};
    g_runs(14, y + 52, &FONT_T, e, 2);
    ui_ask_foot(y + 68, y + h, T("ARM"), T("CANCEL"), 0, 0, B_HOME, B_REC);
}
static void sl_open(void)
{
    sl_default();
    ui_overlay_t o = {.kind = OV_OTHER, .opener = B_SEL, .knob = sl_knob, .tap = sl_tap, .draw = sl_draw, .rec = sl_rec,
                      .lit = sl_lit};
    ui_open(&o);
}
/* the main loop, on every screen: the play position (where the loops paused), the stage fitted to the length, the
 * song's start (the count-in's end), its row 0 known (a replay started for it, two halves on), the tap's events
 * (live_poll), each pattern written once its rows are heard (not while HOME's press may yet cut them back), a punch-in,
 * the replay pausing (it ends where it was heard last) */
static void waits_poll(void);
void lp_live_poll(void)
{
    player_state_t st;
    int here_;
    waits_poll();
    state(&st);
    if (st.what == PL_LOOPS && !st.lcue && !st.lp_rest && st.playing && !st.count_beats) {
        lp.kept = (uint8_t)st.row;                      /* (the play position: where they are) */
        lp.pl_seen = 1;
    } else if (lp.pl_seen) {                            /* (paused: where they were; stopped, their start) */
        lp.pl_seen = 0;
        if (st.what == PL_LOOPS && !st.lcue && !st.playing) lp.kept = (uint8_t)st.row;
    }
    if (lp.arm == ARM_COUNT && st.what == PL_LOOPS && st.playing && !st.count_beats) {   /* (the count's end: started) */
        lp.arm = ARM_NONE;
        lp.run = 0;
    }
    fit();
    if (!lp.song) return;
    live_poll();
    here_ = st.what == PL_LOOPS && st.playing;
    if (lp.song == SG_COUNT && here_ && !st.count_beats) song_begin(st.lp_t0, 1);
    if (lp.song != SG_ON) return;
    if (!live_known()) {
        if (st.halves - live_h0() <= 2) return;
        live_known_set(st.lp_t0);
    }
    if (!here_) { song_end(lp.sat); return; }
    int now = song_now(&st);
    if (lp.hrow >= 0 && !ui_pending(B_HOME)) lp.hrow = -1;   /* (a combination, not a tap) */
    if (lp.hrow < 0 && song_chunks(now)) return;
    live_punch(ui_punch_now());
    lp.sat = (int16_t)now;
}

/* ---------------------------------------------------------------- mutes and solos --- */
static void mutes_toast(int i, int solo, int on, int at_bar)
{
    char nm[8];
    lp_loop_name(nm, i);
    const char *w = solo ? (on ? (at_bar ? T("# SOLO AT THE BAR") : T("# SOLO")) : at_bar ? T("# SOLO OFF AT THE BAR") : T("# SOLO OFF"))
                         : on ? (at_bar ? T("# MUTED AT THE BAR") : T("# MUTED")) : at_bar ? T("# ON AT THE BAR") : T("# ON");
    ui_toast(ui_txt_v(w, nm));
}
/* the waits for the bar line to the player (the store's mutes and solos with them toggled), or none left: the store's
 * now (a change now drops one waiting there) */
static void waits_tell(void)
{
    player_state_t st;
    uint32_t m = loops_muted_mask(), s = loops_soloed_mask();
    if (lp.wm | lp.ws) player_lp_mutes_at_bar(m ^ lp.wm, s ^ lp.ws, loops_bar_rows());
    else player_lp_mutes(m, s);
    state(&st);
    lp.wsent = (lp.wm | lp.ws) != 0;
    lp.wseen = 0;
    lp.wsent_h = (uint8_t)st.halves;
}
/* a solo, one loop at a time: loop i soloed alone, the others' solos off; on a soloed loop, its solo off. At once, or
 * (at_bar, the loops playing) at the next bar line, every solo that changes waiting with it; held again calling the
 * waits off */
static void solo_one(int i, int at_bar)
{
    uint16_t bit = (uint16_t)(1u << i), s = (uint16_t)loops_soloed_mask();
    int on = !(s & bit);
    uint16_t to = on ? bit : (uint16_t)(s & ~bit);
    if (at_bar && lp_playing_here()) {
        int was = lp.ws & bit;
        lp.ws = was ? 0 : (uint16_t)(s ^ to);
        waits_tell();
        if (!was) mutes_toast(i, 1, on, 1);
        return;
    }
    uint16_t keep = lp.ws;
    lp.ws = 0;
    if (set(loops_set_mutes(loops_muted_mask(), to))) { lp.ws = keep; return; }
    if (lp.wm || keep) waits_tell();
    mutes_toast(i, 1, on, 0);
    ui_dirty();
}
/* loop i's mute (or solo: solo_one) switched: at once, or (at_bar, the loops playing) at the next bar line, held again
 * calling the wait off */
static void toggle(int i, int solo, int at_bar)
{
    if (i >= loops_n()) return;
    if (solo) { solo_one(i, at_bar); return; }
    uint16_t bit = (uint16_t)(1u << i);
    int on = !loops_muted(i);
    if (at_bar && lp_playing_here()) {
        int was = lp.wm & bit;
        lp.wm ^= bit;
        waits_tell();
        if (!was) mutes_toast(i, 0, on, 1);
        return;
    }
    uint32_t keep = lp.wm & bit;
    lp.wm &= (uint16_t)~bit;
    if (set(loops_set_muted(i, on))) { lp.wm |= (uint16_t)keep; return; }
    if (lp.wm | lp.ws || keep) waits_tell();            /* (the others still waiting, told again: the change now dropped
                                                         * them in the player) */
    mutes_toast(i, 0, on, 0);
    ui_dirty();
}
/* the bar line came for the waits (the player's wait seen and over, or never seen: taken and over in one half, 3 halves
 * on from the request, a whole one past the one it was posted in), on every screen (lp_live_poll): into the store, as
 * the player has them now */
static void waits_poll(void)
{
    player_state_t st;
    if (!lp.wsent) return;
    state(&st);
    if (st.lp_bar_wait) { lp.wseen = 1; return; }
    if (!lp.wseen && (uint8_t)((uint8_t)st.halves - lp.wsent_h) < 3) return;   /* (not taken yet) */
    uint32_t m = loops_muted_mask() ^ lp.wm, s = loops_soloed_mask() ^ lp.ws;
    lp.wm = lp.ws = 0;
    lp.wsent = 0;
    set(loops_set_mutes(m, s));
}
static void waits_drop(void) { lp.wm = lp.ws = 0; lp.wsent = 0; }
static int waiting(int i) { return lp.wm >> i & 1 ? 1 : lp.ws >> i & 1 ? 2 : 0; }   /* 1 a mute, 2 a solo */

/* CONTROL mode's keys: white keys the loops (a tap at once, held at the bar line), black keys by their print: OP1-OP5
 * QUANTIZE, PIT all back on, GLO MUTE / SOLO, MONO and POLY the keys' mode; OP6 and the blank key nothing */
static void control_key(int n, int down)
{
    int wi = ui_white_index(n), bi = ui_black_index(n);
    if (wi >= 0) {
        if (down) { if (wi < loops_n()) lp_hold_down(wi); }
        else if (lp_hold_up(wi)) toggle(wi, lp.solo, 0);
        return;
    }
    if (!down || bi < 0) return;
    if (bi <= 4) {
        set(loops_set_quant(QKEY[bi]));
        ui_toast(ui_txt_v(T("QUANTIZE: #"), T(QUANTS[QKEY[bi]])));
    } else if (bi == 6) {                               /* PIT: all on */
        waits_drop();
        if (loops_muted_mask() | loops_soloed_mask()) set(loops_set_mutes(0, 0));
        player_lp_mutes(loops_muted_mask(), loops_soloed_mask());
        ui_toast(T("ALL LOOPS ON"));
    } else if (bi == 7) {
        lp.solo = !lp.solo;
        ui_toast(lp.solo ? T("SEQ: SOLO") : T("SEQ: MUTE"));
    } else if (bi == 8 || bi == 9) {                    /* (the keys' mode: how they play, how REC keeps the stage) */
        set(loops_set_keys_mono(bi == 8));
        stage_tell();
        ui_toast(bi == 8 ? T("KEYS: MONO") : T("KEYS: POLY"));
    }
}
/* the white keys held (CONTROL mode's, the pattern recorder's step keys: never at once): a bit a key, done once its hold
 * came, each its press's time */
void lp_hold_down(int i)
{
    uint16_t bit = (uint16_t)(1u << i);
    lp.held |= bit;
    lp.done &= (uint16_t)~bit;
    lp.held_at[i] = (uint16_t)ui_now();
}
int lp_hold_up(int i)
{
    uint16_t bit = (uint16_t)(1u << i);
    int tap = (lp.held & bit) && !(lp.done & bit);
    lp.held &= (uint16_t)~bit;
    return tap;
}
int lp_hold_due(void)
{
    for (int i = 0; i < 16; i++) {
        uint16_t bit = (uint16_t)(1u << i);
        if (!(lp.held & bit) || (lp.done & bit) || (uint16_t)((uint16_t)ui_now() - lp.held_at[i]) < (uint16_t)ui_set[S_HOLD_DELAY])
            continue;
        lp.done |= bit;
        return i;
    }
    return -1;
}
void lp_hold_clear(void) { lp.held = lp.done = 0; }
static void holds_poll(void)                            /* CONTROL's white keys held past the hold delay: at the bar */
{
    int i;
    if (lp_rec_on()) return;                            /* (the pattern recorder's step keys: its own, lp_rec_poll) */
    while ((i = lp_hold_due()) >= 0) toggle(i, lp.solo, 1);
}
/* the white keys' lights, a loop a key: full while it sounds, dim muted or silenced, blinking while a mute or solo
 * waits for the bar line, dark with no loop */
static void mute_leds(uint8_t *keys)
{
    uint32_t q = loops_silent_mask();
    for (int i = 0; i < 16; i++)
        keys[UI_WHITE[i]] = (uint8_t)(i >= loops_n() ? 0 : waiting(i) ? (ui_blink(250) ? LV_FULL : 0) : q >> i & 1u ? LV_DIM : LV_FULL);
}
static void mute_key(int t) { toggle(t, 0, 0); }        /* HOME's combination: at once, held (or after SEQ) a solo */
static void solo_key(int t) { toggle(t, 1, 0); }
static int keys_shown(void) { return lp.control || ui_held(B_SEQ); }   /* (the white keys mute: CONTROL, or SEQ held) */

/* -------------------------------------------------------------------- the keys --- */
/* PLAY mode: a song armed: the key starts it, its note the first; the stage off and the loops playing, or a song keeping
 * them silent (the stage's line stands), it sounds at once in the stage's sound, on no stage; armed (stopped), the key
 * starts the loops at the play position, its note the stage's first; the loops playing, the key goes on the stage; else
 * the keys play freely. CONTROL mode, or SEQ held (SEQ's tap then taken: it stays in PLAY mode): control_key */
static int key(int n, int down)
{
    player_state_t st;
    lp_live_poll();
    if (!down) {
        if (lp.key[n]) { stage_tick(); stage_key_up(n); }
        else if (!off_key_up(n)) control_key(n, 0);
        return 1;
    }
    if (ui_held(B_SEQ)) ui_consume(B_SEQ);
    if (keys_shown()) { control_key(n, 1); return 1; }
    int m = ui_midi(n);
    if (m < 12 || m > 127) return 1;
    if (lp.song == SG_ARMED && !song_start()) return 1;
    sync();
    state(&st);
    if (lp.song == SG_ON && (st.lp_rest || !(st.what == PL_LOOPS && st.playing))) { off_key_down(n, m); return 1; }   /* (a song
                                                         * keeping them silent, or just begun on a replay of its own) */
    if (lp.off && (lp_playing_here() || lp.run == 2) && !counting()) { off_key_down(n, m); return 1; }   /* (just started here
                                                         * too, not yet heard: as the stage's keys) */
    if (lp.arm == ARM_WAIT && !counting() && !lp_playing_here()) { start_play(); stage_key_down(n, m); return 1; }
    /* (playing; or started here and not yet heard: the keys of a chord whose first key started them land with it) */
    if ((lp_playing_here() || lp.run == 2) && !counting()) { stage_key_down(n, m); return 1; }
    return 0;
}
/* the instrument the keys play and the footer names: the pattern recorder's */
static uint16_t keys_inst(void) { return lp_rec_on() ? lp_rec_inst() : 0xFFFFu; }

/* ------------------------------------------------------------------- the knobs --- */
void lp_tempo_turn(int s, int coarse)
{
    char w[24];
    set(loops_set_tempo(loops_tempo() + (s > 0 ? 1 : -1) * (coarse ? 10 : 1)));
    ui_fmt(w, sizeof w, "%s (%d BPM)", ui_hex2(loops_tempo()), loops_bpm());
    ui_show_knob(T("TEMPO"), w);
}
static void view_turn(int s, int quiet)  /* VIEW: 1, 2 or 4 bars a screen */
{
    int v = loops_view(), k = v == 1 ? 0 : v == 2 ? 1 : 2;
    k = ui_clamp(k + (s > 0 ? 1 : -1), 0, 2);
    set(loops_set_view(1 << k));
    if (!quiet) ui_show_knob(T("VIEW"), bars_text(loops_view()));
}
static const txt_t JOBS[8] = {TX("LEVEL"), TX("PAN"), TX("DUCK"), TX("CHORUS SEND"), TX("DELAY SEND"), TX("REVERB SEND"),
                              TX("TEMPO"), TX("VIEW")};   /* (by LJ_*) */
/* K1-K4: each the job GLO gives it: the selected loop's mix value (on + NEW LOOP the next loop's, whose sends are the KEYS
 * column's: a send turned there turns FX MIXER's KEYS column, a setting; SEL held: by 16), the loops' TEMPO, or VIEW */
static void knob_job(int k, int s, int coarse)
{
    int job = loops_job(k), i = loops_sel();
    if (job == LJ_TEMPO) { lp_tempo_turn(s, coarse); return; }
    if (job == LJ_VIEW) { view_turn(s, 0); return; }
    int was = loops_mix(i)[job], v = ui_clamp(was + s * (coarse ? 16 : 1), 0, job == RM_PAN ? 255 : 127);
    if (on_loop() || job < RM_CHORUS) set(loops_set_mix(i, job, v));
    else if (v != was) ui_keys_mix_set(job, v);         /* (unsaved till SAVE; heard from the next block) */
    char w[24];
    if (job == RM_PAN) ui_fmt(w, sizeof w, "%s%s", ui_hex2(v), v == 128 ? T(" MIDDLE") : v < 128 ? T(" LEFT") : T(" RIGHT"));
    else ui_fmt(w, sizeof w, "%s %d%%", ui_hex2(v), (v * 100 + 63) / 127);
    ui_show_knob(T(JOBS[job]), w);
}
/* ALGORITHM: the play position, a row a click (SEL held: a bar); playing, the loops jump there at once (the keys held
 * stop writing; a loop landing in its rest falls silent; OVERWRITE goes on from where they land); stopped, where PLAY
 * starts */
static void alg_turn(int s, int coarse)
{
    char b[24], bb[12];
    int n = loops_rows(), d = s * (coarse ? loops_bar_rows() : 1);
    if (lp_playing_here()) {
        stage_tick();
        stop_writing();
        player_lp_jump(d);
        ui_fmt(b, sizeof b, "%s%d%s", d > 0 ? "+" : "", d, d == 1 || d == -1 ? T(" ROW") : T(" ROWS"));
        ui_show_knob(T("JUMP"), b);
        return;
    }
    lp.kept = (uint8_t)(((lp.kept + d) % n + n) % n);
    bar_beat(bb, lp.kept);
    ui_show_knob(T("PLAY FROM"), bb);
    ui_dirty();
}
/* the pipette (ENV held; SEL's TAKE ITS INSTRUMENT): the selected loop's instrument and MONO / POLY the keys'; nothing on
 * + NEW LOOP */
static void take_its(int from)
{
    char ref[8], nm[16], b[40];
    int i = loops_sel();
    if (!on_loop()) { ui_refuse(from); return; }
    ui_set_inst(loops_inst(i));
    if (loops_keys_mono() != loops_mono(i)) set(loops_set_keys_mono(loops_mono(i)));
    stage_tell();
    ui_ref_text(ref, loops_inst(i));
    inst_name(loops_inst(i), nm);
    ui_fmt(b, sizeof b, "%s %s %s, %s", T("KEYS:"), ref, nm, loops_mono(i) ? T("MONO") : T("POLY"));
    ui_toast(b);
}
static void knob(int e, int s, int coarse)
{
    sync();
    if (e == K_SELECT) {                                /* (clockwise down; the view scrolls past 4; the stage stays,
                                                         * the loop left heard again) */
        int to = ui_clamp(loops_sel() + (s > 0 ? 1 : -1), 0, top_sel());
        if (to == loops_sel()) return;
        stop_writing();
        loops_set_sel(to);
        stage_tell();
        ui_dirty();
        return;
    }
    if (e == K_ALG) { alg_turn(s, coarse); return; }
    if (e >= K_K1 && e <= K_K4) knob_job(e - K_K1, s, coarse);
}

/* --------------------------------------------------------------------- ADJUST --- */
/* SEL → ADJUST: K1 shifts the selected loop a row, K2 a bar (round the length; the loops play on, the shift heard and
 * drawn at once); SEL keeps, HOME cancels. The SHIFT is the loop's own: no note moves */
static void adj_shift(int d)
{
    int i = loops_sel();
    if (on_loop()) set(loops_set_shift(i, loops_shift(i) + d));
}
static void adj_knob(ui_overlay_t *o, int e, int s, int coarse)
{
    (void)o;
    if (e == K_K1) adj_shift(s);
    else if (e == K_K2) adj_shift(s * loops_bar_rows());
    else if (e == K_ALG) alg_turn(s, coarse);
}
static int adj_tap(ui_overlay_t *o, int b)
{
    char nm[8], sh[16];
    if (b != B_SEL) return 1;                           /* (REC, LFO, EDIT, GLO, ENV, SEQ, ARP: nothing) */
    ui_close(o);
    lp.adj = 0;
    lp_loop_name(nm, loops_sel());
    shift_text(sh, loops_shift(loops_sel()));
    ui_toast(ui_txt_v(ui_txt_v(T("#: SHIFT #"), nm), sh));
    return 1;
}
static void adj_cancel(ui_overlay_t *o)
{
    if (o) ui_close(o);
    if (on_loop() && loops_shift(loops_sel()) != lp.adj_was) set(loops_set_shift(loops_sel(), lp.adj_was));
    lp.adj = 0;
    ui_toast(T("CANCELLED"));
}
static int adj_lit(ui_overlay_t *o, int b)
{
    (void)o;
    if (b == B_SEL) return LV_FULL;
    if (b == B_HOME) return LV_DIM;
    if (b == B_REC || b == B_LFO || b == B_EDIT || b == B_GLO || b == B_ENV || b == B_SEQ || b == B_ARP) return 0;
    return LV_NONE;
}
static void adj_rec(ui_overlay_t *o) { (void)o; if (!lp_rec_quiet()) ui_refuse(B_REC); }   /* (REC: nothing, it blinks) */
static void adj_draw(ui_overlay_t *o) { (void)o; }
static void adjust_open(void)
{
    if (!on_loop()) return;
    lp.adj = 1;
    lp.adj_was = (uint8_t)loops_shift(loops_sel());
    ui_overlay_t o = {.kind = OV_OTHER, .opener = B_SEL, .knob = adj_knob, .tap = adj_tap, .cancel = adj_cancel,
                      .draw = adj_draw, .rec = adj_rec, .lit = adj_lit};
    ui_open(&o);
}

/* ------------------------------------------------------------------ the dialogs --- */
static const txt_t GRIDS[3] = {TX("1/16"), TX("1/16T"), TX("1/32")};
static const txt_t LEGATOS[3] = {TX("GLIDE"), TX("JUMP"), TX("LEGATO")};   /* (by LG_*) */
static const uint8_t LGKEY[3] = {LG_GLIDE, LG_LEGATO, LG_JUMP};             /* (K1's order) */
static const txt_t COUNTS[3] = {TX("OFF"), TX("1 BAR"), TX("2 BARS")};
static char pv[28];
static int step1(int v, int d, int n) { return ui_clamp(v + (d > 0 ? 1 : -1), 0, n - 1); }
static const char *p_tempo(void) { ui_fmt(pv, sizeof pv, "%s (%d BPM)", ui_hex2(loops_tempo()), loops_bpm()); return pv; }
static void p_tempo_ch(int d, int c) { set(loops_set_tempo(loops_tempo() + (d > 0 ? 1 : -1) * (c ? 10 : 1))); }
static const char *p_swing(void) { ui_fmt(pv, sizeof pv, "%d %%", loops_swing()); return pv; }
static void p_swing_ch(int d, int c) { (void)c; set(loops_set_swing(loops_swing() + (d > 0 ? 1 : -1))); }
/* LENGTH: a bar more or less, with loops or none; longer at once, silence added at the end; shorter only planned while
 * GLO is open (lp.plan, shown yellow), cut as GLO closes (tick), asking first when a note heard would go */
static const char *p_length(void)
{
    char now[16];
    if (!lp.plan) { ui_fmt(pv, sizeof pv, "%s", lp_len_text(loops_rows())); return pv; }
    ui_fmt(now, sizeof now, "%s", lp_len_text(loops_rows()));
    ui_fmt(pv, sizeof pv, "%s (%s %s)", bars_text(lp.plan / loops_bar_rows()), T("NOW"), now);
    return pv;
}
static int p_length_col(int on) { return lp.plan ? C_YELLOW : on ? C_WHITE : C_LGREY; }
static void length_set(int n)           /* the length n rows (loops.c: the loops written out as heard first) */
{
    stage_tick();
    stop_writing();
    edited(loops_set_rows(n), ui_edit_used(), 1);
    fit();
}
static int len_to;
static void len_answer(int a) { if (a == 1) length_set(len_to); }
static void length_apply(void)          /* GLO closed: the planned shorter length cut, asking once when notes would go */
{
    int n = lp.plan;
    char l0[24];
    lp.plan = 0;
    if (!n || n >= loops_rows()) return;
    if (!loops_cut_drops(n)) { length_set(n); return; }
    len_to = n;
    ui_fmt(l0, sizeof l0, "%s", ui_txt_v(T("CUT TO #?"), bars_text(n / loops_bar_rows())));   /* (the question copies its lines) */
    const char *l[2] = {l0, T("NOTES IN THE BARS CUT GO")};
    ui_ask(l, 2, T("CUT"), T("KEEP"), len_answer);
}
static void p_length_ch(int d, int c)
{
    (void)c;
    int from = lp.plan ? lp.plan : loops_rows(), n = loops_rows_step(from, d);
    if (n == from) return;
    if (n < loops_rows()) { lp.plan = (uint8_t)n; return; }   /* (shorter: planned, nothing cut yet) */
    lp.plan = 0;
    if (n > loops_rows()) length_set(n);
}
static int grid_fixed(void) { return loops_n() || stage_has(); }
static const char *p_grid(void) { return T(GRIDS[loops_grid()]); }
static int p_grid_col(int on) { return grid_fixed() ? C_DGREY : on ? C_WHITE : C_LGREY; }
static void p_grid_ch(int d, int c)     /* GRID: only with no loops and the stage empty; the length keeps its bars */
{
    (void)c;
    if (loops_n()) { ui_toast(T("GRID: ONLY WITH NO LOOPS")); return; }
    if (stage_has()) { ui_toast(T("GRID: ONLY WITH THE STAGE EMPTY")); return; }
    int g = step1(loops_grid(), d, 3);
    if (g == loops_grid()) return;
    stop_writing();
    lp.plan = 0;                                        /* (a planned LENGTH dropped: the rows change) */
    edited(loops_set_grid(g), ui_edit_used(), 1);
    fit();
}
static int qpos(void) { for (int i = 0; i < 5; i++) if (QKEY[i] == loops_quant()) return i; return 3; }
static const char *p_quant(void) { return T(QUANTS[loops_quant()]); }
static void p_quant_ch(int d, int c) { (void)c; set(loops_set_quant(QKEY[step1(qpos(), d, 5)])); }
static const char *p_legato(void) { return T(LEGATOS[loops_legato()]); }
static int lgpos(void) { for (int i = 0; i < 3; i++) if (LGKEY[i] == loops_legato()) return i; return 0; }
static void p_legato_ch(int d, int c) { (void)c; set(loops_set_legato(LGKEY[step1(lgpos(), d, 3)])); }
static const char *p_count(void) { return T(COUNTS[loops_countin()]); }
static void p_count_ch(int d, int c) { (void)c; set(loops_set_countin(step1(loops_countin(), d, 3))); }
static const char *p_view(void) { ui_fmt(pv, sizeof pv, "%s %s", bars_text(loops_view()), T("A SCREEN")); return pv; }
static void p_view_ch(int d, int c) { (void)c; view_turn(d, 1); }
static const char *p_job(int k) { return T(JOBS[loops_job(k)]); }
static void p_job_ch(int k, int d) { set(loops_set_job(k, step1(loops_job(k), d, 8))); }
static const char *p_k1(void) { return p_job(0); }
static const char *p_k2(void) { return p_job(1); }
static const char *p_k3(void) { return p_job(2); }
static const char *p_k4(void) { return p_job(3); }
static void p_k1_ch(int d, int c) { (void)c; p_job_ch(0, d); }
static void p_k2_ch(int d, int c) { (void)c; p_job_ch(1, d); }
static void p_k3_ch(int d, int c) { (void)c; p_job_ch(2, d); }
static void p_k4_ch(int d, int c) { (void)c; p_job_ch(3, d); }
static void props(void)
{
    lp.plan = 0;
    const ui_prop_t items[12] = {{TX("TEMPO"), p_tempo, p_tempo_ch, 0, 0, 0}, {TX("SWING"), p_swing, p_swing_ch, 0, 0, 0},
                                 {TX("LENGTH"), p_length, p_length_ch, 0, 0, p_length_col},
                                 {TX("GRID"), p_grid, p_grid_ch, 0, 0, p_grid_col},
                                 {TX("QUANTIZE"), p_quant, p_quant_ch, 0, 0, 0},
                                 {TX("MONO LEGATO"), p_legato, p_legato_ch, 0, 0, 0},
                                 {TX("COUNT-IN"), p_count, p_count_ch, 0, 0, 0}, {TX("VIEW"), p_view, p_view_ch, 0, 0, 0},
                                 {TX("K1 TURNS"), p_k1, p_k1_ch, 0, 0, 0}, {TX("K2 TURNS"), p_k2, p_k2_ch, 0, 0, 0},
                                 {TX("K3 TURNS"), p_k3, p_k3_ch, 0, 0, 0}, {TX("K4 TURNS"), p_k4, p_k4_ch, 0, 0, 0}};
    ui_props(T("LOOPS PROPERTIES"), items, 12, B_GLO);
}
static int props_on(void)
{
    ui_overlay_t *o = ui_top();
    return o && o->kind == OV_PROPS;
}

/* ENV: the cartridges, titled NEW LOOP: A CARTRIDGE (specs/ui/dialogs.md#the-cartridge-list); ENV again: the keys'
 * instrument the picked one's first. The list taken as it opens (ui_cart_nums_fill, PRESETS' too: never open together) */
static int carts_at(const uint8_t *nums, int n)
{
    for (int i = 0; i < n; i++) if (nums[i] == lp.cart) return i;
    return 0;
}
static void cl_knob(ui_overlay_t *o, int e, int s, int c)
{
    (void)o; (void)c;
    uint8_t nums[256];
    int n = ui_cart_nums(nums), i = carts_at(nums, n);
    if (e == K_SELECT && n) lp.cart = nums[ui_clamp(i + s, 0, n - 1)];
}
static int cl_tap(ui_overlay_t *o, int b)
{
    if (b != B_ENV) return 0;
    ui_close(o);
    for (int s = 0; s < ui_cart_slots(lp.cart); s++)   /* (a FAVORITES cartridge: its first favourite's source) */
        if (ui_slot_has((uint16_t)(lp.cart << 8 | s), 1)) { ui_set_inst((uint16_t)(lp.cart << 8 | s)); stage_tell(); return 1; }
    ui_toast(ui_txt_v(T("CARTRIDGE # IS EMPTY"), ui_hex2(lp.cart)));
    return 1;
}
static void cl_draw(ui_overlay_t *o)
{
    (void)o;
    uint8_t nums[256];
    int n = ui_cart_nums(nums);
    ui_cart_lines(T("NEW LOOP: A CARTRIDGE"), nums, nums, n, carts_at(nums, n), 0);
}
static void carts(void)
{
    ui_cart_nums_fill();
    lp.cart = (uint8_t)(ui_cur.place >> 8);
    ui_overlay_t o = {.kind = OV_LIST, .opener = B_ENV, .knob = cl_knob, .tap = cl_tap, .draw = cl_draw};
    ui_open(&o);
}

/* the SEL menu: TAKE ITS INSTRUMENT, APPLY INSTRUMENT, ADJUST, QUANTIZE, DELETE LOOP (on a loop), DOUBLE (when it can be
 * done), RECORD PATTERN (ARP held + REC's tap route), RECORD LIVE (CANCEL RECORD LIVE while armed), SAVE TO / LOAD FROM
 * PATTERN, CLEAR STAGE (while it holds notes; EDIT held's tap route), CLEAR LOOPS, REVERT (while LOOPS has something
 * unsaved); RECORD LIVE, SAVE TO PATTERN and CLEAR LOOPS say NO LOOPS YET with none */
enum { M_TAKE = 1, M_APPLY, M_ADJUST, M_QUANT, M_DELETE, M_DOUBLE, M_SONG, M_SONG_OFF, M_SAVE_TO, M_LOAD, M_CLEAR, M_REVERT,
       M_RECPAT, M_CLEAR_STAGE };
static void rec_open(int b1, int b2);
static int none(void) { if (loops_n()) return 0; ui_toast(T("NO LOOPS YET")); return 1; }
static void clear_answer(int a)
{
    if (a != 1 || edited(loops_clear(), ui_edit_used(), 0)) return;
    waits_drop();
    clear_stage();                                      /* (the stage emptied too; the length stays) */
    loops_set_sel(0);
    stage_tell();
    ui_toast(T("LOOPS CLEARED"));
}
static void revert_go(void)             /* LOOPS as saved (the flash's plays again, from the next row); the stage kept */
{
    stop_writing();
    ui_edit_revert(REC_LOOPS, 0, 0);
    loops_revert();
    waits_drop();
    loops_changed(1);
    fit();
    stage_tell();
}
/* SAVE TO PATTERN (specs/ui/screens/loops.md#storing-in-a-pattern-and-loading-one): the pattern list, NEW first and
 * picked (the first free number); a pattern not empty asks first */
static void store_go(void)
{
    char b[40];
    uint32_t was = ui_edit_used();
    int left;
    if (ui_edit_loops(loops_store(lp.pat, &left), was)) return;
    if (!left) ui_fmt(b, sizeof b, "%s", ui_txt_v(T("LOOPS STORED IN PATTERN #"), ui_hex2(lp.pat)));
    else if (left == 1) ui_fmt(b, sizeof b, "%s", ui_txt_v(T("PATTERN #: 1 NOTE LEFT OUT"), ui_hex2(lp.pat)));
    else ui_fmt(b, sizeof b, "%s", ui_txt_v(ui_txt_v(T("PATTERN #: # NOTES LEFT OUT"), ui_hex2(lp.pat)), ui_dec(left)));
    ui_toast(b);
}
static void store_answer(int a) { if (a == 1) store_go(); }
static void store_pick(int n)
{
    if (n == ED_PICK_NEW && (n = rec_free_pattern()) < 0) { ui_toast(T("ALL 240 PATTERNS USED")); return; }
    lp.pat = (uint8_t)n;
    if (!ed_pattern_holds((uint8_t)n)) { store_go(); return; }
    char l0[32];                                        /* (the question copies its lines) */
    ui_fmt(l0, sizeof l0, "%s", ui_txt_v(T("PATTERN # IS NOT EMPTY"), ui_hex2(n)));
    const char *l[2] = {l0, T("REPLACE IT WITH THE LOOPS?")};
    ui_ask(l, 2, T("YES"), T("NO"), store_answer);
}
/* LOAD FROM PATTERN: at the timing the pattern plays at in its song (the current song if it plays it, else the first
 * that does; a pattern in no song: the loops' own), as LOOPS takes it (loops_timing_fit) */
static void load_timing(int pat, lp_timing_t *tm)
{
    uint8_t nums[256];
    int ns = ui_songs(nums, 256), found = 0;
    tm->tempo = (uint16_t)loops_tempo();
    tm->ticks = (uint8_t)loops_ticks();
    tm->rpb = (uint8_t)loops_rpb();
    tm->swing = (uint8_t)loops_swing();
    for (int pass = 0; pass < 2 && !found; pass++)
        for (int i = 0; i < ns && !found; i++) {
            ui_song_t s;
            if ((pass == 0) != (nums[i] == ui_cur.song) || !ui_song(nums[i], &s)) continue;
            for (int p = 0; p < s.npos && !found; p++)
                if (s.order[p] == pat) {
                    tm->tempo = s.tempo;
                    tm->ticks = s.ticks;
                    tm->rpb = s.rpb;
                    tm->swing = s.swing;
                    found = 1;
                }
        }
    loops_timing_fit(tm);
}
/* the list's PLAY: the highlighted pattern at the timing LOAD would bring */
static int load_preview(int pat, uint16_t *tempo, uint8_t *ticks, uint8_t *rpb, uint8_t *swing)
{
    lp_timing_t tm;
    load_timing(pat, &tm);
    *tempo = tm.tempo;
    *ticks = tm.ticks;
    *rpb = tm.rpb;
    *swing = tm.swing;
    return 1;
}
static void load_go(void)               /* the loops stop (a song's time runs on: silent, at their start), the play position
                                         * and the selection at their start; the stage stays, fitted to the new length */
{
    char b[48], p[4];
    lp_timing_t tm;
    lp_loaded_t res;
    player_state_t st;
    uint32_t was = ui_edit_used();
    load_timing(lp.pat, &tm);
    stop_writing();
    state(&st);
    if (lp.song == SG_ON) { player_lp_rest(1); player_lp_rest(2); }
    else if (st.what == PL_LOOPS && !st.lcue) player_stop();
    lp.kept = 0;
    if (ui_edit_loops(loops_load(lp.pat, &tm, &res), was)) return;
    waits_drop();
    loops_set_sel(0);
    loops_changed(1);
    fit();
    stage_tell();
    strcpy(p, ui_hex2(lp.pat));
    if (res.insts == 1) ui_fmt(b, sizeof b, "%s", ui_txt_v(T("PATTERN # IN LOOPS: 1 INSTRUMENT LEFT OUT"), p));
    else if (res.insts) ui_fmt(b, sizeof b, "%s", ui_txt_v(ui_txt_v(T("PATTERN # IN LOOPS: # INSTRUMENTS LEFT OUT"), p), ui_dec(res.insts)));
    else if (res.cmds == 1) ui_fmt(b, sizeof b, "%s", ui_txt_v(T("PATTERN # IN LOOPS: 1 COMMAND LEFT OUT"), p));
    else if (res.cmds) ui_fmt(b, sizeof b, "%s", ui_txt_v(ui_txt_v(T("PATTERN # IN LOOPS: # COMMANDS LEFT OUT"), p), ui_dec(res.cmds)));
    else if (res.loops == 1) ui_fmt(b, sizeof b, "%s", ui_txt_v(T("PATTERN # IN LOOPS: 1 LOOP"), p));
    else ui_fmt(b, sizeof b, "%s", ui_txt_v(ui_txt_v(T("PATTERN # IN LOOPS: # LOOPS"), p), ui_dec(res.loops)));
    ui_toast(b);
}
static void load_answer(int a) { if (a == 1) load_go(); }
static void load_pick(int n)
{
    lp.pat = (uint8_t)n;
    if (!loops_n()) { load_go(); return; }
    char l1[24];                                        /* (the question copies its lines) */
    ui_fmt(l1, sizeof l1, "%s", ui_txt_v(T("WITH PATTERN #?"), ui_hex2(n)));
    const char *l[2] = {T("REPLACE THE LOOPS"), l1};
    ui_ask(l, 2, T("YES"), T("NO"), load_answer);
}
/* QUANTIZE (a loop's notes once kept): its list, then the loop's notes to the nearest line of that size */
static const uint8_t QLIST[4] = {LQ_2_ROWS, LQ_3_ROWS, LQ_4_ROWS, LQ_ROW};
static void quant_pick(int id)
{
    char nm[8];
    int i = loops_sel();
    if (!on_loop() || id < 1 || id > 4) return;
    if (edited(loops_quantize(i, QLIST[id - 1]), ui_edit_used(), 0)) return;
    lp_loop_name(nm, i);
    ui_toast(ui_txt_v(ui_txt_v(T("# QUANTIZED: #"), nm), T(QUANTS[QLIST[id - 1]])));
}
static void quant_menu(void)
{
    char t[24], nm[8];
    const ui_mitem_t m[4] = {{TX("2 ROWS"), 1, 0, 0}, {TX("3 ROWS"), 2, 0, 0}, {TX("4 ROWS"), 3, 0, 0}, {TX("ROW"), 4, 0, 0}};
    lp_loop_name(nm, loops_sel());
    ui_fmt(t, sizeof t, "%s", ui_txt_v(T("QUANTIZE #"), nm));
    ui_menu("loops-q", t, m, 4, quant_pick, B_SEL);
}
/* DELETE LOOP: at once (REVERT the way back); the loops after it move up with their mixes and waiting mutes, the selection
 * on the one in its place; the length stays; the stage stays (the loop selected now silent when it holds notes) */
static void delete_loop(void)
{
    char nm[8];
    int i = loops_sel();
    uint32_t was = ui_edit_used(), w = lp.wm, s = lp.ws, lo = (1u << i) - 1u;
    if (!on_loop() || lp_busy(B_SEL)) return;
    if (edited(loops_peel(i), was, 0)) return;
    lp.wm = (uint16_t)((w & lo) | (w >> 1 & ~lo));      /* (the waits of the loops after it move up with them) */
    lp.ws = (uint16_t)((s & lo) | (s >> 1 & ~lo));
    if (lp.wsent) waits_tell();
    loops_set_sel(ui_clamp(i, 0, top_sel()));
    stage_tell();
    lp_loop_name(nm, i);
    ui_toast(ui_txt_v(T("# DELETED"), nm));
}
/* DOUBLE: the length x2, every loop playing twice what it played, the stage played twice; refused (nothing changed) when
 * the stage twice would pass its 3,800 B or the edit memory cannot hold it */
static void double_go(void)
{
    uint32_t len, n = 0;
    int rows = loops_rows(), r;
    if (none() || lp_busy(B_SEL)) return;
    stage_tick();
    stop_writing();
    fit();
    const uint8_t *s = store_stage(&len);
    if (s) {
        n = loops_stage_double(s, len, rows, ui_scratch, STAGE_MOST);
        r = n ? store_stage_check(n) : STORE_ERR_BIG;
        if (r) { ui_refuse(B_SEL); refusal(r); return; }
    }
    r = loops_double();
    if (r == LP_NO) { ui_refuse(B_SEL); ui_toast(ui_txt_v(T("DOUBLE: AT MOST #"), bars_text(loops_max_bars()))); return; }
    if (edited(r, ui_edit_used(), 1)) return;
    if (s) stage_put(ui_scratch, n);
    lp.st_rows = (uint8_t)loops_rows();
    ui_toast(ui_txt_v(T("DOUBLED: #"), lp_len_text(loops_rows())));
}
static void menu_pick(int id)
{
    char nm[8], ref[8], in[16], b[40];
    uint32_t was = ui_edit_used();
    switch (id) {
    case M_TAKE: take_its(B_SEL); return;
    case M_APPLY:                                       /* (the keys' MONO / POLY with it) */
        if (!on_loop() || edited(loops_set_sound(loops_sel(), (uint8_t)(ui_cur.inst >> 8), (uint8_t)ui_cur.inst,
                                                 loops_keys_mono()), was, 0)) return;
        lp_loop_name(nm, loops_sel());
        ui_ref_text(ref, ui_cur.inst);
        inst_name(ui_cur.inst, in);
        ui_fmt(b, sizeof b, "%s: %s %s, %s", nm, ref, in, loops_keys_mono() ? T("MONO") : T("POLY"));
        ui_toast(b);
        return;
    case M_ADJUST: if (!lp_busy(B_SEL)) adjust_open(); return;
    case M_QUANT: if (!lp_busy(B_SEL) && on_loop()) quant_menu(); return;
    case M_DELETE: delete_loop(); return;
    case M_DOUBLE: double_go(); return;
    case M_RECPAT: rec_open(B_SEL, -1); return;
    case M_SONG:
        if (none() || lp_busy(B_SEL)) return;
        if (ui_free_song() < 0) { ui_refuse(B_SEL); ui_toast(T("NO SONG NUMBER FREE")); return; }
        sl_open();
        return;
    case M_SONG_OFF:
        song_cancel();
        ui_toast(T("CANCELLED"));
        return;
    case M_SAVE_TO:
        if (!none()) ed_pattern_list_plus(T("SAVE THE LOOPS TO"), TX("NEW"), store_pick, B_SEL);
        return;
    case M_LOAD:
        if (!lp_busy(B_SEL)) ed_pattern_list_timed(T("LOAD INTO LOOPS"), ui_cur.pattern, load_pick, B_SEL, load_preview);
        return;
    case M_CLEAR_STAGE: stage_clear(B_SEL); return;
    case M_CLEAR: {
        if (none() || lp_busy(B_SEL)) return;
        char l0[24];
        int n = loops_n();
        ui_fmt(l0, sizeof l0, "%s", n == 1 ? T("CLEAR ALL 1 LOOP?") : ui_txt_v(T("CLEAR ALL # LOOPS?"), ui_dec(n)));
        const char *l[2] = {l0, T("THE STAGE TOO; THE LENGTH STAYS")};
        ui_ask(l, 2, T("CLEAR"), T("KEEP"), clear_answer);
        return;
    }
    case M_REVERT:
        if (!lp_busy(B_SEL)) ui_ask_revert(T("REVERT LOOPS TO SAVED?"), 0, revert_go);
        return;
    default: return;
    }
}
static void menu(void)
{
    ui_mitem_t m[13];                                   /* (the menu copies its items and title) */
    char title[24];
    int k = 0;
    if (on_loop()) {
        m[k++] = (ui_mitem_t){TX("TAKE ITS INSTRUMENT"), M_TAKE, 0, 0};
        m[k++] = (ui_mitem_t){TX("APPLY INSTRUMENT"), M_APPLY, 0, 0};
        m[k++] = (ui_mitem_t){TX("ADJUST"), M_ADJUST, 0, 0};
        m[k++] = (ui_mitem_t){TX("QUANTIZE"), M_QUANT, 0, 0};
        m[k++] = (ui_mitem_t){TX("DELETE LOOP"), M_DELETE, 0, 0};
    }
    if (loops_n() && loops_rows() * 2 <= loops_max_rows()) m[k++] = (ui_mitem_t){TX("DOUBLE"), M_DOUBLE, 0, 0};
    m[k++] = (ui_mitem_t){TX("RECORD PATTERN"), M_RECPAT, 0, 0};
    if (lp.song != SG_ON)                               /* (recording: not listed) */
        m[k++] = lp.song ? (ui_mitem_t){TX("CANCEL RECORD LIVE"), M_SONG_OFF, 0, 0} : (ui_mitem_t){TX("RECORD LIVE"), M_SONG, 0, 0};
    m[k++] = (ui_mitem_t){TX("SAVE TO PATTERN"), M_SAVE_TO, 0, 0};
    m[k++] = (ui_mitem_t){TX("LOAD FROM PATTERN"), M_LOAD, 0, 0};
    if (stage_has()) m[k++] = (ui_mitem_t){TX("CLEAR STAGE"), M_CLEAR_STAGE, 0, 0};
    m[k++] = (ui_mitem_t){TX("CLEAR LOOPS"), M_CLEAR, 0, 0};
    if (ui_edit_unsaved(REC_LOOPS, 0, 0)) m[k++] = (ui_mitem_t){TX("REVERT"), M_REVERT, 0, 0};
    if (on_loop()) {
        char nm[8], in[16];
        lp_loop_name(nm, loops_sel());
        inst_name(loops_inst(loops_sel()), in);
        ui_fmt(title, sizeof title, "%s %s", nm, in);
    } else strcpy(title, T("LOOPS"));
    ui_menu("loops", title, m, k, menu_pick, B_SEL);
}

/* ----------------------------------------------------------------- the transport --- */
/* PLAY (at its press): stopped, the loops play from the play position, with loops or none; armed, after COUNT-IN;
 * playing, they stop and the play position goes back to the start (the stage kept, the keys down no longer writing).
 * A song armed with the loops not playing as it was: the loops and the song after COUNT-IN; a song recording: the loops
 * silent, or on again, on its next row (it goes on). During a count-in: cancelled */
void lp_play(void)
{
    player_state_t st;
    lp_live_poll();
    state(&st);
    if (st.count_beats) {
        player_count_stop();                            /* (the start it held dropped with it) */
        lp.arm = ARM_NONE;
        song_cancel();
        ui_toast(T("CANCELLED"));
        return;
    }
    if (lp.song == SG_ON) { player_lp_rest(st.lp_rest == 1 || st.lp_rest == 3 ? 0 : 1); return; }
    if (lp.song == SG_ARMED && !lp.splay && !(st.what == PL_LOOPS && st.playing)) { song_count(); return; }
    if (st.what == PL_LOOPS && st.playing) {
        stage_tick();
        stop_writing();
        player_stop();
        lp.kept = 0;
        return;
    }
    if (lp.arm) { count_then(); return; }
    start_play();
}
/* PLAY: at once; a song recording: as it is let go, unless held (held: its hold below) */
static int play_down(void)
{
    if (lp.song == SG_ON) { lp.pdefer = 1; lp.phold = 0; return 1; }
    lp_play();
    return 1;
}
static void play_up(int used)
{
    (void)used;
    if (!lp.pdefer) return;
    lp.pdefer = 0;
    if (!lp.phold && lp.song == SG_ON) lp_play();
    lp.phold = 0;
}
/* a button held past the hold delay: PLAY stop, back to the start (BACK TO THE START; a song recording: the loops to
 * their start, the song going on); ENV the pipette; EDIT, after 2 s (.long_hold), the stage's clear; SEQ no layer (held: the
 * keys as in CONTROL mode); ARP its layer (REC: RECORD PATTERN, ui_dlg.c), let go nothing. LFO has no hold (.tap_now) */
static int hold(int b)
{
    if (b == B_HOME) lp.hrow = -1;                      /* (HOME held: its combination, no tap) */
    if (b == B_PLAY) {
        if (lp.song == SG_ON && lp.pdefer) { player_lp_rest(2); lp.phold = 1; return 1; }
        stage_tick();
        stop_writing();
        player_stop();
        lp.kept = 0;
        ui_toast(T("BACK TO THE START"));
        return 1;
    }
    if (b == B_ENV) { take_its(B_ENV); return 1; }
    if (b == B_EDIT) { sync(); stage_clear(B_EDIT); return 1; }
    return b == B_SEQ;
}
/* HOME's press, a song recording: the row a tap would end it in */
static void press(int b)
{
    if (b == B_HOME && lp.song == SG_ON) {
        player_state_t st;
        live_poll();
        state(&st);
        lp.hrow = (int16_t)song_now(&st);
    }
}
static int can_play(void) { return 1; }                 /* (the loops play with none, over the length) */
/* REC with the stage off (not in ADJUST): the stage on, nothing sent; the loops stopped, armed at once as REC arms on an
 * empty stage, whatever the stage holds (STAGE ON: ARMED: a key or PLAY starts them writing); playing, the keys write
 * from there (STAGE ON) */
static void stage_on_rec(void)
{
    lp.off = 0;
    stage_tell();
    if (lp_playing_here()) ui_toast(T("STAGE ON"));
    else { lp.arm = ARM_WAIT; ui_toast(T("STAGE ON: ARMED")); }
    ui_dirty();
}
/* REC at its press: a count-in cancelled; RECORD LIVE armed: it starts (the loops as they are); the stage off: it turns
 * on, nothing sent, and the loops stopped it arms (stage_on_rec); else the stage's (rec_press). Each job done, nothing
 * for 500 ms; a refused REC waits for nothing. (ARP held + REC: lp_rec_with, before any of this) */
static void rec(void)
{
    player_state_t st;
    lp_live_poll();
    sync();
    state(&st);
    if (st.count_beats || lp.song == SG_COUNT) {
        if (lp_rec_quiet()) return;
        player_count_stop();
        lp.arm = ARM_NONE;
        song_cancel();
        ui_toast(T("CANCELLED"));
        lp_rec_done();
        return;
    }
    if (lp.song == SG_ARMED) { if (!lp_rec_quiet() && song_start()) lp_rec_done(); return; }
    if (lp_rec_quiet()) return;
    if (lp.off && !lp.adj) { stage_on_rec(); lp_rec_done(); return; }
    if (rec_press()) lp_rec_done();
}
/* the pattern recorder opened (ARP held + REC, at REC's release; SEL's RECORD PATTERN): refused while armed and during a
 * count-in, the buttons it came from blinking (b2 < 0: one); with the stage off it opens, its REC turning the stage on */
static void rec_open(int b1, int b2)
{
    if (lp.arm || counting()) {
        ui_refuse(b1);
        if (b2 >= 0) ui_refuse(b2);
        return;
    }
    lp_rec_open();
}
/* REC with ARP held (or let go within its grace), on the screen itself: the pattern recorder at REC's release, REC's
 * press doing none of its own jobs (ui.c) */
void lp_rec_with(void)
{
    lp_live_poll();
    sync();
    rec_open(B_REC, B_ARP);
}
/* SYNC EXTERNAL (specs/connections.md#clock): the master's transport as LOOPS' PLAY. START: the loops from their start
 * (armed: started as PLAY does, without the count-in; RECORD LIVE armed from silence starting with them); STOP: paused
 * where they are; CONTINUE: on from where they paused, or from a song position given meanwhile; a song position: that
 * place within the length (playing: a jump there). A song recording: the loops silent on STOP, on again on CONTINUE, from
 * their start on START, as PLAY does. With no loops all the same */
static void transport(int st, int spp)
{
    player_state_t ps;
    int n = loops_rows();
    if (lp.song == SG_ON) {
        if (st == 0xFC) player_lp_rest(1);
        else if (st == 0xFB) player_lp_rest(0);
        else if (st == 0xFA) { player_lp_rest(2); player_lp_rest(0); }
        return;
    }
    state(&ps);
    int here_ = ps.what == PL_LOOPS && ps.playing;
    if (st == 0xFC) {
        if (lp.arm == ARM_COUNT || lp.song == SG_COUNT) { player_count_stop(); lp.arm = ARM_WAIT; song_cancel(); }
        stage_tick();
        stop_writing();
        if (ps.playing) player_pause();
        return;
    }
    if (st == 0xFA) {
        lp.spp_on = 0;
        if (lp.arm == ARM_COUNT || lp.song == SG_COUNT) { player_count_stop(); lp.arm = ARM_WAIT; song_cancel(); }   /* (the master counts) */
        if (ps.playing && !here_) player_pause();       /* (what played on into LOOPS) */
        lp.kept = 0;
        if (lp.song == SG_ARMED && !lp.splay) { loops_play(0); song_begin(0, 0); }
        else start_play();
    } else if (st == 0xFB && !here_) {
        if (ps.playing) player_pause();
        if (lp.spp_on) lp.kept = lp.spp_row;
        lp.spp_on = 0;
        start_play();
    } else if (st == 0xF2) {
        uint8_t row = (uint8_t)((uint32_t)spp * (uint32_t)loops_rpb() / 4u % (uint32_t)n);
        if (here_) { stage_tick(); stop_writing(); player_lp_jump(((row - ps.row - 1) % n + n) % n); }
        else { lp.spp_on = 1; lp.spp_row = row; lp.kept = row; }
    }
}
/* HOME during a count-in cancels it; a song armed too; a song recording ends at the row HOME was pressed in, LOOPS staying
 * (else HOME's combination, as everywhere) */
static int back(void)
{
    if (lp.song == SG_ON) {
        int at = lp.hrow;
        lp.hrow = -1;
        song_end_here(at);
        return 1;
    }
    if (counting() || lp.song == SG_COUNT) player_count_stop();
    else if (lp.song != SG_ARMED) return 0;
    lp.arm = ARM_NONE;
    song_cancel();
    ui_toast(T("CANCELLED"));
    return 1;
}
static int tap(int b)
{
    sync();
    if (b == B_SEQ) { lp.control = !lp.control; ui_toast(lp.control ? T("KEYS: CONTROL") : T("KEYS: PLAY")); return 1; }
    if (b == B_GLO) { props(); return 1; }
    if (b == B_ENV) { carts(); return 1; }
    if (b == B_SEL) { menu(); return 1; }
    if (b == B_ARP) { stage_switch(); return 1; }
    if (b == B_EDIT) { edit_fill(); return 1; }
    if (b == B_LFO) {                                   /* (at its release: .tap_now; ADJUST, the recorder, the stage off: nothing) */
        if (lp.adj || lp_rec_on() || lp.off) return 1;
        lp.over = !lp.over;
        ui_toast(lp.over ? T("OVERWRITE") : T("OVERDUB"));
        stage_tick();
        return 1;
    }
    return 0;
}
/* REC blinking while armed, a song armed or a count-in runs; full while a song records or the stage holds notes and is
 * on (REC sends them), else dim; LFO full while OVERWRITE is on (a mode), else dim; EDIT dim on a loop, on + NEW LOOP dim
 * while the stage holds notes or is off (its tap clears it, on), else dark; SEQ full in CONTROL mode or held; ARP full
 * while the stage is on, dim off; ENV dim */
static int lit(int b)
{
    if (b == B_REC) return lp.arm || (lp.song && lp.song != SG_ON) || counting() ? (ui_blink(250) ? LV_FULL : 0)
                           : lp.song == SG_ON || (stage_has() && !lp.off) ? LV_FULL : LV_DIM;
    if (b == B_LFO) return lp.over ? LV_FULL : LV_DIM;
    if (b == B_EDIT) return on_loop() || stage_has() || lp.off ? LV_DIM : 0;
    if (b == B_SEQ) return keys_shown() ? LV_FULL : LV_DIM;
    if (b == B_ARP) return lp.off ? LV_DIM : LV_FULL;
    if (b == B_ENV) return LV_DIM;
    return LV_NONE;
}
/* CONTROL mode (or SEQ held): the white keys the loops; OP1-OP5 the QUANTIZE chosen, MONO or POLY the keys', GLO the white
 * keys' SOLO, PIT dim; PLAY mode: the keys light with their notes */
static int key_leds(uint8_t *keys)
{
    if (!keys_shown()) return 0;
    mute_leds(keys);
    for (int bi = 0; bi < 11; bi++) {
        int v = 0;
        if (bi <= 4) v = QKEY[bi] == loops_quant() ? LV_FULL : LV_DIM;
        else if (bi == 6) v = LV_DIM;
        else if (bi == 7) v = lp.solo ? LV_FULL : LV_DIM;
        else if (bi == 8 || bi == 9) v = (bi == 8) == !!loops_keys_mono() ? LV_FULL : LV_DIM;
        keys[UI_BLACK[bi]] = (uint8_t)v;
    }
    return 1;
}
static const char *seq(void) { return lp.control ? (lp.solo ? T("SOLO") : T("MUTE")) : 0; }
static const char *sub(void) { return lp.adj ? T("ADJUST") : 0; }
static int sel_lines(const char **lines, int max)
{
    if (max < 1) return 0;
    lines[0] = T("K1-K4: BIG STEPS");
    return 1;
}
static void tick(void)
{
    player_state_t st;
    sync();
    holds_poll();
    lp_rec_poll();
    if (lp.plan && !props_on()) length_apply();        /* (GLO closed: a planned shorter length cut now) */
    state(&st);
    if (lp.arm || lp.song || lp.wsent || (st.what == PL_LOOPS && (st.playing || st.lp_alone))) ui_dirty();
}
static void enter(const ui_screen_t *from)
{
    (void)from;
    lp.held = lp.done = 0;
    lp.adj = 0;
    lp.top = (uint8_t)ui_clamp(loops_sel() - 3, 0, 15);
    sync();
}
/* leaving for any screen but FX: an arm or a count-in ended, a song armed cancelled, a song's recording ended at once
 * (what was heard written), ADJUST cancelled, the pattern recorder closed, the keys down stop writing (the stage off: their
 * notes end), OVERWRITE's eraser rests (it acts on LOOPS alone); the loops and the stage play on. FX: the same for the keys
 * down and the eraser (on FX the keys are FX's) */
static void leave(const ui_screen_t *to)
{
    stage_tick();
    stop_writing();
    off_keys_end();                                     /* (nor sound in its sound: the stage off) */
    if (lp.ers) { lp.ers = 0; holds_tell(); }
    if (to && !strcmp(T(to->name), "FX")) return;
    if (counting() || lp.arm == ARM_COUNT) player_count_stop();
    lp.arm = ARM_NONE;
    if (lp.adj) adj_cancel(0);
    if (lp.song == SG_ON) song_end_here(-1);
    else song_cancel();
    lp_rec_close();
    lp.held = lp.done = 0;
}

/* ---------------------------------------------------------------------- drawing --- */
/* Each loop's notes as spans on its lanes (specs/ui/screens/loops.md#the-strips), in its base's ticks: from a note (after
 * its note delay) to its lane's next note, note off, key off (at its tick) or the base's end. Kept for every loop while
 * the store and TICKS A ROW stay as they were, as many as SPANS hold; a loop past them is read again as it is drawn (into
 * ui_scratch's first half). A span: from (12 bits), to (12), its lane << 24. The cells shown (the grid: a row a lane, 0
 * empty, 1 release, 2 attack, 3 sustain) in ui_scratch's second half, as each strip is drawn */
#define SPANS 64
typedef uint32_t span_t;
static span_t spans[SPANS];
static struct { uint32_t gen; uint8_t sp, ok, at[LP_MAX], n[LP_MAX]; } sc;   /* n: 0xFF not kept */
static int decode(const uint8_t *s, const uint8_t *end, int base, int mono, int sp, span_t *out, int max)
{
    int cur[4] = {-1, -1, -1, -1}, n = 0;
    lp_st_rd_t rd = {.mono = mono, .from = -1};
    rec_cell_t c4[LP_LANES];
#define SPAN_END(l, at) do { if (cur[l] >= 0) { if (n >= max) return -1; \
        out[n++] = (span_t)cur[l] | (span_t)(at) << 12 | (span_t)(l) << 24; cur[l] = -1; } } while (0)
    if (s) rec_cur_set_end(&rd.cu, s, end);
    for (int r = 0; s && r < base; r++) {
        int has = loops_stage_row(&rd, r, c4);
        for (int l = 0; l < LP_LANES; l++) {
            const rec_cell_t *c = &c4[l];
            if (!(has >> l & 1)) continue;
            int note = loops_is_note(c), d = 0, k = -1;
            for (int j = 0; j < c->n; j++) {
                if (note && c->cmd[j][0] == 0x0E && c->cmd[j][1] >> 4 == 0xD) d = c->cmd[j][1] & 15;
                if (c->cmd[j][0] == 0x14) k = c->cmd[j][1];
            }
            if (k >= 0 && (!note || k < d)) SPAN_END(l, r * sp + (k < sp ? k : sp));
            if ((c->has & RC_NOTE) && c->note == REC_NOTE_OFF) SPAN_END(l, r * sp);
            if (note) { SPAN_END(l, r * sp + d); cur[l] = r * sp + d; }
            if (note && k > d) SPAN_END(l, r * sp + (k < sp ? k : sp));
        }
    }
    for (int l = 0; l < 4; l++) SPAN_END(l, base * sp);
#undef SPAN_END
    return n;
}
static int decode_loop(int i, int sp, span_t *out, int max)
{
    const uint8_t *end, *s = loops_stream(i, &end);
    return decode(s, end, loops_base(i), 0, sp, out, max);
}
static void spans_keep(int sp)
{
    int at = 0;
    if (sc.ok && sc.gen == store_gen() && sc.sp == sp) return;
    for (int i = 0; i < loops_n(); i++) {
        int n = decode_loop(i, sp, spans + at, SPANS - at);
        sc.at[i] = (uint8_t)at;
        sc.n[i] = (uint8_t)(n < 0 ? 0xFF : n);
        if (n > 0) at += n;
    }
    sc.gen = store_gen();
    sc.sp = (uint8_t)sp;
    sc.ok = 1;
}
#define GRID ((uint8_t *)ui_scratch + UI_SCRATCH_HALF)
/* the cells a loop shows over the length: its base repeated as its count says, SHIFT applied; a note's first cell its
 * attack (when it is longer than one), the cells after its end up to its lane's next note its release (rel cells) */
static void grid(const span_t *sv, int ns, int base, int reps, int sh, int rel)
{
    int len = loops_rows(), sp = loops_ticks();
    uint8_t *g = GRID;
    memset(g, 0, (size_t)len * 4);
#define PUT(r, l, v) do { int k_ = (((r) + sh) % len) * 4 + (l); if (g[k_] < (v)) g[k_] = (uint8_t)(v); } while (0)
    for (int rep = 0; rep < reps && rep * base < len; rep++) {
        int off = rep * base * sp;
        for (int k = 0; k < ns; k++) {
            int l = (int)(sv[k] >> 24 & 3), a = off + (int)(sv[k] & 4095), b = off + (int)(sv[k] >> 12 & 4095);
            int r0 = a / sp, r1 = (b < len * sp ? b : len * sp) + sp - 1, nx = len * sp;
            r1 /= sp;
            if (r1 < r0 + 1) r1 = r0 + 1;
            if (r1 > len) r1 = len;
            if (r0 >= len) continue;
            for (int r = r0; r < r1; r++) PUT(r, l, r == r0 && r1 - r0 > 1 ? 2 : 3);
            for (int j = k + 1; j < ns; j++) if ((int)(sv[j] >> 24 & 3) == l) { nx = off + (int)(sv[j] & 4095); break; }
            for (int r = r1; r < r1 + rel && r < nx / sp && r < len; r++) PUT(r, l, 1);
        }
    }
#undef PUT
}
static int rel_of(uint16_t inst) { int k = ui_cart_kind((uint8_t)(inst >> 8)); return k == ENG_DX7 || k == ENG_JUNO ? 2 : 1; }
/* the window: VIEW's bars (at most the length), the page the play position is on (the last page ends at the length's
 * end) */
typedef struct { int start, v; } win_t;
#define GX 28
#define GW 180
#define SLOT_Y 33
#define SLOT_H 32
#define OV_X 229
static int play_row(const player_state_t *st)
{
    int n = loops_rows();
    if (st->what == PL_LOOPS && st->playing && !st->lcue) {
        if (st->lp_rest == 1 || st->lp_rest == 2) return st->lp_rest_row % n;   /* (silent for a song: their place) */
        return st->row % n;
    }
    return lp.kept % n;
}
static win_t window(int play)
{
    int len = loops_rows(), v = loops_view() * loops_bar_rows();
    win_t w;
    if (v > len) v = len;
    w.v = v;
    w.start = play / v * v;
    if (w.start > len - v) w.start = len - v;
    if (w.start < 0) w.start = 0;
    return w;
}
static int cell_x(int r, const win_t *w) { return GX + (r - w->start) * GW / w->v; }
/* a strip: the grid g (NULL: empty) of `lanes` lanes in the window at y, h px high, in colour col; the play line at play.
 * Empty cells deep grey, a beat's first deep light grey; the sustain in col, the attack and the release its deep shade */
static void strip(const uint8_t *g, int lanes, int y, int h, const win_t *w, int col, int play)
{
    int br = loops_bar_rows(), rpb = loops_rpb(), n = ui_clamp(lanes, 1, 4), each = (h - (n - 1)) / n;
    int off = col < 0;                  /* (the stage off: much fainter, nothing behind its notes) */
    int sus = off ? C_DEEP + C_LGREY : col, edge = off ? C_DEEP + C_DGREY : C_DEEP + col;
    if (each < 1) each = 1;
    for (int r = w->start; r < w->start + w->v; r++) {
        int x0 = cell_x(r, w), x1 = cell_x(r + 1, w), cw = x1 - x0 - (x1 - x0 >= 4 ? 1 : 0);
        int bg = off ? C_BLACK : r % rpb == 0 ? C_DEEP + C_LGREY : C_DEEP + C_DGREY;
        if (cw < 1) cw = 1;
        for (int k = 0; k < n; k++) {
            int s = g ? g[r * 4 + k] : 0;
            g_rect(x0, y + k * (each + 1), cw, each, s == 3 ? sus : s ? edge : bg);
        }
    }
    for (int r = (w->start + br - 1) / br * br; r < w->start + w->v; r += br)
        g_vline(cell_x(r, w) - 1, y - 1, h + 2, off ? C_DEEP + C_LGREY : C_LGREY);
    if (play >= w->start && play < w->start + w->v) g_vline(cell_x(play, w) - 1, y - 1, h + 2, off ? C_DGREY : C_WHITE);
}
/* the top line; ADJUST: the loop and its SHIFT; a song recording: SONG, the time recorded (M:SS, MM:SS from 10
 * minutes), its patterns so far (the one being written counted), the tempo */
static void top(const player_state_t *st)
{
    int y = BODY_Y + 2, br = loops_bar_rows(), here_ = st->what == PL_LOOPS && st->playing && !st->lcue;
    char b[32];
    if (lp.adj) {
        char nm[8], sh[16];
        lp_loop_name(nm, loops_sel());
        shift_text(sh, loops_shift(loops_sel()));
        ui_fmt(b, sizeof b, "%s", ui_txt_v(ui_txt_v(T("#: SHIFT #"), nm), sh));
        g_text(2, y, &FONT_T, b, C_WHITE);
    } else if (lp.song == SG_ON) {
        int secs = (int)((uint64_t)(st->halves - live_h0()) * 256u / 44100u), np = live_done() + 1;
        g_text(2, y, &FONT_T, T("SONG"), C_RED);
        ui_fmt(b, sizeof b, "%d:%02d", secs / 60, secs % 60);
        g_text(secs >= 600 ? 26 : 32, y, &FONT_T, b, C_LGREY);
        g_text(68, y, &FONT_T, np == 1 ? T("1 PATTERN") : ui_txt_v(T("# PATTERNS"), ui_dec(np)), C_LGREY);
        ui_fmt(b, sizeof b, "%d BPM", loops_bpm());
        g_text(146, y, &FONT_T, b, C_LGREY);
    } else {
        ui_fmt(b, sizeof b, "%d BPM", loops_bpm());
        g_text(2, y, &FONT_T, b, C_LGREY);
        g_text(50, y, &FONT_T, lp_len_text(loops_rows()), C_LGREY);   /* (a part bar in beats: `32 BEATS` at most) */
        g_text(98, y, &FONT_T, T(QTOP[loops_quant()]), C_CYAN);
        g_text(146, y, &FONT_T, lp.song ? T("SONG") : lp.arm || st->count_beats ? T("ARMED") : T("REC"),
               lp.song || lp.arm || st->count_beats ? C_RED : C_DGREY);
    }
    int nb = loops_beats(), w = 44 / nb, on = here_ ? st->row % br / loops_rpb() : -1;
    for (int k = 0; k < nb; k++) g_rect(194 + k * w, BODY_Y + 4, w - 2, 8, k == on ? (k ? C_GREEN : C_YELLOW) : C_DEEP + C_GREEN);
    g_hline(0, BODY_Y + 14, 240, C_DGREY);
}
static int staged(int i) { return stage_has() && !lp.off && i == loops_sel(); }   /* (the stage stands for it: silent) */
/* a slot's LEVEL (loop i's; + NEW LOOP's the next loop's) in hex, as K1 and the MIXER show it, at the strip's right */
static void level_text(int i, int y, int col)
{
    const char *v = ui_hex2(loops_mix(i)[RM_LEVEL]);
    g_text(226 - g_tw(&FONT_T, v), y + 14, &FONT_T, v, col);
}
/* a loop's slot (y): its number in the gutter (with the keys shown: its key), its label line (sound, P / M, repeats,
 * SHIFT, its state), its strip; the stage standing for it: silent, grey, STAGED */
static void loop_slot(int i, int y, const win_t *w, int play)
{
    int st = staged(i), dim = st || (loops_silent_mask() >> i & 1u), wt = waiting(i), ks = keys_shown(), sh = shift_signed(loops_shift(i));
    char b[20];
    if (ks) ui_fmt(b, sizeof b, "%2d", i + 1); else strcpy(b, ui_hex2(i));
    g_text(i >= 9 && ks ? 2 : 5, y + 8, &FONT_S, b, ks ? C_WHITE : dim ? C_DGREY : C_CYAN);
    inst_name(loops_inst(i), b);
    b[9] = 0;
    g_text(GX, y, &FONT_T, b, st ? C_RED : dim ? C_DGREY : LP_COL[loops_colour(i)]);
    g_text(GX + 60, y, &FONT_T, loops_mono(i) ? T("M") : T("P"), dim ? C_DGREY : C_LGREY);
    if (loops_repeats(i) > 1) {
        if (loops_repeats(i) > 9) strcpy(b, T("X+")); else ui_fmt(b, sizeof b, "X%d", loops_repeats(i));
        g_text(GX + 72, y, &FONT_T, b, dim ? C_DGREY : C_LGREY);
    }
    if (sh) { ui_fmt(b, sizeof b, "%s %s%d", T("SHIFT"), sh > 0 ? "+" : "", sh); g_text(GX + 90, y, &FONT_T, b, dim ? C_DGREY : C_LGREY); }
    const char *s = st ? T("STAGED") : wt ? (wt == 1 ? T("M") : T("S")) : loops_soloed(i) ? T("S") : loops_muted(i) ? T("M") : 0;
    if (s) g_text(226 - g_tw(&FONT_T, s), y, &FONT_T, s, st ? C_RED : wt || loops_soloed(i) ? C_YELLOW : C_DGREY);
    level_text(i, y, dim ? C_DGREY : C_LGREY);
    if (!g_shown(y + 12, 16)) return;                  /* (a strip out of the band drawn now: not decoded) */
    const span_t *sv = spans + sc.at[i];
    int n = sc.n[i];
    if (sc.n[i] == 0xFF) { sv = (const span_t *)(void *)ui_scratch; n = decode_loop(i, loops_ticks(), (span_t *)(void *)ui_scratch, UI_SCRATCH_HALF / 4); }
    grid(sv, n < 0 ? 0 : n, loops_base(i), loops_repeats(i), loops_shift(i), rel_of(loops_inst(i)));
    strip(GRID, loops_lanes(i), y + 13, 14, w, dim ? C_DGREY : LP_COL[loops_colour(i)], play);
}
/* + NEW LOOP's slot: the number it takes, the keys' instrument at the right; a dashed strip saying where REC puts the
 * stage */
static void new_slot(int y)
{
    char b[24], id[8], nm[16];
    int sy = y + 13;
    g_text(9, y + 8, &FONT_S, T("+"), C_WHITE);
    ui_fmt(b, sizeof b, "%s %s", T("NEW LOOP"), ui_hex2(loops_n()));
    g_text(GX, y, &FONT_T, b, C_WHITE);
    ui_ref_text(id, ui_cur.inst);
    inst_name(ui_cur.inst, nm);
    nm[10] = 0;
    ui_fmt(b, sizeof b, "%s %s", id, nm);
    g_text(226 - g_tw(&FONT_T, b), y, &FONT_T, b, ENG_COL[ui_cart_kind((uint8_t)(ui_cur.inst >> 8))]);
    level_text(loops_n(), y, C_LGREY);
    for (int x = GX; x < GX + GW - 1; x += 4) { g_px(x, sy, C_DGREY); g_px(x, sy + 13, C_DGREY); }
    g_vline(GX, sy, 14, C_DGREY);
    g_vline(GX + GW - 2, sy, 14, C_DGREY);
    g_text(GX + 6, sy + 1, &FONT_T, stage_has() && !on_loop() && !lp.off ? T("REC: THE STAGE GOES HERE") : T("REC: A NEW LOOP"),
           C_DGREY);
}
/* the overview column: a cell a loop in its colour (red standing for the stage, grey muted or silenced, a yellow frame
 * soloed, a yellow dot while a mute or solo waits), a grey + for + NEW LOOP; the selection a small circle; the slots in
 * view framed white */
static void overview(void)
{
    int n = loops_n(), sel = loops_sel();
    uint32_t q = loops_silent_mask();
    for (int i = 0; i < 16; i++) {
        int y = SLOT_Y + i * 8;
        if (i < n) {
            g_rect(OV_X + 2, y + 1, 6, 6, staged(i) ? C_RED : q >> i & 1u ? C_DGREY : LP_COL[loops_colour(i)]);
            if (loops_soloed(i)) g_box(OV_X + 1, y, 8, 8, C_YELLOW);
            if (waiting(i)) g_rect(OV_X + 4, y + 3, 2, 2, C_YELLOW);
        } else if (i == n) {
            g_rect(OV_X + 2, y + 3, 6, 2, C_DGREY);
            g_rect(OV_X + 4, y + 1, 2, 6, C_DGREY);
        }
    }
    int x = OV_X + 2, y = SLOT_Y + sel * 8 + 1;
    g_rect(x, y, 6, 6, C_BLACK);
    g_hline(x + 1, y, 4, C_WHITE);
    g_hline(x + 1, y + 5, 4, C_WHITE);
    g_vline(x, y + 1, 4, C_WHITE);
    g_vline(x + 5, y + 1, 4, C_WHITE);
    g_rect(x + 2, y + 2, 2, 2, sel >= n ? C_LGREY : staged(sel) ? C_RED : q >> sel & 1u ? C_DGREY : LP_COL[loops_colour(sel)]);
    int shown = slots() - lp.top < 4 ? slots() - lp.top : 4;
    g_box(OV_X - 1, SLOT_Y + lp.top * 8 - 1, 11, shown * 8 + 2, C_WHITE);
}
/* THE STAGE, a panel of its own in a dark grey frame: STAGE and its mode on the frame's top edge (OVERDUB in the stage's
 * colour, OVERWRITE red, then a dim LFO where LFO switches them; OFF while the stage is off), where REC sends it at the right
 * (off: REC: STAGE ON, as that is what REC does then); one strip, a little taller than a loop's, its notes as they play (MONO:
 * one at a time), a held
 * key's note drawn only up to the line; in the gutter a dot: red while keys overdub, deep red while the loops play, deep
 * grey stopped or off. While it is off all of it much fainter: the frame deep light grey, the texts dark grey, the strip's own */
static void stage_draw(int y0, int sh, const win_t *w, int play, int on)
{
    int ry = y0 + 14, yb = ry + sh + 2, sel = on_loop(), mono = loops_keys_mono(), keys = 0;
    int scol = sel ? LP_COL[loops_colour(loops_sel())] : LP_COL[lp_next_colour()], len = loops_rows();
    char to[24], tl[8];
    g_box(0, y0 + 6, 240, yb - y0 - 5, lp.off ? C_DEEP + C_LGREY : C_DGREY);
    g_run_t r[4] = {{T("STAGE"), C_CYAN}, {" ", C_LGREY}, {lp.over ? T("OVERWRITE") : T("OVERDUB"), lp.over ? C_RED : scol},
                    {lp.adj ? "" : T(" LFO"), C_DGREY}};
    if (lp.off) { r[0].c = C_DGREY; r[2] = (g_run_t){T("OFF"), C_DGREY}; r[3] = (g_run_t){"", C_LGREY}; }
    lp_loop_name(tl, target());
    if (lp.off) ui_fmt(to, sizeof to, "%s", T(": STAGE ON"));
    else ui_fmt(to, sizeof to, "%s %s", sel ? T(": INTO") : T(": NEW"), tl);
    const char *who = T("REC");
    g_rect(3, y0, g_runs_w(&FONT_T, r, 4) + 6, 12, C_BLACK);
    int tw = g_tw(&FONT_T, who) + g_tw(&FONT_T, to);
    g_rect(234 - tw - 3, y0, tw + 6, 12, C_BLACK);
    g_runs(6, y0, &FONT_T, r, 4);
    g_run_t t[2] = {{who, lp.off ? C_DGREY : C_YELLOW}, {to, lp.off ? C_DGREY : C_LGREY}};
    g_runs(234 - tw, y0, &FONT_T, t, 2);
    for (int n = 0; n < 27; n++) keys |= lp.key[n];
    g_disc(13, ry + (sh >> 1), 4, lp.off ? C_DEEP + C_DGREY : on && keys ? C_RED : on ? C_DEEP + C_RED : C_DEEP + C_DGREY);
    if (lp.off) scol = -1;                              /* (strip: much fainter) */
    uint32_t sl;
    const uint8_t *s = store_stage(&sl);
    if (!s) {
        strip(0, 1, ry, sh, w, scol, play);
        g_text(GX + GW, ry + ((sh - 12) >> 1), &FONT_T, T("EMPTY"), C_DGREY);   /* (its last pixel two clear of the frame) */
        return;
    }
    if (!g_shown(ry, sh)) return;                       /* (out of the band drawn now: not decoded) */
    int lanes = stage_lanes(), m = mono && lanes > 1, ns;
    span_t *sv = (span_t *)(void *)ui_scratch;
    ns = decode(s, s + sl, len, m, loops_ticks(), sv, UI_SCRATCH_HALF / 4);
    grid(sv, ns < 0 ? 0 : ns, len, 1, 0, rel_of(ui_cur.inst));
    if (on) {                                           /* (a held key's note only up to the play row) */
        uint8_t *g = GRID;
        for (int n = 0; n < 27; n++) {
            if (!lp.key[n]) continue;
            int l = m || mono ? 0 : key_lane(n), kr = lp.krow[n];
            for (int x = key_nx(n) || kr > play ? kr : play + 1; x < len; x++) {
                int starts = 0;
                for (int k = 0; k < ns && !starts; k++)
                    starts = (int)(sv[k] >> 24 & 3) == l && (int)(sv[k] & 4095) / loops_ticks() == x;
                if (x != kr && starts) break;
                g[x * 4 + l] = 0;
            }
        }
    }
    strip(GRID, m ? 1 : lanes, ry, sh, w, scol, play);
}
/* the length's bar: a segment a bar with its number (white in view, dark grey out of it), the play position's tick */
static void length_bar(int y, const win_t *w, int play)
{
    int len = loops_rows(), br = loops_bar_rows();
    char b[4];
    for (int k = 0; k * br < len; k++) {
        int x0 = GX + k * br * GW / len, x1 = GX + (k + 1) * br * GW / len, r = k * br, in = r >= w->start && r < w->start + w->v;
        ui_fmt(b, sizeof b, "%d", k + 1);
        g_text(x0, y, &FONT_T, b, in ? C_WHITE : C_DGREY);
        g_rect(x0 + 7, y + 5, x1 - x0 - 9 > 1 ? x1 - x0 - 9 : 1, 3, in ? C_LGREY : C_DEEP + C_LGREY);
    }
    g_vline(GX + play * GW / len, y + 1, 11, C_WHITE);
}
/* the hint line, only where a state changes what the keys, REC or HOME do: ADJUST, a song recording (HOME ends it), a
 * song counting in or armed (HOME cancels it), armed, CONTROL mode or SEQ held, the stage off (not while a song is armed
 * or recorded: the keys write the song then); the first that holds */
static void hint(int y, int play, int on)
{
    g_run_t r[8];
    char at[24], bb[12];
    int n = 0, Y = C_YELLOW, G = C_LGREY;
#define RUN(t, c) (r[n++] = (g_run_t){(t), (c)})
    if (lp.adj) { RUN(T("K1"), Y); RUN(T(" ROW  "), G); RUN(T("K2"), Y); RUN(T(" BAR  "), G); RUN(T("SEL"), Y); RUN(T(" KEEP  "), G);
                  RUN(T("HOME"), Y); RUN(T(" CANCEL"), G); }
    else if (lp.song == SG_ON) { RUN(T("RECORDING: "), C_RED); RUN(T("HOME"), Y); RUN(T(" ENDS AND KEEPS IT"), G); }
    else if (lp.song == SG_COUNT) { RUN(T("COUNTING IN: "), C_RED); RUN(T("HOME"), Y); RUN(T(" CANCELS"), G); }
    else if (lp.song == SG_ARMED) { RUN(T("SONG ARMED: "), C_RED); RUN(T("A NOTE STARTS  "), G); RUN(T("HOME"), Y); RUN(T(" CANCELS"), G); }
    else if (lp.arm && !on) {
        bar_beat(bb, play);
        ui_fmt(at, sizeof at, "%s %s", T(" STARTS AT"), bb);
        RUN(T("ARMED: "), C_RED); RUN(T("A NOTE OR "), G); RUN(T("PLAY"), Y); RUN(at, G);
    } else if (keys_shown()) { RUN(T("WHITE KEYS"), Y); RUN(lp.solo ? T(": SOLO  ") : T(": MUTE  "), G); RUN(T("HELD"), Y); RUN(T(": AT THE BAR"), G); }
    else if (lp.off && !lp.song) { RUN(T("STAGE OFF"), C_CYAN); RUN(T(": THE KEYS ARE NOT KEPT"), G); }
#undef RUN
    if (n) g_runs(2, y, &FONT_T, r, n);
}
static void draw(void)
{
    player_state_t st;
    int n = loops_n(), on;
    state(&st);
    on = st.what == PL_LOOPS && st.playing && !st.lcue;
    lp.top = (uint8_t)ui_clamp(lp.top, loops_sel() - 3, loops_sel());
    if (lp.top > (slots() > 4 ? slots() - 4 : 0)) lp.top = (uint8_t)(slots() > 4 ? slots() - 4 : 0);
    top(&st);
    int play = play_row(&st);
    win_t w = window(play);
    spans_keep(loops_ticks());
    for (int k = 0; k < 4; k++) {
        int i = lp.top + k, y = SLOT_Y + k * SLOT_H;
        if (i >= slots()) break;
        if (i < n) loop_slot(i, y, &w, play); else new_slot(y);
        if (i == loops_sel()) g_box(0, y - 1, 228, 29, C_WHITE);
    }
    overview();
    if (!n) {                                           /* no loops yet: how the first comes */
        g_text(2, 70, &FONT_T, T("PLAY, THEN PLAY NOTES:"), C_LGREY);   /* (the stage off: what it means then) */
        g_text(2, 82, &FONT_T, lp.off ? T("THEY ARE NOT KEPT") : T("THEY GO ON THE STAGE"), C_LGREY);
        g_text(2, 94, &FONT_T, lp.off ? T("REC: THE STAGE BACK ON") : T("REC KEEPS THE STAGE AS A LOOP"), C_LGREY);
        g_run_t r[4] = {{T("PRESETS"), C_YELLOW}, {", ", C_LGREY}, {T("ENV"), C_YELLOW}, {T(": THE SOUND"), C_LGREY}};
        g_runs(2, 112, &FONT_T, r, 4);
        g_run_t g[2] = {{T("GLO"), C_YELLOW}, {T(": LENGTH, TEMPO, VIEW"), C_LGREY}};
        g_runs(2, 124, &FONT_T, g, 2);
    }
    stage_draw(162, 18, &w, play, on);
    length_bar(199, &w, play);
    hint(214, play, on);
}
static const char *footer(uint8_t *col)
{
    *col = C_LGREY;
    if (lp_rec_on()) return T("REC: TO STAGE");
    if (lp.song == SG_ON) {                             /* (the edit memory left) */
        static char m[24];
        uint32_t used = store_edit_used(), room = store_edit_room(), all = used + room;
        *col = C_RED;
        ui_fmt(m, sizeof m, T("MEMORY %d%% FREE"), (int)(all ? (uint64_t)room * 100u / all : 0u));
        return m;
    }
    return loops_keys_mono() ? T("MONO") : T("POLY");
}

/* the state's cursor (specs/connections.md#the-state): the loops; the one selected (the count: + NEW LOOP); the notes it
 * starts; armed (bit 0), the stage holding notes (1), CONTROL mode (2), the selected loop muted (3), soloed (4), a song
 * armed (5), recording (6), the pattern recorder open (7); the length in whole bars, rounded down, never 0; the row (the
 * play position while stopped); u16 the notes all the loops start. LOOPS' bytes at the state's end: lp_state */
static void state_cursor(uint8_t *c)
{
    player_state_t st;
    int n = loops_n(), all = 0, s = loops_sel(), ns = on_loop() ? loops_note_count(s) : 0;
    state(&st);
    for (int i = 0; i < n; i++) all += loops_note_count(i);
    c[0] = (uint8_t)n;
    c[1] = (uint8_t)s;
    c[2] = (uint8_t)(ns > 255 ? 255 : ns);
    c[3] = (uint8_t)((lp.arm ? 1 : 0) | (stage_has() ? 2 : 0) | (lp.control ? 4 : 0) | (on_loop() && loops_muted(s) ? 8 : 0) |
                     (on_loop() && loops_soloed(s) ? 16 : 0) | (lp.song && lp.song != SG_ON ? 32 : 0) |
                     (lp.song == SG_ON ? 64 : 0) | (lp_rec_on() ? 128 : 0));
    c[4] = (uint8_t)(loops_rows() < loops_bar_rows() ? 1 : loops_rows() / loops_bar_rows());   /* (never 0: a part of a bar 1) */
    c[5] = (uint8_t)(st.what == PL_LOOPS && st.playing ? st.row : lp.kept);   /* (stopped: the play position) */
    c[6] = (uint8_t)all;
    c[7] = (uint8_t)(all >> 8);
}
/* what it edits (specs/ui/screens/editor.md): its properties open, the loops play at them (no Fxx); else the selected
 * loop's mix (no `=` commands on its track) */
static void editing(uint8_t *what, uint32_t *key_)
{
    const ui_overlay_t *o = ui_top();
    if (o && o->kind == OV_PROPS) {
        *what = PL_ED_TIME;
        *key_ = PL_ED_LOOPS;
    } else if (!o && on_loop()) {
        *what = PL_ED_MIX;
        *key_ = 1u << (16 + loops_sel());
    }
}

const ui_screen_t SCR_LOOPS = {
    .name = TX("LOOPS"), .group = G_PLAY,
    .does = BIT(B_GLO) | BIT(B_SEQ) | BIT(B_ENV) | BIT(B_SEL) | BIT(B_ARP) | BIT(B_REC) | BIT(B_EDIT) | BIT(B_LFO),
    .enter = enter, .leave = leave, .draw = draw, .sub = sub, .seq = seq, .footer = footer, .knob = knob, .tap = tap,
    .key = key, .back = back, .lit = lit, .key_leds = key_leds, .sel_lines = sel_lines, .play_down = play_down,
    .play_up = play_up, .hold = hold, .rec = rec,
    .press = press, .can_play = can_play, .tick = tick, .recording = rec_any, .mute_key = mute_key, .solo_key = solo_key,
    .mute_leds = mute_leds, .own_arp = UI_ARP_PATTERN, .long_hold = B_EDIT, .tap_now = BIT(B_LFO), .editing = editing, .cursor = state_cursor, .transport = transport,
    .inst = keys_inst,
};
/* the state's LOOPS bytes (ui_lp.h): the stage's notes counted in its stream; OVERWRITE on where it acts (on LOOPS, as
 * overwrite_on), OVERWRITE on, wherever it rests; the stage off (bit 3, on every screen) */
void lp_state(uint8_t *p)
{
    uint32_t len;
    const uint8_t *s = store_stage(&len);
    rec_cur_t cu;
    rec_cell_t c;
    int n = 0;
    if (s) {
        rec_cur_set_end(&cu, s, s + len);
        while (cu.row != 0x7FFF) { int r = cu.row; if (rec_cur_at(&cu, r, &c)) n += loops_is_note(&c); }
    }
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
    p[2] = (uint8_t)(on_loop() ? loops_shift(loops_sel()) : 0);
    p[3] = (uint8_t)((lp.adj ? 1 : 0) | (overwrite_on() ? 2 : 0) | (lp.over ? 4 : 0) | (lp.off ? 8 : 0));
}

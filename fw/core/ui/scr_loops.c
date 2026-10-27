/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* LOOPS (specs/ui/screens/loops.md), a loop station; the playground's is playground/scr-loops.js. Up to 16 loops over
 * one length (play/loops.h: the store's pieces), each one instrument and colour, MONO or POLY, its notes once (its base)
 * and a repeat count. REC records at the selection, one pass of the length: a new loop on + NEW LOOP, a re-record on a
 * loop (its notes gone at once); while the loops play it arms (the player's take, player_lp_take), the take starting at
 * their start; from silence a key starts both at once, PLAY after COUNT-IN. While recording the keys sound exactly as
 * written: each note landed by QUANTIZE, written into the loop's stream and asked of the player's lanes for its row
 * (player_lp_note), MONO legato, POLY 4 notes at most, through the loop's mix. SEQ: the keys PLAY or CONTROL (mutes and
 * solos, now or at the bar line; QUANTIZE; MONO / POLY); EDIT re-records, LFO peels; ARP the pattern recorder
 * (lp_rec.c); GLO the properties; SEL the menu; ENV the cartridges. A screen of the round; the loops play on outside
 * it, and what plays plays on into it until the loops start (specs/ui/navigation.md). */
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

#define ROW_Y 33                        /* loop 0's row; 12 px a row */
#define STRIP_X 111
#define STRIP_W 128
#define NO_ROW 0xFF
const uint8_t LP_COL[12] = {C_LRED, C_YELLOW, C_LGREEN, C_LCYAN, C_LBLUE, C_LMAGENTA, C_BROWN, C_GREEN, C_CYAN, C_RED,
                            C_MAGENTA, C_LGREY};
static const txt_t QUANTS[5] = {TX("ROW"), TX("2 ROWS"), TX("3 ROWS"), TX("4 ROWS"), TX("REAL TIME")};   /* LQ_* */
static const txt_t QTOP[5] = {TX("Q ROW"), TX("Q 2 ROWS"), TX("Q 3 ROWS"), TX("Q 4 ROWS"), TX("Q REAL")};
static const uint8_t QROWS[5] = {1, 2, 3, 4, 0};        /* QUANTIZE's lines (REAL TIME: none) */
static const uint8_t QKEY[5] = {LQ_2_ROWS, LQ_3_ROWS, LQ_4_ROWS, LQ_ROW, LQ_REAL};   /* OP1-OP5's, and K1's order */

/* arm: a take armed: waiting for a key or PLAY (from silence); for the loops' next start (the player's take armed);
 * counting in (the loops' start and the take posted). take: a take recording. key[n]: a key's note in the take, its
 * lane + 1 (0 none), on the next pass's row 0 (8), its delay << 4; krow[n] its row. busy[l]: each lane's last note's row
 * and its off's (NO_ROW: held), and whether a note of the take's pass took its row 0 (a note for the next pass's row 0
 * meets it: detail 3's lanes over the take's notes). wm / ws: loops whose mute / solo toggles at the next bar line (told to the player:
 * wsent; its wait seen: wseen). held: CONTROL's white keys down, a bit a key (done: held past the hold delay) */
enum { ARM_NONE, ARM_WAIT, ARM_PLAY, ARM_COUNT };
/* RECORD LIVE (play/live.h): armed by its panel (SEL's menu); counting in (PLAY started it); recording */
enum { SG_NONE, SG_ARMED, SG_COUNT, SG_ON };
#define NO_ABS 0xFFFFu
static struct {
    uint8_t control, solo;              /* the keys in CONTROL mode; its white keys solo (else mute) */
    uint8_t arm, take;
    int8_t at;                          /* the loop armed or recording (a new one: its place) */
    uint8_t re, first, mono, seen, made;   /* a re-record; the first loop; MONO; the player's take seen running; the new
                                         * loop made */
    int8_t cur;                         /* MONO: the key whose note the lane holds, -1 none */
    uint16_t pass;                      /* the loops' pass the take records */
    uint32_t t0;                        /* the halves when its start was asked for */
    uint8_t key[27], krow[27];
    uint8_t busy[4][3];
    uint16_t wm, ws, held, done;
    uint8_t wsent, wseen, wsent_h;      /* (wsent_h: the halves' count, low byte, when told) */
    uint8_t kept;                       /* the loops' row as the player last played them (0: their start) */
    uint16_t held_at[16];
    uint8_t song;                       /* RECORD LIVE: SG_* */
    uint8_t splay;                      /* the loops played as it was armed (PLAY does not start it then) */
    uint8_t sw;                         /* the player's rest switches told to the song so far (player_state().lp_sw) */
    uint8_t pdefer, phold;              /* PLAY pressed while a song records: acted on as it is let go; its hold came */
    int16_t sat;                        /* the song's row heard last */
    uint8_t kpass[27];                  /* a key's note's pattern of the song (its row there: krow) */
    uint16_t sbusy[4][2];               /* each keys' lane's last note and its end (rows of the song; NO_ABS: held) */
    uint16_t kinst;                     /* MONO: the instrument of the note the lane holds */
    uint8_t cart;                       /* ENV's list: the cartridge highlighted */
    uint8_t pat;                        /* SAVE TO / LOAD FROM PATTERN: the pattern picked */
} lp;

/* ------------------------------------------------------------------------ time --- */
static void state(player_state_t *st) { ui_player_state(st); }
int lp_playing_here(void) { player_state_t st; state(&st); return st.what == PL_LOOPS && st.playing; }
static int counting(void) { player_state_t st; state(&st); return st.count_beats != 0; }
static int speed(const player_state_t *st) { return st->speed ? st->speed : loops_ticks(); }
static int fdiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static int on_loop(void) { return loops_sel() < loops_n(); }
static int recording(void) { return lp.take && lp_playing_here(); }
static int rec_any(void) { return recording() || lp.song == SG_ON; }   /* a take or a song records (the header's ●) */
void lp_loop_name(char *dst, int i) { ui_fmt(dst, 8, T("LOOP %s"), ui_hex2(i)); }
int lp_next_colour(void) { int n = loops_n(); return n ? (loops_colour(n - 1) + 1) % 12 : 0; }
static const char *bars_text(int b) { return b == 1 ? T("1 BAR") : ui_txt_v(T("# BARS"), ui_dec(b)); }
/* a change told as an edit's (MEMORY FOR EDITS IS FULL ...), and to the player when it plays them: 0 done */
static int edited(int r, uint32_t was, int rows)
{
    if (ui_edit_loops(r, was)) return -1;
    loops_changed(rows);
    return 0;
}
static int set(int r) { return edited(r, ui_edit_used(), 0); }
static void loops_stop(void)            /* the loops stopped, back to their start (none left, or cleared) */
{
    player_state_t st;
    lp.kept = 0;                                        /* (their kept row too: the new ones from their start) */
    state(&st);
    if (st.what == PL_LOOPS && !st.lcue) player_stop();
}
int lp_busy(int b)
{
    if (lp.song) { ui_refuse(b); ui_toast(T("RECORDING A SONG")); return 1; }
    if (lp.take || lp.arm) { ui_refuse(b); return 1; }
    return 0;
}

/* --------------------------------------------------------------------- the take --- */
/* the new loop of a take on + NEW LOOP made (the current instrument, the next colour, the next loops' mode and mix),
 * selected while it records: 1 there */
static int make_loop(void)
{
    if (lp.re || lp.made) return 1;
    uint32_t was = ui_edit_used();
    int r = loops_new((uint8_t)(ui_cur.inst >> 8), (uint8_t)ui_cur.inst);
    if (r < 0) { if (r == LP_NO) ui_refuse(B_REC); else ui_edit_loops(r, was); return 0; }
    lp.at = (int8_t)r;
    lp.made = 1;
    loops_set_sel(r);
    loops_changed(lp.first);
    return 1;
}
static void keys_clear(void)
{
    memset(lp.key, 0, sizeof lp.key);
    memset(lp.busy, 0, sizeof lp.busy);
    lp.cur = -1;
}
/* the take begins (the loops' pass it records): 1 begun */
static int begin_take(int pass)
{
    player_state_t st;
    state(&st);
    lp.arm = ARM_NONE;
    if (!make_loop()) { player_lp_take(-1); return 0; }
    lp.take = 1;
    lp.pass = (uint16_t)pass;
    lp.mono = (uint8_t)loops_mono(lp.at);
    lp.seen = 0;
    lp.t0 = st.halves;
    keys_clear();
    ui_dirty();
    return 1;
}
/* from silence: the loops from their start, the take with them (the player's take posted first: the start begins it) */
static void start_take(void)
{
    if (!begin_take(0)) return;
    player_lp_take(lp.at);
    loops_play(0);
}
/* REC (or EDIT) at loop i (-1: a new one): a re-record's notes go at once (armed included); playing, it arms for the
 * loops' next start, else it waits for a key or PLAY */
static void record_at(int i)
{
    char nm[8];
    if (i >= 0) {
        if (set(loops_stream_clear(i))) return;
        lp_loop_name(nm, i);
        ui_toast(ui_txt_v(T("# RE-RECORDING"), nm));
    }
    lp.re = i >= 0;
    lp.at = (int8_t)(i >= 0 ? i : loops_n());
    lp.first = !loops_n();
    lp.made = 0;
    if (lp_playing_here()) {
        player_state_t st;
        state(&st);
        lp.arm = ARM_PLAY;
        lp.t0 = st.halves;
        player_lp_take(lp.at);          /* (a new loop made as it begins: lp.at is its place then) */
    } else lp.arm = ARM_WAIT;
    ui_dirty();
}
/* the take ends (its pass over, REC, a full edit memory, leaving): the player's with it (its notes stop); a new loop
 * with nothing played goes (NOTHING PLAYED: NO LOOP); recorded on + NEW LOOP, the selection there again. 1: a loop stays */
static int end_take(void)
{
    if (!lp.take) return 1;
    lp.take = 0;
    player_lp_take(-1);
    keys_clear();
    ui_dirty();
    if (!lp.re && !loops_has_notes(lp.at)) {
        uint32_t was = ui_edit_used();
        int r = lp.first ? loops_clear() : loops_peel(lp.at);
        if (lp.first) loops_stop();                     /* (no loops: nothing plays, their place back to the start) */
        edited(r, was, 1);
        ui_toast(T("NOTHING PLAYED: NO LOOP"));
        return 0;
    }
    if (!lp.re && loops_sel() == lp.at) loops_set_sel(lp.at + 1);
    return 1;
}
/* the first loop closes on a bar line (detail 5): before a line it runs on to it, up to a beat after one it is cut back
 * to it (what was played after it goes); at the most rows by itself. The bars are the length */
static void close_first(int at_most)
{
    player_state_t st;
    int br = loops_bar_rows(), bars = loops_max_bars();
    state(&st);
    if (!at_most) {
        bars = 1;
        if (st.what == PL_LOOPS && st.lp_pass == lp.pass) {
            int sp = speed(&st), pos = st.row * sp + st.tick, b = pos / (br * sp), into = pos - b * br * sp;
            bars = into < loops_rpb() * sp && b >= 1 ? b : b + 1;
            if (bars > loops_max_bars()) bars = loops_max_bars();
        }
    }
    edited(loops_set_rows(bars * br), ui_edit_used(), 1);
    if (end_take()) {
        char nm[8];
        lp_loop_name(nm, 0);
        ui_toastf("%s: %s", nm, bars_text(bars));
    }
}
static void stop_take(void) { if (lp.take && lp.first) close_first(0); else end_take(); }
/* a full edit memory (or a stream past its 3,800 B) ends the take there, what was written kept */
static void take_full(void) { stop_take(); ui_toast(T("MEMORY FOR EDITS IS FULL")); }
/* an armed take or a count-in given up (REC, PLAY, HOME, leaving): nothing recorded; the first loop made for the
 * count goes with it */
static void disarm(void)
{
    if (lp.arm == ARM_COUNT) player_count_stop();
    if (lp.arm == ARM_PLAY || lp.arm == ARM_COUNT) player_lp_take(-1);
    if (lp.arm == ARM_COUNT && lp.made) edited(loops_clear(), ui_edit_used(), 1);
    lp.arm = ARM_NONE;
    lp.made = 0;
    ui_dirty();
}
/* the take as the player has it: an armed one begins with the player's (the loops' start); one recording ends with it
 * (its pass over; the first loop at its most rows); the count-in over, its take begins */
static void take_sync(void)
{
    player_state_t st;
    state(&st);
    int here = st.what == PL_LOOPS && st.playing;
    if (lp.arm == ARM_PLAY) {
        if (!here) { player_lp_take(-1); lp.arm = ARM_WAIT; }   /* (the loops paused: a key or PLAY starts it then) */
        else if (st.lp_take == PL_TAKE_RUNNING && st.lp_take_loop == lp.at && begin_take(st.lp_pass)) lp.seen = 1;
    }
    if (lp.arm == ARM_COUNT && here && !st.count_beats) begin_take(0);
    if (!lp.take) return;
    if (st.lp_take == PL_TAKE_RUNNING && st.lp_take_loop == lp.at) lp.seen = 1;
    else if (st.lp_take != PL_TAKE_ARMED && (lp.seen || st.halves - lp.t0 > 8)) {   /* (armed: a key began it before
                                                         * the loops' start; just asked for: not seen yet) */
        if (lp.first) close_first(here);
        else end_take();
    }
}

/* --------------------------------------------------------------- the keys' notes --- */
/* where a note played at pos (ticks from the take's start, sp a row) lands (detail 1): its row (the length: the next
 * pass's row 0); *wait 1 when its line is ahead (heard there); *delay REAL TIME's tick, as a note delay */
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
static void cmd_add(rec_cell_t *c, int code, int v)
{
    if (c->n >= 4) return;
    c->cmd[c->n][0] = (uint8_t)code;
    c->cmd[c->n][1] = (uint8_t)v;
    c->n++;
    c->has |= RC_CMD;
}
static int cmd_at(const rec_cell_t *c, int code)
{
    for (int k = 0; k < c->n; k++) if (c->cmd[k][0] == code) return k;
    return -1;
}
static int is_note(const rec_cell_t *c) { return (c->has & RC_NOTE) && c->note != REC_NOTE_OFF; }
/* the place heard now, in ticks from the start of the loops' pass `pass` (*sp a row; the pass before: its start ahead,
 * less than 0); *ok 0 in another pass; 0 when the loops do not play */
static int take_pos(int pass, int len, int *sp, int *ok)
{
    player_state_t st;
    state(&st);
    *sp = speed(&st);
    *ok = 1;
    if (!(st.what == PL_LOOPS && st.playing)) return 0;
    int pos = st.row * *sp + st.tick;
    if (st.lp_pass + 1 == pass) return pos - len * *sp;
    if (st.lp_pass != pass) *ok = 0;
    return pos;
}
static int take_len(void) { return lp.first ? loops_max_rows() : loops_rows(); }
/* a cell the take wrote: an edit's (a full edit memory ends the take there): 0 done */
static int take_put(int row, int lane, const rec_cell_t *c)
{
    uint32_t was = ui_edit_used();
    int r = loops_cell_put(lp.at, row, lane, c);
    if (r == LP_FULL || r == LP_BUSY) { take_full(); return -1; }
    return ui_edit_loops(r, was);
}
/* a key down while a take records (or is armed with the loops playing: a key landing on their start counts): 0 when it
 * does not land in the take (it plays freely then) */
static int take_key_down(int n, int m)
{
    player_state_t st;
    state(&st);
    int len = take_len(), pass = lp.take ? lp.pass : st.lp_pass + 1, sp, ok, wait, delay;
    int pos = take_pos(pass, len, &sp, &ok);
    if (!ok) return 0;
    int row = land(pos, sp, &wait, &delay), wrapped = 0, lane = 0, glide = 0;
    if (row < 0) return 0;                              /* (before the take: played freely) */
    if (row >= len) { row = 0; wrapped = 1; }           /* the length's end: on row 0, for the next pass */
    if (!lp.take && !begin_take(pass)) return 1;
    rec_cell_t c;
    memset(&c, 0, sizeof c);
    c.has = RC_NOTE | RC_VOL;
    c.note = (uint8_t)m;
    c.vol = (uint8_t)ui_set[S_KEYS_VOLUME];
    if (delay) cmd_add(&c, 0x0E, 0xD0 | delay);
    if (lp.mono) {                                      /* MONO (detail 4): one lane; legato as MONO LEGATO says */
        int k = lp.cur;
        if (k >= 0 && (lp.krow[k] != row || !(lp.key[k] & 8) != !wrapped) && loops_legato() != LG_JUMP) {
            if (ui_cart_kind((uint8_t)(loops_inst(lp.at) >> 8)) == ENG_TB303) {   /* a TB-303 slides: SLD on the note held */
                rec_cell_t h;
                if (loops_cell(lp.at, lp.krow[k], 0, &h) && is_note(&h) && cmd_at(&h, 0xC1) < 0 && cmd_at(&h, 0xC2) < 0) {
                    int a = cmd_at(&h, 0xC0);
                    if (a >= 0) h.cmd[a][0] = 0xC2;
                    else cmd_add(&h, 0xC1, 0);
                    if (take_put(lp.krow[k], 0, &h)) return 1;
                }
                glide = 1;
            } else if (loops_legato() == LG_LEGATO) {   /* elsewhere LEGATO: LEG (on FUSION a plain note) */
                cmd_add(&c, 0xD3, 0);
                glide = (int)PL_LP_LEG;
            } else {                                    /* or a glide, 2 semitones a tick */
                cmd_add(&c, 0x03, 0x20);
                glide = 0x20;
            }
        }
    } else {                                            /* POLY (detail 3): the first lane free where it lands */
        while (lane < LP_LANES && ((lp.busy[lane][0] <= row && (lp.busy[lane][1] == NO_ROW || row < lp.busy[lane][1])) ||
                                   (!row && lp.busy[lane][2]))) lane++;
        if (lane >= LP_LANES) return 1;                 /* a fifth note: neither heard nor written */
    }
    lp.key[n] = (uint8_t)((lane + 1) | (wrapped ? 8 : 0) | delay << 4);
    lp.krow[n] = (uint8_t)row;
    lp.busy[lane][0] = (uint8_t)row;
    lp.busy[lane][1] = NO_ROW;
    if (!row && !wrapped) lp.busy[lane][2] = 1;
    if (lp.mono) lp.cur = (int8_t)n;                    /* (keys on the same row: the last registered rewrites the cell) */
    if (take_put(row, lane, &c)) return 1;
    /* heard as written: at its row (waiting for it, the take's start included), or at once; the next pass's row 0 the
     * loop's stream plays */
    if (!wrapped) player_lp_note(lp.at, lane, (uint8_t)m, c.vol, wait ? row : -1, 0, (unsigned)glide);
    return 1;
}
/* a key let go in a take (detail 2): its note off on the nearest row, at least a row after the note (REAL TIME a key off
 * at its tick); past the length nothing is written, the note ending at the take's end. A MONO key ends its note only
 * while it is the lane's */
static void take_key_up(int n)
{
    int info = lp.key[n], lane = (info & 7) - 1, nrow = lp.krow[n], ndelay = info >> 4, sp, ok;
    lp.key[n] = 0;
    if (!lp.take || lane < 0) return;
    if (lp.mono && lp.cur != n) return;
    if (lp.mono) lp.cur = -1;
    int len = take_len(), pos = take_pos(lp.pass, len, &sp, &ok), row;
    if (!ok) return;
    if (info & 8) pos -= len * sp;                      /* (its note is the next pass's row 0) */
    rec_cell_t c, there;
    memset(&c, 0, sizeof c);
    if (loops_quant() == LQ_REAL) {
        int tick = pos - fdiv(pos, sp) * sp;
        row = fdiv(pos, sp);
        if (row < nrow || (row == nrow && tick <= ndelay)) row = nrow + 1;
        cmd_add(&c, 0x14, tick);
    } else {
        row = fdiv(2 * pos + sp, 2 * sp);
        if (row < nrow + 1) row = nrow + 1;
        c.has = RC_NOTE;
        c.note = REC_NOTE_OFF;
    }
    if (lp.busy[lane][0] == nrow && lp.busy[lane][1] == NO_ROW) lp.busy[lane][1] = (uint8_t)(row > 254 ? 254 : row);
    if (row >= len) return;
    int has = loops_cell(lp.at, row, lane, &there);
    if (!(c.has & RC_NOTE) && has && row == nrow && cmd_at(&there, 0x14) < 0) {   /* (let go in its note's row, after its
                                                         * delay: that cell's key off, heard now) */
        cmd_add(&there, 0x14, c.cmd[0][1]);
        if (!take_put(row, lane, &there)) player_lp_off(lp.at, lane, -1);
        return;
    }
    if (has && is_note(&there)) return;                 /* (a note there ends it) */
    if (has && !(c.has & RC_NOTE)) { cmd_add(&there, 0x14, c.cmd[0][1]); c = there; }
    if (take_put(row, lane, &c) || (info & 8)) return;
    player_lp_off(lp.at, lane, row * sp > pos ? row : -1);
}

/* ------------------------------------------------------------------- RECORD LIVE --- */
/* RECORD LIVE (specs/ui/screens/loops.md#record-live): its panel (SEL's menu: the name, LOOPS' tempo) arms it; it starts
 * with REC, a note, or PLAY (the loops not playing as it was armed: after COUNT-IN); REC ends it at once, leaving LOOPS
 * or the loops' replay pausing anywhere too. Its time is the loops' replay's rows (rows_played from its row 0): while
 * it records the replay never stops, PLAY silencing the loops or playing them on again on the next row, held taking
 * them to their start (player_lp_rest; each switch's row told to live.c). play/live.c writes it, a pattern every 128
 * rows once they are heard (lp_live_poll, on every screen: FX keeps it); the keys land as a take's (QUANTIZE, MONO /
 * POLY as the next loops record), heard as written on the keys' lanes (player_lp_keys) */
static int song_number(void)            /* the first song number free, -1 none */
{
    ui_song_t s;
    for (int n = 0; n <= UI_SONG_LAST; n++) if (!ui_song((uint8_t)n, &s)) return n;
    return -1;
}
static void song_keys_clear(void)
{
    keys_clear();
    memset(lp.sbusy, 0xFF, sizeof lp.sbusy);
}
static char sl_name[17];                /* the panel's NAME (16 characters at most) */
/* it begins at its row 0, the loops' replay's rows_played rows0, the loops heard at their row there (0xFF: silent):
 * 1 begun */
static int song_begin(uint32_t rows0, int loops_row)
{
    player_state_t st;
    uint8_t name[12], km[RM_N];
    int n = song_number();
    if (n < 0) { lp.song = SG_NONE; ui_toast(T("NO SONG NUMBER FREE")); return 0; }
    memset(name, 0, sizeof name);
    ui_put_name6(name, 0, 16, sl_name);
    memcpy(km, ui_keys_mix, RM_N);
    km[RM_LEVEL] = 0x7F;                                /* (the keys' tracks: the KEYS column's mix, at full level) */
    state(&st);
    live_begin((uint8_t)n, name, km, (uint8_t)ui_set[S_KEYS_VOLUME], st.halves, rows0, ui_punch_now(), loops_row);
    lp.song = SG_ON;
    lp.sw = st.lp_sw;                                   /* (the player's switches before it: not its) */
    lp.pdefer = lp.phold = 0;
    lp.sat = 0;
    song_keys_clear();
    ui_dirty();
    return 1;
}
/* its start with the loops not playing (REC, a key): their replay from row 0, silent from its first row (rows_played
 * 1), their place kept for PLAY to take them on from */
static int song_begin_silent(void)
{
    loops_play_silent((uint8_t)(lp.kept < loops_rows() ? lp.kept : 0));
    return song_begin(0, 0xFF);                        /* (its row 0's rows_played: the first switch's, lp_live_poll) */
}
/* the song's row heard now (rows_played since its row 0), its tick: the time a key lands in (row 0 until the replay's
 * first row is known) */
static int song_now(const player_state_t *st, int *tick)
{
    int here = st->what == PL_LOOPS && st->playing && live_rows0(), s = here ? (int)(st->rows_played - live_rows0()) : 0;
    *tick = here && s >= 0 ? st->tick : 0;
    return s < 0 ? 0 : s;
}
/* the replay's row that song row `at` is heard at (`at` now or ahead of it: the rows after the one playing) */
static int song_replay_row(const player_state_t *st, int at)
{
    int t, now = song_now(st, &t), n = loops_rows() ? loops_rows() : 1;
    return (st->row + (at > now ? at - now : 0)) % n;
}
enum { SF_DONE, SF_FULL = LIVE_FULL, SF_PATTERNS = LIVE_PATTERNS, SF_256 };
/* it is over: the keys' notes sounding end; the toast says how it went (why: SF_*) */
static void song_finish(int why)
{
    char b[48], s[4], t[12];
    player_state_t st;
    int n = live_done(), left = live_left();
    state(&st);
    int secs = (int)((uint64_t)(st.halves - live_t0()) * 256u / 44100u);
    strcpy(s, ui_hex2(live_song()));
    lp.song = SG_NONE;
    live_stop();
    for (int l = 0; l < 4; l++) player_lp_off(PL_LP_KEYS, l, -1);
    song_keys_clear();
    if (st.what == PL_LOOPS && st.lp_rest) player_pause();   /* (the loops silent: their replay stops, at their place) */
    ui_fmt(t, sizeof t, "%d:%02d", secs / 60, secs % 60);
    if (why == SF_FULL) ui_fmt(b, sizeof b, "%s", ui_txt_v(T("SONG # STOPPED: MEMORY FOR EDITS IS FULL"), s));
    else if (why == SF_PATTERNS) ui_fmt(b, sizeof b, "%s", ui_txt_v(T("SONG # STOPPED: ALL 240 PATTERNS USED"), s));
    else if (why == SF_256) ui_fmt(b, sizeof b, "%s", ui_txt_v(T("SONG # STOPPED: 256 POSITIONS"), s));
    else if (!n) strcpy(b, T("CANCELLED"));
    else if (left == 1) ui_fmt(b, sizeof b, "%s", ui_txt_v(T("SONG #: 1 NOTE LEFT OUT"), s));
    else if (left) ui_fmt(b, sizeof b, "%s", ui_txt_v(ui_txt_v(T("SONG #: # NOTES LEFT OUT"), s), ui_dec(left)));
    else if (n == 1) ui_fmt(b, sizeof b, "%s", ui_txt_v(ui_txt_v(T("SONG # RECORDED: #, 1 PATTERN"), s), t));
    else ui_fmt(b, sizeof b, "%s", ui_txt_v(ui_txt_v(ui_txt_v(T("SONG # RECORDED: #, # PATTERNS"), s), t), ui_dec(n)));
    ui_toast(b);
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
    if (lp.song != SG_ON) { lp.song = SG_NONE; return; }
    if (at >= 0 && song_chunks(at)) return;
    live_punch_end(at + 1);
    if (at >= live_done() * LIVE_ROWS && live_done() < 256 && (r = live_chunk(at % LIVE_ROWS + 1)) != LIVE_OK) {
        song_finish(r);
        return;
    }
    song_finish(SF_DONE);
}
static void song_end_here(void)         /* ... at the row heard now (REC, leaving LOOPS) */
{
    player_state_t st;
    int tick;
    state(&st);
    song_end(!live_rows0() ? -1 : st.what == PL_LOOPS && st.playing ? song_now(&st, &tick) : lp.sat);
}
/* armed, REC or a key: the loops playing, it begins at the row playing (they play on as they were); not playing,
 * their replay starts silent for it */
static int song_start(void)
{
    player_state_t st;
    state(&st);
    if (st.what == PL_LOOPS && st.playing) return song_begin(st.rows_played, st.lp_rest ? 0xFF : st.row);
    return song_begin_silent();
}
static void song_count(void)            /* armed, PLAY (the loops not playing as it was armed): after COUNT-IN */
{
    player_state_t st;
    int ci = loops_countin(), d = loops_ticks() * loops_rpb();
    state(&st);
    if (st.playing) player_pause();                     /* (what played on into LOOPS pauses as the count starts) */
    loops_play(0);                                      /* (with COUNT-IN: it waits for the count's end) */
    if (!ci) { song_begin(0, 0); return; }              /* (a replay from row 0: its first row rows_played 1, known
                                                         * two halves on: lp_live_poll) */
    player_count_in(ci, d ? loops_tempo() * 240 / d : 1200);
    lp.song = SG_COUNT;
}
static void song_cancel(void)           /* armed or counting in: given up */
{
    if (lp.song == SG_COUNT) player_count_stop();
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
static void sl_rec(ui_overlay_t *o)     /* REC (ARM): armed, the panel closed */
{
    player_state_t st;
    ui_close(o);
    state(&st);
    lp.song = SG_ARMED;
    lp.splay = st.what == PL_LOOPS && st.playing;       /* (playing: PLAY does not start it) */
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
/* the main loop, on every screen: the song's start (the count-in's end), its row 0 known (a replay started for it:
 * rows_played 1, two halves on), the player's switches (the loops silent or on again, from their row), each pattern
 * written once its rows are heard, a punch-in, the replay pausing (it ends where it was heard last) */
static void waits_poll(void);
void lp_live_poll(void)
{
    player_state_t st;
    int here, tick;
    waits_poll();
    state(&st);
    if (st.what == PL_LOOPS && !st.lcue && !st.lp_rest) lp.kept = (uint8_t)st.row;   /* (where PLAY takes them on
                                                         * from; held for the keys or silent for a song, they keep it) */
    if (!lp.song) return;
    here = st.what == PL_LOOPS && st.playing;
    if (lp.song == SG_COUNT && here && !st.count_beats) song_begin(1, 0);
    if (lp.song != SG_ON) return;
    if (live_over()) { song_finish(SF_FULL); return; }
    if (!live_rows0()) {
        if (st.halves - live_t0() <= 2) return;
        live_rows0_set(1);
    }
    if (!here) { song_end(lp.sat); return; }
    if (st.lp_sw != lp.sw) {
        lp.sw = st.lp_sw;
        live_loops((int)(st.lp_sw_rows - live_rows0()), st.lp_sw_row);
    }
    int now = song_now(&st, &tick);
    if (song_chunks(now)) return;
    live_punch(ui_punch_now());
    lp.sat = (int16_t)now;
}
/* a key while the song records (or is armed: the key starts it, its first note): landed as a take's (land) on the
 * song's time, on a keys' lane (MONO lane 0, legato as MONO LEGATO says; POLY the first free, a fifth neither heard
 * nor written), written at its row and heard there (or at once); 0 when it does not land in the song (it plays freely
 * then) */
static int song_key_down(int n, int m)
{
    player_state_t st;
    int tick, wait, delay;
    if (lp.song == SG_ARMED && !song_start()) return 1;
    if (lp.song != SG_ON) return 0;
    state(&st);
    int sp = speed(&st), now = song_now(&st, &tick), abs = land(now * sp + tick, sp, &wait, &delay);
    int lane = 0, glide = 0, mono = loops_next_mono();
    if (abs < 0) abs = 0;
    uint16_t in = ui_cur.inst;
    if (mono) {
        int k = lp.cur;
        if (k >= 0 && lp.kpass[k] * LIVE_ROWS + lp.krow[k] != abs && loops_legato() != LG_JUMP) {
            if (ui_cart_kind((uint8_t)(lp.kinst >> 8)) == ENG_TB303) {   /* a TB-303 slides: SLD on the note held */
                live_key_slide(lp.kpass[k] * LIVE_ROWS + lp.krow[k], 0);
                glide = 1;
            } else if (ui_cart_kind((uint8_t)(in >> 8)) != ENG_TB303) {   /* (a TB-303's note after another
                                                         * instrument's starts, as it is heard: never joined) */
                glide = loops_legato() == LG_LEGATO ? (int)PL_LP_LEG : 0x20;   /* (LEGATO: LEG; else a glide, 2
                                                                                  * semitones a tick) */
            }
        }
    } else {
        while (lane < LP_LANES && lp.sbusy[lane][0] != NO_ABS && lp.sbusy[lane][0] <= abs &&
               (lp.sbusy[lane][1] == NO_ABS || abs < lp.sbusy[lane][1]))
            lane++;
        if (lane >= LP_LANES) return 1;                 /* a fifth note: neither heard nor written */
    }
    lp.key[n] = (uint8_t)((lane + 1) | (mono ? 8 : 0) | delay << 4);
    lp.krow[n] = (uint8_t)(abs % LIVE_ROWS);
    lp.kpass[n] = (uint8_t)(abs / LIVE_ROWS > 255 ? 255 : abs / LIVE_ROWS);
    lp.sbusy[lane][0] = (uint16_t)abs;
    lp.sbusy[lane][1] = NO_ABS;
    if (mono) { lp.cur = (int8_t)n; lp.kinst = in; }
    if (live_key(abs, lane, m, delay, glide == 0x20 ? 1 : glide == (int)PL_LP_LEG ? 2 : 0, in)) return 1;
    pl_inst_t pi = {(uint8_t)(in >> 8), (uint8_t)in};
    player_lp_keys(lane, pi, (uint8_t)m, (uint8_t)ui_set[S_KEYS_VOLUME], wait ? song_replay_row(&st, abs) : -1,
                   (unsigned)glide);
    return 1;
}
/* a key let go while the song records: its end as a take's (take_key_up), written and heard at its row; a MONO key ends its
 * note only while it is the lane's */
static void song_key_up(int n)
{
    player_state_t st;
    int info = lp.key[n], lane = (info & 7) - 1, delay = info >> 4, abs, kxx = -1, r, tk;
    lp.key[n] = 0;
    if (lane < 0 || lp.song != SG_ON || ((info & 8) && lp.cur != n)) return;
    if (info & 8) lp.cur = -1;
    state(&st);
    int sp = speed(&st), nabs = lp.kpass[n] * LIVE_ROWS + lp.krow[n], now = song_now(&st, &tk);
    int apos = now * sp + tk;
    if (loops_quant() == LQ_REAL) {
        int tick = apos - fdiv(apos, sp) * sp;
        abs = fdiv(apos, sp);
        if (abs < nabs || (abs == nabs && tick <= delay)) abs = nabs + 1;
        kxx = tick;
    } else {
        abs = fdiv(2 * apos + sp, 2 * sp);
        if (abs < nabs + 1) abs = nabs + 1;
    }
    if (lp.sbusy[lane][0] == nabs && lp.sbusy[lane][1] == NO_ABS) lp.sbusy[lane][1] = (uint16_t)abs;
    if (abs / LIVE_ROWS < live_done()) return;
    r = live_key_off(abs, lane, kxx, abs == nabs);
    if (r == 1) player_lp_off(PL_LP_KEYS, lane, -1);
    else if (r == 2) player_lp_off(PL_LP_KEYS, lane, abs * sp > apos ? song_replay_row(&st, abs) : -1);
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
 * calling the wait off (detail 11) */
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
 * QUANTIZE, PIT all back on, GLO MUTE / SOLO, MONO and POLY the next loops' mode; OP6 and the blank key nothing */
static void control_key(int n, int down)
{
    int wi = ui_white_index(n), bi = ui_black_index(n);
    if (wi >= 0) {
        uint16_t bit = (uint16_t)(1u << wi);
        if (down) {
            if (wi >= loops_n()) return;
            lp.held |= bit;
            lp.done &= (uint16_t)~bit;
            lp.held_at[wi] = (uint16_t)ui_now();
            return;
        }
        if (!(lp.held & bit)) return;
        lp.held &= (uint16_t)~bit;
        if (!(lp.done & bit)) toggle(wi, lp.solo, 0);
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
    } else if (bi == 8 || bi == 9) {
        set(loops_set_next_mono(bi == 8));
        ui_toast(bi == 8 ? T("NEXT LOOPS: MONO") : T("NEXT LOOPS: POLY"));
    }
}
static void holds_poll(void)                            /* CONTROL's white keys held past the hold delay: at the bar */
{
    for (int i = 0; i < 16; i++) {
        uint16_t bit = (uint16_t)(1u << i);
        if (!(lp.held & bit) || (lp.done & bit) || (uint16_t)((uint16_t)ui_now() - lp.held_at[i]) < (uint16_t)ui_set[S_HOLD_DELAY])
            continue;
        lp.done |= bit;
        toggle(i, lp.solo, 1);
    }
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

/* -------------------------------------------------------------------- the keys --- */
/* PLAY mode: a take armed from silence starts with the key's note; a take writes it; else the keys play freely (the
 * core's, a re-record's loop's instrument from its REC on). CONTROL mode: control_key */
static int key(int n, int down)
{
    take_sync();
    lp_live_poll();
    if (!down) {
        if (lp.key[n] && lp.song) song_key_up(n);
        else if (lp.key[n]) take_key_up(n);
        else control_key(n, 0);
        return 1;
    }
    if (lp.control) { control_key(n, 1); return 1; }
    int m = ui_midi(n);
    if (m < 12 || m > 127) return 1;
    if ((lp.song == SG_ON || lp.song == SG_ARMED) && song_key_down(n, m)) return 1;
    if (lp.arm == ARM_WAIT && !counting()) { start_take(); take_key_down(n, m); return 1; }
    if ((lp.take || lp.arm == ARM_PLAY) && take_key_down(n, m)) return 1;
    return 0;
}
/* the instrument the keys play and the footer names: a re-record's loop's from its REC until its take ends; the
 * pattern recorder's */
static uint16_t keys_inst(void)
{
    if (lp_rec_on()) return lp_rec_inst();
    if ((lp.take || lp.arm) && lp.re && lp.at < loops_n()) return loops_inst(lp.at);
    return 0xFFFFu;
}

/* ------------------------------------------------------------------- the knobs --- */
void lp_tempo_turn(int s, int coarse)
{
    char w[24];
    set(loops_set_tempo(loops_tempo() + (s > 0 ? 1 : -1) * (coarse ? 10 : 1)));
    ui_fmt(w, sizeof w, "%s (%d BPM)", ui_hex2(loops_tempo()), loops_bpm());
    ui_show_knob(T("TEMPO"), w);
}
/* K1-K4: the selected loop's LEVEL, PAN, DELAY SEND, REVERB SEND; on + NEW LOOP the next loop's (SEL held: by 16) */
static void knob(int e, int s, int coarse)
{
    if (e == K_SELECT) { loops_set_sel(loops_sel() + (s > 0 ? 1 : -1)); ui_dirty(); return; }   /* (clockwise down) */
    if (e == K_ALG) { lp_tempo_turn(s, coarse); return; }
    if (e < K_K1 || e > K_K4) return;
    static const uint8_t KK[4] = {RM_LEVEL, RM_PAN, RM_DELAY, RM_REVERB};
    static const txt_t KN[4] = {TX("LEVEL"), TX("PAN"), TX("DELAY SEND"), TX("REVERB SEND")};
    int i = loops_sel(), k = KK[e - K_K1], v = ui_clamp(loops_mix(i)[k] + s * (coarse ? 16 : 1), 0, k == RM_PAN ? 255 : 127);
    set(loops_set_mix(i, k, v));
    char w[24];
    if (k == RM_PAN) ui_fmt(w, sizeof w, "%s%s", ui_hex2(v), v == 128 ? T(" MIDDLE") : v < 128 ? T(" LEFT") : T(" RIGHT"));
    else ui_fmt(w, sizeof w, "%s %d%%", ui_hex2(v), (v * 100 + 63) / 127);
    ui_show_knob(T(KN[e - K_K1]), w);
}

/* ------------------------------------------------------------------ the dialogs --- */
static const txt_t GRIDS[3] = {TX("1/16"), TX("1/16T"), TX("1/32")};
static const txt_t LEGATOS[3] = {TX("GLIDE"), TX("JUMP"), TX("LEGATO")};   /* (by LG_*) */
static const uint8_t LGKEY[3] = {LG_GLIDE, LG_LEGATO, LG_JUMP};             /* (K1's order) */
static const txt_t COUNTS[3] = {TX("OFF"), TX("1 BAR"), TX("2 BARS")};
static char pv[24];
static int step1(int v, int d, int n) { return ui_clamp(v + (d > 0 ? 1 : -1), 0, n - 1); }
static const char *p_tempo(void) { ui_fmt(pv, sizeof pv, "%s (%d BPM)", ui_hex2(loops_tempo()), loops_bpm()); return pv; }
static void p_tempo_ch(int d, int c) { set(loops_set_tempo(loops_tempo() + (d > 0 ? 1 : -1) * (c ? 10 : 1))); }
static const char *p_swing(void) { ui_fmt(pv, sizeof pv, "%d %%", loops_swing()); return pv; }
static void p_swing_ch(int d, int c) { (void)c; set(loops_set_swing(loops_swing() + (d > 0 ? 1 : -1))); }
static const char *p_length(void)
{
    if (!loops_rows()) return T("THE FIRST LOOP SETS IT");
    ui_fmt(pv, sizeof pv, "%s", bars_text(loops_rows() / loops_bar_rows()));
    return pv;
}
/* LENGTH: a bar more or less; longer adds silence at the end, shorter cuts (detail 9), asking first when notes would
 * go */
static int len_to;
static void len_set(void) { edited(loops_set_rows(len_to), ui_edit_used(), 1); }
static void len_answer(int a) { if (a == 1) len_set(); }
static void p_length_ch(int d, int c)
{
    (void)c;
    if (lp.song) { ui_refuse(B_GLO); ui_toast(T("RECORDING A SONG")); return; }
    if (!loops_rows() || lp.take || lp.arm) { ui_refuse(B_GLO); return; }
    int br = loops_bar_rows(), bars = ui_clamp(loops_rows() / br + (d > 0 ? 1 : -1), 1, loops_max_bars());
    len_to = bars * br;
    if (len_to == loops_rows()) return;
    if (len_to > loops_rows() || !loops_cut_drops(len_to)) { len_set(); return; }
    char l0[24];                                        /* (the question copies its lines) */
    ui_fmt(l0, sizeof l0, "%s", bars == 1 ? T("CUT TO 1 BAR?") : ui_txt_v(T("CUT TO # BARS?"), ui_dec(bars)));
    const char *l[2] = {l0, T("NOTES IN THE BARS CUT GO")};
    ui_ask(l, 2, T("CUT"), T("KEEP"), len_answer);
}
static const char *p_grid(void) { return T(GRIDS[loops_grid()]); }
static void p_grid_ch(int d, int c)
{
    (void)c;
    if (lp.song) { ui_refuse(B_GLO); ui_toast(T("RECORDING A SONG")); return; }
    if (loops_n() || lp.take || lp.arm) { ui_toast(T("GRID: ONLY BEFORE THE FIRST LOOP")); return; }
    set(loops_set_grid(step1(loops_grid(), d, 3)));
}
static int qpos(void) { for (int i = 0; i < 5; i++) if (QKEY[i] == loops_quant()) return i; return 3; }
static const char *p_quant(void) { return T(QUANTS[loops_quant()]); }
static void p_quant_ch(int d, int c) { (void)c; set(loops_set_quant(QKEY[step1(qpos(), d, 5)])); }
static const char *p_legato(void) { return T(LEGATOS[loops_legato()]); }
static int lgpos(void) { for (int i = 0; i < 3; i++) if (LGKEY[i] == loops_legato()) return i; return 0; }
static void p_legato_ch(int d, int c) { (void)c; set(loops_set_legato(LGKEY[step1(lgpos(), d, 3)])); }
static const char *p_count(void) { return T(COUNTS[loops_countin()]); }
static void p_count_ch(int d, int c) { (void)c; set(loops_set_countin(step1(loops_countin(), d, 3))); }
static void props(void)
{
    const ui_prop_t items[7] = {{TX("TEMPO"), p_tempo, p_tempo_ch, 0, 0, 0}, {TX("SWING"), p_swing, p_swing_ch, 0, 0, 0},
                                {TX("LENGTH"), p_length, p_length_ch, 0, 0, 0}, {TX("GRID"), p_grid, p_grid_ch, 0, 0, 0},
                                {TX("QUANTIZE"), p_quant, p_quant_ch, 0, 0, 0},
                                {TX("MONO LEGATO"), p_legato, p_legato_ch, 0, 0, 0},
                                {TX("COUNT-IN"), p_count, p_count_ch, 0, 0, 0}};
    ui_props(T("LOOPS PROPERTIES"), items, 7, B_GLO);
}

/* ENV: the cartridges, titled NEW LOOP: A CARTRIDGE (specs/ui/dialogs.md#the-cartridge-list); ENV again: the current
 * instrument the picked one's first. The list made as it is drawn (no copy kept) */
static int carts_at(uint8_t *nums, int *n)
{
    *n = ui_carts(nums, 256);
    for (int i = 0; i < *n; i++) if (nums[i] == lp.cart) return i;
    return 0;
}
static void cl_knob(ui_overlay_t *o, int e, int s, int c)
{
    (void)o; (void)c;
    uint8_t nums[256];
    int n, i = carts_at(nums, &n);
    if (e == K_SELECT && n) lp.cart = nums[ui_clamp(i + s, 0, n - 1)];
}
static int cl_tap(ui_overlay_t *o, int b)
{
    if (b != B_ENV) return 0;
    ui_close(o);
    char nm[16];
    for (int s = 0; s < ui_cart_slots(lp.cart); s++)
        if (ui_inst_name((uint16_t)(lp.cart << 8 | s), nm)) { ui_set_inst((uint16_t)(lp.cart << 8 | s)); return 1; }
    ui_toast(ui_txt_v(T("CARTRIDGE # IS EMPTY"), ui_hex2(lp.cart)));
    return 1;
}
static void cl_draw(ui_overlay_t *o)
{
    (void)o;
    uint8_t nums[256];
    int n, sel = carts_at(nums, &n), y = BODY_Y + 2, rows = 13;
    ui_panel(2, y, 236, 206, T("NEW LOOP: A CARTRIDGE"), C_CYAN);
    g_text(8, y + 16, &FONT_T, T("ID NAME         TYPE      SLOTS"), C_DGREY);
    int top = ui_clamp(sel - 6, 0, n - rows > 0 ? n - rows : 0);
    for (int i = top; i < n && i < top + rows; i++) {
        uint8_t c = nums[i];
        int yy = y + 30 + (i - top) * 13, on = i == sel, k = ui_cart_kind(c), used = 0;
        char nm[16], cnt[12];
        for (int s = 0; s < ui_cart_slots(c); s++) used += ui_inst_name((uint16_t)(c << 8 | s), nm);
        if (on) g_box(4, yy - 1, 232, 13, C_WHITE);
        g_text(8, yy, &FONT_T, ui_hex2(c), ins_builtin(c) ? C_BROWN : on ? C_WHITE : C_LGREY);   /* (built in: brown) */
        if (!ui_cart_name(c, nm)) nm[0] = 0;
        nm[12] = 0;
        g_text(26, yy, &FONT_T, nm, on ? C_WHITE : C_LGREY);
        g_text(110, yy, &FONT_T, T(ENG_TYPE[k]), ENG_COL[k]);
        ui_fmt(cnt, sizeof cnt, "%03d/%03d", used, ENG_SLOTS[k]);
        g_text(170, yy, &FONT_T, cnt, C_LGREY);
    }
}
static void carts(void)
{
    lp.cart = (uint8_t)(ui_cur.inst >> 8);
    ui_overlay_t o = {.kind = OV_LIST, .opener = B_ENV, .knob = cl_knob, .tap = cl_tap, .draw = cl_draw};
    ui_open(&o);
}

/* EDIT: the selected loop recorded again (on + NEW LOOP the last), armed as REC arms */
static void re_record(void)
{
    int n = loops_n();
    if (!n) { ui_refuse(B_EDIT); ui_toast(T("NO LOOPS YET")); return; }
    if (lp_busy(B_EDIT)) return;
    record_at(on_loop() ? loops_sel() : n - 1);
}
/* LFO: PEEL, the selected loop goes (on + NEW LOOP the last); the selection stays where it was; the only loop takes the
 * length with it */
static void peel(void)
{
    int n = loops_n(), i = on_loop() ? loops_sel() : n - 1;
    char nm[8];
    if (!n) { ui_refuse(B_LFO); ui_toast(T("NO LOOPS YET")); return; }
    if (lp_busy(B_LFO)) return;
    uint32_t was = ui_edit_used(), w = lp.wm, s = lp.ws, lo = (1u << i) - 1u;
    if (edited(loops_peel(i), was, 1)) return;
    lp.wm = (uint16_t)((w & lo) | (w >> 1 & ~lo));      /* (the waits of the loops after it move up with them) */
    lp.ws = (uint16_t)((s & lo) | (s >> 1 & ~lo));
    if (lp.wsent) waits_tell();
    if (!loops_n()) loops_stop();
    lp_loop_name(nm, i);
    ui_toast(ui_txt_v(T("# PEELED"), nm));
}

/* the SEL menu: APPLY INSTRUMENT (on a loop), DOUBLE, RECORD LIVE (CANCEL RECORD LIVE while armed), SAVE TO / LOAD
 * FROM PATTERN, CLEAR LOOPS, REVERT (while LOOPS has something unsaved); those with loops say NO LOOPS YET without */
enum { M_REPLACE = 1, M_DOUBLE, M_SONG, M_SONG_OFF, M_SAVE_TO, M_LOAD, M_CLEAR, M_REVERT };
static int none(void) { if (loops_n()) return 0; ui_toast(T("NO LOOPS YET")); return 1; }
static void clear_answer(int a)
{
    if (a != 1 || edited(loops_clear(), ui_edit_used(), 1)) return;
    waits_drop();
    loops_stop();
    loops_set_sel(0);
    ui_toast(T("LOOPS CLEARED"));
}
static void revert_go(void)
{
    ui_edit_revert(REC_LOOPS, 0, 0);
    loops_revert();
    waits_drop();
    if (!loops_rows()) loops_stop();
    loops_changed(1);
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
    if (n == ED_PICK_NEW && (n = ed_first_free()) < 0) { ui_toast(T("ALL 240 PATTERNS USED")); return; }
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
static void load_go(void)
{
    char b[48], p[4];
    lp_timing_t tm;
    lp_loaded_t res;
    uint32_t was = ui_edit_used();
    load_timing(lp.pat, &tm);
    loops_stop();                                       /* (the loops paused at their start, the new ones from it) */
    if (ui_edit_loops(loops_load(lp.pat, &tm, &res), was)) return;
    waits_drop();
    loops_set_sel(0);
    loops_changed(1);
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

static void menu_pick(int id)
{
    char nm[8], ref[8], in[16], b[32];
    uint32_t was = ui_edit_used();
    switch (id) {
    case M_REPLACE:
        if (lp.song) { ui_refuse(B_SEL); ui_toast(T("RECORDING A SONG")); return; }
        if (!on_loop() || edited(loops_set_inst(loops_sel(), (uint8_t)(ui_cur.inst >> 8), (uint8_t)ui_cur.inst), was, 0)) return;
        lp_loop_name(nm, loops_sel());
        ui_ref_text(ref, ui_cur.inst);
        if (!ui_inst_name(ui_cur.inst, in)) strcpy(in, T("EMPTY"));
        ui_fmt(b, sizeof b, "%s: %s %s", nm, ref, in);
        ui_toast(b);
        return;
    case M_DOUBLE: {
        if (none() || lp_busy(B_SEL)) return;
        int r = loops_double();
        if (r == LP_NO) { ui_refuse(B_SEL); ui_toast(ui_txt_v(T("DOUBLE: AT MOST # BARS"), ui_dec(loops_max_bars()))); return; }
        if (edited(r, was, 1)) return;
        ui_toast(ui_txt_v(T("DOUBLED: # BARS"), ui_dec(loops_rows() / loops_bar_rows())));
        return;
    }
    case M_SONG:
        if (none() || lp_busy(B_SEL)) return;
        if (song_number() < 0) { ui_refuse(B_SEL); ui_toast(T("NO SONG NUMBER FREE")); return; }
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
    case M_CLEAR: {
        if (none() || lp_busy(B_SEL)) return;
        char l0[24];
        int n = loops_n();
        ui_fmt(l0, sizeof l0, "%s", n == 1 ? T("CLEAR ALL 1 LOOP?") : ui_txt_v(T("CLEAR ALL # LOOPS?"), ui_dec(n)));
        const char *l[2] = {l0, T("THE NEXT ONE SETS THE LENGTH")};
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
    ui_mitem_t m[7];                                    /* (the menu copies its items and title) */
    char title[24];
    int k = 0;
    if (on_loop()) m[k++] = (ui_mitem_t){TX("APPLY INSTRUMENT"), M_REPLACE, 0, 0};
    m[k++] = (ui_mitem_t){TX("DOUBLE"), M_DOUBLE, 0, 0};
    m[k++] = lp.song && lp.song != SG_ON ? (ui_mitem_t){TX("CANCEL RECORD LIVE"), M_SONG_OFF, 0, 0}
                                         : (ui_mitem_t){TX("RECORD LIVE"), M_SONG, 0, 0};
    m[k++] = (ui_mitem_t){TX("SAVE TO PATTERN"), M_SAVE_TO, 0, 0};
    m[k++] = (ui_mitem_t){TX("LOAD FROM PATTERN"), M_LOAD, 0, 0};
    m[k++] = (ui_mitem_t){TX("CLEAR LOOPS"), M_CLEAR, 0, 0};
    if (ui_edit_unsaved(REC_LOOPS, 0, 0)) m[k++] = (ui_mitem_t){TX("REVERT"), M_REVERT, 0, 0};
    if (on_loop()) {
        char nm[8], in[16];
        lp_loop_name(nm, loops_sel());
        if (!ui_inst_name(loops_inst(loops_sel()), in)) strcpy(in, T("EMPTY"));
        ui_fmt(title, sizeof title, "%s %s", nm, in);
    } else strcpy(title, T("LOOPS"));
    ui_menu("loops", title, m, k, menu_pick, B_SEL);
}

/* ----------------------------------------------------------------- the transport --- */
/* PLAY: plays / pauses the loops (a take ends as REC ends it); with no loops pauses what plays, else refused; armed with
 * nothing playing: the loops and the take after COUNT-IN */
void lp_play(void)
{
    player_state_t st;
    take_sync();
    state(&st);
    if (lp.arm == ARM_COUNT || lp.song == SG_COUNT) { disarm(); song_cancel(); ui_toast(T("CANCELLED")); return; }
    if (lp.song == SG_ON) {                             /* (a song recording: the loops silent, or on again, on its next
                                                         * row; it goes on) */
        player_lp_rest(st.lp_rest == 1 || st.lp_rest == 3 ? 0 : 1);
        return;
    }
    if (lp.song == SG_ARMED && !lp.splay && !(st.what == PL_LOOPS && st.playing)) { song_count(); return; }
    if (st.what == PL_LOOPS && st.playing) {
        if (lp.take) stop_take();
        if (lp.arm == ARM_PLAY) { player_lp_take(-1); lp.arm = ARM_WAIT; }   /* (paused while armed: a key or PLAY then) */
        player_pause();
        return;
    }
    if (lp.arm == ARM_WAIT) {                           /* the loops and the take from their start, after COUNT-IN */
        int ci = loops_countin(), d = loops_ticks() * loops_rpb();
        if (st.playing) player_pause();                 /* (what played on into LOOPS pauses as the count starts) */
        if (!ci) { start_take(); return; }
        if (lp.first && !make_loop()) return;           /* (the first loop now: the clock is its rows) */
        player_count_in(ci, d ? loops_tempo() * 240 / d : 1200);
        player_lp_take(lp.at);
        loops_play(0);                                  /* (it waits for the count's end) */
        lp.arm = ARM_COUNT;
        lp.t0 = st.halves;
        return;
    }
    if (!loops_n()) {
        if (st.playing) { player_pause(); return; }     /* (a song played on into LOOPS: paused) */
        ui_refuse(B_PLAY);
        ui_toast(T("REC, THEN PLAY: THE FIRST LOOP"));
        return;
    }
    if (st.what == PL_LOOPS && !st.lcue) player_continue();
    else loops_play(lp.kept < loops_rows() ? lp.kept : 0);   /* (something else played meanwhile: from where they paused) */
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
/* PLAY held: stop, back to the start, as on every screen; a song recording: the loops to their start (playing: from the
 * top on its next row; silent: waiting there), the song going on */
static int hold(int b)
{
    if (b != B_PLAY) return 0;
    if (lp.song == SG_ON && lp.pdefer) { player_lp_rest(2); lp.phold = 1; return 1; }
    player_stop();
    return 1;
}
static int can_play(void) { player_state_t st; state(&st); return loops_n() > 0 || lp.arm || st.playing; }
/* REC: a take (or its end, its disarming); RECORD LIVE armed: it starts (the loops as they are); recording: it ends at
 * once */
static void rec(void)
{
    take_sync();
    if (lp.arm == ARM_COUNT || lp.song == SG_COUNT) { disarm(); song_cancel(); ui_toast(T("CANCELLED")); return; }
    if (lp.song == SG_ARMED) { song_start(); return; }
    if (lp.song == SG_ON) { song_end_here(); return; }
    if (lp.arm) { disarm(); ui_toast(T("CANCELLED")); return; }
    if (lp.take) { stop_take(); return; }
    record_at(on_loop() ? loops_sel() : -1);
}
/* SYNC EXTERNAL: START from the start, STOP, CONTINUE; a song position: that bar within the length, while they play (a
 * song recording: the loops silent on STOP, on again on CONTINUE, from their start on START, as PLAY does) */
static void transport(int st, int spp)
{
    int n = loops_rows();
    if (lp.song == SG_ON) {
        if (st == 0xFC) player_lp_rest(1);
        else if (st == 0xFB) player_lp_rest(0);
        else if (st == 0xFA) { player_lp_rest(2); player_lp_rest(0); }
        return;
    }
    if (!loops_n() || lp.take || !n) return;
    if (st == 0xFA) loops_play(0);
    else if (st == 0xFC && lp_playing_here()) player_pause();
    else if (st == 0xFB && !lp_playing_here()) player_continue();
    else if (st == 0xF2 && lp_playing_here()) loops_play((uint8_t)((uint32_t)spp * (uint32_t)loops_rpb() / 4u % (uint32_t)n));
}
/* HOME during a count-in cancels it (else HOME's combination, as everywhere) */
static int back(void)
{
    if (lp.arm != ARM_COUNT && lp.song != SG_COUNT) return 0;
    disarm();
    song_cancel();
    ui_toast(T("CANCELLED"));
    return 1;
}
static int tap(int b)
{
    if (b == B_SEQ) { lp.control = !lp.control; ui_toast(lp.control ? T("KEYS: CONTROL") : T("KEYS: PLAY")); return 1; }
    if (b == B_GLO) { props(); return 1; }
    if (b == B_ENV) { carts(); return 1; }
    if (b == B_SEL) { menu(); return 1; }
    if (b == B_ARP) { if (!lp_busy(B_ARP)) lp_rec_open(); return 1; }
    if (b == B_EDIT) { re_record(); return 1; }
    if (b == B_LFO) { peel(); return 1; }
    return 0;
}
static int lit(int b)
{
    if (b == B_REC) return lp.arm || (lp.song && lp.song != SG_ON) ? (ui_blink(250) ? LV_FULL : 0) : rec_any() ? LV_FULL : LV_DIM;
    if (b == B_EDIT || b == B_LFO) return loops_n() ? LV_DIM : 0;
    if (b == B_SEQ) return lp.control ? LV_FULL : LV_DIM;
    if (b == B_ARP) return LV_DIM;
    return LV_NONE;
}
/* CONTROL mode's keys: the white keys the loops; OP1-OP5 the QUANTIZE chosen, MONO or POLY the next loops', GLO the
 * white keys' SOLO, PIT dim; PLAY mode: the keys light with their notes */
static int key_leds(uint8_t *keys)
{
    if (!lp.control) return 0;
    mute_leds(keys);
    for (int bi = 0; bi < 11; bi++) {
        int v = 0;
        if (bi <= 4) v = QKEY[bi] == loops_quant() ? LV_FULL : LV_DIM;
        else if (bi == 6) v = LV_DIM;
        else if (bi == 7) v = lp.solo ? LV_FULL : LV_DIM;
        else if (bi == 8 || bi == 9) v = (bi == 8) == !!loops_next_mono() ? LV_FULL : LV_DIM;
        keys[UI_BLACK[bi]] = (uint8_t)v;
    }
    return 1;
}
static const char *seq(void) { return lp.control ? (lp.solo ? T("SOLO") : T("MUTE")) : 0; }
static int sel_lines(const char **lines, int max)
{
    if (max < 1) return 0;
    lines[0] = T("K1-K4: BIG STEPS");
    return 1;
}
static void tick(void)
{
    player_state_t st;
    take_sync();
    holds_poll();
    lp_rec_poll();
    state(&st);
    if (lp.arm || lp.take || lp.song || lp.wsent || (st.what == PL_LOOPS && (st.playing || st.lp_alone))) ui_dirty();
}
static void enter(const ui_screen_t *from) { (void)from; if (!lp.take && lp.song != SG_ON) keys_clear(); lp.held = lp.done = 0; }
/* leaving for any screen but FX: a take ends as REC ends it, an armed one or a song disarmed, a song's recording ended
 * at once (what was heard written), the pattern recorder closed */
static void leave(const ui_screen_t *to)
{
    if (to && !strcmp(T(to->name), "FX")) return;
    take_sync();
    if (lp.arm) disarm();
    if (lp.take) stop_take();
    if (lp.song == SG_ON) song_end_here();
    else song_cancel();
    lp_rec_close();
    lp.held = lp.done = 0;
}

/* ---------------------------------------------------------------------- drawing --- */
/* Each loop's notes as spans on its lanes (specs/ui/screens/loops.md#the-strips), in its base's ticks: from a note (after
 * its note delay) to its lane's next note (a pixel's gap before it), note off, key off (at its tick) or the base's end.
 * Kept for every loop while the store and TICKS A ROW stay as they were, as many as SPANS hold; a loop past them is
 * read again as it is drawn (into ui_scratch). A span: from (12 bits), to (12), its lane << 24, the gap << 26 */
#define SPANS 64
typedef uint32_t span_t;
static span_t spans[SPANS];
static struct { uint32_t gen; uint8_t sp, ok, at[LP_MAX], n[LP_MAX]; } sc;   /* n: 0xFF not kept */
static int decode(int i, int sp, span_t *out, int max)
{
    const uint8_t *end, *s = loops_stream(i, &end);
    int cur[4] = {-1, -1, -1, -1}, n = 0, base = loops_base(i);
    rec_cur_t cu;
    rec_cell_t c;
#define SPAN_END(l, at, gap) do { if (cur[l] >= 0) { if (n >= max) return -1; \
        out[n++] = (span_t)cur[l] | (span_t)(at) << 12 | (span_t)(l) << 24 | (span_t)(gap) << 26; cur[l] = -1; } } while (0)
    rec_cur_set_end(&cu, s, end);
    while (s && cu.row < 4 * base) {
        int sr = cu.row, r = sr >> 2, l = sr & 3, note, d = 0, k = -1;
        rec_cur_at(&cu, sr, &c);
        note = is_note(&c);
        for (int j = 0; j < c.n; j++) {
            if (note && c.cmd[j][0] == 0x0E && c.cmd[j][1] >> 4 == 0xD) d = c.cmd[j][1] & 15;
            if (c.cmd[j][0] == 0x14) k = c.cmd[j][1];
        }
        if (k >= 0 && (!note || k < d)) SPAN_END(l, r * sp + (k < sp ? k : sp), 0);
        if ((c.has & RC_NOTE) && c.note == REC_NOTE_OFF) SPAN_END(l, r * sp, 0);
        if (note) { SPAN_END(l, r * sp + d, 1); cur[l] = r * sp + d; }
    }
    for (int l = 0; l < 4; l++) SPAN_END(l, base * sp, 0);
#undef SPAN_END
    return n;
}
static void spans_keep(int sp)
{
    int at = 0;
    if (sc.ok && sc.gen == store_gen() && sc.sp == sp) return;
    for (int i = 0; i < loops_n(); i++) {
        int n = decode(i, sp, spans + at, SPANS - at);
        sc.at[i] = (uint8_t)at;
        sc.n[i] = (uint8_t)(n < 0 ? 0xFF : n);
        if (n > 0) at += n;
    }
    sc.gen = store_gen();
    sc.sp = (uint8_t)sp;
    sc.ok = 1;
}
/* the strips' window (start, rows shown): the length whole up to 4 bars; longer, 4 bars, the play line going to the
 * middle, the strips scrolling under it, on to the end at the end (the first loop recording: 4 bars, its end the bars
 * so far) */
typedef struct { int start, v; } win_t;
static int first_rec(void) { return lp.take && lp.first && lp_playing_here(); }
static int rec_bars(const player_state_t *st)
{
    int b = st->lp_pass == lp.pass ? st->row / loops_bar_rows() + 1 : 1;
    return b > loops_max_bars() ? loops_max_bars() : b;
}
static int play_row(const player_state_t *st)
{
    int n = loops_rows();
    if (st->what != PL_LOOPS) return 0;
    if (st->lp_rest == 1 || st->lp_rest == 2) return n ? st->lp_rest_row % n : 0;   /* (silent for a song: their place) */
    return n && !first_rec() ? st->row % n : st->row;
}
static win_t window(const player_state_t *st)
{
    int br = loops_bar_rows(), w4 = 4 * br, len = first_rec() ? rec_bars(st) * br : loops_rows(), play = play_row(st);
    win_t w = {0, w4};
    if (first_rec()) w.start = ui_clamp(play - 2 * br, 0, len - w4 > 0 ? len - w4 : 0);
    else if (len && len <= w4) w.v = len;
    else if (len) w.start = ui_clamp(play - 2 * br, 0, len - w4);
    return w;
}
static void strip(int i, int y, const win_t *w, int rec, int dim, const player_state_t *st)
{
    int sp = loops_ticks(), n, lanes = loops_lanes(i), h = lanes == 1 ? 11 : lanes == 2 ? 5 : lanes == 3 ? 3 : 2;
    int base = loops_base(i) * sp, reps = loops_repeats(i), br = loops_bar_rows();
    int ws = w->start * sp, wv = w->v * sp, end = rec ? (st->lp_pass == lp.pass ? st->row * sp + st->tick : 0)
                                                      : (loops_rows() ? loops_rows() : loops_base(i) * reps) * sp;
    int col = rec ? C_RED : dim ? C_DGREY : LP_COL[loops_colour(i)], deep = rec ? C_RED : dim ? C_DGREY : C_DEEP + LP_COL[loops_colour(i)];
    const span_t *sv = spans + sc.at[i];
    g_rect(STRIP_X, y, STRIP_W, 11, C_DEEP + C_DGREY);
    if (sc.n[i] != 0xFF) n = sc.n[i];
    else { sv = (const span_t *)(void *)ui_scratch; n = decode(i, sp, (span_t *)(void *)ui_scratch, UI_SCRATCH / 4); }
#define XT(t) (STRIP_X + ((t) - ws) * STRIP_W / wv)
    for (int rep = 0; rep < reps && rep * base < end; rep++) {
        int off = rep * base;
        for (int k = 0; k < n; k++) {
            int l = (int)(sv[k] >> 24 & 3), a = off + (int)(sv[k] & 4095), b = off + (int)(sv[k] >> 12 & 4095), gap = (int)(sv[k] >> 26 & 1);
            if (b > end) b = end;
            if (l >= lanes || b <= a || b <= ws || a >= ws + wv) continue;
            int ca = a > ws ? a : ws, cb = b < ws + wv ? b : ws + wv, nx = XT(ca), wd = XT(cb) - nx - (gap && cb == b ? 1 : 0);
            g_rect(nx, y + l * (h + 1), wd < 1 ? 1 : wd, h, rep ? deep : col);
        }
    }
    for (int r = (w->start + br - 1) / br * br; r < w->start + w->v; r += br)
        if (r > w->start) g_vline(XT(r * sp), y, 11, C_BLACK);
#undef XT
}
/* how long until an armed take starts: `ARMED: STARTS IN 3 BARS` ... `1 BEAT`; from silence `ARMED: A NOTE OR PLAY` */
static const char *arm_text(const player_state_t *st)
{
    if (!(st->what == PL_LOOPS && st->playing) || lp.arm != ARM_PLAY) return T("ARMED: A NOTE OR PLAY");
    int sp = speed(st), br = loops_bar_rows(), rem = (loops_rows() ? loops_rows() : br) * sp - (st->row * sp + st->tick);
    if (rem < 0) rem = 0;
    if (rem > br * sp) return ui_txt_v(T("ARMED: STARTS IN # BARS"), ui_dec((rem + br * sp - 1) / (br * sp)));
    int b = (rem + loops_rpb() * sp - 1) / (loops_rpb() * sp);
    if (b < 1) b = 1;
    return b >= loops_beats() ? T("ARMED: STARTS IN 1 BAR") : b == 1 ? T("ARMED: STARTS IN 1 BEAT")
                              : ui_txt_v(T("ARMED: STARTS IN # BEATS"), ui_dec(b));
}
static int armed_at(void) { return lp.arm ? lp.at : -1; }
static void top_rest(const player_state_t *st, int here, int fr, int br, int y);
/* the top line: tempo, length (the first loop's bars so far, red), QUANTIZE, REC; the beats. A song recording: SONG, the
 * time recorded (M:SS, MM:SS from 10 minutes), the position being written, the tempo */
static void top(const player_state_t *st)
{
    int y = BODY_Y + 2, br = loops_bar_rows(), here = st->what == PL_LOOPS && st->playing, fr = first_rec();
    char b[24];
    ui_fmt(b, sizeof b, "%d BPM", loops_bpm());
    if (lp.song == SG_ON) {
        int secs = (int)((uint64_t)(st->halves - live_t0()) * 256u / 44100u), np = live_done() + 1;
        char t[12];
        g_text(2, y, &FONT_T, T("SONG"), C_RED);
        ui_fmt(t, sizeof t, "%d:%02d", secs / 60, secs % 60);
        g_text(secs >= 600 ? 26 : 32, y, &FONT_T, t, C_LGREY);
        g_text(68, y, &FONT_T, np == 1 ? T("1 PATTERN") : ui_txt_v(T("# PATTERNS"), ui_dec(np)), C_LGREY);
        g_text(146, y, &FONT_T, b, C_LGREY);
    } else {
        g_text(2, y, &FONT_T, b, C_LGREY);
        top_rest(st, here, fr, br, y);
    }
    int nb = loops_beats(), w = 44 / nb, on = here ? st->row % br / loops_rpb() : -1;
    for (int k = 0; k < nb; k++) g_rect(194 + k * w, BODY_Y + 4, w - 2, 8, k == on ? (k ? C_GREEN : C_YELLOW) : C_DEEP + C_GREEN);
    g_hline(0, BODY_Y + 14, 240, C_DGREY);
}
static void top_rest(const player_state_t *st, int here, int fr, int br, int y)   /* the length, QUANTIZE, REC */
{
    char b[24];
    g_text(50, y, &FONT_T, fr ? bars_text(rec_bars(st)) : loops_rows() ? bars_text(loops_rows() / br) : T("-- BARS"), fr ? C_RED : C_LGREY);
    g_text(98, y, &FONT_T, T(QTOP[loops_quant()]), C_CYAN);
    const char *r = T("REC");
    int rc = C_DGREY;
    if (lp.song) { r = T("SONG"); rc = C_RED; }
    else if (lp.take && here) {
        int k = st->row / br + 1;
        if (lp.first) ui_fmt(b, sizeof b, "%s %d", T("REC"), k);
        else ui_fmt(b, sizeof b, "%s %d/%d", T("REC"), k, loops_rows() / br);
        r = b;
        rc = C_RED;
    } else if (lp.take || lp.arm) rc = C_RED;
    g_text(152, y, &FONT_T, r, rc);
}
static void loop_row(int i, const win_t *w, const player_state_t *st)
{
    int y = ROW_Y + 12 * i, rec = recording() && lp.at == i, dim = !rec && (loops_silent_mask() >> i & 1u), wt = waiting(i);
    char b[16];
    if (lp.control) ui_fmt(b, sizeof b, "%2d", i + 1); else strcpy(b, ui_hex2(i));
    g_text(2, y, &FONT_T, b, rec ? C_RED : dim ? C_DGREY : lp.control ? C_WHITE : C_CYAN);
    if (!ui_inst_name(loops_inst(i), b)) strcpy(b, T("EMPTY"));
    b[9] = 0;
    g_text(20, y, &FONT_T, b, rec ? C_RED : dim ? C_DGREY : LP_COL[loops_colour(i)]);
    if (armed_at() == i) g_text(80, y, &FONT_T, arm_text(st), C_RED);
    else {
        int reps = loops_repeats(i);
        g_text(80, y, &FONT_T, loops_mono(i) ? T("M") : T("P"), dim ? C_DGREY : C_LGREY);
        if (reps > 1) {
            if (reps > 9) strcpy(b, T("X+")); else ui_fmt(b, sizeof b, "X%d", reps);
            g_text(91, y, &FONT_T, b, dim ? C_DGREY : C_LGREY);
        }
        if (rec) g_text(104, y, &FONT_T, T("R"), C_RED);
        else if (wt) g_text(104, y, &FONT_T, wt == 1 ? T("M") : T("S"), C_YELLOW);
        else if (loops_soloed(i)) g_text(104, y, &FONT_T, T("S"), C_YELLOW);
        else if (loops_muted(i)) g_text(104, y, &FONT_T, T("M"), C_DGREY);
        strip(i, y, w, rec, dim, st);
    }
    if (i == loops_sel()) g_box(0, y - 1, 240, 13, C_WHITE);
}
static void placeholder(int n, const player_state_t *st)
{
    int y = ROW_Y + 12 * n;
    char b[40], id[8], nm[16];
    if (armed_at() == n) {
        ui_fmt(b, sizeof b, "%s %s", ui_hex2(n), arm_text(st));
        g_text(20, y, &FONT_T, b, C_RED);
    } else {
        g_text(20, y, &FONT_T, T("+ NEW LOOP"), recording() ? C_DGREY : C_WHITE);
        ui_ref_text(id, ui_cur.inst);
        if (!ui_inst_name(ui_cur.inst, nm)) strcpy(nm, T("EMPTY"));
        nm[9] = 0;
        ui_fmt(b, sizeof b, "%s %s", id, nm);
        g_text(STRIP_X, y, &FONT_T, b, ENG_COL[ui_cart_kind((uint8_t)(ui_cur.inst >> 8))]);
    }
    if (loops_sel() == n) g_box(0, y - 1, 240, 13, C_WHITE);
}
static void draw(void)
{
    player_state_t st;
    int n = loops_n();
    state(&st);
    top(&st);
    win_t w = window(&st);
    spans_keep(loops_ticks());
    for (int i = 0; i < n; i++) loop_row(i, &w, &st);
    if (n < LP_MAX) placeholder(n, &st);
    if (!n) {                                           /* no loops yet: how the first one comes */
        g_text(2, 77, &FONT_T, lp.arm ? T("PLAY OR A NOTE: THE FIRST LOOP") : T("REC, THEN PLAY OR A NOTE:"), C_LGREY);
        if (!lp.arm) g_text(2, 89, &FONT_T, T("THE FIRST LOOP SETS THE LENGTH"), C_LGREY);
        g_run_t r[4] = {{T("PRESETS"), C_YELLOW}, {", ", C_LGREY}, {T("ENV"), C_YELLOW}, {T(": ITS SOUND"), C_LGREY}};
        g_runs(2, 113, &FONT_T, r, 4);
        return;
    }
    int r = play_row(&st);
    if (r >= w.start && r < w.start + w.v) g_vline(STRIP_X + (r - w.start) * STRIP_W / w.v, ROW_Y, 12 * n - 1, C_WHITE);
}
static const char *footer(uint8_t *col)
{
    *col = C_LGREY;
    if (lp_rec_on()) return T("REC: WRITE");
    if (lp.song == SG_ON) {                             /* (the edit memory left) */
        static char m[24];
        uint32_t used = store_edit_used(), room = store_edit_room(), all = used + room;
        *col = C_RED;
        ui_fmt(m, sizeof m, T("MEMORY %d%% FREE"), (int)(all ? (uint64_t)room * 100u / all : 0u));
        return m;
    }
    return loops_n() >= LP_MAX ? T("16 OF 16") : loops_next_mono() ? T("MONO") : T("POLY");
}

/* the state's cursor (specs/connections.md#the-state): the loops; the one selected (the count: + NEW LOOP); the notes it
 * starts; a take armed (bit 0), recording (1), CONTROL mode (2), the selected loop muted (3), soloed (4), a song armed
 * (5), recording (6), the pattern recorder open (7); the length in bars; the row; u16 the notes all the loops start */
static void state_cursor(uint8_t *c)
{
    player_state_t st;
    int n = loops_n(), all = 0, s = loops_sel(), ns = on_loop() ? loops_note_count(s) : 0;
    state(&st);
    for (int i = 0; i < n; i++) all += loops_note_count(i);
    c[0] = (uint8_t)n;
    c[1] = (uint8_t)s;
    c[2] = (uint8_t)(ns > 255 ? 255 : ns);
    c[3] = (uint8_t)((lp.arm ? 1 : 0) | (lp.take ? 2 : 0) | (lp.control ? 4 : 0) | (on_loop() && loops_muted(s) ? 8 : 0) |
                     (on_loop() && loops_soloed(s) ? 16 : 0) | (lp.song && lp.song != SG_ON ? 32 : 0) |
                     (lp.song == SG_ON ? 64 : 0) | (lp_rec_on() ? 128 : 0));
    c[4] = (uint8_t)(loops_bar_rows() ? loops_rows() / loops_bar_rows() : 0);
    c[5] = (uint8_t)(st.what == PL_LOOPS ? st.row : 0);
    c[6] = (uint8_t)all;
    c[7] = (uint8_t)(all >> 8);
}
/* what it edits (specs/ui/screens/editor.md): its properties open, the loops play at them (no Fxx); else the selected
 * loop's mix (no `=` commands on its track) */
static void editing(uint8_t *what, uint32_t *key)
{
    const ui_overlay_t *o = ui_top();
    if (o && o->kind == OV_PROPS) {
        *what = PL_ED_TIME;
        *key = PL_ED_LOOPS;
    } else if (!o && on_loop()) {
        *what = PL_ED_MIX;
        *key = 1u << (16 + loops_sel());
    }
}

const ui_screen_t SCR_LOOPS = {
    .name = TX("LOOPS"), .group = G_PLAY,
    .does = BIT(B_GLO) | BIT(B_SEQ) | BIT(B_ENV) | BIT(B_SEL) | BIT(B_ARP) | BIT(B_REC),
    .enter = enter, .leave = leave, .draw = draw, .seq = seq, .footer = footer, .knob = knob, .tap = tap, .key = key,
    .back = back, .lit = lit, .key_leds = key_leds, .sel_lines = sel_lines, .play_down = play_down, .play_up = play_up,
    .hold = hold, .rec = rec,
    .can_play = can_play, .tick = tick, .recording = rec_any, .mute_key = mute_key, .solo_key = solo_key,
    .mute_leds = mute_leds, .own_arp = UI_ARP_PATTERN, .editing = editing, .cursor = state_cursor, .transport = transport,
    .inst = keys_inst,
};

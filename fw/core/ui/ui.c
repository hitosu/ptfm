/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The UI's core (ui_core.h): the controls' grammar (specs/ui/controls.md), navigation (navigation.md),
 * the frame (frame.md), the LEDs (leds.md), the keys playing the current instrument; the main loop's pass
 * (ui_poll). The dialogs are ui_dlg.c's, the settings ui_set.c's. */
#include <string.h>
#include "ui.h"
#include "ui_core.h"
#include "ui_priv.h"
#include "ui_data.h"
#include "../play/player.h"
#include "../store/store.h"
#include "../store/seed.h"
#include "../midi/midi.h"
#include "../play/dj.h"
#include "../play/loops.h"
#include "fxdef.h"
#include "fx_target.h"
#include "../dbg.h"                     /* the debug build's figures (empty in the release) */
#include "ui_punch.h"
#include "ui_lp.h"                      /* lp_live_poll: RECORD LIVE goes on on LOOPS' FX */
#include "ed_inst.h"                    /* ins_builtin: PRESETS' list browns the built-in ones */

/* the FM-1's keys (F3 at 0): the white ones, and the black ones the screens' keys */
const uint8_t UI_WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
const uint8_t UI_BLACK[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
static const txt_t BLACK_LABEL[11] = {TX("OP1"), TX("OP2"), TX("OP3"), TX("OP4"), TX("OP5"), TX("OP6"), TX("PIT"), TX("GLO"),
                                      TX("MONO"), TX("POLY"), TX("BLANK")};
const txt_t UI_BNAME[B_COUNT] = {TX("OCT-"), TX("OCT+"), TX("FX"), TX("SEL"), TX("ENV"), TX("LFO"), TX("EDIT"), TX("GLO"),
                                 TX("HOME"), TX("SAVE"), TX("ARP"), TX("SEQ"), TX("PLAY"), TX("REC")};
int ui_white_index(int n) { for (int i = 0; i < 16; i++) if (UI_WHITE[i] == n) return i; return -1; }
int ui_black_index(int n) { for (int i = 0; i < 11; i++) if (UI_BLACK[i] == n) return i; return -1; }

/* the black keys' screens (navigation.md): OP1 SONGS, OP2 EDITOR, OP3 INSTRUMENTS, OP4 PLAYBACK, OP5 DJ,
 * PIT VISUAL, GLO STATUS, MONO SETTINGS, POLY LOOPS; OP6 and the blank key none yet */
static const txt_t SCREEN_KEY[11] = {TX("SONGS"), TX("EDITOR"), TX("INSTRUMENTS"), TX("PLAYBACK"), TX("DJ"), 0, TX("VISUAL"),
                                     TX("STATUS"), TX("SETTINGS"), TX("LOOPS"), 0};
/* the round HOME's SELECT turns: the start screen first, SETTINGS last */
static const txt_t ROUND[10] = {TX("STATUS"), TX("SONGS"), TX("EDITOR"), TX("INSTRUMENTS"), TX("PLAYBACK"), TX("VISUAL"),
                                TX("FX"), TX("LOOPS"), TX("DJ"), TX("SETTINGS")};
const uint8_t UI_GROUP_COL[4] = {C_LBLUE, C_GREEN, C_BROWN, C_MAGENTA};

ui_cur_t ui_cur = {0, UI_EDITOR_PATTERN, UI_FIRST_INST, 0, 0, -1, 0};
uint8_t ui_scratch[UI_SCRATCH] PLAT_POOL;

/* ------------------------------------------------------------------- screens --- */
#define MAX_SCREENS 16
static const ui_screen_t *screens[MAX_SCREENS];
static int nscreens;
static const ui_screen_t *cur, *from_scr, *fx_from;
static int is(const ui_screen_t *s, const char *name) { return s && !strcmp(T(s->name), name); }   /* (s is the one named) */
static int loops_shown(void);                           /* (LOOPS on screen, or FX from it: cue_poll) */
static uint8_t cue_now;                                 /* (a screen changed: cue_poll at once) */
typedef struct { const ui_screen_t *from; void (*back)(void); uint8_t locked; } deep_t;
static deep_t deep[4];
static int ndeep;

void ui_register(const ui_screen_t *s) { if (nscreens < MAX_SCREENS) screens[nscreens++] = s; }
const ui_screen_t *ui_screen_now(void) { return cur; }
const ui_screen_t *ui_screen_named(const char *name)
{
    char nm[16];                                        /* (name may be a text: the loop's T() would turn its buffer) */
    ui_fmt(nm, sizeof nm, "%s", name);
    for (int i = 0; i < nscreens; i++) if (!strcmp(T(screens[i]->name), nm)) return screens[i];
    return 0;
}

/* ------------------------------------------------------------------- messages --- */
static struct { char text[48]; g_run_t runs[6]; char rt[6][24]; int nruns; uint32_t until; } toast;
static struct { char text[48]; uint32_t until; } knob_show;
static uint32_t flash_until[B_COUNT];
static uint8_t dirty = 1;

uint32_t ui_now(void) { return plat_ms(); }
int ui_blink(int period) { return (ui_now() / (uint32_t)period) % 2 == 0; }
void ui_dirty(void) { dirty = 1; }

void ui_toast(const char *text)
{
    strncpy(toast.text, text, sizeof toast.text - 1);
    toast.text[sizeof toast.text - 1] = 0;
    toast.nruns = 0;
    toast.until = ui_now() + 2000;
    knob_show.until = 0;                /* (the newest of a toast and a knob turned shows) */
    dirty = 1;
}
void ui_toast_runs(const g_run_t *r, int n)
{
    if (n > 6) n = 6;
    toast.text[0] = 0;
    for (int i = 0; i < n; i++) {
        strncpy(toast.rt[i], r[i].s, sizeof toast.rt[0] - 1);
        toast.rt[i][sizeof toast.rt[0] - 1] = 0;
        toast.runs[i].s = toast.rt[i];
        toast.runs[i].c = r[i].c;
        strncat(toast.text, r[i].s, sizeof toast.text - 1 - strlen(toast.text));
    }
    toast.nruns = n;
    toast.until = ui_now() + 2000;
    knob_show.until = 0;
    dirty = 1;
}
const char *ui_toast_now(void) { return ui_now() < toast.until ? toast.text : 0; }
const char *ui_knob_now(void) { return ui_now() < knob_show.until ? knob_show.text : 0; }
void ui_show_knob(const char *name, const char *value)   /* (in the header's place, as a toast: the newest shows) */
{
    ui_fmt(knob_show.text, sizeof knob_show.text, "%s %s", name, value);
    knob_show.until = ui_now() + 1200;
    toast.until = 0;
    dirty = 1;
}
void ui_refuse(int b) { if (b >= 0 && b < B_COUNT) flash_until[b] = ui_now() + 600; }

/* ------------------------------------------------------------------- overlays --- */
#define MAX_OV 6                        /* (a menu over a list over a question, HOME's over them: 4 at most) */
static ui_overlay_t ovs[MAX_OV];
static int nov;
ui_overlay_t *ui_open(const ui_overlay_t *o)
{
    if (nov == MAX_OV) return 0;
    ovs[nov] = *o;
    dirty = 1;
    return &ovs[nov++];
}
void ui_close(ui_overlay_t *o)
{
    if (!nov) return;
    int i = o ? (int)(o - ovs) : nov - 1;
    if (i < 0 || i >= nov) return;
    memmove(&ovs[i], &ovs[i + 1], (size_t)(nov - i - 1) * sizeof ovs[0]);
    nov--;
    dirty = 1;
}
ui_overlay_t *ui_top(void) { return nov ? &ovs[nov - 1] : 0; }
int ui_overlay_count(void) { return nov; }
ui_overlay_t *ui_overlay_at(int i) { return i >= 0 && i < nov ? &ovs[i] : 0; }
static ui_overlay_t *find_kind(int kind) { for (int i = 0; i < nov; i++) if (ovs[i].kind == kind) return &ovs[i]; return 0; }

/* -------------------------------------------- the grammar's state (controls.md) --- */
/* a button's: held (since t, its hold delay due), or released into its grace period; a combination's lock */
typedef struct {
    uint8_t down, used, closed, layer, custom, timer;
    uint8_t cut;                        /* REC: held as CUT (let go: heard again) */
    uint8_t grace;                      /* released, its tap waiting until grace_until */
    uint32_t t, due, grace_until;
    int lock;                           /* the member that acted (CK_*), 0 none */
    uint8_t solo;                       /* HOME's: the white keys solo (SEQ) */
} btn_t;
static btn_t btn[B_COUNT];
enum { KU_NONE, KU_SCREEN, KU_MUTE, KU_PUNCH, KU_OVERLAY, KU_SCREENKEY, KU_ARP, KU_PLAY };
typedef struct { uint8_t kind, t, solo, held, s; uint32_t due; } keyuse_t;     /* s: the screen that took it */
static keyuse_t key_use[27];
static uint32_t enc_t[K_COUNT];
static uint8_t sel_armed;               /* SEL tapped on the screen, nothing touched since: PRESETS opens its list */

static int hold_ms(void) { return ui_set[S_HOLD_DELAY]; }
static int grace_ms(void) { return ui_set[S_COMBO_GRACE]; }
int ui_held(int b) { return btn[b].down; }
int ui_active(int b)
{
    return (btn[b].down && !btn[b].closed) || (btn[b].grace && ui_now() < btn[b].grace_until);
}
void ui_consume(int b)
{
    if (btn[b].down) btn[b].used = 1;
    btn[b].grace = 0;
}
int ui_use_combo(int b, int kind)
{
    btn_t *h = &btn[b];
    if (!h->down && !h->grace) return 1;
    if (h->lock && h->lock != kind) { h->closed = 1; ui_consume(b); return 0; }
    h->lock = kind;
    ui_consume(b);
    return 1;
}

/* the held combinations a control is no member of close; it does its own job. Not PLAY, REC, OCT, HOME */
static void close_combos(int except)
{
    for (int m = 0; m < B_COUNT; m++) {
        if (!btn[m].down || m == except || m == B_HOME || m == B_PLAY || m == B_REC || m == B_OCTM || m == B_OCTP) continue;
        if (btn[m].closed || btn[m].custom) continue;
        if (cur->hold_action && cur->hold_action(m)) continue;
        btn[m].closed = 1;
        btn[m].used = 1;
    }
}
static int combines(int b)
{
    if (b == B_HOME || b == B_LFO || b == B_SEL || b == B_FX || b == B_SAVE) return 1;
    if (b == B_ARP) return !cur->own_arp || cur->own_arp == UI_ARP_PREVIEW;   /* ARP held + SELECT, ALGORITHM, K1, LFO */
    return (cur->combines & BIT(b)) != 0;
}

/* ------------------------------------------------- HOME's combination (controls.md) --- */
/* open while HOME is held (or in its grace), or tapped open as an overlay of OV_HOME */
static combo_t home_ov;                 /* the tapped-open one's state */
static combo_t home_c;                  /* what home() returns */
static combo_t *home(void)
{
    if (find_kind(OV_HOME)) return &home_ov;
    btn_t *h = &btn[B_HOME];
    if ((h->down && !h->closed) || (h->grace && ui_now() < h->grace_until)) {
        home_c.lock = h->lock; home_c.solo = h->solo; home_c.closed = h->closed; home_c.at = h->t; home_c.is_ov = 0;
        return &home_c;
    }
    return 0;
}
combo_t *ui_home_combo(void) { return home(); }
static void home_sync(combo_t *c)       /* the held one's state back into HOME's button */
{
    if (c == &home_c) { btn[B_HOME].lock = c->lock; btn[B_HOME].solo = c->solo; btn[B_HOME].closed = c->closed; }
}
static void close_home(combo_t *c)
{
    if (c->is_ov) { ui_overlay_t *o = find_kind(OV_HOME); if (o) ui_close(o); }
    else { c->closed = 1; home_sync(c); ui_consume(B_HOME); }
}
static int member(combo_t *c, int kind)
{
    if (c->lock && c->lock != kind) { close_home(c); return 0; }
    c->lock = kind;
    home_sync(c);
    ui_consume(B_HOME);
    return 1;
}
/* SEQ switches the white keys between mute and solo as long as they are members: before any member acted and
 * after they did, again and again */
int ui_home_seq_switches(const combo_t *c) { return !c->lock || c->lock == CK_WHITE; }

/* HOME's lines: what can act; once a member has acted, only that one */
static int home_line(int kind, const combo_t *c) { return !c->lock || c->lock == kind; }
int ui_home_lines(const combo_t *c, g_run_t (*lines)[UI_LINE_RUNS], int *nruns, char (*buf)[48], int max)
{
    int n = 0, b = 0;
#define LINE1(k, text) if (n < max && home_line(k, c)) { ui_key_line(lines[n], &nruns[n], text); n++; }
    LINE1(CK_SELECT, T("SELECT: THE SCREENS, ONE BY ONE"));
    if (home_line(CK_BLACK, c)) {
        /* the black keys' screens, two a line by the keys' order, as printed (`OP1: SONGS  OP2: EDITOR`); the
         * current screen white; a screen not built is left out */
        int ks[11], nk = 0;
        for (int i = 0; i < 11; i++) if (SCREEN_KEY[i] && ui_screen_named(T(SCREEN_KEY[i]))) ks[nk++] = i;
        for (int i = 0; i < nk && n < max; i += 2) {
            int r = 0;
            for (int j = i; j < i + 2 && j < nk; j++) {
                int k = ks[j];
                ui_fmt(buf[b], 48, "%s", T(BLACK_LABEL[k]));                /* (the texts kept in buf: drawn later) */
                lines[n][r].s = buf[b++]; lines[n][r++].c = C_YELLOW;
                ui_fmt(buf[b], 48, ": %s", T(SCREEN_KEY[k]));
                if (j == i && j + 1 < nk) {             /* spaces to the second one's column, after the first's name */
                    int len = (int)strlen(buf[b]), pad = 19 - (int)strlen(buf[b - 1]) - len;
                    for (int q = 0; q < (pad < 1 ? 1 : pad) && len < 47; q++) buf[b][len++] = ' ';
                    buf[b][len] = 0;
                }
                lines[n][r].s = buf[b++];
                lines[n][r++].c = is(cur, T(SCREEN_KEY[k])) ? C_WHITE : C_LGREY;
            }
            nruns[n++] = r;
        }
    }
    LINE1(CK_WHITE, is(cur, "LOOPS") ? (c->solo ? T("WHITE KEY: SOLO A LOOP") : T("WHITE KEY: MUTE A LOOP, HELD: SOLO"))
                                     : c->solo ? T("WHITE KEY: SOLO A TRACK") : T("WHITE KEY: MUTE A TRACK, HELD: SOLO"));
    if (is(cur, "DJ")) LINE1(CK_WHITE, T("KEYS 1-8: LEFT DECK, 9-16: RIGHT"));
    if (cur && cur->home_oct) {                         /* DJ: the tracks the keys reach, where there are more */
        if (cur->home_oct(B_OCTP, 0)) {
            LINE1(CK_WHITE, T("OCT+: TRACKS 08-0F"));
        } else if (cur->home_oct(B_OCTM, 0)) {
            LINE1(CK_WHITE, T("OCT-: TRACKS 00-07"));
        }
    }
    /* (SEQ goes with the white keys: it switches them as long as they are members) */
    LINE1(CK_WHITE, c->solo ? T("SEQ: WHITE KEYS MUTE INSTEAD") : T("SEQ: WHITE KEYS SOLO INSTEAD"));
    LINE1(CK_PLAY, is(cur, "DJ") ? T("PLAY: BACK TO THE CUES") : T("PLAY: BACK TO THE START"));
    LINE1(CK_REC, T("REC: CUT, SILENT WHILE HELD"));
    LINE1(CK_LFO, T("LFO: HELP FOR THIS SCREEN"));
#undef LINE1
    return n;
}

/* ---------------------------------------------------------------- navigation --- */
void ui_go(const ui_screen_t *s)
{
    if (!s || s == cur) return;
    if (ndeep && deep[ndeep - 1].locked) { ui_refuse(B_EDIT); return; }
    const ui_screen_t *prev = cur;
    if (prev && prev->leave) prev->leave(s);
    if (prev && is(prev, "FX") && fx_from && is(fx_from, "LOOPS") && !is(s, "LOOPS") && fx_from->leave)
        fx_from->leave(s);                              /* (going on elsewhere from LOOPS' FX leaves LOOPS then) */
    /* overlays go, but those that stay (HOME's combination while SELECT turns the screens) */
    for (int i = nov - 1; i >= 0; i--) if (!ovs[i].keep) ui_close(&ovs[i]);
    ndeep = 0;
    sel_armed = 0;
    ui_arp_off();                                       /* ARP stays with its screen */
    if (is(s, "FX") && prev) fx_from = prev;
    from_scr = prev;
    int was_loops = loops_shown();
    cur = s;
    if (was_loops && !loops_shown()) player_cue_loops(0);   /* (LOOPS left: the loops' hold for the keys over, at once) */
    cue_now = 1;
    if (s->enter) s->enter(prev);
    g_invalidate();
    dirty = 1;
}
void ui_go_name(const char *name) { ui_go(ui_screen_named(name)); }
static void screen_step(int d)
{
    int at = 0;
    for (int i = 0; i < 10; i++) if (is(cur, T(ROUND[i]))) at = i;
    for (int k = 1; k <= 10; k++) {                     /* the next one built, its way round */
        const ui_screen_t *s = ui_screen_named(T(ROUND[ui_wrap(at + d * k, 10)]));
        if (s) { ui_go(s); return; }
    }
}

/* -------------------------------------------------------- the keys sound --- */
int ui_midi(int n) { return 53 + n + 12 * ui_cur.octave; }
static uint32_t key_at[27];
static uint8_t key_max[27];
static uint8_t key_out[27];                             /* the note MIDI OUT sent for the key, + 1 */
static uint16_t keys_inst(void)                         /* the instrument the keys play: the screen's own, else the current */
{
    uint16_t r = cur && cur->inst ? cur->inst() : 0xFFFFu;
    return r == 0xFFFFu ? ui_cur.inst : r;
}
static void key_on(int n)
{
    int m = ui_midi(n);
    if (m < 12 || m > 127) return;
    key_max[n] = 0;
    key_at[n] = ui_now() | 1;
    uint16_t ref = keys_inst();
    pl_inst_t inst = {(uint8_t)(ref >> 8), (uint8_t)ref};
    int vel = midi_key_vel(n);                          /* a MIDI note: its velocity; the FM-1's keys: KEYS VOLUME */
    player_key_on((uint8_t)m, inst, (uint8_t)(vel ? vel : ui_set[S_KEYS_VOLUME]));
    if (!vel) {                                         /* MIDI OUT: the FM-1's own keys (MIDI's never go back out) */
        midi_out_key((uint8_t)m, (uint8_t)ui_set[S_KEYS_VOLUME], 1);
        key_out[n] = (uint8_t)(m + 1);
    }
}
static void key_off(int n)
{
    int m = ui_midi(n);
    if (m >= 12 && m <= 127) player_key_off((uint8_t)m);
    if (key_out[n]) { midi_out_key((uint8_t)(key_out[n] - 1), 0, 0); key_out[n] = 0; }
}
uint16_t ui_last_inst = 0xFFFFu;
void ui_set_inst(uint16_t ref)
{
    char nm[12];
    ui_cur.inst = ref;
    if (ui_cart_kind((uint8_t)(ref >> 8)) != ENG_FUSION && ui_inst_name(ref, nm)) ui_last_inst = ref;
    dirty = 1;
    if (ui_arp_on()) {                                  /* ARP follows: its next note the new one's, however picked */
        pl_inst_t in = {(uint8_t)(ref >> 8), (uint8_t)ref};
        player_arp_inst(in);
    }
}

/* PRESETS: through the cartridge's used slots, wrapping */
static void step_inst(int d)
{
    uint8_t c = (uint8_t)(ui_cur.inst >> 8);
    int n = ui_cart_slots(c), at = ui_cur.inst & 255, dir = d < 0 ? -1 : 1;
    if (!n) return;
    char nm[12];
    for (int k = 0; k < (d < 0 ? -d : d); k++) {
        int next = -1;
        for (int j = 1; j <= n && next < 0; j++) {
            int i = ui_wrap(at + dir * j, n);
            if (ui_inst_name((uint16_t)(c << 8 | i), nm)) next = i;
        }
        if (next < 0) return;
        at = next;
    }
    ui_set_inst((uint16_t)(c << 8 | at));
}
/* SEL + PRESETS: the cartridges, wrapping, each at its first used slot */
static void step_cart(int d)
{
    uint8_t nums[256];
    int n = ui_carts(nums, 256), i = 0;
    if (!n) return;
    for (int k = 0; k < n; k++) if (nums[k] == ui_cur.inst >> 8) i = k;
    uint8_t c = nums[ui_wrap(i + d, n)];
    int slots = ui_cart_slots(c), first = 0;
    char nm[12];
    for (int s = 0; s < slots; s++) if (ui_inst_name((uint16_t)(c << 8 | s), nm)) { first = s; break; }
    ui_set_inst((uint16_t)(c << 8 | first));
}
/* PRESETS turned, as every knob shows in the header (frame.md): `PRESETS 01:03 E.PIANO`; with SEL the cartridge,
 * `CARTRIDGE 01 KEYS/ORGANS` */
static void show_inst(int cart)
{
    char id[8], nm[16], v[32];
    uint16_t ref = ui_cur.inst;
    if (cart) {
        ui_cart_name((uint8_t)(ref >> 8), nm);
        ui_fmt(v, sizeof v, "%02X %s", ref >> 8, nm);
        ui_show_knob(T("CARTRIDGE"), v);
        return;
    }
    ui_ref_text(id, ref);
    ui_fmt(v, sizeof v, "%s %s", id, ui_inst_name(ref, nm) ? nm : T("EMPTY"));
    ui_show_knob(T("PRESETS"), v);
}

/* PRESETS' cartridge list (dialogs.md#presets-cartridge-list): SEL + PRESETS, SEL held or tapped first, opens it
 * over the screen, the current cartridge highlighted; PRESETS goes on picking; any other control closes it and does
 * its own job (HOME and SEL only close it); opened with SEL held, it closes when SEL is let go (PRESETS the instruments
 * again). sel_armed (above): SEL's tap reached the screen and nothing else was touched since; every overlay then
 * open is one that tap opened */
static struct { uint8_t nums[256]; int n; uint8_t held; } pc;
static void pc_draw(ui_overlay_t *o)
{
    (void)o;
    int y = BODY_Y + 2, rows = 13, sel = 0;
    for (int i = 0; i < pc.n; i++) if (pc.nums[i] == ui_cur.inst >> 8) sel = i;
    ui_panel(2, y, 236, 206, T("CARTRIDGES"), C_CYAN);
    g_text(8, y + 16, &FONT_T, T("ID NAME         TYPE      SLOTS"), C_DGREY);
    int top = ui_clamp(sel - 6, 0, pc.n - rows > 0 ? pc.n - rows : 0);
    for (int i = top; i < pc.n && i < top + rows; i++) {
        uint8_t c = pc.nums[i];
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
static void pc_knob(ui_overlay_t *o, int e, int s, int c) { (void)o; (void)e; (void)s; (void)c; }   /* (never: see pc_end) */
static int pc_tap(ui_overlay_t *o, int b) { (void)o; (void)b; return 0; }
static ui_overlay_t *pc_find(void)
{
    for (int i = 0; i < nov; i++) if (ovs[i].data == (void *)&pc) return &ovs[i];
    return 0;
}
static void pc_open(void)
{
    pc.n = ui_carts(pc.nums, 256);
    ui_overlay_t o = {.kind = OV_LIST, .opener = B_SEL, .knob = pc_knob, .tap = pc_tap, .draw = pc_draw, .data = (void *)&pc};
    ui_open(&o);
}
/* a control other than PRESETS: the list closes (1 if it was open) and SEL's tap no longer counts */
static int pc_end(void)
{
    sel_armed = 0;
    pc.held = 0;
    ui_overlay_t *o = pc_find();
    if (!o) return 0;
    ui_close(o);
    return 1;
}
/* whether the list can open now: nothing over the screen, or only the menus SEL's tap opened (closed for it) */
static int pc_room(int armed)
{
    if (cur->no_keyboard) return 0;
    for (int i = 0; i < nov; i++) if (!armed || ovs[i].kind != OV_MENU) return 0;
    return 1;
}

/* PRESETS' instrument list (dialogs.md#presets-instrument-list): a PRESETS click that changed the keys' instrument, with
 * SETTINGS' PRESETS LIST on and nothing else over the screen (not on INSTRUMENTS, whose own list it is, nor DJ):
 * the cartridge's used slots, the ones PRESETS steps through, the current one highlighted; it closes 1 s after the
 * last click, or at any other control, which does its own job */
extern const ui_screen_t SCR_INSTRUMENTS;
static uint32_t il_at;                  /* the last click's time (| 1), 0 none */
static uint8_t il_tag;                  /* (the overlay's data: its mark) */
static void il_draw(ui_overlay_t *o)
{
    (void)o;
    uint8_t c = (uint8_t)(ui_cur.inst >> 8);
    int y = BODY_Y + 2, rows = 13, slots = ui_cart_slots(c), at = ui_cur.inst & 255, n = 0, sel = 0, k = ui_cart_kind(c);
    char nm[16];
    for (int s = 0; s < slots; s++) if (ui_inst_name((uint16_t)(c << 8 | s), nm)) { if (s == at) sel = n; n++; }
    ui_panel(2, y, 236, 206, T("INSTRUMENTS"), C_CYAN);
    g_text(8, y + 16, &FONT_T, ui_hex2(c), ins_builtin(c) ? C_BROWN : C_LGREY);   /* the cartridge (built in: brown) */
    if (!ui_cart_name(c, nm)) nm[0] = 0;
    nm[12] = 0;
    g_text(26, y + 16, &FONT_T, nm, C_LGREY);
    g_text(232 - g_tw(&FONT_T, T(ENG_TYPE[k])), y + 16, &FONT_T, T(ENG_TYPE[k]), ENG_COL[k]);
    int top = ui_clamp(sel - 6, 0, n - rows > 0 ? n - rows : 0);
    for (int s = 0, i = 0; s < slots && i < top + rows; s++) {
        if (!ui_inst_name((uint16_t)(c << 8 | s), nm)) continue;
        if (i >= top) {
            int yy = y + 30 + (i - top) * 13, on = s == at;
            if (on) g_box(4, yy - 1, 232, 13, C_WHITE);
            g_text(8, yy, &FONT_T, ui_hex2((uint8_t)s), on ? C_WHITE : C_DGREY);   /* (as INSTRUMENTS colours them) */
            g_text(26, yy, &FONT_T, nm, on ? C_WHITE : ENG_COL[k]);
        }
        i++;
    }
}
static ui_overlay_t *il_find(void)
{
    for (int i = 0; i < nov; i++) if (ovs[i].data == (void *)&il_tag) return &ovs[i];
    return 0;
}
static void il_end(void)                /* any other control: it closes */
{
    ui_overlay_t *o = il_find();
    il_at = 0;
    if (o) ui_close(o);
}
static void il_show(void)               /* a click that changed the instrument: open, or kept open 1 s more */
{
    if (ui_set[S_PRESETS_LIST] || cur == &SCR_INSTRUMENTS || cur->no_keyboard || (nov && !il_find())) return;
    il_at = ui_now() | 1u;
    if (!il_find()) {
        ui_overlay_t o = {.kind = OV_LIST, .opener = -1, .knob = pc_knob, .tap = pc_tap, .draw = il_draw, .data = (void *)&il_tag};
        ui_open(&o);
    }
}
static void il_poll(void)
{
    if (il_at && (int32_t)(ui_now() - il_at) >= 1000) il_end();
}

int ui_muted(int t) { return ui_cur.solo >= 0 ? t != ui_cur.solo : (int)(ui_cur.mutes >> t & 1); }
void ui_mutes_changed(void)
{
    uint32_t m = 0;
    for (int t = 0; t < 16; t++) if (ui_muted(t)) m |= 1u << t;
    player_mutes(m);
    dirty = 1;
}
void ui_all_sound_off(void) { player_all_sound_off(); ui_punch_off(); ui_toast(T("ALL SOUND OFF")); }
/* CUT (controls.md#cut): HOME + REC, REC held as it: all sound off; while held no note starts and nothing is worked
 * on (what plays keeps its time); let go, heard again from its next notes, the effects starting dry. Held 2 s, every
 * effect of the setup FX would edit from this screen is switched off: an edit (SAVE keeps it, REVERT brings them back) */
static uint8_t cut_on, cut_fx;
static uint32_t cut_at;
static void cut(int on)
{
    cut_on = (uint8_t)(on != 0);
    cut_fx = 0;
    cut_at = ui_now();
    player_hush(on);
    if (on) { ui_punch_off(); ui_toast(T("SOUND CUT: LET GO OF REC")); }
}
static void all_off_fn(uint8_t *s64, void *arg)
{
    (void)arg;
    for (int e = 0; e < FE_N; e++) fxd_set_on(s64, e, 0);
}
static void cut_poll(void)
{
    fxt_t tg;
    uint8_t s[FX_SETUP_LEN];
    int any = 0, on_fx = is(cur, "FX");
    if (!cut_on || cut_fx || ui_now() - cut_at < 2000u) return;
    cut_fx = 1;
    if (!on_fx) fxt_from(T(cur->name));                 /* (LOOPS', DJ's from theirs; FX keeps its own) */
    int got = fxt_get(&tg);
    if (!on_fx) fxt_from(0);
    if (!got) return;
    fxt_setup(&tg, s);
    for (int e = 0; e < FE_N; e++) any |= fxd_on(s, e);
    if (any) {                                          /* (none on: nothing to change, no edit) */
        if (fxt_edit(&tg, all_off_fn, 0, B_REC) < 0) return;   /* (refused, or the edit's own toast said why) */
        if (tg.plays) player_fx_reload();
    }
    ui_toast(T("SOUND CUT, EFFECTS OFF"));
}

/* ---------------------------------------------------- ARP (specs/ui/controls.md) ---
 * The arpeggiator, one for every screen but those with an ARP of their own (own_arp): a tap on / off;
 * on, the keys feed it (the player's); held, SELECT the rhythm, ALGORITHM the pattern, K1 the latch, LFO
 * lets the latched notes go. A change of screen switches it off. */
static uint8_t arp_is_on;
int ui_arp_on(void) { return arp_is_on; }
void ui_arp_refresh(void)
{
    if (arp_is_on) player_arp(1, (uint8_t)ui_set[S_ARP_RHYTHM], (uint8_t)ui_set[S_ARP_PATTERN], (uint8_t)ui_set[S_ARP_LATCH]);
}
void ui_arp_off(void) { if (arp_is_on) { arp_is_on = 0; player_arp(0, 0, 0, 0); } }
static void arp_tap(void)
{
    if (arp_is_on) { ui_arp_off(); ui_toast(T("ARP OFF")); return; }
    arp_is_on = 1;
    ui_arp_refresh();
    ui_toast(ui_txt_v(T("ARP #"), T(UI_SETTINGS[S_ARP_PATTERN].list[ui_set[S_ARP_PATTERN]])));
}
/* ARP held + a knob: its settings (SETTINGS' ARP RHYTHM, PATTERN, LATCH), a running arpeggio taking them */
static void arp_knob(int e, int step)
{
    char v[24];
    int d = step > 0 ? 1 : -1;
    if (e == K_SELECT) {
        ui_set[S_ARP_RHYTHM] = (int16_t)ui_clamp(ui_set[S_ARP_RHYTHM] + d, 0, UI_SETTINGS[S_ARP_RHYTHM].n - 1);
        ui_setting_text(&UI_SETTINGS[S_ARP_RHYTHM], ui_set[S_ARP_RHYTHM], v);
        ui_show_knob(T("RHYTHM"), v);
    } else if (e == K_ALG) {
        ui_set[S_ARP_PATTERN] = (int16_t)ui_wrap(ui_set[S_ARP_PATTERN] + d, UI_SETTINGS[S_ARP_PATTERN].n);
        ui_toast(ui_txt_v(T("ARP #"), T(UI_SETTINGS[S_ARP_PATTERN].list[ui_set[S_ARP_PATTERN]])));
    } else {
        ui_set[S_ARP_LATCH] = (int16_t)ui_clamp(ui_set[S_ARP_LATCH] + d, 0, UI_SETTINGS[S_ARP_LATCH].n - 1);
        ui_toast(ui_txt_v(T("LATCH #"), T(UI_SETTINGS[S_ARP_LATCH].list[ui_set[S_ARP_LATCH]])));
    }
    ui_settings_changed();
    ui_arp_refresh();
}

/* -------------------------------------------------------------- the transport --- */
static uint8_t last_song_played = 0xFF;
static int can_play(void)
{
    if (cur->can_play) return cur->can_play();
    ui_song_t s;
    player_state_t st;
    ui_player_state(&st);
    if (st.what != PL_NONE) return 1;
    return ui_song(ui_cur.song, &s) && s.npos > 0;
}
/* PLAY pressed on a screen without a PLAY of its own: what plays pauses; else what played last goes on
 * (its song from its place), else the current song from its start; switching songs rewinds */
void ui_play_press(void)
{
    player_state_t st;
    ui_player_state(&st);
    if (st.playing) { player_pause(); return; }
    if (st.what == PL_SONG && last_song_played != 0xFF) { ui_play_song(last_song_played, -1); return; }
    if (st.what != PL_NONE && st.what != PL_SONG) { player_continue(); return; }
    ui_play_song(ui_cur.song, -1);
}
void ui_play_song(uint8_t song, int from)
{
    ui_song_t s;
    if (!ui_song(song, &s) || !s.npos) { ui_refuse(B_PLAY); ui_toast(T("NO SONG TO PLAY")); return; }
    player_state_t st;
    ui_player_state(&st);
    if (from >= 0) player_play_song(song, (uint8_t)from, 0);
    else if (last_song_played != song || st.what != PL_SONG) player_play_song(song, 0, 0);
    else player_continue();                             /* the same song: from where it paused */
    last_song_played = song;
}

/* ------------------------------------------------------------------- saving --- */
static uint8_t save_waiting;
uint32_t ui_unsaved;
/* SAVE: the store writes the changes a little each pass (store_poll); a bar shows how far */
static uint8_t saving;
static void (*save_after)(void);        /* the next step once this save is written (the MIDI CONTROLLER's switch) */
static uint16_t save_refs;              /* this save is a reference update's: the references it updates */
static int save_pct(void) { uint8_t p = 0; store_save_status(&p); return p; }
static void do_save(void)
{
    char t[32];
    ui_settings_keep_state();                           /* (the current instrument, and the screen, with the rest) */
    ui_dj_stand_put();                                  /* (DJ's decks where they paused, if they moved) */
    loops_keep_sel();                                   /* (LOOPS' selection, if it moved: no edit by itself) */
    ui_unsaved_refresh();
    uint32_t n = ui_unsaved;
    save_waiting = 0;
    if (save_refs)
        ui_fmt(t, sizeof t, "%s", ui_txt_v(save_refs == 1 ? T("UPDATING # REFERENCE") : T("UPDATING # REFERENCES"),
                                           ui_dec(save_refs)));
    else ui_fmt(t, sizeof t, "%s", ui_txt_v(n == 1 ? T("SAVING # CHANGE") : T("SAVING # CHANGES"), ui_dec((int)n)));
    if (ui_save_all()) { save_refs = 0; return; }      /* (its question or toast says why; CLEAN UP's SAVE a plain one) */
    saving = 1;
    ui_progress_pct(t, save_pct);
}
/* YES to UPDATE REFERENCES? (specs/ui/saving.md#references), the change and its references made in the edit memory:
 * playback pauses, and everything pending is written as one save, `UPDATING 3 REFERENCES`, then `REFERENCES UPDATED,
 * SAVED` once it is written */
void ui_save_refs(int uses)
{
    player_pause();
    save_refs = (uint16_t)(uses > 0 ? uses : 1);
    do_save();
}
static void save_poll(void)
{
    if (!saving) return;
    uint8_t p;
    int r = store_save_status(&p);
    if (r == STORE_BUSY) return;
    void (*after)(void) = save_after;
    int refs = save_refs;
    saving = 0;
    save_after = 0;
    save_refs = 0;
    ui_progress_close();
    ui_unsaved_refresh();
    player_store_changed();                             /* (the records are where the save put them now) */
    if (r == STORE_OK) ui_toast(refs ? T("REFERENCES UPDATED, SAVED") : T("SAVED"));
    else if (r == STORE_ERR_FULL) ui_ask_storage_full();    /* (its deletions written first, the rest not) */
    else ui_toast(T("SAVE FAILED"));
    if (r == STORE_OK && after) after();                /* (not saved: what waited for it does not happen) */
}
void ui_save_answer(int a)
{
    if (a != 1) return;
    player_pause();
    do_save();
}
void ui_save_again(void) { if (ui_unsaved) do_save(); }
static int edits_unsaved(void) { return ui_unsaved > 0; }
static void save_tap(void)
{
    player_state_t st;
    ui_player_state(&st);
    ui_settings_keep_state();                           /* (a new current instrument is something to save) */
    if (!ui_unsaved) { ui_toast(T("NOTHING TO SAVE")); return; }
    if (st.playing) { save_waiting = 1; ui_toast(T("SAVE WAITS FOR A PAUSE")); return; }
    do_save();
}
static void revert_done(int a)
{
    if (a != 1) return;
    ui_revert_all();                                    /* (it pauses playback first) */
    ui_toast(T("REVERTED TO SAVED"));
}
void ui_revert_all_ask(void)
{
    static char l2[40];
    if (!ui_unsaved) { ui_toast(T("NOTHING TO REVERT")); return; }
    ui_fmt(l2, sizeof l2, "%s", ui_txt_v(ui_unsaved == 1 ? T("DROP # UNSAVED CHANGE") : T("DROP # UNSAVED CHANGES"),
                                         ui_dec(ui_unsaved)));
    const char *lines[3] = {T("REVERT TO SAVED?"), l2, T("AND RELOAD FROM FLASH")};
    ui_ask(lines, 3, T("REVERT"), T("KEEP"), revert_done);
}

/* SETTINGS' MIDI CONTROLLER (specs/ui/screens/settings.md#midi-controller): there is no way back but switching the
 * FM-1 off and on, so it asks first, then about the unsaved changes (saved first, or dropped); then app.c ends the
 * tracker (ui_controller_wanted) and the controller's start screen comes */
static uint8_t ctl_want;
int ui_controller_wanted(void) { return ctl_want; }
static void ctl_go(void) { ctl_want = 1; }
static void ctl_save_answer(int a)
{
    if (a < 0) return;                                  /* (HOME: nothing happens) */
    if (a == 0) { ctl_go(); return; }                   /* DROP: the changes go with the tracker */
    player_pause();
    save_after = ctl_go;
    do_save();
    if (!saving) save_after = 0;                        /* (it could not start: its question or toast says why) */
}
static void ctl_answer(int a)
{
    static char l2[40];
    if (a != 1) return;
    if (!ui_unsaved) { ctl_go(); return; }
    ui_fmt(l2, sizeof l2, "%s", ui_txt_v(ui_unsaved == 1 ? T("# UNSAVED CHANGE") : T("# UNSAVED CHANGES"), ui_dec(ui_unsaved)));
    const char *lines[3] = {T("SAVE BEFORE SWITCHING?"), l2, T("DROP: THEY ARE LOST")};
    ui_ask(lines, 3, T("SAVE"), T("DROP"), ctl_save_answer);
}
void ui_controller_ask(void)
{
    if (saving) { ui_toast(T("A SAVE IS UNDER WAY")); return; }
    const char *lines[3] = {T("SWITCH TO MIDI CONTROLLER?"), T("PT/FM STOPS UNTIL THE FM-1"), T("IS SWITCHED OFF AND ON")};
    ui_ask(lines, 3, T("SWITCH"), T("BACK"), ctl_answer);
}

/* SETTINGS' FACTORY RESET (specs/ui/screens/settings.md): two questions, the second counting the changes not
 * saved (lost with the rest); then the user area erased (seed_wipe: the sound silent, every control waiting, a
 * progress bar) and the restart */
static int wipe_pct(void)
{
    seed_info_t s;
    seed_info(&s);
    return s.total ? s.done * 100 / s.total : 0;
}
static void reset_erase(int a)
{
    if (a != 1) return;
    if (saving || seed_wipe()) { ui_toast(T("A SAVE IS UNDER WAY")); return; }
    ui_progress_pct(T("ERASING"), wipe_pct);
}
static void reset_answer(int a)
{
    char l3[40];
    if (a != 1) return;
    ui_fmt(l3, sizeof l3, "%s", ui_txt_v(ui_unsaved == 1 ? T("# UNSAVED CHANGE LOST TOO") : T("# UNSAVED CHANGES LOST TOO"),
                                         ui_dec(ui_unsaved)));
    const char *lines[3] = {T("ERASE IT ALL NOW?"), T("THERE IS NO WAY BACK"), l3};
    ui_ask(lines, ui_unsaved ? 3 : 2, T("ERASE"), T("KEEP"), reset_erase);
}
void ui_factory_reset_ask(void)
{
    if (saving) { ui_toast(T("A SAVE IS UNDER WAY")); return; }
    const char *lines[3] = {T("FACTORY RESET?"), T("ERASES ALL YOU SAVED OR UPLOADED"), T("ONLY THE BUILT-IN INSTRUMENTS STAY")};
    ui_ask(lines, 3, T("RESET"), T("KEEP"), reset_answer);
}

/* ----------------------------------------------------------- the button grammar --- */
static void home_tap(void)
{
    ui_overlay_t *o = ui_top();
    if (o) { if (o->cancel) o->cancel(o); else ui_close(o); return; }
    if (cur->back && cur->back()) return;
    if (ndeep) {
        deep_t d = deep[--ndeep];
        int keep = ndeep;
        uint8_t lk = 0;
        if (ndeep) { lk = deep[ndeep - 1].locked; deep[ndeep - 1].locked = 0; }
        ui_go(d.from);
        ndeep = keep;
        if (ndeep) deep[ndeep - 1].locked = lk;
        if (d.back) d.back();
        return;
    }
    ui_open_home();
}
static void fx_tap(void)
{
    if (ndeep && deep[ndeep - 1].locked) { ui_refuse(B_EDIT); return; }
    if (is(cur, "FX")) {
        ui_go(fx_from && fx_from != cur ? fx_from : ui_screen_named("PLAYBACK"));
        return;
    }
    const ui_screen_t *fx = ui_screen_named("FX");
    if (fx) ui_go(fx);
    else ui_toast(T("FX: NOT IN THIS FIRMWARE YET"));
}
void ui_tap(int b)
{
    if (ui_blocking()) return;
    ui_overlay_t *o = ui_top();
    if (b == B_HOME) { home_tap(); return; }
    if (o) {
        if (o->tap && o->tap(o, b)) return;
        if (b != B_SAVE && !o->passes) return;
        if (o->passes) ui_close(o);
    }
    if (b == B_SAVE) { save_tap(); return; }
    if (b == B_FX && !(cur->tap && cur->tap(b))) { fx_tap(); return; }
    if (b == B_ARP && !cur->own_arp) { arp_tap(); return; }
    if (b == B_SEL) sel_armed = !nov && !cur->no_keyboard;     /* (PRESETS next: its cartridge list) */
    if (cur->tap && cur->tap(b)) return;
    if (b == B_OCTM || b == B_OCTP) ui_octave(b == B_OCTP ? 1 : -1);
}
/* the keys' octave a step up or down, its toast (OCT; on FX's EDIT, SEL + OCT) */
void ui_octave(int step)
{
    ui_cur.octave = (int8_t)ui_clamp(ui_cur.octave + step, -4, 4);
    char s[8];
    ui_fmt(s, sizeof s, "%s%d", ui_cur.octave > 0 ? "+" : "", ui_cur.octave);
    ui_toast(ui_txt_v(T("OCT #"), s));
}
/* button + button (either order), HOME's aside */
static int combo(int m, int b)
{
    if (m == B_HOME && b == B_LFO) { ui_open_help(); return 1; }
    if (m == B_SAVE && b == B_OCTM) { ui_revert_all_ask(); return 1; }
    if (m == B_SAVE && b == B_OCTP) { save_tap(); return 1; }  /* (SAVE held past its layer: let go, no tap) */
    if (cur->combo && cur->combo(m, b)) return 1;
    if (m == B_ARP && b == B_LFO && !cur->own_arp) { player_arp_clear(); ui_toast(T("LATCHED NOTES LET GO")); return 1; }
    return 0;
}
static int combo_known(int m, int b)
{
    return (m == B_HOME && b == B_LFO) || (m == B_SAVE && (b == B_OCTM || b == B_OCTP)) ||
        (cur->has_combo && cur->has_combo(m, b))
        || (m == B_ARP && b == B_LFO && !cur->own_arp);
}
static void start_hold(int b) { btn[b].due = ui_now() + (uint32_t)hold_ms(); btn[b].timer = 1; }

static void button_down(int b)
{
    if (ui_blocking()) return;
    uint32_t t = ui_now();
    btn_t *h = &btn[b];
    il_end();                                           /* (PRESETS' instrument list: closed, the button does its job) */
    /* PRESETS' cartridge list closes at any other control, which does its own job; HOME and SEL only close it (SEL
     * still held: SEL + PRESETS again) */
    if (pc_end() && (b == B_HOME || b == B_SEL)) { memset(h, 0, sizeof *h); h->down = 1; h->used = 1; h->t = t; return; }
    combo_t *hc = home();
    /* a punch-in's key held: ARP holds it on after the key is let go */
    if (b == B_ARP && ui_punch_now() >= 0 && !ui_punch_kept() && ui_punch_arp()) {
        memset(h, 0, sizeof *h); h->down = 1; h->used = 1; h->t = t;
        return;
    }
    /* HOME's combination: PLAY back to the start, REC all sound off (on press); held, LFO the help, SEQ the
     * white keys' mute / solo; any other button closes it */
    if (hc && b != B_HOME) {
        /* DJ: OCT- / OCT+ the tracks the white keys reach, with them (a member of theirs); nowhere to go: nothing */
        if ((b == B_OCTM || b == B_OCTP) && cur->home_oct) {
            if (cur->home_oct(b, 0) && member(hc, CK_WHITE)) cur->home_oct(b, 1);
            else if (!hc->is_ov) { home_sync(hc); ui_consume(B_HOME); }   /* (HOME held: its let-go no tap either way) */
            memset(h, 0, sizeof *h); h->down = 1; h->used = 1; h->t = t;
            return;
        }
        if ((b == B_PLAY || b == B_REC) && member(hc, b == B_PLAY ? CK_PLAY : CK_REC)) {
            if (hc->is_ov) close_home(hc);
            memset(h, 0, sizeof *h); h->down = 1; h->used = 1; h->t = t;
            if (b == B_PLAY) { if (cur->to_start) cur->to_start(); else player_to_start(); }
            else { h->cut = 1; cut(1); }
            return;
        }
        if (!hc->is_ov) {
            if (b == B_LFO && member(hc, CK_LFO)) { memset(h, 0, sizeof *h); h->down = 1; h->used = 1; h->t = t; ui_open_help(); return; }
            if (b == B_SEQ && ui_home_seq_switches(hc)) {
                hc->solo = !hc->solo; home_sync(hc); ui_consume(B_HOME);
                memset(h, 0, sizeof *h); h->down = 1; h->used = 1; h->t = t;
                return;
            }
            if (!hc->closed && b != B_PLAY && b != B_REC) close_home(hc);
        }
    }
    if (b == B_PLAY || b == B_REC) close_combos(-1);
    if (b == B_PLAY) {
        memset(h, 0, sizeof *h); h->down = 1; h->t = t; h->used = 1;
        ui_overlay_t *o = ui_top();
        if (o && o->play) { o->play(o); return; }       /* a list: its preview, or what it plays */
        int r = cur->play_down ? cur->play_down() : 0;
        if (r == 1) { h->used = 0; h->custom = 1; start_hold(b); return; }
        if (r == 2) return;
        start_hold(b);                                  /* held past the hold delay: stop, back to the start */
        ui_play_press();
        return;
    }
    if (b == B_REC) {
        memset(h, 0, sizeof *h); h->down = 1; h->t = t; h->used = 1;
        ui_overlay_t *o = ui_top();
        if (o && o->rec) { o->rec(o); return; }         /* (RECORD PATTERN: REC writes it) */
        if (cur->rec) cur->rec();
        return;
    }
    for (int m = 0; m < B_COUNT; m++) {
        if (m == b || m == B_HOME || m == B_OCTM || m == B_OCTP || !ui_active(m)) continue;
        int mm = m, bb = b;
        if (!combo_known(m, b) && b != B_OCTM && b != B_OCTP && combo_known(b, m)) { mm = b; bb = m; }
        if (combo_known(mm, bb) && ui_use_combo(m, CK_BUTTON + b)) {
            close_combos(m);
            memset(h, 0, sizeof *h); h->down = 1; h->used = 1; h->t = t;
            combo(mm, bb);
            return;
        }
    }
    close_combos(-1);
    memset(h, 0, sizeof *h);
    h->down = 1;
    h->t = t;
    start_hold(b);
}
static void hold_fired(int b)
{
    btn_t *h = &btn[b];
    h->timer = 0;
    if (b == B_PLAY && !h->custom) { player_stop(); return; }
    if (h->used) return;
    if (h->custom) { if (!ui_top() && cur->hold) cur->hold(b); return; }   /* (a screen's own PLAY: its hold, e.g. a panel) */
    if (!ui_top() && cur->hold && cur->hold(b)) { h->used = 1; return; }
    ui_overlay_t *o = ui_top();
    if (o && o->kind == OV_HOME) return;
    if (ui_layer_open(b)) h->layer = 1;
}
static void button_up(int b)
{
    btn_t *h = &btn[b];
    if (!h->down) return;
    btn_t was = *h;
    h->down = 0;
    h->timer = 0;
    if (b == B_SEL && pc.held) pc_end();               /* (PRESETS' list opened with SEL held: gone with it) */
    if (was.cut) cut(0);                                /* (CUT let go: heard again) */
    if (was.layer) { ui_layer_close(b); if (cur->layer_up) cur->layer_up(b); }
    if (was.custom) { if (cur->play_up) cur->play_up(was.used); return; }
    if (was.used || was.layer) return;
    if (b == B_PLAY || b == B_REC) return;
    if (combines(b)) {
        h->grace = 1;
        h->grace_until = ui_now() + (uint32_t)grace_ms();
        h->lock = was.lock; h->closed = was.closed; h->solo = was.solo;
        if (!grace_ms()) { h->grace = 0; ui_tap(b); }
    } else ui_tap(b);
}

/* ----------------------------------------------------------------- the keys --- */
static void solo(int t)
{
    if (cur->solo_key) { cur->solo_key(t); return; }    /* DJ: a deck's track, within its deck */
    ui_cur.solo = (int8_t)(ui_cur.solo == t ? -1 : t);
    if (ui_cur.solo < 0) ui_toast(T("SOLO OFF"));
    else ui_toast(ui_txt_v(T("TRACK # SOLO"), ui_hex2(t)));
    ui_mutes_changed();
}
static void note_down(int n)
{
    if (ui_blocking()) return;
    pc_end();                                           /* (PRESETS' cartridge list: closed, the key plays) */
    il_end();                                           /* (its instrument list too) */
    int bi = ui_black_index(n), wi = ui_white_index(n);
    combo_t *hc = home();
    keyuse_t *k = &key_use[n];
    memset(k, 0, sizeof *k);
    if (hc && member(hc, bi >= 0 ? CK_BLACK : CK_WHITE)) {
        if (bi >= 0) {
            k->kind = KU_SCREEN;
            if (hc->is_ov) close_home(hc);
            if (!SCREEN_KEY[bi]) return;
            if (ndeep && deep[ndeep - 1].locked) { ui_refuse(B_EDIT); return; }
            const ui_screen_t *s = ui_screen_named(T(SCREEN_KEY[bi]));
            if (s) ui_go(s);
        } else {
            k->kind = KU_MUTE; k->t = (uint8_t)wi; k->solo = hc->solo;
            if (!k->solo) k->due = ui_now() + (uint32_t)hold_ms();
        }
        return;
    }
    if (ui_active(B_FX) && wi >= 0 && ui_use_combo(B_FX, CK_PUNCH)) {
        close_combos(B_FX);
        k->kind = KU_PUNCH; k->t = (uint8_t)wi;
        ui_punch_down(wi);
        return;
    }
    close_combos(-1);
    ui_overlay_t *o = ui_top();
    if (o && o->key && o->key(o, n, 1)) { k->kind = KU_OVERLAY; return; }
    if (cur->key && cur->key(n, 1)) {
        k->kind = KU_SCREENKEY;
        for (int i = 0; i < nscreens; i++) if (screens[i] == cur) k->s = (uint8_t)i;
        return;
    }
    if (cur->no_keyboard) return;
    key_on(n);
    k->kind = KU_PLAY;
}
static void note_up(int n)
{
    keyuse_t k = key_use[n];
    memset(&key_use[n], 0, sizeof key_use[n]);
    switch (k.kind) {
    case KU_MUTE:
        if (k.held) break;                              /* solo: on at the hold */
        if (k.solo) solo(k.t);
        else if (cur->mute_key) cur->mute_key(k.t);    /* LOOPS: white key n mutes loop n; DJ a deck's track */
        else {
            ui_cur.mutes ^= 1u << k.t;
            ui_toast(ui_txt_v(ui_cur.mutes >> k.t & 1 ? T("TRACK # MUTED") : T("TRACK # ON"), ui_hex2(k.t)));
            ui_mutes_changed();
        }
        break;
    case KU_PUNCH: ui_punch_up(k.t); break;
    case KU_OVERLAY: { ui_overlay_t *o = ui_top(); if (o && o->key) o->key(o, n, 0); break; }
    case KU_SCREENKEY: if (k.s < nscreens && screens[k.s]->key) screens[k.s]->key(n, 0); break;   /* (where it went down) */
    case KU_PLAY: key_off(n); break;
    default: break;
    }
}
int ui_keys_held(void)
{
    int c = 0;
    for (int n = 0; n < 27; n++) if (key_use[n].kind == KU_PLAY || key_use[n].kind == KU_SCREENKEY) c++;
    return c;
}

/* ---------------------------------------------------------------- the knobs --- */
static int accel(int e, uint32_t t)
{
    uint32_t dt = t - enc_t[e];
    enc_t[e] = t;
    int sp = ui_set[S_KNOB_SPEED];
    if (sp == 0 || e == K_PRESETS) return 1;
    if (sp == 2) return dt < 30 ? 8 : dt < 55 ? 4 : dt < 90 ? 2 : 1;
    return dt < 30 ? 4 : dt < 60 ? 2 : 1;
}
static void encoder(int e, int step)
{
    if (ui_blocking()) return;
    int steps = step * accel(e, ui_now());
    combo_t *hc = home();
    if (hc) {
        if (e == K_SELECT && member(hc, CK_SELECT)) { screen_step(step); return; }
        if (hc->is_ov || !hc->closed) close_home(hc);
    }
    if (e == K_PRESETS && cur->no_keyboard) { pc_end(); close_combos(-1); return; }
    if (e == K_PRESETS) {
        int cart = ui_active(B_SEL) && ui_use_combo(B_SEL, CK_PRESETS);
        close_combos(cart ? B_SEL : -1);
        /* the list: open already (its next click), or to open: SEL held, in its grace, or tapped just before */
        int list = pc_find() ? 1 : (cart || sel_armed) && pc_room(!cart && sel_armed) ? 2 : 0;
        sel_armed = 0;
        if (list) cart = 1;
        uint16_t was = ui_cur.inst;
        int own = cart ? cur->presets_cart && cur->presets_cart(step) : cur->presets && cur->presets(step);
        if (!own) { if (cart) step_cart(step); else step_inst(step); }
        if (!cart && !own && ui_cur.inst != was) il_show();   /* (PRESETS' instrument list, where it may show) */
        if (!own || ui_cur.inst != was) {               /* (a screen's own that changed nothing has said why) */
            show_inst(cart);
            if (list == 2) { while (nov) ui_close(0); pc_open(); pc.held = btn[B_SEL].down; }   /* (SEL's tap's menu: the
                                                                                                * list in its place) */
        }
        return;
    }
    pc_end();
    il_end();
    /* ARP held: SELECT the rhythm (the EDITOR's row preview's too), ALGORITHM the pattern, K1 the latch (the
     * arpeggiator's: not on the EDITOR); in LOOPS and DJ nothing */
    int arp_k = !cur->own_arp ? e == K_SELECT || e == K_ALG || e == K_K1 : cur->own_arp == UI_ARP_PREVIEW && e == K_SELECT;
    if (ui_active(B_ARP) && arp_k && ui_use_combo(B_ARP, CK_ARP)) {
        close_combos(B_ARP);
        arp_knob(e, step);
        dirty = 1;
        return;
    }
    ui_overlay_t *o = ui_top();
    int sel = ui_active(B_SEL) && ui_use_combo(B_SEL, e == K_SELECT || e == K_ALG ? CK_MARK : CK_BIG);
    close_combos(sel ? B_SEL : -1);
    if (o) {
        if (sel && o->mark && (e == K_SELECT || e == K_ALG)) o->mark(o, e, steps);
        else if (o->knob) o->knob(o, e, steps, sel);
    } else if (sel && (e == K_SELECT || e == K_ALG) && cur->mark) cur->mark(e, steps);
    else if (cur->knob) cur->knob(e, steps, sel);
    dirty = 1;
}

/* ------------------------------------------------------------------ the timers --- */
/* what the screen edits, to the player (specs/ui/screens/editor.md: commands never change what you are editing): on
 * a change, and every half second (a request lost to a full queue) */
static void editing_poll(void)
{
    static uint8_t w0 = 0xFF;
    static uint32_t k0, at;
    uint8_t w = PL_ED_NONE;
    uint32_t k = 0;
    if (cur->editing) cur->editing(&w, &k);
    if (w == w0 && k == k0 && (int32_t)(ui_now() - at) < 500) return;
    player_editing(w, k);
    w0 = w;
    k0 = k;
    at = ui_now();
}

/* nothing played yet (since power-on, or since UNLOAD SONG): the player holds the keys' song all the same
 * (player_cue; UI_KEYS_SONG: the firmware's own), so the keys play as a synth through its FX setup at its tempo,
 * whatever songs there are and whichever is picked; a song that played stays held, stopped, until something else
 * plays. LOOPS shown (FX from LOOPS too) with nothing playing: the loops instead (loops_cue), their setup at their
 * tempo; LOOPS left, what was held before (player_cue_loops(0): at once, before any PLAY). Looked at every 100 ms (at
 * once after a screen change), asked again while the player does not have it (the store not mounted yet) */
static int loops_shown(void)
{
    return is(cur, "LOOPS") || (is(cur, "FX") && is(fx_from, "LOOPS"));
}
static void cue_poll(void)
{
    static uint32_t at;
    player_state_t st;
    if ((int32_t)(ui_now() - at) < 100 && !cue_now) return;
    at = ui_now();
    cue_now = 0;
    ui_player_state(&st);
    if (loops_shown()) {
        if (!st.playing && !st.stepping && !st.lcue && st.what != PL_LOOPS && st.what != PL_PREVIEW && st.what != PL_DJ)
            loops_cue();
        return;
    }
    if (st.lcue) { player_cue_loops(0); return; }
    if (st.what == PL_NONE && st.song != UI_KEYS_SONG) player_cue(UI_KEYS_SONG);
}
/* UNLOAD SONG (SONGS' menu, PLAYBACK's GLO): offered while anything but the keys' song is held, played or not now (a
 * song, the EDITOR's pattern on its setup, the loops, DJ's decks); let go, the sound cut at once; the keys back on the
 * keys' song's setup and tempo, as at power-on */
int ui_song_held(void)
{
    player_state_t st;
    ui_player_state(&st);
    return st.what == PL_SONG || st.what == PL_LOOP || (st.what == PL_LOOPS && !st.lcue) || st.what == PL_DJ;
}
void ui_unload_song(void)
{
    player_unload();
    player_cue(UI_KEYS_SONG);
    last_song_played = 0xFF;
    cue_now = 1;
    ui_toast(T("SONG UNLOADED"));
}

static void timers(void)
{
    uint32_t now = ui_now();
    for (int b = 0; b < B_COUNT; b++) {
        btn_t *h = &btn[b];
        if (h->down && h->timer && (int32_t)(now - h->due) >= 0) hold_fired(b);
        if (h->grace && (int32_t)(now - h->grace_until) >= 0) { h->grace = 0; ui_tap(b); }
    }
    for (int n = 0; n < 27; n++) {
        keyuse_t *k = &key_use[n];
        if (k->kind == KU_MUTE && k->due && !k->held && (int32_t)(now - k->due) >= 0) { k->held = 1; solo(k->t); }
    }
    static uint8_t stand_moved;                         /* (DJ's decks moving or their change waiting: SAVE lit;
                                                         * looked at every 50 ms) */
    static uint32_t stand_at;
    if (now - stand_at >= 50u) {
        stand_at = now;
        if ((uint8_t)ui_dj_stand_moved() != stand_moved) { stand_moved ^= 1u; ui_unsaved_refresh(); }
    }
    if (save_waiting) {
        player_state_t st;
        ui_player_state(&st);
        if (!st.playing) do_save();
    }
    save_poll();
}

/* ----------------------------------------------------------------------- LEDs --- */
static uint8_t led_now[LED_COUNT];
static uint8_t led_known[LED_COUNT];
static void led(int id, int v)
{
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    if (led_known[id] && led_now[id] == v) return;
    led_known[id] = 1;
    led_now[id] = (uint8_t)v;
    plat_led(id, (uint8_t)v);
}
/* a breath: dim to full and back in 2 s, full at t0, even to the eye (exponential) */
int ui_breath(uint32_t t0)
{
    /* level = DIM * 4^s, s = (1 + cos(2 pi ph)) / 2: a table of 32 steps */
    static const uint8_t BR[32] = {255, 252, 242, 227, 208, 187, 166, 146, 128, 111, 98, 87, 78, 72, 67, 65,
                                   64, 65, 67, 72, 78, 87, 98, 111, 127, 146, 166, 187, 208, 227, 242, 252};
    return BR[((ui_now() - t0) % 2000) * 32 / 2000];
}
static int oct_led(int k) { return k <= 0 ? 0 : k == 1 ? LV_DIM : k == 2 ? LV_FULL : (ui_blink(k == 3 ? 500 : 150) ? LV_FULL : 0); }
static void leds(void)
{
    int lv[B_COUNT] = {0};
    lv[B_HOME] = lv[B_SAVE] = lv[B_PLAY] = lv[B_FX] = lv[B_SEL] = lv[B_OCTM] = lv[B_OCTP] = LV_DIM;
    for (int b = 0; b < B_COUNT; b++) if (cur->does & BIT(b)) lv[b] = LV_DIM;
    if (!cur->own_arp) lv[B_ARP] = arp_is_on ? LV_FULL : LV_DIM;      /* the arpeggiator: lit while on */
    if (find_kind(OV_HELP)) lv[B_HOME] = LV_FULL;
    ui_overlay_t *o = ui_top();
    if (o && o->opener >= 0 && o->kind != OV_QUESTION) lv[o->opener] = LV_FULL;
    if (cur->lit) for (int b = 0; b < B_COUNT; b++) { int v = cur->lit(b); if (v != LV_NONE) lv[b] = v; }
    if (ndeep) lv[B_EDIT] = LV_FULL;
    if (ui_punch_now() >= 0) { if (ui_punch_kept()) lv[B_FX] = LV_FULL; else if (lv[B_ARP] < LV_DIM) lv[B_ARP] = LV_DIM; }
    lv[B_SAVE] = save_waiting ? (ui_blink(250) ? LV_DIM : 0) : ui_unsaved ? LV_DIM : 0;
    if (!is(cur, "FX") && !is(cur, "DJ")) { lv[B_OCTM] = oct_led(-ui_cur.octave); lv[B_OCTP] = oct_led(ui_cur.octave); }
    /* PLAY: green playing (brighter on the beat), red dim with a loop; red full paused; at the start red dim;
     * dark where PLAY has nothing to play */
    player_state_t st;
    ui_player_state(&st);
    int green = 0, red = 0;
    if (st.playing) {
        ui_song_t s;
        int rpb = ui_song(st.song, &s) ? s.rpb : 4;
        green = st.row % (rpb ? rpb : 4) == 0 && st.tick < 2 ? LV_FULL : 140;
        if (st.what != PL_SONG && st.what != PL_DJ) red = LV_DIM;
    } else if (can_play()) red = st.what != PL_NONE && !(st.pos == 0 && st.row == 0) ? LV_FULL : LV_DIM;
    lv[B_PLAY] = red;
    if (o && o->lit) {                                  /* an overlay's own (RECORD PATTERN's) */
        for (int b = 0; b < B_COUNT; b++) { int v = o->lit(o, b); if (v != LV_NONE) lv[b] = v; }
        int g = o->lit(o, B_COUNT);
        if (g != LV_NONE) green = g;
    }
    if (o && o->kind == OV_QUESTION) {                  /* a question (specs/ui/leds.md): its two answers lit, HOME dim
                                                         * (it cancels), the rest dark: nothing else acts */
        for (int b = 0; b < B_COUNT; b++) lv[b] = 0;
        lv[B_OCTM] = lv[B_OCTP] = LV_FULL;
        lv[B_HOME] = LV_DIM;
    }
    combo_t *hc = home();
    if (hc) {
        for (int b = 0; b < B_COUNT; b++) lv[b] = 0;
        lv[B_HOME] = ui_breath(hc->at);
        if (home_line(CK_PLAY, hc)) lv[B_PLAY] = LV_DIM;
        if (home_line(CK_REC, hc)) lv[B_REC] = LV_DIM;
        if (home_line(CK_LFO, hc)) lv[B_LFO] = LV_DIM;
        if (ui_home_seq_switches(hc)) lv[B_SEQ] = hc->solo ? LV_FULL : LV_DIM;
        if (home_line(CK_WHITE, hc) && cur->home_oct)   /* DJ: tracks that way */
            for (int b = B_OCTM; b <= B_OCTP; b++) lv[b] = cur->home_oct(b, 0) ? LV_DIM : 0;
    }
    if (cut_on) lv[B_REC] = LV_FULL;                    /* (CUT held) */
    for (int b = 0; b < B_COUNT; b++) if (ui_now() < flash_until[b]) lv[b] = ui_blink(80) ? LV_FULL : 0;
    for (int b = 0; b < B_COUNT; b++) led(b, hc && b == B_HOME ? lv[b] : (lv[b] > (btn[b].down ? 140 : 0) ? lv[b] : (btn[b].down ? 140 : 0)));
    led(LED_PLAY_G, green);
    /* the keys, in layers */
    uint8_t keys[27];
    memset(keys, 0, sizeof keys);
    if (hc) {
        if (home_line(CK_BLACK, hc) || hc->lock == CK_SELECT)
            for (int i = 0; i < 11; i++) if (SCREEN_KEY[i] && ui_screen_named(T(SCREEN_KEY[i])))
                keys[UI_BLACK[i]] = is(cur, T(SCREEN_KEY[i])) ? LV_FULL : LV_DIM;
        if (home_line(CK_WHITE, hc)) {
            if (cur->mute_leds) cur->mute_leds(keys);
            else for (int t = 0; t < 16; t++) keys[UI_WHITE[t]] = ui_cur.solo == t ? LV_FULL : ui_muted(t) ? 0 : LV_DIM;
        }
    } else if (ui_active(B_FX)) {
        ui_punch_leds(keys);
    } else {
        int took = (o && o->key_leds && o->key_leds(o, keys)) || (cur->key_leds && cur->key_leds(keys));
        if (!took) {
            /* a played key's light: full while its note is loud, dim under half its loudest, off under an
             * eighth; lit at once for 100 ms */
            pl_voice_info_t v[PL_VOICES];
            int nv = player_voices_now(v, PL_VOICES);
            uint8_t lvl[27];
            memset(lvl, 0, sizeof lvl);
            for (int i = 0; i < nv; i++) {
                if (!v[i].on || v[i].track != 0xFF) continue;
                int n = v[i].note - ui_midi(0);
                if (n < 0 || n >= 27) continue;
                if (v[i].level > key_max[n]) key_max[n] = v[i].level;
                if (v[i].level > lvl[n]) lvl[n] = v[i].level;
            }
            for (int n = 0; n < 27; n++) {
                int m = key_max[n];
                int full = m && lvl[n] * 2 >= m, dim = m && lvl[n] * 8 >= m;
                int fresh = key_at[n] && (int32_t)(ui_now() - key_at[n]) < 100;   /* (| 1: maybe 1 ms ahead) */
                if (key_use[n].kind == KU_PLAY && !nv) full = 1;
                keys[n] = full || fresh ? LV_FULL : dim ? LV_DIM : 0;
                if (!lvl[n]) key_max[n] = 0;
            }
        }
    }
    for (int n = 0; n < 27; n++) led(LED_NOTE(n), keys[n]);
}

/* ---------------------------------------------------------------------- frame --- */
/* OVER 9000 (specs/ui/frame.md#the-header): the sound's CPU share on either core, each core's mean over each second
 * (172 halves or a few more: the main loop's passes cut the stretches) from the platform's sums; on above 90 %, off
 * after a second at 90 % or less on both. The same pass watches for core 2's stop: its toast once (a fallback ends
 * core 2's use for the rest of this start: specs/second-core.md#start-switch-fallback) */
#define OVER_HALVES 172
static uint8_t over9000;
static void cpu_watch(void)
{
    static uint32_t sum0, sum20, n0;
    static uint8_t primed, c2_seen = 0xFF, c2_told;     /* (c2_seen 0xFF: no state seen yet) */
    plat_cpu_t c;
    plat_cpu(&c, 0);
    /* (only a change to STOPPED: stopped from the start on, after a crash, it is DEVICE's to say) */
    if (c.c2_state == PLAT_C2_STOPPED && c2_seen != 0xFF && c2_seen != PLAT_C2_STOPPED && !c2_told) {
        c2_told = 1;
        ui_toast(T("SECOND CORE STOPPED"));
    }
    c2_seen = c.c2_state;
    if (!primed) { primed = 1; sum0 = c.sum_us; sum20 = c.c2_sum_us; n0 = c.halves; return; }
    uint32_t n = c.halves - n0, s = c.sum_us - sum0, s2 = c.c2_sum_us - sum20;
    if (n < OVER_HALVES) return;
    if (s2 > s) s = s2;                                 /* (the busier core's) */
    int on = c.half_us && (uint64_t)s * 10u > (uint64_t)n * c.half_us * 9u;
    if (on != over9000) { over9000 = (uint8_t)on; dirty = 1; }
    sum0 = c.sum_us;
    sum20 = c.c2_sum_us;
    n0 = c.halves;
}
int ui_over9000(void) { return over9000; }
/* a core's mean share since the sums were last read, %: its time summed (sum) over the halves */
static int cpu_mean(uint32_t sum, uint32_t halves, uint32_t half_us, uint32_t *sum0, uint32_t *halves0)
{
    uint32_t n = halves - *halves0, s = sum - *sum0;
    if (!n || !half_us) return -1;
    *sum0 = sum;
    *halves0 = halves;
    uint32_t mean = (s + n / 2) / n;                    /* (µs a half: no overflow however long the stretch) */
    return (int)((mean * 100 + half_us / 2) / half_us);
}
int ui_cpu_mean(uint32_t *sum0, uint32_t *halves0)
{
    plat_cpu_t c;
    plat_cpu(&c, 0);
    return cpu_mean(c.sum_us, c.halves, c.half_us, sum0, halves0);
}
int ui_cpu2_mean(uint32_t *sum0, uint32_t *halves0)
{
    plat_cpu_t c;
    plat_cpu(&c, 0);
    return cpu_mean(c.c2_sum_us, c.halves, c.half_us, sum0, halves0);
}
/* the header's text: the screen's name, subpage and page, a toast, a knob turned or the help's; SEQ's place */
static void header_text(void)
{
    char sub_b[40], page_b[24], name_b[16];
    const char *text, *sub = 0, *page = 0, *seq = 0, *mark = 0;
    int col = C_WHITE, runs = 0;
    ui_overlay_t *help = find_kind(OV_HELP);
    if (ui_now() < knob_show.until) text = knob_show.text;   /* a knob turned: its name and value, white, 1.2 s */
    else if (ui_now() < toast.until) { text = toast.text; runs = toast.nruns; }
    else if (help) { text = T("LOOK FOR ONSWERS"); col = C_CYAN; page = ui_help_page_text(help, page_b); }
    else {
        ui_fmt(name_b, sizeof name_b, "%s", T(cur->name));   /* (kept: the screen's sub, page and seq unpack theirs) */
        text = name_b;
        col = UI_GROUP_COL[cur->group];
        sub = cur->sub ? cur->sub() : 0;
        page = cur->page ? cur->page() : 0;
        seq = cur->seq && !nov ? cur->seq() : 0;
        if (is(cur, "LOOPS")) mark = T("BETA");         /* (the name's mark, small: frame.md) */
        if (sub) { strncpy(sub_b, sub, sizeof sub_b - 1); sub_b[sizeof sub_b - 1] = 0; sub = sub_b; }
        if (page && page != page_b) { strncpy(page_b, page, sizeof page_b - 1); page_b[sizeof page_b - 1] = 0; page = page_b; }
    }
    char full[96];
    ui_fmt(full, sizeof full, "%s%s%s%s%s", text, sub ? " " : "", sub ? sub : "", page ? " " : "", page ? page : "");
    int room = 200, end;                                /* (to x 200, before the voices) */
    if (g_tw(&FONT_S, full) <= room) {
        int x = runs ? g_runs(2, 0, &FONT_S, toast.runs, runs) : g_text(2, 0, &FONT_S, text, col);
        if (mark) x = g_text(x + 2, 0, &FONT_T, mark, C_DGREY);   /* (T's capitals from row 2, as S's) */
        if (sub) x = g_text(x + 8, 0, &FONT_S, sub, C_WHITE);
        end = page ? g_text(x + 8, 0, &FONT_S, page, C_LGREY) : x;
    } else {
        int x = runs ? g_runs(2, 2, &FONT_T, toast.runs, runs) : g_textn(2, 2, &FONT_T, text, 33, col);
        char ts[80];
        ui_fmt(ts, sizeof ts, "%s %s", text, sub ? sub : "");
        if (sub && g_tw(&FONT_T, ts) <= room) x = g_text(x + 6, 2, &FONT_T, sub, C_WHITE);
        end = page && g_tw(&FONT_T, full) <= room ? g_text(x + 6, 2, &FONT_T, page, C_LGREY) : x;
    }
    if (seq) {
        char s[24];
        ui_fmt(s, sizeof s, T("SEQ %s"), seq);
        int sx = 199 - g_tw(&FONT_T, s);
        if (end + 8 <= sx) {
            g_run_t r[2] = {{T("SEQ"), C_YELLOW}, {s + 3, C_LGREY}};
            g_runs(sx, 2, &FONT_T, r, 2);
        }
    }
}
/* the header's right part (specs/ui/frame.md#the-header): the transport, the CPU bar and the voices, the battery,
 * each in a box of its own fixed width and the text's height, rows 2-11 (where S's digits and capitals are) */
static const uint8_t DIGITS_3X10[10][10] = {    /* 10 and more voices: two narrow digits (frame.md#fonts), a row of
                                                 * 3 bits each, bit 2 the left column */
    {7, 5, 5, 5, 5, 5, 5, 5, 5, 7}, {2, 6, 2, 2, 2, 2, 2, 2, 2, 7}, {7, 1, 1, 1, 7, 4, 4, 4, 4, 7},
    {7, 1, 1, 1, 7, 1, 1, 1, 1, 7}, {5, 5, 5, 5, 7, 1, 1, 1, 1, 1}, {7, 4, 4, 4, 7, 1, 1, 1, 1, 7},
    {7, 4, 4, 4, 7, 5, 5, 5, 5, 7}, {7, 1, 1, 1, 1, 2, 2, 2, 2, 2}, {7, 5, 5, 5, 7, 5, 5, 5, 5, 7},
    {7, 5, 5, 5, 7, 1, 1, 1, 1, 7},
};
static void voices_now(int x, int n, int limit)  /* 7 x 10 at x, 2 */
{
    if (n > 99) n = 99;
    int c = ui_voices_col(n, limit);
    if (n < 10) {                                       /* (S's digit: its glyph from x, rows 2-11) */
        char s[2] = {(char)('0' + n), 0};
        g_text(x - 1, 0, &FONT_S, s, c);
        return;
    }
    for (int k = 0; k < 2; k++) {                       /* (the tens, then the units, a column apart) */
        int d = k ? n % 10 : n / 10;
        for (int j = 0; j < 10; j++)
            for (int i = 0; i < 3; i++) if (DIGITS_3X10[d][j] >> (2 - i) & 1) g_px(x + 4 * k + i, 2 + j, c);
    }
}
/* the transport, 7 x 10 at x, 2 whichever it shows: 1 ▶, 2 ❚❚, 3 ● (a disc 7 x 8 on the text's middle) */
static void transport(int x, int kind, int c)
{
    static const uint8_t PLAY[10] = {1, 3, 4, 6, 7, 7, 6, 4, 3, 1}, REC[8] = {3, 5, 7, 7, 7, 7, 5, 3};
    if (kind == 1) for (int j = 0; j < 10; j++) g_rect(x, 2 + j, PLAY[j], 1, c);
    else if (kind == 2) { g_rect(x, 2, 2, 10, c); g_rect(x + 5, 2, 2, 10, c); }
    else if (kind == 3) for (int j = 0; j < 8; j++) g_rect(x + (7 - REC[j]) / 2, 3 + j, REC[j], 1, c);
}
/* the battery, 7 x 10 at x, 2: standing, its nub on top; inside, three bars lit from the bottom; with none left, its
 * shell red. Charging (a USB host has the FM-1 connected and powers it: no charger line tells more; or no battery
 * reading), the bars fill from the bottom in turn, light blue, a step every half second */
static void battery(int x)
{
    int bat = plat_battery(), usb = bat < 0 || plat_usb_power(), bars = bat >= 67 ? 3 : bat >= 34 ? 2 : bat >= 10 ? 1 : 0;
    if (usb) bars = 1 + (int)(ui_now() / 500u % 3u);
    int bcol = usb ? C_LBLUE : bars >= 2 ? C_GREEN : C_YELLOW, shell = !usb && !bars ? C_RED : C_LGREY;
    g_rect(x + 2, 2, 3, 1, shell);
    g_box(x, 3, 7, 9, shell);
    for (int i = 0; i < 3; i++) g_rect(x + 2, 9 - 2 * i, 3, 1, i < bars ? bcol : C_DEEP + C_DGREY);
}
static void header(void)
{
    g_rect(0, 0, 240, 16, C_BLACK);
    /* OVER 9000 (the sound at its limit): it alone, the header's text blank, the symbols on the right as ever */
    if (over9000) g_text(52, 0, &FONT_S, T("!!! OVER 9000 !!!"), C_RED);
    else header_text();
    /* the transport, the CPU bar and the voices (a pair: 1 px apart), the battery: drawn the same always */
    player_state_t st;
    ui_player_state(&st);
    if (st.count_beats || (cur->recording && cur->recording())) transport(207, 3, C_RED);   /* (frame.md: ● recording) */
    else if (st.playing) transport(207, 1, C_GREEN);
    else if (st.what != PL_NONE) transport(207, 2, C_RED);
    voices_now(222, st.voices_long, st.voice_count);    /* (the most over the last half second) */
    plat_cpu_t c;
    plat_cpu(&c, 0);
    g_rect(217, 2, 4, 10, C_DEEP + C_DGREY);            /* (the CPU bar: 4 x 10 at 217, 2; a column of 2 a core) */
    static uint32_t late_seen, late_at;
    if (c.late != late_seen) { late_seen = c.late; late_at = ui_now() | 1; }
    int late = late_at && (int32_t)(ui_now() - late_at) < 400;  /* (| 1: maybe 1 ms ahead) */
    for (int k = 0; k < 2; k++) {                       /* core 1's, then core 2's only while it runs */
        if (k && c.c2_state != PLAT_C2_ON) break;
        int cpu = k ? ui_cpu2_now() : ui_cpu_now(), h = (cpu * 10 + 50) / 100, col = cpu < 50 ? C_GREEN : cpu < 80 ? C_YELLOW : C_RED;
        if (h < 1) h = 1;
        if (h > 10) h = 10;
        g_rect(217 + 2 * k, 12 - h, 2, h, late ? (ui_blink(80) ? C_RED : C_BLACK) : col);
    }
    battery(232);
    g_hline(0, 16, 240, C_DGREY);
}
static void footer(void)
{
    g_rect(0, 227, 240, 13, C_BLACK);
    g_hline(0, 227, 240, C_DGREY);
    char left[64], name[12], id[8];
    uint16_t ref = keys_inst();
    int eng = ui_cart_kind((uint8_t)(ref >> 8)), has = ui_inst_name(ref, name);
    ui_ref_text(id, ref);
    ui_fmt(left, sizeof left, "%s %s", id, has ? name : T("EMPTY"));
    if (!is(cur, "EDITOR") && eng) ui_fmt(left + strlen(left), (int)(sizeof left - strlen(left)), " %s", T(ENG_SHORT[eng]));
    const char *right = 0;
    uint8_t rcol = C_WHITE;
    if (cur->footer) right = cur->footer(&rcol);       /* (a knob turned shows in the header: frame.md) */
    int room = 40;
    if (right && right[0]) room = 40 - (int)strlen(right) - 1;
    if (room < 0) room = 0;
    g_run_t hints[8];
    int nh = cur->foot_hints ? cur->foot_hints(hints, 8) : 0;
    if (nh) {                                           /* a screen where the instrument plays no part (DJ): key hints */
        int x = 0, left_n = room;
        for (int i = 0; i < nh && left_n > 0; i++) {
            int l = (int)strlen(hints[i].s);
            x = g_textn(x, 228, &FONT_T, hints[i].s, l < left_n ? l : left_n, hints[i].c);
            left_n -= l;
        }
    } else g_textn(0, 228, &FONT_T, left, room, has ? ENG_COL[eng] : C_DGREY);
    if (right && right[0]) g_text(240 - g_tw(&FONT_T, right), 228, &FONT_T, right, rcol);
}
/* the count-in's box (specs/ui/transport.md#the-count-ins-box), over the screen that counts in: the beats left as
 * a digit 32 x 64, a dot a beat, the bar; armed (COUNT-IN KEY), what starts the take */
static void count_box(void)
{
    player_state_t st;
    ui_player_state(&st);
    int armed = cur->armed && cur->armed();
    if (!st.count_beats && !armed) return;
    int x = 50, w = 140, h = 118, y = BODY_Y + (BODY_H - h) / 2;
    g_rect(x, y, w, h, C_BLACK);
    g_box(x, y, w, h, C_RED);
    if (!st.count_beats) {
        const char *ta = T("TAKE ARMED"), *pl = T("PLAY"), *an = T("A NOTE");
        g_text(g_centre(&FONT_S, ta, x, w), y + 12, &FONT_S, ta, C_RED);
        g_text(g_centre(&FONT_L, pl, x, w), y + 36, &FONT_L, pl, C_WHITE);
        g_text(g_centre(&FONT_S, an, x, w), y + 74, &FONT_S, an, C_WHITE);
        g_run_t r[2] = {{T("HOME"), C_YELLOW}, {T(": CANCEL"), C_LGREY}};
        g_runs(x + (w - g_runs_w(&FONT_T, r, 2)) / 2, y + h - 16, &FONT_T, r, 2);
        return;
    }
    int k = st.count_beat ? st.count_beat - 1 : 0, beat = k % 4;
    const char *ci = T("COUNT-IN");
    char left[2] = {(char)('4' - beat), 0}, bar[24];
    g_text(g_centre(&FONT_T, ci, x, w), y + 6, &FONT_T, ci, C_RED);
    g_text(g_centre(&FONT_X, left, x, w), y + 20, &FONT_X, left, beat == 0 ? C_YELLOW : C_WHITE);
    for (int i = 0; i < 4; i++) g_rect(x + 45 + i * 14, y + 92, 8, 8, i > beat ? C_DGREY : i == 0 ? C_YELLOW : C_WHITE);
    ui_fmt(bar, sizeof bar, "%s", ui_txt_v(ui_txt_v(T("BAR # OF #"), ui_dec(k / 4 + 1)), ui_dec(st.count_beats / 4)));
    g_text(g_centre(&FONT_T, bar, x, w), y + h - 14, &FONT_T, bar, C_LGREY);
}
static void draw_frame(void)
{
    g_clear(C_BLACK);
    g_clip(0, BODY_Y, 240, BODY_H);
    if (seed_active() && !seed_wiping()) ui_seed_draw();   /* (FACTORY RESET's erasing: its progress bar on SETTINGS) */
    else {
        cur->draw();
        count_box();
        for (int i = 0; i < nov; i++) { g_clip(0, BODY_Y, 240, BODY_H); if (ovs[i].draw) ovs[i].draw(&ovs[i]); }
        g_clip(0, BODY_Y, 240, BODY_H);
        ui_layers_draw();
    }
    g_noclip();
    header();
    footer();
}
static uint32_t frame_at, frame_ms_last, fps, frames_n, fps_t0;
uint32_t ui_frame_ms(void) { return frame_ms_last; }
uint32_t ui_fps(void) { return fps; }
/* the player as the frame shows it: taken once at its start, read by both halves */
static player_state_t frame_ps;
static pl_voice_info_t frame_pv[PL_VOICES];
static int frame_nv;
static uint8_t drawing;
void ui_player_state(player_state_t *s)
{
    if (drawing) *s = frame_ps;
    else player_state(s, 0);
}
int ui_player_voices(pl_voice_info_t *v, int max)
{
    if (!drawing) return player_voices_now(v, max);
    int n = frame_nv < max ? frame_nv : max;
    memcpy(v, frame_pv, (size_t)n * sizeof *v);
    return n;
}
/* the CPU the header's bar and STATUS show (specs/ui/frame.md#the-header): each core's mean over the halves since the
 * last reading, read again at the first frame 100 ms or more after it (so every 100-133 ms), the same in a frame's
 * halves */
static uint32_t cpu_at, cpu_sum0, cpu_n0, cpu2_sum0, cpu2_n0;
static int cpu_shown, cpu2_shown;
static void cpu_read(void)
{
    uint32_t now = ui_now();
    if (drawing || (cpu_at && (int32_t)(now - cpu_at) < 100)) return;   /* (| 1: maybe 1 ms ahead) */
    int v = ui_cpu_mean(&cpu_sum0, &cpu_n0), v2 = ui_cpu2_mean(&cpu2_sum0, &cpu2_n0);
    if (v >= 0) cpu_shown = v;
    if (v2 >= 0) cpu2_shown = v2;
    cpu_at = now | 1;
}
int ui_cpu_now(void) { cpu_read(); return cpu_shown; }
int ui_cpu2_now(void) { cpu_read(); return cpu2_shown; }
void (*ui_between_halves)(void);        /* (the tests': something changing between the halves) */
void ui_render(void)
{
    uint32_t t0 = plat_us();
    player_state(&frame_ps, 0);
    frame_nv = player_voices_now(frame_pv, PL_VOICES);
    ui_cpu_now();
    drawing = 1;
    for (int half = 0; half < 2; half++) {
        g_band(half);
        draw_frame();
        g_push();
        if (!half && ui_between_halves) ui_between_halves();
    }
    drawing = 0;
#ifdef PTFM_DEBUG
    dbg_frame(plat_us() - t0);
#endif
    frame_ms_last = (plat_us() - t0 + 500) / 1000;
    frames_n++;
    if (ui_now() - fps_t0 >= 1000) { fps = frames_n; frames_n = 0; fps_t0 = ui_now(); }
    dirty = 0;
}

/* -------------------------------------------------------------------- the pass --- */
static plat_input_t in_last;
static uint8_t started;
static void pick_current(void);
/* seeding has ended: the library is new; what the screens hold of it is read again */
static void after_seed(void)
{
    seed_info_t si;
    seed_info(&si);
    player_store_changed();
    ui_settings_load();
    for (int k = 0; k < S_COUNT; k++) ui_settings_apply(k);
    if (!dj_live()) dj_load();                          /* (DJ's stand as the seed left it; the loops: LOOPS' enter) */
    pick_current();
    if (cur->enter) cur->enter(cur);
    g_invalidate();
    if (si.result) ui_toast(T("DATA NOT RECEIVED: TRY AGAIN"));
    else if (si.bad) ui_toast(ui_txt_v(T("DATA RECEIVED, # BAD RECORDS"), ui_dec(si.bad)));
    else ui_toast(ui_txt_v(T("DATA RECEIVED: # RECORDS"), ui_dec(si.records)));
}
static void turn(int e, int k)                          /* a knob's clicks, one at a time */
{
    while (k > 0) { encoder(e, 1); k--; }
    while (k < 0) { encoder(e, -1); k++; }
}
void ui_poll(void)
{
    if (!started) return;
    static uint8_t was_seeding;
    int seeding = seed_active();
    if (seeding && !was_seeding) {                      /* seeding starts: what is held lets go now (the input is
                                                           ignored meanwhile, its releases with it) */
        for (int n = 0; n < 27; n++) if (in_last.notes >> n & 1) note_up(n);
        for (int b = 0; b < B_COUNT; b++) if (in_last.buttons >> b & 1) button_up(b);
        dirty = 1;
    }
    if (was_seeding && !seeding) after_seed();
    was_seeding = (uint8_t)seeding;
    cpu_watch();
    plat_input_t in;
    memset(&in, 0, sizeof in);                          /* (knob_pre 0 where the platform cannot tell) */
    plat_input(&in);
    midi_merge(&in);                                    /* MIDI's learned controls and notes: as the FM-1's own */
    /* MIDI learn takes the FM-1's input while it waits (it does nothing else then) */
    int learning = midi_learning() && midi_learn_take(&in, &in_last);
    /* the clicks that came before the pass's first press, then the presses (buttons, then keys), the other
     * clicks, then releases (keys, then buttons): a turn then a quick tap in one pass stays a turn and a tap, a
     * press then a turn is the button held + the knob */
    uint32_t bd = in.buttons & ~in_last.buttons, bu = in_last.buttons & ~in.buttons;
    uint32_t nd = in.notes & ~in_last.notes, nu = in_last.notes & ~in.notes;
    if (seed_active() || learning) {
        bd = bu = nd = nu = 0;
        memset(in.knob, 0, sizeof in.knob);
        memset(in.knob_pre, 0, sizeof in.knob_pre);
    }
    for (int e = 0; e < K_COUNT; e++) turn(e, in.knob_pre[e]);
    for (int b = 0; b < B_COUNT; b++) if (bd >> b & 1) { button_down(b); dirty = 1; }
    for (int n = 0; n < 27; n++) if (nd >> n & 1) { note_down(n); dirty = 1; }
    for (int e = 0; e < K_COUNT; e++) turn(e, in.knob[e] - in.knob_pre[e]);
    for (int n = 0; n < 27; n++) if (nu >> n & 1) { note_up(n); dirty = 1; }
    for (int b = 0; b < B_COUNT; b++) if (bu >> b & 1) { button_up(b); dirty = 1; }
    in_last = in;
    ui_master = in.master;
    lp_live_poll();                                     /* (before a SAVE waiting for a pause starts: a song's last pass
                                                         * is written first) */
    timers();
    ui_dj_poll();                                       /* (a LOAD at the bar, JUMP / MOVE on the beat off DJ too) */
    if (cur->tick) cur->tick();
    ui_dlg_poll();
    il_poll();                                          /* (PRESETS' instrument list: gone 1 s after its last click) */
    ui_level_poll();
    ui_voices_poll();
    ui_play_mode_poll();
    editing_poll();
    cue_poll();
    cut_poll();
    leds();
    uint32_t now = ui_now();
#ifdef PTFM_DEBUG
    static uint8_t frozen_drawn;                        /* FREEZE THE SCREEN (specs/debug.md): its message drawn, then no more */
    if (dbg_frozen() && frozen_drawn) return;
#endif
    if (now - frame_at >= 33) {                         /* about 30 frames a second */
        frame_at = now;
        ui_render();
#ifdef PTFM_DEBUG
        frozen_drawn = (uint8_t)dbg_frozen();
#endif
    }
}
uint16_t ui_master;

/* the current instrument: as SAVE last kept it (F0:00 at first, the first built-in instrument); gone, the first
 * cartridge's first instrument; the current song: the first there, unless the one now is */
static void pick_current(void)
{
    uint8_t c[256];
    int n = ui_carts(c, 256);
    char nm[12];
    if (n && !ui_inst_name(ui_cur.inst, nm)) {
        ui_cur.inst = (uint16_t)(c[0] << 8);
        for (int s = 0; s < ui_cart_slots(c[0]); s++) if (ui_inst_name((uint16_t)(c[0] << 8 | s), nm)) { ui_cur.inst = (uint16_t)(c[0] << 8 | s); break; }
    }
    ui_song_t sg;
    uint8_t songs[1];
    if (!ui_song(ui_cur.song, &sg) && ui_songs(songs, 1)) ui_cur.song = songs[0];
}

void ui_init(void)
{
    loops_fx_new = fxd_default;                         /* (LOOPS with no FX setup yet: a new song's) */
    ui_keys_song_init();                                /* (the keys' song: the firmware's own, never listed) */
    ui_editor_pattern_init();                           /* (the EDITOR's own pattern: the same) */
    ui_edit_init();
    seed_set_unsaved(edits_unsaved);                    /* seeding refuses with edits unsaved: SAVE OR REVERT FIRST */
    ui_settings_load();
    g_theme(ui_set[S_THEME]);
    ui_settings_apply(S_KEYS_OCTAVE);
    ui_settings_apply(S_VOICES);
    ui_settings_apply(S_SECOND_CORE);                   /* (core 2 on or off, as saved: before its start, 2 s in) */
    ui_settings_apply(S_OUTPUT);                        /* (the analog out's mono and LINE GAIN, as saved) */
    ui_settings_apply(S_USB_GAIN);
    ui_settings_apply(S_KEYS_GAIN);
    ui_settings_apply(S_KEYS_FX);
    ui_cur.inst = ui_settings_inst();                   /* (as SAVE last kept it) */
    ui_cur.pattern = ui_settings_pattern();             /* (the EDITOR's: the same; gone, its own) */
    pick_current();
    ui_screens_register();
    const ui_screen_t *first = ui_power_on_screen();
    if (!first) first = screens[0];
    plat_input(&in_last);                               /* what is held at power-on is no press */
    started = 1;
    ui_go(first);
    ui_render();
}

/* EDIT jumps into the EDITOR go deep: HOME tap returns */
void ui_go_deep(const ui_screen_t *s, void (*back)(void), int locked)
{
    const ui_screen_t *from = cur;
    int keep = ndeep;
    deep_t d = {from, back, (uint8_t)locked};
    ui_go(s);
    ndeep = keep;
    if (ndeep < 4) deep[ndeep++] = d;
}
int ui_is_deep(void) { return ndeep > 0; }

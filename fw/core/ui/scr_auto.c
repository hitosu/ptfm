/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* AUTOMATIONS (specs/ui/screens/automations.md): 16 slots, each the FM-1's own input recorded at its times and played
 * back in its place, on the screen it was recorded on. ui_poll hands each pass's input (MIDI's learned controls merged
 * in) to ua_input before it acts on it: recording takes its changes, playing stands in for it (MASTER the controls',
 * always). The file (specs/files/automations.md): kind REC_AUTO, number 00, a record a slot's part (sub: the slot x 16 +
 * the part): part 0's byte 0 the screen recorded on; each part then its events' count (2 B) and the events bit-packed,
 * whole in a part, a time counting on across parts. COMPACT writes a slot's parts again with its pauses shortened; HOME,
 * ARP, then a white key (ui.c) plays a slot from any screen, HOME, ARP, REC, then a white key records into one; HOME,
 * ARP, REC ends a recording where it is. */
#include <string.h>
#include "ui_core.h"
#include "ui_priv.h"
#include "ui_data.h"
#include "../store/store.h"
#include "../store/seed.h"

#define PART_MAX (STORE_MAX_REC - STORE_HDR)    /* a part's bytes at most */
#define PEND 192                                /* recorded bits waiting for the edit memory, in bytes */
#define OP6 5                                   /* HOME + this black key: AUTOMATIONS */
enum { UA_IDLE, UA_REC, UA_PLAY };
extern const ui_screen_t SCR_AUTO;
enum { EV_PRESS, EV_RELEASE, EV_KNOB };
static const uint8_t TB[5] = {5, 8, 12, 16, 24};   /* a time class's bits after its prefix (0, 10, 110, 1110, 1111) */
static struct {
    uint8_t mode, slot, sel, part, scr, home, end, fresh, wrote;
    uint8_t pend[PEND + 8];             /* recording: the part's bytes from byte fbyte of its events on */
    uint16_t nev;                       /* the part's events: written (recording), left (playing) */
    uint32_t bits, fbyte, fbits;        /* recording: the part's event bits; its event bytes in the edit memory whole; its
                                         * bits at the last flush */
    uint32_t t0, last, home_t, flushed; /* ms: time 0, the last event's, HOME's held-back press, the last flush (recording)
                                         * or the last pass (playing) */
    uint32_t ob, on;                    /* the automation's buttons and keys down (recorded, or given while playing) */
    uint32_t rb, rn, mb, mn;            /* playing: the controls' own last; masked till let go (the player's) */
    struct { uint32_t t; uint8_t k, id, dt; int8_t c; } nx;   /* playing: the next event (dt: 0 in the same pass) */
    uint8_t have;
    const ui_screen_t *from;            /* where AUTOMATIONS was opened from */
    uint32_t gen, dur[16];              /* the slots' lengths (ms), counted while the store is at gen */
    int16_t scrs[16];                   /* ... their screens (-1 empty) */
    struct { uint8_t ok, part; uint16_t nev; uint32_t bits, last, ob, on; } mk;   /* recording: the stream as it was before
                                         * its last HOME press written, while only HOME's, OP6's, ARP's and REC's events
                                         * followed (the end's, HOME tapped then OP6, or then ARP and REC: taken out
                                         * again at the end, rewind_end) */
} ua;

static uint8_t sub(int part) { return (uint8_t)(ua.slot * 16 + part); }
static const uint8_t *part_at(int slot, int part, uint32_t *len)
{
    uint8_t ver;
    const uint8_t *p = store_get(REC_AUTO, 0, (uint8_t)(slot * 16 + part), len, &ver);
    return p && ver == 1 && *len >= (part ? 2u : 3u) ? p : 0;
}
int ua_state(void) { return ua.mode; }
int ua_slot(void) { return ua.slot; }

/* ---- a stream's coding ---- */
static uint32_t off_of(int k) { uint32_t o = 0; for (int i = 0; i < k; i++) o += 1u << TB[i]; return o; }
static void put(uint8_t *b, uint32_t *at, uint32_t n, uint32_t v) { if (b) ui_put_bits(b, *at, (int)n, v); *at += n; }
/* an event coded at bit *at of b (NULL: counted only): its time since the event before (d, ms), then its action */
static void code(uint8_t *b, uint32_t *at, uint32_t d, int k, int id, int c)
{
    int c4 = 0;
    while (c4 < 4 && d >= off_of(c4 + 1)) c4++;
    put(b, at, c4 < 4 ? (uint32_t)c4 + 1 : 4u, (1u << c4) - 1);
    put(b, at, TB[c4], d - off_of(c4) > 0xFFFFFFu ? 0xFFFFFFu : d - off_of(c4));
    if (k == EV_KNOB) { put(b, at, 2, 3); put(b, at, 3, (uint32_t)id); put(b, at, 4, (uint32_t)c & 15); }
    else { put(b, at, (uint32_t)k + 1, k ? 1u : 0u); put(b, at, 6, (uint32_t)id); }
}
/* the part as recorded so far into the edit memory: 0 done; 1 refused (the edit memory full: its own question says so;
 * or the part written before gone); 2 the store busy (the next pass may) */
static int flush(void)
{
    uint32_t hb = ua.part ? 2u : 3u, nb = (ua.bits + 7) / 8, l, k = ua.bits / 8 - ua.fbyte;
    uint8_t *b = store_scratch_claim(1);
    if (!b) return 2;
    const uint8_t *p = part_at(ua.slot, ua.part, &l);
    if (ua.fbyte && (!p || l < hb + ua.fbyte)) { store_scratch_claim(0); return 1; }
    memset(b, 0, hb + nb);
    if (p && ua.fbyte) memcpy(b + hb, p + hb, ua.fbyte);
    b[0] = ua.scr;
    b[hb - 2] = (uint8_t)ua.nev;
    b[hb - 1] = (uint8_t)(ua.nev >> 8);
    memcpy(b + hb + ua.fbyte, ua.pend, nb - ua.fbyte);
    int r = ui_edit_put(REC_AUTO, 0, sub(ua.part), 1, b, hb + nb);
    store_scratch_claim(0);
    if (r) return 1;
    if (!ua.wrote)                      /* (its first write: the slot's older parts past this one gone now, so a SAVE meanwhile
                                         * keeps no part of the automation it replaces beside the new one's) */
        for (int s = ua.part + 1; s < 16; s++) { uint32_t q; if (part_at(ua.slot, s, &q)) ui_edit_del(REC_AUTO, 0, sub(s)); }
    ua.wrote = 1;
    ua.pend[0] = ua.pend[k];
    memset(ua.pend + 1, 0, sizeof ua.pend - 1);
    ua.fbyte += k;
    ua.fbits = ua.bits;
    ua.flushed = ui_now();
    return 0;
}
static void rec_stop(void);
/* a recording's end told: what (its slot's #, then its length's) */
static void told(txt_t what)
{
    char d[12];
    ui_fmt(d, sizeof d, "%d:%02d", (int)((ua.last - ua.t0) / 60000u), (int)((ua.last - ua.t0) / 1000u % 60u));
    ui_toast(ui_txt_v(ui_txt_h(what, ua.slot), d));
}
/* an event at t (ms): HOME's held-back press first; a release of what is not down left out (a key's bit its number, a
 * button's its B_*) */
static void ev(uint32_t t, int k, int id, int c)
{
    uint32_t bit = 1u << (id < 27 ? id : id - 27), *st = id < 27 ? &ua.on : &ua.ob;
    if (ua.mode != UA_REC || ua.end) return;
    if (ua.home) { ua.home = 0; ev(ua.home_t, EV_PRESS, 27 + B_HOME, 0); }
    if (k == EV_RELEASE && !(*st & bit)) return;
    if (k == EV_PRESS && id == 27 + B_HOME)             /* (HOME's press: the end's may begin here) */
        ua.mk = (typeof(ua.mk)){1, ua.part, ua.nev, ua.bits, ua.last, ua.ob, ua.on};
    else if (k == EV_KNOB || (id != 27 + B_HOME && id != UI_BLACK[OP6] && id != 27 + B_ARP && id != 27 + B_REC))
        ua.mk.ok = 0;
    if (k != EV_KNOB) *st = k == EV_PRESS ? *st | bit : *st & ~bit;
    if (!ua.nev && !ua.part) ua.t0 = ua.last = t;
    if ((ua.part ? 2u : 3u) + (ua.bits + 44) / 8 > PART_MAX) {   /* (the part full: the next) */
        if (flush()) { rec_stop(); return; }
        if (ua.part == 15) { rec_stop(); told(TX("AUTOMATION # IS FULL: #")); return; }   /* (its 16 parts: it ends there) */
        ua.part++;
        ua.nev = 0;
        ua.bits = ua.fbyte = ua.fbits = 0;
        memset(ua.pend, 0, sizeof ua.pend);
    } else if (ua.bits + 37 > 8 * (ua.fbyte + PEND) && flush()) { rec_stop(); return; }
    uint32_t at = ua.bits - 8 * ua.fbyte;
    code(ua.pend, &at, t - ua.last, k, id, c);
    ua.bits = at + 8 * ua.fbyte;
    ua.last = t;
    ua.nev++;
}
static void knob_ev(uint32_t t, int e, int c)
{
    while (c) { int s = c > 7 ? 7 : c < -8 ? -8 : c; ev(t, EV_KNOB, e, s); c -= s; }
}
/* playing: the next event read (its time from time 0), across parts; none left: have 0. A part cut short (an event
 * running past its end) ends it there: nothing read past a part's last byte */
static uint32_t rd_bit, rd_end;
static uint32_t get(const uint8_t *p, uint32_t n)
{
    if (rd_bit + n > rd_end) { rd_bit = rd_end + 1; return 0; }
    uint32_t v = ui_bits(p, rd_bit, (int)n);
    rd_bit += n;
    return v;
}
static void next(void)
{
    uint32_t l;
    const uint8_t *p = part_at(ua.slot, ua.part, &l);
    while (p && !ua.nev && ua.part < 15) {              /* (this part's events read: the next part's) */
        p = part_at(ua.slot, ++ua.part, &l);
        if (p) { ua.nev = (uint16_t)(p[0] | p[1] << 8); rd_bit = 16; }
    }
    ua.have = p && ua.nev && (rd_bit + 7) / 8 <= l;
    if (!ua.have) return;
    rd_end = 8u * l;
    int k = 0;
    while (k < 4 && get(p, 1)) k++;
    uint32_t d = off_of(k) + get(p, TB[k]);
    ua.nx.t = ua.last += d;
    ua.nx.dt = d != 0;
    if (!get(p, 1)) { ua.nx.k = EV_PRESS; ua.nx.id = (uint8_t)get(p, 6); }
    else if (!get(p, 1)) { ua.nx.k = EV_RELEASE; ua.nx.id = (uint8_t)get(p, 6); }
    else { ua.nx.k = EV_KNOB; ua.nx.id = (uint8_t)get(p, 3); ua.nx.c = (int8_t)((int)(get(p, 4) << 28) >> 28); }
    if (rd_bit > rd_end) { ua.have = 0; return; }
    ua.nev--;
}
static void rd_start(int s, const uint8_t *p)   /* slot s read from its start (p: its part 0), its first event next */
{
    ua.slot = (uint8_t)s; ua.part = 0; ua.nev = (uint16_t)(p[1] | p[2] << 8); rd_bit = 24; ua.last = 0;
    next();
}
/* the event read, applied to what the automation holds (ua.on, ua.ob): 1 a press. A knob, a number past 40 (kept for
 * later) hold nothing */
static int track(void)
{
    uint32_t bit = 1u << (ua.nx.id < 27 ? ua.nx.id : (ua.nx.id - 27) & 31), *st = ua.nx.id < 27 ? &ua.on : &ua.ob;
    if (ua.nx.k == EV_KNOB || ua.nx.id > 40) return 0;
    if (ua.nx.k == EV_RELEASE) { *st &= ~bit; return 0; }
    *st |= bit;
    return 1;
}

/* ---- recording, playing ---- */
/* AUTOMATIONS opened while recording, HOME tapped then OP6 (or HOME, ARP, REC): the end's controls, the last written, out
 * again (the stream back to before HOME's press; a part filled meanwhile keeps them) */
static void rewind_end(void)
{
    uint32_t nb = ua.mk.bits / 8, l;
    if (!ua.mk.ok || ua.mk.part != ua.part) return;
    if (nb < ua.fbyte) {                /* (its byte already in the edit memory: back into the buffer) */
        const uint8_t *p = part_at(ua.slot, ua.part, &l);
        uint32_t at = (ua.part ? 2u : 3u) + nb;
        memset(ua.pend, 0, sizeof ua.pend);
        ua.pend[0] = p && at < l ? p[at] : 0;
        ua.fbyte = nb;
    } else {
        memset(ua.pend + (nb - ua.fbyte) + 1, 0, sizeof ua.pend - (nb - ua.fbyte) - 1);
    }
    ua.pend[nb - ua.fbyte] &= (uint8_t)((1u << (ua.mk.bits & 7)) - 1u);
    ua.bits = ua.mk.bits; ua.nev = ua.mk.nev; ua.last = ua.mk.last; ua.ob = ua.mk.ob; ua.on = ua.mk.on;
    ua.mk.ok = 0;
}
static void rec_stop(void)              /* stopped where it is (the edit memory full): its parts past this one gone */
{
    for (int k = ua.part + 1; k < 16; k++) { uint32_t l; if (part_at(ua.slot, k, &l)) ui_edit_del(REC_AUTO, 0, sub(k)); }
    ua.mode = UA_IDLE;
}
static void rec_end(void)               /* what the automation holds let go at the end (HOME held back not: unrecorded) */
{
    uint32_t t = ui_now();
    if (ua.mode != UA_REC) return;
    ua.home = ua.end = 0;
    if (!ua.nev && !ua.part) {          /* (nothing written: no flush while the end's HOME may still be taken out) */
        ua.mode = UA_IDLE;
        ui_toast_t(TX("NOTHING RECORDED"));
        return;
    }
    for (int id = 40; id >= 0; id--) ev(t, EV_RELEASE, id, 0);
    if (ua.mode != UA_REC) return;
    ua.end = 1;
    flush();
    rec_stop();
    told(TX("AUTOMATION # RECORDED: #"));
}
/* a recording ends: HOME + OP6 (AUTOMATIONS' enter) or, where it is, HOME, ARP, REC (ui.c); the end's controls out again */
void ua_end(void) { if (ua.mode == UA_REC) { rewind_end(); rec_end(); } }
static void play_end(int stopped)       /* stopped: the player's presses since its start masked till let go (what was held
                                         * as it started stays held, as it was all along) */
{
    ua.mode = UA_IDLE;
    ua.mb = stopped ? ua.rb & ~ua.mb : 0u;
    ua.mn = stopped ? ua.rn & ~ua.mn : 0u;
    ui_toast(stopped ? T("AUTOMATION STOPPED") : ui_txt_h(TX("AUTOMATION # PLAYED"), ua.slot));
}
/* REVERT ALL: a recording or a playback ends (its changes dropped with the others) */
void ua_reverted(void)
{
    if (ua.mode == UA_PLAY) play_end(1);
    else if (ua.mode == UA_REC) { ua.mode = UA_IDLE; ui_toast(T("AUTOMATION STOPPED")); }
    ua.gen = 0;
}
void ua_input(plat_input_t *in, const plat_input_t *last)
{
    if (ua.mode && seed_active()) {     /* (a page writing to the FM-1: it ends, its records may change) */
        if (ua.mode == UA_PLAY) play_end(1);
        else { ua.mode = UA_IDLE; ui_toast(T("AUTOMATION STOPPED")); }
        ua.gen = 0;
    }
    if (ua.mode != UA_PLAY) {           /* (masked: the player's presses that stopped a playback, till let go) */
        ua.mb &= in->buttons;
        ua.mn &= in->notes;
        in->buttons &= ~ua.mb;
        in->notes &= ~ua.mn;
        if (ua.mb | ua.mn) { memset(in->knob, 0, sizeof in->knob); memset(in->knob_pre, 0, sizeof in->knob_pre); }
    }
    if (ua.mode == UA_REC) {
        uint32_t t = ui_in_t, bd = in->buttons & ~last->buttons, bu = last->buttons & ~in->buttons;
        uint32_t nd = in->notes & ~last->notes, nu = last->notes & ~in->notes;
        if (ui_blocking()) return;      /* (the FM-1 takes no input meanwhile: a save's progress) */
        if (ua.home && (nd >> UI_BLACK[OP6] & 1) && (in->buttons >> B_HOME & 1)) return;   /* (HOME held + OP6: AUTOMATIONS
                                         * opens and ends it, neither recorded; refused, HOME's press stays held back) */
        for (int e = 0; e < K_COUNT; e++) knob_ev(t, e, in->knob_pre[e]);
        for (int b = 0; b < B_COUNT; b++)
            if (bd >> b & 1) {
                if (b == B_HOME) { ua.home = 1; ua.home_t = t; ua.mk.ok = 0; }   /* (a new HOME: no earlier one is the end's) */
                else ev(t, EV_PRESS, 27 + b, 0);
            }
        for (int n = 0; n < 27; n++) if (nd >> n & 1) ev(t, EV_PRESS, n, 0);
        for (int e = 0; e < K_COUNT; e++) knob_ev(t, e, in->knob[e] - in->knob_pre[e]);
        for (int n = 0; n < 27; n++) if (nu >> n & 1) ev(t, EV_RELEASE, n, 0);
        for (int b = 0; b < B_COUNT; b++) if (bu >> b & 1) ev(t, EV_RELEASE, 27 + b, 0);
        if (ua.mode == UA_REC && !ua.mk.ok && ua.bits > ua.fbits && t - ua.flushed >= 1000u && flush() == 1)
            rec_stop();                 /* (about a second's events written: not while the end's HOME may be taken out) */
        return;
    }
    if (ua.mode == UA_PLAY) {
        uint32_t nb = in->buttons & ~ua.rb, nn = in->notes & ~ua.rn;
        int turned = 0;
        for (int e = 0; e < K_COUNT; e++) turned |= in->knob[e];
        ua.rb = in->buttons;
        ua.rn = in->notes;
        if (ua.fresh) ua.fresh = 0, ua.mb = ua.rb, ua.mn = ua.rn, nb = nn = 0, turned = 0;
        if (nb | nn | turned) {         /* (a control of the player's: it stops, that control doing nothing else) */
            play_end(1);
            in->buttons &= ~ua.mb;
            in->notes &= ~ua.mn;
            memset(in->knob, 0, sizeof in->knob);
            memset(in->knob_pre, 0, sizeof in->knob_pre);
            return;
        } else {
            uint32_t now = ui_now();
            int first = 1, pressed = 0, still = ui_blocking();
            if (still) ua.t0 += now - ua.flushed;   /* (the FM-1 takes no input meanwhile, a save's progress: the
                                         * automation's time stands still, nothing given; it goes on from there after) */
            ua.flushed = now;
            now -= ua.t0;
            if (!still && ua.have && ua.nx.t <= now) ui_in_t = ua.t0 + ua.nx.t;   /* (the pass played at its recorded time:
                                         * the knobs' acceleration counts the gaps as they were) */
            memset(in->knob, 0, sizeof in->knob);
            memset(in->knob_pre, 0, sizeof in->knob_pre);
            while (!still && ua.have && ua.nx.t <= now && (first || !ua.nx.dt)) {   /* (a recorded pass a pass) */
                if (ua.nx.k == EV_KNOB && ua.nx.id < K_COUNT) {   /* (knob 7, a number past 40: kept for later, skipped) */
                    in->knob[ua.nx.id] += ua.nx.c;
                    if (!pressed) in->knob_pre[ua.nx.id] += ua.nx.c;
                }
                pressed |= track();
                first = 0;
                next();
            }
            in->buttons = ua.ob | (ua.rb & ua.mb);
            in->notes = ua.on | (ua.rn & ua.mn);
            if (!ua.have) play_end(0);
            return;
        }
    }
}

/* ---- the screen ---- */
static void counts(void)                /* each slot's screen and length, read again when the store changed */
{
    uint32_t l, gen = store_gen();
    if (ua.gen == gen + 1u) return;
    uint8_t s = ua.slot, p = ua.part, h = ua.have;
    uint16_t n = ua.nev;
    uint32_t b = rd_bit, la = ua.last;
    for (int i = 0; i < 16; i++) {
        const uint8_t *q = part_at(i, 0, &l);
        ua.scrs[i] = (int16_t)(q ? q[0] : -1);
        ua.dur[i] = 0;
        if (!q || ua.mode == UA_PLAY) continue;
        rd_start(i, q);
        while (ua.have) next();
        ua.dur[i] = ua.last;
    }
    ua.slot = s; ua.part = p; ua.have = h; ua.nev = n; rd_bit = b; ua.last = la;
    ua.gen = ua.mode == UA_PLAY ? 0u : gen + 1u;   /* (playing: the lengths not counted, so counted again after) */
}
int ua_holds(int s) { uint32_t l; return part_at(s, 0, &l) != 0; }
static int empty(int s)                 /* slot s holds none: AUTOMATION 03 IS EMPTY */
{
    if (ua_holds(s)) return 0;
    ui_toast(ui_txt_h(TX("AUTOMATION # IS EMPTY"), s));
    return 1;
}
static void play(void)
{
    uint32_t l;
    if (empty(ua.sel)) { ui_refuse(B_PLAY); return; }
    const uint8_t *p = part_at(ua.sel, 0, &l);
    const ui_screen_t *s = ui_round_screen(p[0]);
    rd_start(ua.sel, p);
    ua.t0 = ua.flushed = ui_now(); ua.ob = ua.on = 0; ua.fresh = 1;
    ua.mode = UA_PLAY;
    ui_go_back(s ? s : ua.from);
}
static void rec_go(void)
{
    ua.slot = ua.sel; ua.part = 0; ua.nev = 0; ua.bits = ua.fbyte = ua.fbits = 0; ua.home = ua.end = ua.wrote = 0;
    ua.ob = ua.on = 0;
    ua.mk.ok = 0;
    memset(ua.pend, 0, sizeof ua.pend);
    ua.scr = (uint8_t)ui_round_at(ua.from);
    ua.flushed = ui_now();
    ua.mode = UA_REC;
    ui_go_back(ua.from);
}
static void rec_answer(int a) { if (a == 1) rec_go(); }
static void rec(void)
{
    uint32_t l;
    char d[12];
    counts();
    if (!part_at(ua.sel, 0, &l)) { rec_go(); return; }
    ui_fmt(d, sizeof d, "%d:%02d", (int)(ua.dur[ua.sel] / 60000u), (int)(ua.dur[ua.sel] / 1000u % 60u));
    const char *q[2] = {ui_txt_v(ui_txt_h(TX("AUTOMATION # HOLDS #"), ua.sel), d), T("REPLACE IT?")};
    ui_ask_t(q, 2, TX("REPLACE"), TX("KEEP"), rec_answer);
}
/* HOME, ARP, then a white key (ui.c): slot s played as PLAY plays it (r: recorded into, as REC does, on the screen it is
 * on, a slot holding one asking first), highlighted here (a screen number no screen has: on the one it is on now); 0
 * refused, the combination staying open */
int ua_shortcut(int s, int r)
{
    if (ua.mode) { ui_toast_t(TX("ONE AUTOMATION AT A TIME")); return 0; }
    if (!r && empty(s)) return 0;
    if (ui_screen_now() != &SCR_AUTO) ua.from = ui_screen_now();
    ua.sel = (uint8_t)s;
    if (r) rec(); else play();
    return 1;
}
static void clear_answer(int a)
{
    if (a != 1) return;
    for (int k = 15; k >= 0; k--) { uint32_t l; if (part_at(ua.sel, k, &l)) ui_edit_del(REC_AUTO, 0, (uint8_t)(ua.sel * 16 + k)); }
    ui_toast(ui_txt_h(TX("AUTOMATION # CLEARED"), ua.sel));
}
static void revert_go(void) { for (int s = 0; s < 256; s++) ui_edit_revert(REC_AUTO, 0, (uint8_t)s); }
static int unsaved(void) { for (int s = 0; s < 256; s++) if (ui_edit_unsaved(REC_AUTO, 0, (uint8_t)s)) return 1; return 0; }
/* COMPACT (specs/ui/screens/automations.md#compacting): the slot's events again, each pause while nothing of the
 * automation's is held (from an event after which none of its keys and buttons is down, to the next) longer than lim
 * (ms) made lim; the time before the first event and the events of one pass (0 ms apart) as they were. Laid out in
 * parts by the file's rule, as ev() lays them, each put from b (NULL: measured only), the parts past them deleted. The
 * edit memory it takes (0: refused); ua.last its new length, *cut whether a pause was shortened. The old parts are read
 * as the new ones are put: a part takes at least the old one's events from its own first on (none takes more bits),
 * so the reading is past each old part before its place is written */
static uint32_t squash(uint32_t lim, uint8_t *b, int *cut)
{
    uint32_t l, at = 0, was = 0, t = 0, need = 0, hb = 3;
    int part = 0, n = 0, first = 1;
    const uint8_t *p = part_at(ua.sel, 0, &l);
    uint8_t scr = p[0];
    *cut = 0;
    ua.on = ua.ob = 0;
    rd_start(ua.sel, p);
    for (;;) {
        if (!ua.have || hb + (at + 44) / 8 > PART_MAX) {   /* (the part closes: none left, or the next might not fit) */
            uint32_t len = hb + (at + 7) / 8;
            need += ((len + 3) & ~3u) + 8;
            if (b) {
                b[0] = scr;
                b[hb - 2] = (uint8_t)n;
                b[hb - 1] = (uint8_t)(n >> 8);
                if (ui_edit_put(REC_AUTO, 0, sub(part), 1, b, len)) return 0;
                memset(b, 0, PART_MAX);
            }
            part++;
            hb = 2; at = 0; n = 0;
            if (!ua.have) break;
        }
        uint32_t d = ua.nx.t - was;
        was = ua.nx.t;
        if (!first && !(ua.on | ua.ob) && d > lim) { d = lim; *cut = 1; }
        first = 0;
        code(b ? b + hb : 0, &at, d, ua.nx.k, ua.nx.id, ua.nx.c);
        t += d;
        n++;
        track();
        next();
    }
    for (; part < 16; part++)
        if (part_at(ua.sel, part, &l)) { need += 8; if (b && ui_edit_del(REC_AUTO, 0, sub(part))) return 0; }
    ua.last = t;
    return need;
}
static void compact_pick(int id)        /* its list's 1 S, 0.5 S, 0.3 S */
{
    static const uint16_t LIM[3] = {1000, 500, 300};
    int cut;
    uint32_t need = squash(LIM[id - 1], 0, &cut);
    if (!cut) { ui_toast_t(TX("NOTHING TO COMPACT")); return; }
    if (ui_edit_reserve(need)) return;  /* (MEMORY FOR EDITS IS FULL asked: the slot as it was) */
    memset(ui_scratch, 0, sizeof ui_scratch);
    store_edit_hold(1);                 /* (the old parts stay where they lie while the new ones are put) */
    need = squash(LIM[id - 1], ui_scratch, &cut);
    store_edit_hold(0);
    ua.t0 = 0;
    if (need) told(TX("AUTOMATION # COMPACTED: #"));
}
enum { M_REC = 1, M_PLAY, M_CLEAR, M_REVERT, M_COMPACT };
static void menu_pick(int id)
{
    uint32_t l;
    if (id == M_REC) rec();
    else if (id == M_PLAY) play();
    else if (id == M_COMPACT && !empty(ua.sel)) {
        const ui_mitem_t m[3] = {{TX("1 S"), 1, 0, 0}, {TX("0.5 S"), 2, 0, 0}, {TX("0.3 S"), 3, 0, 0}};
        ui_menu("auto-c", ui_txt_h(TX("COMPACT AUTOMATION #"), ua.sel), m, 3, compact_pick, B_SEL);   /* (it copies its title) */
    }
    else if (id == M_REVERT) ui_ask_revert(T("REVERT AUTOMATIONS TO SAVED?"), 0, revert_go);
    else if (id == M_CLEAR && part_at(ua.sel, 0, &l)) {
        const char *q[1] = {ui_txt_h(TX("CLEAR AUTOMATION #?"), ua.sel)};
        ui_ask_t(q, 1, TX("CLEAR"), TX("KEEP"), clear_answer);
    }
}
static int tap(int b)
{
    if (b != B_SEL) return 0;
    ui_mitem_t m[5] = {{TX("RECORD"), M_REC, 0, 0}, {TX("PLAY"), M_PLAY, 0, 0}, {TX("COMPACT"), M_COMPACT, 0, 0},
                       {TX("CLEAR"), M_CLEAR, 0, 0}, {unsaved() ? TX("REVERT") : 0, M_REVERT, 0, 0}};
    ui_menu("auto", T("AUTOMATIONS"), m, 5, menu_pick, B_SEL);
    return 1;
}
static int play_down(void) { return 1; }               /* (its own: PLAY plays at its release, nothing held then) */
static void play_up(int used) { if (!used) play(); }
static void knob(int e, int s, int coarse)
{
    (void)coarse;
    if (e == K_SELECT || e == K_ALG) { ua.sel = (uint8_t)ui_clamp(ua.sel + s, 0, 15); ui_dirty(); }
}
static int key(int n, int down)         /* a white key picks its slot; the keys play nothing here */
{
    int w = ui_white_index(n);
    if (down && w >= 0) { ua.sel = (uint8_t)w; ui_dirty(); }
    return 1;
}
static void enter(const ui_screen_t *from)
{
    ua_end();                           /* (HOME + OP6 while recording: it ends) */
    if (from && from != &SCR_AUTO) ua.from = from;   /* (itself: a seeding's end shows it again) */
    if (!ua.from) ua.from = ui_round_screen(0);
    ua.gen = 0;
}
static int lit(int b) { return b == B_REC || b == B_PLAY || b == B_SEL ? LV_DIM : LV_NONE; }
static void draw(void)
{
    static const txt_t H[] = {TX("REC"), TX(" RECORD  "), TX("PLAY"), TX(" PLAY  "), TX("SEL"), TX(" MENU"), 0};
    char b[12];
    counts();
    for (int i = 0; i < 16; i++) {
        int x = (i & 3) * 60, y = BODY_Y + 6 + (i >> 2) * 46, on = i == ua.sel, s = ua.scrs[i];
        const ui_screen_t *sc = s >= 0 ? ui_round_screen(s) : 0;
        if (on) g_box(x, y - 2, 60, 44, C_WHITE);
        g_text(x + 4, y, &FONT_S, ui_hex2(i), on ? C_WHITE : C_CYAN);
        g_textn(x + 4, y + 16, &FONT_T, s < 0 ? T("EMPTY") : sc ? T(sc->name) : "?", 9, s < 0 ? C_DGREY : C_LGREY);
        if (s < 0) continue;
        ui_fmt(b, sizeof b, "%d:%02d", (int)(ua.dur[i] / 60000u), (int)(ua.dur[i] / 1000u % 60u));
        g_text(x + 4, y + 28, &FONT_T, b, C_LGREY);
    }
    ui_hints(2, BODY_Y + BODY_H - 14, H);
}
const ui_screen_t SCR_AUTO = {
    .name = TX("AUTOMATIONS"), .group = G_PLAY, .does = BIT(B_SEL) | BIT(B_REC),
    .enter = enter, .draw = draw, .knob = knob, .tap = tap, .key = key, .lit = lit, .rec = rec,
    .play_down = play_down, .play_up = play_up,
};

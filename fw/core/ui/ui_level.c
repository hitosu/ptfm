/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* AUTO LEVEL (specs/ui/screens/instruments.md#auto-level, specs/ui/screens/settings.md TOOLS): instruments
 * measured as their LEVEL is defined (specs/files/instruments.md, LEVEL; the player's player_measure) and their
 * LEVEL set: -20 dB less the loudest 300 ms, at most 30 dB either way, its peak kept under -6 dB, in half-dB
 * steps. A silent one keeps its LEVEL. An instrument a pass of the main loop under a progress bar (~50 ms each
 * on the FM-1); all of a run one edit. TOOLS' run measures the user area's cartridges; a built-in one is measured
 * from its own menu. */
#include <string.h>
#include "plat.h"
#include "ui_core.h"
#include "ui_priv.h"
#include "ui_data.h"
#include "ed_inst.h"
#include "ed_pat.h"
#include "../play/player.h"
#include "../play/rec.h"
#include "../store/store.h"

#define AL_SLOTS 112                    /* a slot's bytes at most (scr_instr.c's MAXSLOT) */

static struct {
    uint8_t on, all;                    /* running; every cartridge of the user area (else one cartridge, or one) */
    int16_t cart, slot, one;            /* where it is; one: a single slot (-1: the cartridge's every slot) */
    int total, done, set;               /* instruments to measure, measured, LEVELs set */
    int8_t last;                        /* the last LEVEL set (a single one's toast) */
    uint32_t t0;
    void (*after)(int set);
} al;

/* 10 x log10(x) in Q16 dB (x >= 1): log2 by squaring, times 10 log10(2) */
static int32_t db10_q16(uint64_t x)
{
    int n = 63;
    while (n > 0 && !(x >> n)) n--;
    uint32_t y = (uint32_t)(n >= 30 ? x >> (n - 30) : x << (30 - n));     /* [2^30, 2^31) */
    int32_t f = 0;
    for (int i = 0; i < 16; i++) {
        y = (uint32_t)(((uint64_t)y * y) >> 30);
        f <<= 1;
        if (y >= (1u << 31)) { y >>= 1; f |= 1; }
    }
    int64_t l2 = ((int64_t)n << 16) | f;                /* log2(x), Q16 */
    return (int32_t)((l2 * 197283) >> 16);             /* x 3.0103 (10 log10 2) */
}

/* instrument cart:slot's new LEVEL (half dB): 1 measured, 0 silent or nothing to measure */
int ui_level_measure(uint8_t cart, int slot, int8_t *level)
{
    pl_inst_t in = {cart, (uint8_t)slot};
    uint32_t ms;
    int32_t pk;
    if (!player_measure(in, ui_scratch, UI_SCRATCH, plat_lock, plat_unlock, &ms, &pk) || !ms) return 0;
    /* its loudness: 10 log10(ms) - 10 log10(2^30) (full scale: 16 bits); under -90 dB: silent */
    int32_t lv = db10_q16(ms) - 30 * 197283;
    if (lv < -90 * 65536) return 0;
    int32_t db = -20 * 65536 - lv;
    if (db > 30 * 65536) db = 30 * 65536;
    if (db < -30 * 65536) db = -30 * 65536;
    if (pk > 0) {                                       /* the peak under -6 dB: 20 log10(pk / 2^15) */
        int32_t pkdb = 2 * (db10_q16((uint64_t)pk) - 15 * 197283), room = -6 * 65536 - pkdb;
        if (db > room) db = room;
    }
    int32_t h = db >= 0 ? (db * 2 + 32768) >> 16 : -((-db * 2 + 32768) >> 16);   /* half-dB steps, rounded */
    *level = (int8_t)ui_clamp(h, -128, 127);
    return 1;
}

static int set_level(uint8_t cart, int slot, int8_t lv)
{
    uint8_t s[AL_SLOTS];
    int kind = ins_kind(cart);
    if (!ins_slot_bytes(cart, slot, s)) return -1;
    ins_prm_t p = {TX("LEVEL"), 0, (uint16_t)ins_level_pos(kind), 8, PK_LEVEL, -128, 127, 0};
    if (ins_get(s, &p) == lv) return 0;
    ins_set(s, &p, lv);
    return ins_put_slot(cart, slot, s);
}

static int user_cart(uint8_t c) { return ins_kind(c) && !ins_builtin(c); }
static int next_cart(int c)                             /* the user area's next cartridge after c, -1 none */
{
    for (int k = c + 1; k < 256; k++) if (user_cart((uint8_t)k)) return k;
    return -1;
}
/* the next instrument from (al.cart, al.slot) on, al.slot moved to it: 1 found */
static int next_inst(void)
{
    uint8_t s[AL_SLOTS];
    while (al.cart >= 0) {
        int n = al.one >= 0 ? al.one + 1 : ui_cart_slots((uint8_t)al.cart);
        for (; al.slot < n; al.slot++) if (ins_slot_bytes((uint8_t)al.cart, al.slot, s)) return 1;
        if (!al.all) return 0;
        al.cart = (int16_t)next_cart(al.cart);
        al.slot = 0;
    }
    return 0;
}
static int count(void)
{
    int16_t c = al.cart, sl = al.slot;
    int n = 0;
    while (next_inst()) { n++; al.slot++; }
    al.cart = c; al.slot = sl;
    return n;
}
static int pct(void) { return al.total ? al.done * 100 / al.total : 100; }

static void start(const char *text)
{
    al.on = 1;
    al.t0 = ui_now();
    ui_progress_pct(text, pct);
}
/* one instrument: MEASURING ITS NAME, then LEVEL SET TO -1.5 DB */
void ui_auto_level(uint8_t cart, int slot, void (*after)(int set))
{
    char nm[16], t[32];
    if (!ui_inst_name((uint16_t)(cart << 8 | slot), nm)) return;
    memset(&al, 0, sizeof al);
    al.cart = cart; al.slot = (int16_t)slot; al.one = (int16_t)slot;
    al.after = after;
    al.total = 1;
    ui_fmt(t, sizeof t, "%s", ui_txt_v(T("MEASURING #"), nm));
    start(t);
}
/* every instrument of a cartridge (cart >= 0) or of the user area (-1), after a question (it changes every LEVEL
 * at once, and nothing takes a step back): MEASURING 32 SOUNDS, MEASURING EVERY SOUND, then LEVELS SET */
static void all_set(int cart, void (*after)(int set))
{
    memset(&al, 0, sizeof al);
    al.one = -1;
    al.all = cart < 0;
    al.cart = (int16_t)(cart < 0 ? next_cart(-1) : cart);
    al.after = after;
    al.total = count();
}
static int ask_cart;
static void (*ask_after)(int set);
static void all_go(int a)
{
    char t[32];
    if (a != 1) return;
    all_set(ask_cart, ask_after);
    if (ask_cart >= 0)
        ui_fmt(t, sizeof t, "%s", ui_txt_v(al.total == 1 ? T("MEASURING # SOUND") : T("MEASURING # SOUNDS"),
                                           ui_dec(al.total)));
    else ui_fmt(t, sizeof t, "%s", T("MEASURING EVERY SOUND"));
    start(t);
}
void ui_auto_level_all(int cart, void (*after)(int set))
{
    static char l0[32];
    const char *l[2];
    all_set(cart, after);
    ask_cart = cart;
    ask_after = after;
    ui_fmt(l0, sizeof l0, al.total == 1 ? T("AUTO LEVEL %d SOUND?") : T("AUTO LEVEL %d SOUNDS?"), al.total);
    l[0] = l0;
    l[1] = T("EACH ONE'S LEVEL SET AGAIN");
    ui_ask(l, 2, T("LEVEL"), T("KEEP"), all_go);
}

/* a pass of the main loop: the next instrument (after the bar has been drawn) */
void ui_level_poll(void)
{
    if (!al.on || ui_now() - al.t0 < 40) return;
    if (next_inst()) {
        int8_t lv;
        if (ui_level_measure((uint8_t)al.cart, al.slot, &lv) && set_level((uint8_t)al.cart, al.slot, lv) == 0) {
            al.set++;
            al.last = lv;
        }
        al.done++;
        al.slot++;
        return;
    }
    al.on = 0;
    ui_progress_close();
    if (al.one >= 0) {
        char v[16];
        int h = al.last;                                /* (as the LEVEL's value is written: -1.5 DB, 0 DB, +3 DB) */
        ui_fmt(v, sizeof v, "%s%d%s DB", h > 0 ? "+" : h < 0 ? "-" : "", (h < 0 ? -h : h) / 2, (h & 1) ? ".5" : "");
        ui_toast(al.set ? ui_txt_v(T("LEVEL SET TO #"), v) : T("NO SOUND TO MEASURE"));
    } else ui_toast(T("LEVELS SET"));
    if (al.after) al.after(al.set);
}

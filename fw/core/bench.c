/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The debug build's bench (specs/debug.md, The bench): on the page's word (SysEx 32 BENCH) the FM-1 measures, a
 * piece at a time in the main loop, and sends each result as a record (33 BENCH RECORD):
 *   THE CORE  the platform's own (plat_bench_core): loops, arithmetic, RAM, the flash behind the code cache, code
 *             run from RAM against the same from the flash;
 *   ALONE     a voice of each engine, the Juno-106's noise generator and each effect, a block at a time with the
 *             interrupts off: the first run with the code cache emptied (cold), then warm runs.
 * Only what needs the firmware's insides: the sound running (songs, the keys' voices, effects, DJ) is the debug
 * page's autotests' to measure, through the panel (MIRROR) and FIGURES.
 * Before: what plays stops, every effect off (refused with ARP on, the flash busy or DJ on); after: the sound off,
 * the effects as they were. Only with PTFM_DEBUG. */
#ifdef PTFM_DEBUG
#include <string.h>
#include "dbg.h"
#include "plat.h"
#include "play/player.h"
#include "play/rec.h"
#include "play/dj.h"
#include "fx/fx.h"
#include "eng/juno/juno.h"                  /* juno_block: the noise generator alone */
#include "eng/chip/chip.h"                  /* chip_unpack: which CHIP instruments to time */
#include "store/store.h"
#include "store/seed.h"
#include "ui/ui_core.h"

#define WARM 8u                         /* an ALONE piece's warm runs, after its cold one */
#define BLOCK 64u                       /* a voice's block, frames (the player's and the effects') */
#define NENG 14u                        /* ALONE's engine pieces at most */

/* the pieces, in order: THE CORE's, ALONE's engines, the noise generator and the effects (piece_at) */
enum { BK_CORE = 1, BK_ENGINE, BK_NOISE, BK_EFFECT };
typedef struct { uint8_t kind, a, b, c; } piece_t;
static const char *const EFFECT_NAME[FXB_N] = {"REVERB", "DELAY", "CHORUS", "FILTER", "DRIVE", "LO-FI", "GATE",
                                                "LIMITER", "PUNCH-INS, IDLE", "DJ FILTER", "DJ FILTER, 1 CH",
                                                "SETTINGS, GET + PUT"};

static struct {
    uint8_t want, on, stop;             /* the page asked for a run; running; the page asked it to stop */
    uint8_t voices0;                    /* VOICES (the START record says it) */
    uint8_t ncore, neng;                /* the plan's parts */
    uint8_t eng[NENG][3];               /* ALONE's engine pieces: cartridge, slot, note */
    uint8_t samp_base;                  /* the sampler piece's base note (its second an octave up) */
    uint32_t at, npiece;                /* the piece under way, of how many */
    uint32_t began;
    uint32_t halves0;                   /* the audio context's halves at the start: the effects off 2 halves on */
} bn;
/* the bench runs: the platform hands no block to core 2 meanwhile and has it done first, as the pieces run the
 * effects' code on core 1 (specs/second-core.md#who-owns-what) */
volatile uint8_t bench_running;

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

/* ---- records: 33 BENCH RECORD (dbg_send packs and sends them; one a main loop pass) ---- */
enum { BR_START, BR_CORE, BR_ALONE, BR_STEP, BR_END, BR_NEXT };   /* (STEP and NEXT were the steps': gone) */
#define REC_MAX 56
static void head(uint8_t *rec, int kind, uint32_t n, const char *name)
{
    memset(rec, 0, REC_MAX);
    rec[0] = DBG_BENCH_VERSION;
    rec[1] = (uint8_t)kind;
    put16(rec + 2, n);
    put16(rec + 4, bn.npiece);
    strncpy((char *)rec + 6, name, 20);
}
static void send_measure(int kind, const dbg_bench_t *r)
{
    uint8_t rec[REC_MAX];
    head(rec, kind, bn.at + 1u, r->name);
    rec[26] = r->unit;
    rec[27] = r->runs;
    put32(rec + 28, r->ops);
    put32(rec + 32, r->first);
    put32(rec + 36, r->min);
    put32(rec + 40, r->max);
    put32(rec + 44, r->sum);
    dbg_send(0x33, rec, 48);
}
static void send_end(int status)
{
    static const char *const WHY[5] = {"DONE", "STOPPED", "ARP IS ON", "BUSY: THE FLASH", "DJ ON: PLAY A SONG"};
    uint8_t rec[REC_MAX];
    head(rec, BR_END, bn.npiece, WHY[status < 5 ? status : 1]);
    rec[26] = (uint8_t)status;
    put32(rec + 28, plat_ms() - bn.began);
    dbg_send(0x33, rec, 32);
}

/* ---- the plan ---- */
static int juno_mode(const uint8_t *slot)      /* its render's path: 0 BOOST, 1 FLAT, 2 and 3 the HPF's */
{
    uint8_t p[18];
    rec_juno_patch(slot, p);
    return 3 - ((p[17] >> 3) & 3);
}
static void add_eng(int cart, int slot, int note)
{
    if (bn.neng < NENG) {
        bn.eng[bn.neng][0] = (uint8_t)cart;
        bn.eng[bn.neng][1] = (uint8_t)slot;
        bn.eng[bn.neng][2] = (uint8_t)note;
        bn.neng++;
    }
}

static void plan(void)
{
    rec_cart_t c;
    int i, num, ndx = 0, modes = 0, juno = 0, tb = 0, chip = 0;
    uint32_t algs = 0;
    while (plat_bench_core(bn.ncore, 0) && bn.ncore < 60)
        bn.ncore++;
    for (num = 0xF0; num <= 0xFF; num++) {     /* the built-in cartridges: DX7s, the Juno-106, the TB-303 */
        if (rec_cart((uint8_t)num, &c) || !rec_slot(&c, 0))
            continue;
        if (c.kind == REC_DX7) {
            for (i = 0; i < c.nslots && ndx < 3; i++) {   /* three voices of different algorithms */
                const uint8_t *s = rec_slot(&c, (uint8_t)i);
                uint8_t vc[156];
                if (!s)
                    continue;
                rec_dx7_vced(s, vc);
                if (vc[134] < 32u && !(algs >> vc[134] & 1u)) {
                    algs |= 1u << vc[134];
                    add_eng(num, i, 60);
                    ndx++;
                }
            }
        } else if (c.kind == REC_JUNO && !juno) {
            juno = 1;
            for (i = 0; i < c.nslots; i++) {   /* a patch for each of the render's paths */
                const uint8_t *s = rec_slot(&c, (uint8_t)i);
                int m = s ? juno_mode(s) : -1;
                if (m >= 0 && !(modes >> m & 1)) {
                    modes |= 1 << m;
                    add_eng(num, i, 60);
                }
            }
        } else if (c.kind == REC_TB303 && !tb) {
            tb = 1;
            add_eng(num, 0, 45);
        } else if (c.kind == REC_CHIP && !chip) {
            /* the first PULSE at C-4, TRIANGLE at A-4, WAVE at C-7 (a step at every one of its 32 a period: near
             * the ceiling) and NOISE at its fastest RATE (every sample averaged) */
            static const uint8_t WANT[4][2] = {{CHIP_PULSE, 60}, {CHIP_TRIANGLE, 69}, {CHIP_WAVE, 96}, {CHIP_NOISE, 60}};
            chip = 1;
            for (int w = 0; w < 4; w++)
                for (i = 0; i < c.nslots; i++) {
                    const uint8_t *s = rec_slot(&c, (uint8_t)i);
                    chip_patch_t cp;
                    if (!s)
                        continue;
                    chip_unpack(s, &cp);
                    if (cp.osc == WANT[w][0] && (cp.osc != CHIP_NOISE || cp.n_rate == 0)) {
                        add_eng(num, i, WANT[w][1]);
                        break;
                    }
                }
        }
    }
    {                                          /* a sample that sounds while held: the first looped instrument of
                                                * any sampler, else the one with the longest sample */
        int best_c = -1, best_s = 0, looped = 0;
        uint32_t longest = 0;
        for (num = 0; num < 0xF0 && !looped; num++) {
            if (rec_cart((uint8_t)num, &c) || c.kind != REC_SAMPLER)
                continue;
            for (i = 0; i < c.nslots && !looped; i++) {
                const uint8_t *s = rec_slot(&c, (uint8_t)i);
                rec_sinst_t si;
                rec_sample_t sm;
                if (!s)
                    continue;
                rec_sampler_inst(s, &si);
                if (rec_sample(&c, si.sample, &sm))
                    continue;
                if (si.loop != RL_OFF && si.loop_end > si.loop_start + 1u)
                    looped = 1;
                if (looped || sm.length > longest) {
                    longest = sm.length;
                    best_c = num;
                    best_s = i;
                }
            }
        }
        if (best_c >= 0 && !rec_cart((uint8_t)best_c, &c)) {
            rec_sinst_t si;
            rec_sampler_inst(rec_slot(&c, (uint8_t)best_s), &si);
            bn.samp_base = si.base;
            add_eng(best_c, best_s, si.base);
            add_eng(best_c, best_s, si.base + 12);
        }
    }
    bn.npiece = bn.ncore + bn.neng + 1u + FXB_N;
}
static piece_t piece_at(uint32_t i)
{
    piece_t p = {0, 0, 0, 0};
    if (i < bn.ncore)
        return (piece_t){BK_CORE, (uint8_t)i, 0, 0};
    i -= bn.ncore;
    if (i < bn.neng)
        return (piece_t){BK_ENGINE, bn.eng[i][0], bn.eng[i][1], bn.eng[i][2]};
    i -= bn.neng;
    if (i < 1u)
        return (piece_t){BK_NOISE, 0, 0, 0};
    i -= 1u;
    if (i < FXB_N)
        return (piece_t){BK_EFFECT, (uint8_t)i, 0, 0};
    return p;
}

/* ---- ALONE: a voice, an effect; cold, then warm ---- */
static void time_run(dbg_bench_t *r, uint32_t k, uint32_t dt)
{
    if (!k) {
        r->first = dt;
        return;
    }
    if (k == 1 || dt < r->min)
        r->min = dt;
    if (dt > r->max)
        r->max = dt;
    r->sum += dt;
}
static void alone_engine(const piece_t *p, dbg_bench_t *r)
{
    static const char *const MODE[4] = {"BOOST", "FLAT", "HPF 2", "HPF 3"};
    uint64_t mem[1024 / 8];             /* the voice (player_bench_size: ~520 B) */
    int32_t buf[BLOCK];
    pl_inst_t in = {p->a, p->b};
    rec_cart_t c;
    int e = 0;
    uint32_t f;
    if (player_bench_size() <= sizeof mem) {
        f = plat_lock();
        e = player_bench_start(mem, in, p->c);
        plat_unlock(f);
    }
    if (rec_cart(p->a, &c))
        c.kind = 0;
    if (c.kind == REC_DX7) {
        uint8_t vc[156];
        rec_dx7_vced(rec_slot(&c, p->b), vc);
        ui_fmt(r->name, sizeof r->name, "DX7 %02X:%02X ALG %02d", p->a, p->b, vc[134] + 1);
    } else if (c.kind == REC_JUNO)
        ui_fmt(r->name, sizeof r->name, "JUNO %02X:%02X %s", p->a, p->b, MODE[juno_mode(rec_slot(&c, p->b))]);
    else if (c.kind == REC_TB303)
        ui_fmt(r->name, sizeof r->name, "TB-303 %02X:%02X", p->a, p->b);
    else if (c.kind == REC_CHIP) {
        static const char *const OSC[5] = {"PULSE", "TRI", "WAVE", "BUZZ", "NOISE"};   /* (20 characters at most) */
        chip_patch_t cp;
        chip_unpack(rec_slot(&c, p->b), &cp);
        ui_fmt(r->name, sizeof r->name, "CHIP %02X:%02X %s %d", p->a, p->b, OSC[cp.osc], p->c);
    } else
        ui_fmt(r->name, sizeof r->name, "SAMPLE %02X:%02X %s", p->a, p->b, bn.samp_base == p->c ? "BASE" : "+12");
    r->unit = DBU_BLOCK;
    r->ops = 1;
    if (!e)
        return;                         /* (no sound: its audio is not there) */
    for (uint32_t k = 0; k < 3u; k++) { /* (its note's first blocks, not timed: an attack's start is not the rest) */
        memset(buf, 0, sizeof buf);
        f = plat_lock();
        player_bench_render(mem, buf, 0);
        plat_unlock(f);
    }
    for (uint32_t k = 0; k <= WARM; k++) {
        memset(buf, 0, sizeof buf);
        if (!k)
            plat_cache_cold();
        f = plat_lock();
        uint32_t t0 = plat_ticks();
        player_bench_render(mem, buf, k % 3u == 2u);   /* (the Juno-106's loop in every third, as ~236 Hz is) */
        uint32_t dt = plat_ticks() - t0;
        plat_unlock(f);
        time_run(r, k, dt);
    }
    r->runs = WARM;
}
static void alone_noise(dbg_bench_t *r)  /* the Juno-106's noise generator: a block for all its voices */
{
    ui_fmt(r->name, sizeof r->name, "JUNO NOISE");
    r->unit = DBU_BLOCK;
    r->ops = 1;
    for (uint32_t k = 0; k <= WARM; k++) {
        if (!k)
            plat_cache_cold();
        uint32_t f = plat_lock(), t0 = plat_ticks();
        juno_block(0);
        uint32_t dt = plat_ticks() - t0;
        plat_unlock(f);
        time_run(r, k, dt);
    }
    r->runs = WARM;
}
static void alone_effect(const piece_t *p, dbg_bench_t *r)
{
    int32_t l[BLOCK], rr[BLOCK];
    uint32_t seed = 0x1234567u;
    ui_fmt(r->name, sizeof r->name, "%s", EFFECT_NAME[p->a]);
    r->unit = DBU_BLOCK;
    r->ops = 1;
    for (uint32_t k = 0; k < 3u + 1u + WARM; k++) {   /* 3 not timed (a filter's first glide), the cold one, warm */
        for (uint32_t i = 0; i < BLOCK; i++) {   /* noise, about -12 dB */
            seed = seed * 1664525u + 1013904223u;
            l[i] = ((int32_t)(seed >> 16) - 32768) / 4;
            rr[i] = -l[i] / 2;
        }
        if (k == 3u)
            plat_cache_cold();
        uint32_t f = plat_lock(), t0 = plat_ticks();
        fx_bench(p->a, l, rr);
        uint32_t dt = plat_ticks() - t0;
        plat_unlock(f);
        if (k >= 3u)
            time_run(r, k - 3u, dt);
    }
    r->runs = WARM;
}

/* ---- the run ---- */
static void quiet(void)                 /* what plays stops, the keys and every voice off */
{
    player_stop();
    player_keys_off();
    player_all_sound_off();
}

int bench_sysex(int cmd)
{
    if (cmd == 0 && bn.on)
        bn.stop = 1;
    else if (cmd == 1 && !bn.on)
        bn.want = 1;                    /* (bench_poll starts it: its first record needs the out buffer) */
    return 1;
}

static void start(void)
{
    player_state_t st;
    uint8_t rec[REC_MAX];
    player_state(&st, 0);
    memset(&bn, 0, sizeof bn);
    bn.began = plat_ms();
    if (st.arp_on || seed_active() || store_saving()) {   /* (the keys would feed the arpeggiator; the flash busy) */
        send_end(st.arp_on ? 2 : 3);
        return;
    }
    if (dj_live()) {                    /* (DJ's filters and mix would run under the pieces) */
        send_end(4);
        return;
    }
    bn.on = 1;
    bench_running = 1;
    bn.halves0 = st.halves;
    bn.voices0 = st.voice_count;
    quiet();
    for (int e = 0; e < FX_SENDS; e++)  /* every effect off: the audio context leaves their state to the bench */
        player_fx_cmd(0xAF, (uint8_t)e);
    plan();
    head(rec, BR_START, 0, "BENCH");
#ifdef PTFM_VERSION
    strncpy((char *)rec + 26, PTFM_VERSION, 23);
#endif
    rec[50] = bn.voices0;
    put32(rec + 52, plat_ms());
    dbg_send(0x33, rec, 56);
    ui_toast(T("BENCH: HANDS OFF"));
}

void bench_poll(void)
{
    if (bn.want) {
        start();
        return;
    }
    if (!bn.on)
        return;
    if (bn.stop || bn.at >= bn.npiece) {   /* the end: the sound off, the effects back */
        quiet();
        player_fx_reload();
        ui_toast(bn.stop ? T("BENCH STOPPED") : T("BENCH DONE"));
        send_end(bn.stop ? 1 : 0);
        bn.on = 0;
        bench_running = 0;
        return;
    }
    if (bn.at == 0) {                   /* (the effects off reach the audio context in its next half) */
        player_state_t st;
        player_state(&st, 0);
        if (st.halves - bn.halves0 < 2u)
            return;
    }
    piece_t p = piece_at(bn.at);
    dbg_bench_t r;
    memset(&r, 0, sizeof r);
    if (p.kind == BK_CORE)
        plat_bench_core(p.a, &r);
    else if (p.kind == BK_ENGINE)
        alone_engine(&p, &r);
    else if (p.kind == BK_NOISE)
        alone_noise(&r);
    else
        alone_effect(&p, &r);
    send_measure(p.kind == BK_CORE ? BR_CORE : BR_ALONE, &r);
    bn.at++;
}
#endif

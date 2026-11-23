/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* CHIP: a voice of a virtual 8-bit sound chip (chip.h; specs/chip.md). Integers only; every number here has its
 * twin in playground/dsp-chip.js, which must stay equal to it sample for sample (fw/tests/run_chip.sh). */
#include "chip.h"
#include "chip_tables.h"                 /* fw/tools/gen_chip.py */
#ifndef PLAT_COLD
#define PLAT_COLD                        /* (a note's or a screen's work: built for size on the FM-1, core/plat.h) */
#endif

/* (inlined at every call, so that a second caller does not change the render's code) */
#define CHIP_INL static inline __attribute__((always_inline))

static uint32_t lim(uint32_t v, uint32_t top) { return v > top ? top : v; }
static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

uint32_t chip_bits(const uint8_t *b, uint32_t pos, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++, pos++)
        v |= (uint32_t)(b[pos >> 3] >> (pos & 7) & 1u) << i;
    return v;
}

PLAT_COLD void chip_unpack(const uint8_t *s, chip_patch_t *p)
{
    p->osc = (uint8_t)lim(chip_bits(s, CHIP_B_OSC, 3), CHIP_NOISE);
    p->duty = (uint8_t)chip_bits(s, CHIP_B_DUTY, 2);
    p->bz_shape = (uint8_t)lim(chip_bits(s, CHIP_B_BZ_SHAPE, 2), 2);
    p->bz_steps = (uint8_t)chip_bits(s, CHIP_B_BZ_STEPS, 1);
    p->bz_gate = (uint8_t)chip_bits(s, CHIP_B_BZ_GATE, 1);
    p->n_mix = (uint8_t)chip_bits(s, CHIP_B_N_MIX, 1);
    p->n_short = (uint8_t)chip_bits(s, CHIP_B_N_SHORT, 1);
    p->n_rate = (uint8_t)chip_bits(s, CHIP_B_N_RATE, 4);
    p->n_key = (uint8_t)chip_bits(s, CHIP_B_N_KEY, 1);
    p->clock = (uint8_t)lim(chip_bits(s, CHIP_B_CLOCK, 3), CHIP_EXACT);
    p->curve = (uint8_t)chip_bits(s, CHIP_B_CURVE, 2);
    p->filter = (uint8_t)chip_bits(s, CHIP_B_FILTER, 2);
    p->fps50 = (uint8_t)chip_bits(s, CHIP_B_FPS, 1);
    p->sweep = (uint8_t)lim(chip_bits(s, CHIP_B_SWEEP, 2), CHIP_SWEEP_UP);
    p->sw_shift = (uint8_t)chip_bits(s, CHIP_B_SHIFT, 3);
    p->sw_pace = (uint8_t)chip_bits(s, CHIP_B_PACE, 3);
    p->vol = (uint8_t)chip_bits(s, CHIP_B_VOL, 4);
    p->env = (uint8_t)chip_bits(s, CHIP_B_ENV, 2);
    p->env_step = (uint8_t)chip_bits(s, CHIP_B_STEP, 3);
    p->release = (uint8_t)chip_bits(s, CHIP_B_RELEASE, 4);
    p->psg_shape = (uint8_t)chip_bits(s, CHIP_B_PSG_SHAPE, 4);
    p->psg_period = (uint16_t)chip_bits(s, CHIP_B_PERIOD, 16);
    if (!p->psg_period)
        p->psg_period = 1;
    p->wave = (uint8_t)chip_bits(s, CHIP_B_WAVE, 4);
    for (int k = 0; k < CHIP_NTK; k++) {
        p->tab[k] = (uint8_t)chip_bits(s, CHIP_B_TAB + 8 * k, 6);
        p->spd[k] = (uint8_t)(chip_bits(s, CHIP_B_TAB + 8 * k + 6, 2) + 1);
    }
    p->vib_delay = (uint8_t)(chip_bits(s, CHIP_B_VIB_DELAY, 5) * 2);
    p->vib_speed = (uint8_t)(chip_bits(s, CHIP_B_VIB_SPEED, 2) + 1);
    p->vib_depth = (uint8_t)(chip_bits(s, CHIP_B_VIB_DEPTH, 5) * 2);
}

/* ------------------------------------------------------------------ once a frame --- */
/* a table's value this frame (0: the instrument has none of this kind); it steps every SPEED frames, loops back
 * to its loop or holds its last value */
static int take(chip_voice_t *v, int k, int32_t *val)
{
    uint32_t n = v->p.tab[k];
    if (!n || n > CHIP_TAB_COUNT[k])
        return 0;
    uint32_t t = CHIP_TAB_FIRST[k] + n - 1u, len = CHIP_TAB[t].len, i = v->tp[k];
    *val = CHIP_TV[CHIP_TAB[t].at + (i < len ? i : len - 1u)];
    if (++v->tc[k] >= v->p.spd[k]) {
        v->tc[k] = 0;
        if (++i >= len)
            i = CHIP_TAB[t].loop >= 0 ? (uint32_t)CHIP_TAB[t].loop : len;
    }
    v->tp[k] = (uint8_t)i;
    return 1;
}

static void frame_tick(chip_voice_t *v)
{
    const chip_patch_t *p = &v->p;
    int32_t x;
    v->lvol = 15;
    if (take(v, CHIP_TVOL, &x)) {
        v->lvol = (uint8_t)clampi(x, 0, 15);
        uint32_t t = CHIP_TAB_FIRST[CHIP_TVOL] + p->tab[CHIP_TVOL] - 1u;
        if (x <= 0 && CHIP_TAB[t].loop < 0 && v->tp[CHIP_TVOL] >= CHIP_TAB[t].len)
            v->dead = 1;                 /* (a table ended silent: the note is over) */
    }
    v->arp = (int8_t)(take(v, CHIP_TARP, &x) ? x : 0);
    v->cents = (int16_t)(take(v, CHIP_TPITCH, &x) ? x : 0);
    int32_t f = (int32_t)v->frames++ - p->vib_delay;
    if (p->vib_depth && f >= 0) {        /* the vibrato: 0, half, full, half, 0, -half, -full, -half */
        int32_t d = p->vib_depth, h = d >> 1, i = (f / p->vib_speed) & 7;
        v->cents = (int16_t)(v->cents + (i == 1 || i == 3 ? h : i == 2 ? d : i == 5 || i == 7 ? -h : i == 6 ? -d : 0));
    }
    v->duty = (uint8_t)(take(v, CHIP_TDUTY, &x) ? clampi(x, 0, 3) : p->duty);
    v->nrate = (uint8_t)(take(v, CHIP_TNOISE, &x) ? clampi(x, 0, 15) : p->n_rate);
    if (!v->gate) {
        if (!p->release)
            v->dead = 1;
        else if (++v->rel_c >= p->release) {
            v->rel_c = 0;
            if (--v->rel == 0)
                v->dead = 1;
        }
    }
}

/* the sweep: the register moves by itself shifted right by SHIFT; out of its range the note goes silent (one copy,
 * called by block_control at a tick, once in about 5 blocks at the most but after a PACE turned faster, and by the
 * forward work) */
static __attribute__((noinline)) void sweep_tick(chip_voice_t *v)
{
    const chip_patch_t *p = &v->p;
    int s = p->sw_shift, up = p->sweep == CHIP_SWEEP_UP;
    if (p->osc == CHIP_NOISE)
        return;
    if (p->clock == CHIP_EXACT) {
        if (!v->len_x)                   /* (found again at the next block: an OSC, BUZZ or CLOCK command) */
            return;
        v->len_x = up ? v->len_x - v->len_x / ((1u << s) + 1u) : v->len_x + (v->len_x >> s);
        if (v->len_x < CHIP_STEP_MIN)
            v->len_x = CHIP_STEP_MIN;
        if (v->len_x > 0x40000000u)
            v->len_x = 0x40000000u;
        return;
    }
    if (!v->reg)
        return;
    int32_t r = (int32_t)v->reg;
    if (p->clock == CHIP_BRK) {
        int32_t x = 2048 - r;
        x = up ? x + (x >> s) : x - (x >> s);
        if (x > 2047) { v->sw_off = 1; v->sweeping = 0; return; }
        v->reg = (uint32_t)(2048 - (x < 0 ? 0 : x));
    } else if (p->clock == CHIP_8BIT) {
        int32_t t = r - 1;
        t = up ? t - (t >> s) : t + (t >> s);
        if (t > 0x7FF) { v->sw_off = 1; v->sweeping = 0; return; }
        v->reg = (uint32_t)((t < 0 ? 0 : t) + 1);
    } else {
        uint32_t ci = p->clock * 4u + (p->osc == CHIP_PULSE ? 0u : p->osc == CHIP_BUZZ ? (p->bz_steps ? 2u : 3u) : 1u);
        r = up ? r - (r >> s) : r + (r >> s);
        v->reg = (uint32_t)clampi(r, CHIP_RLO[ci], CHIP_RHI[ci]);
    }
}

/* a pitch (1/64 semitones from MIDI note 0) as a step's length (Q16 samples), for 8 << nsh steps a period */
static uint32_t len_of(int32_t pitch, int nsh)
{
    uint32_t P = (uint32_t)clampi(pitch, 0, 135 * 64), o = P / 768u, r = P % 768u;
    uint32_t m = (CHIP_T12[r >> 6] * (uint32_t)CHIP_T64[r & 63u] + 16384u) >> 15;
    return (uint32_t)(((uint64_t)CHIP_LEN0 * m) >> (15u + o + (uint32_t)nsh));
}

/* a register for a step's length, in its range, on a clock and law (ci) */
static uint32_t reg_of(uint32_t len, uint32_t ci)
{
    uint32_t reg = (uint32_t)(((uint64_t)len * CHIP_RC[ci] + (1u << 27)) >> 28);
    return reg < CHIP_RLO[ci] ? CHIP_RLO[ci] : reg > CHIP_RHI[ci] ? CHIP_RHI[ci] : reg;
}

static uint32_t len_of_reg(uint32_t reg, uint32_t ci) { return (uint32_t)(((uint64_t)reg * CHIP_KC[ci]) >> 16); }

static int env_stopped(const chip_voice_t *v)
{
    uint32_t s = v->p.psg_shape;
    return v->e_r >= 1 && (!(s & 8u) || (s & 1u));
}

/* the oscillator's, the gate's, the noise's and the envelope's rates for this block; a stepper starting now takes
 * its first step a length from here */
static void control(chip_voice_t *v)
{
    const chip_patch_t *p = &v->p;
    int32_t pitch = ((int32_t)v->note + v->arp) * 64 + v->bend64 + v->cents * 16 / 25;
    uint32_t lo = 0, lq = 0, ln = 0, le = 0;
    v->silent = v->sw_off;
    if (p->osc != CHIP_NOISE) {
        int nsh = p->osc == CHIP_PULSE ? 0 : p->osc == CHIP_BUZZ ? 1 + p->bz_steps + (p->bz_shape == 2) : 2;
        uint32_t law = p->osc == CHIP_PULSE ? 0u : p->osc == CHIP_BUZZ ? (p->bz_steps ? 2u : 3u) : 1u;
        if (p->clock == CHIP_EXACT) {
            if (!v->sweeping || !v->len_x)
                v->len_x = len_of(pitch, nsh);
            lo = v->len_x;
        } else {
            uint32_t ci = p->clock * 4u + law;
            if (!v->sweeping || !v->reg)
                v->reg = reg_of(len_of(pitch, nsh), ci);
            lo = len_of_reg(v->reg, ci);
            if (p->clock == CHIP_8BIT && p->osc == CHIP_PULSE && v->reg < 9u)
                v->silent = 1;           /* (the console's pulse is silent under period 8) */
        }
        if (p->osc == CHIP_BUZZ && p->bz_gate)   /* the gate: a square at the same note */
            lq = p->clock == CHIP_EXACT ? len_of(pitch, 0) : len_of_reg(reg_of(len_of(pitch, 0), p->clock * 4u), p->clock * 4u);
        if (lo < CHIP_STEP_MIN)          /* (the ceiling: two steps a sample) */
            lo = CHIP_STEP_MIN;
        if (lq && lq < CHIP_STEP_MIN)
            lq = CHIP_STEP_MIN;
    }
    if (p->osc == CHIP_NOISE || p->n_mix) {
        int32_t idx = v->nrate;
        if (p->n_key)
            idx = clampi(idx - ((int32_t)v->note + ((v->bend64 + 32) >> 6) - 60), 0, 15);
        uint32_t fam = p->clock == CHIP_BRK ? 0u : p->clock == CHIP_8BIT || p->clock == CHIP_EXACT ? 1u : p->clock;
        ln = CHIP_NOISE_LEN[fam * 16u + (uint32_t)idx];
    }
    if (p->env == CHIP_ENV_PSG && !env_stopped(v)) {
        uint32_t c = p->clock >= CHIP_PSG177 && p->clock <= CHIP_16BIT ? p->clock - 2u : 0u;
        le = (uint32_t)p->psg_period * CHIP_KE[(p->curve == 3 ? 4u : 0u) + c];
        if (le < CHIP_ENV_STEP_MIN)
            le = CHIP_ENV_STEP_MIN;
    }
    if (lo && !v->len_o) v->nx_o = lo;
    if (lq && !v->len_q) v->nx_q = lq;
    if (ln && !v->len_n) v->nx_n = ln;
    if (le && !v->len_e) v->nx_e = le;
    v->len_o = lo; v->len_q = lq; v->len_n = ln; v->len_e = le;
}

/* a frame's length, the step envelope's step and the sweep's (Q16 samples; 0: it does not step), on the
 * instrument's clock and FPS */
CHIP_INL uint32_t frame_len(const chip_patch_t *p)
{
    return CHIP_FRAME[p->fps50 ? 3 : p->clock == CHIP_BRK ? 0 : p->clock == CHIP_8BIT ? 1 : 2];
}
CHIP_INL uint32_t env_iv(const chip_patch_t *p) { return CHIP_ENV_STEP[(p->clock == CHIP_8BIT ? 8u : 0u) + p->env_step]; }
CHIP_INL uint32_t sweep_iv(const chip_patch_t *p) { return CHIP_SWEEP_PACE[(p->clock == CHIP_8BIT ? 8u : 0u) + p->sw_pace]; }

static void block_control(chip_voice_t *v)
{
    const chip_patch_t *p = &v->p;
    const uint32_t B = (uint32_t)CHIP_N << 16;
    uint32_t fl = frame_len(p), iv;
    v->f_acc += B;
    while (v->f_acc >= fl) {
        v->f_acc -= fl;
        frame_tick(v);
    }
    if (p->env == CHIP_ENV_DOWN || p->env == CHIP_ENV_UP) {
        iv = env_iv(p);
        if (iv) {
            v->e_acc += B;
            while (v->e_acc >= iv) {
                v->e_acc -= iv;
                if (p->env == CHIP_ENV_DOWN) { if (v->vol) v->vol--; }
                else if (v->vol < 15) v->vol++;
            }
        }
    }
    iv = sweep_iv(p);
    if (v->sweeping && iv) {
        v->s_acc += B;
        while (v->s_acc >= iv && v->sweeping) {
            v->s_acc -= iv;
            sweep_tick(v);
        }
    }
    control(v);
}

/* ------------------------------------------------------------------- the output --- */
static uint32_t pop14(uint32_t x)        /* the bits set in x < 2^14 */
{
    x = x - (x >> 1 & 0x5555u);
    x = (x & 0x3333u) + (x >> 2 & 0x3333u);
    x = (x + (x >> 4)) & 0x0F0Fu;
    return (x + (x >> 8)) & 31u;
}

/* k steps of the noise register, the sum of their outputs (the last in nbit). A step shifts the register right and
 * puts the feedback, bit 0 ^ bit T, in at its top (bit 14; the PSG clocks' 17-bit register bit 16); BRK's 7-bit mode
 * also puts it into bit 6; the output is the low bit after the step, inverted but on the PSG's register. As the
 * twin's one step at a time: a run of c steps reads bits 0..c-1 and T..T+c-1 as they were, so c stays at most the
 * width less T; the c outputs are bits 1..c as they were (inline: fast noise runs it every sample) */
static inline __attribute__((always_inline)) uint32_t noise_run(chip_voice_t *v, uint32_t k)
{
    const chip_patch_t *p = &v->p;
    uint32_t seven = p->n_short && p->clock == CHIP_BRK;
    uint32_t psg = !p->n_short && p->clock >= CHIP_PSG177 && p->clock <= CHIP_16BIT;
    uint32_t t = seven || !p->n_short ? (psg ? 3u : 1u) : 6u, w = psg ? 17u : 15u, run = (seven ? 7u : w) - t;
    uint32_t l = v->lfsr ? v->lfsr : 1u, s = 0, o = 0, c = 1, n = k;
    uint32_t empty = seven && !(l & 0x7Fu);   /* (its 7 bits empty, the rest shifting out: a step at a time, the
                                               * register found empty taken as 1, as the twin's every step) */
    if (empty)
        run = 1;
    while (n) {
        if (empty && !l)
            l = 1;
        c = n < run ? n : run;
        uint32_t m = (1u << c) - 1u, f = (l ^ (l >> t)) & m;
        o = (l >> 1) & m;
        if (seven)
            l = (((l >> c) | (f << (15u - c))) & ~0x7Fu) | ((l & 0x7Fu) >> c) | (f << (7u - c));
        else
            l = (l >> c) | (f << (w - c));
        s += pop14(o);
        n -= c;
    }
    v->lfsr = l;
    v->nbit = (uint8_t)((o >> (c - 1u) & 1u) ^ (psg ? 0u : 1u));
    return psg ? s : k - s;
}

/* the PSG envelope's level at round r, place pos, of steps (16 or 32), for a SHAPE */
static uint32_t env_level(uint32_t shape, uint32_t steps, uint32_t r, uint32_t pos)
{
    uint32_t att = shape >> 2 & 1u, alt = shape >> 1 & 1u, hold = shape & 1u, cont = shape >> 3 & 1u, mx = steps - 1u;
    if (r == 0)
        return att ? pos : mx - pos;
    if (!cont)
        return 0;
    if (hold)
        return (att ^ alt) ? mx : 0u;
    return (alt ? (att ^ (r & 1u)) : att) ? pos : mx - pos;
}

static void step_osc(chip_voice_t *v)
{
    if (v->p.osc == CHIP_BUZZ) {
        if (++v->step >= (v->p.bz_steps ? 32u : 16u)) {
            v->step = 0;
            v->bz_r++;
        }
    } else
        v->step = (uint8_t)((v->step + 1u) & 31u);
}

static void step_env(chip_voice_t *v)
{
    if (++v->e_pos >= (v->p.curve == 3 ? 32u : 16u)) {
        v->e_pos = 0;
        v->e_r++;
    }
}

/* the level now before the noise (Q15 of the curve's full scale): the volume (or the PSG envelope) x the VOL table
 * x the release, rounded to a step, through the curve, by the oscillator's step. Its divisions are multiplies,
 * exact for every value they meet (the twin divides) */
static uint32_t tone(const chip_voice_t *v)
{
    const chip_patch_t *p = &v->p;
    uint32_t V, mx = 15, a = 0;
    if (v->dead || v->silent)
        return 0;
    if (p->env == CHIP_ENV_PSG) {
        uint32_t st = p->curve == 3 ? 32u : 16u;
        mx = st - 1u;
        V = env_level(p->psg_shape, st, v->e_r, v->e_pos < mx ? v->e_pos : mx);
    } else
        V = v->vol;
    V = ((V * v->lvol * v->rel * 2u + 225u) * 9321u) >> 22;            /* (/ 450) */
    if (!V)
        return 0;
    const uint16_t *c16 = CHIP_CURVE + p->curve * 64u, *cv = c16 + (mx == 31u ? 32u : 0u);
    uint32_t r2 = mx == 15u ? 1093u : 529u;                               /* (/ 2 mx, >> 15) */
    switch (p->osc) {
    case CHIP_PULSE:
        a = CHIP_DUTY[(p->clock == CHIP_BRK ? 0u : 4u) + v->duty] >> (v->step & 7u) & 1u ? cv[V] : 0u;
        break;
    case CHIP_TRIANGLE:
        a = c16[((CHIP_TRI[v->step & 31u] * V * 2u + mx) * r2) >> 15];
        break;
    case CHIP_WAVE:
        a = c16[((CHIP_WAVES[p->wave * 32u + (v->step & 31u)] * V * 2u + mx) * r2) >> 15];
        break;
    case CHIP_BUZZ: {
        uint32_t st = p->bz_steps ? 32u : 16u, shp = p->bz_shape == 1 ? 12u : p->bz_shape == 2 ? 10u : 8u;
        uint64_t x = (uint64_t)c16[(st == 32u ? 32u : 0u) + env_level(shp, st, v->bz_r, v->step < st ? v->step : st - 1u)] * V;
        a = mx == 15u ? (uint32_t)((x * 559241u) >> 23) : (uint32_t)((x * 541201u) >> 24);   /* (/ mx) */
        if (p->bz_gate && v->sq_step >= 4)
            a = 0;
        break;
    }
    default:
        a = cv[V];
    }
    return a;
}

/* the level put out (16-bit scale): the tone, by the noise where it is heard (ns high of nk steps) */
static int32_t amp(const chip_voice_t *v, uint32_t a, uint32_t ns, uint32_t nk)
{
    if (v->p.osc == CHIP_NOISE || v->p.n_mix)
        a = (uint32_t)(((uint64_t)(a * ns) * CHIP_RCP[nk]) >> 31);      /* (a ns / nk) */
    return (int32_t)((a * CHIP_OUT) >> 15);
}

/* a change of the level at time t (Q16 samples into the block) as a band-limited impulse into the block's sums
 * (out) and, past the block, the tail */
static void emit(chip_voice_t *v, int32_t *out, uint32_t t, int32_t a)
{
    int32_t d = a - v->last;
    if (!d)
        return;
    v->last = a;
    uint32_t i = t >> 16, j, n = CHIP_N - i < CHIP_KW ? CHIP_N - i : CHIP_KW;
    const int16_t *k = CHIP_KERNEL + ((t >> 10) & 63u) * CHIP_KW;
    if (n == CHIP_KW) {                  /* (the usual case, unrolled: a loop's jump a tap costs more than the tap) */
        int32_t *o = out + i;
        o[0] += d * k[0]; o[1] += d * k[1]; o[2] += d * k[2]; o[3] += d * k[3];
        o[4] += d * k[4]; o[5] += d * k[5]; o[6] += d * k[6]; o[7] += d * k[7];
        o[8] += d * k[8]; o[9] += d * k[9]; o[10] += d * k[10]; o[11] += d * k[11];
        o[12] += d * k[12]; o[13] += d * k[13]; o[14] += d * k[14]; o[15] += d * k[15];
        return;
    }
    for (j = 0; j < n; j++)
        out[i + j] += d * k[j];
    for (; j < CHIP_KW; j++)
        v->tail[i + j - CHIP_N] += d * k[j];
}

static void steps_exact(chip_voice_t *v, int32_t *out)
{
    const uint32_t T = (uint32_t)CHIP_N << 16;
    emit(v, out, 0, amp(v, tone(v), v->nbit, 1));
    for (;;) {
        uint32_t t = T;
        if (v->len_o && v->nx_o < t) t = v->nx_o;
        if (v->len_q && v->nx_q < t) t = v->nx_q;
        if (v->len_n && v->nx_n < t) t = v->nx_n;
        if (v->len_e && v->nx_e < t) t = v->nx_e;
        if (t >= T)
            break;
        if (v->len_o && v->nx_o == t) { step_osc(v); v->nx_o += v->len_o; }
        if (v->len_q && v->nx_q == t) { v->sq_step = (uint8_t)((v->sq_step + 1u) & 7u); v->nx_q += v->len_q; }
        if (v->len_n && v->nx_n == t) { noise_run(v, 1); v->nx_n += v->len_n; }
        if (v->len_e && v->nx_e == t) { step_env(v); v->nx_e += v->len_e; if (env_stopped(v)) v->len_e = 0; }
        emit(v, out, t, amp(v, tone(v), v->nbit, 1));
    }
}

/* noise faster than the sample rate: each sample takes the steps that fall in it and the noise's average over
 * them. That average is already the sample's filter, so it goes out as a plain step, where a band-limited one's
 * middle would land (the kernel's tap 7) */
static void steps_fast(chip_voice_t *v, int32_t *out)
{
    const uint32_t rl = 0xFFFFFFFFu / v->len_n + 1u;      /* 2^32 / the noise's step, up: a count by a multiply */
    uint32_t a = tone(v);
    for (uint32_t i = 0; i < CHIP_N; i++) {
        uint32_t te = (i + 1u) << 16, s = 0, k = 0, ch = 0;
        while (v->len_o && v->nx_o < te) { step_osc(v); v->nx_o += v->len_o; ch = 1; }
        while (v->len_q && v->nx_q < te) { v->sq_step = (uint8_t)((v->sq_step + 1u) & 7u); v->nx_q += v->len_q; ch = 1; }
        while (v->len_e && v->nx_e < te) { step_env(v); v->nx_e += v->len_e; if (env_stopped(v)) v->len_e = 0; ch = 1; }
        if (ch)
            a = tone(v);
        if (v->nx_n < te) {              /* (1 + (te - 1 - nx) / len: the steps before te) */
            k = 1u + (uint32_t)(((uint64_t)(te - 1u - v->nx_n) * rl) >> 32);
            v->nx_n += k * v->len_n;
            s = noise_run(v, k);
        }
        int32_t x = k ? amp(v, a, s, k) : amp(v, a, v->nbit, 1), d = x - v->last;
        if (d) {
            v->last = x;
            if (i + 7u < CHIP_N)
                out[i + 7u] += d * (1 << CHIP_KQ);
            else
                v->tail[i + 7u - CHIP_N] += d * (1 << CHIP_KQ);
        }
    }
}

static void block_end(chip_voice_t *v)
{
    const uint32_t T = (uint32_t)CHIP_N << 16;
    if (v->len_o) v->nx_o -= T;
    if (v->len_q) v->nx_q -= T;
    if (v->len_n) v->nx_n -= T;
    if (v->len_e) v->nx_e -= T;
}

void chip_render(chip_voice_t *v, int32_t *out)
{
    const chip_patch_t *p = &v->p;
    uint32_t i;
    block_control(v);
    for (i = 0; i < CHIP_KW; i++) {
        out[i] += v->tail[i];
        v->tail[i] = 0;
    }
    if (v->len_n && v->len_n < 65536u)
        steps_fast(v, out);
    else
        steps_exact(v, out);
    block_end(v);
    /* the sums integrated (band-limited steps), then the family's output filter */
    int32_t acc = v->acc, hx0 = v->hx[0], hy0 = v->hy[0], hx1 = v->hx[1], hy1 = v->hy[1], ly = v->ly;
    int32_t a0 = CHIP_HP[p->filter * 2u], a1 = CHIP_HP[p->filter * 2u + 1u];
    for (i = 0; i < CHIP_N; i++) {
        int32_t x;
        acc += out[i];
        x = (int32_t)(((int64_t)a0 * (hy0 + acc - hx0)) >> 15);
        hx0 = acc;
        hy0 = x;
        if (a1) {
            int32_t y = (int32_t)(((int64_t)a1 * (hy1 + x - hx1)) >> 15);
            hx1 = x;
            hy1 = y;
            x = y;
            ly += (int32_t)(((int64_t)CHIP_LP_8BIT * (x - ly)) >> 15);   /* (only the console has two stages) */
            x = ly;
        }
        out[i] = x >> CHIP_KQ;
    }
    v->acc = acc; v->hx[0] = hx0; v->hy[0] = hy0; v->hx[1] = hx1; v->hy[1] = hy1; v->ly = ly;
}

void chip_skip(chip_voice_t *v)
{
    const uint32_t T = (uint32_t)CHIP_N << 16;
    block_control(v);
    while (v->len_o && v->nx_o < T) { step_osc(v); v->nx_o += v->len_o; }
    while (v->len_q && v->nx_q < T) { v->sq_step = (uint8_t)((v->sq_step + 1u) & 7u); v->nx_q += v->len_q; }
    if (v->len_n && v->nx_n < T) {
        uint32_t k = 1u + (T - 1u - v->nx_n) / v->len_n;
        v->nx_n += k * v->len_n;
        noise_run(v, k);
    }
    while (v->len_e && v->nx_e < T) { step_env(v); v->nx_e += v->len_e; if (env_stopped(v)) v->len_e = 0; }
    block_end(v);
    if (v->dead) {                       /* (ended unheard: nothing of it left to sound) */
        v->last = 0;
        v->acc = 0;
        for (int i = 0; i < CHIP_KW; i++)
            v->tail[i] = 0;
        v->hx[0] = v->hy[0] = v->hx[1] = v->hy[1] = v->ly = 0;
    }
}

PLAT_COLD int chip_active(const chip_voice_t *v)
{
    if (!v->dead || v->last)
        return 1;
    for (int i = 0; i < CHIP_KW; i++)
        if (v->tail[i])
            return 1;
    return v->acc > 4095 || v->acc < -4095 || v->hy[0] > 4095 || v->hy[0] < -4095 || v->ly > 4095 || v->ly < -4095;
}

/* ------------------------------------------------------------------ notes --- */
void chip_note_on(chip_voice_t *v, const chip_patch_t *p, int note)
{
    uint8_t *b = (uint8_t *)v;
    for (uint32_t i = 0; i < sizeof *v; i++)
        b[i] = 0;
    v->p = *p;
    v->note = (uint8_t)note;
    v->gate = 1;
    v->vol = p->vol;
    v->rel = 15;
    v->lfsr = 1;
    v->nbit = 1;
    v->sweeping = (uint8_t)(p->sweep != CHIP_SWEEP_OFF && p->osc != CHIP_NOISE && (p->clock == CHIP_8BIT || p->sw_pace));
    frame_tick(v);
    control(v);
}

void chip_pitch(chip_voice_t *v, int32_t bend64) { v->bend64 = bend64; }

void chip_note_off(chip_voice_t *v)
{
    v->gate = 0;
    if (!v->p.release)
        v->dead = 1;
}

void chip_cut(chip_voice_t *v) { v->dead = 1; v->gate = 0; }

/* the built-in tables, waves and widths for the screens' pictures */
const int8_t *chip_table(int kind, int n, int *len, int *loop)
{
    if (kind < 0 || kind >= CHIP_NTK || n < 1 || n > CHIP_TAB_COUNT[kind])
        return 0;
    uint32_t t = CHIP_TAB_FIRST[kind] + (uint32_t)n - 1u;
    *len = CHIP_TAB[t].len;
    *loop = CHIP_TAB[t].loop;
    return CHIP_TV + CHIP_TAB[t].at;
}
const uint8_t *chip_wave(int w) { return CHIP_WAVES + 32 * (w & 15); }
const uint8_t *chip_tri(void) { return CHIP_TRI; }
uint32_t chip_duty_bits(int clock, int duty) { return CHIP_DUTY[(clock == CHIP_BRK ? 0 : 4) + (duty & 3)]; }

/* the register a note lands on, and how far off true it sounds in tenths of a cent (+ sharp): 1200 log2(true /
 * the register's step) by its series to the third power, in whole numbers (the INSTRUMENTS screen; the twin's
 * tuning()) */
PLAT_COLD void chip_tuning(const chip_patch_t *p, int note, uint32_t *reg, int32_t *c10)
{
    int nsh = p->osc == CHIP_PULSE ? 0 : p->osc == CHIP_BUZZ ? 1 + p->bz_steps + (p->bz_shape == 2) : 2;
    uint32_t ci = p->clock * 4u + (p->osc == CHIP_PULSE ? 0u : p->osc == CHIP_BUZZ ? (p->bz_steps ? 2u : 3u) : 1u);
    if (p->clock == CHIP_EXACT) {        /* (no register: in tune) */
        *reg = 0;
        *c10 = 0;
        return;
    }
    uint32_t lt = len_of(note * 64, nsh), r = reg_of(lt, ci), a = lt, b = len_of_reg(r, ci);
    while (a >= 16384u || b >= 16384u) {
        a >>= 1;
        b >>= 1;
    }
    int32_t x = (int32_t)(((int32_t)a - (int32_t)b) * 65536) / (int32_t)(b ? b : 1u);
    int32_t x2 = (int32_t)(((int64_t)x * x) >> 16), x3 = (int32_t)(((int64_t)x2 * x) >> 16);
    *reg = r;
    *c10 = (int32_t)(((int64_t)(x - x2 / 2 + x3 / 3) * 17312 + 32768) >> 16);
}

/* a parameter command (specs/files/commands.md#chip) on an instrument's settings: the value as the slot stores it
 * (a SPEED 0-3 for X1-X4, the vibrato's frames and cents 0-31 in 2s); a value past its range acts as its top */
PLAT_COLD void chip_set(chip_patch_t *p, uint32_t code, uint32_t val)
{
    switch (code) {
    case 0x40: p->osc = (uint8_t)lim(val, CHIP_NOISE); break;
    case 0x41: p->duty = (uint8_t)lim(val, 3); break;
    case 0x42: p->bz_shape = (uint8_t)lim(val, 2); break;
    case 0x43: p->bz_steps = (uint8_t)lim(val, 1); break;
    case 0x44: p->bz_gate = (uint8_t)lim(val, 1); break;
    case 0x48: p->n_mix = (uint8_t)lim(val, 1); break;
    case 0x49: p->n_short = (uint8_t)lim(val, 1); break;
    case 0x4A: p->n_rate = (uint8_t)lim(val, 15); break;
    case 0x4B: p->n_key = (uint8_t)lim(val, 1); break;
    case 0x4C: p->clock = (uint8_t)lim(val, CHIP_EXACT); break;
    case 0x4D: p->curve = (uint8_t)lim(val, 3); break;
    case 0x4E: p->filter = (uint8_t)lim(val, 3); break;
    case 0x4F: p->fps50 = (uint8_t)lim(val, 1); break;
    case 0x50: p->sweep = (uint8_t)lim(val, CHIP_SWEEP_UP); break;
    case 0x51: p->sw_shift = (uint8_t)lim(val, 7); break;
    case 0x52: p->sw_pace = (uint8_t)lim(val, 7); break;
    case 0x54: p->vol = (uint8_t)lim(val, 15); break;
    case 0x55: p->env = (uint8_t)lim(val, CHIP_ENV_PSG); break;
    case 0x56: p->env_step = (uint8_t)lim(val, 7); break;
    case 0x57: p->release = (uint8_t)lim(val, 15); break;
    case 0x58: p->psg_shape = (uint8_t)lim(val, 15); break;
    case 0x59: p->psg_period = (uint16_t)(lim(val, 255) << 8 | (p->psg_period & 255u)); if (!p->psg_period) p->psg_period = 1; break;
    case 0x5A: p->psg_period = (uint16_t)((p->psg_period & 0xFF00u) | lim(val, 255)); if (!p->psg_period) p->psg_period = 1; break;
    case 0x5B: p->wave = (uint8_t)lim(val, 15); break;
    case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: p->tab[code - 0x60] = (uint8_t)lim(val, 63); break;
    case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: p->spd[code - 0x68] = (uint8_t)(lim(val, 3) + 1u); break;
    case 0x6D: p->vib_delay = (uint8_t)(lim(val, 31) * 2u); break;
    case 0x6E: p->vib_speed = (uint8_t)(lim(val, 3) + 1u); break;
    case 0x6F: p->vib_depth = (uint8_t)(lim(val, 31) * 2u); break;
    default: break;
    }
}

/* the same on a sounding note: what it changes takes effect from the next block (a table starts again; a new
 * oscillator, buzz or clock finds its register again) */
void chip_param(chip_voice_t *v, uint32_t code, uint32_t val)
{
    chip_patch_t *p = &v->p;
    chip_set(p, code, val);
    if (code == 0x40 || code == 0x42 || code == 0x43 || code == 0x4C) { v->reg = 0; v->len_x = 0; }
    else if (code == 0x41) v->duty = p->duty;
    else if (code == 0x4A) v->nrate = p->n_rate;
    else if (code == 0x54) v->vol = p->vol;
    else if (code == 0x55 || code == 0x58) { v->e_pos = 0; v->e_r = 0; }
    else if (code >= 0x60 && code <= 0x64) { v->tp[code - 0x60] = 0; v->tc[code - 0x60] = 0; }
    if ((code >= 0x50 && code <= 0x52) || code == 0x40 || code == 0x4C)   /* (a sweep there, or not, now) */
        v->sweeping = (uint8_t)(p->sweep != CHIP_SWEEP_OFF && p->osc != CHIP_NOISE && (p->clock == CHIP_8BIT || p->sw_pace) &&
                                !v->sw_off);
}

/* ------------------------------------------------------------- the forward work --- */
/* (chip.h) a note's time worked on many blocks at once, to where chip_skip's blocks leave what it keeps. What moves by
 * the frames (the tables, the vibrato's count, the release), the step envelope and the PSG envelope each move by
 * their counts in one sum, the sweep a tick at a time until it stands; a stretch's last block is chip_skip's own,
 * which makes what the next block reads (this frame's table values, the period, the volume, the width) as the steps'
 * last block makes it */
#define CHIP_FWD_TICKS 32u               /* the sweep's ticks a step (chip.h) */
#define FWD_BSH 22                       /* a block (CHIP_N << 16 Q16 samples) as a shift */
typedef char chip_fwd_bsh[(CHIP_N << 16) == 1 << FWD_BSH ? 1 : -1];

/* x / d (d > 0): the quotient mod 2^32, the remainder in *r; long division a bit at a time (no 64-bit division on
 * the FM-1) */
static PLAT_COLD uint32_t fdiv(uint64_t x, uint32_t d, uint32_t *r)
{
    uint32_t h = (uint32_t)(x >> 32) % d, l = (uint32_t)x, q = 0, c;
    for (int i = 0; i < 32; i++) {
        c = h >> 31;                     /* (the bit shifted out of h: h - d is right mod 2^32) */
        h = h << 1 | l >> 31;
        l <<= 1;
        q <<= 1;
        if (c || h >= d) {
            h -= d;
            q |= 1u;
        }
    }
    *r = h;
    return q;
}

/* the times `if (++*c >= per) *c = 0` comes round in m counts (per > 0), *c left as they leave it */
static PLAT_COLD uint32_t wraps(uint8_t *c, uint32_t per, uint32_t m)
{
    uint32_t k = 0, x = *c;
    if (!m)
        return 0;
    if (x >= per) {                      /* (past a period made shorter: the first count comes round) */
        x = 0;
        m--;
        k = 1;
    }
    x += m;
    *c = (uint8_t)(x % per);
    return k + x / per;
}

/* table k moved on m frames, as m frames' take() leave its place: on to its end, round its loop, or held past its
 * end */
static PLAT_COLD void tab_go(chip_voice_t *v, int k, uint32_t m)
{
    uint32_t n = v->p.tab[k], t, len, lp, i;
    if (!n || n > CHIP_TAB_COUNT[k])
        return;
    t = CHIP_TAB_FIRST[k] + n - 1u;
    len = CHIP_TAB[t].len;
    lp = (uint32_t)CHIP_TAB[t].loop;
    i = v->tp[k] + wraps(&v->tc[k], v->p.spd[k], m);
    if (i >= len)
        i = CHIP_TAB[t].loop < 0 ? len : lp + (i - len) % (len > lp ? len - lp : 1u);
    v->tp[k] = (uint8_t)i;
}

/* the VOL table's length when it ends silent and stays (its last value 0 or under, no loop: frame_tick ends the note
 * as its place reaches the end); 0 if not */
static PLAT_COLD uint32_t vol_end(const chip_voice_t *v)
{
    uint32_t n = v->p.tab[CHIP_TVOL], t;
    if (!n || n > CHIP_TAB_COUNT[CHIP_TVOL])
        return 0;
    t = CHIP_TAB_FIRST[CHIP_TVOL] + n - 1u;
    return CHIP_TAB[t].loop < 0 && CHIP_TV[CHIP_TAB[t].at + CHIP_TAB[t].len - 1u] <= 0 ? CHIP_TAB[t].len : 0u;
}

/* the sweep over m blocks, a tick a block at the most (its PACE's step is over a block), up to a ticks (the budget's):
 * its ticks stepped until it stands, its clock as those blocks leave it; *ticks the ticks stepped. Returns the blocks
 * the stretch's sums take: m, or fewer where the budget ends it before the next tick */
static PLAT_COLD uint32_t sweep_go(chip_voice_t *v, uint32_t m, uint32_t iv, uint32_t a, uint32_t *ticks)
{
    const uint32_t B = (uint32_t)CHIP_N << 16;
    uint32_t s0 = v->s_acc, *reg = v->p.clock == CHIP_EXACT ? &v->len_x : &v->reg, was, cut = 0, j = 0, blocks, rest,
             most = fdiv(s0 + ((uint64_t)m << FWD_BSH), iv, &rest);   /* (its ticks in the m blocks) */
    a = a <= most / CHIP_FWD_TICKS ? a * CHIP_FWD_TICKS : most;
    while (j < most) {
        if (j == a) {                    /* (the budget: the stretch to the block before the next tick) */
            cut = 1;
            break;
        }
        was = *reg;
        sweep_tick(v);
        j++;
        if (!v->sweeping || was == *reg)
            break;                       /* (out of its range: it stops in that tick's block; at its limit: the ticks
                                          * left move nothing) */
    }
    if (cut || !v->sweeping) {           /* (the blocks to the tick j + cut's, less cut) */
        blocks = (uint32_t)(((uint64_t)(j + cut) * iv - s0 + B - 1u) >> FWD_BSH) - cut;
        if (cut)
            m = blocks;
        rest = (uint32_t)(s0 + ((uint64_t)blocks << FWD_BSH) - (uint64_t)j * iv);
    }
    v->s_acc = rest;
    *ticks = j;
    return m;
}

/* the frames of m blocks: all but the last in one sum each (the tables, the vibrato's count, the release: a level
 * every RELEASE frames; the VOL table's or the release's end), the last by frame_tick */
static PLAT_COLD void frames_go(chip_voice_t *v, uint32_t m)
{
    const chip_patch_t *p = &v->p;
    uint32_t n = fdiv(v->f_acc + ((uint64_t)m << FWD_BSH), frame_len(p), &v->f_acc), end, levels;
    int k;
    if (!n--)
        return;
    for (k = 0; k < CHIP_NTK; k++)
        tab_go(v, k, n);
    end = vol_end(v);
    if (end && v->tp[CHIP_TVOL] >= end)
        v->dead = 1;
    v->frames += n;
    if (!v->gate && n) {
        if (!p->release) {
            v->dead = 1;
        } else {
            levels = wraps(&v->rel_c, p->release, n);
            if (levels >= v->rel)
                v->dead = 1;
            v->rel = (uint8_t)(v->rel - levels);
        }
    }
    frame_tick(v);
}

/* the PSG envelope over m blocks, running (len_e): to where its first round's last step stops it (a shape that stops:
 * running, its round is the first), or its steps there (their count mod 2^32: the round is kept mod 2^16 and the place
 * mod its st steps, both dividing it) */
static PLAT_COLD void psg_go(chip_voice_t *v, uint32_t m, uint32_t st)
{
    const uint32_t B = (uint32_t)CHIP_N << 16, len = v->len_e, s = v->p.psg_shape;
    uint64_t span = (uint64_t)m << FWD_BSH, last = v->nx_e + (uint64_t)(st - v->e_pos - 1u) * len;
    uint32_t n, rest;
    if ((!(s & 8u) || (s & 1u)) && last < span) {
        v->nx_e = ((uint32_t)last & (B - 1u)) + len;
        v->e_pos = 0;
        v->e_r = 1;
        v->len_e = 0;
    } else if (v->nx_e >= span) {
        v->nx_e -= (uint32_t)span;
    } else {
        n = fdiv(span - 1u - v->nx_e, len, &rest) + 1u + v->e_pos;
        v->e_pos = (uint8_t)(n % st);
        v->e_r = (uint16_t)(v->e_r + n / st);
        v->nx_e = len - 1u - rest;
    }
}

/* the voice moved on n blocks (n > 0): the first m = n - 1 in sums, then the last by chip_skip (the oscillator's, the
 * gate's and the noise's phases, which the sums leave as they were, move on in it from there: they are not kept, and
 * stay a step or less from their next steps). Its sweep moves a tick at a time, as far as the budget allows: past it
 * the stretch ends at the block before the next tick, which the last block takes. One block alone where a sum does
 * not hold: a register to find (the sweep's, found as the block ends), a count past its period (CURVE, a sweep's PACE
 * turned). A stretch costs a step, or a step for each CHIP_FWD_TICKS ticks of its sweep where they are more: never more
 * than *steps, so a call leaves it at 0 at the lowest. Returns the blocks gone */
static PLAT_COLD uint32_t fwd_run(chip_voice_t *v, uint32_t n, int32_t *steps)
{
    const chip_patch_t *p = &v->p;
    uint32_t m = n - 1u, ticks = 0, iv = sweep_iv(p), k, st = p->curve == 3 ? 32u : 16u;
    if ((v->sweeping && iv && (v->s_acc >= iv || !(p->clock == CHIP_EXACT ? v->len_x : v->reg))) || v->e_pos >= st)
        m = 0;
    if (m) {
        control(v);                      /* (the first block's: the PSG envelope's step, started if it starts) */
        if (v->sweeping && iv)
            m = sweep_go(v, m, iv, (uint32_t)*steps, &ticks);
        frames_go(v, m);
        iv = env_iv(p);
        if ((p->env == CHIP_ENV_DOWN || p->env == CHIP_ENV_UP) && iv) {   /* the step envelope */
            k = fdiv(v->e_acc + ((uint64_t)m << FWD_BSH), iv, &v->e_acc);
            if (p->env == CHIP_ENV_DOWN)
                v->vol = (uint8_t)(k < v->vol ? v->vol - k : 0u);
            else
                v->vol = (uint8_t)(k < 15u - v->vol ? v->vol + k : 15u);
        }
        if (v->len_e)
            psg_go(v, m, st);
    }
    chip_skip(v);
    *steps -= ticks > CHIP_FWD_TICKS ? (int32_t)((ticks + CHIP_FWD_TICKS - 1u) / CHIP_FWD_TICKS) : 1;
    return m + 1u;
}

/* (chip.h) the held time, the note off, the release */
PLAT_COLD int chip_forward(chip_voice_t *v, uint32_t *on, uint32_t *off, int32_t *steps)
{
    for (;;) {
        if (!*on && *off != FWD_HELD && v->gate)
            chip_note_off(v);            /* (once, free: its gate tells it was; the release counts with the frames) */
        if (v->dead) {                   /* (ended by itself: nothing of it sounds again) */
            *on = 0;
            if (*off != FWD_HELD)
                *off = 0;
        }
        if (!*on && (*off == FWD_HELD || !*off))
            return 1;
        if (*steps <= 0)
            return 0;
        if (*on)
            *on -= fwd_run(v, *on, steps);
        else
            *off -= fwd_run(v, *off, steps);
    }
}

/* (chip.h) the loudest level still to come: the envelope's highest (the step envelope rising: 15; the PSG envelope's
 * round rising or coming round again: its top), the VOL table's highest from its place on (its loop's too), the
 * release's level (it only falls), through the volume's rounding and the curve */
PLAT_COLD int32_t chip_heard(const chip_voice_t *v)
{
    const chip_patch_t *p = &v->p;
    uint32_t e = v->vol, l = v->lvol, mx = 15, n = p->tab[CHIP_TVOL], s = p->psg_shape;
    if (v->dead || v->sw_off)
        return 0;
    if (p->env == CHIP_ENV_PSG) {        /* (env_level's: stopped, at its end; in its first round falling to its end,
                                          * where it is; else its top) */
        mx = p->curve == 3 ? 31u : 15u;
        if (env_stopped(v))
            e = (s & 8u) && ((s >> 2 ^ s >> 1) & 1u) ? mx : 0u;
        else if (!v->e_r && !(s & 4u) && (!(s & 8u) || (s & 3u) == 1u))
            e = mx - (v->e_pos < mx ? v->e_pos : mx);
        else
            e = mx;
    } else if (p->env == CHIP_ENV_UP && env_iv(p)) {
        e = 15;
    }
    if (n && n <= CHIP_TAB_COUNT[CHIP_TVOL]) {
        uint32_t t = CHIP_TAB_FIRST[CHIP_TVOL] + n - 1u, len = CHIP_TAB[t].len, i = v->tp[CHIP_TVOL];
        i = i < len ? i : len - 1u;
        if (CHIP_TAB[t].loop >= 0 && (uint32_t)CHIP_TAB[t].loop < i)
            i = (uint32_t)CHIP_TAB[t].loop;
        for (; i < len; i++)
            if (CHIP_TV[CHIP_TAB[t].at + i] > (int32_t)l)
                l = CHIP_TV[CHIP_TAB[t].at + i] > 15 ? 15u : (uint32_t)CHIP_TV[CHIP_TAB[t].at + i];
    }
    e = ((e * l * v->rel * 2u + 225u) * 9321u) >> 22;   /* (tone's volume, then its level at its loudest step: BUZZ's
                                                          * the volume's share of the curve's top, as tone's multiply
                                                          * makes it for every volume) */
    e = p->osc == CHIP_BUZZ ? (e << 15) / mx : CHIP_CURVE[p->curve * 64u + (mx == 31u ? 32u : 0u) + e];
    return (int32_t)((e * (uint32_t)CHIP_OUT * 3u) >> 16);   /* (x 1.5: the edges' ringing and the high-pass, chip.h) */
}

/* the frame (1: the next) at which a count `if (++c >= per) c = 0` comes round the a-th time (a > 0) */
static PLAT_COLD uint32_t nth(uint32_t c, uint32_t per, uint32_t a)
{
    return (c >= per ? 1u : per - c) + (a - 1u) * per;
}

/* (chip.h) the frame that ends it (its release's last level, its VOL table's end), as the block counting it */
PLAT_COLD uint32_t chip_end_blocks(const chip_voice_t *v, int held)
{
    const chip_patch_t *p = &v->p;
    const uint32_t B = (uint32_t)CHIP_N << 16;
    uint32_t f = CHIP_NEVER, e = vol_end(v), i = v->tp[CHIP_TVOL];
    uint64_t t;
    if (v->dead)
        return 0;
    if (!held || !v->gate) {
        if (!p->release && v->gate)
            return 0;                    /* (let go now with no RELEASE: chip_note_off ends it) */
        f = p->release ? nth(v->rel_c, p->release, v->rel) : 1u;
    }
    if (e) {
        e = i >= e ? 1u : nth(v->tc[CHIP_TVOL], p->spd[CHIP_TVOL], e - i);
        f = e < f ? e : f;
    }
    if (f == CHIP_NEVER)
        return CHIP_NEVER;
    t = (uint64_t)f * frame_len(p);      /* (f_acc past a frame made shorter by FPS or CLOCK: its frames in block 1) */
    return t > v->f_acc ? (uint32_t)((t - v->f_acc + B - 1u) >> FWD_BSH) : 1u;
}

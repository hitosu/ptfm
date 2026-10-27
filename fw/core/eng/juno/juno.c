/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The Juno-106 voice: see juno.h. Fixed point: audio Q20 (1.0 = 1 << 20), coefficients Q24,
 * products in 64 bits (one instruction on the FM-1's core). The firmware parts (envelope, LFO,
 * VCF cutoff) follow KR-106's reconstruction of the Juno-106 voice CPU (D7811G) routines
 * integer for integer; the analog parts are this engine's own, simpler than KR-106's. */
#include "juno.h"
#include "juno_tables.h"                 /* made once from KR-106's models (its header says how) */
#include "../dx7/dx7.h"                  /* dx7_tanh, dx7_freq: MSFA's integer helpers */

enum { JF_SAW = 1, JF_PULSE = 2, JF_PWM_MAN = 4, JF_ENV_NEG = 8, JF_GATE = 16 };
enum { ENV_ATTACK, ENV_DECAY, ENV_RELEASE, ENV_DONE };
#define ENV_MAX 0x3FFF
#define Q20 (1 << 20)

static inline int32_t mul24(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 24); }

const uint32_t juno_tick_q16 = JUNO_TICK_Q16;

/* a one-pole low-pass, its state kept in 64 bits (Q44): at corners of a few Hz the 32-bit form
 * would get stuck on its rounding (a DC offset of up to 1 / g steps) */
static inline int32_t lp1(int64_t *acc, int32_t x, int32_t g_q24)
{
    *acc += (int64_t)(x - (int32_t)(*acc >> 24)) * g_q24;
    return (int32_t)(*acc >> 24);
}

/* ---------------------------------------------------------- noise --- */
/* one generator for all voices, as on the 106 (KR106Noise): white noise (the sum of 4 uniform
 * randoms) plus a flicker tilt, through the circuit's 5.3 kHz low-pass and 34 Hz high-pass. The
 * white noise also seeds the filters (see juno_render). */
static uint32_t noise_seed = 22222;
static int64_t noise_st[6];
static int32_t noise_buf[JUNO_N], noise_white[JUNO_N];

void juno_init(void)
{
    int i;
    noise_seed = 22222;
    for (i = 0; i < 6; i++)
        noise_st[i] = 0;
}

void juno_noise_get(juno_noise_t *n)
{
    int i;
    n->seed = noise_seed;
    for (i = 0; i < 6; i++)
        n->st[i] = noise_st[i];
}
void juno_noise_set(const juno_noise_t *n)
{
    int i;
    noise_seed = n->seed;
    for (i = 0; i < 6; i++)
        noise_st[i] = n->st[i];
}

/* in three passes a block, each with its filters' states in registers (in one, the six 64-bit states did not fit
 * and went to the stack and back every sample); the same arithmetic in the same order */
void juno_block(int tick)
{
    int i;
    uint32_t seed = noise_seed;
    int64_t s0 = noise_st[0], s1 = noise_st[1], s2 = noise_st[2], s3 = noise_st[3];
    (void)tick;
    for (i = 0; i < JUNO_N; i++) {       /* the white noise: 4 uniform randoms, each +-0.5 in Q20 */
        int32_t w;
        seed = seed * 196314165u + 907633515u;
        w = (int32_t)seed >> 12;
        seed = seed * 196314165u + 907633515u;
        w += (int32_t)seed >> 12;
        seed = seed * 196314165u + 907633515u;
        w += (int32_t)seed >> 12;
        seed = seed * 196314165u + 907633515u;
        w += (int32_t)seed >> 12;
        noise_white[i] = w;
    }
    noise_seed = seed;
    for (i = 0; i < JUNO_N; i++) {       /* the flicker tilt: + 1.5 x the mean of 4 low-passes */
        int32_t w = noise_white[i], fl = 0;
        fl += lp1(&s0, w, JUNO_NOISE_Q24[0]);
        fl += lp1(&s1, w, JUNO_NOISE_Q24[1]);
        fl += lp1(&s2, w, JUNO_NOISE_Q24[2]);
        fl += lp1(&s3, w, JUNO_NOISE_Q24[3]);
        noise_buf[i] = w + ((fl * 3) >> 3);
    }
    noise_st[0] = s0;
    noise_st[1] = s1;
    noise_st[2] = s2;
    noise_st[3] = s3;
    s0 = noise_st[4];
    s1 = noise_st[5];
    for (i = 0; i < JUNO_N; i++) {       /* the circuit's 5.3 kHz low-pass, 34 Hz high-pass */
        int32_t lp = lp1(&s0, noise_buf[i], JUNO_NOISE_Q24[4]);
        noise_buf[i] = lp - lp1(&s1, lp, JUNO_NOISE_Q24[5]);
    }
    noise_st[4] = s0;
    noise_st[5] = s1;
}

/* ------------------------------------------------- envelope (ADSR) --- */
/* KR106ADSR CalcDecay: the D7811G's 16 x 16 multiply from three 8 x 8 products (VL x CL left out) */
static uint16_t calc_decay(uint16_t value, uint16_t coeff)
{
    uint32_t vh = value >> 8, vl = value & 0xFFu, ch = coeff >> 8, cl = coeff & 0xFFu;
    return (uint16_t)(vh * ch + ((vh * cl) >> 8) + ((vl * ch) >> 8));
}

static void env_tick(juno_voice_t *v)   /* ADSR::Tick106 */
{
    switch (v->env_state) {
    case ENV_ATTACK: {
        uint32_t sum = (uint32_t)v->env + v->atk;
        if (sum >= ENV_MAX) {
            v->env = ENV_MAX;
            v->env_state = ENV_DECAY;
        } else {
            v->env = (uint16_t)sum;
        }
        break;
    }
    case ENV_DECAY:
        if (v->env > v->sus)
            v->env = (uint16_t)(calc_decay((uint16_t)(v->env - v->sus), v->dec) + v->sus);
        else
            v->env = v->sus;
        break;
    case ENV_RELEASE:
        v->env = calc_decay(v->env, v->rel);
        if (v->env == 0)
            v->env_state = ENV_DONE;
        break;
    }
}

/* ------------------------------------------------------------- LFO --- */
static void lfo_tick(juno_voice_t *v)   /* LFO::Tick106: a triangle 0..0x1FFF, its sign flipping each cycle */
{
    if (v->lfo_rising) {
        uint32_t sum = (uint32_t)v->lfo_acc + v->lfo_coeff;
        if (sum >= 0x2000u) {
            v->lfo_acc = 0x1FFF;
            v->lfo_rising = 0;
        } else {
            v->lfo_acc = (uint16_t)sum;
        }
    } else if (v->lfo_coeff > v->lfo_acc) {
        v->lfo_acc = 0;
        v->lfo_rising = 1;
        v->lfo_pol ^= 1u;
    } else {
        v->lfo_acc = (uint16_t)(v->lfo_acc - v->lfo_coeff);
    }
    if (v->in_hold) {                    /* the delay: a hold-off, then a ramp */
        uint32_t sum = (uint32_t)v->hold_acc + v->hold_inc;
        if (sum >= 0x4000u) {
            v->in_hold = 0;
            v->hold_acc = 0x4000;
        } else {
            v->hold_acc = (uint16_t)sum;
        }
        v->lfo_amp = 0;
    } else {
        uint32_t sum = (uint32_t)v->ramp_acc + v->ramp_inc;
        if (sum >= 1u << 16) {
            v->ramp_acc = 0xFFFF;
            v->lfo_amp = 255;
        } else {
            v->ramp_acc = (uint16_t)sum;
            v->lfo_amp = (uint8_t)(v->ramp_acc >> 8);
        }
    }
}

/* ------------------------------------------------------- VCF cutoff --- */
/* KR106VcfFreqJ106 calc_vcf_freq: the firmware's 16-bit arithmetic with its carry / borrow flag */
static uint16_t mul8x16_hi(uint32_t coeff, uint32_t value) { return (uint16_t)((coeff * value) >> 8); }

static uint16_t vcf_dac(const juno_voice_t *v, uint32_t lfo_signal, int lfo_neg)
{
    const uint8_t *p = v->p;
    uint32_t ea = (uint32_t)p[5] << 7, env = mul8x16_hi(2u * p[7], v->env);
    uint32_t pitch = (uint32_t)(v->pitch88 >> 2) + (v->pitch88 >> 3), c = 0x1680, kt = 2u * p[9];
    int under = 0;
#define VADD(b) do { ea += (b); if (ea > 0xFFFFu) under = 0; ea &= 0xFFFFu; } while (0)
#define VSUB(b) do { if ((uint32_t)(b) > ea) under = 1; ea = (ea - (b)) & 0xFFFFu; } while (0)
    if (!lfo_neg)
        VADD(lfo_signal);
    else
        VSUB(lfo_signal);
    if (!(v->flags & JF_ENV_NEG))
        VADD(env);
    else
        VSUB(env);
    if (pitch > c)                       /* key follow around middle C */
        VADD(mul8x16_hi(kt, pitch - c));
    else
        VSUB(mul8x16_hi(kt, c - pitch));
#undef VADD
#undef VSUB
    if (ea > 0x3FFFu)
        return under ? 0 : 0x3FFF;
    return (uint16_t)ea;
}

/* the VCA's target: the measured envelope -> gain curve (or the gate), x the levels */
static void vca_target(juno_voice_t *v)
{
    int32_t g;
    if (v->flags & JF_GATE) {
        g = v->env_state < ENV_RELEASE ? JUNO_VCA[256] : 0;
    } else {
        uint32_t x = ((uint32_t)v->env * 1020u) >> 8, i = x >> 8, f = x & 255u;
        g = JUNO_VCA[i] + (((JUNO_VCA[i + 1u] - JUNO_VCA[i]) * (int32_t)f) >> 8);
    }
    v->vca_target = (int32_t)(((int64_t)g * v->level_q12) >> 12);   /* (levels up to x 12.6: trims) */
}

/* one pass of the firmware loop: LFO, envelope, then from both the VCF cutoff, the DCO pitch
 * (LFO vibrato) and pulse width */
static void voice_tick(juno_voice_t *v)
{
    uint32_t tri, sig;
    int neg;
    int32_t lfo, lf;
    lfo_tick(v);
    env_tick(v);
    tri = v->lfo_acc;
    neg = v->lfo_pol && tri;
    sig = (((2u * v->p[8] * v->lfo_amp) >> 8) * tri) >> 9;   /* calc_vcf_lfo_signal */
    v->dac = vcf_dac(v, sig, neg);
    lfo = neg ? -(int32_t)tri : (int32_t)tri;                 /* -8191..8191 */
    /* vibrato: LFO x onset x depth (0..255 = 0..4 semitones), Q24 octaves (/ 2^21 ~ / (8191 x 255)) */
    lf = (int32_t)(((int64_t)lfo * v->lfo_amp * (int64_t)(v->dco_lfo * 21931)) >> 21);
    v->dphase = (uint32_t)dx7_freq(v->logfreq + v->bend + lf) << 8;   /* (+ the player's bend) */
    {   /* pulse width 50..95 %, LFO or manual; the 106's duty is inverted (KR106Oscillators) */
        int32_t pw = v->flags & JF_PWM_MAN ? v->pwm_q15 : (v->pwm_q15 * (8191 + lfo)) / 16382;
        uint32_t duty = 0x80000000u + (uint32_t)pw * 58982u;   /* 0.45 * 2^32 / 2^15 */
        v->pw = 0u - duty;
    }
    vca_target(v);
}

/* ------------------------------------------------------------ notes --- */
/* what the voice takes from its patch (also for a patch changed while it sounds, juno_patch) */
static void patch_set(juno_voice_t *v, const uint8_t *p)
{
    int res = p[6];
    v->p = p;
    v->atk = JUNO_ATK[p[11]];
    v->dec = JUNO_DECREL[p[12]];
    v->sus = (uint16_t)(p[13] ? (p[13] * 0x3F80u) / 127u : 16u);   /* (sustain 0: 0.001, as KR-106) */
    v->rel = JUNO_DECREL[p[14]];
    v->lfo_coeff = JUNO_LFOSPD[p[0]];
    v->hold_inc = JUNO_ATK[p[1]];
    v->ramp_inc = JUNO_LFORAMP[p[1] >> 4];
    v->dco_lfo = JUNO_DCOLFO[p[2]];
    v->pwm_q15 = (p[3] > 105 ? 105 : p[3]) * 32767 / 105;     /* J106: the slider's 105 is the top */
    v->flags = (uint8_t)((p[16] & 0x10 ? JF_SAW : 0) | (p[16] & 0x08 ? JF_PULSE : 0) | (p[17] & 1 ? JF_PWM_MAN : 0) |
                         (p[17] & 2 ? JF_ENV_NEG : 0) | (p[17] & 4 ? JF_GATE : 0));
    v->g_saw = v->flags & JF_SAW ? 19661 : 0;   /* KR-106 J106 mix: saw 0.6, pulse 0.5 (Q15) */
    v->g_pulse = v->flags & JF_PULSE ? 16384 : 0;
    v->g_sub = JUNO_SUB[p[15]];
    v->g_noise = JUNO_NOISE[p[4]];
    v->in_env = (v->g_saw + v->g_pulse + v->g_sub + v->g_noise) << 5;   /* the input's level, Q20 */
    v->k_q12 = JUNO_RESK[res];
    v->kfb_q12 = JUNO_FBS[res];
    v->kfbinv_q12 = (1 << 24) / v->kfb_q12;
    v->res_off = JUNO_RESOFF[res];
    v->level_q12 = (JUNO_LEVEL[p[10]] * v->gain_q12) >> 12;
    v->logfreq = 50857777 + v->note * 1398101 + ((p[16] & 4) ? (1 << 24) : (p[16] & 2) ? 0 : -(1 << 24));
}

void juno_note_on(juno_voice_t *v, const uint8_t *p, int note, int32_t level_q12)
{
    int busy = juno_busy(v), res = p[6];
    v->gain_q12 = level_q12;
    v->note = note;
    v->bend = 0;
    patch_set(v, p);
    v->pitch88 = (uint16_t)((note < 0 ? 0 : note > 127 ? 127 : note) << 8);
    if (!v->gate) {                      /* the LFO's delay restarts when no key was down */
        v->hold_acc = 0;
        v->in_hold = v->hold_inc < ENV_MAX;
        v->ramp_acc = 0;
        v->lfo_amp = 0;
    }
    v->gate = 1;
    v->env_state = ENV_ATTACK;           /* from the level it has: a retrigger does not click */
    env_tick(v);                         /* the first attack step right away (ADSR::NoteOn) */
    v->dac = vcf_dac(v, 0, 0);
    v->dphase = (uint32_t)dx7_freq(v->logfreq) << 8;
    if (!busy) {                         /* a voice at rest starts its DCO and CV smoothing afresh */
        v->phase = 0;
        v->sub = 0;
        v->vca = 0;
        v->dac_s = v->dac << 8;
        if (JUNO_OSC[res]) {             /* a filter that self-oscillates does so between notes too: */
            int32_t a = JUNO_OSC[res];   /* its steady state (k = 4: each stage 45 degrees, x 1 / sqrt 2) */
            v->s[0] = -2 * a;
            v->s[1] = 0;
            v->s[2] = a;
            v->s[3] = a;
        }
    }
    {
        int32_t pw = v->flags & JF_PWM_MAN ? v->pwm_q15 : v->pwm_q15 / 2;
        v->pw = 0u - (0x80000000u + (uint32_t)pw * 58982u);
    }
    vca_target(v);
}

/* a sounding voice's patch changed (the player's parameter commands: p is its own copy), its level */
void juno_patch(juno_voice_t *v, const uint8_t *p)
{
    patch_set(v, p);
    vca_target(v);
}

/* a legato note (PT/FM's LEG): the VCF's key follow moves to note (the pitch comes from the bend; nothing starts again) */
void juno_key(juno_voice_t *v, int note)
{
    v->pitch88 = (uint16_t)((note < 0 ? 0 : note > 127 ? 127 : note) << 8);
}

void juno_level(juno_voice_t *v, int32_t level_q12)
{
    v->gain_q12 = level_q12;
    if (!v->p)
        return;
    v->level_q12 = (JUNO_LEVEL[v->p[10]] * level_q12) >> 12;
    vca_target(v);
}

void juno_note_off(juno_voice_t *v)
{
    v->gate = 0;
    if (v->p)
        v->env_state = ENV_RELEASE;
}

void juno_cut(juno_voice_t *v)
{
    v->gate = 0;
    v->env_state = ENV_DONE;
    v->env = 0;
    v->vca = v->vca_target = 0;
    v->s[0] = v->s[1] = v->s[2] = v->s[3] = 0;
}

int juno_busy(const juno_voice_t *v) { return v->p && (v->env_state != ENV_DONE || v->vca > 4); }

void juno_idle(juno_voice_t *v, int tick)
{
    if (tick && v->p)
        lfo_tick(v);
}

/* a block of a sounding voice's time without its sound (the player's parked voices): the firmware loop, the
 * cutoff's and the VCA's smoothing (to where they head) and the oscillator go on; the filter, DC and HPF states
 * stay as they were */
void juno_skip(juno_voice_t *v, int tick)
{
    if (!v->p)
        return;
    if (tick)
        voice_tick(v);
    v->dac_s = v->dac << 8;
    v->vca = v->vca_target;
    {
        uint64_t to = (uint64_t)v->phase + (uint64_t)v->dphase * JUNO_N;   /* (the sub flips at each wrap) */
        v->sub ^= (uint8_t)((to >> 32) & 1u);
        v->phase = (uint32_t)to;
    }
}

/* ----------------------------------------------------------- render --- */
/* polyBLEP (2 points): the step's residual at t frames after (n >= 0) or before an edge, Q16. The
 * squares in 64 bits: n and m reach 65536, and n * n in an int overflowed above 46340 (undefined:
 * gcc wrapped it negative, a glitch at the edge; clang's optimiser happened to get it right) */
static inline int32_t blep_after(uint32_t t, uint32_t recip)
{
    int32_t n = (int32_t)(((t >> 16) * recip) >> 14);
    return 2 * n - (int32_t)(((int64_t)n * n) >> 16) - 65536;
}
static inline int32_t blep_before(uint32_t t, uint32_t recip)
{
    int32_t m = 65536 - (int32_t)((((0u - t) >> 16) * recip) >> 14);
    return (int32_t)(((int64_t)m * m) >> 16);
}

#define K_IN 1090519                     /* KR106VCF kInputRatio 0.065, Q24 */
#define K_DIRECT 2382364                 /* kDirectGain 0.142 */
#define K_OUT 35185                      /* output make-up 8.59, Q12 */

/* The render keeps the arithmetic of a plain per-frame loop (every sample the same to the bit) in a form the
 * FM-1's core runs faster (a taken jump costs it ~6 cycles, a 64-bit multiply ~5-10, a load ~4):
 * - in passes, each with its state in registers (in one loop, the oscillators', the capacitors' and the
 *   filter's state did not fit, and went to the stack and back every frame): the oscillators for the whole
 *   block (dco_run), then for each 16 frames the coupling capacitors and the seed noise, then the VCF
 *   (vcf_flat, vcf_res);
 * - the oscillators take the polyBLEP code only at an edge's frames (dco_edge, a few in a hundred); between
 *   edges the pulse's sign and the sub stay, so pulse, sub and the saw's -1 add a level (dco_level) that
 *   changes only there;
 * - 16 frames whose resonance k is 0 (no RES, or a cutoff where the 106 has none) skip the feedback path,
 *   which then adds nothing, and 1 / (1 + kG), which is then 1;
 * - the output make-up, the VCA and the HPF switch run after the filter, one loop for each HPF setting. */

/* the oscillators' level between edges: (pulse x g_pulse + sub x g_sub - g_saw) x Q20 >> 15 */
static inline int32_t dco_level(const juno_voice_t *v, uint32_t phase, uint32_t pw)
{
    return (-v->g_saw + (phase < pw ? -v->g_pulse : v->g_pulse) + (v->sub ? -v->g_sub : v->g_sub)) * 32;
}

/* DCO: saw, pulse and sub from one phase, and the noise, at a frame where polyBLEP works (the phase's wrap
 * or the pulse's rising edge, a frame either side); flips the sub at the wrap */
static __attribute__((noinline)) int32_t dco_edge(juno_voice_t *v, uint32_t phase, uint32_t dt, uint32_t recip,
                                                  uint32_t pw, int32_t nb)
{
    int32_t saw, pulse, subv, b0 = 0, b1 = 0;
    uint32_t t2;
    int64_t mix;
    if (phase < dt)
        v->sub ^= 1u;
    saw = (int32_t)(phase >> 11) - Q20;
    if (phase < dt)
        b0 = blep_after(phase, recip);
    else if (phase > 0u - dt)
        b0 = blep_before(phase, recip);
    saw -= b0 << 4;
    pulse = phase < pw ? -Q20 : Q20;
    pulse -= b0 << 4;
    t2 = phase - pw;
    if (t2 < dt)
        b1 = blep_after(t2, recip);
    else if (t2 > 0u - dt)
        b1 = blep_before(t2, recip);
    pulse += b1 << 4;
    subv = (65536 - (b0 < 0 ? -b0 : b0)) << 4;
    if (v->sub)
        subv = -subv;
    mix = (int64_t)saw * v->g_saw + (int64_t)pulse * v->g_pulse + (int64_t)subv * v->g_sub + (int64_t)nb * v->g_noise;
    return (int32_t)(mix >> 15);
}

/* the block's DCO and noise into x[] */
static __attribute__((noinline)) void dco_run(juno_voice_t *v, int32_t *x)
{
    uint32_t phase = v->phase, dt = v->dphase, pw = v->pw, dt16, recip, off0, off1, win;
    int32_t level, g_saw = v->g_saw, g_noise = v->g_noise;
    const int32_t *nb = noise_buf;
    int i;
    dt16 = dt >> 16 ? dt >> 16 : 1u;
    recip = (1u << 30) / dt16;
    /* polyBLEP works at a frame where phase < dt or phase > -dt (the wrap), or the same of phase - pw (the
     * pulse's edge); for 1 <= dt <= 2^31 each is one unsigned compare, phase + dt - 1 < 2 dt - 1. Any other
     * dt (a pitch above half the sample rate) takes the edge code at every frame: phase + 0 and phase + 1
     * cannot both be 0xFFFFFFFF */
    if (dt - 1u < 0x80000000u) {
        off0 = dt - 1u;
        off1 = v->g_pulse ? dt - 1u - pw : off0;   /* (no pulse: its edge changes nothing, the wrap's window only) */
        win = 2u * dt - 1u;
    } else {
        off0 = 0;
        off1 = 1u;
        win = 0xFFFFFFFFu;
    }
    level = dco_level(v, phase, pw);
    for (i = 0; i < JUNO_N; i++) {
        uint32_t q0, q1;
        phase += dt;
        q0 = phase + off0;
        q1 = phase + off1;
        if (__builtin_expect((q0 < q1 ? q0 : q1) < win, 0)) {   /* (one test: either window) */
            x[i] = dco_edge(v, phase, dt, recip, pw, nb[i]);
            level = dco_level(v, phase, pw);   /* (up to the next edge) */
        } else {                         /* the saw's ramp and the noise; pulse and sub in the level */
            x[i] = (int32_t)(((int64_t)(int32_t)(phase >> 11) * g_saw + (int64_t)nb[i] * g_noise) >> 15) + level;
        }
    }
    v->phase = phase;
}

/* 16 frames of x[]: the coupling capacitors (KR-106: DCO -> VCF 0.16 Hz, VCF -> VCA 1.59 Hz, as one), then the
 * filter's seed noise */
static inline __attribute__((always_inline)) void dc_seed(juno_voice_t *v, int32_t *x, const int32_t *nw,
                                                          int32_t seed)
{
    int64_t dc = v->dc;
    int i;
    for (i = 0; i < 16; i++) {           /* (one frame a turn: two, interleaved, spilled more than the jump costs) */
        int32_t xx = x[i];
        xx -= lp1(&dc, xx, JUNO_DC_Q24);
        x[i] = xx + (int32_t)(((int64_t)nw[i] * seed) >> 24);
    }
    v->dc = dc;
}

/* the ladder's four stages, one frame: the input u, the last stage's output */
static inline __attribute__((always_inline)) int32_t ladder(int32_t *s0, int32_t *s1, int32_t *s2, int32_t *s3,
                                                            int32_t u, int32_t g1)
{
    int32_t vv, yy;
    vv = mul24(u - *s0, g1);
    yy = *s0 + vv;
    *s0 = yy + vv;
    vv = mul24(yy - *s1, g1);
    yy = *s1 + vv;
    *s1 = yy + vv;
    vv = mul24(yy - *s2, g1);
    yy = *s2 + vv;
    *s2 = yy + vv;
    vv = mul24(yy - *s3, g1);
    yy = *s3 + vv;
    *s3 = yy + vv;
    return yy;
}

/* VCF: 4-pole TPT ladder (KR106VCF); 16 frames of x[] in place. Without resonance (k = 0): */
static __attribute__((noinline)) void vcf_flat(juno_voice_t *v, int32_t *x, const int32_t *nw, int32_t seed)
{
    int32_t s0, s1, s2, s3, g1 = v->f_g1;
    int i;
    dc_seed(v, x, nw, seed);
    s0 = v->s[0];
    s1 = v->s[1];
    s2 = v->s[2];
    s3 = v->s[3];
    for (i = 0; i < 16; i++)
        x[i] = ladder(&s0, &s1, &s2, &s3, mul24(x[i], K_DIRECT), g1);
    v->s[0] = s0;
    v->s[1] = s1;
    v->s[2] = s2;
    v->s[3] = s3;
}

/* the same with the resonance feedback through the OTA's tanh (1 / kfb read from v in the loop: as a local, the
 * compiler made t x 1 / kfb >> 16 a 64 x 64 multiply) */
static __attribute__((noinline)) void vcf_res(juno_voice_t *v, int32_t *x, const int32_t *nw, int32_t seed)
{
    const int32_t *exp2tab, *tanhtab;    /* for MSFA's tanh, inline (dx7_tanh_tab) */
    int32_t s0, s1, s2, s3, g1 = v->f_g1, g1_2 = v->f_g1_2, g1_3 = v->f_g1_3, om = v->f_om, kfb = v->kfb_q12;
    int32_t fk = v->f_k, invden = v->f_invden;
    int i;
    dc_seed(v, x, nw, seed);
    dx7_tables(&exp2tab, &tanhtab);
    s0 = v->s[0];
    s1 = v->s[1];
    s2 = v->s[2];
    s3 = v->s[3];
    for (i = 0; i < 16; i++) {
        int32_t xx = x[i], sum, diff, t, ota, u;
        sum = (int32_t)(((int64_t)s0 * g1_3 + (int64_t)s1 * g1_2 + (int64_t)s2 * g1) >> 24) + s3;
        sum = mul24(sum, om);
        diff = mul24(xx, K_IN) - sum;
        t = dx7_tanh_tab((int32_t)(((int64_t)diff * kfb) >> 8), exp2tab, tanhtab);
        ota = (int32_t)(((int64_t)t * v->kfbinv_q12) >> 16);
        u = mul24(xx, K_DIRECT) + mul24(fk, ota);   /* (|u| < 2^23 before 1 / (1 + kG): int32) */
        u = (int32_t)(((int64_t)u * invden) >> 16);
        x[i] = ladder(&s0, &s1, &s2, &s3, u, g1);
    }
    v->s[0] = s0;
    v->s[1] = s1;
    v->s[2] = s2;
    v->s[3] = s3;
}

/* the output make-up x the output scale, then the VCA, its gain's CV smoothed (687 us): one frame (steady: the
 * smoothing's step is 0 throughout the block, see juno_render) */
static inline __attribute__((always_inline)) int32_t vca_run(const juno_voice_t *v, int32_t *vca, int32_t y,
                                                             const int steady)
{
    y = mul24(y, v->f_out);              /* (y x K_OUT >> 12: a constant here costs 6 instructions) */
    if (!steady)
        *vca += ((v->vca_target - *vca) * JUNO_VCA_SLEW_Q15) >> 15;
    return (int32_t)(((int64_t)y * *vca) >> 15);
}

/* the HPF switch's bass boost: direct + gain x two low shelves; one frame */
static inline __attribute__((always_inline)) int32_t bass_boost(int64_t *hp0, int64_t *hp1, int32_t y)
{
    int32_t b = y, l;
    l = lp1(hp0, b, JUNO_BASSBOOST_Q24[3]);
    b = l + mul24(b - l, JUNO_BASSBOOST_Q24[2]);
    l = lp1(hp1, b, JUNO_BASSBOOST_Q24[5]);
    b = l + mul24(b - l, JUNO_BASSBOOST_Q24[4]);
    return mul24(y, JUNO_BASSBOOST_Q24[0]) + mul24(b, JUNO_BASSBOOST_Q24[1]);
}

/* the filter's output y through the VCA and the HPF switch (mode 0 the bass boost, 1 flat, 2 and 3 the HPF), added to
 * out (steady: see juno_render) */
static inline __attribute__((always_inline)) void vca_hpf(juno_voice_t *v, const int32_t *y, int32_t *out, int mode,
                                                          const int steady)
{
    int32_t vca = v->vca;
    int i;
    if (mode == 1) {                     /* (two frames a turn: a taken jump costs ~6 cycles) */
        for (i = 0; i < JUNO_N; i += 2) {
            out[i] += vca_run(v, &vca, y[i], steady) >> 5;   /* Q20 -> 16-bit scale (1.0 = 32768) */
            out[i + 1] += vca_run(v, &vca, y[i + 1], steady) >> 5;
        }
    } else if (mode >= 2) {              /* the HPF at 236 or 754 Hz */
        int64_t hp = v->hp[0];
        int32_t g = mode == 2 ? JUNO_HPF2_Q24 : JUNO_HPF3_Q24, yy;
        for (i = 0; i < JUNO_N; i += 2) {
            yy = vca_run(v, &vca, y[i], steady);
            yy -= lp1(&hp, yy, g);
            out[i] += yy >> 5;
            yy = vca_run(v, &vca, y[i + 1], steady);
            yy -= lp1(&hp, yy, g);
            out[i + 1] += yy >> 5;
        }
        v->hp[0] = hp;
    } else {
        int64_t hp0 = v->hp[0], hp1 = v->hp[1];
        for (i = 0; i < JUNO_N; i += 2) {
            out[i] += bass_boost(&hp0, &hp1, vca_run(v, &vca, y[i], steady)) >> 5;
            out[i + 1] += bass_boost(&hp0, &hp1, vca_run(v, &vca, y[i + 1], steady)) >> 5;
        }
        v->hp[0] = hp0;
        v->hp[1] = hp1;
    }
    v->vca = vca;
}

void juno_render(juno_voice_t *v, int32_t *out, int tick)
{
    int32_t y[JUNO_N];
    int j, mode;
    if (!v->p)
        return;
    mode = 3 - ((v->p[17] >> 3) & 3);
    if (tick)
        voice_tick(v);
    dco_run(v, y);
    for (j = 0; j < JUNO_N; j += 16) {
        int32_t code, g1, g1_2, G, k, seed, energy, *s = v->s;
        uint32_t e;
        /* KR106VCF's seed: white noise into the filter at 0.01 / (1 + 1000 x the signal's level),
         * which starts the self-oscillation of patches without oscillators (at 1x: half of its
         * 4x level, for the same noise density in the audio band) */
        energy = (s[0] < 0 ? -s[0] : s[0]) + (s[1] < 0 ? -s[1] : s[1]) + (s[2] < 0 ? -s[2] : s[2]) +
                 (s[3] < 0 ? -s[3] : s[3]);
        energy = energy > v->in_env ? energy : v->in_env;
        seed = 21475328 / (1024 + (energy >> 10) * 1000);   /* Q24 */
        /* the cutoff CV, smoothed (522 us), then the coefficients for these 16 frames */
        v->dac_s += (int32_t)(((int64_t)((v->dac << 8) - v->dac_s) * JUNO_VCF_SLEW_Q15) >> 15);
        code = (v->dac_s >> 8) + v->res_off;
        code = code < 0 ? 0 : code > 16383 ? 16383 : code;
        e = JUNO_VCF[code >> 2];
        g1 = (int32_t)(e & 0xFFFFFFu);
        k = v->k_q12;
        if (k && (e >> 24) != 255u)
            k = (k * (int32_t)(e >> 24)) / 255;
        v->f_g1 = g1;                    /* (for vcf_flat / vcf_res: see juno.h) */
        if (k) {
            g1_2 = mul24(g1, g1);
            G = mul24(g1_2, g1_2);
            v->f_g1_2 = g1_2;
            v->f_g1_3 = mul24(g1_2, g1);
            v->f_om = (1 << 24) - g1;
            v->f_invden = (1 << 30) / ((1 << 14) + (int32_t)(((int64_t)k * G) >> 22));   /* 1 / (1 + kG), Q16 */
            v->f_k = k << 12;            /* Q24 */
            vcf_res(v, y + j, noise_white + j, seed);
        } else {
            vcf_flat(v, y + j, noise_white + j, seed);
        }
    }
    v->f_out = (int32_t)(((int64_t)K_OUT * JUNO_OUT_Q16) >> 4);   /* Q24, x the output scale */
    {   /* the VCA's smoothing steps by (target - vca) x SLEW >> 15: 0 at every frame when that is 0 at the first (the
         * target stays for the block), as when a held note's gain has settled */
        int32_t d = v->vca_target - v->vca;
        if (d >= 0 && (d * JUNO_VCA_SLEW_Q15) >> 15 == 0)
            vca_hpf(v, y, out, mode, 1);
        else
            vca_hpf(v, y, out, mode, 0);
    }
}

/* ---------------------------------------------------------- chorus --- */
int juno_chorus_mode(const uint8_t *p) { return p[16] & 0x20 ? 0 : p[16] & 0x40 ? 1 : 2; }

/* the BBD chorus: a delay line, read at two taps whose delays a triangle LFO moves in opposite
 * directions (left, right), each through the BBD's low-pass; dry + wet per side */

/* one tap at d (8.8 frames back from w) through its low-pass lp: the wet sample */
static inline __attribute__((always_inline)) int32_t chorus_tap(const int32_t *buf, uint32_t w, int32_t d, int32_t *lp)
{
    uint32_t at = w - (uint32_t)(d >> 8), f = (uint32_t)d & 255u;
    int32_t a = buf[at & 255u], b = buf[(at - 1u) & 255u], wet;
    wet = a + (int32_t)(((int64_t)(b - a) * f) >> 8);
    *lp += (int32_t)(((int64_t)(wet - *lp) * JUNO_CH_LP_Q15) >> 15);
    return *lp;
}

/* (its state in locals: in c, every store to l / r made the compiler read it again) */
void juno_chorus(juno_chorus_t *c, int mode, const int32_t *in, int32_t *l, int32_t *r)
{
    int32_t depth = mode == 1 ? JUNO_CH_DEPTH1 : JUNO_CH_DEPTH2, lp0 = c->lp[0], lp1 = c->lp[1], *buf = c->buf;
    uint32_t rate = mode == 1 ? JUNO_CH_RATE1 : JUNO_CH_RATE2, w = c->w, lfo = c->lfo;
    int i;
    for (i = 0; i < JUNO_N; i++) {
        int32_t x = in[i], tri, dd;
        buf[w & 255u] = x;
        lfo += rate;
        tri = (int32_t)(lfo >> 16);      /* 0..65535 */
        tri = tri < 32768 ? 2 * tri - 32768 : 98303 - 2 * tri;   /* -32768..32767 */
        dd = (depth * tri) >> 15;
        l[i] += (int32_t)(((int64_t)x * JUNO_CH_DRY_Q12 +
                           (int64_t)chorus_tap(buf, w, JUNO_CH_CENTER + dd, &lp0) * JUNO_CH_WET_Q12) >> 12);
        r[i] += (int32_t)(((int64_t)x * JUNO_CH_DRY_Q12 +
                           (int64_t)chorus_tap(buf, w, JUNO_CH_CENTER - dd, &lp1) * JUNO_CH_WET_Q12) >> 12);
        w++;
    }
    c->w = w;
    c->lfo = lfo;
    c->lp[0] = lp0;
    c->lp[1] = lp1;
}

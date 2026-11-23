/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* A Juno-60 patch as the closest Juno-106 one (specs/editor.md, Juno-60 patches as Juno-106 ones): the sound kept, not
 * the slider. The tables and the arrays the rules read are juno60_tables.js's (made by editor/tools/juno60/, whose
 * convert.h is this file's twin: the same arithmetic in the same order, its answers for made-up patches in its
 * vectors.json, which the tests compare).
 *   ED.juno60.convert(v) -> { p: Uint8Array(18) (the Juno-106 patch: 16 sliders in its order, sw1, sw2), pwmEnv }
 *   v: conv_juno.js's from60(): the 16 tape values (0-255) by name, env_polarity, pwm_source (0 LFO, 1 MAN, 2 ENV), hpf
 *   (the tape's 0-3: 122 Hz, FLAT, 269 Hz, 571 Hz), vca_gate, lfo_trigger, chorus1, pulse, saw, sub_on, chorus2,
 *   chorus_off. pwmEnv: PWM SOURCE ENV that mattered (PULSE on, DCO PWM above 0): the closest kept. */
(function (ED) {
"use strict";
const ENV_MAX = 0x3FFF;

/* the cutoff (log2 Hz x 10000) at a DAC code, and held between 20 Hz and the engine's cap */
const lhz = (T, code) => T.HZ106[(code < 0 ? 0 : code > 0x3FFF ? 0x3FFF : code) >> 2];
const held = (T, x) => x < T.FLOOR ? T.FLOOR : x > T.CAP ? T.CAP : x;

/* VCF ENV: the movement at the envelope's peak, from the patch's own cutoff, at its polarity's scale */
function vcfEnv(T, v, ea0) {
    const l60 = T.HZ60[v.vcf_freq], m60 = v.env_polarity ? -T.ENV60N[v.vcf_env] : T.ENV60P[v.vcf_env];
    const mov60 = held(T, l60 + m60) - held(T, l60), base = held(T, lhz(T, ea0));
    let best = 0, be = 1 << 30;
    for (let b = 0; b < 128; b++) {
        const e = (2 * b * ENV_MAX) >> 8;                       /* (mul8x16_hi(2 x VCF ENV, ENV_MAX)) */
        const ea = !v.env_polarity ? (ea0 + e > 0x3FFF ? 0x3FFF : ea0 + e) : (e > ea0 ? 0 : ea0 - e);
        const err = Math.abs(mov60 - (held(T, lhz(T, ea)) - base));
        if (err < be) { be = err; best = b; }
    }
    return best;
}
/* VCF LFO: the swing from the LFO's peak down to its trough, at the patch's own cutoff */
function vcfLfo(T, v, ea0) {
    const l60 = T.HZ60[v.vcf_freq], d = T.VLFO60[v.vcf_lfo];
    const sw60 = held(T, l60 + d) - held(T, l60 - d);
    let best = 0, be = 1 << 30;
    for (let b = 0; b < 128; b++) {
        const sig = (((2 * b * 255) >> 8) * 8191) >> 9;          /* (the LFO's peak, its onset full) */
        const up = ea0 + sig > 0x3FFF ? 0x3FFF : ea0 + sig, dn = sig > ea0 ? 0 : ea0 - sig;
        const err = Math.abs(sw60 - (held(T, lhz(T, up)) - held(T, lhz(T, dn))));
        if (err < be) { be = err; best = b; }
    }
    return best;
}
/* VCF KYBD: the cutoff two octaves up and down (C6, C2) from C4, at the patch's own cutoff */
function vcfKybd(T, v, ea0) {
    const l60 = T.HZ60[v.vcf_freq], k = T.KBD60[v.vcf_kybd], b60 = held(T, l60);
    const up60 = held(T, l60 + k) - b60, dn60 = b60 - held(T, l60 - k), base = held(T, lhz(T, ea0));
    let best = 0, be = 1 << 30;
    for (let b = 0; b < 128; b++) {
        const m = (2 * b * 2304) >> 8;                          /* (mul8x16_hi(2 x KYBD, 24 semitones x 96)) */
        const up = ea0 + m > 0x3FFF ? 0x3FFF : ea0 + m, dn = m > ea0 ? 0 : ea0 - m;
        const err = Math.abs(up60 - (held(T, lhz(T, up)) - base)) + Math.abs(dn60 - (base - held(T, lhz(T, dn))));
        if (err < be) { be = err; best = b; }
    }
    return best;
}
/* a note's first second on the Juno-60's envelope, stepped by the ms (its attack toward 1.2 up to the peak, its decay
 * toward the sustain): its mean level, and its level at the end (where a note is usually let go) */
function firstSecond(T, v) {
    const a = T.ACOEF[v.env_a], d = T.DCOEF[v.env_d];
    let s = v.env_s / 255, env = 0, sum = 0, attack = true;
    if (s < 0.001) s = 0.001;
    for (let i = 0; i < 1000; i++) {
        if (attack) { env = env + (1.2 - env) * a; if (env >= 1) { env = 1; attack = false; } }
        else env = env + (s - env) * d;
        sum = sum + env;
    }
    return { mean: sum / 1000, last: env };
}
/* ENV R, from the level a note has a second in: with VCA MODE ENV the time the sound takes to fall 20 dB through each
 * VCA (what is heard); with GATE (the VCA shut at once, the envelope moving the filter alone) the envelope's time to a
 * tenth of its level */
function envR(T, v) {
    const E = 1000 * 10000, j = Math.min(32, Math.round(firstSecond(T, v).last * 32));
    const t60 = T.R60TAU[v.env_r] * (v.vca_gate ? T.RRAT60[j] : T.RSND60[j]), r106 = v.vca_gate ? T.R106ENV : T.RSND106;
    let best = 0, be = Infinity;
    for (let b = 0; b < 128; b++) {
        const t106 = r106[128 * j + b] * 1000000;          /* (0.1 ms -> us x 10000) */
        const a = (t60 + E) / (t106 + E), c = (t106 + E) / (t60 + E), err = a > c ? a : c;
        if (err < be) { be = err; best = b; }
    }
    return best;
}
/* PWM SOURCE ENV: MAN, the width the envelope gives on average over a note's first second */
function pwmEnv(T, v) {
    const target = v.dco_pwm / 255 * firstSecond(T, v).mean;
    let best = 0, be = Infinity;
    for (let b = 0; b < 106; b++) {
        const err = Math.abs(target - T.Q15[b] / 32768);
        if (err < be) { be = err; best = b; }
    }
    return best;
}

function convert(v) {
    const T = ED.juno60tab, p = new Uint8Array(18);
    p[0] = T.LFO_RATE[v.lfo_rate];
    p[1] = T.LFO_DELAY[v.lfo_delay];
    p[2] = T.DCO_LFO[v.dco_lfo];
    const pe = v.pwm_source === 2 && !!v.pulse && v.dco_pwm > 0;
    p[3] = pe ? pwmEnv(T, v) : T.DCO_PWM[v.dco_pwm];
    p[4] = T.DCO_NOISE[v.dco_noise];
    p[5] = T.VCF_FREQ[v.vcf_freq];
    p[6] = T.VCF_RES[v.vcf_res];
    const ea0 = p[5] << 7;
    p[7] = vcfEnv(T, v, ea0);
    p[8] = vcfLfo(T, v, ea0);
    p[9] = vcfKybd(T, v, ea0);
    p[10] = T.VCA_LEVEL[v.vca_level];
    p[11] = (v.vca_gate ? T.ENV_A_GATE : T.ENV_A_ENV)[v.env_a];
    p[12] = T.ENV_D[v.env_d];
    p[13] = (v.vca_gate ? T.ENV_S_GATE : T.ENV_S_ENV)[v.env_s];
    p[14] = envR(T, v);
    p[15] = v.sub_on ? T.DCO_SUB[v.dco_sub] : 0;          /* (SUB off: 0) */
    /* RANGE 8'; CHORUS I and II both: II, neither: off; LFO TRIGGER dropped (the LFO runs as with AUTO) */
    const chorus = v.chorus1 && v.chorus2 ? 2 : v.chorus1 ? 1 : v.chorus2 ? 2 : 0;
    p[16] = 0x02 | (v.pulse ? 0x08 : 0) | (v.saw ? 0x10 : 0) | (chorus === 0 ? 0x20 : chorus === 1 ? 0x40 : 0);
    /* the HPF: FLAT to FLAT, 269 Hz to 236, 571 Hz to 754, 122 Hz as the check found closer; BASS BOOST never */
    const hpf = v.hpf === 0 ? T.HPF_122 : v.hpf === 1 ? 1 : v.hpf;
    const man = v.pwm_source !== 0 ? 1 : 0;               /* (LFO; MAN, and ENV's rule) */
    p[17] = man | (v.env_polarity ? 2 : 0) | (v.vca_gate ? 4 : 0) | (3 - hpf) << 3;
    return { p, pwmEnv: pe };
}

ED.juno60 = { convert };
})(globalThis.ED = globalThis.ED || {});

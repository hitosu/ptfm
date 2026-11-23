/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground, sound: the Juno-106 (after KR-106's models: the voice CPU's envelope, LFO and
 * VCF cutoff routines; a DCO of saw, pulse with PWM, a sub one octave down and the shared noise source;
 * a 4-pole ladder with its resonance through a tanh; the VCA's measured curve; the HPF's four
 * positions; chorus I and II), the TB-303 (after Open303: saw to square, the diode ladder with its
 * feedback high-pass, the main envelope, the accent's RC, the amp envelope and its declicker, slide)
 * and the sampler (START, END, a sustain LOOP forward or ping-pong, BASE NOTE, TUNE, an envelope as a
 * DX7 operator's). The Juno-106 and the TB-303 follow the firmware's engines step by step, in plain
 * JS numbers (the voice CPU's integer routines as integers), so they sound and measure as the device
 * does. Each engine's parameter commands: E.params. */
PTFM.dsp = (PTFM.dsp || []).concat(function (E) {
    "use strict";
    const SR = E.SR, TWO_PI = Math.PI * 2;
    /* the engines' output scales: each engine's reference sound comes to -20 dB RMS (its loudest 300 ms)
     * at the voice's output (specs/files/instruments.md, LEVEL), whatever any data does; an instrument's
     * LEVEL then only says how it differs from its engine's plain sound. tests/playground/sound.js
     * measures the four reference sounds */
    E.GAIN = { DX7: .0707, JUNO: 4990 / 65536, TB303: 24956 / 65536, SAMPLER: .1423 };

    /* polyBLEP: a band-limited step, t the phase in cycles, dt the step */
    function blep(t, dt) {
        if (t < dt) { t /= dt; return t + t - t * t - 1; }
        if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
        return 0;
    }
    const onepole = hz => 1 - Math.exp(-TWO_PI * hz / SR);      /* y += (x - y) g */

    /* ------------------------------------------------------------------ Juno-106 --- */
    /* its parameters (specs/files/commands.md): the 16 sliders in the Juno-106's order, then the switches */
    const JUNO_SLIDERS = [["lfo", "rate"], ["lfo", "delay"], ["dco", "lfo"], ["dco", "pwm"], ["dco", "noise"], ["vcf", "freq"],
                          ["vcf", "res"], ["vcf", "env"], ["vcf", "lfo"], ["vcf", "kybd"], ["vca", "level"], ["env", "a"],
                          ["env", "d"], ["env", "s"], ["env", "r"], ["dco", "sub"]];
    const pickOf = (list, v) => list[Math.min(v, list.length - 1)];
    E.params.JUNO = function (p, c, v) {
        if (c >= 0x40 && c <= 0x4F) { const [g, k] = JUNO_SLIDERS[c - 0x40]; p[g][k] = Math.min(127, v); return; }
        if (c === 0x50) p.dco.range = pickOf(["16'", "8'", "4'"], v);
        else if (c === 0x51) p.dco.pulse = !!v;
        else if (c === 0x52) p.dco.saw = !!v;
        else if (c === 0x53) p.chorus = pickOf(["off", "I", "II"], v);
        else if (c === 0x54) p.dco.pwm_source = v ? "manual" : "lfo";
        else if (c === 0x55) p.vcf.env_polarity = v ? "-" : "+";
        else if (c === 0x56) p.vca.mode = v ? "gate" : "env";
        else if (c === 0x57) p.hpf = Math.min(3, v);
    };

    /* the tables, after KR-106's measurements of the hardware and its reading of the voice CPU's ROM */
    const ENV_MAX = 0x3FFF, ATTACK = 0, DECAY = 1, RELEASE = 2, DONE = 3;
    const J = (() => {
        const t = {}, sliders = f => Array.from({ length: 128 }, (_, b) => f(b));
        t.ATK = sliders(b => {                                      /* the attack's step a loop pass */
            const s = b / 127;
            if (s < .003937) return ENV_MAX;
            if (s <= .5) return Math.floor(8192 / (s * 127) + .5);
            if (s <= .681102) return Math.floor(305.03 - 352.26 * s + .5);
            if (s <= .846457) return Math.floor(194.74 - 190.50 * s + .5);
            if (s <= .956693) return Math.floor(86.37 - 62.52 * s + .5);
            return Math.max(Math.floor(148 - 127 * s + .5), 1);
        });
        t.DECREL = [0x1000];                                        /* decay / release: the factor a pass */
        [[4, 0x2000], [1, 0x1000], [10, 0x0800], [28, 0x0080], [22, 0x000C], [58, 0x0004], [4, 0x0001]].forEach(([n, s]) => {
            for (let k = 0; k < n; k++) t.DECREL.push((t.DECREL[t.DECREL.length - 1] + s) & 0xFFFF);
        });
        t.LFOSPD = [0x0005, 0x000f, 0x0019, 0x0028, 0x0037, 0x0046, 0x0050, 0x005a, 0x0064, 0x006e, 0x0078, 0x0082,
            0x008c, 0x0096, 0x00a0, 0x00aa, 0x00b4, 0x00be, 0x00c8, 0x00d2, 0x00dc, 0x00e6, 0x00f0, 0x00fa,
            0x0104, 0x010e, 0x0118, 0x0122, 0x012c, 0x0136, 0x0140, 0x014a, 0x0154, 0x015e, 0x0168, 0x0172,
            0x017c, 0x0186, 0x0190, 0x019a, 0x01a4, 0x01ae, 0x01b8, 0x01c2, 0x01cc, 0x01d6, 0x01e0, 0x01ea,
            0x01f4, 0x01fe, 0x0208, 0x0212, 0x021c, 0x0226, 0x0230, 0x023a, 0x0244, 0x024e, 0x0258, 0x0262,
            0x026c, 0x0276, 0x0280, 0x028a, 0x029a, 0x02aa, 0x02ba, 0x02ca, 0x02da, 0x02ea, 0x02fa, 0x030a,
            0x031a, 0x032a, 0x033a, 0x034a, 0x035a, 0x036a, 0x037a, 0x038a, 0x039a, 0x03aa, 0x03ba, 0x03ca,
            0x03da, 0x03ea, 0x03fa, 0x040a, 0x041a, 0x042a, 0x043a, 0x044a, 0x045a, 0x046a, 0x047a, 0x048a,
            0x04be, 0x04f2, 0x0526, 0x055a, 0x058e, 0x05c2, 0x05f6, 0x062c, 0x0672, 0x06b8, 0x0708, 0x0758,
            0x07a8, 0x07f8, 0x085c, 0x08c0, 0x0924, 0x0988, 0x09ec, 0x0a50, 0x0ab4, 0x0b18, 0x0b7c, 0x0be0,
            0x0c58, 0x0cd0, 0x0d48, 0x0dde, 0x0e74, 0x0f0a, 0x0fa0, 0x1000];
        t.LFORAMP = [0xFFFF, 0x0419, 0x020C, 0x015E, 0x0100, 0x0100, 0x0100, 0x0100];
        t.DCOLFO = sliders(b => b < 3 ? 0 : b < 64 ? b - 2 : b < 96 ? 62 + 2 * (b - 64) : Math.min(255, 128 + 4 * (b - 96)));
        const SUB = [0.00154, 0.01251, 0.10875, 0.22125, 0.33576, 0.45206, 0.55750, 0.67178, 0.78447, 0.91132, 1.0];
        t.SUB = sliders(b => { const x = b / 127 * 10, i = Math.min(Math.floor(x), 9); return (SUB[i] + (x - i) * (SUB[i + 1] - SUB[i])) * .75; });
        t.NOISE = sliders(b => { const d = b / 127 - .0594; return b ? 1.0632 * (Math.sqrt(d * d + .0146 * .0146) + d) * .5 * 1.2 : 0; });
        t.LEVEL = sliders(b => Math.pow(10, (-10 + 20 * b / 127) / 20));
        t.RESK = sliders(b => {                                     /* the resonance's feedback */
            const r = b / 127;
            let k = 4.6265 * r + 4.3866 * r * r - 8.9869 * r ** 3 + 3.3902 * r ** 4;
            if (r > .7) { const knee = (r - .7) / .3; k += Math.min(knee * knee * 1.2, .35); }
            return k * (1 + Math.max(0, (r - .898) * (.17 / .102)));
        });
        t.RESOFF = t.RESK.map(k => {                                /* its pull on the peak, as a cutoff offset (DAC units) */
            const kf = Math.min(Math.max(k, 0), 5);
            let lm = .20646 + kf * (-.04140 + kf * (.00602 - kf * .00127));
            if (k < 1) lm *= Math.max(k, 0);
            if (k > 3.7) lm *= Math.min(1, Math.max(0, (4.4 - k) / .7));
            return Math.round(lm / Math.LN2 * 1143);
        });
        t.FBS = t.RESK.map(k => 4.2 * Math.min(Math.max(k - 2.5, .3), 1));
        /* the self-oscillation's amplitude where k > 4 (the tanh's describing function 1 - z^2 / 4 = 4 / k) */
        t.OSC = t.RESK.map((k, b) => k > 4 ? 2 * Math.sqrt(1 - 4 / k) / t.FBS[b] : 0);
        /* the VCF: Hz per 12-bit DAC code (measured on a voice card), as knots of log-linear pieces;
         * per code the ladder's coefficient (cutoff held under 0.85 Nyquist) and the resonance's fade
         * from 0.7 to 0.9 Nyquist (0-255) */
        const V4HZ = [0, 5.7339, 277, 11.1958, 419, 15.7532, 534, 20.7844, 660, 28.1232, 786, 38.1101, 1023, 67.244, 1024, 67.2247,
            1172, 95.7826, 1311, 133.7763, 1438, 181.3828, 1590, 261.5678, 1637, 292.6994, 1668, 314.9104, 1714, 351.8426, 1770, 402.6563,
            1807, 440.4358, 1867, 508.6977, 1891, 539.0497, 1923, 580.85, 2047, 783.7125, 2048, 796.2545, 2088, 877.0107, 2121, 949.0731,
            2147, 1010.574, 2182, 1097.161, 2315, 1511.284, 2351, 1648.209, 2381, 1771.642, 2404, 1872.856, 2438, 2029.319, 2490, 2300.735,
            2550, 2659.89, 2635, 3263.402, 2660, 3466.849, 2695, 3765.8, 2791, 4748.575, 2824, 5145.807, 2891, 6042.899, 2918, 6449.372,
            2976, 7403.517, 3071, 9299.016, 3072, 9297.264, 3107, 10113.17, 3142, 10974.84, 3170, 11728.44, 3204, 12669.07, 3235, 13617.39,
            3277, 14973.37, 3300, 15775.15, 3334, 16997.3, 3395, 19374.12, 3431, 20860.28, 3462, 22195.3, 3492, 23530.1, 3554, 26436.17,
            3588, 28064.89, 3629, 30069.18, 3651, 31178.45, 3683, 32762.48, 3746, 35908.77, 3782, 37682.65, 3825, 39819.93, 3843, 40725.45,
            3882, 42598.97, 3908, 43849.26, 3940, 45327.11, 4004, 48194.07, 4054, 50355, 4095, 52216.42];
        t.VCF_G = new Float64Array(4096);
        t.VCF_F = new Uint8Array(4096);
        for (let k = 0; k + 2 < V4HZ.length; k += 2) {
            const c0 = V4HZ[k], c1 = V4HZ[k + 2], l0 = Math.log2(V4HZ[k + 1]), l1 = Math.log2(V4HZ[k + 3]);
            for (let c = c0; c <= c1; c++) {
                const frq = Math.pow(2, l0 + (l1 - l0) * (c - c0) / (c1 - c0)) / (SR / 2);
                const g = Math.tan(Math.min(frq, .85) * Math.PI / 2);
                t.VCF_G[c] = g / (1 + g);
                t.VCF_F[c] = Math.min(255, Math.round((frq <= .7 ? 1 : Math.max(0, 1 - (frq - .7) / .2)) * 255));
            }
        }
        /* the VCA: envelope -> gain, measured (knots of linear pieces over 256 steps) */
        const VCA = [0, 0, 2, 0.00036584, 4, 0.00157534, 6, 0.00590519, 8, 0.01165812, 10, 0.01869752, 12, 0.025833, 14, 0.0332908,
            18, 0.04870371, 20, 0.05594957, 22, 0.06420603, 24, 0.07128263, 26, 0.07903423, 28, 0.08762877, 29, 0.09089718, 30, 0.09403543,
            31, 0.09893298, 32, 0.10406616, 33, 0.10741369, 34, 0.11050384, 35, 0.11433945, 36, 0.11829026, 37, 0.12311393, 38, 0.12808174,
            39, 0.13139634, 40, 0.13442011, 41, 0.13894453, 42, 0.14374946, 43, 0.14737569, 44, 0.15076841, 46, 0.15784593, 47, 0.16231941,
            48, 0.16699654, 49, 0.17107621, 50, 0.17501668, 51, 0.17826345, 52, 0.1813402, 53, 0.18536685, 54, 0.18963803, 55, 0.19317203,
            56, 0.196507, 57, 0.20085121, 58, 0.20548085, 60, 0.2151267, 61, 0.21818812, 62, 0.22069705, 63, 0.22439538, 70, 0.2529722,
            71, 0.25689376, 78, 0.28391636, 79, 0.28810312, 86, 0.31842297, 87, 0.32243112, 94, 0.34932978, 95, 0.35340598, 102, 0.38289979,
            103, 0.38664123, 110, 0.41061437, 111, 0.41453889, 118, 0.44468303, 119, 0.44881336, 126, 0.47665693, 127, 0.48040958, 134, 0.50512781,
            135, 0.50912363, 142, 0.54072638, 143, 0.5450176, 150, 0.57307305, 151, 0.57717821, 158, 0.60689605, 159, 0.61123654, 166, 0.64271527,
            167, 0.646633, 174, 0.66640862, 175, 0.66957914, 182, 0.69703187, 183, 0.70100609, 190, 0.72975263, 191, 0.73418681, 198, 0.77203257,
            199, 0.77699595, 206, 0.80077148, 207, 0.80396651, 214, 0.82027925, 215, 0.82300248, 222, 0.85677672, 223, 0.86179402, 230, 0.90625891,
            231, 0.91213508, 238, 0.92133936, 239, 0.92291244, 246, 0.96080777, 247, 0.96618401, 255, 1];
        t.VCA = new Float64Array(257);
        for (let k = 0; k + 2 < VCA.length; k += 2)
            for (let i = VCA[k]; i <= VCA[k + 2]; i++) t.VCA[i] = VCA[k + 1] + (VCA[k + 3] - VCA[k + 1]) * (i - VCA[k]) / (VCA[k + 2] - VCA[k]);
        t.VCA[256] = 1;
        /* the bass boost (HPF 0): direct + gain x two low shelves c + (1 - c) / (1 + s tp), from the circuit */
        const t1z = 47e3 * .047e-6, t1p = 47e3 * (.047e-6 + .01e-6), t2z = (10e3 * 100e3 / 110e3) * .022e-6, t2p = 100e3 * .022e-6;
        t.BB = [1, (47e3 / 220e3) * (1 + 100e3 / 10e3), t1z / t1p, onepole(1 / (TWO_PI * t1p)), t2z / t2p, onepole(1 / (TWO_PI * t2p))];
        return t;
    })();
    /* the voice CPU's loop passes, as the FM-1 counts them: one clock for every voice, J_TQ (Q16) a 64-frame block (U),
     * a pass at the start of each block it overflows in, from block 0 (the player's pl_tick_acc, juno_tick_q16: 22,466 at
     * 44.1 kHz). Its passes in blocks [a, b): one division, the forward work's too */
    const J_U = 64, J_TQ = Math.round(J_U * (1000 / 4.2335) / SR * 65536);   /* (J_U: the FM-1's block, frames) */
    const jPasses = (a, b) => Math.floor(b * J_TQ / 65536) - Math.floor(a * J_TQ / 65536);
    /* the passes a note has had by frame f (absolute), from its first frame t0: the pass of the block it starts in (at
     * its first frame, as a note starting in a block plays it whole on the FM-1), then each later block's at its start */
    const jHad = (t0, f) => f <= t0 ? 0 : jPasses(Math.floor(t0 / J_U), Math.max(Math.floor(t0 / J_U) + 1, Math.ceil(f / J_U)));
    const J_VCA_SLEW = 1 - Math.exp(-1 / (.000687 * SR)), J_VCF_SLEW = 1 - Math.exp(-16 / (.000522 * SR));
    const J_DC = onepole(1.59), J_HPF = [0, 0, onepole(236), onepole(754)];
    const J_IN = .065, J_DIRECT = .142, J_OUT = 35185 / 4096;
    const J_CHORUS = [null, { depth: .00213 * SR, hz: .514, at: -1, ph: 0 }, { depth: .00171 * SR, hz: .842, at: -1, ph: 0 }];
    /* a chorus mode's LFO: one for every voice, as the device runs one chorus a mode on all its voices
     * (the chorus is linear: voices each through their own delay line on the shared LFO sum to the
     * device's one); it moves on while something chorused sounds, a block at a time. Its phase at the
     * block's first frame */
    function chorusLfo(c) {
        if (c.at !== currentTime) {
            if (c.at >= 0) { c.ph += E.BLK * c.hz / SR; c.ph -= Math.floor(c.ph); }
            c.at = currentTime;
        }
        return c.ph;
    }
    const J_CH_CENTER = .0033 * SR, J_CH_LP = 1 - Math.exp(-TWO_PI * 9661 / SR), J_CH_DRY = .863, J_CH_WET = 1.257;

    /* the noise source all voices share (as on the 106): white noise (4 uniforms) plus a flicker tilt,
     * through the circuit's 5.3 kHz low-pass and 34 Hz high-pass; made a block at a time. The white
     * noise also seeds the filters */
    const NZ = { at: -1, seed: 22222, st: new Float64Array(6), buf: new Float32Array(E.BLK), white: new Float32Array(E.BLK),
                 g: [30, 300, 3000, 15000, 5305 * (1 - 2.2 * (5305 / SR) ** 2), 34].map(onepole) };
    function noise() {
        if (NZ.at === currentTime) return NZ;
        NZ.at = currentTime;
        const st = NZ.st, g = NZ.g;
        for (let i = 0; i < E.BLK; i++) {
            let w = 0, fl = 0;
            for (let j = 0; j < 4; j++) { NZ.seed = (Math.imul(NZ.seed, 196314165) + 907633515) >>> 0; w += (NZ.seed | 0) / 4294967296; }
            for (let j = 0; j < 4; j++) fl += (st[j] += (w - st[j]) * g[j]);
            const lp = (st[4] += (w + fl * .375 - st[4]) * g[4]);
            NZ.buf[i] = lp - (st[5] += (lp - st[5]) * g[5]);
            NZ.white[i] = w;
        }
        return NZ;
    }

    /* the voice CPU's routines, on integers as it runs them */
    function envTick(v) {
        if (v.envState === ATTACK) {
            const s = v.env + v.atk;
            if (s >= ENV_MAX) { v.env = ENV_MAX; v.envState = DECAY; } else v.env = s;
        } else if (v.envState === DECAY) v.env = v.env > v.sus ? decay(v.env - v.sus, v.dec) + v.sus : v.sus;
        else if (v.envState === RELEASE) { v.env = decay(v.env, v.relC); if (v.env === 0) v.envState = DONE; }
    }
    function decay(value, coeff) {          /* its 16 x 16 multiply from three 8 x 8 products */
        const vh = value >> 8, vl = value & 255, ch = coeff >> 8, cl = coeff & 255;
        return (vh * ch + ((vh * cl) >> 8) + ((vl * ch) >> 8)) & 0xFFFF;
    }
    function lfoTick(v) {                   /* a triangle 0..0x1FFF, its sign flipping each cycle; the delay */
        if (v.lfoRising) {
            const s = v.lfoAcc + v.lfoCoeff;
            if (s >= 0x2000) { v.lfoAcc = 0x1FFF; v.lfoRising = 0; } else v.lfoAcc = s;
        } else if (v.lfoCoeff > v.lfoAcc) { v.lfoAcc = 0; v.lfoRising = 1; v.lfoPol ^= 1; }
        else v.lfoAcc -= v.lfoCoeff;
        if (v.inHold) {
            const s = v.holdAcc + v.holdInc;
            if (s >= 0x4000) { v.inHold = false; v.holdAcc = 0x4000; } else v.holdAcc = s;
            v.lfoAmp = 0;
        } else {
            const s = v.rampAcc + v.rampInc;
            if (s >= 0x10000) { v.rampAcc = 0xFFFF; v.lfoAmp = 255; } else { v.rampAcc = s; v.lfoAmp = s >> 8; }
        }
    }
    function vcfDac(v, sig, neg) {          /* the cutoff CV: 16-bit sums with the carry / borrow flag */
        const f = v.p.vcf, pitch = (v.pitch88 >> 2) + (v.pitch88 >> 3), c = 0x1680, kt = 2 * f.kybd;
        let ea = f.freq << 7, under = 0;
        const add = b => { ea += b; if (ea > 0xFFFF) under = 0; ea &= 0xFFFF; };
        const sub = b => { if (b > ea) under = 1; ea = (ea - b) & 0xFFFF; };
        if (neg) sub(sig); else add(sig);
        const env = ((2 * f.env * v.env) >> 8) & 0xFFFF;
        if (f.env_polarity === "-") sub(env); else add(env);
        if (pitch > c) add(((kt * (pitch - c)) >> 8) & 0xFFFF); else sub(((kt * (c - pitch)) >> 8) & 0xFFFF);
        return ea > 0x3FFF ? (under ? 0 : 0x3FFF) : ea;
    }
    function vcaTarget(v) {
        let g;
        if (v.gateMode) g = v.envState < RELEASE ? J.VCA[256] : 0;
        else { const x = (v.env * 1020) >> 8, i = x >> 8; g = J.VCA[i] + (J.VCA[i + 1] - J.VCA[i]) * (x & 255) / 256; }
        v.vcaT = g * v.level;
    }
    function voiceTick(v) {                 /* a loop pass: LFO, envelope, then the cutoff, the pitch, the width */
        lfoTick(v);
        envTick(v);
        const tri = v.lfoAcc, neg = v.lfoPol && tri;
        v.dac = vcfDac(v, ((((2 * v.p.vcf.lfo * v.lfoAmp) >> 8) * tri) >> 9), neg);
        const lfo = neg ? -tri : tri;
        /* vibrato: LFO x its onset x depth (0-255: 0-4 semitones) */
        const oct = lfo * v.lfoAmp * v.dcoLfo * 21931 / 2097152 / 16777216;
        v.dt = E.mtof(v.midi + 12 * v.oct + (v.bend || 0) + 12 * oct) / SR;
        /* the pulse's width, 50-95 %, LFO or manual (the 106's duty inverted) */
        v.pw = .5 - (v.pwMan ? v.pwm : Math.trunc(v.pwm * (8191 + lfo) / 16382)) * 58982 / 4294967296;
        vcaTarget(v);
    }
    /* every 16 frames (on the absolute 16-frame grid, a block's start among them): the filter's seed, the cutoff CV
     * smoothed, the coefficients */
    function junoChunk(v) {
        const energy = Math.max(Math.abs(v.s0) + Math.abs(v.s1) + Math.abs(v.s2) + Math.abs(v.s3), v.inEnv);
        v.seed = .00125 / (1 + 1000 * energy);   /* white noise into the filter: what starts a self-oscillation */
        v.dacS += (v.dac - v.dacS) * J_VCF_SLEW;
        const code = E.clamp(Math.floor(v.dacS) + v.resOff, 0, 16383) >> 2, g1 = J.VCF_G[code], fade = J.VCF_F[code];
        const k = fade === 255 ? v.k : v.k * fade / 255, g2 = g1 * g1;
        v.g1 = g1; v.g2 = g2; v.g3 = g2 * g1; v.om = 1 - g1;
        v.invden = 1 / (1 + k * g2 * g2);
        v.kk = k;
    }
    /* the forward work (a ghost's return, dsp-core.js), as the firmware's juno_forward: the voice CPU's loop worked on
     * over many ticks at once, to the state its ticks leave. The envelope a stretch at a time: its attack to its top in
     * one sum; its DECAY and RELEASE (decay's ticks on the distance d to the sustain, or to 0) by stretches where the
     * coefficient's high byte ch is 254 or 255 (the slow ones: a tick takes d down by vh x w - (vh x cl >> 8) + q while
     * d's high byte vh and its low byte's band stand, w = 256 - ch, q = ceil(vl x w / 256)), J_FWD_TICKS ticks a step
     * below; at its sustain, or ended, it stands. The integers are the routines' own, so it is exact */
    const J_FWD_TICKS = 4;
    function jEnvGo(v, n, steps) {
        let t = 0;
        while (t < n) {
            const e = v.env;
            let k;
            if (v.envState === ATTACK) {
                k = e < ENV_MAX ? Math.floor((ENV_MAX - 1 - e) / v.atk) + 1 : 1;   /* (the tick that reaches the top) */
                if (n - t < k) { v.env = e + (n - t) * v.atk; t = n; }
                else { v.env = ENV_MAX; v.envState = DECAY; t += k; }
                steps.n--;
            } else if (v.envState === RELEASE || (v.envState === DECAY && e > v.sus)) {
                const s = v.envState === DECAY ? v.sus : 0, c = v.envState === DECAY ? v.dec : v.relC;
                let d = e - s;
                if (c >> 8 >= 254 && d) {                   /* (a stretch: its band, its amount a tick, its ticks) */
                    const w = 256 - (c >> 8), vh = d >> 8, vl = d & 255, q = (vl * w + 255) >> 8;
                    const lo = q ? ((q - 1) << (9 - w)) + 1 : 0, by = vh * w - ((vh * (c & 255)) >> 8) + q;
                    k = Math.min(Math.floor((vl - lo) / by) + 1, n - t);
                    d -= k * by;
                    steps.n--;
                } else {
                    let m = n - t;                          /* (the ticks the budget allows, J_FWD_TICKS a step) */
                    if (steps.n <= Math.floor((m - 1) / J_FWD_TICKS)) m = steps.n * J_FWD_TICKS;
                    k = 0;
                    do d = decay(d, c); while (++k < m && d);
                    steps.n -= Math.ceil(k / J_FWD_TICKS);
                }
                v.env = d + s;
                if (!d && v.envState === RELEASE) v.envState = DONE;
                t += k;
            } else {                                        /* (at its sustain, or ended: it stands) */
                if (v.envState === DECAY) v.env = v.sus;
                return n;
            }
            if (steps.n <= 0) break;
        }
        return t;
    }
    /* the LFO moved on n ticks: its triangle rises from 0 and falls from 0x1FFF, each for h = 0x1FFF / coeff + 1
     * ticks, its sign flipping as each rise starts; its delay, the hold then the ramp, each to its end in one sum */
    function jLfoGo(v, n) {
        const c = v.lfoCoeff, h = Math.floor(0x1FFF / c) + 1, a = v.lfoAcc;
        if (!n) return;
        let t = v.lfoRising ? Math.floor((0x1FFF - a) / c) + 1 + h : Math.floor(a / c) + 1;   /* (the next rise's first tick) */
        if (n < t) {
            if (!v.lfoRising) v.lfoAcc = a - n * c;
            else if (n < t - h) v.lfoAcc = a + n * c;
            else { v.lfoAcc = 0x1FFF - (n - (t - h)) * c; v.lfoRising = 0; }
        } else {
            let r = n - t;
            v.lfoPol ^= (1 + Math.floor(r / (2 * h))) & 1;
            r %= 2 * h;
            v.lfoRising = r < h ? 1 : 0;
            v.lfoAcc = r < h ? r * c : 0x1FFF - (r - h) * c;
        }
        if (v.inHold) {                                     /* (no LFO throughout, the tick that ends it too) */
            t = Math.floor((0x3FFF - v.holdAcc) / v.holdInc) + 1;
            v.lfoAmp = 0;
            if (n < t) { v.holdAcc += n * v.holdInc; return; }
            v.inHold = false;
            v.holdAcc = 0x4000;
            if (!(n -= t)) return;
        }
        t = Math.floor((0xFFFF - v.rampAcc) / v.rampInc) + 1;   /* (the tick that fills it) */
        if (n < t) { v.rampAcc += n * v.rampInc; v.lfoAmp = v.rampAcc >> 8; }
        else { v.rampAcc = 0xFFFF; v.lfoAmp = 255; }
    }
    /* the loop moved on by up to n ticks (n > 0): the envelope and the LFO to the tick before the last one gone, that
     * tick by voiceTick, then the cutoff's and the VCA's smoothing where they head (a step with the LFO's move). The
     * ticks gone */
    function jRun(v, n, steps) {
        const to = jEnvGo(v, n - 1, steps);
        jLfoGo(v, to);
        voiceTick(v);
        v.dacS = v.dac;
        v.vca = v.vcaT;
        steps.n--;
        return to + 1;
    }
    /* the ticks a RELEASE takes from the envelope's top until it is under J_REL_SILENT, the first envelope the firmware's
     * VCA curve (in whole numbers) gives a gain at all: silent at any level from there, by RELEASE (the firmware's
     * juno_release_ticks, its table counted the same way): a ghost let go longer ago is dropped with no work */
    const J_REL_SILENT = 11;
    J.REL_TICKS = J.DECREL.slice(0, 128).map(c => { let e = ENV_MAX, k = 0; while (e >= J_REL_SILENT) { e = decay(e, c); k++; } return k; });
    /* the VCA's gain at the envelope's top (an attack's most), as vcaTarget makes it there */
    J.VCA_TOP = (() => { const x = (ENV_MAX * 1020) >> 8, i = x >> 8; return J.VCA[i] + (J.VCA[i + 1] - J.VCA[i]) * (x & 255) / 256; })();

    /* the chorus's delay line (written at w) read d frames back, between two of its samples */
    function bbd(cb, w, d) {
        const n = Math.floor(d), at = w - n, a = cb[at & 255];
        return a + (cb[(at - 1) & 255] - a) * (d - n);
    }
    E.engines.JUNO = {
        /* a new start of the sound (the worklet made again): the chorus modes' LFOs and the noise source from where they
         * start, as the FM-1's start has them */
        fresh() {
            for (const c of J_CHORUS) if (c) { c.at = -1; c.ph = 0; }
            NZ.at = -1; NZ.seed = 22222; NZ.st.fill(0);
        },
        /* a legato note (LEG): the VCF's key follow moves to it (the pitch comes from the bend) */
        key(v, midi) { v.pitch88 = E.clamp(midi, 0, 127) << 8; },
        /* (last: its engine's last Juno-106 note, as the FM-1's juno_note_on finds it there, its sound ended or cut (a
         * note never meets one sounding: the pool's engines are cut or ended before their next note): the LFO's triangle
         * and sign, the coupling capacitors and the HPF go on; the filter too where it was left ringing out (not cut)
         * and does not self-oscillate; its delay, the envelope (at 0), the DCO and the VCA start again. None: a fresh
         * engine's zeros) */
        start(v, p, midi, m, proc, last) {
            const was = last ? last.st : null;
            v.p = p;
            this.refresh(v);
            v.pitch88 = E.clamp(midi, 0, 127) << 8;
            v.holdAcc = 0; v.inHold = v.holdInc < ENV_MAX; v.rampAcc = 0; v.lfoAmp = 0;   /* (its gate was down: the delay
                                                         * starts again) */
            v.lfoAcc = was ? was.lfoAcc : 0; v.lfoRising = was ? was.lfoRising : 0; v.lfoPol = was ? was.lfoPol : 0;
            v.gate = 1;
            v.env = 0; v.envState = ATTACK;
            envTick(v);                                  /* the first attack step at once */
            v.dac = vcfDac(v, 0, 0);
            v.dacS = v.dac;
            v.dt = E.mtof(midi + 12 * v.oct) / SR;
            v.ph = 0; v.sub = 0; v.vca = 0;
            /* a filter past self-oscillation rings between notes too, as on the 106: the note starts
             * with it ringing (its steady state: each stage 45 degrees, x 1 / sqrt 2), so a patch with
             * no oscillator (its drums: a kick is the filter swept by the envelope) sounds at once */
            const a = J.OSC[p.vcf.res];
            if (a || !was || last.cut) { v.s0 = -2 * a; v.s1 = 0; v.s2 = a; v.s3 = a; }   /* (cut: juno_cut's zeros) */
            else { v.s0 = was.s0; v.s1 = was.s1; v.s2 = was.s2; v.s3 = was.s3; }
            v.pw = .5 - (v.pwMan ? v.pwm : v.pwm >> 1) * 58982 / 4294967296;
            vcaTarget(v);
            if (was) { v.dc = was.dc; v.hp0 = was.hp0; v.hp1 = was.hp1; } else { v.dc = 0; v.hp0 = 0; v.hp1 = 0; }
            v.jAbs = -1; v.c16 = 0;                    /* (the loop's passes on the one clock: from its first render) */
            v.cw = 0; v.clp0 = 0; v.clp1 = 0;
        },
        /* what follows from the patch; again when a parameter command changes it */
        refresh(v) {
            const p = v.p, d = p.dco;
            v.atk = J.ATK[p.env.a]; v.dec = J.DECREL[p.env.d]; v.relC = J.DECREL[p.env.r];   /* (v.rel: the core's) */
            v.sus = p.env.s ? Math.floor(p.env.s * 0x3F80 / 127) : 16;
            v.lfoCoeff = J.LFOSPD[p.lfo.rate]; v.holdInc = J.ATK[p.lfo.delay]; v.rampInc = J.LFORAMP[p.lfo.delay >> 4];
            v.dcoLfo = J.DCOLFO[d.lfo];
            v.pwm = Math.floor(Math.min(d.pwm, 105) * 32767 / 105);
            v.pwMan = d.pwm_source === "manual";
            v.gateMode = p.vca.mode === "gate";
            v.gSaw = d.saw ? .6 : 0; v.gPulse = d.pulse ? .5 : 0; v.gSub = J.SUB[d.sub]; v.gNoise = J.NOISE[d.noise];
            v.inEnv = v.gSaw + v.gPulse + v.gSub + v.gNoise;
            v.k = J.RESK[p.vcf.res]; v.kfb = J.FBS[p.vcf.res]; v.resOff = J.RESOFF[p.vcf.res];
            v.level = J.LEVEL[p.vca.level];
            v.oct = d.range === "16'" ? -1 : d.range === "4'" ? 1 : 0;
            v.cho = J_CHORUS[p.chorus === "I" ? 1 : p.chorus === "II" ? 2 : 0];
            if (v.cho && !v.cb) v.cb = new Float32Array(256);
            if (v.envState !== undefined) vcaTarget(v);
        },
        release(v) { v.gate = 0; v.envState = RELEASE; },
        /* the forward work (dsp-core.js: a ghost coming back, as the firmware's juno_forward): a note just started
         * worked on without its sound, in its loop's ticks, w.on ticks held, then (w.off >= 0) its note off and w.off
         * ticks more, spending steps.n; true when done, false when the budget ran out first (called again it goes on).
         * Its envelope, LFO and delay, cutoff, pitch, width and VCA come out as those ticks leave them, the cutoff's
         * and the VCA's smoothing where they head; the oscillator's phase and the filter's state are not kept */
        forward(v, w, steps) {
            for (;;) {
                if (!w.on && (w.off < 0 || (!w.off && !v.gate))) return true;
                if (steps.n <= 0) return false;
                if (w.on) w.on -= jRun(v, w.on, steps);
                else if (v.gate) { this.release(v); steps.n--; }   /* (the note off, once: its gate tells it was) */
                else w.off -= jRun(v, w.off, steps);
            }
        },
        /* the most it still reaches: the VCA (its target or its smoothing, the larger; an attack counted at its top)
         * times the output's scale for a filter's output of 1, four times that with the bass boost (HPF 0) */
        heard(v) {
            let t = Math.max(v.vcaT, v.vca);
            if (v.envState === ATTACK && !v.gateMode) t = Math.max(t, J.VCA_TOP * v.level);
            return t * J_OUT * (v.p.hpf === 0 ? 4 : 1);
        },
        releaseTicks: p => p.vca.mode === "gate" ? 1 : J.REL_TICKS[Math.min(127, p.env.r)],   /* (VCA GATE: a tick) */
        active: v => v.envState !== DONE || v.vca > 4 / 32768,   /* (it still sounds: render's) */
        /* its loop's passes in f frames from its start t0 (absolute), on the one clock (jHad) */
        units: (f, up, t0 = 0) => jHad(t0, t0 + f),
        /* worked forward over the passes before frame f of its note (t0 + f absolute): the passes at or after it come
         * as render reaches their blocks; the chunk it falls in made now, on the 16-frame grid; let go there (rel), its
         * note off after it */
        resume(v, f, rel, t0 = 0) {
            const at = t0 + f, r = at % 16;
            v.jAbs = at;
            v.c16 = 0;
            if (r) { junoChunk(v); v.c16 = 16 - r; }
            if (rel) this.release(v);
        },
        render(v, out, a, b, pool) {
            const nz = noise(), cho = v.cho, hpf = v.p.hpf, hpg = J_HPF[hpf], B = J.BB;
            const gSaw = v.gSaw, gPulse = v.gPulse, gSub = v.gSub, gNoise = v.gNoise, kfb = v.kfb, nbuf = nz.buf, white = nz.white;
            let i = a;
            if (v.jAbs < 0) {                   /* (its first frame, absolute: its block's pass, if the block has one) */
                v.jAbs = (pool && pool.frame !== undefined ? pool.frame : 0) + a;
                const k = Math.floor(v.jAbs / J_U);
                if (v.jAbs % J_U && jPasses(k, k + 1)) voiceTick(v);
            }
            const f0 = v.jAbs - a;             /* (out[i] is frame f0 + i: its frames rendered one after another) */
            v.jAbs = f0 + b;
            while (i < b) {
                const at = f0 + i;
                if (at % J_U === 0) {           /* (a block's start: its pass on the one clock) */
                    const k = at / J_U;
                    if (jPasses(k, k + 1)) { voiceTick(v); v.c16 = 0; }
                }
                if (v.c16 === 0) { junoChunk(v); v.c16 = 16 - at % 16; }
                const end = Math.min(b, i + v.c16);
                v.c16 -= end - i;
                /* the chunk's values (the loop pass and the coefficients change between chunks) */
                const dt = v.dt, pw = v.pw, seed = v.seed, g1 = v.g1, g2 = v.g2, g3 = v.g3, om = v.om, kk = v.kk, invden = v.invden, vcaT = v.vcaT;
                let ph = v.ph, sub = v.sub, s0 = v.s0, s1 = v.s1, s2 = v.s2, s3 = v.s3, dc = v.dc, vca = v.vca, hp0 = v.hp0, hp1 = v.hp1;
                for (; i < end; i++) {
                    /* DCO: saw, pulse and sub from one phase */
                    ph += dt;
                    if (ph >= 1) { ph -= 1; sub ^= 1; }
                    const b0 = blep(ph, dt);
                    let t2 = ph - pw; if (t2 < 0) t2 += 1;
                    const saw = 2 * ph - 1 - b0, pulse = (ph < pw ? -1 : 1) - b0 + blep(t2, dt);
                    let x = saw * gSaw + pulse * gPulse + (1 - Math.abs(b0)) * (sub ? -gSub : gSub) + nbuf[i] * gNoise;
                    /* the coupling capacitors (DCO -> VCF, VCF -> VCA) as one; the filter's seed */
                    dc += (x - dc) * J_DC;
                    x += white[i] * seed - dc;
                    /* VCF: a 4-pole ladder (TPT), the resonance's feedback through the OTA's tanh */
                    const sum = (s0 * g3 + s1 * g2 + s2 * g1 + s3) * om;
                    const u = (x * J_DIRECT + kk * Math.tanh((x * J_IN - sum) * kfb) / kfb) * invden;
                    let w = (u - s0) * g1, y = s0 + w; s0 = y + w;
                    w = (y - s1) * g1; y = s1 + w; s1 = y + w;
                    w = (y - s2) * g1; y = s2 + w; s2 = y + w;
                    w = (y - s3) * g1; y = s3 + w; s3 = y + w;
                    /* VCA: its CV smoothed */
                    vca += (vcaT - vca) * J_VCA_SLEW;
                    y *= J_OUT * vca;
                    /* the HPF switch: 3 and 2 high-pass, 1 flat, 0 the bass boost */
                    if (hpf >= 2) { hp0 += (y - hp0) * hpg; y -= hp0; }
                    else if (hpf === 0) {
                        hp0 += (y - hp0) * B[3];
                        let s = hp0 + (y - hp0) * B[2];
                        hp1 += (s - hp1) * B[5];
                        s = hp1 + (s - hp1) * B[4];
                        y = y * B[0] + s * B[1];
                    }
                    out[i] = y;
                }
                v.ph = ph; v.sub = sub; v.s0 = s0; v.s1 = s1; v.s2 = s2; v.s3 = s3; v.dc = dc; v.vca = vca; v.hp0 = hp0; v.hp1 = hp1;
            }
            /* chorus: the BBD read at two taps a triangle moves apart and together, each through the
             * BBD's low-pass, dry + wet (left and right, here summed). Its LFO is the mode's, shared */
            if (cho) {
                const cb = v.cb, inc = cho.hz / SR, depth = cho.depth, ph0 = chorusLfo(cho);
                let cw = v.cw, l0 = v.clp0, l1 = v.clp1;
                for (let k = a; k < b; k++) {
                    const y = out[k];
                    cb[cw & 255] = y;
                    let clfo = ph0 + (k + 1) * inc;
                    clfo -= Math.floor(clfo);
                    const tri = clfo < .5 ? 4 * clfo - 1 : 3 - 4 * clfo;
                    l0 += (bbd(cb, cw, J_CH_CENTER + depth * tri) - l0) * J_CH_LP;
                    l1 += (bbd(cb, cw, J_CH_CENTER - depth * tri) - l1) * J_CH_LP;
                    cw++;
                    out[k] = y * J_CH_DRY + (l0 + l1) * .5 * J_CH_WET;
                }
                v.cw = cw; v.clp0 = l0; v.clp1 = l1;
            }
            return v.envState !== DONE || v.vca > 4 / 32768;
        },
    };

    /* ------------------------------------------------------------------- TB-303 --- */
    /* Open303's voice as the firmware's engine runs it (1x, the cutoff up to 10 kHz). Knobs 0-127:
     * WAVE (saw to square), CUTOFF 314-2394 Hz, RESONANCE, ENV MOD, DECAY 200-2000 ms, ACCENT. Parameter
     * commands turn them (0x40-0x45); DECAY and ACCENT take effect from the next note, as Open303's */
    const TB_KNOBS = ["wave", "cutoff", "reso", "envmod", "decay", "accent"];
    E.params.TB303 = (p, c, v) => { if (c >= 0x40 && c <= 0x45) p[TB_KNOBS[c - 0x40]] = Math.min(127, v); };
    const TB = (() => {
        const ctl = 16, ms = x => SR * .001 * x;
        const hp = fc => { const x = Math.exp(-TWO_PI * fc / SR); return [.5 * (1 + x), x]; };   /* b, a */
        const w = TWO_PI * 200 / SR, s = Math.sin(w), c = Math.cos(w), alpha = s / (2 * Math.SQRT1_2), sc = 1 / (1 + alpha);
        const t = { CTL: ctl, L0: Math.log2(200), PW: 15 / 32,
                    HP1: hp(44.486), FB: hp(150), HP2: hp(24.167),
                    DK_B1: (1 - c) * sc, DK_A1: 2 * c * sc, DK_A2: (alpha - 1) * sc,   /* the declicker: 200 Hz, Q 0.707 */
                    AMP_DEC: Math.exp(-1 / ms(1230)), REL_N: Math.exp(-1 / ms(1)), REL_A: Math.exp(-1 / ms(50)),
                    RC2_C: Math.exp(-ctl / ms(15)), SLEW_C: Math.exp(-ctl / ms(12)), ACC_DECAY_C: Math.exp(-ctl / ms(200)),
                    OF: .048292930943553, OC: .294391201442418, SLOF: 3.773996325111173, SLOC: .736965594166206,
                    SHIF: 4.194548788411135, SHIC: .864344900642434 };
        t.DK_B0 = .5 * t.DK_B1;
        t.DECAY_C = Array.from({ length: 128 }, (_, v) => Math.exp(-ctl / ms(200 * Math.pow(10, v / 127))));
        /* the diode ladder's b0 and k (before x resonance) per 1/16 octave from 200 Hz to 10 kHz: Open303's fits */
        t.NLAD = Math.ceil((Math.log2(10000) - t.L0) * 16) + 2;
        t.B0 = new Float64Array(t.NLAD); t.K = new Float64Array(t.NLAD);
        for (let n = 0; n < t.NLAD; n++) {
            const fx = Math.pow(2, t.L0 + n / 16) * Math.SQRT1_2 / SR;
            t.B0[n] = (.00045522346 + 6.1922189 * fx) / (1 + 12.358354 * fx + 4.4156345 * fx * fx);
            t.K[n] = fx * (fx * (fx * (fx * (fx * (fx + 7198.6997) - 5837.7917) - 476.47308) + 614.95611) + 213.87126) + 16.998792;
        }
        return t;
    })();
    /* the knobs' consequences: the envelope's measured mapping, the resonance's skew, the wave blend */
    function tbDerive(v) {
        const k = v.p, e = k.envmod / 127, c = k.cutoff / 127, b = k.wave / 127;
        v.cutL2 = Math.log2(314 * Math.pow(2394 / 314, c));
        v.envSc = (1 - c) * (TB.SLOF * e + TB.SLOC) + c * (TB.SHIF * e + TB.SHIC);
        v.envOff = TB.OF * c + TB.OC;
        v.rsk = (1 - Math.exp(-3 * k.reso / 127)) / (1 - Math.exp(-3));
        v.fSaw = 1 - b; v.fSq = b / 2;
    }
    function tbAccent(v, accent) {
        v.acc = !!accent;
        v.accGain = accent ? v.p.accent / 127 : 0;
        v.megC = accent ? TB.ACC_DECAY_C : TB.DECAY_C[v.p.decay];
        v.relC = accent ? TB.REL_A : TB.REL_N;                     /* (v.rel: the core's) */
    }
    const tbActive = v => v.gate || v.amp > 1 / 16384 || Math.abs(v.dkY1) > 1 / 16384;
    /* every 16 frames: the slide, the envelope, the accent's RC, the cutoff, the ladder's coefficients */
    function tbControl(v) {
        v.incTo = E.mtof(v.target + (v.bend || 0)) / SR;
        v.inc = v.incTo + (v.inc - v.incTo) * TB.SLEW_C;
        const inp = v.accGain > 0 ? v.meg : 0;
        v.rc2 = inp + (v.rc2 - inp) * TB.RC2_C;
        const pos = E.clamp((v.cutL2 + v.envSc * (v.meg - v.envOff) + v.accGain * v.rc2 - TB.L0) * 16, 0, TB.NLAD - 2.000001);
        const n = Math.floor(pos), fr = pos - n, r = v.rsk;
        const b0 = TB.B0[n] + (TB.B0[n + 1] - TB.B0[n]) * fr, kp = TB.K[n] + (TB.K[n + 1] - TB.K[n]) * fr;
        v.fB0 = b0;
        v.fK = kp * r;
        v.fG2 = 2 * ((kp / 17 - 1) * r + 1) * (1 + r);
        v.fMegAmp = v.gate ? v.meg * (.45 + 4 * v.accGain) : 0;     /* the main envelope's part of the amp */
        v.meg *= v.megC;
    }
    E.engines.TB303 = {
        start(v, p, midi, m, proc) {
            v.p = p;
            v.gate = 0; v.amp = 0; v.dkY1 = 0;
            this.note(v, midi, m, proc, true);
        },
        /* a new note on its own voice: a slide (SLD) glides there, else the note starts again */
        note(v, midi, m, proc, first) {
            v.midi = midi;
            v.target = midi;
            if (m.glide && !first) { tbAccent(v, m.accent); v.gate = 1; return; }
            if (!tbActive(v)) {                        /* silent: start clean */
                v.ph = 0;
                v.hp1X = v.hp1Y = v.y1 = v.y2 = v.y3 = v.y4 = v.fbX = v.fbY = v.hp2X = v.hp2Y = 0;
                v.dkX1 = v.dkX2 = v.dkY1 = v.dkY2 = 0;
                v.rc2 = 0;
                v.fGain = 0;
                v.c16 = 0;
            }
            tbDerive(v);
            tbAccent(v, m.accent);
            v.inc = v.incTo = E.mtof(midi + (v.bend || 0)) / SR;
            v.meg = 1;                                 /* the main envelope's first sample is 1 */
            v.amp = 1;                                 /* the amp envelope: attack 0 */
            v.gate = 1;
        },
        refresh(v) { tbDerive(v); },
        release(v) { v.gate = 0; },
        render(v, out, a, b) {
            const wave = v.fSq === 0 ? 0 : v.fSaw === 0 ? 1 : 2;   /* saw, square, both */
            const fSaw = v.fSaw, fSq = v.fSq, PW = TB.PW, B0 = TB.DK_B0, B1 = TB.DK_B1, A1 = TB.DK_A1, A2 = TB.DK_A2;
            const H1B = TB.HP1[0], H1A = TB.HP1[1], FBB = TB.FB[0], FBA = TB.FB[1], H2B = TB.HP2[0], H2A = TB.HP2[1];
            let i = a;
            while (i < b) {
                if (v.c16 === 0) { tbControl(v); v.c16 = TB.CTL; }
                const end = Math.min(b, i + v.c16);
                v.c16 -= end - i;
                /* the control step's values */
                const inc = v.inc, ampK = v.gate ? TB.AMP_DEC : v.relC, megAmp = v.fMegAmp, fK = v.fK, b0 = v.fB0, fG2 = v.fG2;
                let ph = v.ph, amp = v.amp, x1 = v.dkX1, x2 = v.dkX2, g1 = v.dkY1, g2 = v.dkY2;
                let hp1X = v.hp1X, hp1Y = v.hp1Y, fbX = v.fbX, fbY = v.fbY, y1 = v.y1, y2 = v.y2, y3 = v.y3, y4 = v.y4, hp2X = v.hp2X, hp2Y = v.hp2Y;
                for (; i < end; i++) {
                    /* the amp: its envelope decaying while the note is on, releasing after, + the main
                     * envelope's part, through the declicker (its last output is this frame's gain) */
                    amp *= ampK;
                    const inp = amp + megAmp, g = (inp + x2) * B0 + x1 * B1 + g1 * A1 + g2 * A2;
                    x2 = x1; x1 = inp; g2 = g1; g1 = g;
                    ph += inc;
                    if (ph >= 1) ph -= 1;
                    let x = 0;
                    if (wave !== 1) {                  /* the saw: up from -1 to 1, its edge at half a cycle */
                        let t = ph + .5; if (t >= 1) t -= 1;
                        const saw = 2 * t - 1 - blep(t, inc);
                        x = wave ? saw * fSaw : saw;
                    }
                    if (wave) {                        /* the square: up at 0, down at 15/32 */
                        let t2 = ph - PW; if (t2 < 0) t2 += 1;
                        const sq = (ph < PW ? 1 : -1) + blep(ph, inc) - blep(t2, inc);
                        x += wave === 1 ? sq / 2 : sq * fSq;
                    }
                    x = -x;
                    hp1Y = (x - hp1X) * H1B + hp1Y * H1A;   /* the high-pass before the filter */
                    hp1X = x;
                    const fb = fK * y4;                /* the feedback through its high-pass */
                    fbY = (fb - fbX) * FBB + fbY * FBA;
                    fbX = fb;
                    const y0 = hp1Y - fbY;             /* the diode ladder */
                    y1 += 2 * (y0 - y1 + y2) * b0;
                    y2 += (y1 - 2 * y2 + y3) * b0;
                    y3 += (y2 - 2 * y3 + y4) * b0;
                    y4 += (y3 - 2 * y4) * b0;
                    const d = y4 * fG2;
                    hp2Y = (d - hp2X) * H2B + hp2Y * H2A;   /* the high-pass after it */
                    hp2X = d;
                    out[i] = hp2Y * g2 * .25;          /* the amp, and Open303's -12 dB */
                }
                v.ph = ph; v.amp = amp; v.dkX1 = x1; v.dkX2 = x2; v.dkY1 = g1; v.dkY2 = g2;
                v.hp1X = hp1X; v.hp1Y = hp1Y; v.fbX = fbX; v.fbY = fbY; v.y1 = y1; v.y2 = y2; v.y3 = y3; v.y4 = y4; v.hp2X = hp2X; v.hp2Y = hp2Y;
            }
            return tbActive(v);
        },
    };

    /* ------------------------------------------------------------------ sampler --- */
    /* a sample instrument (specs/files/instruments.md): START to END of its sample, a sustain loop
     * (forward or ping-pong) repeated while the key is held, BASE NOTE, TUNE in 1/128 of a semitone, an
     * envelope as a DX7 operator's; at release the sound goes on from where it is through the rest of
     * the loop to END while the envelope releases. Its parameters (commands.md): positions as 1/256 of the
     * sample's length */
    /* the sampler's parameter codes (specs/files/commands.md): START, END, LOOP, LOOP START, LOOP END, TUNE, then the
     * envelope's four rates and four levels */
    const SC = E.SAMP_CODES = { START: 0x40, END: 0x41, LOOP: 0x42, LOOP_START: 0x43, LOOP_END: 0x44, TUNE: 0x45, RATES: 0x48, LEVELS: 0x4C };
    E.params.SAMPLER = function (p, c, v, proc) {
        /* (a position: v / 256 of the sample's length in whole frames, as the firmware works it out, 255 its end) */
        const s = proc.samples[p.key], len = s ? s.data.length : 0, at = v >= 255 ? len : (len >> 8) * v + ((len & 255) * v >> 8);
        if (c === SC.START) p.start = at >= len ? 0 : at;   /* (its end: from the sample's start, as the firmware's) */
        else if (c === SC.END) p.end = Math.max(1, at);     /* (END 0: one frame, not the whole sample) */
        else if (c === SC.LOOP) p.loop = ["OFF", "FORWARD", "PING-PONG"][Math.min(2, v)];
        else if (c === SC.LOOP_START) p.loopStart = at;
        else if (c === SC.LOOP_END) p.loopEnd = at;
        else if (c === SC.TUNE) p.tune = v - 128;
        else if (c >= SC.RATES && c < SC.RATES + 4) p.env.rates[c - SC.RATES] = Math.min(99, v);
        else if (c >= SC.LEVELS && c < SC.LEVELS + 4) p.env.levels[c - SC.LEVELS] = Math.min(99, v);
    };
    /* the sampler's envelope moved on by up to n steps, a stretch a step (E.opEnv.span, as the DX7's): the steps gone */
    function sRun(v, n, steps) {
        const e = v.env;
        let t = 0;
        while (t < n) {
            const sp = E.opEnv.span(e);
            if (!sp) { t = n; steps.n--; break; }           /* (standing for good: its sustain, or released to its end) */
            const k = Math.min(sp, n - t);
            E.opEnv.go(e, k, sp);
            t += k;
            if (--steps.n <= 0) break;
        }
        if (t) v.g1 = E.opEnv.gain(e.level);
        return t;
    }
    /* Where a sampler note is in its sample without its voice (a ghost, dsp-core.js; the firmware's samp_place_t): its
     * position, going back or not (a ping-pong pass), in its sustain loop or not, ended or not; moved on by the frames
     * at a pitch as render() moves a voice's, in one sum a run (to the loop's end, a turn, END), the loop's rounds held
     * by their remainder; so a ghost's end and the place it comes back at come from one count */
    /* the frames until x, moving up by S, reaches T (the frame it does, the last); falling by d, goes under T */
    const rise = (x, T, S) => x >= T ? 1 : Math.max(1, Math.ceil((T - x) / S));
    const fall = (x, d, T) => x < T ? 1 : Math.floor((x - T) / d) + 1;
    const PLACE = { pos: 0, dir: 1, looping: false, ended: false };   /* (endFrames' copy of a place, kept) */
    E.samp = {
        /* an instrument's points as a voice plays them (its END, its loop's within it, room for a loop) */
        facts(p, s) {
            const len = s ? s.data.length : 0, end = Math.min(p.end || len, len), le = Math.min(p.loopEnd, end);
            return { end, ls: p.loopStart, le, room: le > p.loopStart + 1, mode: p.loop };
        },
        /* the loop rule (the firmware's), the one place it is written: patch p (its points P) has a loop to play, LOOP on
         * with room; parameter code c turned on it takes a note out of its loop: LOOP to OFF, or END or a loop point
         * leaving it no room (any other command leaves a note's loop as it is, and none puts a note into one); code c
         * moves a ghost's place (END, the loop and its points, TUNE) */
        loops: (p, P) => p.loop !== "OFF" && P.room,
        unloops: (c, p, P) => (c === SC.LOOP && p.loop === "OFF") || ((c === SC.END || c === SC.LOOP_START || c === SC.LOOP_END) && !P.room),
        moves: c => c >= SC.END && c <= SC.TUNE,
        inc: (p, s, midi, bend) => s.rate / SR * Math.pow(2, (midi + (bend || 0) - p.base + E.samp.tune(p)) / 12),
        /* TUNE (1/128 semitones) as the FM-1's pitch takes it: in 1/64 steps, its half toward zero */
        tune: p => Math.trunc((p.tune || 0) / 2) / 64,
        /* where a note starts (its SAMPLE OFFSET past START), as start() puts it */
        start(p, s, offset) {
            const P = E.samp.facts(p, s);
            return { pos: Math.min((p.start || 0) + (offset || 0), P.end - 1), dir: 1, looping: E.samp.loops(p, P), ended: !s };
        },
        of: v => ({ pos: v.pos, dir: v.dir, looping: v.looping, ended: !!v.dead }),
        set(v, pl) { v.pos = pl.pos; v.dir = pl.dir; v.looping = pl.looping; if (pl.ended) v.dead = true; },
        /* pl moved on n frames at inc, its key held or not: the frames walked (n, or fewer if it ended: pl.ended; Infinity:
         * n was, and it never ends). A stretch at a time, as the firmware's samp.c walks its place: each a sum up to the
         * frame its way changes at (render's frame by frame: on by inc, back by it on a ping-pong's pass back; held in
         * the loop a FORWARD loop's end takes it back by the loop L, a PING-PONG's ends mirror it; let go, out of the loop
         * at its end, or a pass back's turn; then past END, or under the sample's start, the end) */
        go(pl, P, inc, n, held) {
            const S = inc, LS = P.ls, LE = P.le, L = LE - LS, E1 = P.end - 1, pp = P.mode === "PING-PONG", inL = S <= L;
            let done = 0, k;                             /* (the frames walked; a stretch's frames: n and done move by them) */
            while (n > 0 && !pl.ended) {
                const x = pl.pos;
                if (!pl.looping) {                           /* out of its loop: on past END; back under the sample's start,
                                                  * or past END at once if the frame back stands there (in it by its
                                                  * flag alone, as render's: a loop with no room too) */
                    if (pl.dir > 0) {
                        k = rise(x, E1, S);
                        if (k > n) { pl.pos += n * S; return done + n; }
                        pl.pos += k * S;
                        pl.ended = true;
                        return done + k;
                    }
                    k = x - S >= E1 ? 1 : fall(x, S, 0);
                    if (k > n) { pl.pos -= n * S; return done + n; }
                    pl.pos -= k * S;
                    pl.ended = true;
                    return done + k;
                }
                if (!pp && pl.dir < 0) {                     /* a FORWARD loop going back (LOOP turned from PING-PONG on a
                                                  * pass back): under the sample's start; past the loop's end, out of
                                                  * it let go, or round it each frame held, back by the step and the loop */
                    if (x - S < LE) {
                        k = fall(x, S, 0);
                        if (k > n) { pl.pos -= n * S; return done + n; }
                        pl.pos -= k * S;
                        pl.ended = true;
                        return done + k;
                    }
                    if (!held) {
                        pl.pos = x - S; pl.looping = false; n--; done++;
                        if (pl.pos >= E1) { pl.ended = true; return done; }
                        continue;
                    }
                    if (S + L <= 0) {                        /* (a loop no longer than the step back, one with no room: on
                                                  * by -L - S each frame, standing there or running away up for good) */
                        if (n === Infinity) return Infinity;
                        pl.pos -= n * (S + L);
                        return done + n;
                    }
                    k = fall(x, S + L, LE + S);
                    if (k > n) { pl.pos -= n * (S + L); return done + n; }
                    pl.pos -= k * (S + L);
                    n -= k; done += k;
                    continue;
                }
                if (!held) {                                 /* let go: to the loop's end and out of it, or a pass back to
                                                  * its start, turned there and out; on to END after */
                    if (pl.dir > 0) {
                        k = rise(x, LE, S);
                        if (k > n) { pl.pos += n * S; return done + n; }
                        pl.pos += k * S;
                    } else {
                        k = fall(x, S, LS);
                        if (k > n) { pl.pos -= n * S; return done + n; }
                        pl.pos = 2 * LS - (x - k * S);
                        pl.dir = 1;
                    }
                    pl.looping = false;
                    n -= k; done += k;
                    if (pl.pos >= E1) { pl.ended = true; return done; }
                    continue;
                }
                if (inL && x >= LS && x < LE) {              /* held in it, the step within its length: round a FORWARD
                                                  * loop, to and fro in a PING-PONG one (its two ways a round of 2 L) */
                    if (n === Infinity) return Infinity;
                    if (!pp) pl.pos = LS + (x - LS + n * S) % L;
                    else {
                        const u = ((pl.dir < 0 ? 2 * L - (x - LS) : x - LS) + n * S) % (2 * L);
                        pl.dir = u < L ? 1 : -1;
                        pl.pos = u < L ? LS + u : LS + 2 * L - u;
                    }
                    return done + n;
                }
                if (pl.dir < 0) {                            /* a PING-PONG's pass back (from past its end, or at a step
                                                  * past its length): to its start, turned there */
                    k = fall(x, S, LS);
                    if (k > n) { pl.pos -= n * S; return done + n; }
                    pl.pos = 2 * LS - (x - k * S);
                    pl.dir = 1;
                    n -= k; done += k;
                    continue;
                }
                if (x + S < LE) {                            /* on to the loop's end, up to the frame before the one that
                                                  * passes it */
                    k = rise(x, LE - S, S);
                    if (k > n) { pl.pos += n * S; return done + n; }
                    pl.pos += k * S;
                    n -= k; done += k;
                    continue;
                }
                if (!pp) {                                   /* a FORWARD loop held past its end (started there, or its
                                                  * end moved): round each frame, nearer by L - S, into it; at a step
                                                  * past its length, round each frame running away by S - L (it never
                                                  * ends) */
                    if (S >= L) { if (n === Infinity) return Infinity; pl.pos += n * (S - L); return done + n; }
                    k = fall(x, L - S, LE - S);
                    if (k > n) { pl.pos -= n * (L - S); return done + n; }
                    pl.pos -= k * (L - S);
                    n -= k; done += k;
                    continue;
                }
                /* a PING-PONG's, going on past its end (or at a step past its length): mirrored there, and its pass back
                 * turned at its start the frame after (a turn of two frames, on by 2 (S - L): nearer while the step is
                 * within the loop, farther out else) while the turn is whole; ended as a mirror at its end lands before
                 * the sample's start */
                const d = 2 * (S - L), Tf = LE + L - 2 * S, Te = 2 * LE - S;
                if (x > Te) { pl.pos = 2 * LE - x - S; pl.dir = -1; pl.ended = true; return done + 1; }
                if (x > Tf && n >= 2) {
                    let t = d < 0 ? Math.ceil((x - Tf) / -d) : d > 0 ? Math.floor((Te - x) / d) + 1 : Infinity;
                    if (t === Infinity && n === Infinity) return Infinity;
                    t = Math.min(t, Math.floor(n / 2));
                    pl.pos += t * d;
                    n -= 2 * t; done += 2 * t;
                    continue;
                }
                pl.pos = 2 * LE - x - S;                     /* (a frame: mirrored at its end, back) */
                pl.dir = -1;
                n--; done++;
            }
            return done;
        },
        /* the frames from pl until its note ends by itself, by its place, at inc (Infinity: held in its loop, never); the
         * walk on a copy of it, one kept for that */
        endFrames(pl, P, inc, held) {
            if (pl.ended) return 0;
            const q = PLACE;
            q.pos = pl.pos; q.dir = pl.dir; q.looping = pl.looping; q.ended = false;
            const k = E.samp.go(q, P, inc, Infinity, held);
            return q.ended ? k : Infinity;
        },
    };
    E.engines.SAMPLER = {
        start(v, p, midi, m, proc) {
            const s = proc.samples[p.key];
            v.s = s;
            v.p = p;
            if (!s) { v.dead = true; return; }
            v.end = Math.min(p.end || s.data.length, s.data.length);
            v.pos = Math.min((p.start || 0) + (m.offset || 0), v.end - 1);
            v.dir = 1;
            v.looping = E.samp.loops(p, E.samp.facts(p, s));   /* (a loop with no room: none for this note, as the
                                                         * firmware's start refuses it) */
            v.env = E.opEnv.make(p.env.rates, p.env.levels);
            v.g0 = v.g1 = 0;
            v.n = 0;
        },
        /* the forward work (dsp-core.js: a ghost coming back, as the firmware's samp_forward): its envelope worked on in
         * its own 64-frame steps, w.on held, then (w.off >= 0) its note off and w.off more, spending steps.n (a stretch
         * a step, the envelope standing read once a step; the note off a step). Its place is not moved here: it is the
         * ghost's own (E.samp.go), put on it as it comes back. A voice that does not sound is done at once */
        forward(v, w, steps) {
            if (v.dead) { w.on = 0; if (w.off >= 0) w.off = 0; return true; }
            for (;;) {
                if (!w.on && (w.off < 0 || (!w.off && v.releasing))) return true;
                if (steps.n <= 0) return false;
                if (w.on) w.on -= sRun(v, w.on, steps);
                else if (!v.releasing) { this.release(v); steps.n--; }
                else w.off -= sRun(v, w.off, steps);
            }
        },
        /* the most it still reaches: its envelope's gain (a stage rising counted at its target; the last step's, which
         * the next frames glide from), as a full-scale sample plays */
        heard: v => v.dead ? 0 : Math.max(E.opEnv.gain(E.opEnv.top(v.env)), v.g1),
        units: (f, up) => up ? Math.ceil(f / 64) : Math.floor(f / 64),   /* (its envelope's steps in f frames) */
        active: v => !v.dead && !E.opEnv.done(v.env),   /* (it still sounds: render's) */
        /* worked forward over the steps before frame f of its note: the step of the 64 frames f falls in made now and
         * glided across from f's place in them, so its next steps fall where the note never taken's do; let go within
         * them (rel), its note off after that step */
        resume(v, f, rel) {
            const r = f % 64;
            v.n = 0;
            if (r) { v.g0 = v.g1; v.g1 = E.opEnv.step(v.env); v.n = 64 - r; }
            if (rel) this.release(v);
        },
        /* the patch changed by parameter `code` (dsp-core.js's paramOn): its END, and its loop by the loop rule
         * (E.samp.unloops); a note with no sample (dead: nothing made) is left as it is */
        refresh(v, code) {
            if (v.dead) return;
            const p = v.p, P = E.samp.facts(p, v.s);
            v.end = P.end;
            if (E.samp.unloops(code, p, P)) v.looping = false;
            E.opEnv.retune(v.env, p.env.rates, p.env.levels);
        },
        release(v) { v.releasing = true; if (v.env) E.opEnv.release(v.env); },
        render(v, out, a, b) {
            if (v.dead) return false;
            const p = v.p, d = v.s.data;
            const inc = v.s.rate / SR * Math.pow(2, (v.midi + (v.bend || 0) - p.base + E.samp.tune(p)) / 12);
            /* (in its loop by its flag alone, as the firmware's: a loop left with no room by its instrument's edit while a
             * ghost (dsp-core.js's ready) loops as it stands, on one sample or running away past END for good) */
            const ls = p.loopStart, le = Math.min(p.loopEnd, v.end);
            for (let i = a; i < b; i++) {
                const loop = v.looping;
                if (v.n-- <= 0) { v.g0 = v.g1; v.g1 = E.opEnv.step(v.env); v.n = 63; }
                const g = v.g1 + (v.g0 - v.g1) * v.n / 64;
                const ip = Math.floor(v.pos), fr = v.pos - ip;
                const nx = ip + 1 < (loop ? le : v.end) ? ip + 1 : loop && p.loop === "FORWARD" ? ls : ip;
                const x = (d[ip] || 0) * (1 - fr) + (d[nx] || 0) * fr;
                out[i] = x * g;
                v.pos += inc * v.dir;
                if (loop) {
                    if (p.loop === "PING-PONG") {
                        if (v.dir > 0 && v.pos >= le) {
                            if (v.releasing) v.looping = false;
                            else { v.pos = 2 * le - v.pos; v.dir = -1; }
                        } else if (v.dir < 0 && v.pos < ls) {
                            v.pos = 2 * ls - v.pos; v.dir = 1;
                            if (v.releasing) v.looping = false;
                        }
                    } else if (v.pos >= le) {
                        if (v.releasing) v.looping = false;
                        else v.pos -= le - ls;
                    }
                }
                if (v.pos < 0 || (v.pos >= v.end - 1 && !v.looping)) return false;
            }
            return !E.opEnv.done(v.env);
        },
    };
});

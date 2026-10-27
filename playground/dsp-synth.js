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
    const J_TICKS16 = 16 * (1000 / 4.2335) / SR;                   /* the voice CPU's loop passes in 16 frames */
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
    /* every 16 frames: the loop's passes due, the filter's seed, the cutoff CV smoothed, the coefficients */
    function junoChunk(v) {
        v.tk += J_TICKS16;
        if (v.tk >= 1) { v.tk -= 1; voiceTick(v); }
        const energy = Math.max(Math.abs(v.s0) + Math.abs(v.s1) + Math.abs(v.s2) + Math.abs(v.s3), v.inEnv);
        v.seed = .00125 / (1 + 1000 * energy);   /* white noise into the filter: what starts a self-oscillation */
        v.dacS += (v.dac - v.dacS) * J_VCF_SLEW;
        const code = E.clamp(Math.floor(v.dacS) + v.resOff, 0, 16383) >> 2, g1 = J.VCF_G[code], fade = J.VCF_F[code];
        const k = fade === 255 ? v.k : v.k * fade / 255, g2 = g1 * g1;
        v.g1 = g1; v.g2 = g2; v.g3 = g2 * g1; v.om = 1 - g1;
        v.invden = 1 / (1 + k * g2 * g2);
        v.kk = k;
    }
    /* the chorus's delay line (written at w) read d frames back, between two of its samples */
    function bbd(cb, w, d) {
        const n = Math.floor(d), at = w - n, a = cb[at & 255];
        return a + (cb[(at - 1) & 255] - a) * (d - n);
    }
    E.engines.JUNO = {
        /* a legato note (LEG): the VCF's key follow moves to it (the pitch comes from the bend) */
        key(v, midi) { v.pitch88 = E.clamp(midi, 0, 127) << 8; },
        start(v, p, midi) {
            v.p = p;
            this.refresh(v);
            v.pitch88 = E.clamp(midi, 0, 127) << 8;
            v.holdAcc = 0; v.inHold = v.holdInc < ENV_MAX; v.rampAcc = 0; v.lfoAmp = 0;
            v.lfoAcc = 0; v.lfoRising = 0; v.lfoPol = 0;
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
            v.s0 = -2 * a; v.s1 = 0; v.s2 = a; v.s3 = a;
            v.pw = .5 - (v.pwMan ? v.pwm : v.pwm >> 1) * 58982 / 4294967296;
            vcaTarget(v);
            v.dc = 0; v.hp0 = 0; v.hp1 = 0;
            v.tk = .5; v.c16 = 0;                       /* the loop runs on its own clock: half a pass to the next */
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
        render(v, out, a, b) {
            const nz = noise(), cho = v.cho, hpf = v.p.hpf, hpg = J_HPF[hpf], B = J.BB;
            const gSaw = v.gSaw, gPulse = v.gPulse, gSub = v.gSub, gNoise = v.gNoise, kfb = v.kfb, nbuf = nz.buf, white = nz.white;
            let i = a;
            while (i < b) {
                if (v.c16 === 0) { junoChunk(v); v.c16 = 16; }
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
    E.params.SAMPLER = function (p, c, v, proc) {
        const s = proc.samples[p.key], len = s ? s.data.length : 0, at = Math.round(Math.min(v, 255) / 256 * len);
        if (c === 0x40) p.start = at;
        else if (c === 0x41) p.end = v >= 255 ? len : at;
        else if (c === 0x42) p.loop = ["OFF", "FORWARD", "PING-PONG"][Math.min(2, v)];
        else if (c === 0x43) p.loopStart = at;
        else if (c === 0x44) p.loopEnd = v >= 255 ? len : at;
        else if (c === 0x45) p.tune = v - 128;
        else if (c >= 0x48 && c <= 0x4B) p.env.rates[c - 0x48] = Math.min(99, v);
        else if (c >= 0x4C && c <= 0x4F) p.env.levels[c - 0x4C] = Math.min(99, v);
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
            v.looping = p.loop !== "OFF";
            v.env = E.opEnv.make(p.env.rates, p.env.levels);
            v.g0 = v.g1 = 0;
            v.n = 0;
        },
        refresh(v) {
            const p = v.p;
            v.end = Math.min(p.end || v.s.data.length, v.s.data.length);
            v.looping = v.looping && p.loop !== "OFF";
            E.opEnv.retune(v.env, p.env.rates, p.env.levels);
        },
        release(v) { v.releasing = true; E.opEnv.release(v.env); },
        render(v, out, a, b) {
            if (v.dead) return false;
            const p = v.p, d = v.s.data;
            const inc = v.s.rate / SR * Math.pow(2, (v.midi + (v.bend || 0) - p.base + (p.tune || 0) / 128) / 12);
            const ls = p.loopStart, le = Math.min(p.loopEnd, v.end), room = le > ls + 1;
            for (let i = a; i < b; i++) {
                const loop = room && v.looping;
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
                if (v.pos < 0 || (v.pos >= v.end - 1 && !(room && v.looping))) return false;
            }
            return !E.opEnv.done(v.env);
        },
    };
});

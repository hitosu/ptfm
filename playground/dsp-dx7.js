/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground, sound: the DX7, after MSFA (Dexed's engine, Apache-2.0): its 32 algorithms,
 * its envelopes, output levels, keyboard scaling, frequencies and feedback, the pitch envelope and
 * the LFO, in plain JS numbers. Close to the ear, not bit-exact: the firmware's engine is the
 * reference. The envelopes move every 64 samples, as MSFA's; the gains glide between. */
PTFM.dsp = (PTFM.dsp || []).concat(function (E) {
    "use strict";
    const SR = E.SR, N = 64, LG_N = 6;
    /* the algorithms: per operator, OP6 first: bits 0-1 the bus it writes (0 the output), 4 adds
     * to it, 4-5 << 4 the bus it reads, 0xc0 self-feedback (MSFA's fm_core.cc) */
    const ALG = [
        [0xc1, 0x11, 0x11, 0x14, 0x01, 0x14], [0x01, 0x11, 0x11, 0x14, 0xc1, 0x14], [0xc1, 0x11, 0x14, 0x01, 0x11, 0x14],
        [0xc1, 0x11, 0x94, 0x01, 0x11, 0x14], [0xc1, 0x14, 0x01, 0x14, 0x01, 0x14], [0xc1, 0x94, 0x01, 0x14, 0x01, 0x14],
        [0xc1, 0x11, 0x05, 0x14, 0x01, 0x14], [0x01, 0x11, 0xc5, 0x14, 0x01, 0x14], [0x01, 0x11, 0x05, 0x14, 0xc1, 0x14],
        [0x01, 0x05, 0x14, 0xc1, 0x11, 0x14], [0xc1, 0x05, 0x14, 0x01, 0x11, 0x14], [0x01, 0x05, 0x05, 0x14, 0xc1, 0x14],
        [0xc1, 0x05, 0x05, 0x14, 0x01, 0x14], [0xc1, 0x05, 0x11, 0x14, 0x01, 0x14], [0x01, 0x05, 0x11, 0x14, 0xc1, 0x14],
        [0xc1, 0x11, 0x02, 0x25, 0x05, 0x14], [0x01, 0x11, 0x02, 0x25, 0xc5, 0x14], [0x01, 0x11, 0x11, 0xc5, 0x05, 0x14],
        [0xc1, 0x14, 0x14, 0x01, 0x11, 0x14], [0x01, 0x05, 0x14, 0xc1, 0x14, 0x14], [0x01, 0x14, 0x14, 0xc1, 0x14, 0x14],
        [0xc1, 0x14, 0x14, 0x14, 0x01, 0x14], [0xc1, 0x14, 0x14, 0x01, 0x14, 0x04], [0xc1, 0x14, 0x14, 0x14, 0x04, 0x04],
        [0xc1, 0x14, 0x14, 0x04, 0x04, 0x04], [0xc1, 0x05, 0x14, 0x01, 0x14, 0x04], [0x01, 0x05, 0x14, 0xc1, 0x14, 0x04],
        [0x04, 0xc1, 0x11, 0x14, 0x01, 0x14], [0xc1, 0x14, 0x01, 0x14, 0x04, 0x04], [0x04, 0xc1, 0x11, 0x14, 0x04, 0x04],
        [0xc1, 0x14, 0x04, 0x04, 0x04, 0x04], [0xc4, 0x04, 0x04, 0x04, 0x04, 0x04],
    ];
    E.DX7_ALG = ALG;
    const LEVELLUT = [0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46];
    const scaleOut = l => l >= 20 ? 28 + l : LEVELLUT[l];
    const COARSE = [-16777216, 0, 16777216, 26591258, 33554432, 38955489, 43368474, 47099600, 50331648, 53182516,
                    55732705, 58039632, 60145690, 62083076, 63876816, 65546747, 67108864, 68576247, 69959732, 71268397,
                    72509921, 73690858, 74816848, 75892776, 76922906, 77910978, 78860292, 79773775, 80654032, 81503396,
                    82323963, 83117622];
    const VEL = [0, 70, 86, 97, 106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163, 166, 170, 173, 174, 178, 181,
                 184, 186, 189, 190, 194, 196, 198, 200, 202, 205, 206, 209, 211, 214, 216, 218, 220, 222, 224, 225, 227,
                 229, 230, 232, 233, 235, 237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251, 252, 253, 254];
    const EXPSCALE = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33, 39, 47, 56, 66, 80, 94, 110, 126, 142, 158,
                      174, 190, 206, 222, 238, 250];
    const CURVES = { "-lin": 0, "-exp": 1, "+exp": 2, "+lin": 3 };
    const PE_RATE = [1, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 16, 17,
                     18, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 30, 31, 33, 34, 36, 37, 38, 39, 41, 42, 44, 46, 47, 49,
                     51, 53, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 79, 82, 85, 88, 91, 94, 98, 102, 106, 110, 115,
                     120, 125, 130, 135, 141, 147, 153, 159, 165, 171, 178, 185, 193, 202, 211, 232, 243, 254, 255];
    const PE_TAB = [-128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43, -41, -39, -37, -35, -33, -32, -31,
                    -30, -29, -28, -27, -26, -25, -24, -23, -22, -21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11,
                    -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
                    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 38, 40, 43, 46, 49, 53, 58, 65,
                    73, 82, 92, 103, 115, 127];
    const LFO_HZ = [0.0625, 0.125, 0.312, 0.437, 0.625, 0.751, 0.936, 1.125, 1.25, 1.437, 1.561, 1.752, 1.875, 2.062, 2.247,
                    2.374, 2.56, 2.687, 2.874, 2.999, 3.188, 3.37, 3.5, 3.682, 3.812, 4.001, 4.186, 4.311, 4.501, 4.623,
                    4.815, 4.93, 5.122, 5.315, 5.435, 5.617, 5.75, 5.947, 6.063, 6.248, 6.432, 6.564, 6.749, 6.868, 7.052,
                    7.251, 7.376, 7.556, 7.688, 7.878, 7.994, 8.182, 8.372, 8.505, 8.685, 8.811, 8.986, 9.122, 9.301, 9.5,
                    9.608, 9.798, 9.95, 10.117, 11.251, 11.384, 12.563, 13.676, 13.904, 15.092, 16.367, 16.639, 17.87,
                    19.194, 19.425, 20.833, 21.035, 22.502, 24.004, 24.26, 25.747, 27.174, 27.579, 29.053, 30.694, 31.192,
                    32.658, 34.317, 34.674, 36.417, 38.197, 38.551, 40.388, 40.75, 42.626, 44.326, 44.883, 46.773, 48.591,
                    49.261];
    const PMS = [0, 10, 20, 33, 55, 92, 153, 255], AMS = [0, 4342338 / 16777216, 7171437 / 16777216, 1];
    const WAVES = ["triangle", "saw down", "saw up", "square", "sine", "sample&hold"];

    function scaleCurve(group, depth, curve) {
        let s = curve === 0 || curve === 3 ? (group * depth * 329) >> 12 : (EXPSCALE[Math.min(group, 32)] * depth * 329) >> 15;
        return curve < 2 ? -s : s;
    }
    function scaleLevel(midi, k) {
        const off = midi - k.break_point - 17;
        return off >= 0 ? scaleCurve(((off + 1) / 3) | 0, k.right_depth, CURVES[k.right_curve])
                        : scaleCurve((-(off - 1) / 3) | 0, k.left_depth, CURVES[k.left_curve]);
    }
    const scaleVel = (vel, sens) => (((sens * (VEL[Math.min(127, vel) >> 1] - 239) + 7) >> 3) << 4);
    const scaleRate = (midi, sens) => (sens * Math.min(31, Math.max(0, ((midi / 3) | 0) - 7))) >> 3;
    function logfreq(midi, op) {
        let lf;
        if (op.mode === "ratio") {
            lf = (1 << 24) * (Math.log2(440) - 69 / 12) + midi * (1 << 24) / 12;
            const dr = 0.0209 * Math.exp(-0.396 * (lf / (1 << 24))) / 7;
            lf += dr * lf * op.detune;
            lf += COARSE[op.coarse & 31];
            if (op.fine) lf += Math.floor(24204406.323123 * Math.log(1 + 0.01 * op.fine) + 0.5);
        } else {
            lf = (4458616 * ((op.coarse & 3) * 100 + op.fine)) >> 3;
            lf += op.detune > 0 ? 13457 * op.detune : 0;
        }
        return lf;
    }

    /* an envelope (MSFA's env.cc, its accurate envelope), level in its units << 16. A stage whose level
     * is already there holds for a time its rate sets (frames), as on the DX7 */
    const STATICS = [1764000, 1764000, 1411200, 1411200, 1190700, 1014300, 992250, 882000, 705600, 705600, 584325, 507150,
        502740, 441000, 418950, 352800, 308700, 286650, 253575, 220500, 220500, 176400, 145530, 145530,
        125685, 110250, 110250, 88200, 88200, 74970, 61740, 61740, 55125, 48510, 44100, 37485, 31311, 30870,
        27562, 27562, 22050, 18522, 17640, 15435, 14112, 13230, 11025, 9261, 9261, 7717, 6615, 6615, 5512,
        5512, 4410, 3969, 3969, 3439, 2866, 2690, 2249, 1984, 1896, 1808, 1411, 1367, 1234, 1146, 926, 837,
        837, 705, 573, 573, 529, 441, 441];
    function envAdvance(e, ix) {
        e.ix = ix;
        if (ix < 4) {
            const nl = e.levels[ix];
            let act = (scaleOut(nl) >> 1 << 6) + e.out - 4256;
            act = act < 16 ? 16 : act;
            e.target = act * 65536;
            e.rising = e.target > e.level;
            let q = (e.rates[ix] * 41) >> 6;
            q = Math.min(q + e.rs, 63);
            if (e.target === e.level || (ix === 0 && nl === 0)) {
                const sr = Math.min(e.rates[ix] + e.rs, 99);
                e.st = sr < 77 ? STATICS[sr] : 20 * (99 - sr);
                if (sr < 77 && ix === 0 && nl === 0) e.st = Math.floor(e.st / 20);   /* (an attack's: faster) */
            } else e.st = 0;
            e.inc = (4 + (q & 3)) * Math.pow(2, 2 + LG_N + (q >> 2));
        }
    }
    function envStep(e) {
        if (e.st) {
            e.st -= N;
            if (e.st <= 0) { e.st = 0; envAdvance(e, e.ix + 1); }
        }
        if (e.ix < 3 || (e.ix < 4 && !e.down)) {
            if (e.st) { /* holding */ }
            else if (e.rising) {
                const jump = 1716 * 65536;
                if (e.level < jump) e.level = jump;
                e.level += Math.floor(((17 * 16777216) - e.level) / 16777216) * e.inc;
                if (e.level >= e.target) { e.level = e.target; envAdvance(e, e.ix + 1); }
            } else {
                e.level -= e.inc;
                if (e.level <= e.target) { e.level = e.target; envAdvance(e, e.ix + 1); }
            }
        }
        return e.level;
    }
    function peAdvance(p, ix) {
        p.ix = ix;
        if (ix < 4) {
            p.target = PE_TAB[p.levels[ix]] / 32;          /* octaves */
            p.rising = p.target > p.level;
            p.inc = PE_RATE[p.rates[ix]] / 21.3 * N / SR;
        }
    }
    function peStep(p) {
        if (p.ix < 3 || (p.ix < 4 && !p.down)) {
            if (p.rising) { p.level += p.inc; if (p.level >= p.target) { p.level = p.target; peAdvance(p, p.ix + 1); } }
            else { p.level -= p.inc; if (p.level <= p.target) { p.level = p.target; peAdvance(p, p.ix + 1); } }
        }
        return p.level;
    }

    /* the forward work (a ghost's return, dsp-core.js; the firmware's dx7_forward): an envelope worked on many blocks
     * at once, a stretch at a time. Within a stretch its change a block is the same (a hold's count, a falling stage's
     * step, a rising stage's step within one of the 17 bands of its level), so its blocks are one sum, and the block
     * that may end it is envStep's own. The levels are whole numbers here as in MSFA, so the sums are exact */
    const JUMP = 1716 * 65536, U24 = 16777216;
    const RUN_AT = new Float64Array(7), RUN_SPAN = new Float64Array(7);   /* (run's: each envelope's place and stretch) */
    /* the blocks of e's stretch from where it is, the last of them the one that may end it; 0: it stands for good (a
     * sustain held, a release ended) */
    function envSpan(e) {
        if (e.st) return Math.floor((e.st + N - 1) / N);
        if (!(e.ix < 3 || (e.ix < 4 && !e.down))) return 0;
        if (!e.rising) return e.level > e.target ? Math.floor((e.level - e.target - 1) / e.inc) + 1 : 1;
        const l = e.level < JUMP ? JUMP : e.level, m = Math.floor((17 * U24 - l) / U24);
        if (m <= 0) {                                   /* (above the patches' ranges: standing, or down a band at a time) */
            if (!m) return l < e.target ? 0 : 1;
            return Math.floor((l - (16 - m) * U24 - 1) / (-m * e.inc)) + 1;
        }
        const d = m * e.inc, to = e.target > l ? Math.floor((e.target - l - 1) / d) + 1 : 1;
        return Math.min(to, Math.floor(((17 - m) * U24 - l) / d) + 1);   /* (to its target, or out of its band) */
    }
    /* e moved on n blocks of its stretch (span: envSpan's, n <= span): those before the last in one sum, the last by
     * envStep itself */
    function envGo(e, n, span) {
        if (!span) return;
        const last = n === span;
        if (last) n--;
        if (n) {
            if (e.st) e.st -= n * N;
            else if (e.rising) { const l = e.level < JUMP ? JUMP : e.level; e.level = l + n * Math.floor((17 * U24 - l) / U24) * e.inc; }
            else e.level -= n * e.inc;
        }
        if (last) envStep(e);
    }
    /* the pitch envelope's stretch: a stage to its target */
    function peSpan(p) {
        if (!(p.ix < 3 || (p.ix < 4 && !p.down))) return 0;
        const gap = p.rising ? p.target - p.level : p.level - p.target;
        return gap <= 0 ? 1 : Math.max(1, Math.ceil(gap / p.inc));
    }
    function peGo(p, n, span) {
        if (!span) return;
        const last = n === span;
        if (last) n--;
        p.level += (p.rising ? 1 : -1) * n * p.inc;
        if (last) peStep(p);
    }
    /* the LFO moved on n blocks: its time and phase in one sum; the sample-and-hold a new value once a cycle crossed */
    function lfoGo(L, n) {
        const ph0 = L.ph;
        L.t += n * N / SR;
        L.ph += n * L.hz * N / SR;
        if (L.wave === 5)                /* (the FM-1's generator, a step each cycle crossed; it comes round in 128) */
            for (let k = (Math.floor(L.ph) - Math.floor(ph0)) % 128; k > 0; k--) L.rs = (L.rs * 179 + 17) & 255;
    }
    /* an envelope's gain at its most from now on: a stage rising now (no hold) counted at its target */
    const envTop = e => (e.rising && !e.st && (e.ix < 3 || (e.ix < 4 && !e.down)) ? e.target : e.level);

    /* an operator's envelope on its own, for the sampler's (specs/files/instruments.md: "as a
     * DX7 operator's"): its gain 0-1, moved every 64 samples */
    E.opEnv = {
        make(rates, levels) {
            const e = { rates: rates.map(x => Math.min(99, x)), levels: levels.map(x => Math.min(99, x)), out: 127 << 5, rs: 0, level: 0, down: true };
            envAdvance(e, 0);
            return e;
        },
        step: e => Math.pow(2, envStep(e) / 16777216 - 15),
        release(e) { e.down = false; envAdvance(e, 3); },
        retune(e, rates, levels) { e.rates = rates.map(x => Math.min(99, x)); e.levels = levels.map(x => Math.min(99, x)); envAdvance(e, e.ix); },
        done: e => !e.down && e.ix >= 4 && e.level < 1716 * 65536 * 1.6,
        span: envSpan, go: envGo,                       /* (the forward work's, as the DX7's) */
        gain: l => Math.pow(2, Math.min(l, 20 * U24) / U24 - 15),   /* (step's, at a level) */
        top: envTop,
    };

    /* the DX7's parameter commands (specs/files/commands.md): eight codes an operator from 0x40, OP1
     * first; then the voice's */
    const LFO_WAVES = ["triangle", "saw down", "saw up", "square", "sine", "sample&hold"];
    E.params.DX7 = function (p, c, v) {
        if (c >= 0x40 && c < 0x70) {
            const op = p.ops[(c - 0x40) >> 3], k = (c - 0x40) & 7;
            if (k === 0) op.level = Math.min(99, v);
            else if (k === 1) op.coarse = Math.min(31, v);
            else if (k === 2) op.fine = Math.min(99, v);
            else if (k === 3) op.detune = Math.min(14, v) - 7;
            else op.eg.rates[k - 4] = Math.min(99, v);
            return;
        }
        const L = p.lfo;
        switch (c) {
            case 0x70: p.algorithm = Math.min(31, v) + 1; break;
            case 0x71: p.feedback = Math.min(7, v); break;
            case 0x72: p.osc_key_sync = !!v; break;
            case 0x73: p.transpose = Math.min(48, v) - 24; break;
            case 0x74: L.speed = Math.min(99, v); break;
            case 0x75: L.delay = Math.min(99, v); break;
            case 0x76: L.pm_depth = Math.min(99, v); break;
            case 0x77: L.am_depth = Math.min(99, v); break;
            case 0x78: L.wave = LFO_WAVES[Math.min(5, v)]; break;
            case 0x79: L.key_sync = !!v; break;
            case 0x7A: L.pitch_mod_sens = Math.min(7, v); break;
        }
    };
    function opOut(op, midi, vel) {
        let out = scaleOut(op.level) + scaleLevel(midi, op.kbd);
        out = Math.min(127, out) << 5;
        return Math.max(0, out + scaleVel(vel, op.key_vel_sens));
    }
    function lfoOf(p, ph) {
        return { hz: LFO_HZ[p.lfo.speed], ph, wave: WAVES.indexOf(p.lfo.wave),
                 pmd: (p.lfo.pm_depth * 165 >> 6) / 255 * PMS[p.lfo.pitch_mod_sens & 7] / 255,
                 amd: (p.lfo.am_depth * 165 >> 6) / 256, delay: p.lfo.delay, t: 0, rs: 0 };   /* (rs: the sample-and-hold's
                                                         * 8-bit generator, as a fresh engine's on the FM-1) */
    }

    E.engines.DX7 = {
        /* (last: its engine's last DX7 note, as the FM-1's dx7_note_on finds it there: the operators' phases go on unless
         * OSC KEY SYNC, the LFO's phase unless its KEY SYNC, its sample-and-hold's generator always; none: a fresh
         * engine's zeros) */
        /* (m.vol: its volume at its start, 0-127, its modulators' velocity: harder, brighter; the carriers' (the
         * operators on the output) 100, the FM-1's PL_VEL, its loudness its volume's gain) */
        start(v, p, midi, m, proc, last) {
            const vel = m.vel !== undefined ? m.vel : m.vol === undefined ? 100 : m.vol, was = last ? last.st : null,
                  ph = was && !p.osc_key_sync,
                  alg = ALG[p.algorithm - 1];
            v.p = p;
            v.vel = vel;
            v.ops = [];
            for (let i = 0; i < 6; i++) {
                const op = p.ops[5 - i];                     /* MSFA's order: OP6 first */
                const e = { rates: op.eg.rates.map(x => Math.min(99, x)), levels: op.eg.levels.map(x => Math.min(99, x)),
                            out: opOut(op, midi, (alg[i] & 7) === 4 ? 100 : vel), rs: scaleRate(midi, op.rate_scaling),
                            level: 0, down: true };
                envAdvance(e, 0);
                v.ops.push({ e, lf: logfreq(midi, op), fixed: op.mode !== "ratio", ph: ph ? was.ops[i].ph : 0, g0: 0, g1: 0, y: 0, ams: AMS[op.amp_mod_sens & 3] });
            }
            v.alg = ALG[p.algorithm - 1];
            v.fbs = p.feedback ? Math.pow(2, -(8 - p.feedback)) : 0;
            v.fb0 = 0; v.fb1 = 0;
            v.pe = { rates: p.pitch_eg.rates, levels: p.pitch_eg.levels, level: PE_TAB[p.pitch_eg.levels[3]] / 32, down: true };
            peAdvance(v.pe, 0);
            v.lfo = lfoOf(p, p.lfo.key_sync ? .5 : was ? was.lfo.ph : 0);   /* (MSFA's key sync: half a cycle in; else on
                                                         * from its engine's last DX7 note there, a fresh engine's 0) */
            if (was) v.lfo.rs = was.lfo.rs;
            v.transpose = p.transpose || 0;
            v.n = 0;
            v.bus = [new Float32Array(N), new Float32Array(N)];
            v.out = new Float32Array(N);
            v.left = 0;
            v.quiet = 0;
            v.rel = false;                               /* (let go: its own, its quiet count's) */
            v.midi0 = midi;
        },
        release(v) {
            for (const o of v.ops) { o.e.down = false; envAdvance(o.e, 3); }
            v.pe.down = false; peAdvance(v.pe, 3);
            v.rel = true;
        },
        /* the forward work (dsp-core.js: a ghost coming back, as the firmware's dx7_forward): a note just started worked
         * on without its sound, in its own 64-frame blocks, w.on blocks held, then (w.off >= 0) its note off and w.off
         * blocks more, spending steps.n (a stretch a step; its note off 7). True when done; false when the budget ran
         * out first, called again it goes on from where it is. Its six envelopes, its pitch envelope and its LFO come
         * out as those blocks leave them; the operators' phases are not kept */
        forward(v, w, steps) {
            for (;;) {
                if (!w.on && (w.off < 0 || (!w.off && !v.pe.down))) return true;
                if (steps.n <= 0) return false;
                if (w.on) w.on -= this.run(v, w.on, steps);
                else if (v.pe.down) { this.release(v); steps.n -= 7; }
                else w.off -= this.run(v, w.off, steps);
            }
        },
        /* the envelopes moved on together by up to n blocks, their stretches taken in the order they end (a step
         * each), so that when the budget runs out they all stand at one block; then the LFO as far (a step). The
         * blocks gone */
        run(v, n, steps) {
            const at = RUN_AT, span = RUN_SPAN, ops = v.ops;   /* (kept arrays: a return's work makes nothing) */
            let to = n;
            at.fill(0);
            for (let i = 0; i < 7; i++) { span[i] = i < 6 ? envSpan(ops[i].e) : peSpan(v.pe); if (span[i]) steps.n--; }
            for (;;) {
                let c = -1, end = n;
                for (let i = 0; i < 7; i++) if (span[i] && span[i] < end - at[i]) { end = at[i] + span[i]; c = i; }
                if (c < 0) break;
                if (c < 6) { envGo(ops[c].e, span[c], span[c]); span[c] = envSpan(ops[c].e); }
                else { peGo(v.pe, span[c], span[c]); span[c] = peSpan(v.pe); }
                at[c] = end;
                if (--steps.n <= 0) { to = end; break; }
            }
            for (let i = 0; i < 6; i++) envGo(ops[i].e, to - at[i], span[i]);
            peGo(v.pe, to - at[6], span[6]);
            lfoGo(v.lfo, to);
            steps.n--;
            return to;
        },
        /* the most the note still reaches (its carriers' envelopes, a stage rising counted at its target), on its
         * render's scale */
        heard(v) {
            let g = 0;
            for (let k = 0; k < 6; k++) if ((v.alg[k] & 3) === 0) g += Math.pow(2, Math.min(envTop(v.ops[k].e), 20 * U24) / U24 - 14);
            return g;
        },
        units: (f, up) => up ? Math.ceil(f / N) : Math.floor(f / N),   /* (its blocks in f frames from its start) */
        active: v => v.quiet < 8,                       /* (it still sounds: render's) */
        /* worked forward over the blocks before frame f of its note (its own 64-frame grid from its start): the block f
         * falls in made now (its step, its sound), read from f's place in it, so its next steps fall where the note never
         * taken's do; let go within that block (rel), its note off after it, as the note never taken's came between
         * its steps. Its operators' gains ramp up from 0 in that block, as a fresh engine's */
        resume(v, f, rel) {
            const r = f % N;
            v.left = 0;
            if (r) { this.block(v); v.left = N - r; }
            if (rel) this.release(v);
        },
        /* a parameter command under a sounding note: what it changes, again */
        refresh(v, c) {
            const p = v.p, midi = v.midi0;
            if (c >= 0x40 && c < 0x70) {
                const n = (c - 0x40) >> 3, op = p.ops[n], o = v.ops[5 - n];
                o.e.out = opOut(op, midi, (v.alg[5 - n] & 7) === 4 ? 100 : v.vel);
                o.e.rates = op.eg.rates.map(x => Math.min(99, x));
                o.lf = logfreq(midi, op);
                envAdvance(o.e, o.e.ix);
                return;
            }
            v.alg = ALG[p.algorithm - 1];
            v.fbs = p.feedback ? Math.pow(2, -(8 - p.feedback)) : 0;
            v.transpose = p.transpose || 0;
            const ph = v.lfo.ph, t = v.lfo.t;
            v.lfo = lfoOf(p, ph);
            v.lfo.t = t;
        },
        render(v, out, a, b) {
            let i = a;
            while (i < b) {
                if (v.left === 0) this.block(v);
                const n = Math.min(v.left, b - i), o0 = N - v.left;
                for (let k = 0; k < n; k++) out[i + k] = v.out[o0 + k];
                v.left -= n;
                i += n;
            }
            return v.quiet < 8;
        },
        /* 64 samples (MSFA's N), the envelopes and the LFO moved once */
        block(v) {
            const L = v.lfo;
            L.t += N / SR;
            L.ph += L.hz * N / SR;
            const ph = L.ph - Math.floor(L.ph);
            let lv;                                           /* 0..1, at MSFA's phases */
            switch (L.wave) {
                case 0: lv = ph < .5 ? ph * 2 : 2 - ph * 2; break;
                case 1: lv = ph < .5 ? .5 - ph : 1.5 - ph; break;
                case 2: lv = ph < .5 ? ph + .5 : ph - .5; break;
                case 3: lv = ph < .5 ? 1 : 0; break;
                case 4: lv = .5 + .5 * Math.sin(ph * 2 * Math.PI); break;
                default:                      /* sample and hold: 179x + 17 on 8 bits, a step as a cycle begins */
                    if (ph < L.hz * N / SR) L.rs = (L.rs * 179 + 17) & 255;
                    lv = ((L.rs ^ 0x80) + 1) / 256;
            }
            const dly = L.delay ? Math.min(1, Math.max(0, (L.t - L.delay * .02) / (L.delay * .03 + .01))) : 1;
            const pitch = peStep(v.pe) + L.pmd * dly * (lv - .5) + ((v.bend || 0) + v.transpose) / 12;
            const amd = L.amd * dly * (1 - lv);
            const base = Math.pow(2, pitch);
            const bus = v.bus, outb = v.out, has = [true, false, false];
            outb.fill(0);
            let loud = 0;
            for (let k = 0; k < 6; k++) {
                const o = v.ops[k], fl = v.alg[k];
                let level = envStep(o.e);
                /* amplitude modulation, MSFA's curve: at full depth the operator falls silent, and any AMP
                 * MOD SENS takes about 1 dB off even with no LFO */
                if (o.ams) level -= level * Math.exp(4.48 * o.ams * amd + 12.2) / 16777216;
                o.g0 = o.g1;
                o.g1 = Math.pow(2, level / 16777216 - 14);
                const hz = Math.pow(2, o.lf / 16777216) * (o.fixed ? 1 : base);
                const inc = hz / SR;
                const inb = (fl >> 4) & 3, outbus = fl & 3;
                let add = (fl & 4) !== 0;
                const dst = outbus === 0 ? outb : bus[outbus - 1];
                if (o.g0 < 1e-4 && o.g1 < 1e-4) { if (!add) has[outbus] = false; o.ph += inc * N; continue; }
                if (!has[outbus]) add = false;
                const src = inb && has[inb] ? bus[inb - 1] : null, fb = (fl & 0xc0) === 0xc0 && v.fbs;
                let ph = o.ph, g = o.g0;
                const dg = (o.g1 - o.g0) / N;
                for (let s = 0; s < N; s++) {
                    let mod = src ? src[s] : 0;
                    if (fb) mod += (v.fb0 + v.fb1) * .5 * v.fbs;
                    const y = E.sin(ph + mod) * g;
                    if (fb) { v.fb0 = v.fb1; v.fb1 = y; }
                    dst[s] = add ? dst[s] + y : y;
                    ph += inc;
                    g += dg;
                }
                o.ph = ph - Math.floor(ph);
                has[outbus] = true;
                if (outbus === 0) loud = Math.max(loud, o.g1);
            }
            v.quiet = v.rel && loud < 2e-4 ? v.quiet + 1 : 0;
            v.left = N;
        },
    };
});

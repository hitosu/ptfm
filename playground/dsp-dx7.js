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
                 amd: (p.lfo.am_depth * 165 >> 6) / 256, delay: p.lfo.delay, t: 0, sh: 0 };
    }

    E.engines.DX7 = {
        start(v, p, midi, m) {
            const vel = m.accent ? 127 : 100;
            v.p = p;
            v.vel = vel;
            v.ops = [];
            for (let i = 0; i < 6; i++) {
                const op = p.ops[5 - i];                     /* MSFA's order: OP6 first */
                const e = { rates: op.eg.rates.map(x => Math.min(99, x)), levels: op.eg.levels.map(x => Math.min(99, x)),
                            out: opOut(op, midi, vel), rs: scaleRate(midi, op.rate_scaling), level: 0, down: true };
                envAdvance(e, 0);
                v.ops.push({ e, lf: logfreq(midi, op), fixed: op.mode !== "ratio", ph: 0, g0: 0, g1: 0, y: 0, ams: AMS[op.amp_mod_sens & 3] });
            }
            v.alg = ALG[p.algorithm - 1];
            v.fbs = p.feedback ? Math.pow(2, -(8 - p.feedback)) : 0;
            v.fb0 = 0; v.fb1 = 0;
            v.pe = { rates: p.pitch_eg.rates, levels: p.pitch_eg.levels, level: PE_TAB[p.pitch_eg.levels[3]] / 32, down: true };
            peAdvance(v.pe, 0);
            v.lfo = lfoOf(p, p.lfo.key_sync ? .5 : Math.random());   /* (MSFA's key sync: half a cycle in) */
            v.transpose = p.transpose || 0;
            v.n = 0;
            v.bus = [new Float32Array(N), new Float32Array(N)];
            v.out = new Float32Array(N);
            v.left = 0;
            v.quiet = 0;
            v.midi0 = midi;
        },
        release(v) {
            for (const o of v.ops) { o.e.down = false; envAdvance(o.e, 3); }
            v.pe.down = false; peAdvance(v.pe, 3);
        },
        /* a parameter command under a sounding note: what it changes, again */
        refresh(v, c) {
            const p = v.p, midi = v.midi0;
            if (c >= 0x40 && c < 0x70) {
                const n = (c - 0x40) >> 3, op = p.ops[n], o = v.ops[5 - n];
                o.e.out = opOut(op, midi, v.vel);
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
                default: if (ph < L.hz * N / SR) L.sh = Math.random(); lv = L.sh;
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

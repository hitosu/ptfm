/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground, sound: the CHIP engine (specs/chip.md), the firmware's fw/core/eng/chip/chip.c in JS. Not
 * floats as the other engines here: the same integers, line for line, so at 44,100 Hz the two are equal sample
 * for sample (fw/tests/run_chip.sh renders every instrument through both). Keep any change in both files.
 *
 * A voice renders the firmware's 64-frame blocks into a buffer of its own and hands them out as the worklet asks
 * (render(v, out, a, b)), so its blocks start where its note started. Its output is the 16-bit scale (E.GAIN.CHIP
 * takes it to the worklet's). At another sample rate the times are worked out for it (tablesFor). */
window.PTFM = window.PTFM || {};
PTFM.dsp = (PTFM.dsp || []).concat(function (E) {
    "use strict";
    const N = 64, KW = 16, OUT = 6390;                   /* CHIP_N, CHIP_KW, CHIP_OUT */
    const STEP_MIN = 32768, ENV_STEP_MIN = 65536;        /* CHIP_STEP_MIN, CHIP_ENV_STEP_MIN: a voice's ceiling */
    const PULSE = 0, TRIANGLE = 1, WAVE = 2, BUZZ = 3, NOISE = 4;
    const BRK = 0, BIT8 = 1, PSG177 = 2, BIT16 = 5, EXACT = 6;
    const ENV_DOWN = 1, ENV_UP = 2, ENV_PSG = 3, SWEEP_OFF = 0, SWEEP_UP = 2;
    const B = N * 65536;
    let T = null;

    /* the tables at this sample rate: chip_tables.h's at 44,100 Hz; at another rate the times as fw/tools/gen_chip.py
     * would make them for it (the rest does not depend on the rate) */
    function tablesFor(sr) {
        const t = E.CHIPT;
        if (sr === 44100) return t;
        const r = x => Math.floor(x + .5), Q = 65536, psg = [1773400, 1000000, 1789772, 2000000];
        const law = (c, l) => c === 0 ? (l === 0 ? 1048576 : 2097152) : c === 1 ? (l === 0 ? 894886.5 : 1789773)
                            : l === 0 ? psg[c - 2] / 2 : l === 3 ? psg[c - 2] / 16 : psg[c - 2] / 8;
        const o = Object.assign({}, t, { KC: [], RC: [], NOISE_LEN: [] });
        for (let c = 0; c < 6; c++) for (let l = 0; l < 4; l++) { o.KC.push(r(sr * 2 ** 32 / law(c, l))); o.RC.push(r(law(c, l) * 4096 / sr)); }
        o.LEN0 = r(sr * Q / (440 * 2 ** (-69 / 12) * 8));
        o.FRAME = [59.7275, 60.0988, 60, 50].map(hz => r(sr * Q / hz));
        o.ENV_STEP = [0, 1, 2, 3, 4, 5, 6, 7].map(s => r(s * sr * Q / 64)).concat([0, 1, 2, 3, 4, 5, 6, 7].map(s => r((s + 1) * sr * Q / 240)));
        o.SWEEP_PACE = [0, 1, 2, 3, 4, 5, 6, 7].map(s => r(s * sr * Q / 128)).concat([0, 1, 2, 3, 4, 5, 6, 7].map(s => r((s + 1) * sr * Q / 120)));
        o.KE = psg.map(hz => r(16 * sr * Q / hz)).concat(psg.map(hz => r(8 * sr * Q / hz)));
        const per8bit = [4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068];   /* the noise's periods */
        const perPsg = [1, 2, 3, 4, 5, 6, 8, 10, 12, 14, 16, 19, 22, 25, 28, 31];
        const rates = [[524288].concat(Array.from({ length: 15 }, (_, i) => 262144 / 2 ** i)), per8bit.map(p => 1789773 / p)];
        for (const hz of psg) rates.push(perPsg.map(p => hz / (16 * p)));
        for (const fam of rates) for (const x of fam) o.NOISE_LEN.push(r(sr * Q / x));
        const hp = f => { const rc = 1 / (2 * Math.PI * f); return r(32768 * rc / (rc + 1 / sr)); };
        o.HP = [hp(30), 0, hp(90), hp(440), hp(20), 0, hp(5), 0];
        o.LP_8BIT = r(32768 * (1 - Math.exp(-2 * Math.PI * 14000 / sr)));
        return o;
    }

    const lim = (v, top) => v > top ? top : v;
    const clampi = (v, lo, hi) => v < lo ? lo : v > hi ? hi : v;
    const bits = (b, pos, n) => { let v = 0; for (let i = 0; i < n; i++, pos++) v += (b[pos >> 3] >> (pos & 7) & 1) * 2 ** i; return v; };

    /* chip_unpack: an instrument's 28 bytes as its settings (the record: specs/files/instruments.md) */
    E.chipUnpack = s => {
        const p = { osc: lim(bits(s, 0, 3), NOISE), duty: bits(s, 3, 2), bz_shape: lim(bits(s, 5, 2), 2), bz_steps: bits(s, 7, 1),
                    bz_gate: bits(s, 8, 1), n_mix: bits(s, 9, 1), n_short: bits(s, 10, 1), n_rate: bits(s, 11, 4), n_key: bits(s, 15, 1),
                    clock: lim(bits(s, 16, 3), EXACT), curve: bits(s, 19, 2), filter: bits(s, 21, 2), fps50: bits(s, 23, 1),
                    sweep: lim(bits(s, 24, 2), SWEEP_UP), sw_shift: bits(s, 26, 3), sw_pace: bits(s, 29, 3), vol: bits(s, 32, 4),
                    env: bits(s, 36, 2), env_step: bits(s, 38, 3), release: bits(s, 41, 4), psg_shape: bits(s, 45, 4),
                    psg_period: bits(s, 49, 16) || 1, wave: bits(s, 65, 4), tab: [], spd: [],
                    vib_delay: bits(s, 109, 5) * 2, vib_speed: bits(s, 114, 2) + 1, vib_depth: bits(s, 116, 5) * 2 };
        for (let k = 0; k < 5; k++) { p.tab.push(bits(s, 69 + 8 * k, 6)); p.spd.push(bits(s, 75 + 8 * k, 2) + 1); }
        return p;
    };

    /* ------------------------------------------------------------------ once a frame --- */
    function take(v, k) {                                 /* the table's value, or null */
        const n = v.p.tab[k];
        if (!n || n > T.TAB_COUNT[k]) return null;
        const t = T.TAB[T.TAB_FIRST[k] + n - 1], len = t[1];
        let i = v.tp[k];
        const val = T.TV[t[0] + (i < len ? i : len - 1)];
        if (++v.tc[k] >= v.p.spd[k]) {
            v.tc[k] = 0;
            if (++i >= len) i = t[2] >= 0 ? t[2] : len;
        }
        v.tp[k] = i;
        return val;
    }
    function frameTick(v) {
        const p = v.p;
        let x = take(v, 0);
        v.lvol = 15;
        if (x !== null) {
            v.lvol = clampi(x, 0, 15);
            const t = T.TAB[T.TAB_FIRST[0] + p.tab[0] - 1];
            if (x <= 0 && t[2] < 0 && v.tp[0] >= t[1]) v.dead = 1;
        }
        x = take(v, 1); v.arp = x === null ? 0 : x;
        x = take(v, 2); v.cents = x === null ? 0 : x;
        const f = v.frames++ - p.vib_delay;
        if (p.vib_depth && f >= 0) {
            const d = p.vib_depth, h = d >> 1, i = Math.floor(f / p.vib_speed) & 7;
            v.cents += i === 1 || i === 3 ? h : i === 2 ? d : i === 5 || i === 7 ? -h : i === 6 ? -d : 0;
        }
        x = take(v, 3); v.duty = x === null ? p.duty : clampi(x, 0, 3);
        x = take(v, 4); v.nrate = x === null ? p.n_rate : clampi(x, 0, 15);
        if (!v.gate) {
            if (!p.release) v.dead = 1;
            else if (++v.rel_c >= p.release) { v.rel_c = 0; if (--v.rlev === 0) v.dead = 1; }
        }
    }
    const lawOf = p => p.osc === PULSE ? 0 : p.osc === BUZZ ? (p.bz_steps ? 2 : 3) : 1;
    function sweepTick(v) {
        const p = v.p, s = p.sw_shift, up = p.sweep === SWEEP_UP;
        if (p.osc === NOISE) return;
        if (p.clock === EXACT) {
            if (!v.len_x) return;                        /* (found again at the next block) */
            v.len_x = up ? v.len_x - Math.floor(v.len_x / (2 ** s + 1)) : v.len_x + Math.floor(v.len_x / 2 ** s);
            v.len_x = clampi(v.len_x, STEP_MIN, 0x40000000);
            return;
        }
        if (!v.reg) return;
        let r = v.reg;
        if (p.clock === BRK) {
            let x = 2048 - r;
            x = up ? x + (x >> s) : x - (x >> s);
            if (x > 2047) { v.sw_off = 1; v.sweeping = 0; return; }
            v.reg = 2048 - (x < 0 ? 0 : x);
        } else if (p.clock === BIT8) {
            let t = r - 1;
            t = up ? t - (t >> s) : t + (t >> s);
            if (t > 0x7FF) { v.sw_off = 1; v.sweeping = 0; return; }
            v.reg = (t < 0 ? 0 : t) + 1;
        } else {
            const ci = p.clock * 4 + lawOf(p);
            r = up ? r - (r >> s) : r + (r >> s);
            v.reg = clampi(r, T.RLO[ci], T.RHI[ci]);
        }
    }
    function lenOf(pitch, nsh) {
        const P = clampi(pitch, 0, 135 * 64), o = Math.floor(P / 768), r = P % 768;
        const m = Math.floor((T.T12[r >> 6] * T.T64[r & 63] + 16384) / 32768);
        return Math.floor(T.LEN0 * m / 2 ** (15 + o + nsh));
    }
    function regOf(len, ci) {
        const reg = Math.floor((len * T.RC[ci] + 134217728) / 268435456);
        return reg < T.RLO[ci] ? T.RLO[ci] : reg > T.RHI[ci] ? T.RHI[ci] : reg;
    }
    const lenOfReg = (reg, ci) => Math.floor(reg * T.KC[ci] / 65536);
    const envStopped = v => v.e_r >= 1 && (!(v.p.psg_shape & 8) || (v.p.psg_shape & 1));
    function control(v) {
        const p = v.p, pitch = (v.note + v.arp) * 64 + v.bend64 + Math.trunc(v.cents * 16 / 25);
        let lo = 0, lq = 0, ln = 0, le = 0;
        v.silent = v.sw_off;
        if (p.osc !== NOISE) {
            const nsh = p.osc === PULSE ? 0 : p.osc === BUZZ ? 1 + p.bz_steps + (p.bz_shape === 2 ? 1 : 0) : 2, law = lawOf(p);
            if (p.clock === EXACT) {
                if (!v.sweeping || !v.len_x) v.len_x = lenOf(pitch, nsh);
                lo = v.len_x;
            } else {
                const ci = p.clock * 4 + law;
                if (!v.sweeping || !v.reg) v.reg = regOf(lenOf(pitch, nsh), ci);
                lo = lenOfReg(v.reg, ci);
                if (p.clock === BIT8 && p.osc === PULSE && v.reg < 9) v.silent = 1;
            }
            if (p.osc === BUZZ && p.bz_gate)
                lq = p.clock === EXACT ? lenOf(pitch, 0) : lenOfReg(regOf(lenOf(pitch, 0), p.clock * 4), p.clock * 4);
            if (lo < STEP_MIN) lo = STEP_MIN;            /* (the ceiling: two steps a sample) */
            if (lq && lq < STEP_MIN) lq = STEP_MIN;
        }
        if (p.osc === NOISE || p.n_mix) {
            let idx = v.nrate;
            if (p.n_key) idx = clampi(idx - (v.note + ((v.bend64 + 32) >> 6) - 60), 0, 15);
            const fam = p.clock === BRK ? 0 : p.clock === BIT8 || p.clock === EXACT ? 1 : p.clock;
            ln = T.NOISE_LEN[fam * 16 + idx];
        }
        if (p.env === ENV_PSG && !envStopped(v)) {
            const c = p.clock >= PSG177 && p.clock <= BIT16 ? p.clock - 2 : 0;
            le = p.psg_period * T.KE[(p.curve === 3 ? 4 : 0) + c];
            if (le < ENV_STEP_MIN) le = ENV_STEP_MIN;
        }
        if (lo && !v.len_o) v.nx_o = lo;
        if (lq && !v.len_q) v.nx_q = lq;
        if (ln && !v.len_n) v.nx_n = ln;
        if (le && !v.len_e) v.nx_e = le;
        v.len_o = lo; v.len_q = lq; v.len_n = ln; v.len_e = le;
    }
    function blockControl(v) {
        const p = v.p, fl = T.FRAME[p.fps50 ? 3 : p.clock === BRK ? 0 : p.clock === BIT8 ? 1 : 2];
        v.f_acc += B;
        while (v.f_acc >= fl) { v.f_acc -= fl; frameTick(v); }
        if (p.env === ENV_DOWN || p.env === ENV_UP) {
            const iv = T.ENV_STEP[(p.clock === BIT8 ? 8 : 0) + p.env_step];
            if (iv) {
                v.e_acc += B;
                while (v.e_acc >= iv) {
                    v.e_acc -= iv;
                    if (p.env === ENV_DOWN) { if (v.evol) v.evol--; } else if (v.evol < 15) v.evol++;
                }
            }
        }
        const iv = T.SWEEP_PACE[(p.clock === BIT8 ? 8 : 0) + p.sw_pace];
        if (v.sweeping && iv) {
            v.s_acc += B;
            while (v.s_acc >= iv && v.sweeping) { v.s_acc -= iv; sweepTick(v); }
        }
        control(v);
    }

    /* ------------------------------------------------------------------- the output --- */
    function lfsrStep(v) {
        const p = v.p;
        let l = v.lfsr || 1, fb;
        if (p.n_short && p.clock === BRK) {
            fb = (l ^ (l >> 1)) & 1;
            l = (l >> 1) | (fb << 14);
            l = (l & ~0x40) | (fb << 6);
            v.lfsr = l;
            return ~l & 1;
        }
        if (p.n_short) { fb = (l ^ (l >> 6)) & 1; l = (l >> 1) | (fb << 14); v.lfsr = l; return ~l & 1; }
        if (p.clock >= PSG177 && p.clock <= BIT16) { fb = (l ^ (l >> 3)) & 1; l = (l >> 1) | (fb << 16); v.lfsr = l; return l & 1; }
        fb = (l ^ (l >> 1)) & 1; l = (l >> 1) | (fb << 14); v.lfsr = l;
        return ~l & 1;
    }
    function envLevel(shape, steps, r, pos) {
        const att = shape >> 2 & 1, alt = shape >> 1 & 1, hold = shape & 1, cont = shape >> 3 & 1, mx = steps - 1;
        if (r === 0) return att ? pos : mx - pos;
        if (!cont) return 0;
        if (hold) return (att ^ alt) ? mx : 0;
        return (alt ? (att ^ (r & 1)) : att) ? pos : mx - pos;
    }
    function stepOsc(v) {
        if (v.p.osc === BUZZ) { if (++v.step >= (v.p.bz_steps ? 32 : 16)) { v.step = 0; v.bz_r++; } }
        else v.step = (v.step + 1) & 31;
    }
    function stepEnv(v) { if (++v.e_pos >= (v.p.curve === 3 ? 32 : 16)) { v.e_pos = 0; v.e_r++; } }
    function amp(v, ns, nk) {
        const p = v.p;
        let V, mx = 15, a = 0;
        if (v.dead || v.silent) return 0;
        if (p.env === ENV_PSG) {
            const st = p.curve === 3 ? 32 : 16;
            mx = st - 1;
            V = envLevel(p.psg_shape, st, v.e_r, v.e_pos < mx ? v.e_pos : mx);
        } else V = v.evol;
        V = Math.floor((V * v.lvol * v.rlev * 2 + 225) / 450);
        if (!V) return 0;
        const c16 = p.curve * 64, cv = c16 + (mx === 31 ? 32 : 0), C = T.CURVE;
        switch (p.osc) {
            case PULSE: a = T.DUTY[(p.clock === BRK ? 0 : 4) + v.duty] >> (v.step & 7) & 1 ? C[cv + V] : 0; break;
            case TRIANGLE: a = C[c16 + Math.floor((T.TRI[v.step & 31] * V * 2 + mx) / (2 * mx))]; break;
            case WAVE: a = C[c16 + Math.floor((T.WAVES[p.wave * 32 + (v.step & 31)] * V * 2 + mx) / (2 * mx))]; break;
            case BUZZ: {
                const st = p.bz_steps ? 32 : 16, shp = p.bz_shape === 1 ? 12 : p.bz_shape === 2 ? 10 : 8;
                a = Math.floor(C[c16 + (st === 32 ? 32 : 0) + envLevel(shp, st, v.bz_r, v.step < st ? v.step : st - 1)] * V / mx);
                if (p.bz_gate && v.sq_step >= 4) a = 0;
                break;
            }
            default: a = C[cv + V];
        }
        if (p.osc === NOISE || p.n_mix) a = Math.floor(a * ns / nk);
        return (a * OUT) >> 15;
    }
    function emit(v, out, t, a) {
        const d = a - v.last;
        if (!d) return;
        v.last = a;
        const i = Math.floor(t / 65536), n = N - i < KW ? N - i : KW, k0 = (Math.floor(t / 1024) & 63) * KW, K = T.KERNEL;
        let j;
        for (j = 0; j < n; j++) out[i + j] += d * K[k0 + j];
        for (; j < KW; j++) v.tail[i + j - N] += d * K[k0 + j];
    }
    function stepsExact(v, out) {
        emit(v, out, 0, amp(v, v.nbit, 1));
        for (;;) {
            let t = B;
            if (v.len_o && v.nx_o < t) t = v.nx_o;
            if (v.len_q && v.nx_q < t) t = v.nx_q;
            if (v.len_n && v.nx_n < t) t = v.nx_n;
            if (v.len_e && v.nx_e < t) t = v.nx_e;
            if (t >= B) break;
            if (v.len_o && v.nx_o === t) { stepOsc(v); v.nx_o += v.len_o; }
            if (v.len_q && v.nx_q === t) { v.sq_step = (v.sq_step + 1) & 7; v.nx_q += v.len_q; }
            if (v.len_n && v.nx_n === t) { v.nbit = lfsrStep(v); v.nx_n += v.len_n; }
            if (v.len_e && v.nx_e === t) { stepEnv(v); v.nx_e += v.len_e; if (envStopped(v)) v.len_e = 0; }
            emit(v, out, t, amp(v, v.nbit, 1));
        }
    }
    /* noise faster than the sample rate: each sample the steps that fall in it, the noise their average, put out as
     * a plain step where a band-limited one's middle would land (the kernel's tap 7) */
    function stepsFast(v, out) {
        for (let i = 0; i < N; i++) {
            const te = (i + 1) * 65536;
            let s = 0, k = 0;
            while (v.len_o && v.nx_o < te) { stepOsc(v); v.nx_o += v.len_o; }
            while (v.len_q && v.nx_q < te) { v.sq_step = (v.sq_step + 1) & 7; v.nx_q += v.len_q; }
            while (v.len_e && v.nx_e < te) { stepEnv(v); v.nx_e += v.len_e; if (envStopped(v)) v.len_e = 0; }
            while (v.nx_n < te) { v.nbit = lfsrStep(v); s += v.nbit; k++; v.nx_n += v.len_n; }
            const x = k ? amp(v, s, k) : amp(v, v.nbit, 1), d = x - v.last;
            if (d) {
                v.last = x;
                if (i + 7 < N) out[i + 7] += d * 4096; else v.tail[i + 7 - N] += d * 4096;
            }
        }
    }
    function blockEnd(v) {
        if (v.len_o) v.nx_o -= B;
        if (v.len_q) v.nx_q -= B;
        if (v.len_n) v.nx_n -= B;
        if (v.len_e) v.nx_e -= B;
    }
    /* chip_render: a block into out (an Int32Array of N, zeroed) */
    function block(v, out) {
        const p = v.p;
        blockControl(v);
        for (let i = 0; i < KW; i++) { out[i] += v.tail[i]; v.tail[i] = 0; }
        if (v.len_n && v.len_n < 65536) stepsFast(v, out); else stepsExact(v, out);
        blockEnd(v);
        let acc = v.acc, hx0 = v.hx0, hy0 = v.hy0, hx1 = v.hx1, hy1 = v.hy1, ly = v.ly;
        const a0 = T.HP[p.filter * 2], a1 = T.HP[p.filter * 2 + 1];
        for (let i = 0; i < N; i++) {
            acc = (acc + out[i]) | 0;
            let x = Math.floor(a0 * (hy0 + acc - hx0) / 32768);
            hx0 = acc; hy0 = x;
            if (a1) {
                const y = Math.floor(a1 * (hy1 + x - hx1) / 32768);
                hx1 = x; hy1 = y; x = y;
                ly += Math.floor(T.LP_8BIT * (x - ly) / 32768);
                x = ly;
            }
            out[i] = x >> 12;
        }
        v.acc = acc; v.hx0 = hx0; v.hy0 = hy0; v.hx1 = hx1; v.hy1 = hy1; v.ly = ly;
    }
    function skip(v) {
        blockControl(v);
        while (v.len_o && v.nx_o < B) { stepOsc(v); v.nx_o += v.len_o; }
        while (v.len_q && v.nx_q < B) { v.sq_step = (v.sq_step + 1) & 7; v.nx_q += v.len_q; }
        while (v.len_n && v.nx_n < B) { v.nbit = lfsrStep(v); v.nx_n += v.len_n; }
        while (v.len_e && v.nx_e < B) { stepEnv(v); v.nx_e += v.len_e; if (envStopped(v)) v.len_e = 0; }
        blockEnd(v);
        if (v.dead) {                                     /* (ended unheard: nothing of it left to sound) */
            v.last = 0; v.acc = 0; v.tail.fill(0);
            v.hx0 = v.hy0 = v.hx1 = v.hy1 = v.ly = 0;
        }
    }
    function active(v) {
        if (!v.dead || v.last) return true;
        for (let i = 0; i < KW; i++) if (v.tail[i]) return true;
        return v.acc > 4095 || v.acc < -4095 || v.hy0 > 4095 || v.hy0 < -4095 || v.ly > 4095 || v.ly < -4095;
    }
    function noteOn(v, p, note) {
        Object.assign(v, { p: Object.assign({}, p, { tab: p.tab.slice(), spd: p.spd.slice() }), note, gate: 1, dead: 0, silent: 0,
                           sweeping: p.sweep !== SWEEP_OFF && p.osc !== NOISE && (p.clock === BIT8 || p.sw_pace) ? 1 : 0, sw_off: 0,
                           tp: [0, 0, 0, 0, 0], tc: [0, 0, 0, 0, 0], arp: 0, lvol: 0, duty: 0, nrate: 0, cents: 0, evol: p.vol, rlev: 15,
                           rel_c: 0, step: 0, sq_step: 0, e_pos: 0, nbit: 1, bz_r: 0, e_r: 0, frames: 0, bend64: 0, f_acc: 0, e_acc: 0,
                           s_acc: 0, reg: 0, len_x: 0, len_o: 0, len_q: 0, len_n: 0, len_e: 0, nx_o: 0, nx_q: 0, nx_n: 0, nx_e: 0,
                           lfsr: 1, last: 0, acc: 0, tail: new Int32Array(KW), hx0: 0, hy0: 0, hx1: 0, hy1: 0, ly: 0 });
        frameTick(v);
        control(v);
    }
    /* chip_set: a parameter command on an instrument's settings, the value as the slot stores it */
    function set(p, code, val) {
        switch (code) {
            case 0x40: p.osc = lim(val, NOISE); break;
            case 0x41: p.duty = lim(val, 3); break;
            case 0x42: p.bz_shape = lim(val, 2); break;
            case 0x43: p.bz_steps = lim(val, 1); break;
            case 0x44: p.bz_gate = lim(val, 1); break;
            case 0x48: p.n_mix = lim(val, 1); break;
            case 0x49: p.n_short = lim(val, 1); break;
            case 0x4A: p.n_rate = lim(val, 15); break;
            case 0x4B: p.n_key = lim(val, 1); break;
            case 0x4C: p.clock = lim(val, EXACT); break;
            case 0x4D: p.curve = lim(val, 3); break;
            case 0x4E: p.filter = lim(val, 3); break;
            case 0x4F: p.fps50 = lim(val, 1); break;
            case 0x50: p.sweep = lim(val, SWEEP_UP); break;
            case 0x51: p.sw_shift = lim(val, 7); break;
            case 0x52: p.sw_pace = lim(val, 7); break;
            case 0x54: p.vol = lim(val, 15); break;
            case 0x55: p.env = lim(val, ENV_PSG); break;
            case 0x56: p.env_step = lim(val, 7); break;
            case 0x57: p.release = lim(val, 15); break;
            case 0x58: p.psg_shape = lim(val, 15); break;
            case 0x59: p.psg_period = (lim(val, 255) << 8 | (p.psg_period & 255)) || 1; break;
            case 0x5A: p.psg_period = ((p.psg_period & 0xFF00) | lim(val, 255)) || 1; break;
            case 0x5B: p.wave = lim(val, 15); break;
            case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: p.tab[code - 0x60] = lim(val, 63); break;
            case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: p.spd[code - 0x68] = lim(val, 3) + 1; break;   /* (as stored: X1-X4 0-3) */
            case 0x6D: p.vib_delay = lim(val, 31) * 2; break;                                                       /* (0-31: 2s) */
            case 0x6E: p.vib_speed = lim(val, 3) + 1; break;
            case 0x6F: p.vib_depth = lim(val, 31) * 2; break;
        }
    }
    /* what a command changed in v.p does to the sounding note (chip_param's rest): from the next block */
    function affect(v, code) {
        const p = v.p;
        if (code === 0x40 || code === 0x42 || code === 0x43 || code === 0x4C) { v.reg = 0; v.len_x = 0; }
        else if (code === 0x41) v.duty = p.duty;
        else if (code === 0x4A) v.nrate = p.n_rate;
        else if (code === 0x54) v.evol = p.vol;
        else if (code === 0x55 || code === 0x58) { v.e_pos = 0; v.e_r = 0; }
        else if (code >= 0x60 && code <= 0x64) { v.tp[code - 0x60] = 0; v.tc[code - 0x60] = 0; }
        if ((code >= 0x50 && code <= 0x52) || code === 0x40 || code === 0x4C)   /* (a sweep there, or not, now) */
            v.sweeping = p.sweep !== SWEEP_OFF && p.osc !== NOISE && (p.clock === BIT8 || p.sw_pace) && !v.sw_off ? 1 : 0;
    }
    /* chip_param: the same on the sounding note's own settings (v.p) */
    function param(p, v, code, val) { set(p, code, val); if (v) affect(v, code); }
    /* chip_tuning: the register a note lands on, and how far off true it sounds, in tenths of a cent (+ sharp):
     * 1200 log2(true / register's step) by its series to the third power, in whole numbers */
    function tuning(p, note) {
        const nsh = p.osc === PULSE ? 0 : p.osc === BUZZ ? 1 + p.bz_steps + (p.bz_shape === 2 ? 1 : 0) : 2;
        if (p.clock === EXACT) return { reg: 0, c10: 0 };   /* (no register: in tune) */
        const ci = p.clock * 4 + lawOf(p), lt = lenOf(note * 64, nsh), reg = regOf(lt, ci);
        let a = lt, b = lenOfReg(reg, ci);
        while (a >= 16384 || b >= 16384) { a >>= 1; b >>= 1; }
        const x = Math.trunc((a - b) * 65536 / b), x2 = Math.floor(x * x / 65536), x3 = Math.floor(x2 * x / 65536);
        return { reg, c10: Math.floor(((x - Math.trunc(x2 / 2) + Math.trunc(x3 / 3)) * 17312 + 32768) / 65536) };
    }
    /* the engine core, for the twin test (fw/tests/run_chip.sh) and the pages (PTFM.chip) */
    E.chip = { noteOn, block, skip, active, param, tuning, setRate: sr => { T = tablesFor(sr); },
               noteOff: v => { v.gate = 0; if (!v.p.release) v.dead = 1; }, cut: v => { v.dead = 1; v.gate = 0; } };

    /* ----------------------------------------------------------------- in the worklet --- */
    E.GAIN = Object.assign(E.GAIN || {}, { CHIP: 1 / 32768 });
    E.params.CHIP = (p, c, v) => { if (p.tab) set(p, c, v); };
    E.engines.CHIP = {
        start(v, p, midi) {
            if (!T) T = tablesFor(E.SR);
            noteOn(v, p, midi);
            v.buf = new Int32Array(N);
            v.bi = N;
        },
        refresh: affect,                                  /* (a parameter command: dsp-core changed v.p already) */
        release(v) { v.gate = 0; if (!v.p.release) v.dead = 1; },
        render(v, out, a, b) {
            v.bend64 = Math.round((v.bend || 0) * 64);
            for (let i = a; i < b; i++) {
                if (v.bi >= N) {
                    if (!active(v)) return false;
                    v.buf.fill(0);
                    block(v, v.buf);
                    v.bi = 0;
                }
                out[i] = v.buf[v.bi++];
            }
            return true;
        },
    };
});
/* the engine on the page (the INSTRUMENTS screens' pictures: chip_tuning), at the device's rate */
PTFM.chip = (() => {
    const E = { params: {}, engines: {}, SR: 44100, CHIPT: PTFM.chipTables };
    PTFM.dsp[PTFM.dsp.length - 1](E);
    E.chip.setRate(44100);
    return E.chip;
})();

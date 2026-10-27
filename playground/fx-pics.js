/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground: the FX EDIT page's pictures, one an effect (specs/ui/screens/fx.md, The
 * pictures). Each is drawn from the knobs only, never from what plays (GATE's playhead and the
 * METRONOME's beat follow the clock, FILTER's cutoff its envelope); none writes a value a knob already
 * shows. On the device a picture is drawn again only when a knob changes. c: { x, y, w, h, col, deep, k
 * (the knobs as they play), editing (GATE's steps), step (GATE's playing step or -1), beat (the
 * METRONOME's or -1), open (FILTER: how far its envelope has it open, 0-1), hit (its source just hit) } */
(function () {
    "use strict";
    const D = PTFM.draw, F = D.F, M = PTFM.model, P = PTFM.player;
    const dB = x => 20 * Math.log10(Math.max(x, 1e-9)), lin = d => Math.pow(10, d / 20);
    const fmtK = f => (f / 1000).toFixed(1) + " KHZ";
    const dotted = (x, y0, h, col) => { for (let y = y0; y < y0 + h; y += 2) D.px(x, y, col); };
    function curve(x0, n, yOf, col) {                         /* a line through n columns, joined */
        let py = null;
        for (let i = 0; i < n; i++) {
            const y = yOf(i);
            if (py != null && Math.abs(py - y) > 1) D.vline(x0 + i, Math.min(py, y) + 1, Math.abs(py - y) - 1, col);
            D.px(x0 + i, y, col);
            py = y;
        }
    }
    PTFM.fxPics = {
        /* the 16 steps, ratchets as stacked bars, the playhead; the pattern's name */
        GATE(c) {
            const { x, y, col, k } = c, g = M.state.gates[k[0]];
            /* the name, and how its steps are edited: EDIT first; then a key, and a key held with ALGORITHM */
            D.runs(x, y, F.T, c.editing ? [[g.name, D.WHITE], ["  KEY", D.YELLOW], [": ON/OFF  ", D.LGREY], ["KEY+ALG", D.YELLOW], [": X2-X4", D.LGREY]]
                                        : [[g.name, D.LGREY], ["  EDIT", D.YELLOW], [": ITS STEPS", D.DGREY]]);
            g.steps.forEach((v, i) => {
                const sx = x + i * 14 + 4, sy = y + 16;
                if (i === c.step) D.rect(sx - 1, sy - 1, 13, 30, D.DEEP + D.BLUE);
                D.box(sx, sy, 11, 28, v ? col : D.DGREY);
                if (v) for (let q = 0; q < v; q++) D.rect(sx + 2, sy + 2 + q * Math.floor(24 / v), 7, Math.floor(24 / v) - 2, col);
            });
        },
        /* the response in dB over 20 Hz - 20 kHz (100, 1K, 10K), filled, the cutoff marked; RESO's peak */
        FILTER(c) {
            const { x, y, w, h, col, deep, k } = c, cut = k[0], q = .7 + k[1] / 100 * 8, top = y + 2, gh = h - 16;
            const fAt = i => 20 * Math.pow(1000, i / (w - 1)), xOf = f => Math.round(Math.log(f / 20) / Math.log(1000) * (w - 1));
            const ydb = db => top + Math.round((18 - Math.max(-24, Math.min(18, db))) / 42 * (gh - 1));
            for (const f of [100, 1000, 10000]) { D.vline(x + xOf(f), top, gh, D.DEEP + D.DGREY); D.text(x + xOf(f) - 6, top + gh + 2, F.T, f === 100 ? "100" : f === 1000 ? "1K" : "10K", D.DGREY); }
            D.hline(x, ydb(0), w, D.DGREY);
            if (!cut) return;                                    /* OFF: the flat line */
            /* where its envelope has it now: a share of the way from CUT to fully open (20 kHz, 20 Hz), in octaves */
            const fcut = M.filterHz(cut), lp = cut < 0, fopen = lp ? 20000 : 20, env = k[2] || 0;
            const fc = fcut * Math.pow(fopen / fcut, c.open || 0);
            let peak = -99;
            for (let i = 0; i < w; i++) {
                const r = fAt(i) / fc, db = dB((lp ? 1 : r * r) / Math.sqrt(Math.pow(1 - r * r, 2) + Math.pow(r / q, 2)));
                peak = Math.max(peak, db);
                const yy = ydb(db);
                D.vline(x + i, yy + 1, top + gh - yy - 1, deep); D.px(x + i, yy, col);
            }
            if (env > 0) {                                       /* where it rests, how far a note opens it */
                const xr = xOf(fcut * Math.pow(fopen / fcut, env / 100));
                dotted(x + xOf(fcut), top, gh, D.LGREY);
                dotted(x + xr, top, gh, D.YELLOW);
                if (c.hit) D.vline(x + xr, top, 6, D.WHITE);
            }
            D.vline(x + xOf(fc), top, gh, col);
            if (peak > 1) {                                      /* (clear of the knob page, `ALG 1/2`; on black over the lines) */
                const s = "PEAK +" + Math.round(peak) + " DB", tw = D.tw(F.T, s), tx = xOf(fc) < w / 2 ? x + w - 48 - tw : x + 2;
                D.rect(tx - 1, top, tw + 2, 12, D.BLACK);
                D.text(tx, top, F.T, s, D.LGREY);
            }
        },
        /* the clean sine dim, the crushed one over it: BITS' steps, RATE's holds (whole samples, 2 px a
         * sample, taken at each hold's middle), DUST as specks from a fixed scatter */
        "LO-FI"(c) {
            const { x, y, w, h, col, deep, k } = c, steps = Math.pow(2, k[0] - 1), hold = 2 * Math.max(1, Math.round(44.1 / k[1])), dust = k[2] / 100;
            const mid = y + 8 + (h - 10) / 2, amp = (h - 10) / 2 - 2;
            for (let i = 0; i < w; i++) D.px(x + i, Math.round(mid - Math.sin(i / w * Math.PI * 4) * amp), deep);
            curve(x, w, i => { const t = Math.floor(i / hold) * hold + hold / 2; return Math.round(mid - Math.round(Math.sin(t / w * Math.PI * 4) * steps) / steps * amp); }, col);
            let seed = 12345;
            const rnd = () => (seed = (Math.imul(seed, 1103515245) + 12345) & 0x7fffffff) / 0x7fffffff;   /* (imul: the
                                                                     * product exact, as the firmware's 32 bits) */
            for (let i = 0; i < 400; i++) { const sx = Math.floor(rnd() * w), sy = Math.floor(rnd() * (h - 12)); if (rnd() < dust) D.px(x + sx, y + 10 + sy, D.LGREY); }
        },
        /* the pitch each side gets, in cents, over 3 s: left the effect's colour, right grey */
        CHORUS(c) {
            const { x, y, w, h, col, k } = c, mode = k[0], rate = k[1] / 100, depth = k[2] / 100;
            const hz = [.5, .83, 9][mode] * (.3 + rate * 1.4), dep = [.0019, .0029, .0006][mode] * (.3 + depth * 1.4);
            const cents = 1200 * Math.log2(1 + dep * 2 * Math.PI * hz), scale = [30, 80, 160][mode];
            const top = y + 13, gh = h - 26, X0 = x + 26, GW = w - 26, wob = dep * 2 * Math.PI * hz;
            const ym = ct => top + Math.round((1 - Math.max(-1, Math.min(1, ct / scale))) / 2 * (gh - 1));
            D.hline(X0, ym(0), GW, D.DEEP + D.DGREY);
            for (const s of [1, 2]) D.vline(X0 + Math.round(s / 3 * (GW - 1)), top, gh, D.DEEP + D.DGREY);
            D.text(x, top - 2, F.T, "+" + scale, D.DGREY); D.text(x + 6, ym(0) - 6, F.T, "0", D.DGREY); D.text(x, top + gh - 10, F.T, "-" + scale, D.DGREY);
            curve(X0, GW, i => ym(1200 * Math.log2(1 + wob * Math.cos(i / (GW - 1) * 3 * hz * 2 * Math.PI))), D.LGREY);
            curve(X0, GW, i => ym(1200 * Math.log2(1 - wob * Math.cos(i / (GW - 1) * 3 * hz * 2 * Math.PI))), col);
            const s = (hz < 1 ? hz.toFixed(2) : hz.toFixed(1)) + " HZ  +-" + Math.round(cents) + " CENTS";
            D.text(x + w - D.tw(F.T, s), y, F.T, s, col);
            D.text(x, y, F.T, "L", col); D.text(x + 10, y, F.T, "R", D.LGREY);
            D.text(X0, top + gh + 1, F.T, "0", D.DGREY);
            for (const sec of [1, 2, 3]) { const l = sec + " S", lx = X0 + Math.round(sec / 3 * (GW - 1)); D.text(Math.min(x + w - D.tw(F.T, l), lx - D.tw(F.T, l) / 2), top + gh + 1, F.T, l, D.DGREY); }
        },
        /* a bar's 4 beats: the dry, then each echo in dB (TONE's treble bright inside it), PING-PONG's
         * left up, right down; what plays written */
        DELAY(c) {
            const { x, y, w, h, col, k } = c, list = M.FX.DELAY.knobs[0].list;
            const NV = { "1/32": .125, "1/16T": 1 / 6, "1/16": .25, "1/8T": 1 / 3, "1/16.": .375, "1/8": .5, "1/4T": 2 / 3, "1/8.": .75, "1/4": 1, "1/2T": 4 / 3, "1/4.": 1.5, "1/2": 2 };
            const PR = 29400, beat = 60 / P.tempo(), pp = k[3] === 1, most = pp ? .5 : 1;
            let nv = k[0];
            while (nv > 0 && NV[list[nv]] * beat > most) nv--;
            const beats = NV[list[nv]], fb = k[1] / 100 * .92, mix = k[4] / 100, kk = 1 - Math.pow(1 - (.1 + k[2] / 100 * .85), 1.5);
            const Hf = f => { const a = 2 * Math.PI * f / PR; return kk / Math.hypot(1 - (1 - kk) * Math.cos(a), (1 - kk) * Math.sin(a)); };
            const cc = 1 - kk * kk / (2 * (1 - kk)), fc = cc > -1 ? Math.acos(cc) * PR / (2 * Math.PI) : PR / 2;
            const top = y + 13, gh = h - 26, mid = top + Math.floor(gh / 2), half = Math.floor(gh / 2) - 1;
            const xb = b => x + 3 + Math.round(b / 4 * (w - 8)), hd = db => Math.max(0, Math.round((48 + db) / 48 * half));
            for (const b of [1, 2, 3]) D.vline(xb(b), top, gh, D.DEEP + D.DGREY);
            D.hline(x, mid, w, D.DEEP + D.DGREY);
            D.rect(xb(0) - 1, mid - half, 3, 2 * half + 1, D.WHITE);
            for (let n = 1, t0 = beats; t0 <= 4 + 1e-9 && n < 80; n++, t0 += beats) {
                const db = dB(mix) + (n - 1) * dB(fb || 1e-9), tre = db + n * dB(Hf(5000));
                if (db < -48) break;
                const ex = xb(t0) - 1, eh = hd(db), et = hd(tre), side = pp ? (n % 2 ? -1 : 1) : 0;
                if (side <= 0) { D.rect(ex, mid - eh, 3, eh, D.DEEP + col); D.rect(ex, mid - et, 3, et, col); }
                if (side >= 0) { D.rect(ex, mid + 1, 3, eh, D.DEEP + col); D.rect(ex, mid + 1, 3, et, col); }
            }
            /* clear of the knob page (`ALG 0/1`, 48 px at the right): with single spaces when it would not
             * fit, then without TONE's Hz (K3 shows TONE) */
            const parts = [nv !== k[0] ? "PLAYS " + list[nv] : "", Math.round(beats * beat * 1000) + " MS", fb ? Math.round(dB(fb)) + " DB EACH" : "ONCE",
                           "TONE " + (fc >= 13000 ? "OPEN" : M.hzShort(fc))].filter(Boolean), room = w - 50;
            let s = parts.join("  ");
            if (D.tw(F.T, s) > room) s = parts.join(" ");
            if (D.tw(F.T, s) > room) s = parts.slice(0, -1).join(" ");
            D.text(x + w - 48 - D.tw(F.T, s), y, F.T, s, col);
            if (pp) { D.text(x + w - 6, top, F.T, "L", D.DGREY); D.text(x + w - 6, top + gh - 11, F.T, "R", D.DGREY); }
            ["1", "2", "3", "4"].forEach((l, i) => D.text(xb(i) + (i ? -2 : 3), top + gh + 1, F.T, l, D.DGREY));
        },
        /* the decay relief: time across, 12 bands bass to treble, each shaded as it decays, DAMP
         * shortening the treble; the decay time and the treble's written */
        REVERB(c) {
            const { x, y, w, h, col, deep, k } = c, size = k[0] / 100, damp = k[1] / 100, pre = k[2] / 1000, mix = k[3] / 100;
            const rt = 1.3 * Math.pow(11 / 1.3, size), rtHi = rt * (1 - .75 * damp), span = [2, 4, 8, 12].find(v => v >= rt) || 12, N = 12;
            const bh = Math.max(2, Math.min(3, Math.floor((h - 26) / N)));          /* 3 px a band; 2 where SENDS' row takes room */
            const fmtS = s => (s < 10 ? s.toFixed(1) : Math.round(s)) + " S", top = y + 12, gh = N * bh, start = dB(Math.max(.01, mix));
            for (let b = 0; b < N; b++) {
                const rtb = rt * (1 - .75 * damp * Math.pow(b / (N - 1), 1.5)), y0 = top + gh - (b + 1) * bh;
                for (let i = 0; i < w; i++) {
                    const s = i / w * span - pre;
                    if (s < 0) continue;
                    const db = start - b * .6 - 60 * s / rtb;
                    for (let yy = 0; yy < bh; yy++) { const chk = (i + yy) & 1, cc = db > -12 ? col : db > -24 ? (chk ? col : deep) : db > -40 ? deep : db > -60 ? (chk ? deep : -1) : -1; if (cc >= 0) D.px(x + i, y0 + yy, cc); }
                }
            }
            for (let s = 0; s <= span; s += span <= 4 ? 1 : 2) { const lx = x + Math.round(s / span * (w - 1)), l = s === span ? s + " S" : String(s); D.text(Math.min(lx, x + w - D.tw(F.T, l)), top + gh + 1, F.T, l, D.DGREY); }
            D.text(x, y, F.T, fmtS(rt), col);
            D.text(x + w - D.tw(F.T, "TREBLE " + fmtS(rtHi)), y, F.T, "TREBLE " + fmtS(rtHi), D.DGREY);
        },
        /* what DRIVE adds to a 441 Hz sine at -12 dB, TONE's lowpass included: the odd harmonics 1-15
         * in dB, the fundamental white, TONE's cutoff dotted among them. DRIVE's own law (dsp-fx.js): the
         * low cut, the drive into a soft clip with its small bias, the make-up gain, TONE */
        DRIVE(c) {
            const { x, y, w, h, col, k } = c, d = Math.floor(k[0] * 127 / 100), G = 1 + d * d / 2048, mk = (30000 - 120 * d) / 32768;
            const kk = .05 + k[1] / 100 * .9, bias = 2400 / 32768, b0 = Math.tanh(bias);
            const out = new Float64Array(100);
            let s = 0, hp = 0;
            for (let i = -4410; i < 100; i++) {
                const xin = .25 * Math.sin(2 * Math.PI * 441 * i / 44100);
                hp += (xin - hp) / 128;
                const u = (xin - hp) * G + bias;
                s += ((Math.tanh(u > 2 ? 2 : u < -2 ? -2 : u) - b0) * mk - s) * kk;
                if (i >= 0) out[i] = s;
            }
            const cc = 1 - kk * kk / (2 * (1 - kk)), fc = cc > -1 ? Math.acos(cc) * 44100 / (2 * Math.PI) : 22050;
            const top = y + 13, gh = h - 26, NB = 8, sw = Math.floor(w / NB);
            for (let j = 0; j < NB; j++) {
                const hn = 2 * j + 1;
                let re = 0, im = 0;
                for (let i = 0; i < 100; i++) { re += out[i] * Math.cos(2 * Math.PI * hn * i / 100); im += out[i] * Math.sin(2 * Math.PI * hn * i / 100); }
                const bh = Math.max(0, Math.round((72 + dB(Math.hypot(re, im) / 50)) / 72 * gh)), bx = x + j * sw + 2;
                if (bh) D.rect(bx + 4, top + gh - bh, sw - 12, bh, hn === 1 ? D.WHITE : col); else D.hline(bx + 4, top + gh - 1, sw - 12, D.DEEP + D.DGREY);
                D.text(Math.round(bx + (sw - 4) / 2 - D.tw(F.T, "" + hn) / 2), top + gh + 1, F.T, "" + hn, D.DGREY);
            }
            const hc = fc / 441;
            if (hc < 2 * NB) dotted(Math.round(x + (hc - 1) / 2 * sw + sw / 2 - 2), top, gh, D.LGREY);
            const gain = dB(G * mk * (1 - b0 * b0)), lab = (gain >= 0 ? "+" : "") + gain.toFixed(0) + " DB  TONE " + (fc >= 15000 ? "OPEN" : fc >= 1000 ? fmtK(fc) : Math.round(fc) + " HZ");
            D.text(x, y, F.T, "HARMONICS", D.LGREY); D.text(x + w - D.tw(F.T, lab), y, F.T, lab, col);
        },
        /* one box, one output axis: input to output at the left, flat at CEILING; on into time after a
         * +6 dB peak, a sound held at -12 dB climbing back over RELEASE; the gain taken off deep red */
        LIMITER(c) {
            const { x, y, w, h, col, k } = c, ceil = k[0], rel = k[1], top = y + 2, gh = h - 16, LW = 70, RX = x + LW + 1, RW = w - LW - 1;
            const yd = d => top + gh - 1 - Math.round((Math.max(-24, Math.min(6, d)) + 24) / 30 * (gh - 1));
            for (let i = 0; i < w; i += 3) D.px(x + i, yd(ceil), D.LGREY);
            D.vline(x + LW, top, gh, D.DGREY);
            for (let i = 0; i < LW; i++) {
                const din = i / (LW - 1) * 30 - 24, yc = yd(din), yo = yd(Math.min(din, ceil));
                if (yo > yc) D.vline(x + i, yc, yo - yc, D.DEEP + D.RED);
                D.px(x + i, yc, D.DGREY); D.px(x + i, yo, col);
            }
            const g0 = Math.min(1, lin(ceil) / lin(6)), held = Math.min(-12, ceil);
            for (let i = 0; i < RW; i++) {
                const g = 1 - (1 - g0) * Math.exp(-i / (RW - 1) * 1000 / rel), yo = yd(held + dB(g)), yh = yd(held);
                if (yo > yh) D.vline(RX + i, yh, yo - yh, D.DEEP + D.RED);
                D.px(RX + i, yh, D.DGREY); D.px(RX + i, yo, col);
            }
            dotted(RX + Math.round(Math.min(1, rel / 1000) * (RW - 1)), top, gh, D.LGREY);
            D.text(x, top + gh + 1, F.T, "IN", D.DGREY); D.text(x + LW - D.tw(F.T, "+6"), top + gh + 1, F.T, "+6", D.DGREY);
            D.text(RX + 2, top + gh + 1, F.T, "0", D.DGREY); D.text(x + w - D.tw(F.T, "1 S"), top + gh + 1, F.T, "1 S", D.DGREY);
        },
        /* one canvas, one dB scale: the tracks' dips as bars rising from the bottom (DEPTH × each one's
         * DCK), the curve after one loud hit hanging from the top over a bar; the source inverted */
        DUCK(c) {
            const { x, y, w, h, col, k } = c, src = k[0], depth = k[1] / 100, rel = k[2], top = y + 13, gh = h - 26;
            const s = PTFM.audio.fxSong(), mixer = s.mixer, ntrk = s === M.state.loops ? 16 : Math.max(...s.order.map(n => (M.pattern(n) || { tracks: 1 }).tracks), 1);
            const span = 4 * 60 / P.tempo(), hd = d => Math.round(Math.min(24, -d) / 24 * (gh - 1));
            const n = Math.max(ntrk, 8), cw = Math.floor(w / n), bot = top + gh - 1;
            let any = false;
            for (let b = 1; b < 4; b++) D.vline(x + Math.round(b / 4 * (w - 1)), top, gh, D.DEEP + D.DGREY);
            for (let i = 0; i < n; i++) {
                const bx = x + i * cw, dig = i.toString(16).toUpperCase(), tx = bx + Math.floor((cw - 6) / 2);
                if (i === src) { D.rect(bx + 2, top + gh + 1, cw - 4, 12, D.WHITE); D.text(tx, top + gh + 1, F.T, dig, D.BLACK); continue; }
                const a = i < ntrk && mixer[i] ? depth * mixer[i].dck / 127 : 0, bh = hd(dB(1 - a));
                if (a) any = true;
                if (bh) { D.rect(bx + 3, bot - bh + 1, cw - 6, bh, D.DEEP + col); D.hline(bx + 3, bot - bh + 1, cw - 6, col); }
                D.text(tx, top + gh + 1, F.T, dig, a ? col : D.DGREY);
            }
            D.hline(x, bot, w, D.DGREY); D.vline(x, top, gh, D.WHITE);
            /* the dip after the hit: back in a straight line over RELEASE (dsp-fx.js) */
            curve(x + 1, w - 1, i => top + hd(dB(1 - depth * Math.max(0, 1 - (i + 1) / (w - 1) * span * 1000 / rel))), D.WHITE);
            if (!any) D.text(x + w - D.tw(F.T, "NO TRACK DUCKS: MIXER, DCK"), top + gh - 13, F.T, "NO TRACK DUCKS: MIXER, DCK", D.LGREY);
            const name = srcName(s, src), lab = Math.round(dB(1 - depth)) + " DB AT MOST";
            if (name) D.text(x, y, F.T, name, D.WHITE);
            D.text(x + w - D.tw(F.T, lab), y, F.T, lab, col);
        },
        /* a box a beat, the bar's first taller; the playing beat lit; only FOLLOW's tempo written */
        METRONOME(c) {
            const { x, y, w, h, col, k } = c, n = k[1], top = y + 13, gh = h - 14, bw = Math.floor(w / n);
            for (let i = 0; i < n; i++) {
                const bx = x + i * bw + 1, bwd = Math.max(2, bw - 3), bh = i ? Math.round(gh * .6) : gh, lit = i === c.beat;
                D.rect(bx, top + gh - bh, bwd, bh, lit ? col : D.DEEP + col);
                if (!lit) D.hline(bx, top + gh - bh, bwd, col);
            }
            if (k[0] === 29) { const s = Math.round(P.tempo()) + " BPM"; D.text(x + w - 22 - D.tw(F.T, s), y, F.T, s, col); }
        },
    };
    /* the source track's instrument: the first its cells name; the keys' (SOURCE 16), the one they play */
    function srcName(s, t) {
        if (t >= 16) { const i = M.inst(PTFM.ui.inst); return i ? i.name : ""; }
        if (s === M.state.loops) { const l = s.list[t]; const i = l && M.inst(l.inst); return i ? i.name : ""; }   /* (loop t: track t) */
        for (const n of s.order) {
            const p = M.pattern(n);
            if (!p || t >= p.tracks) continue;
            for (let r = 0; r < p.rows; r++) { const cl = p.cells[r * 16 + t]; if (cl && cl.inst != null && M.inst(cl.inst)) return M.inst(cl.inst).name; }
        }
        return "";
    }
})();

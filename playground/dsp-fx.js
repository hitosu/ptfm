/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground, sound: after the voices, FX (specs/ui/screens/fx.md): the mixer (each track's
 * level, pan, DUCK amount and sends, as a song keeps them (specs/files/songs.md): bytes, LEVEL 7F full, PAN 80
 * the middle; DUCK works on the tracks before they are mixed), the chain (GATE, FILTER, DRIVE, LO-FI,
 * CHORUS, DELAY, REVERB, LIMITER, in the song's order; in SENDS routing CHORUS, DELAY and REVERB are
 * shared sends whose returns join at the SENDS item), the punch-in effects (FX + white key, on the
 * whole mix, from a buffer of what just played), the METRONOME (after the effects), then MASTER and
 * LINE GAIN, the output, mono or stereo, and the soft ceiling. Mixer and FX commands change what plays, over the setup kept, until the
 * next song starts. On DJ (specs/ui/screens/dj.md#the-sound) each deck has its song's mixer and small effects
 * (a deck's own instance of these), then a DJ FILTER and the crossfader; CHORUS, DELAY, REVERB and the LIMITER
 * are DJ's own, after them; PREVIEW splits the ears. */
PTFM.dsp = (PTFM.dsp || []).concat(function (E) {
    "use strict";
    const SR = E.SR, BLK = E.BLK, TWO_PI = Math.PI * 2;
    const DRIVE_BIAS = 2400 / 32768;                  /* DRIVE's bias into the clip: the even harmonics */
    const PUNCH_OUT = .08;                            /* a punch-in let go fades back into the mix (s) */
    const PUNCH_BUF = 6 * Math.round(SR * .5 / 6);    /* the punch-in buffer: the last 0.5 s, the device's (specs/files/ram.md) */
    /* kept as the device keeps it (specs/ui/screens/fx.md#punch-in-effects): its middle, (L + R) / 2, at 2/3 of the
     * rate, its sides, (L - R) / 2, at 1/6, through the device's converters: a 12.5 kHz low-pass, then two samples
     * of every three; the sides' 16-frame sum of three running sums of 6 (a CIC filter), a sample every 6 frames,
     * and its droop compensator (-0.4 1.8 -0.4); read back linear between samples, through a 12.5 kHz and a
     * 3.5 kHz low-pass: left = middle + sides, right = middle - sides. PUNCH_FULL_RATE true keeps it as before
     * instead (stereo at the full rate), to hear the two side by side */
    const PUNCH_FULL_RATE = false;
    const PUNCH_MID = PUNCH_BUF * 2 / 3, PUNCH_SIDE = PUNCH_BUF / 6;
    const PUNCH_LAG = 32;                             /* a replay reads this far behind the newest frame (the converters') */
    const PUNCH_SPAN = PUNCH_BUF - PUNCH_LAG - 64 - 16;   /* the most a replay reaches back from there (the device's) */
    const PUNCH_SIDE_AT = 4.25;                       /* frames: the sides read on, to meet the middle in time */
    const PUNCH_CIC = [1, 3, 6, 10, 15, 21, 25, 27, 27, 25, 21, 15, 10, 6, 3, 1].map(v => v / 216);
    const STOP_MOST = SR;                             /* tape stop winds down over a beat, at most 1 s: its lag stays in the buffer */
    const NV = { "1/32": .125, "1/16T": 1 / 6, "1/16": .25, "1/8T": 1 / 3, "1/16.": .375, "1/8": .5, "1/4T": 2 / 3,
                 "1/8.": .75, "1/4": 1, "1/2T": 4 / 3, "1/4.": 1.5, "1/2": 2 };
    const NV_LIST = ["1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2"];
    const GATE_RATE = [.5, .25, .125, 1 / 3, 1 / 6];

    /* -------------------------------------------------------- building blocks --- */
    function svf() { return { ic1: 0, ic2: 0 }; }
    function svfRun(s, x, hz, q, mode) {          /* TPT state variable: mode 0 low, 1 high, 2 band */
        const g = Math.tan(Math.PI * Math.min(hz, SR * .45) / SR), k = 1 / q;
        const a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const v3 = x - s.ic2, v1 = a1 * s.ic1 + a2 * v3, v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
        s.ic1 = 2 * v1 - s.ic1; s.ic2 = 2 * v2 - s.ic2;
        return mode === 0 ? v2 : mode === 1 ? x - k * v1 - v2 : v1;
    }
    function lpCoef(hz) {                           /* a biquad low-pass, q 0.7071 (the firmware's converters') */
        const w = TWO_PI * hz / SR, a = Math.sin(w) / (2 * .7071), a0 = 1 + a, b = (1 - Math.cos(w)) / 2 / a0;
        return { b0: b, b1: 2 * b, b2: b, a1: -2 * Math.cos(w) / a0, a2: (1 - a) / a0 };
    }
    const bqState = () => ({ x1: 0, x2: 0, y1: 0, y2: 0 });
    function bqRun(c, s, x) {
        const y = c.b0 * x + c.b1 * s.x1 + c.b2 * s.x2 - c.a1 * s.y1 - c.a2 * s.y2;
        s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = y;
        return y;
    }
    const PUNCH_AA = lpCoef(12500), PUNCH_SD = lpCoef(3500);

    /* the punch-in buffer (PUNCH_BUF, above): a place counts the output's frames; frame f's middle sample is at
     * 2f / 3, its sides' at (f + 2.5) / 6 */
    function histNew() {
        return { w: 0, M: new Float32Array(PUNCH_MID), S: new Float32Array(PUNCH_SIDE), lp: bqState(), held: 0,
                 sx: new Float32Array(16), s1: 0, s2: 0,
                 L: PUNCH_FULL_RATE ? new Float32Array(PUNCH_BUF) : null, R: PUNCH_FULL_RATE ? new Float32Array(PUNCH_BUF) : null };
    }
    function histClear(H) {
        H.M.fill(0); H.S.fill(0); H.sx.fill(0); H.lp = bqState(); H.held = 0; H.s1 = H.s2 = 0;
        if (H.L) { H.L.fill(0); H.R.fill(0); }
    }
    function histRec(H, l, r) {                     /* a frame in, at H.w */
        const w = H.w, a = Math.max(-1, Math.min(1, l)), c = Math.max(-1, Math.min(1, r));
        if (PUNCH_FULL_RATE) { H.L[w] = a; H.R[w] = c; H.w = (w + 1) % PUNCH_BUF; return; }
        const y = bqRun(PUNCH_AA, H.lp, (a + c) / 2), ph = w % 3, k = 2 * Math.floor(w / 3);
        if (ph === 0) H.M[k] = y;
        else if (ph === 1) H.held = y;
        else H.M[k + 1] = (H.held + y) / 2;
        H.sx[w & 15] = (a - c) / 2;
        if (w % 6 === 5) {                          /* a sides sample: the sum, then the compensator on the one before */
            let v = 0;
            for (let n = 0; n < 16; n++) v += PUNCH_CIC[n] * H.sx[(w - n) & 15];
            const j = Math.floor(w / 6);
            H.S[(j - 1 + PUNCH_SIDE) % PUNCH_SIDE] = 1.8 * H.s1 - .4 * H.s2 - .4 * v;
            H.s2 = H.s1; H.s1 = v;
        }
        H.w = (w + 1) % PUNCH_BUF;
    }
    function histPlay(p, H, f, g) {                 /* the buffer at place f (frames) as heard, times g: [left, right] */
        if (PUNCH_FULL_RATE) {
            const j = Math.floor(f), t = f - j, n = PUNCH_BUF;
            return [(H.L[j % n] * (1 - t) + H.L[(j + 1) % n] * t) * g, (H.R[j % n] * (1 - t) + H.R[(j + 1) % n] * t) * g];
        }
        const pm = f * 2 / 3, jm = Math.floor(pm), tm = pm - jm, m0 = H.M[jm % PUNCH_MID];
        const ps = ((f + PUNCH_SIDE_AT) % PUNCH_BUF) / 6, js = Math.floor(ps), ts = ps - js, s0 = H.S[js % PUNCH_SIDE];
        const m = bqRun(PUNCH_AA, p.um, (m0 + (H.M[(jm + 1) % PUNCH_MID] - m0) * tm) * g);
        const s = bqRun(PUNCH_SD, p.us, (s0 + (H.S[(js + 1) % PUNCH_SIDE] - s0) * ts) * g);
        return [m + s, m - s];
    }
    /* the REVERB: Dattorro's plate (J. Dattorro, "Effect Design, Part 1", JAES 1997): four input
     * diffusers, then a figure-eight tank of two halves, each a modulated allpass, a delay, a damping
     * lowpass, an allpass and a delay, feeding the other half; stereo out from seven taps a side.
     * PRE-DELAY runs before it (REVERB, below). SIZE sets the tank's decay (about 1.3 s to 11 s to
     * -60 dB), DAMP its lowpass.
     * It runs at 2/3 of the output's rate (29.4 kHz at 44.1, near the paper's own 29761 Hz), as the
     * firmware's will, for delay lines of ~45 KB at 16 bits (specs/files/ram.md): the tail keeps
     * everything to ~13 kHz (half the rate loses audibly; this is close to the full rate by ear). The
     * paper's lengths are scaled to that rate, and the filters' and LFO's steps to keep their meaning
     * at 44.1 kHz. */
    const PR = SR * 2 / 3, PK = 44100 / PR;
    const DS = n => Math.floor(n * PR / 29761 + .5), EXC = DS(16);
    const PL = { in1: DS(142), in2: DS(107), in3: DS(379), in4: DS(277), apL: DS(672), d1L: DS(4453), ap2L: DS(1800), d2L: DS(3720),
                 apR: DS(908), d1R: DS(4217), ap2R: DS(2656), d2R: DS(3163) };
    const PT = [266, 2974, 1913, 1996, 1990, 187, 1066, 353, 3627, 1228, 2673, 2111, 335, 121].map(DS);
    const BW = 1 - Math.pow(.3, PK), LFO = .000128 * PK;
    /* into and out of the plate's rate: polyphase, by L / Mn, on the grid of twice the output's rate,
     * through one 127-tap lowpass at 0.9 of the plate's Nyquist (Blackman window) */
    const RLP = (() => {
        const N = 127, fc = .9 * (PR / 2) / (2 * SR), h = [];
        let s = 0;
        for (let i = 0; i < N; i++) {
            const m = i - (N - 1) / 2, w = .42 - .5 * Math.cos(TWO_PI * i / (N - 1)) + .08 * Math.cos(2 * TWO_PI * i / (N - 1));
            h.push((m === 0 ? 2 * fc : Math.sin(TWO_PI * fc * m) / (Math.PI * m)) * w);
            s += h[i];
        }
        return h.map(v => v / s);
    })();
    function rsmp(L, Mn) {
        const H = RLP, o = (H.length - 1) >> 1, N = 256, x = new Float32Array(N), y = new Float32Array(N);
        let nin = 0, j = 0, yr = 0, yw = 0;
        return {
            push(v) {                                  /* one sample in; every output it completes made */
                x[nin++ & (N - 1)] = v;
                for (;;) {
                    const t = j * Mn + o;
                    if (Math.floor(t / L) >= nin) break;
                    let a = 0;
                    for (let k = t % L; k < H.length; k += L) { const i = (t - k) / L; if (i >= 0) a += H[k] * x[i & (N - 1)]; }
                    y[yw++ & (N - 1)] = a * L;
                    j++;
                }
            },
            get n() { return yw - yr; },
            pop() { return y[yr++ & (N - 1)]; },
        };
    }
    const dline = n => ({ b: new Float32Array(n), n, i: 0 });
    const tap = (d, k) => { const j = d.i - k; return d.b[j < 0 ? j + d.n : j]; };     /* written k samples ago */
    const push = (d, x) => { d.b[d.i] = x; if (++d.i >= d.n) d.i = 0; };
    function ap(d, len, x, g) { const z = tap(d, len), v = x - g * z; push(d, v); return z + g * v; }
    function apMod(d, len, x, g) {                 /* a fractional, moving length */
        const k = Math.floor(len), z0 = tap(d, k), z = z0 + (tap(d, k + 1) - z0) * (len - k), v = x - g * z;
        push(d, v);
        return z + g * v;
    }
    function reverb() {
        const L = {};
        for (const k in PL) L[k] = dline(PL[k] + (k === "apL" || k === "apR" ? EXC + 2 : 0));
        return { L, bw: 0, dl: 0, dr: 0, s: 0, c: 1, pre: new Float32Array(SR >> 2), pw: 0,
                 down: rsmp(2, 3), upL: rsmp(3, 2), upR: rsmp(3, 2), go: false };
    }
    /* a sample in and out at the output's rate; the plate runs when the resampler has one for it */
    function revRun(r, inL, inR, size, damp) {
        r.down.push((inL + inR) * .4);                            /* as loud as Freeverb was on noise */
        while (r.down.n) { const [l, rr] = plate(r, r.down.pop(), size, damp); r.upL.push(l); r.upR.push(rr); }
        if (!r.go) { if (r.upL.n < 3) return [0, 0]; r.go = true; }   /* a little kept in hand: never runs dry */
        return [r.upL.n ? r.upL.pop() : 0, r.upR.n ? r.upR.pop() : 0];
    }
    function plate(r, x0, size, damp) {
        const P = r.L, decay = .25 + .65 * Math.pow(size, 1.5), dd2 = Math.min(.5, Math.max(.25, decay + .15)), dk = 1 - Math.pow(.7 * damp, PK);
        const fbL = tap(P.d2R, PL.d2R), fbR = tap(P.d2L, PL.d2L);
        r.bw += BW * (x0 - r.bw);                                 /* input bandwidth */
        let x = ap(P.in1, PL.in1, r.bw, .75);
        x = ap(P.in2, PL.in2, x, .75);
        x = ap(P.in3, PL.in3, x, .625);
        x = ap(P.in4, PL.in4, x, .625);
        const s = r.s + LFO * r.c, c = r.c - LFO * s;            /* ~0.9 Hz, sine and cosine for the two halves */
        r.s = s; r.c = c;
        push(P.d1L, apMod(P.apL, PL.apL + EXC * s, x + decay * fbL, -.7));
        r.dl += dk * (tap(P.d1L, PL.d1L) - r.dl);
        push(P.d2L, ap(P.ap2L, PL.ap2L, r.dl * decay, dd2));
        push(P.d1R, apMod(P.apR, PL.apR + EXC * c, x + decay * fbR, -.7));
        r.dr += dk * (tap(P.d1R, PL.d1R) - r.dr);
        push(P.d2R, ap(P.ap2R, PL.ap2R, r.dr * decay, dd2));
        return [.6 * (tap(P.d1R, PT[0]) + tap(P.d1R, PT[1]) - tap(P.ap2R, PT[2]) + tap(P.d2R, PT[3]) - tap(P.d1L, PT[4]) - tap(P.ap2L, PT[5]) - tap(P.d2L, PT[6])),
                .6 * (tap(P.d1L, PT[7]) + tap(P.d1L, PT[8]) - tap(P.ap2L, PT[9]) + tap(P.d2L, PT[10]) - tap(P.d1R, PT[11]) - tap(P.ap2R, PT[12]) - tap(P.d2R, PT[13]))];
    }

    /* the DELAY, as the firmware's: one line of 1 s at the plate's rate (29.4 kHz, ~59 KB at 16 bits;
     * echoes keep everything to ~13 kHz, and TONE's lowpass darkens each repeat), fed one signal: the
     * sends are mono, and in GLOBAL routing the mix is summed. PING-PONG reads the same line at the
     * time (left) and twice it (right, the lowpass twice over, as a second pass makes it), feeding back
     * from the right: the repeats of two lines of the time each, so its time is at most 0.5 s. */
    const DLY_S = 1;
    function delayLine() {
        return { b: new Float32Array(Math.ceil(DLY_S * PR) + 2), w: 0, a: 0, b1: 0, b2: 0,
                 down: rsmp(2, 3), upL: rsmp(3, 2), upR: rsmp(3, 2), go: false };
    }
    function delayRun(d, x, len, fb, k, pp) {      /* one sample at the output's rate */
        d.down.push(x);
        const n = d.b.length;
        while (d.down.n) {
            const v = d.down.pop();
            d.a += (d.b[(d.w - len + n) % n] - d.a) * k;
            let yl = d.a, yr = yl;
            if (pp) {
                d.b1 += (d.b[(d.w - 2 * len + 2 * n) % n] - d.b1) * k;
                d.b2 += (d.b1 - d.b2) * k;
                yr = d.b2 * fb;
            }
            d.b[d.w] = v + (pp ? yr : yl) * fb;
            d.w = (d.w + 1) % n;
            d.upL.push(yl); d.upR.push(yr);
        }
        if (!d.go) { if (d.upL.n < 3) return [0, 0]; d.go = true; }
        return [d.upL.n ? d.upL.pop() : 0, d.upR.n ? d.upR.pop() : 0];
    }

    /* deckBase: a DJ deck's own mixer and small effects (0 or 16, its tracks'), run by the main one's DJ mix */
    E.createFx = function (proc, deckBase) {
        const fx = {
            setup: null, mixer: null, base: deckBase || 0, tempo: 120, master: .5, gain: 1, mono: false,
            live: {}, over: {}, overOn: {},           /* commands: a track's mixer, the FX's knobs, on / off */
            act: {}, cost: {},
            dryL: new Float32Array(BLK), dryR: new Float32Array(BLK),
            sendCho: new Float32Array(BLK), sendDly: new Float32Array(BLK), sendRev: new Float32Array(BLK),
            retL: new Float32Array(BLK), retR: new Float32Array(BLK),
            st: {
                gate: { g: 1 }, filt: [svf(), svf()], drive: [{ hp: 0, lp: 0 }, { hp: 0, lp: 0 }], lofi: { hold: [0, 0], n: 0 },
                env: { v: 0, stage: 0, pos: 0, hits: 0, fireT: -1 },   /* FILTER's envelope */
                cho: { bufL: new Float32Array(4096), bufR: new Float32Array(4096), w: 0, ph: 0 },
                dly: delayLine(),
                rev: reverb(), lim: { g: 1 }, duck: { env: 0 },
            },
            mutes: new Array(34).fill(false), trackGain: new Float32Array(34).fill(1), duckG: new Float32Array(34).fill(1),
            hist: histNew(),
            punch: null, clicks: [],
            /* ECHO THROW and REVERB SWELL work in the song's DELAY line and REVERB plate (the FM-1 has one of each):
             * while they hold them the chain's DELAY / REVERB is skipped; given back, its MIX (REVERB's dry too) glides
             * from theirs over PUNCH_OUT (Hand: samples left; revWet / revDry the swell's last levels); ranDly /
             * ranRev: the chain ran it in this block */
            dlyOver: false, revSwell: false, dlyHand: 0, revHand: 0, revWet: 0, revDry: 1, ranDly: false, ranRev: false,
            bypass: false,                            /* FX LIVE's BYPASS held: the chain and DUCK off */
            anchor: 0,                                /* the audio time of the last bar's start: GATE's step 1 */
            watch: false, blk: 0,                     /* the EFFECTS list on screen: its activity measured, 1 block in 4 */
        };
        /* the FX's codes (specs/files/commands.md): each knob of each effect in order, then FX ON / OFF */
        const FX_ORDER = ["GATE", "FILTER", "DRIVE", "LO-FI", "CHORUS", "DELAY", "REVERB", "LIMITER", "DUCK", "METRONOME"];
        const KNOBS = { GATE: [[0, 1, 0], [0, 1, 0], [0, 1, 0], [0, 1, 0]],
                        FILTER: [[-64, 1, 0], [0, 1, 0], [0, 1, 0], [0, 1, 0], [0, 1, 0], [0, 1, 0], [0, 1, 0], [0, 1, 0]],
                        DRIVE: [[0, 1, 0], [0, 1, 0]],
                        "LO-FI": [[2, 1, 0], [1, 1, 0], [0, 1, 0]], CHORUS: [[0, 1, 2], [0, 1, 0], [0, 1, 0], [0, 1, 0]],
                        DELAY: [[0, 1, 11], [0, 1, 0], [0, 1, 0], [0, 1, 1], [0, 1, 0]], REVERB: [[0, 1, 0], [0, 1, 0], [0, 1, 0], [0, 1, 0]],
                        LIMITER: [[-12, 1, 0], [10, 10, 0]], DUCK: [[0, 1, 0], [0, 1, 0], [20, 10, 0]] };
        const CODES = [];                         /* (by code - 0x90: FILTER's envelope, added later, at 0xB0-0xB5) */
        let code = 0x90;
        for (const n of FX_ORDER.slice(0, 9))
            KNOBS[n].forEach((k, i) => { CODES[(n === "FILTER" && i >= 2 ? 0xB0 + i - 2 : code++) - 0x90] = [n, i, k]; });
        fx.timed = function (ev) {
            /* DJ: a track's mixer command goes to its deck, an FX command to the deck that played it */
            if (fx.dj && fx.decks && (ev.t === "tmix" ? ev.track < 32 : ev.deck != null)) return fx.decks[ev.t === "tmix" ? ev.track >> 4 : ev.deck].timed(ev);
            if (ev.t === "tmix") { (fx.live[ev.track] = fx.live[ev.track] || {})[ev.key] = ev.value; return; }
            if (ev.code === 0xAE || ev.code === 0xAF) { if (FX_ORDER[ev.value]) fx.overOn[FX_ORDER[ev.value]] = ev.code === 0xAE; return; }
            const k = CODES[ev.code - 0x90];
            if (!k) return;
            const [n, i, [min, step, top]] = k;
            fx.over[n + "/" + i] = top ? Math.min(top, ev.value) : min + ev.value * step;
        };
        fx.msg = function (m) {
            if (m.t === "fx") {
                /* another setup (a song's start): the effect under the FX page's cursor kept as it plays */
                if (fx.mine && fx.setup && m.id !== fx.id && fx.setup.knobs[fx.mine]) {
                    const n = fx.mine, was = fx.setup.knobs[n].map((v, i) => { const o = fx.over[n + "/" + i]; return o !== undefined ? o : v; });
                    const wasOn = fx.overOn[n] !== undefined ? fx.overOn[n] : fx.setup.on[n];
                    was.forEach((v, i) => { fx.over[n + "/" + i] = v; });
                    fx.overOn[n] = wasOn;
                }
                /* (a setup taken as the FM-1 takes one: another, or its chain or ROUTING changed; not a knob) */
                const sig = s => s ? JSON.stringify([s.chain, s.routing]) : "", another = m.setup !== fx.setup || sig(m.setup) !== fx.setupSig;
                fx.setup = m.setup; fx.mixer = m.mixer; fx.base = m.base || 0; fx.tempo = m.tempo || fx.tempo; proc.gates = m.gates; fx.id = m.id;
                fx.setupSig = sig(m.setup);
                if (another) setupTaken();
            }
            else if (m.t === "mine") fx.mine = m.fx;
            else if (m.t === "fxreset") {                 /* (but the effect under the FX page's cursor, as it plays) */
                const n = fx.mine, keep = {}, keepOn = n ? fx.overOn[n] : undefined;
                for (const k in fx.over) if (n && k.startsWith(n + "/")) keep[k] = fx.over[k];
                fx.live = {}; fx.over = keep; fx.overOn = keepOn !== undefined ? { [n]: keepOn } : {};
            }
            else if (m.t === "empty") cutTails();         /* all sound off, a clean start: the tails gone */
            else if (m.t === "fxunset") {
                if (m.key) { if (fx.live[m.track]) delete fx.live[m.track][m.key]; }
                else if (m.i < 0) delete fx.overOn[m.name];
                else delete fx.over[m.name + "/" + m.i];
            }
            else if (m.t === "tempo") fx.tempo = m.tempo;
            else if (m.t === "mute") fx.mutes[m.track] = m.on;
            else if (m.t === "hush") fx.hush = !!m.on;    /* CUT (specs/ui/controls.md#cut) */
            else if (m.t === "master") fx.master = m.v;
            else if (m.t === "output") fx.mono = m.mono;
            else if (m.t === "pulseside") fx.pulseSide = m.side;   /* SYNC PULSE: 0 off, 1 left, 2 right */
            else if (m.t === "gain") fx.gain = m.v;
            else if (m.t === "punch") punch(m.k, m.on);
            else if (m.t === "keysdry") fx.keysDry = !!m.on;    /* SETTINGS' KEYS: DRY (specs/ui/screens/fx.md#the-keys-dry) */
            else if (m.t === "keysmix") fx.keysMix = m.m;       /* FX MIXER's KEYS column: their PAN, DUCK and sends */
            else if (m.t === "bypass") fx.bypass = m.on;
            else if (m.t === "bar") { if (m.deck != null && fx.decks) fx.decks[m.deck].anchor = m.time; else fx.anchor = m.time; }
            else if (m.t === "watch") fx.watch = m.on;
            /* DJ (specs/ui/screens/dj.md#effects): on / off and its mix; each deck's song's setup and mixer; the
             * decks' tempos; a deck's commands' changes forgotten (LOAD), or taken over from the song that moved
             * onto it; ECHO OUT's send */
            else if (m.t === "dj") {
                fx.dj = m.on ? m : null;
                if (m.on && !fx.decks) {
                    fx.decks = [E.createFx(proc, 0), E.createFx(proc, 16)];
                    for (const d of fx.decks) { d.mutes = fx.mutes; d.levels = new Float32Array(34); }
                    fx.djF = [0, 1].map(() => ({ dry: [svf(), svf()], snd: [svf(), svf(), svf()] }));
                    fx.djG = [0, 0];
                }
            }
            else if (m.t === "djdeck" && fx.decks) { const d = fx.decks[m.deck]; d.setup = m.setup; d.mixer = m.mixer; }
            else if (m.t === "djtempo" && fx.decks) m.tempo.forEach((v, i) => { if (v) fx.decks[i].tempo = v; });
            else if (m.t === "djreset" && fx.decks) { const d = fx.decks[m.deck]; d.live = {}; d.over = {}; d.overOn = {}; }
            else if (m.t === "djecho") (fx.outs = fx.outs || [{}, {}])[m.deck][m.fx] = m.on;   /* ECHO / REVERB OUT */
        };
        fx.silence = () => { fx.punch = null; takeoverOff(); };
        /* for the screens: what each effect does now, and what the song's commands set (the FX page shows it) */
        fx.stats = () => ({ act: fx.act, duck: fx.st.duck.env, levels: fx.levels, over: fx.over, overOn: fx.overOn, live: fx.live, step: fx.step,
                            fopen: fx.envOpen || 0, fhits: fx.st.env.hits,   /* (FILTER's envelope: the picture) */
                            lfo: fx.dj ? fx.lfoNow : null });   /* (DJ's LFOs: where the crossfader, the filters, the ECHO are now) */
        fx.click = ev => fx.clicks.push({ off: ev.off, accent: ev.accent, sound: ev.sound, level: ev.level / 100, n: 0 });
        /* SYNC PULSE (specs/connections.md#clock): a 5 ms click at full scale on its side, from its frame */
        fx.pulse = ev => { fx.pulseAt = ev.off; };

        const beat = () => 60 / fx.tempo;
        const on = n => fx.setup && (fx.overOn[n] !== undefined ? fx.overOn[n] : fx.setup.on[n]);
        const kn = (n, i) => { const o = fx.over[n + "/" + i]; return o !== undefined ? o : fx.setup.knobs[n][i]; };
        /* CHORUS's, DELAY's or REVERB's MIX, 0-1: on DJ where its FX LFO has it now (fx.lfoMix), else as set */
        const MIX_KNOB = { CHORUS: 3, DELAY: 4, REVERB: 3 };
        const mixOf = n => fx.dj && fx.lfoMix && fx.lfoMix.fx === n ? fx.lfoMix.v : kn(n, MIX_KNOB[n]) / 100;

        /* ------------------------------------------------------------- the mixer --- */
        /* every voice enters the mix at half (-6.02 dB), as on the FM-1: an instrument's loudest at -26 dB of
         * the mix's full scale, a dozen loud ones struck together peaking under it. So the effects that hang
         * on a level (LIMITER's CEILING, DRIVE's clipping, DUCK's following, LO-FI's bits) meet the FM-1's
         * levels; the page plays the mix at twice (PAGE, at the output) and sounds as loud as before */
        const MIX = .5, PAGE = 2;
        const mixRow = t => {
            if (t === KEYS_T) return Object.assign({ lvl: 127 }, fx.keysMix);  /* (the keys': their LVL is KEYS GAIN, on the voices) */
            const row = fx.mixer && fx.mixer[t - fx.base];
            return row ? Object.assign({}, row, fx.live[t]) : null;
        };
        fx.keysMix = { pan: 128, dck: 0, cho: 127, dly: 127, rev: 127 };
        function mixer(tracks, lo = 0, hi = tracks.length) {
            const S = fx.setup, sends = S && S.routing === "SENDS";
            const dL = fx.dryL, dR = fx.dryR;
            dL.fill(0); dR.fill(0); fx.sendCho.fill(0); fx.sendDly.fill(0); fx.sendRev.fill(0);
            /* DUCK, as the FM-1 follows its source: the source track's peak in the mix (its LEVEL in it); 4x
             * that, at most 1, rises at once and falls in a straight line, from 1 to 0 over RELEASE
             * (specs/ui/screens/fx.md); SOURCE 16 is the keys (their own line), which then never duck themselves */
            let src = -1, depth = 0, fall = 1;
            if (S && on("DUCK") && !fx.bypass) {
                const s = kn("DUCK", 0);
                src = s >= 16 ? E.KEYS : fx.base + s;
                depth = kn("DUCK", 1) / 100;
                fall = BLK / (kn("DUCK", 2) / 1000 * SR);
            }
            let level = 0;
            if (src >= 0 && tracks[src]) {
                const b = tracks[src], r = mixRow(src);
                for (let i = 0; i < BLK; i++) { const q = Math.abs(b[i]); if (q > level) level = q; }
                level *= MIX * (r ? r.lvl / 127 : 1);
            }
            const d = fx.st.duck, to = Math.min(1, level * 4);
            d.env = src < 0 ? 0 : to > d.env ? to : Math.max(to, d.env - fall);
            fx.levels = fx.levels || new Float32Array(34);
            for (let t = lo; t < hi; t++) {
                const b = tracks[t];
                let pk = 0;
                for (let i = 0; i < BLK; i++) { const q = b[i] < 0 ? -b[i] : b[i]; if (q > pk) pk = q; }
                fx.levels[t] = Math.max(fx.levels[t] * .9, pk);
                /* the dip this block, eased from the last block's over this one, so it never clicks */
                const r = mixRow(t), dck = r ? r.dck / 127 : 0;
                const k0 = fx.duckG[t], k1 = src >= 0 && t !== src ? 1 - d.env * depth * dck : 1;
                fx.duckG[t] = k1;
                if (!pk) continue;
                let lvl = 1, pan = 0, cho = 0, dly = 0, rev = 0;
                if (r) { lvl = r.lvl / 127; pan = (r.pan - 128) / 128; cho = r.cho / 127; dly = r.dly / 127; rev = r.rev / 127; }
                const tg = fx.mutes[t] ? 0 : 1;
                fx.trackGain[t] += (tg - fx.trackGain[t]) * .5;
                const g = lvl * fx.trackGain[t] * MIX, dk = (k1 - k0) / BLK;
                const gl = g * Math.min(1, 1 - pan), gr = g * Math.min(1, 1 + pan);
                for (let i = 0; i < BLK; i++) { const x = b[i] * (k0 + dk * (i + 1)); dL[i] += x * gl; dR[i] += x * gr; }
                if (sends) {
                    for (let i = 0; i < BLK; i++) { const x = b[i] * (k0 + dk * (i + 1)) * g; fx.sendCho[i] += x * cho; fx.sendDly[i] += x * dly; fx.sendRev[i] += x * rev; }
                }
            }
        }

        /* FILTER's envelope (specs/ui/screens/fx.md#filters-envelope) a block on, its level the block's mean: SOURCE 0
         * the keys, 1-16 a track of what plays (fx.base on), 17-21 the beat every 4, 2, 1, 1/2, 1/4 beats from the
         * bar's start (fx.anchor, as GATE locks to it); the notes started and held come from the voices
         * (proc.fireT, proc.heldT: a track's, the keys' at KEYS_T) */
        const ENV_T = (d, i) => Math.round((d ? 50 : 20) * Math.pow(d ? 1000 : 1500, Math.min(127, i) / 127)) / 10000;
        function envBlock(e, t0) {
            const src = kn("FILTER", 3);
            let fire = 0, held = 0;
            if (src <= 16) {
                const t = src === 0 ? (deckBase != null ? -1 : KEYS_T) : fx.base + src - 1;   /* (KEYS: not on DJ's decks) */
                if (t >= 0) { fire = proc.fireT[t]; held = proc.heldT[t]; }
            } else {
                const I = [4, 2, 1, .5, .25][Math.min(4, src - 17)], b = beat();
                const p0 = (t0 - fx.anchor) / b, p1 = p0 + BLK / SR / b;
                const from = p0 < e.pos - 1e-9 || p0 > e.pos + .25 ? p0 : e.pos;   /* (a jump: from where it landed) */
                fire = Math.ceil(from / I - 1e-9) * I < p1 && !(t0 - e.fireT < I * b / 4) ? 1 : 0;
                held = fire || ((p0 % I) + I) % I < I / 2 ? 1 : 0;
                e.pos = p1;
            }
            if (fire) { e.stage = 1; e.hits++; e.fireT = t0; }
            else if (!held && (e.stage === 1 || e.stage === 2)) e.stage = 3;
            const v0 = e.v;
            if (e.stage === 1) { e.v += BLK / (ENV_T(0, kn("FILTER", 4)) * SR); if (e.v >= 1) { e.v = 1; e.stage = 2; } }
            else if (e.stage === 2) { const s = Math.min(100, kn("FILTER", 6)) / 100; e.v = s + (e.v - s) * Math.exp(-BLK * 4 / (ENV_T(1, kn("FILTER", 5)) * SR)); }
            else if (e.stage === 3) { e.v *= Math.exp(-BLK * 4 / (ENV_T(1, kn("FILTER", 7)) * SR)); if (e.v < 1e-6) { e.v = 0; e.stage = 0; } }
            return (v0 + e.v) / 2;
        }

        /* ------------------------------------------------------------ the effects --- */
        const EFX = {
            GATE(L, R, t0) {
                const pat = proc.gates && proc.gates[kn("GATE", 0)] || [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1];
                const rate = GATE_RATE[kn("GATE", 1)] * beat(), depth = kn("GATE", 2) / 100, sm = kn("GATE", 3) / 100;
                const k = 1 - Math.exp(-1 / ((.0005 + sm * .02) * SR)), st = fx.st.gate;
                /* locked to the song's position: step 1 on the bar's first beat (fx.anchor, posted at every
                 * bar's start); with nothing playing it runs on from the last bar at the tempo */
                for (let i = 0; i < BLK; i++) {
                    const t = (t0 + i / SR - fx.anchor) / rate, step = ((Math.floor(t) % 16) + 16) % 16, v = pat[step], fr = t - Math.floor(t);
                    let open = v === 0 ? 0 : v === 1 ? (fr < .75 ? 1 : 0) : ((fr * v) % 1 < .55 ? 1 : 0);
                    st.g += ((1 - depth) + depth * open - st.g) * k;
                    L[i] *= st.g; R[i] *= st.g;
                }
                fx.step = ((Math.floor((t0 - fx.anchor) / rate) % 16) + 16) % 16;
            },
            FILTER(L, R, t0) {
                const c = kn("FILTER", 0), q = .7 + kn("FILTER", 1) / 100 * 8, env = Math.min(100, kn("FILTER", 2)), e = fx.st.env;
                if (!c || !env) { e.v = 0; e.stage = 0; fx.envOpen = 0; }
                if (!c) return;
                const lp = c < 0;
                let hz = lp ? 20000 * Math.pow(100 / 20000, -c / 64) : 20 * Math.pow(8000 / 20, c / 63);
                if (env) {                               /* its envelope: a share of the way to fully open, in octaves */
                    const amt = envBlock(e, t0) * env / 100;
                    fx.envOpen = amt;
                    hz *= Math.pow((lp ? 20000 : 20) / hz, amt);
                }
                const [a, b] = fx.st.filt;
                for (let i = 0; i < BLK; i++) { L[i] = svfRun(a, L[i], hz, q, lp ? 0 : 1); R[i] = svfRun(b, R[i], hz, q, lp ? 0 : 1); }
            },
            /* DRIVE, as the FM-1 drives the mix: a ~55 Hz low cut keeps the bass out of the clipper (and
             * out of the sound), the drive (1x at 0 % to ~9x at 100 %) into a soft clip with a little bias for
             * the even harmonics, a make-up gain falling with the drive (a quiet sound +1.4 dB at 25 %, +2.4 at
             * 30 %); then TONE's low-pass */
            DRIVE(L, R) {
                const d = Math.floor(kn("DRIVE", 0) * 127 / 100), G = 1 + d * d / 2048, mk = (30000 - 120 * d) / 32768;
                const k = .05 + kn("DRIVE", 1) / 100 * .9, b0 = Math.tanh(DRIVE_BIAS), st = fx.st.drive;
                for (let c = 0; c < 2; c++) {
                    const x = c ? R : L, s = st[c];
                    for (let i = 0; i < BLK; i++) {
                        s.hp += (x[i] - s.hp) / 128;
                        const u = (x[i] - s.hp) * G + DRIVE_BIAS;
                        s.lp += ((Math.tanh(u > 2 ? 2 : u < -2 ? -2 : u) - b0) * mk - s.lp) * k;
                        x[i] = s.lp;
                    }
                }
            },
            "LO-FI"(L, R) {
                const q = Math.pow(2, kn("LO-FI", 0) - 1), hold = Math.max(1, Math.round(SR / (kn("LO-FI", 1) * 1000)));
                const dust = kn("LO-FI", 2) / 100, s = fx.st.lofi;
                for (let i = 0; i < BLK; i++) {
                    if (s.n++ % hold === 0) { s.hold[0] = Math.round(L[i] * q) / q; s.hold[1] = Math.round(R[i] * q) / q; }
                    let n = 0;
                    if (dust && Math.random() < dust * .0015) n = (Math.random() - .5) * .5 * dust;
                    L[i] = s.hold[0] + n; R[i] = s.hold[1] + n;
                }
            },
            CHORUS(L, R, t0, wetOnly, inL, inR) {
                const mode = kn("CHORUS", 0), rate = kn("CHORUS", 1) / 100, depth = kn("CHORUS", 2) / 100, mix = mixOf("CHORUS");
                const hz = [.5, .83, 9][mode] * (.3 + rate * 1.4), dep = [.0019, .0029, .0006][mode] * (.3 + depth * 1.4);
                const c = fx.st.cho;
                for (let i = 0; i < BLK; i++) {
                    const xl = inL ? inL[i] : L[i], xr = inR ? inR[i] : R[i];
                    c.bufL[c.w & 4095] = xl; c.bufR[c.w & 4095] = xr;
                    c.ph += hz / SR;
                    const m = Math.sin(c.ph * TWO_PI);
                    const rd = (dl) => { const r = c.w - dl * SR, ri = Math.floor(r), f = r - ri; return [ri & 4095, (ri + 1) & 4095, f]; };
                    const [a0, a1, af] = rd(.004 + dep * (1 + m)), [b0, b1, bf] = rd(.004 + dep * (1 - m));
                    const yl = c.bufL[a0] * (1 - af) + c.bufL[a1] * af, yr = c.bufR[b0] * (1 - bf) + c.bufR[b1] * bf;
                    c.w++;
                    if (wetOnly) { L[i] += yl * mix; R[i] += yr * mix; }
                    else { L[i] = xl * (1 - mix * .5) + yl * mix; R[i] = xr * (1 - mix * .5) + yr * mix; }
                }
            },
            DELAY(L, R, t0, wetOnly, inL, inR) {
                const pp = kn("DELAY", 3) === 1, most = pp ? DLY_S / 2 : DLY_S;
                let nv = kn("DELAY", 0);                              /* a TIME that does not fit: the longest that does */
                while (nv > 0 && NV[NV_LIST[nv]] * beat() > most) nv--;
                const time = Math.min(most, NV[NV_LIST[nv]] * beat()), fb = kn("DELAY", 1) / 100 * .92;
                const tone = kn("DELAY", 2) / 100, d = fx.st.dly;
                let mix = mixOf("DELAY");
                if (fx.dlyHand > 0) {                                 /* (just back from ECHO THROW: its MIX from the throw's) */
                    mix += (.8 - mix) * fx.dlyHand / (PUNCH_OUT * SR);
                    fx.dlyHand = Math.max(0, fx.dlyHand - BLK);
                }
                fx.ranDly = true;
                const len = Math.max(1, Math.round(time * PR)), k = 1 - Math.pow(1 - (.1 + tone * .85), PK);
                for (let i = 0; i < BLK; i++) {
                    const xl = inL ? inL[i] : L[i], xr = inR ? inR[i] : R[i];
                    const [yl, yr] = delayRun(d, (xl + xr) * .5, len, fb, k, pp);
                    if (wetOnly) { L[i] += yl * mix; R[i] += yr * mix; }
                    else { L[i] = xl + yl * mix; R[i] = xr + yr * mix; }
                }
            },
            REVERB(L, R, t0, wetOnly, inL, inR) {
                const size = kn("REVERB", 0) / 100, damp = kn("REVERB", 1) / 100, pre = Math.round(kn("REVERB", 2) / 1000 * SR);
                const r = fx.st.rev, n = r.pre.length, hand = fx.revHand > 0;
                let mix = mixOf("REVERB"), dry = wetOnly ? 1 : 1 - mix * .4;
                if (hand) {                                           /* (just back from REVERB SWELL: its levels gliding to
                                                                       * these; the sends' dry, which it lowered, to whole) */
                    const t = fx.revHand / (PUNCH_OUT * SR);
                    mix += (fx.revWet - mix) * t; dry += (fx.revDry - dry) * t;
                    fx.revHand = Math.max(0, fx.revHand - BLK);
                }
                fx.ranRev = true;
                for (let i = 0; i < BLK; i++) {
                    const xl = inL ? inL[i] : L[i], xr = inR ? inR[i] : R[i];
                    r.pre[r.pw] = (xl + xr) * .5;
                    const x = r.pre[(r.pw - pre + n) % n];
                    r.pw = (r.pw + 1) % n;
                    const [yl, yr] = revRun(r, x, x, size, damp);
                    if (wetOnly) { if (hand) { L[i] *= dry; R[i] *= dry; } L[i] += yl * mix; R[i] += yr * mix; }
                    else { L[i] = xl * dry + yl * mix; R[i] = xr * dry + yr * mix; }
                }
            },
            LIMITER(L, R) {
                const ceil = E.dB(kn("LIMITER", 0)), rel = Math.exp(-1 / (kn("LIMITER", 1) / 1000 * SR)), s = fx.st.lim;
                let red = 1;
                for (let i = 0; i < BLK; i++) {
                    const pk = Math.max(Math.abs(L[i]), Math.abs(R[i])) * s.g;
                    if (pk > ceil) s.g = ceil / Math.max(Math.abs(L[i]), Math.abs(R[i]));
                    else s.g = 1 - (1 - s.g) * rel;
                    L[i] *= s.g; R[i] *= s.g;
                    red = Math.min(red, s.g);
                }
                fx.limRed = red;
            },
        };

        /* the punch-ins, from the last 0.5 s of the mix (specs/ui/screens/fx.md) */
        function punch(k, down) {
            const H = fx.hist, n = PUNCH_BUF;
            /* let go: vinyl stop spins back up from what was heard last, landing in time; every other one
             * fades back into the mix over PUNCH_OUT (it keeps running under the fade) */
            if (!down) {
                const p = fx.punch;
                if (p && p.k === k && p.out == null) {
                    if (k === 15) { p.up = true; p.t = 0; p.rp = (H.w - Math.round(SR * .05) + n) % n; }
                    else { p.out = 0; if (k === 11) swellGive(); }
                }
                return;
            }
            const was = fx.punch;                                /* (replaced: ECHO THROW's echoes left in the line for
                                                                  * DELAY, REVERB SWELL's plate to REVERB as let go) */
            if (was && was.k === 10 && fx.dlyOver) throwGive();
            if (was && was.k === 11 && fx.revSwell) swellGive();
            const b = beat(), LOOP = [1, .5, .25, .125];
            const p = { k, t: 0, w0: H.w };
            if (k <= 3) p.len = Math.max(64, Math.round(LOOP[k] * b * SR));
            if (k === 4) p.len = Math.round(b / 6 * SR);
            if (k === 12) p.len = Math.round(b * SR);
            if (p.len) {
                while (p.len > PUNCH_SPAN) p.len >>= 1;          /* a beat that does not fit: the longest note value that does */
                p.start = (H.w - PUNCH_LAG - p.len + 2 * n) % n;
                p.keep = true;                                   /* the buffer keeps what it replays: no recording meanwhile */
            }
            if (k === 12) p.start = (H.w - PUNCH_LAG + n) % n;
            if (k >= 13) { p.rp = (H.w - 1 - PUNCH_LAG + n) % n; p.speed = 1; }
            p.f = [svf(), svf()];
            p.um = bqState(); p.us = bqState();                  /* the buffer read back: its low-passes */
            /* (from where the DELAY / REVERB were: the throw's echoes from DELAY's MIX, the swell from REVERB's wet, dry
             * and feed; off: from nothing) */
            if (k === 10) { fx.dlyOver = true; fx.dlyHand = 0; p.from = on("DELAY") || outsOn("DELAY") ? mixOf("DELAY") : 0; }
            if (k === 11) {
                const was = on("REVERB") || outsOn("REVERB"), mix = mixOf("REVERB");
                const chain = was && !fx.dj && fx.setup && fx.setup.routing !== "SENDS";
                p.from = { wet: was ? mix : 0, dry: chain ? 1 - mix * .4 : 1, in: chain ? 1 : 0 };
                fx.revSwell = true; fx.revHand = 0; fx.revWet = p.from.wet; fx.revDry = p.from.dry;
            }
            fx.punch = p;
        }
        /* the line / the plate given back to the chain: its MIX gliding from the punch-in's when the chain runs it */
        const outsOn = n => !!(fx.dj && fx.outs && fx.outs.some(o => o && o[n]));   /* (DJ's ECHO / REVERB OUT run it too) */
        function throwGive() { fx.dlyOver = false; fx.dlyHand = on("DELAY") || outsOn("DELAY") ? PUNCH_OUT * SR : 0; }
        function swellGive() { fx.revSwell = false; fx.revHand = on("REVERB") || outsOn("REVERB") ? PUNCH_OUT * SR : 0; }
        function takeoverOff() { fx.dlyOver = fx.revSwell = false; fx.dlyHand = fx.revHand = 0; }
        /* another setup taken (a song's or a source's, REVERT, a chain or ROUTING edit): REVERB SWELL as let go, ECHO
         * THROW over at once, its echoes in the line for the setup's DELAY */
        function setupTaken() {
            const p = fx.punch;
            if (p && p.k === 11 && p.out == null) { p.out = 0; swellGive(); }
            if (p && p.k === 10) fx.punch = null;
            fx.dlyOver = fx.revSwell = false;
        }
        function punchRun(L, R) {
            const H = fx.hist, n = PUNCH_BUF, b = beat() * SR, OUT = PUNCH_OUT * SR;
            if (fx.punch && (fx.punch.k === 10 ? fx.ranDly : fx.punch.k === 11 && fx.ranRev))
                fx.punch = null;                                   /* (the chain ran the line / the plate in this block: it
                                                                    * has them back; never run twice in a block) */
            const p = fx.punch;
            if (p && (p.k === 10 || p.k === 11)) return spaceRun(p, L, R, H, b, OUT);
            for (let i = 0; i < BLK; i++) {
                if (!fx.punch || !fx.punch.keep) histRec(H, L[i], R[i]);
                if (!p || fx.punch !== p) continue;
                const k = p.k, t = p.t++, dl = L[i], dr = R[i];
                if (p.out != null) {                               /* let go: the effect fading back into the mix */
                    punchSample(p, k, t, L, R, i, H, n, b);
                    const g = 1 - p.out++ / OUT;
                    L[i] = dl + (L[i] - dl) * g; R[i] = dr + (R[i] - dr) * g;
                    if (p.out >= OUT) fx.punch = null;
                    continue;
                }
                punchSample(p, k, t, L, R, i, H, n, b);
            }
        }
        /* ECHO THROW and REVERB SWELL (keys 11, 12), in the song's line and plate (the chain's DELAY / REVERB skipped
         * meanwhile). The throw: the mix into the line, a dotted 1/8 at 65 % feedback, ping-pong, TONE 60, its echoes
         * at 0.8, from DELAY's MIX over PUNCH_OUT; let go, nothing more in, the echoes ringing 30 x PUNCH_OUT, then the
         * line back with DELAY. The swell: the mix into the plate (SIZE 95, DAMP 20, no PRE-DELAY), from REVERB's wet,
         * dry and feed (p.from) to its own over 2 beats, wet 1.2 and dry 0.5; let go, the plate back with REVERB at
         * once (the chain then runs it: punchRun ends this), or, REVERB off, faded out over PUNCH_OUT and emptied */
        function spaceRun(p, L, R, H, b, OUT) {
            const d = fx.st.dly, len = Math.min(d.b.length / 2 - 2, Math.round(b / SR * .75 * PR));
            const kT = 1 - Math.pow(1 - (.1 + .6 * .85), PK);
            for (let i = 0; i < BLK; i++) {
                histRec(H, L[i], R[i]);
                if (fx.punch !== p) continue;
                const t = p.t++, held = p.out == null;
                if (p.k === 10) {
                    const [yl, yr] = delayRun(d, held ? (L[i] + R[i]) * .5 : 0, len, .65, kT, true);
                    const m = t >= OUT ? .8 : p.from + (.8 - p.from) * t / OUT;
                    L[i] += yl * m; R[i] += yr * m;
                    if (!held && ++p.out >= OUT * 30) { fx.punch = null; throwGive(); }
                } else {
                    const sw = Math.min(1, t / (b * 2)) * (held ? 1 : Math.max(0, 1 - p.out / OUT)), f = p.from;
                    const x = held ? (L[i] + R[i]) * .5 * (f.in + (1 - f.in) * sw) : 0;
                    const [yl, yr] = revRun(fx.st.rev, x, x, .95, .2);
                    fx.revWet = f.wet + (1.2 - f.wet) * sw; fx.revDry = f.dry + (.5 - f.dry) * sw;
                    L[i] = L[i] * fx.revDry + yl * fx.revWet; R[i] = R[i] * fx.revDry + yr * fx.revWet;
                    if (!held && ++p.out >= OUT) { fx.punch = null; fx.revHand = 0; fx.st.rev = reverb(); }
                }
            }
        }
        /* one sample of a punch-in, in place. The keys' order: loops and the stutter (keys 1-5), the chops
         * (6-8), the sweeps and the spaces (9-12: the spaces in spaceRun), the tape moves (13-16) */
        function punchSample(p, k, t, L, R, i, H, n, b) {
            if (k <= 4) {                                     /* loops and the stutter: the last bit, again */
                const j = (p.start + (t % p.len)) % n, fade = Math.min(1, (t % p.len) / 64, (p.len - t % p.len) / 64);
                [L[i], R[i]] = histPlay(p, H, j, fade);
            } else if (k === 5) {                             /* bit crush */
                if (t % 6 === 0) { p.hl = Math.round(L[i] * 8) / 8; p.hr = Math.round(R[i] * 8) / 8; }
                L[i] = p.hl; R[i] = p.hr;
            } else if (k === 6 || k === 7) {                  /* gate 1/16, 1/32 */
                const len = b / (k === 6 ? 4 : 8), open = (t % len) < len * .5 ? 1 : 0;
                p.g = (p.g || 0) + (open - (p.g || 0)) * .02;
                L[i] *= p.g; R[i] *= p.g;
            } else if (k === 8 || k === 9) {                  /* sweeps over a bar, faded in over PUNCH_OUT (no bump) */
                const x = Math.min(1, t / (b * 4)), g = Math.min(1, t / (PUNCH_OUT * SR)), dl = L[i], dr = R[i];
                const hz = k === 8 ? 18000 * Math.pow(200 / 18000, x) : 30 * Math.pow(6000 / 30, x);
                L[i] = svfRun(p.f[0], L[i], hz, 2.5, k === 8 ? 0 : 1); R[i] = svfRun(p.f[1], R[i], hz, 2.5, k === 8 ? 0 : 1);
                L[i] = dl + (L[i] - dl) * g; R[i] = dr + (R[i] - dr) * g;
            } else if (k === 12) {                            /* reverse: the last beat backwards */
                const j = (p.start - 1 - (t % p.len) + 2 * n) % n;
                [L[i], R[i]] = histPlay(p, H, j, 1);
            } else {                                          /* half speed, tape stop, vinyl stop */
                let g = 1;
                if (k === 13) {
                    /* half speed falls behind by half the time held: once that reaches the buffer, it starts
                     * again from the moment (every second), a short fade over the seam */
                    if (p.hs == null || (t - p.hs) * .5 >= PUNCH_SPAN - 1) {
                        p.seam = p.hs != null; p.hs = t; p.w0 = (H.w - 1 - PUNCH_LAG + n) % n;
                    }
                    const u = t - p.hs;
                    p.rp = (p.w0 + u * .5) % n;
                    g = Math.min(1, p.seam ? u / 64 : 1, (PUNCH_SPAN - 1 - u * .5) / 32);
                } else {
                    if (k === 14) p.speed = Math.max(0, 1 - t / Math.min(b, STOP_MOST));
                    else p.speed = p.up ? Math.min(1, t / (.1 * SR)) : Math.max(0, 1 - t / (.5 * SR));   /* (specs/ui/screens/fx.md: 0.5 s) */
                    p.rp = (p.rp + p.speed) % n;
                    g = Math.min(1, p.speed * 10);            /* stopped: silence, not the last sample held */
                    if ((H.w - 1 - Math.floor(p.rp) + 2 * n) % n + 1 > n - 16) g = 0;   /* (fallen behind past the buffer) */
                }
                [L[i], R[i]] = histPlay(p, H, p.rp, g);
                if (k === 15 && p.up && p.speed >= 1) fx.punch = null;    /* spun back up: the song again */
            }
        }
        /* the METRONOME's sounds */
        function clicks(L, R) {
            const keep = [];
            for (const c of fx.clicks) {
                for (let i = c.off; i < BLK; i++) {
                    const t = c.n++ / SR;
                    if (t > .08) break;
                    let x;
                    if (c.sound === 0) x = (Math.random() * 2 - 1) * Math.exp(-t * 300);
                    else if (c.sound === 1) x = Math.sin(TWO_PI * (c.accent ? 1500 : 1000) * t) * Math.exp(-t * 60);
                    else if (c.sound === 2) x = Math.sin(TWO_PI * (c.accent ? 1100 : 800) * t * (1 - t * 2)) * Math.exp(-t * 90);
                    else x = (Math.sign(Math.sin(TWO_PI * 540 * t)) + Math.sign(Math.sin(TWO_PI * 800 * t))) * .4 * Math.exp(-t * 40);
                    x *= c.level * (c.accent ? .5 : .32) * MIX;   /* (in the mix as a voice is) */
                    L[i] += x; R[i] += x;
                }
                c.off = 0;
                if (c.n / SR <= .08) keep.push(c);
            }
            fx.clicks = keep;
        }

        /* ------------------------------------------------------------------- DJ --- */
        /* a DJ deck's own part (specs/ui/screens/dj.md#effects): its song's mixer on its 16 tracks (levels,
         * pans, DUCK, sends, = commands), then its song's GATE, FILTER, DRIVE and LO-FI in the chain's order;
         * CHORUS, DELAY and REVERB are DJ's, shared: in SENDS routing the tracks' sends feed them, in GLOBAL
         * the deck's whole mix goes to the ones its song has on, at their MIX; the LIMITER is DJ's */
        fx.deckRun = function (tracks, t0) {
            mixer(tracks, fx.base, fx.base + 16);
            const S = fx.setup;
            if (!S) return;
            const dL = fx.dryL, dR = fx.dryR, snd = { CHORUS: fx.sendCho, DELAY: fx.sendDly, REVERB: fx.sendRev };
            for (const name of S.chain) {
                if (name === "SENDS" || name === "LIMITER") continue;
                if (snd[name]) {
                    if (S.routing !== "SENDS" && on(name)) {
                        const k = kn(name, MIX_KNOB[name]) / 100, b = snd[name];
                        for (let i = 0; i < BLK; i++) b[i] += (dL[i] + dR[i]) * .5 * k;
                    }
                    continue;
                }
                if (on(name) && EFX[name]) EFX[name](dL, dR, t0, false);
            }
        };
        /* the DJ FILTER, in place: low-pass one way, high-pass the other, OFF (0) in the middle; CUT as FILTER's */
        function djFilter(st, x, cut, q) {
            if (!cut) return;
            const lp = cut < 0, hz = lp ? 20000 * Math.pow(100 / 20000, -cut / 64) : 20 * Math.pow(8000 / 20, cut / 63);
            for (let i = 0; i < BLK; i++) x[i] = svfRun(st, x[i], hz, q, lp ? 0 : 1);
        }
        /* DJ's LFOs (specs/ui/screens/dj.md#the-lfo-pads): each swings by itself, its phase counted here a block at
         * a time (ph: cycles since it set off), from `from` to `to` and back as its shape goes (w 0 at from, 1 at
         * to); off, the thing is where it is set. What comes out is smoothed over about 15 ms, so a change of
         * shape or range, or switching off, glides instead of clicking. The shapes move as a hand on a knob would:
         * SQUARE's flips and the saws' jumps back take a hand's 90 ms, eased in and out (less of a cycle at high
         * speeds: never more than a quarter); TRIANGLE slows into its corners and starts back slowly (a sixth of each
         * way eased at either end); RANDOM glides to a new place each cycle */
        const LFO_SLEW = 1 - Math.exp(-BLK / SR / .015), LFO_HAND = .09, TRI_EASE = .17;
        const rc = x => (1 - Math.cos(Math.PI * x)) / 2;
        /* TRIANGLE's way, 0-1 eased at both ends (parabolas joining the straight middle, its slope kept), and back */
        const K2 = 2 * TRI_EASE * (1 - TRI_EASE), KW = TRI_EASE / (2 * (1 - TRI_EASE));
        const ease = x => x < TRI_EASE ? x * x / K2 : x > 1 - TRI_EASE ? 1 - (1 - x) * (1 - x) / K2 : (x - TRI_EASE / 2) / (1 - TRI_EASE);
        const unease = w => w < KW ? Math.sqrt(K2 * w) : w > 1 - KW ? 1 - Math.sqrt(K2 * (1 - w)) : w * (1 - TRI_EASE) + TRI_EASE / 2;
        const handOf = hz => Math.min(.25, LFO_HAND * hz);
        function lfoW(st, shape, hz) {
            const f = st.ph - Math.floor(st.ph), e = handOf(hz);
            switch (shape) {
                case 0: return (1 - Math.cos(TWO_PI * f)) / 2;                                       /* SINE */
                case 1: return f < e ? rc(f / e) : f < .5 ? 1 : f < .5 + e ? 1 - rc((f - .5) / e) : 0;   /* SQUARE */
                case 2: return ease(f < .5 ? 2 * f : 2 - 2 * f);                                     /* TRIANGLE */
                case 3: return f < 1 - e ? f / (1 - e) : 1 - rc((f - 1 + e) / e);                    /* SAW UP */
                case 4: return f < e ? rc(f / e) : 1 - (f - e) / (1 - e);                            /* SAW DOWN */
                default: return st.r0 + (st.r1 - st.r0) * rc(f);                                    /* RANDOM */
            }
        }
        /* a fresh LFO sets off from where the thing is (u, between from and to), heading for `to` */
        function lfoStart(st, a) {
            const e = handOf(a.hz);
            st.ph = a.shape === 0 ? Math.acos(1 - 2 * a.u) / TWO_PI : a.shape === 2 ? unease(a.u) / 2 : a.shape === 3 ? a.u * (1 - e) : a.shape === 5 ? 0 : e;
            st.r0 = a.u; st.r1 = Math.random(); st.c = Math.floor(st.ph);
            st.gen = a.gen;
        }
        /* a new one sets off at a.at (a step), its phase from there; switched off (a: null), its swing fades out
         * over LFO_OFF, running on meanwhile; home: where the thing is set, where it comes back to */
        const LFO_OFF = .2;
        function lfoRun(st, a, home, t) {
            if (a && ((st.gen === a.gen && st.a) || !(t < a.at))) {
                if (st.gen !== a.gen || !st.a) {             /* (from its beat; a filter's key on later: from now) */
                    const late = t - (a.at || t);
                    lfoStart(st, a);
                    if (late > 0 && late < .05) st.ph += late * a.hz;
                }
                st.a = a; st.g = 1;
            }
            else if (st.a && (st.g -= BLK / SR / LFO_OFF) <= 0) st.a = null;
            let to = home;
            const b = st.a;
            if (b) {
                if (Math.floor(st.ph) !== st.c) { st.c = Math.floor(st.ph); st.r0 = st.r1; st.r1 = Math.random(); }   /* (RANDOM's next place) */
                to = home + (b.from + (b.to - b.from) * lfoW(st, b.shape, b.hz) - home) * st.g;
                st.ph += b.hz * BLK / SR;
            }
            st.v = st.v == null || Math.abs(to - st.v) < .01 ? to : st.v + (to - st.v) * LFO_SLEW;
            return st.v;
        }
        /* DJ's mix: each deck's part, its DJ FILTER (its three send streams too, each through a copy of it, with
         * FILTER THE SENDS), then the crossfader on its sound and its sends alike; the shared CHORUS, DELAY and
         * REVERB with DJ's settings (ECHO OUT: the deck's whole sound into the DELAY, on for it); DJ's LIMITER.
         * The LFOs move the crossfader, the filters and the FX LFO's MIX (only an effect that is on: off, it
         * does nothing) */
        function djMix(tracks, t0) {
            const dj = fx.dj, dL = fx.dryL, dR = fx.dryR, q = .7 + dj.reso / 100 * 8, lfo = dj.lfo || {};
            dL.fill(0); dR.fill(0); fx.sendCho.fill(0); fx.sendDly.fill(0); fx.sendRev.fill(0);
            fx.levels = fx.levels || new Float32Array(34);
            const S = fx.lfoSt = fx.lfoSt || { fader: {}, filt: [{}, {}], fx: {}, heard: [0, 0], plain: [{}, {}] };
            dj.xfNow = lfoRun(S.fader, lfo.fader, dj.xf, t0);   /* (read by proc.djGain) */
            /* a filter's LFO swings on while its FILTER key has the filter off (heard false: a preview, around the
             * setting the key brings in); what is heard glides between the swing and the plain filter (itself
             * smoothed as ever) over the slew's 15 ms as the key comes and goes; an LFO switched off keeps where it
             * was, heard or not, fading out */
            const filtLfo = k => lfo.filt && lfo.filt[k], plain = k => dj.filt ? dj.filt[k] : 0;
            const glide = (st, to) => (st.v = st.v == null || Math.abs(to - st.v) < .01 ? to : st.v + (to - st.v) * LFO_SLEW);
            const prev = [0, 1].map(k => lfoRun(S.filt[k], filtLfo(k), filtLfo(k) ? filtLfo(k).home : plain(k), t0));
            const cuts = prev.map((v, k) => {
                const a = filtLfo(k), p = glide(S.plain[k], plain(k));
                if (a) { const on = a.heard ? 1 : 0; S.heard[k] = Math.abs(on - S.heard[k]) < .01 ? on : S.heard[k] + (on - S.heard[k]) * LFO_SLEW; }
                return p + (v - p) * S.heard[k];
            });
            /* the FX LFO: its effect's MIX as set ↔ 100 %, or 0 ↔ 100 % setting off from it; another effect picked,
             * the one before goes back to its MIX at once */
            const fl = lfo.fx, name = fl ? fl.fx : S.fx.name;
            if (name !== S.fx.name) { S.fx = { name }; fx.lfoMix = null; }
            let mix = null;
            if (name && on(name) && name !== fx.mine) {     /* (not the FX page's effect under the cursor: the hand's) */
                const set = kn(name, MIX_KNOB[name]) / 100;
                mix = lfoRun(S.fx, fl && Object.assign({}, fl, { from: fl.range ? 0 : set, to: 1, u: fl.range ? set : 0 }), set, t0);
                fx.lfoMix = S.fx.a ? { fx: name, v: mix } : null;
            } else fx.lfoMix = null;
            fx.lfoNow = { xf: dj.xfNow, filt: cuts, prev, mix };     /* (prev: the filters' swings, heard or not) */
            const outRun = { DELAY: false, REVERB: false };     /* (run for an ECHO / REVERB OUT even when off) */
            for (let k = 0; k < 2; k++) {
                const d = fx.decks[k], f = fx.djF[k], cut = cuts[k];
                d.deckRun(tracks, t0);
                for (let t = 16 * k; t < 16 * k + 16; t++) fx.levels[t] = d.levels[t];
                djFilter(f.dry[0], d.dryL, cut, q); djFilter(f.dry[1], d.dryR, cut, q);
                if (dj.sendsFiltered) { djFilter(f.snd[0], d.sendCho, cut, q); djFilter(f.snd[1], d.sendDly, cut, q); djFilter(f.snd[2], d.sendRev, cut, q); }
                /* ECHO OUT (DELAY), REVERB OUT: true, the deck's dry sound into it; "ring", fed no more, its tail
                 * ringing out */
                const g0 = fx.djG[k], g1 = proc.djGain(k) * (dj.vol ? dj.vol[k] : 1), dg = (g1 - g0) / BLK, o = fx.outs ? fx.outs[k] : {};   /* (the crossfader times the deck's VOLUME) */
                const eo = o.DELAY === true, ro = o.REVERB === true;
                fx.djG[k] = g1;
                if (o.DELAY) outRun.DELAY = true;
                if (o.REVERB) outRun.REVERB = true;
                for (let i = 0; i < BLK; i++) {
                    const g = g0 + dg * (i + 1), dry = (d.dryL[i] + d.dryR[i]) * .5;
                    dL[i] += d.dryL[i] * g; dR[i] += d.dryR[i] * g;
                    fx.sendCho[i] += d.sendCho[i] * g;
                    fx.sendRev[i] += (d.sendRev[i] + (ro ? dry : 0)) * g;
                    fx.sendDly[i] += (d.sendDly[i] + (eo ? dry : 0)) * g;
                }
            }
            if (fx.bypass) return;                           /* (FX LIVE's BYPASS: the shared effects off while held) */
            for (const [n, src] of [["CHORUS", fx.sendCho], ["DELAY", fx.sendDly], ["REVERB", fx.sendRev]])
                if ((on(n) || outRun[n]) && !taken(n)) EFX[n](dL, dR, t0, true, src, src);
            if (on("LIMITER")) EFX.LIMITER(dL, dR);
        }

        const rms = (L, R) => { let s = 0; for (let i = 0; i < BLK; i += 4) s += L[i] * L[i] + R[i] * R[i]; return Math.sqrt(s / (BLK / 2)); };
        /* DJ's REC held: everything heard (PREVIEW's ear too) down to nothing in about 12 ms, and back up when let go.
         * Silent, the effects' tails are cut (the shared CHORUS, DELAY and REVERB, ECHO / REVERB OUT's rings, a
         * punch-in and its buffer): once faded out, and again as it is let go, so what follows starts dry */
        function cutTails() {
            const c = fx.st.cho;
            c.bufL.fill(0); c.bufR.fill(0);
            fx.st.dly = delayLine();
            fx.st.rev = reverb();
            fx.outs = [{}, {}];
            fx.punch = null;
            takeoverOff();
            histClear(fx.hist);
            fx.st.env.v = 0; fx.st.env.stage = 0; fx.envOpen = 0;
        }
        /* CUT (specs/ui/controls.md#cut): the output faded to silence over ~12 ms, the effects emptied once silent and
         * again as it ends (they start dry); what plays plays on unheard */
        function hush() {
            const on = fx.hush;
            const g0 = fx.hushG == null ? 1 : fx.hushG, g1 = on ? Math.max(0, g0 - .25) : Math.min(1, g0 + .25);
            fx.hushG = g1;
            if (g0 === 0 && (on ? !fx.hushCut : fx.hushCut)) { cutTails(); fx.hushCut = !!on; }
            if (g0 === 1 && g1 === 1) return;
            for (let i = 0; i < BLK; i++) {
                const g = g0 + (g1 - g0) * (i + 1) / BLK;
                fx.dryL[i] *= g; fx.dryR[i] *= g;
            }
        }
        /* the keys heard DRY (SETTINGS' KEYS, specs/ui/screens/fx.md#the-keys-dry): their track (the last) past every
         * effect, the punch-ins and the METRONOME, into the mix just before the output stage */
        const KEYS_T = 33;
        function keysDryIn(tracks) {
            const b = tracks[KEYS_T], dL = fx.dryL, dR = fx.dryR;
            if (!b) return;
            let pk = 0;
            for (let i = 0; i < BLK; i++) { const q = b[i] < 0 ? -b[i] : b[i]; if (q > pk) pk = q; }
            fx.levels[KEYS_T] = Math.max(fx.levels[KEYS_T] * .9, pk);
            fx.trackGain[KEYS_T] += (1 - fx.trackGain[KEYS_T]) * .5;
            if (!pk) return;
            const g = fx.trackGain[KEYS_T] * MIX, pan = (fx.keysMix.pan - 128) / 128;   /* (their PAN, FX MIXER's KEYS) */
            const gl = g * Math.min(1, 1 - pan), gr = g * Math.min(1, 1 + pan);
            for (let i = 0; i < BLK; i++) { dL[i] += b[i] * gl; dR[i] += b[i] * gr; }
        }
        /* the chain's DELAY / REVERB, not while ECHO THROW / REVERB SWELL holds the line / the plate */
        const taken = n => (n === "DELAY" && fx.dlyOver) || (n === "REVERB" && fx.revSwell);
        fx.process = function (tracks, L, R, t0) {
            const dry = fx.keysDry && !fx.dj;
            fx.ranDly = fx.ranRev = false;
            if (fx.dj) djMix(tracks, t0);
            else mixer(tracks, 0, dry ? KEYS_T : tracks.length);
            const dL = fx.dryL, dR = fx.dryR, S = fx.setup;
            /* the activity meters cost two RMS passes an effect: measured only while the EFFECTS list
             * shows them, one block in 4, each held to its peak between (specs/ui/screens/fx.md#edit) */
            const look = fx.watch && (fx.blk++ & 3) === 0;
            if (S && !fx.bypass && !fx.dj) {
                const sends = S.routing === "SENDS";
                for (const name of S.chain) {
                    if (name === "SENDS") {
                        if (!sends) continue;
                        const pairs = [["CHORUS", fx.sendCho], ["DELAY", fx.sendDly], ["REVERB", fx.sendRev]];
                        for (const [n, src] of pairs) {
                            if (!on(n) || taken(n)) continue;
                            const before = look ? rms(dL, dR) : 0;
                            EFX[n](dL, dR, t0, true, src, src);
                            if (look) fx.act[n] = Math.max(fx.act[n] * .8 || 0, Math.abs(rms(dL, dR) - before) * 3);
                        }
                        continue;
                    }
                    if (sends && (name === "CHORUS" || name === "DELAY" || name === "REVERB")) continue;
                    if (!on(name) || !EFX[name] || taken(name)) { if (look) fx.act[name] = (fx.act[name] || 0) * .8; continue; }
                    const before = look ? rms(dL, dR) : 0;
                    EFX[name](dL, dR, t0, false);
                    if (look) fx.act[name] = Math.max((fx.act[name] || 0) * .85, Math.min(1, Math.abs(rms(dL, dR) - before) * 4 + before * .5));
                }
                fx.act.DUCK = on("DUCK") ? fx.st.duck.env : 0;
            }
            punchRun(dL, dR);
            const mb = look ? rms(dL, dR) : 0;
            clicks(dL, dR);
            if (look) fx.act.METRONOME = S && on("METRONOME") ?     /* (its meter: what its clicks add, as a chain effect's) */
                Math.max((fx.act.METRONOME || 0) * .85, Math.min(1, Math.abs(rms(dL, dR) - mb) * 4 + mb * .5)) : (fx.act.METRONOME || 0) * .8;
            if (dry) keysDryIn(tracks);                  /* (DRY: past the effects, the punch-ins, the METRONOME) */
            hush();                                      /* (after the punch-ins and the METRONOME: nothing heard) */
            const g = fx.master * fx.gain * PAGE;        /* MASTER and LINE GAIN (specs/ui/screens/settings.md); the page's level */
            const ps = fx.pulseSide || 0;                /* SYNC PULSE: its side the clicks, the other the music mono */
            for (let i = 0; i < BLK; i++) {
                let l = dL[i] * g, r = dR[i] * g;
                if (fx.mono || ps) l = r = (l + r) * .5;
                L[i] = l > .8 || l < -.8 ? Math.sign(l) * (.8 + .2 * Math.tanh((Math.abs(l) - .8) / .2)) : l;     /* a soft ceiling */
                R[i] = r > .8 || r < -.8 ? Math.sign(r) * (.8 + .2 * Math.tanh((Math.abs(r) - .8) / .2)) : r;
                if (ps) {
                    if (fx.pulseAt === i) fx.pulseLeft = Math.round(SR * .005);
                    const c = fx.pulseLeft > 0 ? Math.min(1, g) : 0;
                    if (fx.pulseLeft > 0) fx.pulseLeft--;
                    if (ps === 1) L[i] = c; else R[i] = c;
                }
            }
            fx.pulseAt = -1;
            fx.out = rms(L, R);
        };
        return fx;
    };
});

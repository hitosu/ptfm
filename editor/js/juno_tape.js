/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Juno tape recordings (specs/editor.md, Juno tape recordings): the Juno-106's and the Juno-60's cassette dumps read
 * from sound, as it comes (the recorder) or all at once (a drop).
 *   - The Juno-106: a square wave, a bit a half-cycle (0 short, 1 long, 4:1, told apart by their ratio); bytes of a
 *     start bit, 8 data bits (least significant first) and a stop bit, no gaps, after a leader of 1s; a dump `4A 55`,
 *     64 patches of 16 bytes, a checksum, `AA AA`.
 *   - The Juno-60: two tones (1 high, about 2.35 kHz; 0 low, about 1.3 kHz), about 3.0 ms a bit; bytes of a start
 *     bit, 8 data bits and 2 stop bits after a leader of 1s; a dump `80`, then 64 records of 18 bytes: the record's
 *     number, 16 bytes, a checksum (the 18 sum to 0).
 * Both are read from the sound's zero crossings (the sound high-passed, a hysteresis at a share of its level), so a
 * level or a rate does not matter, only the ratio of lengths.
 *   new ED.junoTape.Reader(rate) -> push(Float32Array), end(), state() (what has come in so far), result()
 *   ED.junoTape.read(snd, { window, detect }) -> the result for a sound (its channels mixed); window: the seconds a
 *     leader and a dump's first bytes are looked for in (a drop: 20), the rest read only once one was found there;
 *     detect: only { machine } once found, nothing more read
 *   result: null (no tape found) or { machine: 106 | 60, dumps: [{ patches: [Uint8Array(16|18) | null], ok, why,
 *     damaged: [index] (a Juno-60's patches whose records came wrong), missing: [index] (those the sound ended before)
 *     }], ok, why ("checksum", "ended", "nopatch") } */
(function (ED) {
"use strict";

/* ---- zero crossings: a one-pole high-pass, a one-pole low-pass, a hysteresis at a share of the halves' own peaks
 * (their mean). A recorded square wave droops toward 0 between its edges, often to nearly 0 (the recording's own
 * high-pass): only an edge crosses 0 and goes on to a peak, a droop never does, so noise near 0 must not count; a
 * peak-based hysteresis keeps it out where one on the mean level would not. Each crossing is timed where the sound
 * passes half the hysteresis on its new side (an edge is steep there, a droop never gets there; rising and falling
 * alike, so a half's length is kept) ---- */
class Crossings {
    constructor(rate, hp, lp, share, onCross) {
        this.rate = rate;
        this.ah = Math.exp(-2 * Math.PI * hp / rate);
        this.al = lp ? 1 - Math.exp(-2 * Math.PI * lp / rate) : 1;
        this.share = share;
        this.kl = 1 - Math.exp(-1 / (0.002 * rate));      /* (the peak follows within a couple of ms) */
        this.rel = Math.exp(-1 / (0.02 * rate));          /* (a half's peak let go over 20 ms when no crossing comes: a
                                                           * click, or a tape played again quieter, does not leave the
                                                           * hysteresis above the sound for good) */
        this.back = new Float32Array(64);                 /* (the last 64 samples, to find where an edge passed) */
        this.x1 = 0; this.y1 = 0; this.z = 0; this.pk = 0; this.top = 0;
        this.state = 0; this.n = 0;
        this.onCross = onCross;
    }
    push(x) {
        let { x1, y1, z, pk, top, state, n } = this;
        const ah = this.ah, al = this.al, share = this.share, kl = this.kl, rel = this.rel, back = this.back;
        for (let i = 0; i < x.length; i++, n++) {
            const v = x[i];
            y1 = ah * (y1 + v - x1); x1 = v;              /* (high-pass) */
            z += al * (y1 - z);                           /* (low-pass) */
            back[n & 63] = z;
            const a = Math.abs(z);
            top = a > top ? a : top * rel;                /* (this half's peak so far) */
            pk += kl * (top - pk);                        /* (the peaks' mean, the halves' peaks counted as they grow) */
            const h = share * pk;
            if (h > 1e-5 && (state <= 0 && z > h || state >= 0 && z < -h)) {
                const s = z > 0 ? 1 : -1, q = s * h / 2;
                if (state !== 0) {
                    /* where it last passed q on its way here, interpolated */
                    let j = n;
                    while (j > n - 63 && j > 0 && (back[(j - 1) & 63] - q) * s >= 0) j--;
                    const p = back[(j - 1) & 63], c = back[j & 63];
                    this.onCross(j - 1 + (c !== p ? (q - p) / (c - p) : 0.5));
                }
                state = s;
                top = a;
            }
        }
        Object.assign(this, { x1, y1, z, pk, top, state, n });
    }
}

/* ---- a run of like halves: the leader (a few odd ones in it forgiven: noise, a tape's dropout) ---- */
class Leader {
    constructor(lo, hi, least) { this.lo = lo; this.hi = hi; this.least = least; this.reset(); }
    reset() { this.n = 0; this.m = 0; this.odd = 0; }
    add(d) {
        if (this.n && Math.abs(d - this.m) < 0.25 * this.m) {
            this.n++; this.m += (d - this.m) / Math.min(this.n, 64); this.odd *= 0.95;
        } else if (this.n > 16 && (this.odd = this.odd * 0.95 + 1) < 4) {
            return false;                                /* (an odd half: forgiven) */
        } else if (d >= this.lo && d <= this.hi) { this.n = 1; this.m = d; this.odd = 0; }
        else { this.reset(); return false; }
        return this.n >= this.least && this.m >= this.lo && this.m <= this.hi;
    }
}

/* ---- the Juno-106: a half a bit ---- */
const J106_LEN = 1029;                                    /* 4A 55, 64 x 16, the checksum, AA AA */
class J106 {
    constructor(rate) {
        const ms = rate / 1000;
        this.lead = new Leader(0.3 * ms, 1.4 * ms, 150);  /* (a 1 about 0.65 ms; a leader of about 0.4 s) */
        this.dumps = []; this.cur = null; this.found = false; this.wrong = false;
        this.seek();
    }
    seek() { this.mode = "seek"; this.lead.reset(); this.cur = null; }
    half(d) {
        if (this.mode === "seek") {
            if (this.lead.add(d)) { this.mode = "sync"; this.L = this.lead.m; this.S = this.L / 4; this.bits = []; this.bytes = []; this.oks = []; }
            return;
        }
        if (d > 3 * this.L) { this.lost(); return; }     /* (the signal gone: a half far too long) */
        const th = Math.sqrt(this.S * this.L), bit = d > th ? 1 : 0;
        if (bit) this.L += (d - this.L) / 16; else this.S += (d - this.S) / 16;
        if (this.S > this.L / 2.5) this.S = this.L / 2.5; /* (the ratio kept near 4:1) */
        if (!this.bits.length && bit) return;             /* (idle: 1s) */
        this.bits.push(bit);
        if (this.bits.length === 10) {
            const b = this.bits;
            let v = 0;
            for (let k = 0; k < 8; k++) v |= b[1 + k] << k;
            this.byte(v, b[9] === 1);
            this.bits = [];
        }
    }
    byte(v, ok) {
        if (this.mode === "sync") {
            /* (after the leader its first two bytes are 4A 55, else it is no dump) */
            this.bytes.push(v); this.oks.push(ok);
            if (this.bytes.length < 2) return;
            if (this.bytes[0] === 0x4A && v === 0x55 && this.oks[0] && ok) {
                this.mode = "dump"; this.found = true; this.wrong = false;   /* (a byte come in wrong: this dump's) */
                this.cur = { bytes: [0x4A, 0x55], oks: [true, true], patches: [], ok: false, why: "", damaged: [] };
                this.dumps.push(this.cur);
            } else this.seek();
            return;
        }
        const c = this.cur;
        c.bytes.push(v); c.oks.push(ok);
        if (!ok) this.wrong = true;
        if (c.bytes.length === J106_LEN) this.close(true);
    }
    lost() {
        if (this.mode === "dump") this.close(false);
        this.seek();
    }
    close(whole) {
        const c = this.cur, b = c.bytes;
        if (!whole) { c.ok = false; c.why = "ended"; }
        else {
            let sum = 0;
            for (let i = 0; i < 1026; i++) sum += b[i];
            const chk = sum & 255;
            c.ok = b[1026] === chk;
            c.why = c.ok ? "" : "checksum";
        }
        c.patches = [];
        for (let k = 0; k < 64; k++) c.patches.push(b.length >= 2 + 16 * k + 16 ? Uint8Array.from(b.slice(2 + 16 * k, 18 + 16 * k)) : null);
        this.seek();
    }
    end() { if (this.mode === "dump") this.close(false); }
    /* the patches whose 16 bytes came with every start and stop bit right, of the dump coming in */
    count() {
        const c = this.cur || this.dumps[this.dumps.length - 1];
        if (!c) return 0;
        let n = 0;
        for (let k = 0; k < 64; k++) {
            const a = 2 + 16 * k;
            if (c.oks.length < a + 16) break;
            if (c.oks.slice(a, a + 16).every(Boolean)) n++;
        }
        return n;
    }
}

/* ---- the Juno-60: two tones, a bit 3 ms ---- */
class J60 {
    constructor(rate) {
        const ms = rate / 1000;
        this.ms = ms;
        this.lead = new Leader(0.11 * ms, 0.45 * ms, 1500);  /* (the high tone's half about 0.21 ms; a leader of 2.6 s) */
        this.dumps = []; this.cur = null; this.found = false;
        this.h = [];                                       /* the halves since the frame's start: [start, end, tone] */
        this.seek();
    }
    seek() { this.mode = "seek"; this.lead.reset(); this.cur = null; this.h = []; this.start = -1; }
    cross(t) {
        const d = this.t0 === undefined ? 0 : t - this.t0, t0 = this.t0;
        this.t0 = t;
        if (t0 === undefined) return;
        if (this.mode === "seek") {
            if (this.lead.add(d)) {
                this.mode = "sync"; this.H = this.lead.m; this.th = this.H * 1.34; this.T = 14.1 * this.H;
                this.bytes = []; this.h = []; this.start = -1; this.after = -Infinity; this.lastStart = undefined;
            }
            return;
        }
        if (this.mode === "dump" && this.start < 0) {
            const c = this.cur, per = 11 * this.T;
            if (c.seg.length && t - c.last > 3 * per) this.record();   /* (the last record before a pause) */
            if (c.recs[63] || t - c.last > 30 * per) { this.close(); return; }   /* (the filler read, or a second without
                                                                                  * bytes: the dump is over) */
        }
        if (d > 4 * this.th && this.mode === "sync") { this.lost(); return; }
        const tone = d < this.th ? 1 : 0;
        this.h.push([t0, t, tone]);
        if (this.start < 0) {
            /* (idle: a frame starts where two low halves in a row begin) */
            const n = this.h.length;
            if (n >= 2 && !this.h[n - 1][2] && !this.h[n - 2][2] && this.h[n - 2][0] >= this.after) {
                this.start = this.h[n - 2][0];
                this.h = this.h.slice(n - 2);
            } else if (n > 64) this.h = this.h.slice(n - 2);
            return;
        }
        if (t < this.start + 10.9 * this.T) return;
        /* the frame's 11 bits read at their middles: the share of the high tone over the middle 70 % of each */
        const bits = [];
        for (let j = 0; j < 11; j++) {
            const a = this.start + (j + 0.15) * this.T, b = this.start + (j + 0.85) * this.T;
            let hi = 0, all = 0;
            for (const [s, e, tn] of this.h) {
                const o = Math.min(e, b) - Math.max(s, a);
                if (o > 0) { all += o; if (tn) hi += o; }
            }
            bits.push(all && hi / all > 0.5 ? 1 : 0);
        }
        let v = 0;
        for (let k = 0; k < 8; k++) v |= bits[1 + k] << k;
        const at = this.start;
        if (this.lastStart !== undefined) {               /* (frames back to back: the bit's length followed) */
            const per = (at - this.lastStart) / 11;
            if (Math.abs(per - this.T) < 0.06 * this.T) this.T += (per - this.T) / 8;
        }
        this.lastStart = at;
        this.after = at + 10.5 * this.T;
        this.start = -1;
        this.h = this.h.filter(x => x[1] > this.after);
        this.byte(v, bits[0] === 0 && bits[9] === 1 && bits[10] === 1, at);
    }
    byte(v, ok, at) {
        if (this.mode === "sync") {
            /* (after the leader 80, then record 00's number, 0, within its first four bytes: a click late in the leader
             * reads as a byte before them, and the leader, found once, is not there to be found again) */
            this.bytes.push([v, ok, at]);
            const n = this.bytes.length;
            if (n >= 2 && this.bytes[n - 2][0] === 0x80 && this.bytes[n - 2][1] && v === 0x00 && ok) {
                this.mode = "dump"; this.found = true;
                this.cur = { recs: [], seg: [[0, ok, at]], fails: 0, failAt: new Set(), patches: [], ok: false, why: "", damaged: [], missing: [],
                    t0: at, last: at };
                this.dumps.push(this.cur);
            } else if (n >= 4) this.seek();
            return;
        }
        /* a record's bytes come back to back, an idle frame or so between records: a gap starts the next record */
        const c = this.cur, per = 11 * this.T;
        if (at - c.last > 1.5 * per) this.record();
        c.seg.push([v, ok, at]);
        c.last = at;
    }
    /* the record the bytes since the last gap make: each byte in its place by its time (a byte lost does not move the
     * others), its number and its checksum right */
    record() {
        const c = this.cur, seg = c.seg, per = 11 * this.T;
        c.seg = [];
        if (!seg.length) return;
        const b = new Array(18).fill(-1);
        let extra = false;
        for (const [v, ok, at] of seg) {
            const i = Math.round((at - seg[0][2]) / per);
            if (i < 18 && b[i] < 0 && ok) b[i] = v; else extra = true;
        }
        let s = 0;
        for (const v of b) s += v;
        if (!extra && b.every(v => v >= 0) && !(s & 255) && b[0] < 64) c.recs[b[0]] = b.slice(1, 17);
        else {
            /* (which record failed, by its time: records 19 frames apart) */
            c.fails++;
            c.failAt.add(Math.round((seg[0][2] - c.t0) / (19 * per)));
        }
    }
    lost() { if (this.mode === "dump") this.close(); this.seek(); }
    /* the dump read: a patch whole when its sliders' record and its switches' record are; one whose record never came
     * (the sound ending first: records come a frame apart, 19 frames a record, so the last byte's time says which was
     * the last sent) is missing, not damaged; no patch whole, the dump does not read, `ended` when it was cut short */
    close() {
        const c = this.cur;
        this.record();
        const recs = [];
        for (let n = 0; n < 64; n++) recs.push(c.recs[n] || null);
        c.recs = recs;
        const lastSent = recs[63] ? 63 : Math.floor((c.last - c.t0) / (19 * 11 * this.T));
        c.patches = [];
        for (let k = 0; k < 56; k++) {
            const s = recs[k], w = recs[56 + (k >> 3)];
            if (!s || !w) { c.patches.push(null); (k > lastSent || 56 + (k >> 3) > lastSent ? c.missing : c.damaged).push(k); continue; }
            const q = new Uint8Array(18);
            q.set(s, 0); q[16] = w[2 * (k & 7)]; q[17] = w[2 * (k & 7) + 1];
            c.patches.push(q);
        }
        c.ok = c.patches.some(Boolean);
        c.why = c.ok ? "" : c.missing.length ? "ended" : "nopatch";
        delete c.seg; delete c.failAt;
        this.seek();
    }
    end() { if (this.mode === "dump") this.close(); }
    /* the sliders' records come first: those read right so far, and those that came wrong (by their place in time; a
     * switches' record or what follows the dump not counted); once the dump is in, its patches */
    count() {
        const c = this.cur;
        if (!c) { const d = this.dumps[this.dumps.length - 1]; return d ? { good: d.patches.filter(Boolean).length, bad: d.damaged.length } : { good: 0, bad: 0 }; }
        let good = 0, bad = 0;
        for (let n = 0; n < 56; n++) if (c.recs[n]) good++; else if (c.failAt.has(n)) bad++;
        return { good, bad };
    }
}

class Reader {
    constructor(rate) {
        this.rate = rate;
        this.j106 = new J106(rate);
        this.j60 = new J60(rate);
        this.c106 = new Crossings(rate, 20, 0, 0.35, t => { const d = t - this.p106; this.p106 = t; if (d > 0) this.j106.half(d); });
        this.c60 = new Crossings(rate, 400, 5000, 0.2, t => this.j60.cross(t));
        this.p106 = 0;
        this.frames = 0;
    }
    get machine() { return this.j106.found ? 106 : this.j60.found ? 60 : 0; }
    push(x) {
        const m = this.machine;
        if (m !== 60) this.c106.push(x);
        if (m !== 106) this.c60.push(x);
        this.frames += x.length;
    }
    end() { this.j106.end(); this.j60.end(); }
    /* what has come in: { machine, reading (a dump coming in), count, of, damaged, wrong } */
    state() {
        const m = this.machine;
        if (m === 106) return { machine: 106, reading: this.j106.mode === "dump", count: this.j106.count(), of: 64, wrong: this.j106.wrong, dumps: this.j106.dumps.length };
        if (m === 60) { const c = this.j60.count(); return { machine: 60, reading: this.j60.mode === "dump", count: c.good, of: 56, damaged: c.bad, dumps: this.j60.dumps.length }; }
        return { machine: 0 };
    }
    result() {
        const m = this.machine;
        if (!m) return null;
        const dumps = (m === 106 ? this.j106 : this.j60).dumps.map(d => ({ patches: d.patches, ok: d.ok, why: d.why, damaged: d.damaged,
            missing: d.missing || [] }));
        const bad = dumps.find(d => !d.ok);
        return { machine: m, dumps, ok: !bad, why: bad ? bad.why : "" };
    }
}

/* a sound read whole: a leader and a dump's first bytes looked for in its first `window` seconds */
function read(snd, o) {
    const rate = snd.rate, n = snd.ch[0].length, win = o && o.window ? Math.min(n, Math.round(o.window * rate)) : n;
    const rd = new Reader(rate), step = 8192;
    const mono = (a, b) => {
        if (snd.ch.length === 1) return snd.ch[0].subarray(a, b);
        const out = new Float32Array(b - a);
        for (const c of snd.ch) for (let i = a; i < b; i++) out[i - a] += c[i] / snd.ch.length;
        return out;
    };
    for (let a = 0; a < win; a += step) rd.push(mono(a, Math.min(win, a + step)));
    if (!rd.machine) return null;
    if (o && o.detect) return { machine: rd.machine };   /* (found: a drop asks before reading it all) */
    for (let a = win; a < n; a += step) rd.push(mono(a, Math.min(n, a + step)));
    rd.end();
    return rd.result();
}

ED.junoTape = { Reader, read };
})(globalThis.ED = globalThis.ED || {});

/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Juno banks and tapes made by the tests (specs/editor.md, Tests: Juno banks and tapes): the repository holds no
 * factory patches, libraries or recordings found elsewhere, so the tests write their own, from patches of their own.
 * Plain functions, no DOM: in Node (require) and in the page (its source evaluated there: globalThis.JunoSynth).
 *   patches106(n, seed) -> [Uint8Array(18)] (every field at its ends and between: sliders 0, 127 and between, every
 *     RANGE, PULSE, SAW, CHORUS, PWM SOURCE, POLARITY, VCA MODE and HPF)
 *   patches60(n, seed) -> [{ s: Uint8Array(16) (the tape's order), sw0, sw1 }]
 *   syx(msgs) / msg30(p, pp, ch) / msg31(p) / msg32(cc, vv) / dx7Voice(name): .syx bytes; lib106(name, [{ name, p }]): a
 *     Juno-106 Librarian library; pat([{ name, p }]): a .pat
 *   tape106([[64 x Uint8Array(18)]], o) / tape60([[56 patches60]], o) -> Float32Array (o: rate, level, noise, seed, hp the
 *     recording's high-pass in Hz, speed, gap s of silence between dumps, damage: [{ dump, byte }] (a byte of the
 *     dump's stream changed, its checksum not), cut: the share of the last dump kept)
 *   tapeBytes106(p64) -> Uint8Array(16) per patch: the tape's form of a patch; wav(x, rate) -> a 16-bit WAV's bytes */
(function (g) {
"use strict";
function lcg(seed) { let s = seed >>> 0 || 1; return () => (s = (s * 1103515245 + 12345) >>> 0) / 4294967296; }

/* ---- patches ---- */
function patches106(n, seed) {
    const r = lcg(seed || 1), out = [];
    for (let i = 0; i < n; i++) {
        const p = new Uint8Array(18);
        for (let k = 0; k < 16; k++) p[k] = i === 0 ? 0 : i === 1 ? 127 : i === 2 ? 64 : Math.floor(r() * 128);
        p[16] = [1, 2, 4][i % 3] | ((i >> 1) & 1) << 3 | ((i >> 2) & 1) << 4 | [0x20, 0x40, 0][(i >> 3) % 3];
        p[17] = (i & 1) | ((i >> 2) & 1) << 1 | ((i >> 3) & 1) << 2 | (i % 4) << 3;
        out.push(p);
    }
    return out;
}
function patches60(n, seed) {
    const r = lcg(seed || 2), out = [];
    for (let i = 0; i < n; i++) {
        const s = new Uint8Array(16);
        for (let k = 0; k < 16; k++) s[k] = i === 0 ? 0 : i === 1 ? 255 : i === 2 ? 128 : Math.floor(r() * 256);
        const sw0 = (i & 1) | (i % 3) << 1 | ((i >> 1) % 4) << 3 | ((i >> 2) & 1) << 5 | ((i >> 3) & 1) << 7;
        const ch = [1, 16, 32, 17][i % 4];                     /* (CHORUS I, II, off, I and II) */
        const sw1 = ch | ((i >> 1) & 1) << 1 | ((i >> 2) & 1) << 2 | ((i >> 4) & 1) << 3;
        out.push({ s, sw0, sw1 });
    }
    return out;
}

/* ---- files ---- */
const cat = parts => { const n = parts.reduce((a, p) => a + p.length, 0), o = new Uint8Array(n); let at = 0; for (const p of parts) { o.set(p, at); at += p.length; } return o; };
const syx = msgs => cat(msgs.map(m => Uint8Array.from(m)));
const msg30 = (p, pp, ch) => [0xF0, 0x41, 0x30, ch || 0, pp & 127, ...p, 0xF7];
const msg31 = (p, ch) => [0xF0, 0x41, 0x31, ch || 0, 0, ...p, 0xF7];
const msg32 = (cc, vv) => [0xF0, 0x41, 0x32, 0, cc, vv, 0xF7];
/* a DX7 single voice's dump (155 bytes): a plain voice, its name */
function dx7Voice(name) {
    const v = new Array(155).fill(0);
    for (let op = 0; op < 6; op++) { for (let k = 0; k < 8; k++) v[21 * op + k] = 99; v[21 * op + 16] = op === 5 ? 99 : 0; v[21 * op + 18] = 1; v[21 * op + 20] = 7; }
    for (let k = 0; k < 4; k++) v[126 + k] = 99;
    for (let k = 0; k < 4; k++) v[130 + k] = 50;
    v[134] = 31; v[136] = 1; v[137] = 35; v[144] = 24;
    const nm = String(name).padEnd(10).slice(0, 10);
    for (let k = 0; k < 10; k++) v[145 + k] = nm.charCodeAt(k);
    let s = 0;
    for (const x of v) s += x;
    return [0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B, ...v, (-s) & 0x7F, 0xF7];
}
const K106 = [71, 124, 6, 50, 46, 50, 32, 104, 34, 68, 40, 103, 61, 43, 49, 45];
function lib106(name, list) {
    const parts = [], u32 = v => [v >>> 24, (v >>> 16) & 255, (v >>> 8) & 255, v & 255];
    const str = s => { const b = new TextEncoder().encode(s); return [...u32(b.length), ...b]; };
    const u64 = () => [0, 0, 1, 0x8E, 5, 0xD1, 0xFA, 0x7B];
    parts.push([0x21, 0x6A, 0x31, 0x30, 0x36, 0x5C], str("Library"), u32(3), u32(1), str(name), u32(0), u32(list.length), u64(), u32(0), u32(0x5820CDF8));
    list.forEach((x, n) => {
        const vals = [];
        for (let i = 0; i < 20; i++) { const v = (i < 18 ? x.p[i] : 0) * K106[(i + 9 * n) % 16]; vals.push(v >> 8, v & 255); }
        parts.push(str(x.name), str("952708a4-5618-4734-bf4a-3706e090273e"), u64(), vals, u32(0xC6CBD5AF));
    });
    parts.push(u32(0x12345678));
    return cat(parts.map(p => Uint8Array.from(p)));
}
const hx = v => (v < 16 ? "0" : "") + v.toString(16).toUpperCase();
const pat = list => new TextEncoder().encode(list.map(x => Array.from(x.p, hx).join(" ") + "," + x.name).join("\r\n") + "\r\n");

/* ---- tapes ---- */
/* a patch in the Juno-106 tape's 16 bytes: the sliders' 7 bits, sw1's bits 0-6 on bytes 0-6's top bits, sw2's 0-4 on
 * bytes 8-12's */
function tapeBytes106(p) {
    const b = new Uint8Array(16);
    for (let i = 0; i < 16; i++) b[i] = p[i] & 127;
    for (let i = 0; i < 7; i++) b[i] |= ((p[16] >> i) & 1) << 7;
    for (let i = 0; i < 5; i++) b[8 + i] |= ((p[17] >> i) & 1) << 7;
    return b;
}
/* the recording: a level a sample (its edges at their own times, within a sample linear), high-passed (the recording's
 * droop), low-passed at 9 kHz, at a level, with noise */
function render(segs, o) {
    const rate = o.rate || 44100, speed = o.speed || 1;
    let tot = 0;
    for (const [dur] of segs) tot += dur / speed;
    const n = Math.ceil(tot * rate) + 16, x = new Float32Array(n);
    let t = 0, i = 0;
    for (const [dur, v, f] of segs) {
        const end = t + dur / speed;
        if (f) {                                             /* (a tone: phase kept from the last one) */
            for (; i < n && i / rate < end; i++) { render.ph += 2 * Math.PI * f * speed / rate; x[i] = Math.sin(render.ph); }
        } else for (; i < n && i / rate < end; i++) x[i] = v;
        t = end;
    }
    const hp = Math.exp(-2 * Math.PI * (o.hp == null ? 300 : o.hp) / rate), lp = 1 - Math.exp(-2 * Math.PI * 9000 / rate);
    const r = lcg(o.seed || 3), lvl = o.level == null ? 0.5 : o.level, noise = o.noise || 0;
    let x1 = 0, y = 0, z = 0;
    for (let k = 0; k < n; k++) {
        y = hp * (y + x[k] - x1); x1 = x[k];
        z += lp * (y - z);
        x[k] = lvl * z + noise * (2 * r() - 1);
    }
    return x;
}
render.ph = 0;
function stream106(bytes, segs) {
    const half = b => segs.push([b ? 0.00063 : 0.000165, segs.lvl = -segs.lvl]);
    for (let k = 0; k < 615; k++) half(1);                   /* (the leader: about 0.4 s of 1s) */
    for (const v of bytes) { half(0); for (let k = 0; k < 8; k++) half((v >> k) & 1); half(1); }
    for (let k = 0; k < 64; k++) half(1);
}
function dump106(p64) {
    const b = [0x4A, 0x55];
    for (const p of p64) b.push(...tapeBytes106(p));
    let s = 0;
    for (const v of b) s += v;
    b.push(s & 255, 0xAA, 0xAA);
    return b;
}
function tape106(dumps, o) {
    o = o || {};
    const segs = [];
    segs.lvl = 1;
    segs.push([0.3, 0]);
    dumps.forEach((p64, di) => {
        let b = dump106(p64);
        for (const d of o.damage || []) if (d.dump === di) b[d.byte] ^= 0x10;
        if (o.cut && di === dumps.length - 1) b = b.slice(0, Math.floor(b.length * o.cut));
        stream106(b, segs);
        segs.push([o.gap == null ? 1 : o.gap, 0]);
    });
    return render(segs, o);
}
function tape60(dumps, o) {
    o = o || {};
    const T = 0.003, segs = [], bit = b => segs.push([T, 0, b ? 2350 : 1300]);
    const frame = v => { bit(0); for (let k = 0; k < 8; k++) bit((v >> k) & 1); bit(1); bit(1); };
    const idle = () => { for (let k = 0; k < 11; k++) bit(1); };
    render.ph = 0;
    segs.push([0.3, 0]);
    dumps.forEach((list, di) => {
        const recs = [];
        for (let n = 0; n < 64; n++) {
            let body;
            if (n < 56) body = Array.from(list[n].s);
            else if (n < 63) { body = []; for (let k = 0; k < 8; k++) { const q = list[8 * (n - 56) + k]; body.push(q.sw0, q.sw1); } }
            else body = new Array(16).fill(0x55);
            let s = n;
            for (const v of body) s += v;
            recs.push([n, ...body, (-s) & 255]);
        }
        for (let k = 0; k < 2600 / 3; k++) bit(1);           /* (the leader: about 2.6 s of 1s) */
        frame(0x80); idle();
        const flat = recs.flat();
        for (const d of o.damage || []) if (d.dump === di) flat[d.byte] ^= 0x10;
        const keep = o.cut && di === dumps.length - 1 ? Math.floor(flat.length * o.cut) : flat.length;
        for (let k = 0; k < keep; k++) { frame(flat[k]); if (k % 18 === 17) idle(); }
        for (let k = 0; k < 100; k++) bit(1);
        segs.push([o.gap == null ? 1 : o.gap, 0]);
    });
    return render(segs, o);
}
function wav(x, rate) {
    const n = x.length, b = new Uint8Array(44 + 2 * n), dv = new DataView(b.buffer);
    const put = (at, s) => { for (let i = 0; i < 4; i++) b[at + i] = s.charCodeAt(i); };
    put(0, "RIFF"); dv.setUint32(4, 36 + 2 * n, true); put(8, "WAVE"); put(12, "fmt "); dv.setUint32(16, 16, true);
    dv.setUint16(20, 1, true); dv.setUint16(22, 1, true); dv.setUint32(24, rate, true); dv.setUint32(28, rate * 2, true);
    dv.setUint16(32, 2, true); dv.setUint16(34, 16, true); put(36, "data"); dv.setUint32(40, 2 * n, true);
    for (let i = 0; i < n; i++) dv.setInt16(44 + 2 * i, Math.max(-32768, Math.min(32767, Math.round(x[i] * 32767))), true);
    return b;
}

const api = { lcg, patches106, patches60, syx, msg30, msg31, msg32, dx7Voice, lib106, pat, tapeBytes106, tape106, tape60, wav };
if (typeof module !== "undefined" && module.exports) module.exports = api;
g.JunoSynth = api;
})(globalThis);

/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2019, Martin Cameron (IBXM, upstream 83b5d9a: its module loaders)
 * Copyright (C) 2026 Vasily Kazantsev (this JavaScript port), BSD-3-Clause as IBXM: ../../fw/core/play/ibxm/LICENSE-ibxm.txt */
/* IBXM's module loaders (MOD, S3M, XM) written again in JavaScript for the editor's converter (conv_mod.js), so a
 * module is read exactly as IBXM reads it: the same keys, instruments, volumes, effects (S3M's at 0x80 + their
 * letter), samples, loops, relative notes and finetunes, IBXM's fixed-point arithmetic included. Differences, all
 * in what is kept, not in what is read: samples stay at their own depth (IBXM widens 8-bit ones to 16), XM's
 * ping-pong loops are not unrolled (PT/FM's sampler plays them), XM's envelopes are kept only as their points.
 *   ED.ibxm.loadModule(bytes) -> { kind, name, channels, c2Rate, gain, gvol, speed, tempo, linear, fastVol, restart,
 *     panning[], sequence[], patterns: [{ rows, channels, data: 5 B a note }], instruments: [{ name, samples,
 *     keyToSample, volEnv, fadeout, vibDepth }], warnings } */
(function (ED) {
"use strict";

/* ---- IBXM's fixed point ---- */
const FP_SHIFT = 15, FP_ONE = 32768, FP_MASK = 32767;
const EXP2 = [32768, 32946, 33125, 33305, 33486, 33667, 33850, 34034, 34219, 34405, 34591, 34779, 34968, 35158, 35349, 35541,
    35734, 35928, 36123, 36319, 36516, 36715, 36914, 37114, 37316, 37518, 37722, 37927, 38133, 38340, 38548, 38757,
    38968, 39180, 39392, 39606, 39821, 40037, 40255, 40473, 40693, 40914, 41136, 41360, 41584, 41810, 42037, 42265,
    42495, 42726, 42958, 43191, 43425, 43661, 43898, 44137, 44376, 44617, 44859, 45103, 45348, 45594, 45842, 46091,
    46341, 46593, 46846, 47100, 47356, 47613, 47871, 48131, 48393, 48655, 48920, 49185, 49452, 49721, 49991, 50262,
    50535, 50810, 51085, 51363, 51642, 51922, 52204, 52488, 52773, 53059, 53347, 53637, 53928, 54221, 54515, 54811,
    55109, 55408, 55709, 56012, 56316, 56622, 56929, 57238, 57549, 57861, 58176, 58491, 58809, 59128, 59449, 59772,
    60097, 60423, 60751, 61081, 61413, 61746, 62081, 62419, 62757, 63098, 63441, 63785, 64132, 64480, 64830, 65182, 65536];
function exp2(x) {
    const x0 = (x & FP_MASK) >> (FP_SHIFT - 7), c = EXP2[x0], m = EXP2[x0 + 1] - c;
    const y = ((m * (x & (FP_MASK >> 7))) >> 8) + c;
    return (y << FP_SHIFT) >> ((FP_SHIFT - (x >> FP_SHIFT)) & 31);
}
function log2(x) {
    let y = 16 << FP_SHIFT;
    for (let step = y; step > 0; step >>= 1) if (exp2(y - step) >= x) y -= step;
    return y;
}

/* ---- reading ---- */
class Data {
    constructor(b) { this.b = b; this.length = b.length; }
    u8(o) { return o < this.length ? this.b[o] : 0; }
    s8(o) { const v = this.u8(o); return v > 127 ? v - 256 : v; }
    u16le(o) { return o + 1 < this.length ? this.b[o] | this.b[o + 1] << 8 : 0; }
    u16be(o) { return o + 1 < this.length ? this.b[o] << 8 | this.b[o + 1] : 0; }
    u32le(o) { return o + 3 < this.length ? (this.b[o] | this.b[o + 1] << 8 | this.b[o + 2] << 16 | this.b[o + 3] << 24) >>> 0 : 0; }
    ascii(o, n) { let s = ""; for (let i = 0; i < n && o + i < this.length; i++) { const c = this.b[o + i]; s += c > 32 ? String.fromCharCode(c) : " "; } return s.replace(/ +$/, ""); }
    sam8(o, n) {                                    /* signed 8-bit, as 8-bit values */
        const out = new Int16Array(n);
        for (let i = 0; i < n; i++) { const v = this.u8(o + i); out[i] = v > 127 ? v - 256 : v; }
        return out;
    }
    sam16(o, n) {
        const out = new Int16Array(n);
        for (let i = 0; i < n; i++) { const v = this.u16le(o + 2 * i); out[i] = v > 32767 ? v - 65536 : v; }
        return out;
    }
}
const newSample = () => ({ name: "", data: new Int16Array(0), bits: 8, loopStart: 0, loopLength: 0, pingPong: false, volume: 0,
    panning: 0, relNote: 0, fineTune: 0 });
const newInstrument = () => ({ name: "", samples: [newSample()], keyToSample: new Uint8Array(97), volEnv: null, fadeout: 0, vibDepth: 0 });

function loadXm(d, warn) {
    if (d.u16le(58) !== 0x0104) throw new Error("XM format version must be 0x0104");
    const m = { kind: "XM", name: d.ascii(17, 20), c2Rate: 8363, gain: 64, gvol: 64 };
    const deltaEnv = d.ascii(38, 15) === "DigiBooster Pro";
    let offset = 60 + d.u32le(60);
    m.sequenceLen = d.u16le(64); m.restart = d.u16le(66); m.channels = d.u16le(68);
    const numPatterns = d.u16le(70), numInstruments = d.u16le(72);
    m.linear = d.u16le(74) & 1; m.speed = d.u16le(76); m.tempo = d.u16le(78); m.fastVol = 0;
    m.panning = new Array(m.channels).fill(128);
    m.sequence = [];
    for (let i = 0; i < m.sequenceLen; i++) { const e = d.u8(80 + i); m.sequence.push(e < numPatterns ? e : 0); }
    m.patterns = [];
    for (let p = 0; p < numPatterns; p++) {
        if (d.u8(offset + 4)) throw new Error("unknown pattern packing type");
        const rows = Math.max(1, d.u16le(offset + 5)), len = d.u16le(offset + 7);
        offset += d.u32le(offset);
        const next = offset + len, n = rows * m.channels, data = new Uint8Array(n * 5);
        if (len > 0) for (let i = 0; i < n; i++) {
            let flags = d.u8(offset);
            if (!(flags & 0x80)) flags = 0x1F; else offset++;
            const key = flags & 1 ? d.u8(offset++) : 0, ins = flags & 2 ? d.u8(offset++) : 0, vol = flags & 4 ? d.u8(offset++) : 0;
            let fx = flags & 8 ? d.u8(offset++) : 0, fp = flags & 16 ? d.u8(offset++) : 0;
            if (fx >= 0x40) fx = fp = 0;
            data.set([key, ins, vol, fx, fp], i * 5);
        }
        m.patterns.push({ rows, channels: m.channels, data });
        offset = next;
    }
    m.instruments = [newInstrument()];
    for (let i = 1; i <= numInstruments; i++) {
        const ins = newInstrument();
        ins.name = d.ascii(offset + 4, 22);
        const ns = d.u16le(offset + 27);
        ins.samples = [];
        if (ns > 0) {
            for (let k = 0; k < 96; k++) { const s = d.u8(offset + 33 + k); ins.keyToSample[k + 1] = s < ns ? s : 0; }
            const env = d.u8(offset + 233) & 1 && d.u8(offset + 225) > 0 && d.u8(offset + 225) <= 12;
            if (env) {
                const pts = [];
                let tick = 0;
                for (let p = 0; p < d.u8(offset + 225); p++) { tick = (deltaEnv ? tick : 0) + d.u16le(offset + 129 + p * 4); pts.push([tick, d.u16le(offset + 131 + p * 4)]); }
                ins.volEnv = { points: pts, sustain: d.u8(offset + 233) & 2 ? d.u8(offset + 227) & 15 : -1 };
            }
            ins.vibDepth = d.u8(offset + 237) & 0x7F;
            ins.fadeout = d.u16le(offset + 239);
        }
        offset += d.u32le(offset);
        let head = offset;
        offset += ns * 40;
        for (let s = 0; s < ns; s++) {
            const sm = newSample();
            const bytes = d.u32le(head);
            let ls = d.u32le(head + 4), ll = d.u32le(head + 8);
            sm.volume = d.u8(head + 12); sm.fineTune = d.s8(head + 13);
            const t = d.u8(head + 14), looped = (t & 3) > 0, sixteen = (t & 0x10) > 0;
            sm.pingPong = (t & 2) > 0;
            sm.panning = d.u8(head + 15) + 1;
            sm.relNote = d.s8(head + 16);
            sm.name = d.ascii(head + 18, 22);
            head += 40;
            let n = bytes;
            if (sixteen) { n >>= 1; ls >>= 1; ll >>= 1; }
            if (!looped || ls + ll > n) { ls = n; ll = 0; }
            sm.loopStart = ls; sm.loopLength = ll;
            sm.bits = sixteen ? 16 : 8;
            const raw = sixteen ? d.sam16(offset, n) : d.sam8(offset, n);
            /* the delta coding undone, wrapping at the sample's depth */
            let amp = 0;
            for (let k = 0; k < n; k++) {
                amp += raw[k];
                if (sixteen) amp = ((amp + 32768) & 0xFFFF) - 32768; else amp = ((amp + 128) & 0xFF) - 128;
                raw[k] = amp;
            }
            sm.data = raw;
            ins.samples.push(sm);
            offset += bytes;
        }
        if (!ins.samples.length) ins.samples.push(newSample());
        if (ins.volEnv) warn("XM instrument " + i + " " + ins.name + ": its volume envelope is left out (the sampler's own envelope plays)");
        if (ins.vibDepth) warn("XM instrument " + i + " " + ins.name + ": its auto-vibrato is left out");
        m.instruments.push(ins);
    }
    return m;
}

function loadS3m(d) {
    if (d.u32le(44) !== 0x4d524353) throw new Error("not an S3M file");
    const m = { kind: "S3M", name: d.ascii(0, 28), c2Rate: 8363, restart: 0, linear: 0 };
    m.sequenceLen = d.u16le(32);
    const numInstruments = d.u16le(34), numPatterns = d.u16le(36), flags = d.u16le(38), version = d.u16le(40);
    m.fastVol = (flags & 0x40) === 0x40 || version === 0x1300 ? 1 : 0;
    const signed = d.u16le(42) === 1;
    m.gvol = d.u8(48); m.speed = d.u8(49); m.tempo = d.u8(50); m.gain = d.u8(51) & 0x7F;
    const stereo = (d.u8(51) & 0x80) === 0x80, defaultPan = d.u8(53) === 0xFC;
    const map = [];
    m.channels = 0;
    for (let i = 0; i < 32; i++) { map[i] = -1; if (d.u8(64 + i) < 16) map[i] = m.channels++; }
    let idx = 96 + m.sequenceLen;
    m.sequence = [];
    for (let i = 0; i < m.sequenceLen; i++) m.sequence.push(d.u8(96 + i));
    while (m.sequence.length && m.sequence[m.sequence.length - 1] >= numPatterns) m.sequence.pop();
    m.sequenceLen = m.sequence.length;
    m.instruments = [newInstrument()];
    for (let i = 1; i <= numInstruments; i++) {
        const ins = newInstrument(), sm = ins.samples[0];
        const at = d.u16le(idx) << 4;
        idx += 2;
        ins.name = d.ascii(at + 48, 28);
        if (d.u8(at) === 1 && d.u16le(at + 76) === 0x4353) {
            const off = (d.u8(at + 13) << 20) + (d.u16le(at + 14) << 4), len = d.u32le(at + 16);
            let ls = d.u32le(at + 20), ll = d.u32le(at + 24) - ls;
            sm.volume = d.u8(at + 28);
            if (d.u8(at + 30) !== 0) throw new Error("packed S3M samples are not read");
            if (ls + ll > len) ll = len - ls;
            if (ll < 1 || !(d.u8(at + 31) & 1)) { ls = len; ll = 0; }
            sm.loopStart = ls; sm.loopLength = ll;
            const sixteen = (d.u8(at + 31) & 4) !== 0;
            const tune = (log2(d.u32le(at + 32)) - log2(m.c2Rate)) * 12;
            sm.relNote = tune >> FP_SHIFT;
            sm.fineTune = (tune & FP_MASK) >> (FP_SHIFT - 7);
            sm.bits = sixteen ? 16 : 8;
            const data = sixteen ? d.sam16(off, len) : d.sam8(off, len);
            if (!signed) for (let k = 0; k < len; k++) data[k] = sixteen ? ((data[k] & 0xFFFF) - 32768) : (((data[k] & 0xFF) ^ 0x80) << 24 >> 24);
            sm.data = data;
            sm.name = ins.name;
        }
        m.instruments.push(ins);
    }
    m.patterns = [];
    for (let p = 0; p < numPatterns; p++) {
        const data = new Uint8Array(m.channels * 64 * 5);
        let po = (d.u16le(idx) << 4) + 2, row = 0;
        while (row < 64) {
            const tok = d.u8(po++);
            if (!tok) { row++; continue; }
            let key = 0, ins = 0, vol = 0, fx = 0, fp = 0;
            if (tok & 0x20) {
                key = d.u8(po++); ins = d.u8(po++);
                if (key < 0xFE) key = (key >> 4) * 12 + (key & 15) + 1; else if (key === 0xFF) key = 0;
            }
            if (tok & 0x40) { vol = (d.u8(po++) & 0x7F) + 0x10; if (vol > 0x50) vol = 0; }
            if (tok & 0x80) {
                fx = d.u8(po++); fp = d.u8(po++);
                if (fx < 1 || fx >= 0x40) fx = fp = 0; else fx += 0x80;
            }
            const ch = map[tok & 0x1F];
            if (ch >= 0) data.set([key, ins, vol, fx, fp], (row * m.channels + ch) * 5);
            if (po > d.length) break;
        }
        m.patterns.push({ rows: 64, channels: m.channels, data });
        idx += 2;
    }
    m.panning = [];
    for (let ch = 0; ch < 32; ch++) {
        if (map[ch] < 0) continue;
        let pan = 7;
        if (stereo) pan = d.u8(64 + ch) < 8 ? 3 : 12;
        if (defaultPan) { const f = d.u8(idx + ch); if ((f & 0x20) === 0x20) pan = f & 15; }
        m.panning[map[ch]] = pan * 17;
    }
    return m;
}

function loadMod(d) {
    const m = { kind: "MOD", name: d.ascii(0, 20), gvol: 64, speed: 6, tempo: 125, linear: 0, fastVol: 0 };
    m.sequenceLen = d.u8(950) & 0x7F;
    m.restart = d.u8(951) & 0x7F;
    if (m.restart >= m.sequenceLen) m.restart = 0;
    m.sequence = [];
    let numPatterns = 0;
    for (let i = 0; i < 128; i++) { const p = d.u8(952 + i) & 0x7F; m.sequence.push(p); if (p >= numPatterns) numPatterns = p + 1; }
    m.sequence.length = m.sequenceLen;
    switch (d.u16be(1082)) {
        case 0x4b2e: case 0x4b21: case 0x5434: m.channels = 4; m.c2Rate = 8287; m.gain = 64; break;
        case 0x484e: m.channels = d.u8(1080) - 48; m.c2Rate = 8363; m.gain = 32; break;
        case 0x4348: m.channels = (d.u8(1080) - 48) * 10 + d.u8(1081) - 48; m.c2Rate = 8363; m.gain = 32; break;
        default: throw new Error("MOD format not recognised");
    }
    m.panning = [];
    for (let i = 0; i < m.channels; i++) m.panning.push((i & 3) === 1 || (i & 3) === 2 ? 204 : 51);
    let at = 1084;
    m.patterns = [];
    for (let p = 0; p < numPatterns; p++) {
        const data = new Uint8Array(m.channels * 64 * 5);
        for (let i = 0; i < data.length; i += 5) {
            const period = ((d.u8(at) & 15) << 8 | d.u8(at + 1)) * 4;
            if (period >= 112 && period <= 6848) {
                let key = -12 * log2(((period << FP_SHIFT) / 29021) | 0);
                key = (key + (key & (FP_ONE >> 1))) >> FP_SHIFT;
                data[i] = key;
            }
            data[i + 1] = ((d.u8(at + 2) & 0xF0) >> 4) | (d.u8(at) & 0x10);
            let fx = d.u8(at + 2) & 15, fp = d.u8(at + 3);
            if (fp === 0 && (fx < 3 || fx === 0xA)) fx = 0;
            if (fp === 0 && (fx === 5 || fx === 6)) fx -= 2;
            if (fx === 8) {
                if (m.channels === 4) fx = fp = 0;
                else if (fp > 128) fp = 128;
                else fp = (fp * 255) >> 7;
            }
            data[i + 3] = fx; data[i + 4] = fp;
            at += 4;
        }
        m.patterns.push({ rows: 64, channels: m.channels, data });
    }
    m.instruments = [newInstrument()];
    for (let i = 1; i <= 31; i++) {
        const ins = newInstrument(), sm = ins.samples[0];
        ins.name = sm.name = d.ascii(i * 30 - 10, 22);
        const len = d.u16be(i * 30 + 12) * 2;
        const ft = (d.u8(i * 30 + 14) & 15) << 4;
        sm.fineTune = (ft & 0x7F) - (ft & 0x80);
        sm.volume = Math.min(64, d.u8(i * 30 + 15) & 0x7F);
        let ls = d.u16be(i * 30 + 16) * 2, ll = d.u16be(i * 30 + 18) * 2;
        if (ls + ll > len) { if (ls / 2 + ll <= len) ls = ls / 2; else ll = len - ls; }
        if (ll < 4) { ls = len; ll = 0; }
        sm.loopStart = ls; sm.loopLength = ll;
        sm.data = d.sam8(at, len);
        at += len;
        m.instruments.push(ins);
    }
    return m;
}

function loadModule(b) {
    const d = new Data(b), warns = [];
    const warn = w => warns.push(w);
    let m;
    if (d.ascii(0, 16) === "Extended Module:") m = loadXm(d, warn);
    else if (d.ascii(44, 4) === "SCRM") m = loadS3m(d);
    else m = loadMod(d);
    if (m.channels < 1 || !m.patterns.length || m.sequenceLen < 1) throw new Error("an invalid module file");
    m.warnings = warns;
    return m;
}

ED.ibxm = { loadModule, exp2, log2 };
})(globalThis.ED = globalThis.ED || {});

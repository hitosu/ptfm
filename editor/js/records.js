/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM's records both ways, byte for byte as specs/files/ say (the editor's; tools/ptfm_records.py is the
 * packer's, and tests/editor keeps the two equal). Decoded objects keep the bytes they came from (`raw`), and
 * coding starts from them, so spare bits and fields the editor does not know travel unchanged.
 *
 *   ED.rec.KIND, NAME_CHARS, name / packName / unpackName
 *   cartridges: unpackCart(kind, bytes) -> { name, slots: [slot | null] }, packCart(kind, cart)
 *     slots by kind: DX7 { name, level, ops: [6 x {field}], voice: {field} } (OP6 first, as Yamaha's packed voice),
 *     Juno-106 / TB-303 / FUSION { name, level, ...fields }
 *   samplers: unpackSampler(bytes) -> { name, instruments: [inst | null], samples: [entry | null] }, packSampler
 *   PTR: ptrEncode(Int16Array|number[] samples, bits) -> Uint8Array, ptrDecode(bytes) -> { bits, samples }
 *   patterns: unpackPattern([part payloads]) -> { tracks, rows, step, cells: Map("row,track" -> cell) },
 *     packPattern(p) -> [part payloads]; a cell { note, ins, vol, cmds: [[code, value]] } (note 0 none, 1 off;
 *     ins -1 none, else cart << 8 | slot; vol -1 none)
 *   songs: unpackSong(bytes), packSong(song); the FX setup stays its 64 bytes (fx helpers below)
 *   settings: unpackSettings(bytes), packSettings(s); MIDI map: unpackMidiMap, packMidiMap
 *   the loops: unpackLoops([parts]) -> { the setup, loops: [{ instrument, ..., cells }] }, packLoops(L) -> [parts]
 *   older versions: upgrade(records) -> the records, each object of an older version converted (the loops' 3 -> 4)
 *   newer versions, for WRITE to an FM-1 that reads an older one: downgrade(an object's records, version) -> { recs,
 *     lost } or null; canDowngrade(kind, from, to) (the loops' 4 -> 3; the editor only, not the Python twin) */
(function (ED) {
"use strict";

const KIND = { DX7: 0x01, JUNO: 0x02, TB303: 0x03, SAMPLER: 0x04, FUSION: 0x05, CHIP: 0x06, AUDIO: 0x10, PATTERN: 0x20,
    SONG: 0x21, LOOPS: 0x22, DJ: 0x23, SETTINGS: 0x30, MIDIMAP: 0x31, NOTES: 0xE0 };
const VERSION = { 0x01: 1, 0x02: 1, 0x03: 1, 0x04: 1, 0x05: 1, 0x06: 1, 0x10: 1, 0x20: 1, 0x21: 1, 0x22: 4, 0x23: 1, 0x30: 1,
    0x31: 1, 0xE0: 1 };
const CART_KINDS = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06];
const RECORD_MAX = 4080;
const NAME_CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:.-?|/\\*#$@%";
const NAME_EMPTY = 63;
const NOTE_OFF = 1;

/* ---- names ---- */
function nameCodes(name, width) {
    const s = String(name || "").toUpperCase();
    const codes = [];
    for (let i = 0; i < width; i++) {
        const c = i < s.length ? NAME_CHARS.indexOf(s[i]) : 0;
        codes.push(c < 0 ? 0 : c);                  /* (a character outside the set: a space) */
    }
    return codes;
}
function nameText(codes) {
    if (codes.length && codes[0] === NAME_EMPTY) return null;
    return codes.map(c => c < NAME_CHARS.length ? NAME_CHARS[c] : " ").join("").replace(/ +$/, "");
}
/* a name typed by the user, as the FM-1 can show it: capitals and the names' signs, at most width */
function cleanName(s, width) {
    let o = "";
    for (const ch of String(s || "").toUpperCase()) { o += NAME_CHARS.includes(ch) ? ch : " "; if (o.length >= width) break; }
    return o.replace(/ +$/, "");
}

/* ---- bits, from bit 0 of the first byte up, each value least significant bit first ---- */
class BitWriter {
    constructor(bytes) { this.b = bytes; this.n = 0; }
    put(v, bits) {
        for (let i = 0; i < bits; i++, this.n++) {
            const byte = this.n >> 3, bit = this.n & 7;
            if ((v >>> i) & 1) this.b[byte] |= 1 << bit; else this.b[byte] &= ~(1 << bit);
        }
    }
    putSigned(v, bits) { this.put(v & ((1 << bits) - 1), bits); }
    putName(name, width) { for (const c of nameCodes(name, width)) this.put(c, 6); }
    skip(bits) { this.n += bits; }
}
class BitReader {
    constructor(bytes) { this.b = bytes; this.n = 0; }
    get(bits) {
        let v = 0;
        for (let i = 0; i < bits; i++, this.n++) v |= ((this.b[this.n >> 3] >> (this.n & 7)) & 1) << i;
        return v >>> 0;
    }
    getSigned(bits) { const v = this.get(bits); return v >> (bits - 1) ? v - (1 << bits) : v; }
    getName(width) { const c = []; for (let i = 0; i < width; i++) c.push(this.get(6)); return nameText(c); }
    skip(bits) { this.n += bits; }
}
function packName(name, width) {
    const b = new Uint8Array(width * 6 / 8);
    new BitWriter(b).putName(name, width);
    return b;
}
function unpackName(b, width) { return new BitReader(b).getName(width) || ""; }

/* ---- cartridges (instruments.md) ---- */
const DX7_OP = [["r1", 7], ["r2", 7], ["r3", 7], ["r4", 7], ["l1", 7], ["l2", 7], ["l3", 7], ["l4", 7],
    ["break_point", 7], ["left_depth", 7], ["right_depth", 7], ["left_curve", 2], ["right_curve", 2],
    ["rate_scaling", 3], ["amp_mod_sens", 2], ["key_vel_sens", 3], ["output_level", 7], ["mode", 1],
    ["coarse", 5], ["fine", 7], ["detune", 4]];
const DX7_VOICE = [["pr1", 7], ["pr2", 7], ["pr3", 7], ["pr4", 7], ["pl1", 7], ["pl2", 7], ["pl3", 7], ["pl4", 7],
    ["algorithm", 5], ["feedback", 3], ["osc_key_sync", 1], ["lfo_speed", 7], ["lfo_delay", 7],
    ["lfo_pm_depth", 7], ["lfo_am_depth", 7], ["lfo_key_sync", 1], ["lfo_wave", 3],
    ["pitch_mod_sens", 3], ["transpose", 6]];
const JUNO = [["lfo_rate", 7], ["lfo_delay", 7], ["dco_lfo", 7], ["dco_pwm", 7], ["dco_noise", 7], ["vcf_freq", 7],
    ["vcf_res", 7], ["vcf_env", 7], ["vcf_lfo", 7], ["vcf_kybd", 7], ["vca_level", 7], ["env_a", 7],
    ["env_d", 7], ["env_s", 7], ["env_r", 7], ["dco_sub", 7], ["range", 2], ["pulse", 1], ["saw", 1],
    ["chorus", 2], ["pwm_source", 1], ["env_polarity", 1], ["vca_mode", 1], ["hpf", 2]];
const TB303 = [["wave", 7], ["cutoff", 7], ["resonance", 7], ["env_mod", 7], ["decay", 7], ["accent", 7]];
const FUSION = [["pattern", 8]];
/* a CHIP instrument's fields in the slot's order (specs/files/instruments.md): a table's SPEED and the vibrato's
 * SPEED as stored (0-3 for X1-X4), its DELAY and DEPTH halved (0-31) */
const CHIP = [["osc", 3], ["duty", 2], ["bz_shape", 2], ["bz_steps", 1], ["bz_gate", 1], ["n_mix", 1], ["n_short", 1],
    ["n_rate", 4], ["n_key", 1], ["clock", 3], ["curve", 2], ["filter", 2], ["fps50", 1], ["sweep", 2], ["sw_shift", 3],
    ["sw_pace", 3], ["vol", 4], ["env", 2], ["env_step", 3], ["release", 4], ["psg_shape", 4], ["psg_period", 16],
    ["wave", 4], ["tab_vol", 6], ["spd_vol", 2], ["tab_arp", 6], ["spd_arp", 2], ["tab_pitch", 6], ["spd_pitch", 2],
    ["tab_duty", 6], ["spd_duty", 2], ["tab_noise", 6], ["spd_noise", 2], ["vib_delay", 5], ["vib_speed", 2],
    ["vib_depth", 5]];
const SAMPLER = [["sample", 6], ["start", 20], ["end", 20], ["loop_start", 20], ["loop_end", 20], ["loop", 2],
    ["base_note", 7], ["tune", 8]];
const ENV = [["r1", 7], ["r2", 7], ["r3", 7], ["r4", 7], ["l1", 7], ["l2", 7], ["l3", 7], ["l4", 7]];
/* kind: fields, slot size, spare bits after name and LEVEL, slots at most */
const SLOT = { 0x01: [null, 112, 44, 32], 0x02: [JUNO, 28, 33, 128], 0x03: [TB303, 24, 82, 128], 0x05: [FUSION, 16, 52, 64],
    0x06: [CHIP, 28, 35, 128] };
const SAMPLER_SLOT = 40, SAMPLE_ENTRY = 16, SAMPLER_MAX = 64;

function getFields(r, fields) { const o = {}; for (const [f, bits] of fields) o[f] = r.get(bits); return o; }
function putFields(w, fields, v) {
    for (const [f, bits] of fields) {
        const x = v[f] | 0;
        w.put(Math.max(0, Math.min((1 << bits) - 1, x)), bits);
    }
}

function unpackSlot(kind, b) {
    const r = new BitReader(b);
    let s;
    if (kind === KIND.DX7) {
        const ops = [];
        for (let i = 0; i < 6; i++) ops.push(getFields(r, DX7_OP));
        s = { ops, voice: getFields(r, DX7_VOICE) };
    } else s = getFields(r, SLOT[kind][0]);
    s.name = r.getName(10);
    s.level = r.getSigned(8);
    if (s.name === null) return null;
    s.raw = b.slice();
    return s;
}
function packSlot(kind, s) {
    const size = SLOT[kind][1];
    if (!s) {                                       /* empty: its name's first code 63 */
        const b = new Uint8Array(size), w = new BitWriter(b);
        w.skip(size * 8 - 60 - 8 - SLOT[kind][2]);
        w.put(NAME_EMPTY, 6);
        return b;
    }
    const b = s.raw && s.raw.length === size ? s.raw.slice() : new Uint8Array(size);
    const w = new BitWriter(b);
    if (kind === KIND.DX7) {
        for (const o of s.ops) putFields(w, DX7_OP, o);
        putFields(w, DX7_VOICE, s.voice);
    } else putFields(w, SLOT[kind][0], s);
    w.putName(s.name || "", 10);
    w.putSigned(Math.max(-128, Math.min(127, s.level | 0)), 8);
    return b;
}
function unpackCart(kind, b) {
    if (kind === KIND.SAMPLER) return unpackSampler(b);
    const size = SLOT[kind][1], n = b[9];
    if (b.length !== 10 + n * size) throw new Error("a cartridge of kind " + kind + ": " + b.length + " B for " + n + " slots");
    const slots = [];
    for (let i = 0; i < n; i++) slots.push(unpackSlot(kind, b.subarray(10 + i * size, 10 + (i + 1) * size)));
    return { name: unpackName(b.subarray(0, 9), 12), slots };
}
function packCart(kind, c) {
    if (kind === KIND.SAMPLER) return packSampler(c);
    const size = SLOT[kind][1], most = SLOT[kind][3];
    let slots = c.slots.slice();
    while (slots.length && !slots[slots.length - 1]) slots.pop();
    if (slots.length > most) throw new Error(c.name + ": " + slots.length + " slots, " + most + " at most");
    const b = new Uint8Array(10 + slots.length * size);
    b.set(packName(c.name, 12), 0);
    b[9] = slots.length;
    slots.forEach((s, i) => b.set(packSlot(kind, s), 10 + i * size));
    return b;
}

function unpackSamplerInst(b) {
    const r = new BitReader(b);
    const s = getFields(r, SAMPLER);
    s.level = r.getSigned(8);
    s.volume = r.get(7);
    s.env = getFields(r, ENV);
    s.name = r.getName(10);
    if (s.name === null) return null;
    s.raw = b.slice();
    return s;
}
function packSamplerInst(s) {
    if (!s) {
        const b = new Uint8Array(SAMPLER_SLOT), w = new BitWriter(b);
        w.skip(SAMPLER_SLOT * 8 - 86 - 60);
        w.put(NAME_EMPTY, 6);
        return b;
    }
    const b = s.raw && s.raw.length === SAMPLER_SLOT ? s.raw.slice() : new Uint8Array(SAMPLER_SLOT);
    const w = new BitWriter(b);
    putFields(w, SAMPLER, s);
    w.putSigned(Math.max(-128, Math.min(127, s.level | 0)), 8);
    w.put(Math.max(0, Math.min(127, s.volume | 0)), 7);
    putFields(w, ENV, s.env);
    w.putName(s.name || "", 10);
    return b;
}
function unpackSampleEntry(b) {
    const r = new BitReader(b);
    const e = { length: r.get(20), rate: r.get(18), coding: r.get(4), name: r.getName(10) };
    if (e.name === null) return null;
    e.raw = b.slice();
    return e;
}
function packSampleEntry(e) {
    const b = e && e.raw && e.raw.length === SAMPLE_ENTRY ? e.raw.slice() : new Uint8Array(SAMPLE_ENTRY);
    const w = new BitWriter(b);
    if (!e) { w.skip(42); w.put(NAME_EMPTY, 6); return b; }
    w.put(e.length, 20);
    w.put(e.rate, 18);
    w.put(e.coding, 4);
    w.putName(e.name || "", 10);
    return b;
}
function unpackSampler(b) {
    const ni = b[9], ns = b[10];
    if (b.length !== 11 + ni * SAMPLER_SLOT + ns * SAMPLE_ENTRY)
        throw new Error("a sampler cartridge: " + b.length + " B for " + ni + " instruments, " + ns + " samples");
    const at = 11 + ni * SAMPLER_SLOT, instruments = [], samples = [];
    for (let i = 0; i < ni; i++) instruments.push(unpackSamplerInst(b.subarray(11 + i * SAMPLER_SLOT, 11 + (i + 1) * SAMPLER_SLOT)));
    for (let i = 0; i < ns; i++) samples.push(unpackSampleEntry(b.subarray(at + i * SAMPLE_ENTRY, at + (i + 1) * SAMPLE_ENTRY)));
    return { name: unpackName(b.subarray(0, 9), 12), instruments, samples };
}
function packSampler(c) {
    if (c.instruments.length > SAMPLER_MAX || c.samples.length > SAMPLER_MAX) throw new Error(c.name + ": more than 64 instruments or samples");
    const b = new Uint8Array(11 + c.instruments.length * SAMPLER_SLOT + c.samples.length * SAMPLE_ENTRY);
    b.set(packName(c.name, 12), 0);
    b[9] = c.instruments.length;
    b[10] = c.samples.length;
    c.instruments.forEach((s, i) => b.set(packSamplerInst(s), 11 + i * SAMPLER_SLOT));
    const at = 11 + c.instruments.length * SAMPLER_SLOT;
    c.samples.forEach((e, i) => b.set(packSampleEntry(e), at + i * SAMPLE_ENTRY));
    return b;
}

/* ---- PTR (ptr.md): blocks of 256 samples, prediction order 0-3 or raw, Rice codes in partitions of 64 ---- */
const PTR_8 = 1, PTR_16 = 2, PTR_BLOCK = 256, PTR_PART = 64, PTR_RAW = 7;
class MsbWriter {
    constructor() { this.out = []; this.acc = 0; this.n = 0; }
    put(v, bits) {                                  /* bits <= 24 */
        for (let i = bits - 1; i >= 0; i--) {
            this.acc = (this.acc << 1) | ((v >>> i) & 1);
            if (++this.n === 8) { this.out.push(this.acc & 0xFF); this.acc = 0; this.n = 0; }
        }
    }
    putRice(u, k) {
        let q = Math.floor(u / (1 << k));
        while (q >= 24) { this.put(0, 24); q -= 24; }
        this.put(1, q + 1);
        if (k) this.put(u & ((1 << k) - 1), k);
    }
    finish() { if (this.n) this.put(0, 8 - this.n); return this.out; }
}
class MsbReader {
    constructor(b, at) { this.b = b; this.pos = at * 8; }
    get(bits) {
        let v = 0;
        for (let i = 0; i < bits; i++, this.pos++) v = (v << 1) | ((this.b[this.pos >> 3] >> (7 - (this.pos & 7))) & 1);
        return v >>> 0;
    }
    getRice(k) { let q = 0; while (this.get(1) === 0) q++; return q * (1 << k) + (k ? this.get(k) : 0); }
}
const predict = (o, x1, x2, x3) => o === 0 ? 0 : o === 1 ? x1 : o === 2 ? 2 * x1 - x2 : 3 * x1 - 3 * x2 + x3;
function residuals(block, order) {
    const us = [];
    for (let i = order; i < block.length; i++) {
        const x1 = i >= 1 ? block[i - 1] : 0, x2 = i >= 2 ? block[i - 2] : 0, x3 = i >= 3 ? block[i - 3] : 0;
        const e = block[i] - predict(order, x1, x2, x3);
        us.push(e >= 0 ? 2 * e : -2 * e - 1);
    }
    return us;
}
function riceCost(us, k) { let c = us.length * (1 + k); for (const u of us) c += Math.floor(u / (1 << k)); return c; }
const bitLength = v => v <= 0 ? 0 : Math.floor(Math.log2(v)) + 1;
function bestK(us, kbits, plainBits) {
    const escape = (1 << kbits) - 1;
    let best = [us.length * plainBits, escape];
    let sum = 0;
    for (const u of us) sum += u;
    const mean = Math.floor(sum / Math.max(1, us.length));
    const k0 = Math.max(0, bitLength(mean) - 2);
    for (let k = Math.max(0, k0 - 1); k <= Math.min(escape - 1, k0 + 3); k++) {
        const c = riceCost(us, k);
        if (c < best[0]) best = [c, k];
    }
    return best[1];
}
function encodeBlock(block, bits) {
    const kbits = bits === 8 ? 4 : 5, plain = bits === 8 ? 12 : 20, escape = (1 << kbits) - 1, mask = (1 << bits) - 1;
    let best = null;
    for (let order = 0; order <= Math.min(3, block.length); order++) {
        const w = new MsbWriter();
        w.put(order, 8);
        for (let i = 0; i < order; i++) w.put(block[i] & mask, bits);
        const us = residuals(block, order);
        for (let p = 0; p < us.length; p += PTR_PART) {
            const part = us.slice(p, p + PTR_PART);
            const k = bestK(part, kbits, plain);
            w.put(k, kbits);
            for (const u of part) {
                if (k === escape) {
                    if (u >= 1 << plain) throw new Error("PTR: a residual past the plain width");
                    w.put(u, plain);
                } else w.putRice(u, k);
            }
        }
        const b = w.finish();
        if (!best || b.length < best.length) best = b;
    }
    const w = new MsbWriter();
    w.put(PTR_RAW, 8);
    for (const x of block) w.put(x & mask, bits);
    const raw = w.finish();
    return raw.length < best.length ? raw : best;
}
const signed = (v, bits) => v >> (bits - 1) ? v - (1 << bits) : v;
function decodeBlock(b, at, count, bits, out, o) {
    const kbits = bits === 8 ? 4 : 5, plain = bits === 8 ? 12 : 20, escape = (1 << kbits) - 1;
    const r = new MsbReader(b, at);
    const mode = r.get(8);
    if (mode === PTR_RAW) { for (let i = 0; i < count; i++) out[o + i] = signed(r.get(bits), bits); return; }
    if (mode > 3) throw new Error("PTR: block mode " + mode);
    let n = 0;
    for (; n < Math.min(mode, count); n++) out[o + n] = signed(r.get(bits), bits);
    let k = 0, done = 0;
    for (; n < count; n++, done++) {
        if (done % PTR_PART === 0) k = r.get(kbits);
        const u = k === escape ? r.get(plain) : r.getRice(k);
        const e = (u & 1) ? -((u + 1) / 2) : u / 2;
        const x1 = n >= 1 ? out[o + n - 1] : 0, x2 = n >= 2 ? out[o + n - 2] : 0, x3 = n >= 3 ? out[o + n - 3] : 0;
        out[o + n] = e + predict(mode, x1, x2, x3);
    }
}
function ptrEncode(samples, bits) {
    const n = samples.length;
    if (n >= 1 << 24) throw new Error("PTR: a sample past 2^24 samples");
    const blocks = [];
    for (let i = 0; i < n; i += PTR_BLOCK) blocks.push(encodeBlock(Array.from(samples.slice(i, i + PTR_BLOCK)), bits));
    const nb = blocks.length, groups = Math.ceil(nb / 16), head = 4 + 4 * groups + 2 * nb;
    let total = head;
    for (const blk of blocks) { if (blk.length > 0xFFFF) throw new Error("PTR: a block past 64 KB"); total += blk.length; }
    const rec = new Uint8Array(total), dv = new DataView(rec.buffer);
    rec[0] = bits === 8 ? PTR_8 : PTR_16;
    rec[1] = n & 0xFF; rec[2] = n >> 8 & 0xFF; rec[3] = n >> 16 & 0xFF;
    let at = head;
    blocks.forEach((blk, i) => {
        if (i % 16 === 0) dv.setUint32(4 + 4 * (i / 16), at, true);
        dv.setUint16(4 + 4 * groups + 2 * i, blk.length, true);
        rec.set(blk, at);
        at += blk.length;
    });
    const back = ptrDecode(rec);
    for (let i = 0; i < n; i++) if (back.samples[i] !== samples[i]) throw new Error("PTR: the coded sample does not decode back");
    return rec;
}
function ptrInfo(rec) {
    return { bits: rec[0] === PTR_8 ? 8 : rec[0] === PTR_16 ? 16 : 0, length: rec[1] | rec[2] << 8 | rec[3] << 16 };
}
function ptrDecode(rec) {
    const info = ptrInfo(rec), n = info.length, bits = info.bits;
    if (!bits) throw new Error("PTR: coding " + rec[0]);
    const dv = new DataView(rec.buffer, rec.byteOffset, rec.byteLength);
    const nb = Math.ceil(n / PTR_BLOCK), groups = Math.ceil(nb / 16), lens = 4 + 4 * groups;
    const out = new Int16Array(n);
    for (let g = 0; g < groups; g++) {
        let at = dv.getUint32(4 + 4 * g, true);
        for (let i = g * 16; i < Math.min(nb, g * 16 + 16); i++) {
            decodeBlock(rec, at, Math.min(PTR_BLOCK, n - i * PTR_BLOCK), bits, out, i * PTR_BLOCK);
            at += dv.getUint16(lens + 2 * i, true);
        }
    }
    return { bits, samples: out };
}

/* ---- patterns (patterns.md) ---- */
const CMD_LIST = 0xFF;
function trackStream(cells) {                       /* [{row, note, ins, vol, cmds}] in row order -> bytes, ended by 0 */
    const out = [];
    let lastIns = null, prev = -1;
    for (const c of cells) {
        if (!c.note && !(c.ins >= 0) && !(c.vol >= 0) && !(c.cmds && c.cmds.length)) continue;   /* (nothing: not stored) */
        const skip = c.row - prev - 1;
        prev = c.row;
        if (skip < 0 || skip > 255) throw new Error("row " + c.row + ": a skip of " + skip);
        let head = Math.min(skip, 7);
        const body = [];
        if (c.note) {
            if (c.note !== NOTE_OFF && (c.note < 12 || c.note > 127)) throw new Error("row " + c.row + ": note " + c.note);
            head |= 0x80;
            body.push(c.note);
        }
        if (c.ins >= 0) {
            if (lastIns === c.ins) head |= 0x20;
            else if (lastIns !== null && lastIns >> 8 === c.ins >> 8) { head |= 0x40; body.push(c.ins & 0xFF); }
            else { head |= 0x60; body.push(c.ins >> 8, c.ins & 0xFF); }
            lastIns = c.ins;
        }
        if (c.vol >= 0) { head |= 0x10; body.push(Math.min(0x7F, c.vol)); }
        const cmds = c.cmds || [];
        if (cmds.length) {
            if (cmds.length > 4) throw new Error("row " + c.row + ": " + cmds.length + " commands");
            head |= 0x08;
            if (cmds.length === 1) body.push(cmds[0][0], cmds[0][1]);
            else { body.push(CMD_LIST, cmds.length); for (const [k, v] of cmds) body.push(k, v); }
        }
        if (skip >= 7) body.push(skip);
        out.push(head, ...body);
    }
    out.push(0);
    return out;
}
function readStream(b, at) {
    const cells = [];
    let lastIns = null, prev = -1;
    for (;;) {
        if (at >= b.length) throw new Error("a track's stream runs past its record");
        const head = b[at++];
        if (head === 0) return { cells, at };
        let note = 0, ins = -1, vol = -1;
        if (head & 0x80) note = b[at++];
        const mode = (head >> 5) & 3;
        if (mode === 1) ins = lastIns;
        else if (mode === 2) ins = (lastIns & 0xFF00) | b[at++];
        else if (mode === 3) { ins = b[at] << 8 | b[at + 1]; at += 2; }
        if (mode) {
            if (lastIns === null && mode !== 3) throw new Error("a cell names the track's last instrument before it has one");
            lastIns = ins;
        } else ins = -1;
        if (head & 0x10) vol = b[at++];
        const cmds = [];
        if (head & 0x08) {
            let n = 1;
            if (b[at] === CMD_LIST) { n = b[at + 1]; at += 2; }
            for (let i = 0; i < n; i++) { cmds.push([b[at], b[at + 1]]); at += 2; }
        }
        let skip = head & 7;
        if (skip === 7) skip = b[at++];
        const row = prev + 1 + skip;
        prev = row;
        cells.push({ row, note, ins, vol, cmds });
    }
}
const cellKey = (row, track) => row + "," + track;
/* 1-128 rows a pattern (patterns.md#size); a record of more (made before the rule) reads as its first 128 */
const PAT_ROWS_MAX = 128;
function unpackPattern(parts) {
    const cells = new Map();
    let head = null;
    for (const b of parts) {
        const h = [b[0], Math.min(b[1] + 1, PAT_ROWS_MAX), b[2]];
        if (!head) head = h;
        else if (h[0] !== head[0] || h[1] !== head[1] || h[2] !== head[2]) throw new Error("a pattern's parts disagree on its size");
        const mask = b[3] | b[4] << 8;
        let at = 5;
        for (let t = 0; t < 16; t++) {
            if (!(mask >> t & 1)) continue;
            const s = readStream(b, at);
            at = s.at;
            for (const c of s.cells) if (c.row < head[1] && t < head[0])
                cells.set(cellKey(c.row, t), { note: c.note, ins: c.ins, vol: c.vol, cmds: c.cmds });
        }
    }
    if (!head) throw new Error("a pattern with no parts");
    return { tracks: head[0], rows: head[1], step: head[2], cells };
}
function packPattern(p) {
    if (p.tracks < 1 || p.tracks > 16 || p.rows < 1 || p.rows > PAT_ROWS_MAX) throw new Error("a pattern of " + p.tracks + " tracks, " + p.rows + " rows");
    const streams = {};
    for (let t = 0; t < p.tracks; t++) {
        const mine = [];
        for (let r = 0; r < p.rows; r++) {
            const c = p.cells.get(cellKey(r, t));
            if (c && (c.note || c.ins >= 0 || c.vol >= 0 || (c.cmds && c.cmds.length))) mine.push(Object.assign({ row: r }, c));
        }
        if (mine.length) streams[t] = trackStream(mine);
    }
    const parts = [];
    let cur = [], curLen = 5;
    for (const t of Object.keys(streams).map(Number).sort((a, b) => a - b)) {
        if (curLen + streams[t].length > RECORD_MAX && cur.length) { parts.push(cur); cur = []; curLen = 5; }
        cur.push(t);
        curLen += streams[t].length;
    }
    if (cur.length || !parts.length) parts.push(cur);
    if (parts.length > 16) throw new Error("a pattern past 16 parts");
    return parts.map(part => {
        let n = 5;
        for (const t of part) n += streams[t].length;
        const b = new Uint8Array(n);
        let mask = 0;
        for (const t of part) mask |= 1 << t;
        b[0] = p.tracks; b[1] = p.rows - 1; b[2] = p.step; b[3] = mask & 0xFF; b[4] = mask >> 8;
        let at = 5;
        for (const t of part) { b.set(streams[t], at); at += streams[t].length; }
        return b;
    });
}

/* ---- songs (songs.md) ---- */
const MIX_FIELDS = ["level", "pan", "duck", "chorus", "delay", "reverb"];
const MIX_DEFAULT = [0x7F, 0x80, 0, 0, 0, 0];
function unpackSong(b) {
    const npos = b[20] + 1;
    let at = 21 + npos;
    const nmix = b[at];
    const mixer = [];
    for (let i = 0; i < 16; i++) mixer.push(i < nmix ? Array.from(b.subarray(at + 1 + 6 * i, at + 7 + 6 * i)) : MIX_DEFAULT.slice());
    at += 1 + 6 * nmix;
    if (b.length < at + 64) throw new Error("a song of " + b.length + " B, " + (at + 72) + " expected");
    return {
        name: unpackName(b.subarray(0, 12), 16), tempo: b[12] | b[13] << 8, ticks: b[14], rpb: b[15], swing: b[16],
        flags: b[17], loopFrom: b[18], gvol: b[19], positions: Array.from(b.subarray(21, 21 + npos)), mixer,
        fx: b.slice(at, at + 64), spare: b.slice(at + 64)
    };
}
function packSong(s) {
    const pos = s.positions;
    if (pos.length < 1 || pos.length > 256) throw new Error(s.name + ": " + pos.length + " positions");
    const mixer = s.mixer.map(m => m.slice());
    while (mixer.length && mixer[mixer.length - 1].every((v, i) => v === MIX_DEFAULT[i])) mixer.pop();
    const spare = s.spare && s.spare.length === 8 ? s.spare : new Uint8Array(8);
    const b = new Uint8Array(21 + pos.length + 1 + 6 * mixer.length + 64 + 8);
    b.set(packName(s.name, 16), 0);
    b[12] = s.tempo & 0xFF; b[13] = s.tempo >> 8;
    b[14] = s.ticks; b[15] = s.rpb; b[16] = s.swing; b[17] = s.flags; b[18] = s.loopFrom; b[19] = s.gvol;
    b[20] = pos.length - 1;
    b.set(pos, 21);
    let at = 21 + pos.length;
    b[at++] = mixer.length;
    for (const m of mixer) { b.set(m, at); at += 6; }
    b.set(s.fx, at);
    b.set(spare, at + 64);
    return b;
}
/* a song's flags (byte 17) */
const SONG_STOP = 1, SONG_LINEAR = 2, SONG_FAST_VOL = 4;
function newSong(name) {
    const fx = new Uint8Array(64);
    defaultFx(fx);
    return { name: name || "NEW SONG", tempo: 125, ticks: 6, rpb: 4, swing: 50, flags: SONG_LINEAR, loopFrom: 0, gvol: 64,
        positions: [0], mixer: Array.from({ length: 16 }, () => MIX_DEFAULT.slice()), fx, spare: new Uint8Array(8) };
}

/* ---- the FX setup's 64 bytes (songs.md#the-fx-setup) ---- */
const FX_CHAIN_DEFAULT = [0, 1, 3, 10, 4, 5, 6, 2, 7];      /* GATE FILTER LO-FI SENDS CHORUS DELAY REVERB DRIVE LIMITER */
const fxChain = fx => { const c = []; for (let i = 0; i < 12; i++) { const v = (fx[1 + (i >> 1)] >> (4 * (i & 1))) & 15; if (v === 15) break; c.push(v); } return c; };
function setFxChain(fx, chain) {
    for (let i = 0; i < 6; i++) fx[1 + i] = 0;
    for (let i = 0; i < 12; i++) { const v = i < chain.length ? chain[i] : 15; fx[1 + (i >> 1)] |= v << (4 * (i & 1)); }
}
const fxOn = fx => fx[7] | fx[8] << 8;
function setFxOn(fx, mask) { fx[7] = mask & 0xFF; fx[8] = mask >> 8 & 0xFF; }
function defaultFx(fx) {                            /* the defaults as the FX screen gives a new song */
    fx.fill(0);
    setFxChain(fx, FX_CHAIN_DEFAULT);
    setFxOn(fx, 1 << 7);                            /* the LIMITER on */
    const d = ED.defs ? ED.defs.fxDefaults() : null;
    if (d) for (const code in d) fx[16 + (code - 0x90)] = d[code];
    fx[11] = 4; fx[12] = 1; fx[13] = 60;            /* METRONOME: FOLLOW, 4 beats, BEEP, 60 % */
}

/* ---- settings (settings.md) ---- */
const SETTINGS_SIZE = 128;
function unpackSettings(b) {
    const p = new Uint8Array(SETTINGS_SIZE);
    p.set(b.subarray(0, Math.min(SETTINGS_SIZE, b.length)));
    return { raw: p, len: b.length };
}
function packSettings(s) { return s.raw.slice(0, Math.max(SETTINGS_SIZE, s.len || SETTINGS_SIZE)); }

/* ---- the MIDI map (midi.md) ---- */
function unpackMidiMap(b) {
    const n = Math.min(b[0], Math.floor((b.length - 4) / 4)), maps = [];
    for (let i = 0; i < n; i++) {
        const o = 4 + 4 * i;
        maps.push({ ch: b[o] & 15, note: (b[o] >> 4) & 3, num: b[o + 1], control: b[o + 2], type: b[o + 3] });
    }
    return { maps };
}
function packMidiMap(m) {
    const b = new Uint8Array(4 + 4 * m.maps.length);
    b[0] = m.maps.length;
    m.maps.forEach((x, i) => { const o = 4 + 4 * i; b[o] = (x.ch & 15) | (x.note & 3) << 4; b[o + 1] = x.num & 127; b[o + 2] = x.control; b[o + 3] = x.type; });
    return b;
}

/* ---- the loops (loops.md, version 4): the head (an 8-byte header, the FX setup 8-71, the next loop's mix 72-77, the
 * loop table from 80, 12 B a loop), then with loops a pattern payload whose track i is loop i's stream (lane l of row
 * r at the stream's row 4r + l), its streams spread over parts as a pattern's tracks are ----
 *   { tempo, ticks, countIn, legato (MONO LEGATO: 0 GLIDE, 1 JUMP, 2 LEGATO), swing, grid, quantize, nextMono, n, selected,
 *     length, fx (64 B), mix: [6],
 *     loops: [{ cart, slot, colour, mono, muted, lanes, base, repeat, mixer: [6], cells: Map("row,lane" -> cell) }] }
 *   (an entry's bit 6, a solo, is never read and written 0: solos are not saved)
 * fields as stored (COUNT-IN 3 and QUANTIZE 5-7, read by the FM-1 as OFF and ROW, kept); the head's and each loop's
 * spare bits travel through `head` and `raw` */
const LOOPS_HEAD = 80, LOOP_SIZE = 12, LOOPS_MAX = 16, LOOP_ROWS_MAX = 128, LOOP_STREAM_MAX = 3800;
/* a stream's gap past the skip byte's 255 rows is bridged by a cell holding only ED0 (a note delay with no note:
 * nothing), a skip of 255 after the cell before it, as often as needed (read wherever it is) */
const LOOP_FILL = [0x0E, 0xD0];
function unpackLoops(parts) {
    const b = parts[0];
    if (!b || b.length < LOOPS_HEAD) throw new Error("the loops' head is short");
    const n = b[5], top = LOOPS_HEAD + LOOP_SIZE * n;
    if (n > LOOPS_MAX || b.length < top) throw new Error("the loops: " + n + " loops in " + b.length + " B");
    const L = { tempo: b[0] | b[1] << 8, ticks: b[2] & 31, countIn: (b[2] >> 5) & 3, legato: b[4] & 0x40 ? 2 : b[2] >> 7, swing: b[3], grid: b[4] & 3,
        quantize: (b[4] >> 2) & 7, nextMono: (b[4] >> 5) & 1, n, selected: b[6], length: b[7], fx: b.slice(8, 72),
        mix: Array.from(b.subarray(72, 78)), head: b.slice(0, LOOPS_HEAD), loops: [] };
    for (let i = 0; i < n; i++) {
        const o = LOOPS_HEAD + LOOP_SIZE * i;
        L.loops.push({ cart: b[o], slot: b[o + 1], colour: b[o + 2] & 15, mono: (b[o + 2] >> 4) & 1, muted: !!(b[o + 2] & 0x20),
            lanes: (b[o + 3] & 3) + 1, base: b[o + 4] + 1, repeat: b[o + 5],
            mixer: Array.from(b.subarray(o + 6, o + 12)), cells: new Map(), raw: b.slice(o, o + LOOP_SIZE) });
    }
    if (!n) return L;
    let head = null;
    parts.forEach((p, sub) => {
        const at0 = sub ? 0 : top;
        if (p.length < at0 + 5) throw new Error("the loops' part " + sub + " is short");
        const h = [p[at0], p[at0 + 1], p[at0 + 2]];
        if (!head) head = h;
        else if (h[0] !== head[0] || h[1] !== head[1] || h[2] !== head[2]) throw new Error("the loops' parts disagree");
        const mask = p[at0 + 3] | p[at0 + 4] << 8;
        let at = at0 + 5;
        for (let i = 0; i < 16; i++) {
            if (!(mask >> i & 1)) continue;
            if (i >= n) throw new Error("a stream of loop " + i + " of " + n);
            const s = readStream(p, at), lp = L.loops[i];
            at = s.at;
            for (const c of s.cells) if (c.row >> 2 < lp.base)
                lp.cells.set(cellKey(c.row >> 2, c.row & 3), { note: c.note, ins: c.ins, vol: c.vol, cmds: c.cmds });
        }
    });
    return L;
}
const hasField = c => c && (c.note || c.ins >= 0 || c.vol >= 0 || (c.cmds && c.cmds.length));
/* a loop's cells -> its stream's bytes, ended by 0 (null: no cells, nothing stored) */
function loopStream(cells) {
    const list = [];
    for (const [k, c] of cells) {
        const [r, l] = k.split(",").map(Number);
        if (hasField(c)) list.push(Object.assign({}, c, { row: 4 * r + l }));
    }
    if (!list.length) return null;
    list.sort((a, b) => a.row - b.row);
    const out = [];
    let prev = -1;
    for (const c of list) {
        while (c.row - prev - 1 > 255) {
            prev += 256;                            /* (a skip of 255, whatever lane that row is) */
            out.push({ row: prev, note: 0, ins: -1, vol: -1, cmds: [LOOP_FILL.slice()] });
        }
        out.push(c);
        prev = c.row;
    }
    const s = trackStream(out);
    if (s.length > LOOP_STREAM_MAX) throw new Error("a loop's stream of " + s.length + " B, " + LOOP_STREAM_MAX + " at most");
    return s;
}
function packLoops(L) {
    const n = L.loops.length, top = LOOPS_HEAD + LOOP_SIZE * n;
    if (n > LOOPS_MAX) throw new Error(n + " loops, " + LOOPS_MAX + " at most");
    if (n && !(L.length >= 1 && L.length <= LOOP_ROWS_MAX)) throw new Error("the loops' length: " + L.length + " rows");
    const b = new Uint8Array(top);
    if (L.head && L.head.length === LOOPS_HEAD) b.set(L.head);
    b[0] = L.tempo & 0xFF; b[1] = L.tempo >> 8 & 0xFF;
    b[2] = (L.ticks & 31) | (L.countIn & 3) << 5 | (L.legato === 1 ? 0x80 : 0);
    b[3] = L.swing;
    b[4] = (b[4] & 0x80) | (L.grid & 3) | (L.quantize & 7) << 2 | (L.nextMono ? 0x20 : 0) | (L.legato === 2 ? 0x40 : 0);
    b[5] = n; b[6] = L.selected; b[7] = n ? L.length : 0;
    b.set(L.fx, 8);
    b.set(L.mix, 72);
    L.loops.forEach((lp, i) => {
        const o = LOOPS_HEAD + LOOP_SIZE * i, raw = lp.raw && lp.raw.length === LOOP_SIZE ? lp.raw : new Uint8Array(LOOP_SIZE);
        if (!(lp.lanes >= 1 && lp.lanes <= 4 && lp.base >= 1 && lp.base <= LOOP_ROWS_MAX && lp.repeat >= 1 && lp.repeat <= 255))
            throw new Error("loop " + hex2(i) + ": " + lp.lanes + " lanes, a base of " + lp.base + " rows, " + lp.repeat + " repeats");
        b[o] = lp.cart; b[o + 1] = lp.slot;
        b[o + 2] = (raw[2] & 0x80) | (lp.colour & 15) | (lp.mono ? 0x10 : 0) | (lp.muted ? 0x20 : 0);
        b[o + 3] = (raw[3] & 0xFC) | (lp.lanes - 1);
        b[o + 4] = lp.base - 1; b[o + 5] = lp.repeat;
        b.set(lp.mixer, o + 6);
    });
    if (!n) return [b];
    const streams = L.loops.map(lp => loopStream(lp.cells));
    const parts = [];
    let cur = [], curLen = top + 5;
    streams.forEach((s, i) => {
        if (!s) return;
        if (curLen + s.length > RECORD_MAX && (cur.length || !parts.length)) { parts.push(cur); cur = []; curLen = 5; }
        cur.push(i);
        curLen += s.length;
    });
    parts.push(cur);
    if (parts.length > 16) throw new Error("the loops past 16 parts");
    return parts.map((part, sub) => {
        const at0 = sub ? 0 : top;
        let len = at0 + 5, mask = 0;
        for (const i of part) { len += streams[i].length; mask |= 1 << i; }
        const p = new Uint8Array(len);
        if (!sub) p.set(b);
        p[at0] = n; p[at0 + 1] = L.length - 1; p[at0 + 2] = 0; p[at0 + 3] = mask & 0xFF; p[at0 + 4] = mask >> 8;
        let at = at0 + 5;
        for (const i of part) { p.set(streams[i], at); at += streams[i].length; }
        return p;
    });
}

/* ---- older versions, converted (disk.md#versions: only the PC side converts; the FM-1 reads the current ones) ---- */
const CMD_EXT = 0x0E, CMD_KEY_OFF = 0x14;
/* the loops' version 3 (loops.md, "Versions": a loop a track, one 16-track pattern) -> version 4's parts */
function loopsV3(parts) {
    const b = parts[0];
    if (!b || b.length < 72) throw new Error("the loops' header is short");
    const n = b[5];
    if (n > LOOPS_MAX || b.length < 72 + 10 * n + (n ? 5 : 0)) throw new Error("the loops (version 3): " + n + " loops in " + b.length + " B");
    const pat = n ? unpackPattern([b.subarray(72 + 10 * n)].concat(parts.slice(1))) : null;
    const length = pat ? pat.rows : 0;              /* (its first 128 rows, as any pattern's: patterns.md#size) */
    const grid = (b[4] & 3) > 2 ? 0 : b[4] & 3, q = (b[4] >> 2) & 3, c = (b[2] >> 5) & 3, sel = b[6];
    const L = { tempo: b[0] | b[1] << 8, ticks: b[2] & 31, countIn: c === 3 ? 0 : c, legato: 0, swing: b[3], grid,
        quantize: q === 1 ? 1 + grid : q === 2 ? 4 : 0,  /* STEP: ROW; 1/8: the half beat's 2, 3 or 4 ROWS; OFF: REAL TIME */
        nextMono: 0, selected: sel < n ? sel : Math.min(n, LOOPS_MAX - 1), length, fx: b.slice(8, 72), mix: MIX_DEFAULT.slice(),
        loops: [] };
    const kept = ([k, v]) => k === CMD_KEY_OFF || (k === CMD_EXT && (v & 0xF0) === 0xD0);
    for (let i = 0; i < n; i++) {
        const o = 72 + 10 * i, track = b[o + 2], cells = new Map();
        for (let r = 0; r < length; r++) {
            const x = pat.cells.get(cellKey(r, track));
            if (!x) continue;
            const y = { note: x.note, ins: -1, vol: x.vol, cmds: x.cmds.filter(kept) };
            if (hasField(y)) cells.set(cellKey(r, 0), y);
        }
        L.loops.push({ cart: b[o], slot: b[o + 1], colour: i % 12, mono: 0, muted: !!(b[o + 3] & 1), lanes: 1,
            base: length, repeat: 1, mixer: Array.from(b.subarray(o + 4, o + 10)), cells });
    }
    return packLoops(L);
}
const CONVERT = { 0x22: { 3: loopsV3 } };
const canConvert = (kind, ver) => !!(CONVERT[kind] && CONVERT[kind][ver]);

/* ---- newer versions, converted down: WRITE to an FM-1 that reads only an older one (the editor only) ---- */
/* the loops' version 4 -> version 3: each lane of a loop a loop of its own (16 at most), its base written out over the
 * length as it plays (its repeats, then nothing); QUANTIZE ROW STEP, REAL TIME OFF, the others 1/8. -> { parts, lost:
 * [what version 3 cannot hold, in words] } */
function loopsV4to3(parts) {
    const L = unpackLoops(parts), lost = new Set(), out = [];
    const q = L.quantize === 0 ? 0 : L.quantize === 4 ? 2 : 1;
    if (q === 1 && L.quantize !== 1 + L.grid) lost.add("QUANTIZE " + (L.quantize + 1) + " ROWS (1/8 INSTEAD)");
    if (L.legato) lost.add(L.legato === 2 ? "MONO LEGATO LEGATO" : "MONO LEGATO JUMP");
    if (L.n && !L.mix.every((v, i) => v === MIX_DEFAULT[i]) && !L.mix.every(v => !v)) lost.add("THE NEXT LOOP'S MIX");
    let selected = L.selected >= L.n ? -1 : null;
    L.loops.forEach((lp, i) => {
        if (lp.mono) lost.add("MONO (PLAYED POLY)");
        for (let lane = 0; lane < lp.lanes; lane++) {
            const cells = [];
            for (let r = 0; r < L.length && Math.floor(r / lp.base) < lp.repeat; r++) {
                const c = lp.cells.get(cellKey(r % lp.base, lane));
                if (hasField(c)) cells.push({ row: r, note: c.note, ins: -1, vol: c.vol, cmds: c.cmds });
            }
            if (lane && !cells.length) continue;    /* (an empty lane but the first: nothing to keep) */
            if (out.length >= LOOPS_MAX) { lost.add("LOOPS PAST 16 (EACH LANE TAKES ONE)"); continue; }
            if (i === L.selected && selected === null) selected = out.length;
            out.push({ cart: lp.cart, slot: lp.slot, muted: lp.muted, mixer: lp.mixer, cells });
        }
    });
    const n = out.length, head = new Uint8Array(72 + 10 * n);
    head[0] = L.tempo & 0xFF; head[1] = L.tempo >> 8 & 0xFF;
    head[2] = (L.ticks & 31) | (L.countIn === 3 ? 0 : L.countIn & 3) << 5;
    head[3] = L.swing;
    head[4] = (L.grid & 3) | q << 2;
    head[5] = n; head[6] = selected == null || selected < 0 ? n : selected;
    head.set(L.fx, 8);
    out.forEach((lp, i) => { const o = 72 + 10 * i; head[o] = lp.cart; head[o + 1] = lp.slot; head[o + 2] = i; head[o + 3] = lp.muted ? 1 : 0; head.set(lp.mixer, o + 4); });
    if (!n) return { parts: [head], lost: [...lost] };
    /* one 16-track pattern as long as the loops, loop i's cells on track i: part 0 after the head, the rest in parts
     * of their own, each with the pattern's bytes 0-4 */
    const streams = out.map(lp => lp.cells.length ? trackStream(lp.cells) : null);
    const groups = [[]];
    let len = head.length + 5;
    streams.forEach((s, t) => {
        if (!s) return;
        if (5 + s.length > RECORD_MAX) throw new Error("loop " + hex2(t) + "'s notes over the length do not fit version 3");
        if (len + s.length > RECORD_MAX) { groups.push([]); len = 5; }
        groups[groups.length - 1].push(t);
        len += s.length;
    });
    if (groups.length > 16) throw new Error("the loops do not fit version 3's 16 parts");
    return { lost: [...lost], parts: groups.map((g, sub) => {
        let size = (sub ? 0 : head.length) + 5, mask = 0;
        for (const t of g) { size += streams[t].length; mask |= 1 << t; }
        const p = new Uint8Array(size);
        let at = 0;
        if (!sub) { p.set(head); at = head.length; }
        p.set([16, L.length - 1, 1, mask & 0xFF, mask >> 8], at);
        at += 5;
        for (const t of g) { p.set(streams[t], at); at += streams[t].length; }
        return p;
    }) };
}
const DOWN = { 0x22: { 4: loopsV4to3 } };
/* an object's records (its parts, in order) of version from -> version to, a step down at a time; null: no way down.
 * -> { recs (numbered as the given ones), lost } */
function downgrade(recs, to) {
    const r0 = recs[0];
    let parts = recs.slice().sort((a, b) => a.sub - b.sub).map(r => r.payload), v = r0.ver;
    const lost = [];
    while (v > to) {
        const f = DOWN[r0.kind] && DOWN[r0.kind][v];
        if (!f) return null;
        const d = f(parts);
        parts = d.parts; lost.push(...d.lost); v--;
    }
    return { lost, recs: parts.map((payload, sub) => ({ kind: r0.kind, num: r0.num, sub, ver: to, payload })) };
}
const canDowngrade = (kind, from, to) => { for (let v = from; v > to; v--) if (!(DOWN[kind] && DOWN[kind][v])) return false; return from > to; };
/* records [{ kind, num, sub, ver, payload, ... }] -> the same, each object of a version converted here (all its parts)
 * replaced by its current records, in its place; one that does not convert is kept as it is */
function upgrade(recs) {
    const groups = new Map(), gkey = r => r.kind + "." + r.num + "." + r.ver;
    for (const r of recs) if (r.payload && canConvert(r.kind, r.ver)) {
        if (!groups.has(gkey(r))) groups.set(gkey(r), []);
        groups.get(gkey(r)).push(r);
    }
    if (!groups.size) return recs;
    const done = new Map();
    for (const [k, g] of groups) {
        g.sort((a, b) => a.sub - b.sub);
        const r0 = g[0];
        try {
            done.set(k, CONVERT[r0.kind][r0.ver](g.map(r => r.payload)).map((payload, sub) => ({ kind: r0.kind, num: r0.num, sub,
                ver: VERSION[r0.kind], len: payload.length, crc: ED.util.crc32(payload), payload })));
        } catch (e) { done.set(k, g); }
    }
    const out = [];
    for (const r of recs) {
        const k = gkey(r);
        if (!done.has(k)) out.push(r);
        else if (done.get(k)) { out.push(...done.get(k)); done.set(k, null); }
    }
    return out;
}

/* ---- a record's place in the store: a cartridge's kind from its number (one number space) ---- */
const isCart = k => CART_KINDS.includes(k);
const hex2 = v => (v < 16 ? "0" : "") + (v & 0xFF).toString(16).toUpperCase();
const NOTE_NAMES = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"];
/* a cell's note as the grid shows it: C-4 (MIDI 60), --- none, === note off */
const noteName = n => !n ? "---" : n === NOTE_OFF ? "===" : NOTE_NAMES[n % 12] + Math.floor(n / 12 - 1);
function parseNote(s) {
    const m = /^([A-G])([-#])?(-?\d)$/i.exec(String(s).trim());
    if (!m) return -1;
    const base = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 }[m[1].toUpperCase()] + (m[2] === "#" ? 1 : 0);
    const n = (parseInt(m[3], 10) + 1) * 12 + base;
    return n >= 12 && n <= 127 ? n : -1;
}

ED.rec = { KIND, VERSION, CART_KINDS, RECORD_MAX, NAME_CHARS, NOTE_OFF, BitReader, BitWriter, nameCodes, nameText, cleanName,
    packName, unpackName, SLOT, DX7_OP, DX7_VOICE, JUNO, TB303, FUSION, CHIP, SAMPLER, ENV, SAMPLER_SLOT, SAMPLE_ENTRY,
    unpackCart, packCart, unpackSlot, packSlot, unpackSampler, packSampler, unpackSamplerInst, packSamplerInst,
    ptrEncode, ptrDecode, ptrInfo, PAT_ROWS_MAX, unpackPattern, packPattern, cellKey, unpackSong, packSong, newSong, MIX_FIELDS,
    MIX_DEFAULT, SONG_STOP, SONG_LINEAR, SONG_FAST_VOL, fxChain, setFxChain, fxOn, setFxOn, defaultFx, FX_CHAIN_DEFAULT,
    unpackSettings, packSettings, unpackMidiMap, packMidiMap, LOOPS_HEAD, LOOP_SIZE, LOOP_FILL, unpackLoops, packLoops, loopsV3,
    canConvert, upgrade, loopsV4to3, downgrade, canDowngrade, isCart, hex2, noteName, parseNote };
})(globalThis.ED = globalThis.ED || {});

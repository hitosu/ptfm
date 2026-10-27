/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* DX7 cartridges both ways (specs/files/instruments.md#dx7): Yamaha's 32-voice bulk dump (F0 43 0n 09 20 00, 32 packed
 * voices of 128 B, a checksum, F7) and the single voice's dump (F0 43 0n 00 01 1B, 155 B) into a DX7 cartridge; a DX7
 * cartridge back into a bulk dump. The record keeps the packed voice's 145 parameters, so the bits come back as they
 * went in, but the names' characters outside PT/FM's set and the bits the DX7 itself ignores. */
(function (ED) {
"use strict";
const R = ED.rec, K = R.KIND;
ED.conv = ED.conv || {};

const OPF = ["r1", "r2", "r3", "r4", "l1", "l2", "l3", "l4", "break_point", "left_depth", "right_depth"];
function fromPacked(v) {                            /* 128 B -> a slot */
    const ops = [];
    for (let i = 0; i < 6; i++) {
        const b = v.subarray(17 * i, 17 * i + 17), o = {};
        OPF.forEach((f, k) => { o[f] = b[k] & 127; });
        o.left_curve = b[11] & 3; o.right_curve = (b[11] >> 2) & 3;
        o.rate_scaling = b[12] & 7; o.detune = (b[12] >> 3) & 15;
        o.amp_mod_sens = b[13] & 3; o.key_vel_sens = (b[13] >> 2) & 7;
        o.output_level = b[14] & 127;
        o.mode = b[15] & 1; o.coarse = (b[15] >> 1) & 31;
        o.fine = b[16] & 127;
        ops.push(o);
    }
    const voice = { pr1: v[102] & 127, pr2: v[103] & 127, pr3: v[104] & 127, pr4: v[105] & 127, pl1: v[106] & 127, pl2: v[107] & 127,
        pl3: v[108] & 127, pl4: v[109] & 127, algorithm: v[110] & 31, feedback: v[111] & 7, osc_key_sync: (v[111] >> 3) & 1,
        lfo_speed: v[112] & 127, lfo_delay: v[113] & 127, lfo_pm_depth: v[114] & 127, lfo_am_depth: v[115] & 127,
        lfo_key_sync: v[116] & 1, lfo_wave: (v[116] >> 1) & 7, pitch_mod_sens: (v[116] >> 4) & 7, transpose: Math.min(63, v[117] & 127) };
    const name = R.cleanName(String.fromCharCode(...v.subarray(118, 128)), 10);
    return { name: name || " ", level: 0, ops, voice };
}
function toPacked(s) {                              /* a slot -> 128 B */
    const v = new Uint8Array(128);
    for (let i = 0; i < 6; i++) {
        const o = s.ops[i], at = 17 * i;
        OPF.forEach((f, k) => { v[at + k] = o[f] & 127; });
        v[at + 11] = (o.left_curve & 3) | (o.right_curve & 3) << 2;
        v[at + 12] = (o.rate_scaling & 7) | (o.detune & 15) << 3;
        v[at + 13] = (o.amp_mod_sens & 3) | (o.key_vel_sens & 7) << 2;
        v[at + 14] = o.output_level & 127;
        v[at + 15] = (o.mode & 1) | (o.coarse & 31) << 1;
        v[at + 16] = o.fine & 127;
    }
    const x = s.voice;
    [x.pr1, x.pr2, x.pr3, x.pr4, x.pl1, x.pl2, x.pl3, x.pl4].forEach((n, k) => { v[102 + k] = n & 127; });
    v[110] = x.algorithm & 31;
    v[111] = (x.feedback & 7) | (x.osc_key_sync & 1) << 3;
    v[112] = x.lfo_speed & 127; v[113] = x.lfo_delay & 127; v[114] = x.lfo_pm_depth & 127; v[115] = x.lfo_am_depth & 127;
    v[116] = (x.lfo_key_sync & 1) | (x.lfo_wave & 7) << 1 | (x.pitch_mod_sens & 7) << 4;
    v[117] = x.transpose & 127;
    const n = (s.name || "").padEnd(10).slice(0, 10);
    for (let k = 0; k < 10; k++) v[118 + k] = n.charCodeAt(k) & 127;
    return v;
}
function fromVced(b) {                              /* the single voice's 155 B */
    const ops = [];
    for (let i = 0; i < 6; i++) {
        const a = b.subarray(21 * i, 21 * i + 21);
        ops.push({ r1: a[0], r2: a[1], r3: a[2], r4: a[3], l1: a[4], l2: a[5], l3: a[6], l4: a[7], break_point: a[8], left_depth: a[9],
            right_depth: a[10], left_curve: a[11] & 3, right_curve: a[12] & 3, rate_scaling: a[13] & 7, amp_mod_sens: a[14] & 3,
            key_vel_sens: a[15] & 7, output_level: a[16], mode: a[17] & 1, coarse: a[18] & 31, fine: a[19], detune: a[20] & 15 });
    }
    const v = b.subarray(126);
    const voice = { pr1: v[0], pr2: v[1], pr3: v[2], pr4: v[3], pl1: v[4], pl2: v[5], pl3: v[6], pl4: v[7], algorithm: v[8] & 31,
        feedback: v[9] & 7, osc_key_sync: v[10] & 1, lfo_speed: v[11], lfo_delay: v[12], lfo_pm_depth: v[13], lfo_am_depth: v[14],
        lfo_key_sync: v[15] & 1, lfo_wave: v[16] & 7, pitch_mod_sens: v[17] & 7, transpose: Math.min(63, v[18]) };
    return { name: R.cleanName(String.fromCharCode(...v.subarray(19, 29)), 10) || " ", level: 0, ops, voice };
}

/* a .syx's bytes -> { recs, what } (a DX7 cartridge numbered 00 for now: the import places it at a free number) */
ED.conv.syx = function (b, name) {
    const carts = [];
    for (let i = 0; i < b.length; i++) {
        if (b[i] !== 0xF0 || b[i + 1] !== 0x43) continue;
        const fmt = b[i + 3];
        if (fmt === 0x09 && b[i + 4] === 0x20 && b[i + 5] === 0x00 && i + 6 + 4096 < b.length) {
            const d = b.subarray(i + 6, i + 6 + 4096);
            const slots = [];
            for (let k = 0; k < 32; k++) slots.push(fromPacked(d.subarray(128 * k, 128 * k + 128)));
            carts.push({ slots });
            i += 4096 + 7;
        } else if (fmt === 0x00 && b[i + 4] === 0x01 && b[i + 5] === 0x1B && i + 6 + 155 < b.length) {
            carts.push({ slots: [fromVced(b.subarray(i + 6, i + 6 + 155))] });
            i += 155 + 7;
        }
    }
    if (!carts.length) throw new Error(name + ": no DX7 voice dump in it (a 32-voice bulk dump or a single voice)");
    const base = R.cleanName(String(name || "").replace(/\.[^.]*$/, ""), 12) || "DX7 SYX";
    const recs = carts.map((c, i) => {
        const cartName = carts.length > 1 ? R.cleanName(base.slice(0, 9) + " " + (i + 1), 12) : base;
        const payload = R.packCart(K.DX7, { name: cartName, slots: c.slots });
        return { kind: K.DX7, num: i, sub: 0, ver: R.VERSION[K.DX7], payload };
    });
    return { recs, what: carts.map(c => "A DX7 CARTRIDGE OF " + c.slots.length + " VOICE" + (c.slots.length === 1 ? "" : "S")).join(", ") };
};
/* a DX7 cartridge -> a 32-voice bulk dump (empty slots: the DX7's INIT VOICE) */
ED.conv.toSyx = function (cart) {
    const d = new Uint8Array(4096);
    for (let k = 0; k < 32; k++) {
        const s = cart.slots[k] || Object.assign(ED.defs.newSlot(K.DX7, "INIT VOICE"));
        d.set(toPacked(s), 128 * k);
    }
    let sum = 0;
    for (const x of d) sum += x;
    const out = new Uint8Array(4104);
    out.set([0xF0, 0x43, 0x00, 0x09, 0x20, 0x00], 0);
    out.set(d, 6);
    out[4102] = (-sum) & 0x7F;
    out[4103] = 0xF7;
    return out;
};
ED.conv.dx7Packed = { fromPacked, toPacked };
})(globalThis.ED = globalThis.ED || {});

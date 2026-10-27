/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The editor's records (editor/js/records.js) both ways: every record of the demo disk and every built-in
 * instrument decoded and coded again, byte for byte (cartridges, samplers, patterns in parts, songs, PTR audio);
 * edits that must keep the rest; names; notes. Then the decoded fields against tools/ptfm_records.py's, through
 * a dump it makes (tests/editor/py_dump.py).
 *
 *   node tests/editor/records_test.js [PY_DUMP.json] */
"use strict";
const fs = require("fs");
const { ED, S, wasm, demo, check, done } = require("./load.js");
const R = ED.rec, K = R.KIND;
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

const disk = S.parseData(demo());
check(!disk.problems.length, "the demo disk reads: " + disk.problems.join("; "));
const files = disk.files.slice();
/* the built-in instruments, from the editor's WebAssembly */
const ex = wasm();
const mem = () => new Uint8Array(ex.memory.buffer);
for (let i = 0; i < ex.ed_builtin_count(); i++) {
    const h = mem().slice(ex.ed_builtin(i), ex.ed_builtin(i) + 12), dv = new DataView(h.buffer);
    const len = dv.getUint32(4, true), at = dv.getUint32(8, true);
    files.push({ kind: h[0], num: h[1], sub: h[2], ver: h[3], len, payload: mem().slice(at, at + len), builtin: true });
}
check(files.some(f => f.builtin && f.kind === K.DX7 && f.num === 0xF0), "the built-in DX7 cartridge F0 is there");

let n = { cart: 0, sampler: 0, pattern: 0, song: 0, audio: 0 };
const patterns = new Map();
for (const f of files) {
    if (R.isCart(f.kind)) {
        const c = R.unpackCart(f.kind, f.payload);
        check(eq(R.packCart(f.kind, c), f.payload), `cartridge ${f.kind}.${R.hex2(f.num)} codes back`);
        /* a field changed and back: the rest unchanged */
        const s = (c.slots || c.instruments).find(x => x);
        if (s) {
            s.level = s.level === 5 ? 6 : 5;
            check(!eq(R.packCart(f.kind, c), f.payload), `cartridge ${R.hex2(f.num)}: a LEVEL changed shows`);
        }
        f.kind === K.SAMPLER ? n.sampler++ : n.cart++;
    } else if (f.kind === K.PATTERN) {
        if (!patterns.has(f.num)) patterns.set(f.num, []);
        patterns.get(f.num)[f.sub] = f.payload;
    } else if (f.kind === K.SONG) {
        const s = R.unpackSong(f.payload);
        check(eq(R.packSong(s), f.payload), `song ${R.hex2(f.num)} codes back`);
        n.song++;
    } else if (f.kind === K.AUDIO) {
        const d = R.ptrDecode(f.payload);
        const back = R.ptrEncode(d.samples, d.bits);
        check(eq(back, f.payload), `sample ${R.hex2(f.num)}.${R.hex2(f.sub)} (${d.bits}-bit, ${d.samples.length}) codes back byte for byte`);
        n.audio++;
    }
}
for (const [num, parts] of patterns) {
    const p = R.unpackPattern(parts);
    const back = R.packPattern(p);
    check(back.length === parts.length && back.every((b, i) => eq(b, parts[i])), `pattern ${R.hex2(num)} codes back (${parts.length} parts)`);
    n.pattern++;
}
check(n.cart >= 12 && n.sampler >= 5 && n.pattern >= 20 && n.song >= 18 && n.audio >= 30,
    "counts: " + JSON.stringify(n));

/* names */
check(R.unpackName(R.packName("CITY LIGHTS", 16), 16) === "CITY LIGHTS", "a name both ways");
check(R.cleanName("abc~def", 6) === "ABC DE", "a typed name cleaned");
check(R.noteName(60) === "C-4" && R.parseNote("C-4") === 60 && R.parseNote("a#3") === 58 && R.noteName(1) === "===", "notes");

/* a pattern made from nothing: cells, long skips, four commands, parts; the largest (16 x 128, every cell full: 8
 * parts at most, specs/files/patterns.md#size) */
const p = { tracks: 16, rows: 128, step: 1, cells: new Map() };
for (let t = 0; t < 16; t++) for (let r = 0; r < 128; r += 1)
    p.cells.set(R.cellKey(r, t), { note: 12 + (r % 100), ins: (t << 8) | (r & 7), vol: r & 127, cmds: [[0x0A, 4], [0x08, 0x80], [0x0C, 32], [0x0F, 0x40]] });
p.cells.set(R.cellKey(127, 3), { note: 1, ins: -1, vol: -1, cmds: [] });
const pp = R.packPattern(p);
check(pp.length > 4 && pp.length <= 8 && pp.every(b => b.length <= R.RECORD_MAX), "a full pattern in parts: " + pp.length);
let refused = "";
try { R.packPattern({ tracks: 1, rows: 129, step: 1, cells: new Map() }); } catch (e) { refused = e.message; }
check(refused, "a pattern of 129 rows refused (128 at most): " + refused);
/* a record of 200 rows (made before the rule) reads as its first 128 (specs/files/patterns.md#size): C-4 on row 0,
 * C#4 on row 150 */
const old200 = R.unpackPattern([Uint8Array.of(1, 199, 1, 1, 0, 0x80, 60, 0x87, 61, 149, 0)]);
check(old200.rows === 128 && old200.cells.size === 1 && old200.cells.get("0,0").note === 60,
    "a record of 200 rows reads as its first 128: " + old200.rows + " rows, " + old200.cells.size + " cells");
const p2 = R.unpackPattern(pp);
let same = p2.cells.size === p.cells.size;
for (const [k, c] of p.cells) { const d = p2.cells.get(k); if (!d || d.note !== c.note || d.ins !== c.ins || d.vol !== c.vol || JSON.stringify(d.cmds) !== JSON.stringify(c.cmds)) same = false; }
check(same, "a full pattern decodes to the same cells");
const sparse = { tracks: 4, rows: 128, step: 0, cells: new Map([[R.cellKey(0, 0), { note: 60, ins: -1, vol: -1, cmds: [] }],
    [R.cellKey(100, 0), { note: 61, ins: 0x0102, vol: -1, cmds: [] }], [R.cellKey(127, 3), { note: 0, ins: -1, vol: 5, cmds: [] }]]) };
const sp = R.unpackPattern(R.packPattern(sparse));
check(sp.cells.size === 3 && sp.cells.get("100,0").note === 61 && sp.cells.get("127,3").vol === 5, "a long skip both ways");
/* the settings record of 128 B (specs/files/settings.md: bytes 126-127, the EDITOR's pattern) both ways */
const st128 = R.packSettings(R.unpackSettings(Uint8Array.from({ length: 128 }, (_, i) => i)));
check(st128.length === 128 && st128[126] === 126 && st128[127] === 127, "a settings record of 128 B both ways, bytes 126-127 kept");
const st126 = R.packSettings(R.unpackSettings(new Uint8Array(126).fill(7)));
check(st126.length === 128 && st126[125] === 7 && st126[126] === 0 && st126[127] === 0, "a settings record of 126 B (from before) written as 128, the EDITOR's own");
const nst = ED.defs.newSettings();
check(nst.length === 128 && nst[126] === 0, "new settings: 128 B, the EDITOR on its own pattern (byte 126 bit 0 clear)");
/* SECOND CORE: byte 123's bit 1 (set: OFF), the current instrument's bit 0 kept beside it */
{
    const sc = ED.defs.SETTINGS.find(s => s.n === "SECOND CORE"), p = new Uint8Array(128);
    ED.defs.setCurInst(p, 0x0102);
    check(sc && sc.get(p) === 0 && sc.get(nst) === 0, "SECOND CORE: ON by default (byte 123 bit 1 clear)");
    sc.set(p, 1);
    check(p[123] === 3 && sc.get(p) === 1 && ED.defs.curInst(p) === 0x0102, "SECOND CORE OFF: byte 123 bit 1 set, the current instrument kept (" + p[123] + ")");
    const back = R.packSettings(R.unpackSettings(p));
    check(back[123] === 3 && sc.get(back) === 1, "SECOND CORE OFF both ways through the record");
    sc.set(p, 0);
    check(p[123] === 1 && sc.get(p) === 0, "SECOND CORE ON again: bit 1 clear, bit 0 kept");
}
/* the EDITOR's own pattern FE and the keys' song FE: items of the FM-1 panel's SYSTEM group; numbers given are 00-EF */
{
    const M = ED.model, song = R.packSong(R.newSong("K"));
    const fe = M.fromRecords([{ kind: K.PATTERN, num: 0xFE, sub: 0, ver: 1, payload: Uint8Array.of(8, 63, 1, 0, 0) },
        { kind: K.SONG, num: 0xFE, sub: 0, ver: 1, payload: song },
        { kind: K.PATTERN, num: 0x01, sub: 0, ver: 1, payload: Uint8Array.of(8, 63, 1, 0, 0) }], { side: "R" }).items;
    const names = fe.map(i => M.groupOf(i) + ":" + M.nameOf(i, fe)).join(", ");
    check(names === "SYSTEM:EDITOR'S PATTERN, SYSTEM:KEYS' SONG, PATTERNS:PATTERN 01", "FE on the FM-1 panel: " + names);
    const taken = new Set(Array.from({ length: 0xF0 }, (_, n) => n));
    check(M.pickNum(taken, 0xFE) === -1 && M.pickNum(new Set([0]), 0xFE) === 1, "numbers given are 00-EF, never the firmware's F0-FF");
}

/* PTR on its own: noise, silence, a full-scale square, 8 and 16 bits */
for (const bits of [8, 16]) {
    const max = bits === 8 ? 127 : 32767, xs = new Int16Array(3000);
    for (let i = 0; i < xs.length; i++) xs[i] = i < 600 ? 0 : i < 1500 ? ((i >> 5) & 1 ? max : -max - 1) : Math.round((Math.random() * 2 - 1) * max);
    const rec = R.ptrEncode(xs, bits), d = R.ptrDecode(rec);
    check(d.bits === bits && eq(Array.from(d.samples), Array.from(xs)), `PTR ${bits}-bit both ways`);
}

/* against tools/ptfm_records.py: its dump of the demo disk's decoded fields */
const dumpPath = process.argv[2];
if (dumpPath && fs.existsSync(dumpPath)) {
    const py = JSON.parse(fs.readFileSync(dumpPath, "utf8"));
    let compared = 0;
    for (const f of disk.files) {
        const key = `${f.kind}.${f.num}.${f.sub}`, want = py[key];
        if (!want) continue;
        let got;
        if (R.isCart(f.kind)) {
            const c = R.unpackCart(f.kind, f.payload);
            got = { name: c.name, slots: (c.slots || c.instruments).map(s => s ? { name: s.name, level: s.level } : null) };
            if (c.samples) got.samples = c.samples.map(e => e ? { name: e.name, length: e.length, rate: e.rate, coding: e.coding } : null);
        } else if (f.kind === K.SONG) {
            const s = R.unpackSong(f.payload);
            got = { name: s.name, tempo: s.tempo, ticks: s.ticks, rpb: s.rpb, swing: s.swing, positions: s.positions, mixer: s.mixer };
        } else if (f.kind === K.AUDIO) {
            const d = R.ptrDecode(f.payload);
            let sum = 0;
            for (const x of d.samples) sum = (sum * 31 + (x & 0xFFFF)) >>> 0;
            got = { bits: d.bits, length: d.samples.length, hash: sum };
        } else continue;
        check(JSON.stringify(got) === JSON.stringify(want), `${S.describe(f)} as tools/ptfm_records.py reads it`);
        compared++;
    }
    for (const [num, parts] of patterns) {
        const want = py[`32.${num}`];
        if (!want) continue;
        const p = R.unpackPattern(parts), cells = [];
        for (const [k, c] of [...p.cells].sort((a, b) => { const [ra, ta] = a[0].split(",").map(Number), [rb, tb] = b[0].split(",").map(Number); return ra - rb || ta - tb; }))
            cells.push([k, c.note, c.ins, c.vol, c.cmds]);
        check(JSON.stringify({ tracks: p.tracks, rows: p.rows, step: p.step, cells }) === JSON.stringify(want), `pattern ${R.hex2(num)} as tools/ptfm_records.py reads it`);
        compared++;
    }
    check(compared > 50, "compared with tools/ptfm_records.py: " + compared);
} else console.log("(no tools/ptfm_records.py dump given: that comparison skipped)");
done("records");

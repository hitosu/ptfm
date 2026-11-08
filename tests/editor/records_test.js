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
    } else if (f.kind === K.SAMPLE) {                   /* a sample of its own: its entry, then its audio */
        const s = R.unpackSample(f.payload), d = R.ptrDecode(s.audio);
        check(eq(R.ptrEncode(d.samples, d.bits), s.audio) && eq(R.packSample(s.entry, s.audio), f.payload) && s.entry &&
              s.entry.length === d.samples.length && s.entry.coding === (d.bits === 8 ? 1 : 2),
              `sample ${R.hex2(f.num)} (${d.bits}-bit, ${d.samples.length}) codes back byte for byte, its entry its audio's`);
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
        } else if (f.kind === K.SAMPLE) {
            const s = R.unpackSample(f.payload), d = R.ptrDecode(s.audio), e = s.entry;
            let sum = 0;
            for (const x of d.samples) sum = (sum * 31 + (x & 0xFFFF)) >>> 0;
            got = { entry: e && { name: e.name, length: e.length, rate: e.rate, coding: e.coding }, bits: d.bits, length: d.samples.length,
                    hash: sum };
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

/* ---- samples of their own and sampler cartridges of version 2 (instruments.md#samples) ---- */
const hexOf = b => Array.from(b, x => (x < 16 ? "0" : "") + x.toString(16)).join("");
const unhex = h => Uint8Array.from(h.match(/../g) || [], x => parseInt(x, 16));
const rec = (kind, num, sub, ver, payload) => ({ kind, num, sub, ver, len: payload.length, crc: S.crc32(payload), payload });
check(K.SAMPLE === 0x11 && R.VERSION[0x11] === 1 && R.SAMPLER_VERSION === 2, "kind 0x11 version 1; the sampler cartridge's current version is 2");
{
    const inst = (sample, name, o) => Object.assign({ sample, start: 0, end: 100, loop_start: 0, loop_end: 0, loop: 0, base_note: 60, tune: 128,
        level: 0, volume: 64, env: { r1: 99, r2: 90, r3: 80, r4: 70, l1: 99, l2: 90, l3: 80, l4: 0 }, name }, o);
    const c = { name: "V2 SAMPLER", instruments: [inst(0xC3, "HIGH NUM"), null, inst(7, "SEVEN")] };
    const b = R.packSampler(c);
    check(b.length === 10 + 3 * 40 && b[9] === 3, "a version 2 sampler: 10 B + 40 B an instrument, no table (" + b.length + ")");
    const u = R.unpackSampler(b);
    check(u.instruments[0].sample === 0xC3 && u.instruments[2].sample === 7 && !u.instruments[1] && !("samples" in u) &&
        u.instruments[0].name === "HIGH NUM" && u.instruments[2].level === 0, "SAMPLE is 8 bits: 0xC3 round-trips");
    check(eq(R.unpackSampler(b, 2).instruments.map(s => s && s.sample), [0xC3, null, 7]) && eq(R.packSampler(u), b), "a version 2 sampler codes back byte for byte");
    /* the spare bits (84) are kept: the last bit of the first slot, and a bit in the middle of the spare */
    const sp = b.slice();
    sp[10 + 39] |= 0x80; sp[10 + 24] |= 0x04;
    check(eq(R.packSampler(R.unpackSampler(sp)), sp), "version 2: spare bits set in a record travel unchanged");
    const edit = R.unpackSampler(sp); edit.instruments[0].sample = 0x11;
    const eb = R.packSampler(edit);
    check(eb[10] === 0x11 && eb[10 + 39] === sp[10 + 39] && eb[10 + 24] === sp[10 + 24], "SAMPLE changed: the spare bits stay");
    check(R.unpackCart(K.SAMPLER, b).instruments[2].sample === 7 && eq(R.packCart(K.SAMPLER, R.unpackCart(K.SAMPLER, b)), b), "unpackCart / packCart take version 2");
    /* the old form is still read: odd length (an old sampler and its audio of tools/ptfm_records.py's conversion cases:
     * the demo disk holds the new form) */
    const pyc = dumpPath && fs.existsSync(dumpPath) ? JSON.parse(fs.readFileSync(dumpPath, "utf8")).conv.basic.in : [];
    const oldRec = pyc.find(([kind, , , ver]) => kind === K.SAMPLER && ver === 1), oldAudio = pyc.find(([kind]) => kind === K.AUDIO);
    if (oldRec && oldAudio) {
        const dsk = { payload: unhex(oldRec[4]) }, old = R.unpackSampler(dsk.payload);
        check(old.samples && eq(R.packSampler(old), dsk.payload) && R.unpackSampler(dsk.payload, 1).samples.length === old.samples.length,
              "the old sampler is read and coded as before");
    } else console.log("(no tools/ptfm_records.py dump given: the old sampler's reading skipped)");
    /* a sample record: the entry (16 B), then the audio; the index's offsets count from byte 16 */
    const xs = Array.from({ length: 700 }, (_, i) => Math.round(Math.sin(i / 9) * 90));
    const audio = R.ptrEncode(xs, 8), entry = { length: 700, rate: 22050, coding: 1, name: "SINE" };
    const sb = R.packSample(entry, audio), su = R.unpackSample(sb);
    check(sb.length === 16 + audio.length && eq(sb.subarray(16), audio), "a sample record: the entry, then the audio whole (" + sb.length + " B)");
    check(su.entry.name === "SINE" && su.entry.length === 700 && su.entry.rate === 22050 && su.entry.coding === 1 && eq(su.audio, audio) &&
        eq(R.packSample(su.entry, su.audio), sb), "a sample codes back byte for byte");
    check(eq(Array.from(R.ptrDecode(sb.subarray(16)).samples), xs), "the index's offsets count from byte 16: the audio decodes from there");
    check(R.unpackSample(R.packSample(null, audio)).entry === null, "an emptied entry reads as none");
    /* an old audio record is the audio of a sample unchanged; the demo disk's samples' audio decodes as it */
    if (oldAudio) {
        const a0 = unhex(oldAudio[4]);
        check(eq(R.unpackSample(R.packSample(entry, a0)).audio, a0), "an old audio record is the audio of a sample, unchanged");
    }
    const s0 = files.find(f => f.kind === K.SAMPLE);
    check(s0 && R.ptrDecode(R.unpackSample(s0.payload).audio).samples.length === R.unpackSample(s0.payload).entry.length,
          "the demo disk's samples: records of their own, the audio after the entry");
}

/* ---- the converter: hand-made sets from tools/ptfm_records.py (tests/editor/py_dump.py), converted here and there ---- */
const py = dumpPath && fs.existsSync(dumpPath) ? JSON.parse(fs.readFileSync(dumpPath, "utf8")) : null;
const toRecs = list => list.map(([kind, num, sub, ver, h]) => rec(kind, num, sub, ver, unhex(h)));
const listOf = recs => recs.map(r => [r.kind, r.num, r.sub, r.ver, hexOf(r.payload)]);
const sampleOf = (recs, num) => { const r = recs.find(x => x.kind === 0x11 && x.num === num); return r && R.unpackSample(r.payload); };
const samplerOf = (recs, num) => { const r = recs.find(x => x.kind === K.SAMPLER && x.num === num); return r && R.unpackSampler(r.payload); };
if (py) {
    check(Object.keys(py.conv).length === 7, "the converter's cases: " + Object.keys(py.conv).join(", "));
    check(JSON.stringify(py.conv.short.bad) === "[6]" && JSON.stringify(S.convertSamplers(toRecs(py.conv.short.in)).bad) === "[6]",
        "an old sampler under 11 bytes: unreadable in both twins, kept as it is (" + JSON.stringify(py.conv.short.bad) + ")");
    /* v2 forms: packed here as there */
    const f = py.v2, insts = f.instruments.map(s => s && Object.assign({}, s));
    check(hexOf(R.packSampler({ name: "V2 SAMPLER", instruments: insts })) === f.sampler, "a version 2 sampler packs here as tools/ptfm_records.py packs it");
    check(hexOf(R.packSample(f.entry, unhex(f.audio))) === f.sample, "a sample record packs here as tools/ptfm_records.py packs it");
    const back = R.unpackSampler(unhex(f.sampler));
    check(JSON.stringify(back.instruments.map(s => s && { sample: s.sample, name: s.name, level: s.level, volume: s.volume, loop: s.loop, tune: s.tune })) ===
        JSON.stringify(f.instruments.map(s => s && { sample: s.sample, name: s.name, level: s.level, volume: s.volume, loop: s.loop, tune: s.tune })),
        "a version 2 sampler of tools/ptfm_records.py reads here with the same fields");
    for (const [name, c] of Object.entries(py.conv)) {
        const input = toRecs(c.in), r = S.convertSamplers(input);
        check(JSON.stringify(listOf(r.recs)) === JSON.stringify(c.out), `${name}: the records converted here equal tools/ptfm_records.py's`);
        check(JSON.stringify(r.older) === JSON.stringify(c.older) && r.samples === c.samples && r.dropped === c.dropped &&
            JSON.stringify(r.bad) === JSON.stringify(c.bad),
            `${name}: older ${JSON.stringify(r.older)}, ${r.samples} samples, ${r.dropped} dropped, unreadable ${JSON.stringify(r.bad)} as there`);
        /* one sampler at a time numbers exactly so */
        const old = input.filter(x => x.kind === K.SAMPLER && x.ver === 1).sort((a, b) => a.num - b.num);
        const ex = input.filter(x => x.kind === 0x11).map(x => ({ num: x.num, len: x.payload.length, payload: x.payload })), named = new Set();
        for (const x of input) if (x.kind === K.SAMPLER && x.ver === 2) R.unpackSampler(x.payload).instruments.forEach(s => s && named.add(s.sample));
        const conv = S.samplerConverter({ samples: ex, named }), made = new Map(), samplers = new Map();
        for (const o of old) {
            if (conv.full) break;               /* (the caller leaves the rest in the old form once one did not fit) */
            const audio = new Map(input.filter(x => x.kind === 0x10 && x.num === o.num).map(x => [x.sub, x.payload]));
            let res;
            try { res = conv.convert(o.num, o.payload, audio); } catch (e) { continue; }   /* (unreadable: kept, as convertSamplers does) */
            if (!res) continue;
            samplers.set(o.num, res.sampler);
            for (const s of res.samples) if (!s.merged) made.set(s.num, s.payload);
        }
        const want = r.recs.filter(x => x.kind === 0x11 && !ex.some(e => e.num === x.num)).map(x => x.num + ":" + hexOf(x.payload));
        check(JSON.stringify([...made].sort((a, b) => a[0] - b[0]).map(([n, p]) => n + ":" + hexOf(p))) === JSON.stringify(want) &&
            [...samplers].every(([n, p]) => eq(p, r.recs.find(x => x.kind === K.SAMPLER && x.num === n).payload)) && samplers.size === r.converted.length,
            `${name}: samplerConverter one at a time numbers as convertSamplers does`);
        check(conv.full === (r.older.length > 0), `${name}: full is set when a sampler did not fit`);
    }
    /* what the cases must show */
    const C = n => toRecs(py.conv[n].in), conv = n => S.convertSamplers(C(n));
    {
        const r = conv("basic"), s = r.recs;
        check(["KICK", "SNARE", "HAT", "CLAP"].every((nm, i) => sampleOf(s, i).entry.name === nm) && s.filter(x => x.kind === 0x11).length === 4,
            "samples 00 01 02 03: the shared one once, the numbers in sampler and table order");
        const a = samplerOf(s, 2).instruments, b = samplerOf(s, 5).instruments;
        check(a[0].sample === 0 && a[1].sample === 1 && a[2].sample === 2 && a[3] === null && a[4] === null, "sampler 02: its instruments name 00 01 02, one past the table's end is empty");
        check(b[0].sample === 3 && b[1].sample === 1 && b[0].name === "CLAP" && b[1].name === "SNARE 2", "sampler 05 names 03 and the shared 01, names and slots kept");
        check(!s.some(x => x.kind === 0x10) && r.dropped === 2 && r.samples === 4 && r.older.length === 0, "the old audio is gone: 2 with no entry dropped");
        check(s.filter(x => x.kind === K.SAMPLER).every(x => x.ver === 2 && x.payload.length === 10 + (x.num === 2 ? 5 : 2) * 40), "the samplers are version 2, 40 B an instrument");
        const u = R.upgrade(C("basic"));
        check(u.conversion && u.conversion.samples === 4 && JSON.stringify(listOf(u)) === JSON.stringify(listOf(s)), "upgrade() delegates to the page's converter");
        check(JSON.stringify(listOf(R.upgrade(C("basic"), { samplers: false }))) === JSON.stringify(py.conv.basic.in),"upgrade() leaves the samplers alone when asked");
    }
    {
        const s = conv("named").recs;
        check(sampleOf(s, 2).entry.name === "HELD" && ["KICK", "SNARE", "HAT"].every((nm, i) => sampleOf(s, [1, 3, 4][i]).entry.name === nm) && sampleOf(s, 5).entry.name === "CLAP",
            "numbers an instrument names (00) and a record holds (02) are passed over: 01 03 04, then 05");
        check(samplerOf(s, 7).instruments[0].sample === 0 && !sampleOf(s, 0), "the version 2 sampler is untouched (it names 00, which has no record)");
    }
    {
        const r = conv("existing"), s = r.recs;
        check(s.filter(x => x.kind === 0x11).length === 3 && sampleOf(s, 9).entry.name === "SNARE" && samplerOf(s, 2).instruments[1].sample === 9 &&
            samplerOf(s, 2).instruments[0].sample === 0 && samplerOf(s, 2).instruments[2].sample === 1, "a sample equal to a record already there is that one (09); the others take 00 01");
    }
    {
        const r = conv("emptied"), s = r.recs;
        const g = samplerOf(s, 3).instruments;
        check(g[0] === null && g[1].sample === 0 && sampleOf(s, 0).entry.name === "FIVE", "an emptied entry: its instruments are emptied, the other goes on");
        const t = samplerOf(s, 4).instruments;
        check(t[0].sample === 1 && t[1].sample === 2 && sampleOf(s, 1).entry.name === "AAA" && sampleOf(s, 2).entry.name === "BBB" &&
            eq(sampleOf(s, 1).audio, sampleOf(s, 2).audio), "the same audio under two names stays two samples");
    }
    {
        const r = conv("257"), s = r.recs;
        check(r.samples === 256 && r.converted.join() === "1,2,3,4" && r.older.join() === "5", "257 samples: the last whole sampler stays old (" + r.older + ")");
        check(s.filter(x => x.kind === 0x11).length === 256 && s.filter(x => x.kind === 0x10).length === 1 && s.find(x => x.kind === 0x10).older === true &&
            s.find(x => x.kind === K.SAMPLER && x.num === 5).older === true && s.find(x => x.kind === K.SAMPLER && x.num === 5).ver === 1,
            "it keeps its record and its audio, marked older");
        const r2 = conv("stops");
        check(r2.converted.join() === "1,2,3,4" && r2.older.join() === "5,6" && r2.samples === 252, "a sampler that does not fit stops the rest, a smaller one after it too (" + r2.older + ")");
    }
    /* back down to the old form (WRITE to an older PT/FM) and up again */
    {
        const up = S.convertSamplers(C("basic")).recs;
        const dg = R.downgrade(up.filter(x => x.kind === K.SAMPLER || x.kind === 0x11), 1);
        check(dg && R.canDowngrade(K.SAMPLER, 2, 1) && !R.canDowngrade(K.SAMPLER, 1, 0), "samplers 2 -> 1: a way down");
        check(dg.recs.every(x => x.ver === 1) && dg.recs.filter(x => x.kind === K.SAMPLER).length === 2 && dg.recs.filter(x => x.kind === 0x10).length === 5 &&
            !dg.recs.some(x => x.kind === 0x11), "each sampler an old sampler with copies of its samples as old audio (2 + 3 ... 5 copies), no samples sent");
        check(dg.lost.length === 1 && /SEVERAL SAMPLERS NAME/.test(dg.lost[0]), "lost: the sample two samplers name, copied into each (" + dg.lost + ")");
        const s5 = R.unpackSampler(dg.recs.find(x => x.kind === K.SAMPLER && x.num === 5).payload);
        check(s5.samples.length === 2 && s5.instruments[0].sample === 0 && s5.instruments[1].sample === 1 && s5.instruments[0].name === "CLAP", "sampler 05 down: numbered 00.. under it in the order its instruments name them");
        const again = S.convertSamplers(dg.recs.map(x => Object.assign({}, x)));
        check(again.recs.filter(x => x.kind === 0x11).length === 4 && JSON.stringify(listOf(again.recs.filter(x => x.kind === K.SAMPLER))) === JSON.stringify(listOf(up.filter(x => x.kind === K.SAMPLER))),
            "and up again: the same samplers, the same four samples");
        const noName = up.filter(x => x.kind === K.SAMPLER && x.num === 2).concat(up.filter(x => x.kind === 0x11));
        const d2 = R.downgrade(noName, 1);
        check(d2.lost.length === 1 && /1 SAMPLE NO INSTRUMENT NAMES/.test(d2.lost[0]), "lost: a sample no instrument names is not sent (" + d2.lost + ")");
        const missing = R.downgrade(up.filter(x => x.kind === K.SAMPLER && x.num === 5), 1);
        check(missing.recs.filter(x => x.kind === 0x10).length === 0 && /MISSING SAMPLE/.test(missing.lost.join()), "an instrument naming a missing sample is emptied, and said so");
    }
}
done("records");

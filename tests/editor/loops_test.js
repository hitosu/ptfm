/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The loops' record (specs/files/loops.md) in the editor: version 3 converted to version 4 as "Versions" says, every
 * byte of the result checked; a version 4 record of 16 loops over several parts decoded and coded back byte for byte;
 * a cartridge renumbered, the loop table rewritten in place; a version 3 record converted wherever records come in.
 *
 *   node tests/editor/loops_test.js */
"use strict";
const { ED, check, done } = require("./load.js");
const R = ED.rec, K = R.KIND, U = ED.util;
const eq = (a, b) => a.length === b.length && Array.prototype.every.call(a, (v, i) => v === b[i]);
const hexs = b => Array.from(b, v => R.hex2(v)).join(" ");
const cell = (note, vol, cmds) => ({ note, ins: -1, vol: vol === undefined ? -1 : vol, cmds: cmds || [] });

/* ---- version 3, built by hand: 3 loops on tracks 2, 0 and 5 of a 6-track pattern of 48 rows (2 bars at 1/16T) ---- */
const fx = Uint8Array.from({ length: 64 }, (_, i) => (i * 7 + 1) & 0xFF);
const v3cells = new Map([
    /* loop 0, track 2: a note, its off, a note with ED3, a key off alone, a note with A04 (goes) and ED1 (kept), a cell
       of F06 alone (goes, and the cell with it), a note off at the last row */
    [R.cellKey(0, 2), cell(60, 0x7F)], [R.cellKey(3, 2), cell(R.NOTE_OFF)], [R.cellKey(6, 2), cell(62, 0x60, [[0x0E, 0xD3]])],
    [R.cellKey(10, 2), cell(0, -1, [[0x14, 0x05]])], [R.cellKey(12, 2), cell(64, -1, [[0x0A, 0x04], [0x0E, 0xD1]])],
    [R.cellKey(20, 2), cell(0, -1, [[0x0F, 0x06]])], [R.cellKey(47, 2), cell(R.NOTE_OFF)],
    /* loop 1, track 0: a note with E12 (code 0x0E, not a note delay: goes), its off */
    [R.cellKey(0, 0), cell(45, 0x40, [[0x0E, 0x12]])], [R.cellKey(24, 0), cell(R.NOTE_OFF)],
    /* track 1: no loop's (left out); loop 2, track 5: nothing */
    [R.cellKey(5, 1), cell(50, 0x30)]]);
const v3pat = R.packPattern({ tracks: 6, rows: 48, step: 1, cells: v3cells });
check(v3pat.length === 1, "the version 3 pattern in one part");
const v3head = new Uint8Array(72 + 30);
v3head.set([0x2C, 0x01,                     /* TEMPO 300 */
    6 | 3 << 5,                             /* TICKS A ROW 6, COUNT-IN KEY */
    58,                                     /* SWING */
    1 | 1 << 2 | 0x10 | 0x20,               /* 1/16T, QUANTIZE 1/8, ALGORITHM WHILE PLAYING SKIP, a retired bit */
    3, 1, 0x2A]);                           /* 3 loops, loop 01 selected, the cursor on row 2A */
v3head.set(fx, 8);
v3head.set([0x03, 0x05, 2, 1, 0x60, 0x40, 0x00, 0x10, 0x20, 0x30], 72);     /* 03:05 on track 2, muted */
v3head.set([0xF1, 0x00, 0, 0, 0x7F, 0x80, 0x00, 0x00, 0x00, 0x00], 82);     /* F1:00 (built in) on track 0 */
v3head.set([0x03, 0x07, 5, 0, 0x50, 0xC0, 0x05, 0x00, 0x00, 0x40], 92);     /* 03:07 on track 5 */
const v3 = new Uint8Array(v3head.length + v3pat[0].length);
v3.set(v3head);
v3.set(v3pat[0], v3head.length);

/* version 4, by hand */
const want = [];
want.push(0x2C, 0x01, 0x06, 58, 0x01 | 2 << 2, 3, 1, 48);     /* COUNT-IN OFF; 1/8 at 1/16T: 3 ROWS (2); the length */
want.push(...fx);
want.push(0x7F, 0x80, 0, 0, 0, 0, 0, 0);                        /* the next loop's mix: a new song's track's; 78-79 */
want.push(0x03, 0x05, 0x00 | 0x20, 0, 47, 1, 0x60, 0x40, 0x00, 0x10, 0x20, 0x30);   /* colour 0, muted, 1 lane, 48 rows, once */
want.push(0xF1, 0x00, 0x01, 0, 47, 1, 0x7F, 0x80, 0, 0, 0, 0);                       /* colour 1 */
want.push(0x03, 0x07, 0x02, 0, 47, 1, 0x50, 0xC0, 0x05, 0x00, 0x00, 0x40);          /* colour 2 */
want.push(3, 47, 0, 0x03, 0x00);                                /* the payload: 3 loops, 48 rows, streams of loops 0 and 1 */
want.push(0x90, 60, 0x7F,                                       /* row 0 (stream row 0): C-4 7F */
    0x87, 0x01, 11,                                             /* row 3 (12): === */
    0x9F, 62, 0x60, 0x0E, 0xD3, 11,                             /* row 6 (24): D-4 60 ED3 */
    0x0F, 0x14, 0x05, 15,                                       /* row 10 (40): K05 */
    0x8F, 64, 0x0E, 0xD1, 7,                                    /* row 12 (48): E-4 ED1 (A04 gone) */
    0x87, 0x01, 139,                                            /* row 47 (188): === (row 20's F06 gone with its cell) */
    0x00);
want.push(0x90, 45, 0x40,                                       /* loop 1: A-2 40 (E12 gone) */
    0x87, 0x01, 95,                                             /* row 24 (96): === */
    0x00);
const v4 = R.loopsV3([v3]);
check(v4.length === 1, "converted: one part (" + v4.length + ")");
check(eq(v4[0], want), "version 3 converted, byte for byte:\n  got  " + hexs(v4[0]) + "\n  want " + hexs(want));
const L = R.unpackLoops(v4);
check(L.n === 3 && L.length === 48 && L.tempo === 300 && L.ticks === 6 && L.countIn === 0 && L.legato === 0 && L.swing === 58 &&
    L.grid === 1 && L.quantize === 2 && L.nextMono === 0 && L.selected === 1 && eq(L.fx, fx) && eq(L.mix, R.MIX_DEFAULT),
    "the converted setup reads back: " + JSON.stringify({ n: L.n, length: L.length, quantize: L.quantize, countIn: L.countIn }));
check(L.loops.every((l, i) => l.colour === i && l.lanes === 1 && l.base === 48 && l.repeat === 1 && !l.mono && !l.soloed) &&
    L.loops[0].muted && !L.loops[1].muted && L.loops[2].cells.size === 0, "each loop: its colour by its place, 1 lane, its base the length, once");
check(L.loops[0].cells.size === 6 && L.loops[0].cells.get("6,0").cmds[0][1] === 0xD3 && L.loops[0].cells.get("10,0").cmds[0][0] === 0x14 &&
    L.loops[1].cells.size === 2 && L.loops[1].cells.get("0,0").cmds.length === 0, "the cells on lane 0 of their rows");
check(eq(R.packLoops(L)[0], v4[0]), "the converted record codes back byte for byte");

/* QUANTIZE and COUNT-IN by the table of "Versions"; the selection on the placeholder; no loops */
const conv = (b2, b4, sel, n) => { const b = v3.slice(); b[2] = b2; b[4] = b4; if (sel !== undefined) b[6] = sel; return R.unpackLoops(R.loopsV3([b])); };
const q = (grid, q3) => conv(6, grid | q3 << 2).quantize;
check(q(0, 0) === 0 && q(0, 1) === 1 && q(1, 1) === 2 && q(2, 1) === 3 && q(2, 2) === 4 && q(1, 2) === 4,
    "QUANTIZE: STEP -> ROW, 1/8 -> 2 / 3 / 4 ROWS by the grid, OFF -> REAL TIME");
check(conv(6 | 1 << 5, 0).countIn === 1 && conv(6 | 2 << 5, 0).countIn === 2 && conv(6 | 3 << 5, 0).countIn === 0, "COUNT-IN: 1 and 2 BARS kept, KEY -> OFF");
check(conv(6, 0, 16).selected === 3 && conv(6, 0, 2).selected === 2, "the selection kept: a loop, or the placeholder (the loops' count)");
const none = new Uint8Array(72);
none.set([125, 0, 6, 50, 0, 0, 0, 0x10]);
none.set(fx, 8);
const v4none = R.loopsV3([none]);
check(v4none.length === 1 && v4none[0].length === 80 && v4none[0][7] === 0 && v4none[0][5] === 0 && eq(v4none[0].subarray(8, 72), fx) &&
    eq(v4none[0].subarray(72, 80), [0x7F, 0x80, 0, 0, 0, 0, 0, 0]), "no loops: the head alone, the length 0 (" + hexs(v4none[0].subarray(0, 8)) + ")");

/* a version 3 pattern in two parts, rows past 128 (made before the rule): one part of 128 rows, a long gap bridged */
{
    const cells = new Map([[R.cellKey(0, 0), cell(60, 0x7F)], [R.cellKey(100, 0), cell(R.NOTE_OFF)]]);
    for (let t = 1; t < 16; t++) for (let r = 0; r < 128; r++) cells.set(R.cellKey(r, t), cell(40 + t, 0x50, [[0x0A, 0x11], [0x04, 0x22]]));
    const pat = R.packPattern({ tracks: 16, rows: 128, step: 1, cells });
    pat.forEach(p => { p[1] = 199; });                          /* (200 rows, as before the rule) */
    const h = new Uint8Array(82);
    h.set([125, 0, 6, 50, 0, 1, 0, 0]);
    h.set([0x05, 0x01, 0, 0, 0x7F, 0x80, 0, 0, 0, 0], 72);
    const p0 = new Uint8Array(h.length + pat[0].length);
    p0.set(h); p0.set(pat[0], h.length);
    const recs = [{ kind: K.LOOPS, num: 0, sub: 0, ver: 3, payload: p0 }].concat(pat.slice(1).map((p, i) => ({ kind: K.LOOPS, num: 0, sub: i + 1, ver: 3, payload: p })));
    check(recs.length > 1, "a version 3 record in " + recs.length + " parts");
    const up = R.upgrade([{ kind: K.SONG, num: 1, sub: 0, ver: 1, payload: Uint8Array.of(9) }].concat(recs));
    check(up.length === 2 && up[0].kind === K.SONG && up[1].kind === K.LOOPS && up[1].ver === 4 && up[1].sub === 0 &&
        up[1].len === up[1].payload.length && up[1].crc === U.crc32(up[1].payload), "upgrade: its parts become one version 4 record, the rest kept");
    const l = R.unpackLoops([up[1].payload]);
    const s = up[1].payload.subarray(80 + 12 + 5);
    /* rows 0 and 100: stream rows 0 and 400, a gap of 399: bridged by ED0 at stream row 256 (lane 0, row 64) */
    check(l.length === 128 && l.loops[0].base === 128 && eq(s, [0x90, 60, 0x7F, 0x0F, 0x0E, 0xD0, 255, 0x87, 0x01, 143, 0x00]),
        "128 rows; the gap past a skip byte bridged by ED0 on its lane: " + hexs(s));
    check(l.loops[0].cells.size === 3 && eq(l.loops[0].cells.get("64,0").cmds[0], R.LOOP_FILL), "the bridge reads as a cell of ED0 alone");
}
/* MONO LEGATO LEGATO (loops.md: byte 4's bit 6, byte 2's bit 7 then 0): both ways, and named going down to version 3 */
{
    const setup = legato => ({ tempo: 125, ticks: 6, countIn: 0, legato, swing: 50, grid: 0, quantize: 0, nextMono: 0, selected: 0,
        length: 0, fx, mix: R.MIX_DEFAULT.slice(), loops: [] });
    const lg = R.packLoops(setup(2))[0], jp = R.packLoops(setup(1))[0];
    check(lg[2] === 6 && lg[4] === 0x40 && R.unpackLoops([lg]).legato === 2 && jp[2] === (6 | 0x80) && jp[4] === 0 &&
          R.unpackLoops([jp]).legato === 1, "MONO LEGATO: LEGATO byte 4's bit 6 (byte 2's bit 7 0), JUMP byte 2's bit 7");
    check(R.loopsV4to3([lg]).lost.join() === "MONO LEGATO LEGATO", "... LEGATO named going down to version 3");
}
/* the bridge a skip of 255 after the cell before it, whatever lane that row is (loops.md, "A loop's stream") */
{
    const one = cells => R.packLoops({ tempo: 125, ticks: 6, countIn: 0, legato: 0, swing: 50, grid: 0, quantize: 0, nextMono: 0, selected: 0,
        length: 128, fx, mix: R.MIX_DEFAULT.slice(), loops: [{ cart: 1, slot: 0, colour: 0, mono: 0, muted: false, soloed: false, lanes: 3,
            base: 128, repeat: 1, mixer: R.MIX_DEFAULT.slice(), cells: new Map(cells) }] });
    /* lane 1 of row 1 (stream row 5), lane 0 of row 100 (400): ED0 at 5 + 256 = 261 (lane 1 of row 65), then a skip of 138 */
    const a = one([[R.cellKey(1, 1), cell(60, 0x7F)], [R.cellKey(100, 0), cell(R.NOTE_OFF)]]);
    check(eq(a[0].subarray(97), [0x95, 60, 0x7F, 0x0F, 0x0E, 0xD0, 255, 0x87, 0x01, 138, 0x00]),
        "a gap from stream row 5 to 400: ED0 a skip of 255 on (row 261): " + hexs(a[0].subarray(97)));
    /* from the start: lane 2 of row 127 (510) alone: ED0 at stream row 255 (lane 3 of row 63), then a skip of 254 */
    const b = one([[R.cellKey(127, 2), cell(60, 0x7F)]]);
    check(eq(b[0].subarray(97), [0x0F, 0x0E, 0xD0, 255, 0x97, 60, 0x7F, 254, 0x00]),
        "a first cell at stream row 510: ED0 at row 255, whatever its lane: " + hexs(b[0].subarray(97)));
    const la = R.unpackLoops(a), lb = R.unpackLoops(b);
    check(la.loops[0].cells.size === 3 && eq(la.loops[0].cells.get("65,1").cmds[0], R.LOOP_FILL) &&
        lb.loops[0].cells.size === 2 && eq(lb.loops[0].cells.get("63,3").cmds[0], R.LOOP_FILL) && !lb.loops[0].cells.get("63,3").note,
        "the bridges read where they are (lane 1 of row 65, lane 3 of row 63)");
    check(eq(R.packLoops(la)[0], a[0]) && eq(R.packLoops(lb)[0], b[0]), "and code back byte for byte, no second bridge");
}

/* ---- version 4: 16 loops over several parts, decoded and coded back byte for byte ---- */
const L16 = { tempo: 128, ticks: 6, countIn: 2, legato: 1, swing: 54, grid: 2, quantize: 4, nextMono: 1, selected: 15, length: 128,
    fx, mix: [0x70, 0x90, 1, 2, 3, 4], loops: [] };
for (let i = 0; i < 16; i++) {
    const lanes = i % 4 + 1, cells = new Map();
    const base = i === 6 ? 128 : 64 * (1 + (i & 1));
    for (let r = 0; r < base; r++) for (let ln = 0; ln < lanes; ln++) {
        const c = (r + ln) % 5 === 0 ? cell(R.NOTE_OFF) : cell(36 + (r * 7 + ln * 3 + i) % 60, (r * 5 + i) & 127,
            (r + i) % 4 === 0 ? [[0x0E, 0xD0 | (r & 15)], [0x14, 3]] : (r + i) % 4 === 1 ? [[0x14, 1 + (r & 7)]] : []);
        cells.set(R.cellKey(r, ln), c);
    }
    if (i === 6) { cells.clear(); cells.set(R.cellKey(0, 1), cell(60, 0x7F)); cells.set(R.cellKey(127, 2), cell(R.NOTE_OFF)); }   /* a long gap */
    if (i === 9) cells.clear();                                 /* a loop with no notes: no stream */
    L16.loops.push({ cart: 0x10 + (i & 3), slot: i, colour: i % 12, mono: i === 3 ? 1 : 0, muted: i === 5, soloed: i === 7, lanes,   /* (loop 07's solo: never written) */
        base, repeat: 128 / base, mixer: [0x7F - i, 0x80 + i, i, 0, 0, i * 2], cells });
}
const parts16 = R.packLoops(L16);
check(parts16.length >= 4 && parts16.length <= 16 && parts16.every(p => p.length <= R.RECORD_MAX),
    "16 loops in " + parts16.length + " parts: " + parts16.map(p => p.length).join(" ") + " B");
const top16 = 80 + 12 * 16;
check(parts16[0][5] === 16 && parts16[0][7] === 128 && parts16[0][6] === 15 && parts16[0][2] === (6 | 2 << 5 | 0x80) && parts16[0][4] === (2 | 4 << 2 | 0x20),
    "the head: 16 loops, 128 rows, loop 0F selected, COUNT-IN 2 BARS with JUMP, 1/32 REAL TIME and MONO next");
let masks = 0, order = [], heads = true;
parts16.forEach((p, sub) => {
    const at = sub ? 0 : top16, m = p[at + 3] | p[at + 4] << 8;
    if (p[at] !== 16 || p[at + 1] !== 127 || p[at + 2] !== 0) heads = false;
    if (masks & m) heads = false;
    masks |= m;
    for (let i = 0; i < 16; i++) if (m >> i & 1) order.push(i);
});
check(heads && masks === (0xFFFF & ~(1 << 9)) && order.every((v, i) => !i || v > order[i - 1]),
    "every part repeats the payload's bytes 0-2; each stream once, in loop order, the empty loop's none: " + order.join(","));
const back16 = R.unpackLoops(parts16);
check(back16.n === 16 && back16.loops.every((l, i) => l.cells.size === L16.loops[i].cells.size + (i === 6 ? 1 : 0)),
    "decoded: every loop's cells (loop 06's gap bridged once)");
let same = true;
back16.loops.forEach((l, i) => { for (const [k, c] of L16.loops[i].cells) { const d = l.cells.get(k); if (!d || d.note !== c.note || d.vol !== c.vol || JSON.stringify(d.cmds) !== JSON.stringify(c.cmds)) same = false; } });
check(same, "decoded cells equal what was coded");
const again = R.packLoops(back16);
check(again.length === parts16.length && again.every((p, i) => eq(p, parts16[i])), "16 loops coded back byte for byte, every part");
const lpBack = back16.loops;
check(lpBack[3].mono === 1 && lpBack[5].muted && !lpBack[7].soloed && !(lpBack[7].raw[2] & 0x40) && lpBack[15].lanes === 4 && lpBack[0].lanes === 1 && lpBack[0].base === 64 &&
    lpBack[0].repeat === 2 && lpBack[1].base === 128 && lpBack[1].repeat === 1 && lpBack[10].mixer[5] === 20, "the loop table's fields read back");
/* a spare bit travels */
{
    const p = parts16.map(x => x.slice());
    p[0][4] |= 0x80; p[0][80 + 12 * 2 + 3] |= 0x80;     /* (byte 4's bit 6 is MONO LEGATO's LEGATO, its bit 7 spare) */
    const b = R.packLoops(R.unpackLoops(p));
    check(b.every((x, i) => eq(x, p[i])), "spare bits travel through a decode and a code");
}
/* a stream past 3,800 B refused */
{
    const big = { ...L16, loops: [Object.assign({}, L16.loops[15], { cells: new Map() })] };
    for (let r = 0; r < 128; r++) for (let ln = 0; ln < 4; ln++) big.loops[0].cells.set(R.cellKey(r, ln), cell(60, 0x7F, [[0x0E, 0xD1], [0x14, 2]]));
    let refused = "";
    try { R.packLoops(big); } catch (e) { refused = e.message; }
    check(/3800/.test(refused), "a stream past 3,800 B refused: " + refused);
}

/* ---- a cartridge renumbered by a move: the loop table's instruments rewritten in place ---- */
const M = ED.model;
M.setBuiltins([{ kind: K.DX7, num: 0xF1, sub: 0, ver: 1, payload: R.packCart(K.DX7, { name: "B", slots: [ED.defs.newSlot(K.DX7)] }) }]);
const tb = num => ({ kind: K.TB303, num, sub: 0, ver: 1, payload: R.packCart(K.TB303, { name: "BASSES", slots: Array.from({ length: 8 }, () => ED.defs.newSlot(K.TB303)) }) });
const other = num => ({ kind: K.TB303, num, sub: 0, ver: 1, payload: R.packCart(K.TB303, { name: "OTHER", slots: [ED.defs.newSlot(K.TB303)] }) });
{
    const T = M.fromRecords([tb(0x03), { kind: K.LOOPS, num: 0, sub: 0, ver: 4, payload: v4[0] }], { side: "L", src: "X" }).items;
    const loops = T.find(i => i.kind === K.LOOPS), cart = T.find(i => i.kind === K.TB303);
    check(M.kindText(loops) === "3 LOOPS" && M.groupOf(loops) === "SYSTEM", "the loops listed: " + M.kindText(loops));
    check((M.users(T).get(cart.id) || []).includes(loops.id) && loops.link.cF1 === M.BUILTIN, "the loops use the cartridge and the built-in F1");
    /* the FM-1 panel has another cartridge at 03: the loops' takes another number, their table rewritten */
    const r = M.transfer({ L: T, R: M.fromRecords([other(0x03)], { side: "R" }).items, base: [] }, "L", [loops.id], {});
    const moved = r.st.R.find(i => i.kind === K.LOOPS), c2 = r.st.R.find(i => i.kind === K.TB303 && i.num !== 0x03);
    const b = moved.recs[0].payload;
    check(r.ok && c2 && b[80] === c2.num && b[104] === c2.num && b[92] === 0xF1 && b.every((v, i) => i === 80 || i === 104 || v === v4[0][i]),
        "03 taken: the cartridge lands on " + M.H2(c2.num) + ", the loop table's 03s rewritten, every other byte the same");
    check(moved.link[M.lk("c", c2.num)] === c2.id, "the references follow");
    /* the 16 loops: part 0's table rewritten, the other parts untouched */
    const T16 = M.fromRecords([tb(0x11)].concat(parts16.map((p, sub) => ({ kind: K.LOOPS, num: 0, sub, ver: 4, payload: p }))), { side: "L", src: "Y" }).items;
    const r16 = M.transfer({ L: T16, R: M.fromRecords([other(0x11)], { side: "R" }).items, base: [] }, "L", [T16.find(i => i.kind === K.LOOPS).id], {});
    const l16 = r16.st.R.find(i => i.kind === K.LOOPS), n16 = r16.st.R.find(i => i.kind === K.TB303 && i.num !== 0x11).num;
    const now = l16.recs.map(x => x.payload);
    const at = [1, 5, 9, 13].map(i => 80 + 12 * i);
    check(at.every(i => now[0][i] === n16) && now[0].every((v, i) => at.includes(i) || v === parts16[0][i]) &&
        now.slice(1).every((p, i) => p === parts16[i + 1] || eq(p, parts16[i + 1])), "16 loops: 11 renumbered to " + M.H2(n16) + " in part 0's table, the other parts byte for byte");
}

/* ---- a version 3 record converted wherever records come in ---- */
{
    /* on the TABLE (a disk dropped) and on the FM-1 panel (READ, UPLOAD DISK) alike: converted when it comes in */
    for (const side of ["L", "R"]) {
        const items = M.fromRecords([{ kind: K.LOOPS, num: 0, sub: 0, ver: 3, payload: v3 }, { kind: K.SONG, num: 2, sub: 0, ver: 1, payload: R.packSong(R.newSong("A")) }], { side }).items;
        const l = items.find(i => i.kind === K.LOOPS);
        check(items.length === 2 && l.recs.length === 1 && l.recs[0].ver === 4 && eq(l.recs[0].payload, v4[0]) && !l.dim,
            (side === "L" ? "the TABLE" : "the FM-1 panel") + " holds version 3 loops converted to version 4");
    }
    /* read from an FM-1 holding version 3: the panel's version 4 is a change WRITE sends (the PC side converts) */
    const fromFm1 = M.fromRecords([{ kind: K.LOOPS, num: 0, sub: 0, ver: 3, payload: v3 }], { side: "R" }).items;
    const base = [Object.assign({}, fromFm1[0], { recs: [{ sub: 0, ver: 3, payload: v3 }] })];
    check(M.pending({ R: fromFm1, base }).count === 1, "the converted loops: one change for WRITE");
    /* a version 3 record coming in renumbered: converted, then rewritten */
    const T3 = M.fromRecords([{ kind: K.LOOPS, num: 0, sub: 0, ver: 3, payload: v3 }, tb(0x03)], { side: "L" }).items;
    const r3 = M.transfer({ L: T3, R: M.fromRecords([other(0x03)], { side: "R" }).items, base: [] }, "L", [T3.find(i => i.kind === K.LOOPS).id], {});
    const lr = r3.st.R.find(i => i.kind === K.LOOPS), n3 = r3.st.R.find(i => i.kind === K.TB303 && i.num !== 3).num;
    check(lr && lr.recs[0].ver === 4 && lr.recs[0].payload[80] === n3 && lr.recs[0].payload[104] === n3 && lr.recs[0].payload.every((v, i) => i === 80 || i === 104 || v === v4[0][i]),
        "a version 3 record renumbered: converted to version 4, its table rewritten");
    /* one that does not convert (cut short) is kept as it is */
    const bad = R.upgrade([{ kind: K.LOOPS, num: 0, sub: 0, ver: 3, payload: v3.subarray(0, 90) }]);
    check(bad.length === 1 && bad[0].ver === 3, "a version 3 record that does not read is kept as it is");
}

/* ---- down to version 3 (WRITE to an FM-1 that reads only it) ---- */
{
    /* what version 3 can hold goes down and comes back up byte for byte, nothing lost */
    const d = R.loopsV4to3(v4);
    const back = R.loopsV3(d.parts);
    check(d.lost.length === 0 && back.length === v4.length && back.every((p, i) => eq(p, v4[i])),
        "version 4 made from version 3: down and up again byte for byte, nothing lost (" + d.lost.join("; ") + ")");
    /* lanes, a base repeated over the length, MONO: a loop a lane, the base written out, the rest named */
    const L4 = R.unpackLoops(v4);
    L4.length = 32; L4.selected = 0;
    L4.loops = [{ cart: 1, slot: 2, colour: 0, mono: 1, muted: false, lanes: 2, base: 8, repeat: 3, mixer: [0x60, 0x80, 0, 0, 0x10, 0],
        cells: new Map([[R.cellKey(0, 0), cell(60, 0x7F)], [R.cellKey(4, 1), cell(64, -1, [[0x0E, 0xD2]])]]) },
        { cart: 3, slot: 0, colour: 1, mono: 0, muted: true, lanes: 1, base: 32, repeat: 1, mixer: R.MIX_DEFAULT.slice(),
        cells: new Map([[R.cellKey(31, 0), cell(R.NOTE_OFF)]]) }];
    const d2 = R.loopsV4to3(R.packLoops(L4));
    const L3 = R.unpackLoops(R.loopsV3(d2.parts));
    const rows = lp => [...lp.cells.keys()].map(k => +k.split(",")[0]).sort((a, b) => a - b).join(",");
    check(L3.n === 3 && L3.length === 32 && rows(L3.loops[0]) === "0,8,16" && rows(L3.loops[1]) === "4,12,20" && rows(L3.loops[2]) === "31" &&
        L3.loops[1].cells.get("12,0").cmds[0][1] === 0xD2 && L3.loops[0].cart === 1 && L3.loops[1].slot === 2 && L3.loops[2].muted &&
        eq(L3.loops[1].mixer, [0x60, 0x80, 0, 0, 0x10, 0]),
        "a loop of 2 lanes, a base of 8 rows 3 times over 32: a loop a lane, the notes where they play (" + L3.loops.map(rows).join(" / ") + ")");
    check(d2.lost.includes("MONO (PLAYED POLY)") && d2.lost.length === 1, "what version 3 cannot hold, named: " + d2.lost.join("; "));
    /* the records of an object: down a step at a time; no way down: null */
    const dg = R.downgrade(R.packLoops(L4).map((payload, sub) => ({ kind: K.LOOPS, num: 0, sub, ver: 4, payload })), 3);
    check(dg && dg.recs.every(r => r.ver === 3 && r.kind === K.LOOPS) && dg.lost.length === 1 && R.canDowngrade(K.LOOPS, 4, 3) &&
        !R.canDowngrade(K.LOOPS, 4, 2) && !R.canDowngrade(K.SONG, 1, 0) && R.downgrade([{ kind: K.SONG, num: 0, sub: 0, ver: 2, payload: new Uint8Array(4) }], 1) === null,
        "downgrade: the loops 4 -> 3; no way down elsewhere");
}
done("loops");

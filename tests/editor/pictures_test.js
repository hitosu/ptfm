/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The editor's pictures (editor/wasm/ed_pics.c) against the FM-1's own screen: the firmware's WebAssembly (with the
 * editor's part, -DPTFM_EDITOR) runs as the virtual FM-1 with the demo disk laid into its flash; its UI is driven as
 * the panel drives it (buttons, knobs and keys, frames rendered, the rows plat_lcd_rows got) to each picture, and the
 * same picture drawn by ed_picture / ed_fx_picture from the item's bytes must equal the screen's rectangle pixel for
 * pixel (and be black outside it; the virtual FM-1's screen put back as it was). Every engine's browsing details and
 * edit-mode pictures (DX7 on every page with its operators, Juno-106, TB-303, CHIP's four pages with each oscillator and
 * each TABLES row, its rows' small tables, FUSION, a sample instrument's pictures, its waveform, the samples' list's),
 * and FX EDIT's picture of every effect for three FX setups (a song's, everything on, the other routing). Pictures go
 * to build/editor/pictures/ as PNGs (the FM-1's screen beside each that differs).
 *
 *   node tests/editor/pictures_test.js WASM */
"use strict";
const fs = require("fs"), path = require("path"), zlib = require("zlib");
const ROOT = path.join(__dirname, "..", "..");
const S = require(path.join(ROOT, "fw", "web", "seed.js"));
const wasmPath = process.argv[2] || path.join(ROOT, "build", "editor", "wasm", "ptfm.wasm");
const OUT = path.join(ROOT, "build", "editor", "pictures");
fs.mkdirSync(OUT, { recursive: true });

let fails = 0, checks = 0;
function check(ok, what) {
    checks++;
    if (!ok) { fails++; console.log("FAIL: " + what); }
    else if (process.env.VERBOSE) console.log("ok: " + what);
}

/* ---- the FM-1's ids (fw/core/plat.h), the kinds (store.h) ---- */
const B = { OCTM: 0, OCTP: 1, FX: 2, SEL: 3, ENV: 4, LFO: 5, EDIT: 6, GLO: 7, HOME: 8, SAVE: 9, ARP: 10, SEQ: 11, PLAY: 12, REC: 13 };
const KN = { SELECT: 0, ALG: 1, K1: 2, K2: 3, K3: 4, K4: 5, PRESETS: 6 };
const OP3 = 5;                                          /* the black key A#3: INSTRUMENTS */
const K = { DX7: 1, JUNO: 2, TB303: 3, SAMPLER: 4, FUSION: 5, CHIP: 6, AUDIO: 0x10, SONG: 0x21 };
const SLOT_SIZE = { 1: 112, 2: 28, 3: 24, 4: 40, 5: 16, 6: 28 };
const NAME_BIT = { 1: 784, 2: 123, 3: 42, 4: 174, 5: 8, 6: 121 };
const NAME_CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:.-?|/\\*#$@%";
const PIC = { DETAILS: 0, EDIT: 1, CHIP_ROW: 2, WAVE: 3, SAMPLES: 4 };
const FX_NAMES = ["GATE", "FILTER", "DRIVE", "LO-FI", "CHORUS", "DELAY", "REVERB", "LIMITER", "DUCK", "METRONOME", "SENDS"];
const bits = (b, pos, n) => { let v = 0; for (let i = 0; i < n; i++) if (b[(pos + i) >> 3] >> ((pos + i) & 7) & 1) v |= 1 << i; return v; };
const slotName = (k, s) => {
    let t = "";
    for (let i = 0; i < 10; i++) { const c = bits(s, NAME_BIT[k] + 6 * i, 6); t += c < NAME_CHARS.length ? NAME_CHARS[c] : " "; }
    return t.replace(/ +$/, "");
};
const used = (k, s) => bits(s, NAME_BIT[k], 6) !== 63;
const hex2 = v => (v < 16 ? "0" : "") + v.toString(16).toUpperCase();

/* ---- a PNG of a 240 x 240 RGB565 screen ---- */
function png(file, px) {
    const raw = Buffer.alloc(240 * (1 + 240 * 3));
    for (let y = 0; y < 240; y++) {
        raw[y * 721] = 0;
        for (let x = 0; x < 240; x++) {
            const c = px[y * 240 + x], o = y * 721 + 1 + x * 3;
            raw[o] = (c >> 11) * 255 / 31 | 0; raw[o + 1] = (c >> 5 & 63) * 255 / 63 | 0; raw[o + 2] = (c & 31) * 255 / 31 | 0;
        }
    }
    const chunk = (type, data) => {
        const td = Buffer.concat([Buffer.from(type, "ascii"), data]), len = Buffer.alloc(4), crc = Buffer.alloc(4);
        len.writeUInt32BE(data.length); crc.writeUInt32BE(S.crc32(td));
        return Buffer.concat([len, td, crc]);
    };
    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(240, 0); ihdr.writeUInt32BE(240, 4); ihdr[8] = 8; ihdr[9] = 2;
    fs.writeFileSync(path.join(OUT, file), Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk("IHDR", ihdr),
        chunk("IDAT", zlib.deflateSync(raw)), chunk("IEND", Buffer.alloc(0))]));
}

/* ---- the demo disk, and the flash laid out as one save writes it (specs/files/store.md#a-record): records packed
 * from the user area's start, none crossing a block; audio after them, from a block of its own, packed ---- */
const disk = S.parseData(new Uint8Array(fs.readFileSync(path.join(ROOT, "dist", "firmware", "demo.ptfmdsk"))));
if (disk.problems.length) throw new Error("the demo disk: " + disk.problems.join("; "));
function layFlash(fl, files) {
    let at = 0x93000, last = -1;
    const put = f => {
        const size = 16 + f.payload.length, audio = f.kind === K.AUDIO;
        if (!audio && (at & 4095) + size > 4096) at = (at + 4095) & ~4095;
        fl[at] = f.kind; fl[at + 1] = f.num; fl[at + 2] = f.sub; fl[at + 3] = f.ver;
        fl[at + 4] = f.payload.length & 255; fl[at + 5] = f.payload.length >> 8 & 255; fl[at + 6] = f.payload.length >> 16 & 255;
        fl[at + 7] = 0; fl[at + 8] = 1; fl[at + 9] = fl[at + 10] = fl[at + 11] = 0;       /* save 1 */
        fl.set(f.payload, at + 16);
        last = at;
        at = (at + size + 3) & ~3;
        if (at > 0xE0000) throw new Error("the disk does not fit the user area's first region");
    };
    const crc = a => {
        const len = fl[a + 4] | fl[a + 5] << 8 | fl[a + 6] << 16;
        let c = S.crc32(fl.subarray(a, a + 12));
        c = S.crc32(fl.subarray(a + 16, a + 16 + len), c);
        fl[a + 12] = c & 255; fl[a + 13] = c >>> 8 & 255; fl[a + 14] = c >>> 16 & 255; fl[a + 15] = c >>> 24;
    };
    const heads = [];
    for (const f of files) if (f.kind !== K.AUDIO) { put(f); heads.push(last); }
    at = (at + 4095) & ~4095;
    for (const f of files) if (f.kind === K.AUDIO) { put(f); heads.push(last); }
    fl[last + 7] |= 1;                                  /* the save's last record */
    for (const a of heads) crc(a);
}

/* ---- the virtual FM-1: an instance of the WebAssembly, powered on with a flash image ---- */
const wasmModule = new WebAssembly.Module(fs.readFileSync(wasmPath));
function boot(files) {
    let fake = 0;
    const ex = new WebAssembly.Instance(wasmModule, { env: { web_clock_us: () => (fake += 1) } }).exports;
    ex.web_flash_reset();
    layFlash(new Uint8Array(ex.memory.buffer, ex.web_flash(), 1 << 20), files);
    ex.web_init();
    const fm = { ex };
    fm.run = ms => { for (let n = Math.round(ms * 44.1); n > 0; n -= 1024) ex.web_render(Math.min(1024, n)); };
    fm.press = b => { ex.web_button(b, 1); fm.run(30); };
    fm.release = b => { ex.web_button(b, 0); fm.run(30); };
    fm.tap = b => { fm.press(b); fm.release(b); fm.run(200); };
    fm.key = (n, down) => { ex.web_note(n, down); fm.run(30); };
    fm.turn = (k, clicks) => { for (let i = 0; i < Math.abs(clicks); i++) { ex.web_knob(k, clicks < 0 ? -1 : 1); fm.run(120); } };
    fm.homeKey = n => { fm.press(B.HOME); fm.key(n, 1); fm.key(n, 0); fm.release(B.HOME); fm.run(200); };
    fm.screen = () => new Uint16Array(ex.memory.buffer, ex.web_fb(), 240 * 240).slice();
    fm.run(300);
    return fm;
}

/* the built-in instruments' records (ed_glue.c's), and the disk's cartridges: kind and payload by number */
function cartridges(ex) {
    const carts = new Map();
    for (let i = 0; i < ex.ed_builtin_count(); i++) {
        const h = new Uint8Array(ex.memory.buffer, ex.ed_builtin(i), 12), dv = new DataView(h.buffer, h.byteOffset, 12);
        if (h[0] >= K.DX7 && h[0] <= K.CHIP)
            carts.set(h[1], { kind: h[0], payload: new Uint8Array(ex.memory.buffer, dv.getUint32(8, true), dv.getUint32(4, true)).slice() });
    }
    for (const f of disk.files) if (f.kind >= K.DX7 && f.kind <= K.CHIP) carts.set(f.num, { kind: f.kind, payload: f.payload });
    for (const c of carts.values()) {
        c.n = c.payload[9];
        c.head = c.kind === K.SAMPLER ? 11 : 10;
        c.slot = i => c.payload.slice(c.head + SLOT_SIZE[c.kind] * i, c.head + SLOT_SIZE[c.kind] * (i + 1));
        c.first = 0;
        while (c.first < c.n - 1 && !used(c.kind, c.slot(c.first))) c.first++;
    }
    return carts;
}

/* a picture's answer: its rectangle and pixels */
function answer(ex, ptr) {
    const r = new Int32Array(ex.memory.buffer, ptr, 4);
    return { x: r[0], y: r[1], w: r[2], h: r[3], px: new Uint16Array(ex.memory.buffer, ptr + 16, 240 * 240).slice() };
}
/* the picture against the screen: its rectangle equal (but `skip`, a rectangle the FM-1 draws over it), black outside;
 * the screen untouched by drawing it. Returns whether it is equal. */
let shots = 0;
function compare(fm, a, screen, what, skip, quiet) {
    const after = fm.screen();
    let same = a.w > 0 && a.h > 0, outside = 0, diff = 0, first = "";
    for (let y = 0; y < 240; y++) for (let x = 0; x < 240; x++) {
        const i = y * 240 + x, inside = x >= a.x && x < a.x + a.w && y >= a.y && y < a.y + a.h;
        const skipped = skip && x >= skip.x && x < skip.x + skip.w && y >= skip.y && y < skip.y + skip.h;
        if (!inside) { if (a.px[i]) outside++; continue; }
        if (!skipped && a.px[i] !== screen[i]) { if (!diff) first = `${x},${y}: ${a.px[i].toString(16)} for ${screen[i].toString(16)}`; diff++; }
    }
    same = same && !diff;
    if (!quiet) {
        const name = String(++shots).padStart(3, "0") + "_" + what.replace(/[^A-Z0-9]+/gi, "_").toLowerCase();
        check(a.w > 0 && a.h > 0, `${what}: a picture (${a.x}, ${a.y}, ${a.w} x ${a.h})`);
        check(!diff, `${what}: equal to the FM-1's screen in (${a.x}, ${a.y}, ${a.w} x ${a.h})` + (diff ? `: ${diff} pixels differ, first ${first}` : ""));
        check(!outside, `${what}: black outside its rectangle (${outside} pixels not)`);
        check(after.every((v, i) => v === screen[i]), `${what}: the virtual FM-1's screen put back`);
        png(name + ".png", a.px);
        if (diff) png(name + "_fm1.png", screen);
    }
    return same;
}
/* the picture has something in it (not all black: a picture that draws nothing proves nothing) */
const drawn = a => { let n = 0; for (let y = a.y; y < a.y + a.h; y++) for (let x = a.x; x < a.x + a.w; x++) if (a.px[y * 240 + x]) n++; return n; };

/* ============================================================== the instruments ============================== */
const fm = boot(disk.files);
const ex = fm.ex, carts = cartridges(ex);
const mem = () => new Uint8Array(ex.memory.buffer);
const order = [...carts.keys()].sort((a, b) => a - b);  /* (ui_carts' order: the user's, then the built-in ones) */
let cur = { cart: 0xF0, slot: 0 };                     /* the current instrument at power-on: F0:00 */
check(carts.has(0xF0) && carts.get(0xF0).kind === K.DX7, "F0 is a DX7 cartridge (the current instrument at power-on)");
fm.homeKey(OP3);
fm.run(100);
function toCart(num) {                                  /* K1: a cartridge at a time, at its first used slot */
    const d = order.indexOf(num) - order.indexOf(cur.cart);
    fm.turn(KN.K1, d);
    cur = { cart: num, slot: carts.get(num).first };
    fm.run(100);
}
function toSlot(s) { fm.turn(KN.SELECT, s - cur.slot); cur.slot = s; fm.run(100); }
function picture(what, page, group, bytes, kind, inst, octave) {
    const c = carts.get(cur.cart), s = bytes || c.slot(cur.slot);
    mem().set(s, ex.ed_pic_buf());
    return answer(ex, ex.ed_picture(what, kind || c.kind, s.length, inst === undefined ? cur.cart << 8 | cur.slot : inst, page, group, octave || 0));
}
const label = () => `${hex2(cur.cart)}:${hex2(cur.slot)} ${slotName(carts.get(cur.cart).kind, carts.get(cur.cart).slot(cur.slot))}`;
const counts = {};
let rowsDrawn = 0;
const rowsWith = new Set();
function same(what, page, group) {                      /* the picture of the current instrument against the screen now */
    const screen = fm.screen();
    const a = picture(what, page, group);
    const k = carts.get(cur.cart).kind;
    const tag = ["", "DX7", "JUNO-106", "TB-303", "SAMPLER", "FUSION", "CHIP"][k] + " " + ["DETAILS", "EDIT", "CHIP ROW", "WAVE", "SAMPLES"][what];
    counts[tag] = (counts[tag] || 0) + 1;
    const ok = compare(fm, a, screen, `${label()} ${tag}` + (what === PIC.DETAILS ? "" : ` page ${page} group ${group}`));
    if (what === PIC.CHIP_ROW) { if (drawn(a)) { rowsDrawn++; rowsWith.add(group); } }   /* (a row without a table is empty,
                                                          * there as here) */
    else check(drawn(a) > 0, `${label()} ${tag} page ${page} group ${group}: something drawn`);
    /* the next frames: the FM-1's own screen there again, unchanged by the picture */
    fm.run(70);
    const again = fm.screen();
    let back = true;
    for (let y = a.y; y < a.y + a.h; y++) for (let x = a.x; x < a.x + a.w; x++) if (again[y * 240 + x] !== screen[y * 240 + x]) back = false;
    check(back, `${label()} ${tag}: the FM-1's next frames show the same as before the picture`);
    return ok;
}
/* edit mode on the current instrument: each page (ALGORITHM), the groups asked (SELECT), each checked */
function editPages(groupsOf, rows) {
    fm.tap(B.EDIT);
    fm.run(100);
    const pages = groupsOf.length;
    for (let p = 0; p < pages; p++) {
        if (p) { fm.turn(KN.ALG, 1); fm.run(100); }
        let g = 0;
        for (const want of groupsOf[p]) {
            fm.turn(KN.SELECT, want - g); g = want;
            fm.run(100);
            same(PIC.EDIT, p, g);
            if (rows && p === 2) same(PIC.CHIP_ROW, p, g);
        }
    }
    fm.tap(B.HOME);                                     /* (out of edit mode; nothing changed) */
    fm.run(100);
}

/* --- DX7: F0, a few slots' details; every page's groups on one, the operators' highlight and envelopes --- */
const range = n => Array.from({ length: n }, (_, i) => i);
for (const s of [0, 1, 2, 5]) { toSlot(s); same(PIC.DETAILS, 0, 0); }
toSlot(0);
editPages([range(6), range(12), range(4), range(3)]);
{   /* another algorithm: the first slot of F1 whose algorithm differs from F0:00's */
    toCart(0xF1);
    const c = carts.get(0xF1), a0 = bits(carts.get(0xF0).slot(0), 678 + 56, 5);
    let s = c.first;
    while (s < c.n - 1 && (!used(K.DX7, c.slot(s)) || bits(c.slot(s), 678 + 56, 5) === a0)) s++;
    toSlot(s);
    same(PIC.DETAILS, 0, 0);
    editPages([[0, 3, 5], [7], [0], [0]]);
}
/* the bytes are the page's, not the store's: the instrument drawn with its algorithm changed differs from the screen */
{
    const s = carts.get(cur.cart).slot(cur.slot).slice();
    const screen = fm.screen();
    const a0 = picture(PIC.DETAILS, 0, 0);
    s[Math.floor((678 + 56) / 8)] ^= 1 << ((678 + 56) % 8);    /* (the algorithm's lowest bit) */
    const a1 = picture(PIC.DETAILS, 0, 0, s);
    check(compare(fm, a0, screen, "", null, true) && !compare(fm, a1, screen, "", null, true), "DX7 details: drawn from the bytes given (an algorithm changed shows)");
}

/* --- Juno-106 (F8), TB-303 (FA): details, every edit page --- */
for (const num of order.filter(n => n >= 0xF0 && carts.get(n).kind === K.JUNO).slice(0, 1)) {
    toCart(num);
    for (const s of [cur.slot, cur.slot + 3]) { toSlot(s); same(PIC.DETAILS, 0, 0); }
    editPages([[0, 1], [0, 1], [0, 2], [0, 1]]);
}
for (const num of order.filter(n => carts.get(n).kind === K.TB303).slice(0, 1)) {
    toCart(num);
    for (const s of [cur.slot, cur.slot + 1, cur.slot + 7]) { toSlot(s); same(PIC.DETAILS, 0, 0); }
    editPages([[0, 1, 2, 3]]);
}

/* --- CHIP: an instrument of each oscillator (PULSE, TRIANGLE, WAVE, BUZZ, NOISE): details, its four pages, every
 * TABLES row's picture and its small table --- */
for (const num of order.filter(n => carts.get(n).kind === K.CHIP).slice(0, 1)) {
    toCart(num);
    const c = carts.get(num), seen = new Set();
    for (let s = 0; s < c.n; s++) {
        const b = c.slot(s), osc = bits(b, 0, 3);
        if (!used(K.CHIP, b) || seen.has(osc)) continue;
        seen.add(osc);
        toSlot(s);
        same(PIC.DETAILS, 0, 0);
        editPages([[0, 2], [0, 1], range(6), [0]], true);
    }
    check(seen.size === 5, "CHIP: an instrument of every oscillator tried (" + [...seen].join(" ") + ")");
    /* an envelope of the PSG's, a vibrato, tables with loops: the first slots with them */
    const pick = f => { for (let s = 0; s < c.n; s++) if (used(K.CHIP, c.slot(s)) && f(c.slot(s))) return s; return -1; };
    const psg = pick(b => bits(b, 36, 2) === 3), vib = pick(b => bits(b, 116, 5) > 0);
    for (const s of [psg, vib]) if (s >= 0) { toSlot(s); same(PIC.DETAILS, 0, 0); editPages([[0], [0, 1], range(6), [0]], true); }
    check(psg >= 0 && vib >= 0, `CHIP: a PSG envelope (${psg}) and a vibrato (${vib}) tried`);
    /* a table of each kind (VOL, ARP, PITCH, DUTY, NOISE: TABLE at bit 69 + 8 x kind): the first instrument with one, on
     * its TABLES page */
    const kinds = [];
    for (let t = 0; t < 5; t++) {
        const s = pick(b => bits(b, 69 + 8 * t, 6) > 0);
        kinds.push(s);
        if (s >= 0) { toSlot(s); editPages([[], [], range(6)], true); }
    }
    check(kinds.every(s => s >= 0), "CHIP: an instrument with a table of each kind tried: slots " + kinds.join(" "));
    check([0, 1, 2, 3, 4, 5].every(g => rowsWith.has(g)), `CHIP: every TABLES row drawn with something in it (${[...rowsWith].sort().join(" ")}; ${rowsDrawn} rows in all)`);
}

/* --- FUSION (06): every slot's details, edit mode on those with a pattern --- */
for (const num of order.filter(n => carts.get(n).kind === K.FUSION).slice(0, 1)) {
    toCart(num);
    const c = carts.get(num);
    for (let s = 0; s < Math.min(c.n, 8); s++) {
        if (!used(K.FUSION, c.slot(s))) continue;
        toSlot(s);
        same(PIC.DETAILS, 0, 0);
        if (c.slot(s)[0] !== 0xFF) editPages([[0, 1]]);
    }
    /* another octave: FUSION's band of the keys' rows moves (OCT+), the picture asked at that octave */
    toSlot(c.first);
    fm.tap(B.OCTP);
    fm.run(2200);                                       /* (past the toast: the header only, but quiet all the same) */
    const screen = fm.screen(), a = picture(PIC.DETAILS, 0, 0, null, null, undefined, 1);
    compare(fm, a, screen, `${label()} FUSION DETAILS at OCT +1`);
    check(!compare(fm, picture(PIC.DETAILS, 0, 0), screen, "", null, true), "FUSION: the octave given moves the keys' band");
    fm.tap(B.OCTM);
    fm.run(2200);
}

/* --- sample instruments (01-05): details, both edit pages, the waveform, the samples' list's waveform --- */
let samplerLoops = 0;
for (const num of order.filter(n => n < 0xF0 && carts.get(n).kind === K.SAMPLER).slice(0, 3)) {
    toCart(num);
    const c = carts.get(num);
    const loop = (() => { for (let s = 0; s < c.n; s++) if (used(K.SAMPLER, c.slot(s)) && bits(c.slot(s), 86, 2)) return s; return -1; })();
    for (const s of [c.first, loop].filter((s, i, a) => s >= 0 && a.indexOf(s) === i)) {
        toSlot(s);
        same(PIC.DETAILS, 0, 0);
        fm.tap(B.EDIT); fm.run(150);
        same(PIC.EDIT, 0, 0);
        same(PIC.WAVE, 0, 0);
        {   /* the bytes are the page's: START moved (bits 6-25) moves its marker in the waveform */
            const b = c.slot(s).slice(), screen = fm.screen(), len = bits(b, 26, 20) || 1000;
            const start = Math.floor(len / 3);
            for (let i = 0; i < 20; i++) { const p = 6 + i; b[p >> 3] = b[p >> 3] & ~(1 << (p & 7)) | (start >> i & 1) << (p & 7); }
            check(!compare(fm, picture(PIC.WAVE, 0, 0, b), screen, "", null, true), `${label()} WAVE: drawn from the bytes given (START moved shows)`);
        }
        fm.turn(KN.SELECT, 2); fm.run(100);
        same(PIC.EDIT, 0, 2);
        fm.turn(KN.SELECT, -2); fm.run(100);
        fm.tap(B.ENV); fm.run(150);                      /* the samples' list, its cursor on the instrument's sample */
        same(PIC.SAMPLES, 0, 0);
        fm.tap(B.HOME); fm.run(100);                     /* (the list closed, nothing picked) */
        fm.turn(KN.ALG, 1); fm.run(100);
        same(PIC.EDIT, 1, 1);
        fm.tap(B.HOME); fm.run(100);
    }
    if (loop >= 0) samplerLoops++;
}
check(samplerLoops > 0, `a sample instrument with a loop tried (${samplerLoops} cartridges had one)`);
{   /* the WAVE picture of an instrument made up for a sample alone (its cut as START / END): drawn, its markers there */
    const num = order.find(n => carts.get(n).kind === K.SAMPLER), c = carts.get(num), s = c.slot(c.first).slice();
    const a = picture(PIC.WAVE, 0, 0, s, K.SAMPLER, num << 8);
    check(a.w === 240 && a.h === 72 && drawn(a) > 0, `a sample's waveform alone: drawn (${a.x}, ${a.y}, ${a.w} x ${a.h})`);
    const none = picture(PIC.WAVE, 0, 0, s, K.JUNO, num << 8);
    check(none.w === 0, "WAVE of a Juno-106 instrument: no such picture");
}

/* ================================================================ FX EDIT ==================================== */
/* the keys' song FE (what FX edits while nothing has played since power-on) laid as a copy of the demo's first song with
 * the FX setup and the mixer given; every item of FX EDIT's list then against ed_fx_picture */
const songs = disk.files.filter(f => f.kind === K.SONG).sort((a, b) => a.num - b.num);
const song0 = songs[0].payload;
const songParts = b => { const npos = b[20] + 1, at = 21 + npos, nmix = b[at]; return { npos, at, nmix, fx: at + 1 + 6 * nmix }; };
function songWith(fx, mixer) {
    const p = songParts(song0), out = new Uint8Array(p.at + 1 + 6 * 16 + 64 + 8);
    out.set(song0.subarray(0, p.at));
    out[p.at] = 16;
    for (let t = 0; t < 16; t++) out.set(mixer[t], p.at + 1 + 6 * t);
    out.set(fx, p.at + 1 + 96);
    out.set(song0.subarray(p.fx + 64, p.fx + 72), p.at + 1 + 96 + 64);
    return out;
}
const patterns = new Map();
for (const f of disk.files) if (f.kind === 0x20 && f.sub === 0) patterns.set(f.num, f.payload);
const tracksOf = b => { const p = songParts(b); let most = 0; for (let i = 0; i < p.npos; i++) { const q = patterns.get(b[21 + i]); if (q && q[0] > most) most = q[0]; } return most; };
const chainOf = fx => { const c = []; for (let i = 0; i < 12; i++) { const v = fx[1 + (i >> 1)] >> (4 * (i & 1)) & 15; if (v === 15) break; c.push(v); } return c; };
function itemsOf(fx) {                                  /* scr_fx.c's items(): the list FX EDIT shows */
    const out = [], sends = fx[0] !== 0;
    for (const e of chainOf(fx)) {
        if (sends ? e === 4 || e === 5 || e === 6 : e === 10) continue;
        out.push(e);
        if (e === 10) out.push(4, 5, 6);
    }
    return out.concat([8, 9]);
}
const fx0 = song0.slice(songParts(song0).fx, songParts(song0).fx + 64);
const mixer0 = range(16).map(t => { const p = songParts(song0); return t < p.nmix ? Array.from(song0.subarray(p.at + 1 + 6 * t, p.at + 7 + 6 * t)) : [0x7F, 0x80, 0, 0, 0, 0]; });
const DUCK_SOURCE = 16 + 0xAA - 0x90;                   /* (its byte: DUCK's first knob's code, 0xAA) */
const mixDuck = mixer0.map((m, t) => { const n = m.slice(); n[2] = [0, 127, 64, 0, 100, 30, 0, 0, 90][t] || 0; return n; });
const allOn = fx0.slice(), otherRouting = fx0.slice();
allOn[7] = 0xFF; allOn[8] = 0x03; allOn[DUCK_SOURCE] = 16;                  /* every effect on; DUCK's SOURCE the keys */
otherRouting[0] = fx0[0] ? 0 : 1; otherRouting[7] = 0xFF; otherRouting[8] = 0x03; otherRouting[DUCK_SOURCE] = 15;   /* (a track no
                                                          * pattern of the song has: no instrument named) */
/* every knob turned somewhere (bytes 16-53, clamped as the FM-1 clamps them), everything on: FILTER cutting with its
 * envelope (its SOURCE the keys: still while none plays), the METRONOME at a tempo of its own */
const turned = allOn.slice();
for (let b = 16, r = 7; b < 54; b++) { r = (r * 1103515245 + 12345) & 0x7FFFFFFF; turned[b] = r >> 16 & 127; }
turned[16 + 0x94 - 0x90] = 40;                          /* FILTER's CUT: LP 24 (its byte: CUT's code 0x94) */
turned[16 + 0x95 - 0x90] = 70;                          /* its RESO: a peak */
turned[16 + 0xB1 - 0x90] = 0;                           /* FILTER's envelope's SOURCE: the keys */
turned[DUCK_SOURCE] = 1;
turned[9] = 140; turned[10] = 0; turned[11] = 3;        /* METRONOME: 140 BPM, 3 beats */
const fxCases = [["THE SONG'S SETUP", fx0, mixer0], ["EVERYTHING ON", allOn, mixDuck], ["THE OTHER ROUTING", otherRouting, mixDuck],
                 ["KNOBS TURNED", turned, mixDuck]];
/* the names an instrument can have here (DUCK's source on the song's own setup: the first its track's cells name) */
const allNames = (() => {
    const names = new Set([""]);
    for (const c of carts.values()) for (let s = 0; s < c.n; s++) if (used(c.kind, c.slot(s))) names.add(slotName(c.kind, c.slot(s)));
    return [...names];
})();
const fxCount = {};
for (const [title, fx, mixer] of fxCases) {
    const fe = songWith(fx, mixer);
    const files = disk.files.concat([{ kind: K.SONG, num: 0xFE, sub: 0, ver: 1, payload: fe }]);
    const f = boot(files), fex = f.ex, fmem = () => new Uint8Array(fex.memory.buffer), buf = fex.ed_pic_buf();
    const name0 = slotName(K.DX7, cartridges(fex).get(0xF0).slot(0));
    const ntrk = tracksOf(fe), bpm10 = Math.floor((fe[12] | fe[13] << 8) * 240 / (fe[14] * fe[15]));
    const items = itemsOf(fx);
    check(fx[DUCK_SOURCE] < ntrk || fx[DUCK_SOURCE] >= 15, `${title}: DUCK's source (${fx[DUCK_SOURCE]}) and the song's ${ntrk} tracks`);
    f.tap(B.FX); f.run(100);
    f.tap(B.SEQ); f.run(150);                           /* LIVE -> EDIT */
    for (let i = 0; i < items.length; i++) {
        if (i) { f.turn(KN.SELECT, 1); f.run(100); }
        const e = items[i], screen = f.screen();
        fmem().set(fx, buf + 256);
        const nk = fex.ed_fx_setup(e);
        const fields = new Int32Array(fex.memory.buffer, buf, 37);
        fields[18] = bpm10;
        /* what the page would put in beside the setup: DUCK's tracks, DCKs, its source's name; the METRONOME's beat as it
         * clicks now (tried: the screen shows the one it clicked last) */
        let names = [""], beats = [-1];
        if (e === 8) {
            fields[35] = ntrk;
            for (let t = 0; t < 16; t++) fields[19 + t] = mixer[t][2];
            names = fx[DUCK_SOURCE] >= 16 ? [name0] : fx[DUCK_SOURCE] >= ntrk ? [""] : allNames;   /* (the keys: the current
                                                          * instrument, F0:00 at power-on) */
        }
        if (e === 9) beats = range(17).map(b => b - 1);
        const pic = (name, beat) => {
            const b = fmem();
            b.fill(0, buf + 148, buf + 164);
            b.set(Buffer.from(name.slice(0, 15), "ascii"), buf + 148);
            new Int32Array(fex.memory.buffer, buf, 37)[17] = beat;
            return answer(fex, fex.ed_fx_picture(e));
        };
        const kp = nk > 4 ? { x: 194, y: fields[1], w: 46, h: 12 } : null;   /* (the knob page the FM-1 draws over it) */
        let best = null;
        for (const beat of beats) for (const name of names) if (!best && compare(f, pic(name, beat), screen, "", kp, true)) best = [name, beat];
        const a = best ? pic(best[0], best[1]) : pic(names[0], beats[0]);
        const what = `FX ${title}: ${FX_NAMES[e]}` + (best && e === 8 && best[0] ? ` (source ${best[0]})` : "") + (best && e === 9 ? ` (beat ${best[1]})` : "");
        compare(f, a, screen, what, kp);
        if (e < 10) check(drawn(a) > 0, `${what}: something drawn`);
        fxCount[FX_NAMES[e]] = (fxCount[FX_NAMES[e]] || 0) + 1;
    }
}
check(FX_NAMES.every(n => fxCount[n] >= 2), "every effect's picture checked: " + JSON.stringify(fxCount));

console.log("pictures checked: " + JSON.stringify(counts));
console.log(`pictures: ${checks - fails} of ${checks} checks passed (PNGs in ${path.relative(ROOT, OUT)})`);
process.exit(fails ? 1 : 0);

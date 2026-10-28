/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM's seeding module (specs/files/blob.md): one module for the install page and the emulator. It
 * reads a disk (specs/files/disk.md) or a blob, asks the device for its layout, plans where every record
 * goes, and writes the blocks. The device's side is the firmware's own code (fw/core/store/seed.c).
 *
 * A classic script: in a page it defines window.PTFM_seed; in Node, require() returns the same object.
 *
 * API
 *   PTFM_seed.parseData(bytes)            a .ptfmdsk disk ("PTFMDISK") or a blob ("PTFMDATA"), Uint8Array ->
 *       { kind: "disk"|"blob", files: [{ kind, num, sub, ver, len, crc, payload, label }], notes, problems }
 *       files of the disk-only types (0xE0-0xEF) are left out of files; notes holds the 0xE0 text.
 *   PTFM_seed.midiTransport(input, output)   a transport over Web MIDI (an access with sysex: true)
 *   transport = { send(Uint8Array), onMessage(cb) }   cb(Uint8Array) gets every whole SysEx message
 *   PTFM_seed.session(transport, opts?) ->
 *       hello()                    -> Promise<layout>; rejects with an Error whose .code is the device's
 *                                     result (e.g. RESULT.UNSAVED: "SAVE OR REVERT FIRST" on the FM-1)
 *       write(plan, onProgress)    -> Promise<{ records, bad, lastSave }> once the device has mounted;
 *                                     onProgress({ done, total, block }) after every block
 *       abort()                    -> Promise: the device stops and mounts what is there
 *       clear(onProgress)          -> Promise<{ records, bad, lastSave }> as write()'s: everything the device holds
 *                                     in its user area erased (planClean with no files: the debug page's CLEAR
 *                                     USER DATA), planned again if the device saved meanwhile; an Error thrown
 *                                     before anything was written has .before set (hello()'s refusal among them)
 *       close()                    stops listening
 *     opts: { timeout: ms for one answer (default 4000), blockTimeout: for a BLOCK's (an erase and 16
 *             programs: default 8000), endTimeout: for END's and ABORT's (a mount: default 20000), log(text) }
 *   PTFM_seed.plan(layout, data)  -> plan, nothing sent:
 *       { ok, upToDate, problems: [text], keep: [rec], write: [rec], del: [rec], rewrites,
 *         blocks: [{ block, image: Uint8Array(4096), crc }] in the order to write, saveNumber,
 *         space: { blocks, free, used, after, reserve, need }, low, summary: [text lines] }
 *       ok false: problems says why (it does not fit, a kind or version the device does not take); nothing
 *       may be written then. A cartridge or pattern numbered F0-FF is a built-in thing's change: taken.
 *       The device's reserve is kept when it can be, else the user area is filled to its last block: low then
 *       says what fewer free blocks than advised cost the device's own saves (also a LOW SPACE line), else null.
 *   PTFM_seed.planClean(layout, data) -> a plan as plan()'s (clean: true) that erases everything the device
 *       holds in its user area (damaged records too), and writes data's files over the user area as if it
 *       were empty: the install page's clean install with no files (an empty user area)
 *   session.backup(layout, onProgress) -> Promise<Uint8Array>: every live record read back from the device,
 *       each checked against its CRC, as a .ptfmdsk disk (a copy of the library);
 *       onProgress({ done, total }) in bytes
 *   PTFM_seed.makeDisk(files, notes?)  files [{ kind, num, sub, ver, payload }] -> a .ptfmdsk disk, Uint8Array
 *   PTFM_seed.describe(rec)          "PATTERN 03.1", "SAMPLE 02.0C" ...
 *   PTFM_seed.crc32(bytes, crc?)     CRC-32 (zlib's)
 *   PTFM_seed.CMD, RESULT, KIND_NAME the protocol's numbers
 *
 * THE PROTOCOL: SysEx F0 7D 50 54 cmd payload F7; the payload 7-bit packed, 7 bytes in 8: each group of
 * up to 7 bytes is sent as a byte holding their top bits (bit i the top bit of the group's byte i), then
 * the bytes' low 7 bits. At most 512 bytes of data a message. Numbers little-endian.
 *   01 HELLO   u8 protocol version (1)
 *   02 LAYOUT  u32 total length, u32 offset, the layout's bytes from offset (several messages)
 *   03 READ    u32 address, u16 length (at most 504): the library read back, for the editor page's reading
 *              and its backups (the user area only)
 *   04 DATA    u32 address, the bytes: READ's answer (or an ACK with an error)
 *   10 BEGIN   u32 save number, u16 blocks (the save number must be the layout's last + 1, else the device
 *              answers CHANGED: something was saved or moved there since; HELLO and plan again)
 *   11 CHUNK   u16 block, u16 offset, up to 512 B of the block's bytes (the device starts each new block
 *              as all 0xFF, so chunks of only 0xFF need not be sent)
 *   12 BLOCK   u16 block, u32 CRC-32 of its 4096 B: the device erases it unless all 0xFF, programs it,
 *              reads it back
 *   13 END     the device mounts; its ACK carries u16 records, u16 bad records, u32 the last save
 *   1F ABORT   the device stops and mounts what is there
 *   20 ACK     u8 the command answered, u16 its block (0 when none), u8 result (RESULT), extra bytes
 *   A block is the flash address >> 12. The device answers every command but HELLO with an ACK (HELLO:
 *   the LAYOUT messages, or an ACK with an error).
 *
 * THE LAYOUT (version 1):
 *   0 "PTLY", 4 u8 layout version, 5 u8 protocol, 6 u16 spare, 8 u32 firmware version, 12 u32 where the
 *   firmware ends, 16 u16 largest data a message, 18 u16 reserve (blocks), 20 u8 regions R, 21 u8 accepted
 *   pairs A, 22 u16 built-ins B, 24 u32 the last save number, 28 u32 records N, 32 u8 flags (bit 0: edits
 *   not saved), 33-35 spare; then R x 12 (u32 start, u32 end, u8 area 0 user (1 FWS: never reported, never
 *   written), u8 bit 0 read in place, u16 spare), A x 2 (kind, version), B x 4 (kind, num, sub, ver), N x 24 (kind, num, sub, ver,
 *   u32 CRC-32 of the payload, u32 address, u32 payload length, u32 save, u8 flags bit 1 deleted, 3 spare),
 *   then u8 n and n characters: the firmware's version as text (layout.fwText).
 *   Bytes 8-11 (a numeric firmware version) are 0 so far.
 */
(function (root) {
"use strict";

const BLOCK = 4096, HDR = 16, MSG_DATA = 512, MAX_RECORD = 4096;
const PREFIX = [0xF0, 0x7D, 0x50, 0x54];
const CMD = { HELLO: 0x01, LAYOUT: 0x02, READ: 0x03, DATA: 0x04, BEGIN: 0x10, CHUNK: 0x11, BLOCK: 0x12, END: 0x13,
    ABORT: 0x1F, ACK: 0x20 };
const READ_MAX = 504;
const RESULT = { OK: 0, BAD_MSG: 1, BAD_BLOCK: 2, UNSAVED: 3, BAD_CRC: 4, FLASH: 5, NOT_BEGUN: 6, VERSION: 7, BUSY: 8,
    CHANGED: 9 };
const RESULT_TEXT = ["done", "a message the device did not understand", "a block outside the device's regions",
    "edits not saved: SAVE OR REVERT FIRST on the FM-1", "a block's CRC did not match", "the flash failed to write",
    "a block before BEGIN", "a protocol version the device does not take", "the device is saving: try again",
    "the FM-1's library changed since its layout was read (a SAVE or a move there): plan again"];
const AUDIO = 0x10;
const KIND_NAME = { 0x01: "DX7 CART", 0x02: "JUNO CART", 0x03: "TB-303 CART", 0x04: "SAMPLER CART",
    0x05: "FUSION CART", 0x06: "CHIP CART", 0x10: "SAMPLE", 0x20: "PATTERN", 0x21: "SONG", 0x22: "LOOPS", 0x23: "DJ",
    0x30: "SETTINGS", 0x31: "MIDI MAP", 0xE0: "NOTES" };
const BUILTIN_KINDS = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x20];   /* numbers F0-FF are the firmware's */

/* ---- bytes ---- */
const CRC_T = (() => { const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
    return t; })();
function crc32(b, crc) {
    let c = (crc === undefined ? 0 : crc) ^ 0xFFFFFFFF;
    for (let i = 0; i < b.length; i++) c = CRC_T[(c ^ b[i]) & 0xFF] ^ (c >>> 8);
    return (c ^ 0xFFFFFFFF) >>> 0;
}
const u16 = (b, o) => b[o] | b[o + 1] << 8;
const u32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
function put16(a, v) { a.push(v & 0xFF, v >>> 8 & 0xFF); }
function put32(a, v) { a.push(v & 0xFF, v >>> 8 & 0xFF, v >>> 16 & 0xFF, v >>> 24 & 0xFF); }
const hex2 = v => (v < 16 ? "0" : "") + v.toString(16).toUpperCase();
const align4 = n => (n + 3) & ~3;

function pack7(d) {
    const out = [];
    for (let i = 0; i < d.length; i += 7) {
        const n = Math.min(7, d.length - i);
        let top = 0;
        for (let j = 0; j < n; j++) if (d[i + j] & 0x80) top |= 1 << j;
        out.push(top);
        for (let j = 0; j < n; j++) out.push(d[i + j] & 0x7F);
    }
    return out;
}
function unpack7(p) {
    const out = [];
    for (let i = 0; i < p.length; i += 8) {
        const top = p[i], n = Math.min(7, p.length - i - 1);
        for (let j = 0; j < n; j++) out.push(p[i + 1 + j] | (top >> j & 1) << 7);
    }
    return Uint8Array.from(out);
}
function message(cmd, payload) {
    return Uint8Array.from(PREFIX.concat([cmd], pack7(payload || []), [0xF7]));
}
function parseMessage(m) {
    if (m.length < 6 || m[m.length - 1] !== 0xF7) return null;
    for (let i = 0; i < 4; i++) if (m[i] !== PREFIX[i]) return null;
    return { cmd: m[4], data: unpack7(m.subarray(5, m.length - 1)) };
}

/* ---- disks and blobs ---- */
function describe(r) {
    const name = KIND_NAME[r.kind] || "KIND " + hex2(r.kind);
    if (r.kind === AUDIO) return name + " " + hex2(r.num) + "." + hex2(r.sub);
    if (r.kind === 0x20 || r.kind === 0x22) return name + " " + hex2(r.num) + "." + r.sub;
    return name + " " + hex2(r.num);
}
function parseData(bytes) {
    const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
    const magic = String.fromCharCode.apply(null, b.subarray(0, 8));
    const res = { kind: null, files: [], notes: null, problems: [] };
    const add = (f) => {
        if (f.kind >= 0xE0 && f.kind <= 0xEF) {
            if (f.kind === 0xE0) res.notes = new TextDecoder("utf-8").decode(f.payload);
            return;
        }
        if (crc32(f.payload) !== f.crc) res.problems.push(describe(f) + ": its CRC does not match");
        res.files.push(f);
    };
    if (magic === "PTFMDISK") {
        res.kind = "disk";
        if (b[8] !== 1) res.problems.push("a disk of layout " + b[8] + ", not 1");
        const n = u32(b, 12);
        let o = 16;
        for (let i = 0; i < n; i++) {
            if (o + 12 > b.length) { res.problems.push("the disk ends early"); break; }
            const len = u32(b, o + 4);
            if (o + 12 + len > b.length) { res.problems.push("the disk ends early"); break; }
            const f = { kind: b[o], num: b[o + 1], sub: b[o + 2], ver: b[o + 3], len, crc: u32(b, o + 8),
                payload: b.slice(o + 12, o + 12 + len) };
            f.label = describe(f);
            add(f);
            o = align4(o + 12 + len);
        }
    } else if (magic === "PTFMDATA") {
        res.kind = "blob";
        if (u16(b, 8) !== 1) res.problems.push("a blob of version " + u16(b, 8) + ", not 1");
        const n = u16(b, 10);
        if (u32(b, 12) !== b.length) res.problems.push("the blob's length does not match");
        if (crc32(b.subarray(32)) !== u32(b, 16)) res.problems.push("the blob's CRC does not match");
        for (let i = 0; i < n; i++) {
            const e = 32 + i * 32, off = u32(b, e + 8), len = u32(b, e + 12);
            if (off + len > b.length) { res.problems.push("an entry past the blob's end"); continue; }
            let label = "";
            for (let j = 0; j < 12 && b[e + 20 + j]; j++) label += String.fromCharCode(b[e + 20 + j]);
            const f = { kind: b[e], num: b[e + 1], sub: b[e + 2], ver: b[e + 3], len, crc: u32(b, e + 16),
                payload: b.slice(off, off + len) };
            f.label = label || describe(f);
            add(f);
        }
    } else {
        res.problems.push("not a PT/FM disk or blob");
    }
    const seen = new Set();
    for (const f of res.files) {
        const k = f.kind << 16 | f.num << 8 | f.sub;
        if (seen.has(k)) res.problems.push(describe(f) + " twice");
        seen.add(k);
    }
    return res;
}

/* ---- the layout ---- */
function parseLayout(b) {
    if (String.fromCharCode(b[0], b[1], b[2], b[3]) !== "PTLY") throw new Error("not a PT/FM layout");
    const L = { version: b[4], protocol: b[5], fwVersion: u32(b, 8), fwEnd: u32(b, 12), maxData: u16(b, 16),
        reserve: u16(b, 18), lastSave: u32(b, 24), unsaved: !!(b[32] & 1),
        regions: [], accepts: [], builtins: [], records: [] };
    const R = b[20], A = b[21], B = u16(b, 22), N = u32(b, 28);
    let o = 36;
    for (let i = 0; i < R; i++, o += 12)
        L.regions.push({ start: u32(b, o), end: u32(b, o + 4), area: b[o + 8] ? "rom" : "user", inPlace: !!(b[o + 9] & 1) });
    for (let i = 0; i < A; i++, o += 2) L.accepts.push({ kind: b[o], ver: b[o + 1] });
    for (let i = 0; i < B; i++, o += 4) L.builtins.push({ kind: b[o], num: b[o + 1], sub: b[o + 2], ver: b[o + 3] });
    for (let i = 0; i < N; i++, o += 24) {
        const r = { kind: b[o], num: b[o + 1], sub: b[o + 2], ver: b[o + 3], crc: u32(b, o + 4), addr: u32(b, o + 8),
            len: u32(b, o + 12), save: u32(b, o + 16), deleted: !!(b[o + 20] & 2) };
        r.label = describe(r);
        L.records.push(r);
    }
    L.fwText = "";
    if (o < b.length) for (let i = 0, n = b[o++]; i < n && o < b.length; i++) L.fwText += String.fromCharCode(b[o++]);
    return L;
}

/* ---- the plan (blob.md, Seeding, step 2) ---- */
const key = r => r.kind << 16 | r.num << 8 | r.sub;
const size = r => HDR + align4(r.del ? 0 : r.len);

function checkFiles(P, layout, data) {       /* what the device cannot take, into P.problems */
    const accepts = new Set(layout.accepts.map(a => a.kind << 8 | a.ver));
    for (const f of data.files) {                    /* (F0-FF too: a built-in thing's change, found before the
                                                      * firmware's own, when the firmware has one of that kind there) */
        if (BUILTIN_KINDS.includes(f.kind) && f.num >= 0xF0 && !layout.builtins.some(b => b.kind === f.kind && b.num === f.num))
            P.problems.push(f.label + " uses a built-in number (F0-FF) with no built-in " +
                            (KIND_NAME[f.kind] || "kind " + hex2(f.kind)) + " there");
        if (!accepts.has(f.kind << 8 | f.ver))
            P.problems.push(f.label + ": the device does not take " + (KIND_NAME[f.kind] || "kind " + hex2(f.kind)) + " version " + f.ver);
        if (f.kind !== AUDIO && HDR + f.len > MAX_RECORD)
            P.problems.push(f.label + " is over 4 KB");
    }
}

function plan(layout, data) {
    const P = { ok: false, upToDate: false, problems: data.problems.slice(), keep: [], write: [], del: [],
        rewrites: 0, blocks: [], saveNumber: (layout.lastSave + 1) >>> 0, space: {}, summary: [] };
    checkFiles(P, layout, data);
    /* what the device holds (its live records), against the disk */
    const dev = new Map();
    for (const r of layout.records) if (!r.deleted) dev.set(key(r), r);
    const disk = new Map();
    for (const f of data.files) disk.set(key(f), f);
    for (const f of data.files) {
        const d = dev.get(key(f));
        if (d && d.ver === f.ver && d.len === f.len && d.crc === f.crc) P.keep.push(f);
        else P.write.push(f);
    }
    for (const r of dev.values())
        if (!disk.has(key(r))) P.del.push({ kind: r.kind, num: r.num, sub: r.sub, ver: r.ver, len: 0, del: true, label: r.label });

    /* the user area's blocks */
    const user = layout.regions.filter(r => r.area === "user");
    const blocks = [];                       /* { block, region } in address order */
    user.forEach((r, ri) => { for (let a = r.start; a + BLOCK <= r.end; a += BLOCK) blocks.push({ block: a >>> 12, region: ri }); });
    const occupied = new Map();              /* block -> the live records in it (deleted ones too: their headers) */
    for (const r of layout.records) {
        const end = r.addr + HDR + align4(r.deleted ? 0 : r.len);
        for (let b = r.addr >>> 12; b <= (end - 1) >>> 12; b++) {
            if (!occupied.has(b)) occupied.set(b, []);
            occupied.get(b).push(r);
        }
    }
    const goes = r => !r.deleted && (P.del.some(d => key(d) === key(r)) || P.write.some(w => key(w) === key(r)));
    const freeNow = blocks.filter(b => !occupied.has(b.block)).length;
    P.space = { blocks: blocks.length, free: freeNow, used: blocks.length - freeNow, reserve: layout.reserve,
        need: 0, after: 0 };
    if (!P.write.length && !P.del.length) {
        P.upToDate = true;
        P.ok = !P.problems.length;
        P.summary = summary(P, layout);
        return P;
    }

    /* two tries: free blocks only, then also blocks whose every record is replaced or deleted (a cut then
       leaves a part: seeding again mends it); each keeping the reserve, then (it does not fit so) without it, to
       the user area's last block: the reserve is advice, and P.low says what fewer free blocks cost */
    let layoutTry = null;
    tries: for (const keep of [layout.reserve, 0])
        for (const rewrite of [false, true]) {
            const usable = blocks.filter(b => !occupied.has(b.block) ||
                (rewrite && occupied.get(b.block).every(r => !r.deleted && goes(r))));   /* a deleted record stays: it hides an older version */
            layoutTry = place(P, usable, keep, occupied);
            if (layoutTry.fits) { P.rewrites = [...layoutTry.written].filter(b => occupied.has(b)).length; break tries; }
        }
    P.space.need = layoutTry.blocksUsed;
    P.space.after = layoutTry.freeAfter;
    if (!layoutTry.fits) P.problems.push(layoutTry.why);
    if (!P.problems.length) {
        P.blocks = images(P, layoutTry);
        P.ok = true;
        P.low = lowSpace(P.space.after, layout.reserve);
    }
    P.summary = summary(P, layout);
    return P;
}

/* what few free blocks leave the device's own saves (store.md): a SAVE keeps 2 free (compaction's) and a record
   written needs one more, so under 3 it saves nothing, a delete included; new things keep 10 free (the EDITOR's
   room); the reserve is what is advised. -> words, or null when there is enough */
const SAVE_MIN_FREE = 3, SAVE_ROOM = 10;
function lowSpace(free, reserve) {
    const left = "only " + Math.max(0, free) * 4 + " KB will be free on the FM-1: ";
    if (free < SAVE_MIN_FREE) return left + "it can save nothing, not even a delete, until something frees room";
    if (free < SAVE_ROOM) return left + "it can't save new things (its saves keep " + SAVE_ROOM * 4 + " KB free); it can still delete";
    if (free < reserve) return left + "less than the " + reserve * 4 + " KB advised for its own saves";
    return null;
}

/* A clean seeding (the install page's CLEAN INSTALL, with no files: an empty user area): the data's files laid out
   over the whole user area as if it were empty, and every other block of the user area written empty (all 0xFF:
   the device erases it), so nothing that was there before stays, damaged records included. Only the user area's
   regions are written. The empty blocks go first, the files' blocks after them with the save's last record last,
   as plan()'s. */
function planClean(layout, data) {
    const P = { ok: false, upToDate: false, clean: true, problems: data.problems.slice(), keep: [],
        write: data.files.slice(), del: [], rewrites: 0, blocks: [], saveNumber: (layout.lastSave + 1) >>> 0,
        space: {}, summary: [] };
    checkFiles(P, layout, data);
    const all = [];                          /* { block, region } in address order: the user area's */
    layout.regions.forEach((r, ri) => { if (r.area === "user") for (let a = r.start; a + BLOCK <= r.end; a += BLOCK) all.push({ block: a >>> 12, region: ri, area: r.area }); });
    const user = all;
    let lay = P.write.length ? place(P, user, layout.reserve, new Map()) : { fits: true, at: [], blocksUsed: 0, freeAfter: user.length };
    if (!lay.fits) lay = place(P, user, 0, new Map());  /* (the reserve advised only, as plan()'s) */
    P.space = { blocks: user.length, free: user.length, used: 0, reserve: layout.reserve, need: lay.blocksUsed, after: lay.freeAfter,
        erase: 0 };
    if (!lay.fits) P.problems.push(lay.why);
    if (!P.problems.length) {
        const files = lay.at.length ? images(P, lay) : [];
        const empty = new Uint8Array(BLOCK).fill(0xFF), ecrc = crc32(empty), written = new Set(files.map(b => b.block));
        const erase = all.filter(b => !written.has(b.block)).map(b => ({ block: b.block, image: empty, crc: ecrc }));
        P.space.erase = erase.length;
        P.blocks = erase.concat(files);
        P.ok = true;
        if (P.write.length) P.low = lowSpace(P.space.after, layout.reserve);
    }
    const s = P.summary;
    if (layout.fwText) s.push("THE FM-1: PT/FM " + layout.fwText + ".");
    s.push("ERASE: everything the FM-1 holds, its user area (" + kb(user.length * BLOCK) + ").");
    s.push("WRITE: " + P.write.length + " files, " + kb(P.write.reduce((n, f) => n + f.len, 0)) + ", into " +
        P.space.need + " blocks; " + P.space.after + " left free.");
    if (P.low) s.push("LOW SPACE: " + P.low + ".");
    for (const p of P.problems) s.push("PROBLEM: " + p);
    return P;
}

/* Lays the records out: audio first, packed over runs of consecutive blocks of one region; then the other
   records and the deleted ones, each within one block, into the blocks left. Free blocks before blocks
   written over (occupied: those holding records now), so as few as can be are written over. */
function place(P, usable, reserve, occupied) {
    const audio = P.write.filter(f => f.kind === AUDIO);
    const recs = P.write.filter(f => f.kind !== AUDIO).concat(P.del);
    const runs = [];
    for (const b of usable) {
        const last = runs[runs.length - 1];
        if (last && last.region === b.region && last.end === b.block) last.end++;
        else runs.push({ region: b.region, start: b.block, end: b.block + 1 });
    }
    const over = r => { let n = 0; for (let b = r.start; b < r.end; b++) if (occupied.has(b)) n++; return n; };
    runs.sort((a, b) => over(a) - over(b) || a.start - b.start);
    usable = usable.filter(b => !occupied.has(b.block)).concat(usable.filter(b => occupied.has(b.block)));
    const used = new Set(), at = [];        /* at: { rec, addr } */
    let ri = 0, pos = runs.length ? runs[0].start * BLOCK : 0;
    for (const f of audio) {
        const n = size(f);
        while (ri < runs.length && pos + n > runs[ri].end * BLOCK) {
            ri++;
            if (ri < runs.length) pos = runs[ri].start * BLOCK;
        }
        if (ri >= runs.length) return { fits: false, why: "it does not fit: the samples' audio needs more room in the user area", blocksUsed: 0, freeAfter: 0 };
        at.push({ rec: f, addr: pos });
        for (let b = pos >>> 12; b <= (pos + n - 1) >>> 12; b++) used.add(b);
        pos += n;
    }
    const open = [];                         /* { block, fill } */
    for (const f of recs) {
        const n = size(f);
        let o = open.find(x => x.fill + n <= BLOCK);
        if (!o) {
            const b = usable.find(x => !used.has(x.block));
            if (!b) return { fits: false, why: "it does not fit in the user area", blocksUsed: 0, freeAfter: 0 };
            used.add(b.block);
            o = { block: b.block, fill: 0 };
            open.push(o);
        }
        at.push({ rec: f, addr: o.block * BLOCK + o.fill });
        o.fill += n;
    }
    /* the free blocks left once the seeding has counted: every usable block not written (a block written
       over was not free before, but what was in it is gone after) */
    const left = usable.length - used.size;
    if (left < reserve)
        return { fits: false, why: "it does not fit: " + (reserve - left) + " more blocks (" + 4 * (reserve - left) +
            " KB) are needed to keep the device's reserve free", blocksUsed: used.size, freeAfter: left };
    return { fits: true, at, blocksUsed: used.size, freeAfter: left, written: used };
}

function images(P, lay) {
    const imgs = new Map();
    const img = b => { if (!imgs.has(b)) imgs.set(b, new Uint8Array(BLOCK).fill(0xFF)); return imgs.get(b); };
    const write = (addr, bytes) => {
        for (let i = 0; i < bytes.length; ) {
            const b = (addr + i) >>> 12, o = (addr + i) & (BLOCK - 1), n = Math.min(bytes.length - i, BLOCK - o);
            img(b).set(bytes.subarray(i, i + n), o);
            i += n;
        }
    };
    /* the last record of the save: the highest one in the block of the last record placed (a record's
       block, not audio, when there is one), written last */
    const nonAudio = lay.at.filter(x => x.rec.kind !== AUDIO);
    const pool = nonAudio.length ? nonAudio : lay.at;
    const lastBlock = (pool[pool.length - 1].addr) >>> 12;
    let last = null;
    for (const x of lay.at) if ((x.addr >>> 12) === lastBlock && (!last || x.addr > last.addr)) last = x;
    for (const x of lay.at) {
        const r = x.rec, len = r.del ? 0 : r.len;
        const h = new Uint8Array(HDR);
        h[0] = r.kind; h[1] = r.num; h[2] = r.sub; h[3] = r.ver;
        h[4] = len & 0xFF; h[5] = len >>> 8 & 0xFF; h[6] = len >>> 16 & 0xFF;
        h[7] = (x === last ? 1 : 0) | (r.del ? 2 : 0);
        h[8] = P.saveNumber & 0xFF; h[9] = P.saveNumber >>> 8 & 0xFF; h[10] = P.saveNumber >>> 16 & 0xFF; h[11] = P.saveNumber >>> 24 & 0xFF;
        let c = crc32(h.subarray(0, 12));
        if (len) c = crc32(r.payload, c);
        h[12] = c & 0xFF; h[13] = c >>> 8 & 0xFF; h[14] = c >>> 16 & 0xFF; h[15] = c >>> 24 & 0xFF;
        write(x.addr, h);
        if (len) write(x.addr + HDR, r.payload);
    }
    /* the last record's block last; an audio record's blocks in address order before it */
    const order = [...imgs.keys()].sort((a, b) => a - b);
    const lastEnd = (last.addr + size(last.rec) - 1) >>> 12;
    const tail = [];
    for (let b = last.addr >>> 12; b <= lastEnd; b++) tail.push(b);
    const rest = order.filter(b => !tail.includes(b));
    return rest.concat(tail).map(b => ({ block: b, image: imgs.get(b), crc: crc32(imgs.get(b)) }));
}

function kb(n) { return n >= 1024 ? Math.round(n / 1024) + " KB" : n + " B"; }
function summary(P, layout) {
    const s = [], S = P.space;
    if (layout.fwText) s.push("THE FM-1: PT/FM " + layout.fwText + ".");
    s.push("USER AREA: " + S.blocks + " blocks of 4 KB (" + kb(S.blocks * BLOCK) + "), " + S.free + " free now; " +
        S.reserve + " advised free for the device's own saves.");
    if (P.upToDate) { s.push("The device already holds exactly this: nothing to write."); return s; }
    const bytes = P.write.reduce((n, f) => n + f.len, 0);
    s.push("WRITE: " + P.write.length + " files, " + kb(bytes) + " (" + P.write.filter(f => f.kind === AUDIO).length +
        " samples' audio)" + (P.ok ? ", into " + S.need + " blocks; " + S.after + " left free." : "."));
    if (P.keep.length) s.push("KEEP: " + P.keep.length + " files the device already holds, unchanged.");
    if (P.del.length) s.push("DELETE: " + P.del.length + " files the device holds and this does not: " +
        P.del.map(d => d.label).join(", ") + ".");
    if (P.rewrites) s.push("REWRITE: " + P.rewrites + " blocks of files being replaced are written over; a seeding cut short then leaves a part (seeding again mends it).");
    if (P.low) s.push("LOW SPACE: " + P.low + ".");
    for (const p of P.problems) s.push("PROBLEM: " + p);
    return s;
}

/* ---- the session ---- */
function session(transport, opts) {
    opts = opts || {};
    const timeout = opts.timeout || 4000, log = opts.log || (() => {});
    const blockTimeout = opts.blockTimeout || 8000, endTimeout = opts.endTimeout || 20000;
    let waiter = null, layoutBuf = null, layoutGot = 0, closed = false;
    transport.onMessage(m => {
        if (closed) return;
        const p = parseMessage(m);
        if (!p || !waiter) return;
        waiter(p);
    });
    const send = (cmd, payload) => transport.send(message(cmd, payload));
    function wait(match, ms) {
        return new Promise((resolve, reject) => {
            const t = setTimeout(() => {         /* (timeout: no answer at all, as against an answer saying no) */
                waiter = null;
                const e = new Error("the device did not answer");
                e.timeout = true;
                reject(e);
            }, ms || timeout);
            waiter = p => {
                const r = match(p);
                if (r === undefined) return;
                clearTimeout(t);
                waiter = null;
                if (r instanceof Error) reject(r); else resolve(r);
            };
        });
    }
    const ackError = (d) => { const e = new Error(RESULT_TEXT[d[3]] || "result " + d[3]); e.code = d[3]; return e; };
    function ack(cmd, block, ms) {
        const w = wait(p => {
            if (p.cmd !== CMD.ACK || p.data[0] !== cmd) return undefined;
            if (block !== undefined && u16(p.data, 1) !== block) return undefined;
            return p.data[3] === RESULT.OK ? p.data : ackError(p.data);
        }, ms);
        return w;
    }
    return {
        hello() {
            layoutBuf = null; layoutGot = 0;
            const w = wait(p => {
                if (p.cmd === CMD.ACK && p.data[0] === CMD.HELLO) return ackError(p.data);
                if (p.cmd !== CMD.LAYOUT) return undefined;
                const total = u32(p.data, 0), off = u32(p.data, 4), part = p.data.subarray(8);
                if (!layoutBuf || layoutBuf.length !== total) { layoutBuf = new Uint8Array(total); layoutGot = 0; }
                layoutBuf.set(part, off);
                layoutGot += part.length;
                if (layoutGot < total) return undefined;
                return parseLayout(layoutBuf);
            });
            send(CMD.HELLO, [1]);
            return w;
        },
        async write(P, onProgress) {
            if (!P.ok || P.upToDate) throw new Error(P.upToDate ? "nothing to write" : "the plan has problems");
            const b = []; put32(b, P.saveNumber); put16(b, P.blocks.length);
            let w = ack(CMD.BEGIN, 0); send(CMD.BEGIN, b); await w;
            let done = 0;
            for (const blk of P.blocks) {
                for (let off = 0; off < BLOCK; off += MSG_DATA) {
                    const part = blk.image.subarray(off, off + MSG_DATA);
                    if (part.every(x => x === 0xFF)) continue;
                    const c = []; put16(c, blk.block); put16(c, off);
                    w = ack(CMD.CHUNK, blk.block); send(CMD.CHUNK, c.concat(Array.from(part))); await w;
                }
                const c = []; put16(c, blk.block); put32(c, blk.crc);
                w = ack(CMD.BLOCK, blk.block, blockTimeout); send(CMD.BLOCK, c); await w;
                done++;
                log("block " + hex2(blk.block >> 8) + hex2(blk.block & 0xFF) + " written");
                if (onProgress) onProgress({ done, total: P.blocks.length, block: blk.block });
            }
            w = ack(CMD.END, 0, endTimeout); send(CMD.END, []);
            const d = await w;
            return { records: u16(d, 4), bad: u16(d, 6), lastSave: u32(d, 8) };
        },
        async abort() {
            const w = ack(CMD.ABORT, 0, endTimeout); send(CMD.ABORT, []); await w;
        },
        async clear(onProgress) {
            for (let tries = 0; ; tries++) {
                let P;
                try {
                    P = planClean(await this.hello(), { files: [], problems: [] });
                    if (!P.ok) throw new Error(P.problems[0] || "the user area cannot be erased");
                } catch (e) { e.before = true; throw e; }
                try { return await this.write(P, onProgress); }
                catch (e) {                          /* (CHANGED: refused at BEGIN, nothing written yet) */
                    if (e && e.code === RESULT.CHANGED) { if (tries < 2) continue; e.before = true; }
                    throw e;
                }
            }
        },
        read(addr, n) {
            const w = wait(p => {
                if (p.cmd === CMD.ACK && p.data[0] === CMD.READ) return ackError(p.data);
                if (p.cmd !== CMD.DATA || u32(p.data, 0) !== addr) return undefined;
                return p.data.slice(4);
            });
            const b = []; put32(b, addr); put16(b, n);
            send(CMD.READ, b);
            return w;
        },
        async backup(L, onProgress) {
            const live = L.records.filter(r => !r.deleted);
            const total = live.reduce((n, r) => n + HDR + r.len, 0);
            let done = 0;
            const files = [];
            for (const r of live) {
                const rec = new Uint8Array(HDR + r.len);
                for (let o = 0; o < rec.length; o += READ_MAX) {
                    const n = Math.min(READ_MAX, rec.length - o);
                    rec.set(await this.read(r.addr + o, n), o);
                    done += n;
                    if (onProgress) onProgress({ done, total });
                }
                const len = rec[4] | rec[5] << 8 | rec[6] << 16;
                const ok = rec[0] === r.kind && rec[1] === r.num && rec[2] === r.sub && len === r.len &&
                    crc32(rec.subarray(HDR), crc32(rec.subarray(0, 12))) === u32(rec, 12);
                if (!ok) throw new Error(r.label + " read back wrong");
                files.push({ kind: r.kind, num: r.num, sub: r.sub, ver: r.ver, payload: rec.slice(HDR) });
            }
            return makeDisk(files);
        },
        close() { closed = true; waiter = null; }
    };
}

/* A disk (specs/files/disk.md): settings, cartridges, songs, patterns, the loops, notes, then the audio,
   each by number and sub. */
const DISK_ORDER = [0x30, 0x31, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x21, 0x20, 0x22, 0x23, 0xE0, 0x10];
function makeDisk(files, notes) {
    const all = files.slice();
    if (notes) all.push({ kind: 0xE0, num: 0, sub: 0, ver: 1, payload: new TextEncoder().encode(notes) });
    const rank = k => { const i = DISK_ORDER.indexOf(k); return i < 0 ? 100 + k : i; };
    all.sort((a, b) => rank(a.kind) - rank(b.kind) || a.num - b.num || a.sub - b.sub);
    let n = 16;
    for (const f of all) n = align4(n + 12 + f.payload.length);
    const b = new Uint8Array(n);
    b.set([0x50, 0x54, 0x46, 0x4D, 0x44, 0x49, 0x53, 0x4B], 0);             /* PTFMDISK */
    b[8] = 1;
    const dv = new DataView(b.buffer);
    dv.setUint32(12, all.length, true);
    let o = 16;
    for (const f of all) {
        b[o] = f.kind; b[o + 1] = f.num; b[o + 2] = f.sub; b[o + 3] = f.ver;
        dv.setUint32(o + 4, f.payload.length, true);
        dv.setUint32(o + 8, crc32(f.payload), true);
        b.set(f.payload, o + 12);
        o = align4(o + 12 + f.payload.length);
    }
    return b;
}

function midiTransport(input, output) {
    const cbs = [];
    input.addEventListener("midimessage", e => {
        const d = e.data;
        if (d[0] === 0xF0 && d[1] === 0x7D && d[2] === 0x50 && d[3] === 0x54) for (const cb of cbs) cb(d);
    });
    return { send(bytes) { output.send(Array.from(bytes)); }, onMessage(fn) { cbs.push(fn); } };
}

const S = { parseData, parseLayout, plan, planClean, describe, session, midiTransport, makeDisk, crc32, message, parseMessage,
    pack7, unpack7, CMD, RESULT, RESULT_TEXT, KIND_NAME, BLOCK };
if (typeof module !== "undefined" && module.exports) module.exports = S;
else root.PTFM_seed = S;
})(typeof window !== "undefined" ? window : globalThis);

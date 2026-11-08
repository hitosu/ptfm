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
 *       Every plan keeps within the store's list of records (layout.mapRoom), else a problem says so.
 *   PTFM_seed.planPartial(layout, write, del) -> a plan (partial: true) writing these records and deleting these, all
 *       else kept as it is, unread and unchecked; free blocks only, the reserve kept when it can be (reserveKept);
 *       not fitting: space.short blocks more, space.piece KB in one piece, space.mapShort records more than the list holds
 *   PTFM_seed.needsConversion(layout) -> { samplers, audio } the old form the firmware no longer reads, or null
 *   PTFM_seed.convertDevice(session, opts) -> Promise<result>: the old form on the device converted, whole samplers a
 *       save in partial plans (blob.md, Converting after an install: the install page after an install, the emulator's
 *       kept flash); opts { layout, onStep, most }; see it below
 *   PTFM_seed.upgradeFor(layout, data) -> data, an older PT/FM's disk converted as it loads where the device takes the
 *       new form only (.converted says what)
 *   PTFM_seed.convertSamplers(recs), samplerConverter(existing): the old samplers' converter (below)
 *   session.readRecord(rec) -> Promise<payload>: one record the layout lists read back (READ), checked against its header
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
 *   0 "PTLY", 4 u8 layout version, 5 u8 protocol, 6 u16 the records the store can list (its map's room, 560: past it a
 *   start would drop records; 0 from a firmware that does not say), 8 u32 firmware version, 12 u32 where the
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
const AUDIO_OLD = 0x10;                     /* the old form's audio, numbered under its sampler, sub its index */
/* audio (specs/files/store.md#audio): both kinds, the samples (0x11) and the old form's audio (0x10): a record that may
 * cross blocks, placed in runs of blocks of its own */
const isAudio = kind => (kind & 0xFE) === AUDIO_OLD;
const KIND_NAME = { 0x01: "DX7 CART", 0x02: "JUNO CART", 0x03: "TB-303 CART", 0x04: "SAMPLER CART",
    0x05: "FUSION CART", 0x06: "CHIP CART", 0x10: "SAMPLE", 0x11: "SAMPLE", 0x20: "PATTERN", 0x21: "SONG", 0x22: "LOOPS", 0x23: "DJ",
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
    if (r.kind === AUDIO_OLD) return name + " " + hex2(r.num) + "." + hex2(r.sub);
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
    const L = { version: b[4], protocol: b[5], mapRoom: u16(b, 6), fwVersion: u32(b, 8), fwEnd: u32(b, 12), maxData: u16(b, 16),
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

function userBlocks(layout) {                  /* { block, region } in address order: the user area's */
    const blocks = [];
    layout.regions.forEach((r, ri) => { if (r.area === "user") for (let a = r.start; a + BLOCK <= r.end; a += BLOCK) blocks.push({ block: a >>> 12, region: ri }); });
    return blocks;
}
function occupiedBlocks(layout) {              /* block -> the records listed in it (deleted ones too: their headers) */
    const occupied = new Map();
    for (const r of layout.records) {
        const end = r.addr + HDR + align4(r.deleted ? 0 : r.len);
        for (let b = r.addr >>> 12; b <= (end - 1) >>> 12; b++) {
            if (!occupied.has(b)) occupied.set(b, []);
            occupied.get(b).push(r);
        }
    }
    return occupied;
}
/* the store's list of records (its map, blob.md): the device lists every (kind, number, sub) it holds, a deleted one too
 * until its compaction; past mapRoom a start would drop records. after: how many it lists once the plan is written */
function mapCheck(P, layout, after) {
    P.space.mapAfter = after;
    P.space.mapShort = layout.mapRoom && after > layout.mapRoom ? after - layout.mapRoom : 0;
    if (P.space.mapShort)
        P.problems.push("it does not fit in the FM-1's list of records: " + after + " records, " + layout.mapRoom + " at most");
}

/* a sample's entry and its audio's head agree (coding and length), as the firmware checks them (store.h store_sample_ok):
 * a sample whose two differ is read as missing everywhere (specs/files/instruments.md#samples) */
function sampleOk(p) {
    if (!p || p.length < SAMPLE_ENTRY + 4) return false;
    return ((p[4] >> 6 | p[5] << 2) & 15) === p[16] && (p[0] | p[1] << 8 | (p[2] & 15) << 16) === (p[17] | p[18] << 8 | p[19] << 16);
}
function checkFiles(P, layout, data) {       /* what the device cannot take, into P.problems; the samples it will read as
                                                missing (their entry and audio differ: it takes them, plays them as none,
                                                CHECK STORAGE names them), into P.differ */
    P.differ = data.files.filter(f => f.kind === 0x11 && !sampleOk(f.payload)).map(f => f.label || describe(f));
    const accepts = new Set(layout.accepts.map(a => a.kind << 8 | a.ver));
    for (const f of data.files) {                    /* (F0-FF too: a built-in thing's change, found before the
                                                      * firmware's own, when the firmware has one of that kind there) */
        if (BUILTIN_KINDS.includes(f.kind) && f.num >= 0xF0 && !layout.builtins.some(b => b.kind === f.kind && b.num === f.num))
            P.problems.push(f.label + " uses a built-in number (F0-FF) with no built-in " +
                            (KIND_NAME[f.kind] || "kind " + hex2(f.kind)) + " there");
        if (!accepts.has(f.kind << 8 | f.ver))
            P.problems.push(f.label + ": the device does not take " + (KIND_NAME[f.kind] || "kind " + hex2(f.kind)) + " version " + f.ver);
        if (!isAudio(f.kind) && HDR + f.len > MAX_RECORD)
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
    const blocks = userBlocks(layout), occupied = occupiedBlocks(layout);
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
    const listed = new Set(layout.records.map(key));     /* (what it lists then: what it lists now, and what is written) */
    for (const f of P.write) listed.add(key(f));
    mapCheck(P, layout, listed.size);

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
    const all = userBlocks(layout), user = all;
    let lay = P.write.length ? place(P, user, layout.reserve, new Map()) : { fits: true, at: [], blocksUsed: 0, freeAfter: user.length };
    if (!lay.fits) lay = place(P, user, 0, new Map());  /* (the reserve advised only, as plan()'s) */
    P.space = { blocks: user.length, free: user.length, used: 0, reserve: layout.reserve, need: lay.blocksUsed, after: lay.freeAfter,
        erase: 0 };
    if (!lay.fits) P.problems.push(lay.why);
    mapCheck(P, layout, new Set(P.write.map(key)).size);       /* (the user area emptied: it lists what is written) */
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
    if (P.differ.length) s.push("MISSING: " + P.differ.join(", ") + ": " + (P.differ.length === 1 ? "its entry and audio differ" :
        "their entries and audio differ") + ", so the FM-1 reads " + (P.differ.length === 1 ? "it" : "them") + " as missing.");
    if (P.low) s.push("LOW SPACE: " + P.low + ".");
    for (const p of P.problems) s.push("PROBLEM: " + p);
    return P;
}

/* A partial plan (blob.md, Converting after an install): these records written, these deleted ({ kind, num, sub, ver }:
   records the device lists), and everything else the device holds kept as it is, unread and unchecked (only what is
   written is checked against what the device takes). Free blocks only, never a block holding records (a deleted one's
   header too), so a cut at any moment leaves what was there; the reserve kept when it can be (reserveKept), else to the
   user area's last block (low says what that costs); within the store's list of records. Not fitting the free blocks:
   space.short, the blocks more it needs (the save laid out alone, against the free blocks), or, when the free blocks hold
   it in amount but lie in pieces too small, space.piece, the KB its biggest audio record needs in one piece (a deletion
   on the device makes no piece longer); past the list, space.mapShort, the records more than it holds. */
function planPartial(layout, write, del) {
    const P = { ok: false, upToDate: false, partial: true, reserveKept: false, problems: [], keep: [],
        write: write.map(f => ({ kind: f.kind, num: f.num, sub: f.sub, ver: f.ver, payload: f.payload, len: f.payload.length,
            crc: crc32(f.payload), label: f.label || describe(f) })),
        del: del.map(d => ({ kind: d.kind, num: d.num, sub: d.sub, ver: d.ver, len: 0, del: true, label: d.label || describe(d) })),
        rewrites: 0, blocks: [], saveNumber: (layout.lastSave + 1) >>> 0, space: {}, summary: [] };
    checkFiles(P, layout, { files: P.write });
    const blocks = userBlocks(layout), occupied = occupiedBlocks(layout);
    const usable = blocks.filter(b => !occupied.has(b.block));
    P.space = { blocks: blocks.length, free: usable.length, used: blocks.length - usable.length, reserve: layout.reserve,
        need: 0, after: 0, short: 0, piece: 0, mapAfter: layout.records.length, mapShort: 0 };
    if (!P.write.length && !P.del.length) {
        P.upToDate = true;
        P.ok = !P.problems.length;
        P.summary = summary(P, layout);
        return P;
    }
    const listed = new Set(layout.records.map(key));
    for (const f of P.write) listed.add(key(f));
    mapCheck(P, layout, listed.size);
    let lay = place(P, usable, layout.reserve, occupied, true);
    P.reserveKept = lay.fits;
    if (!lay.fits) lay = place(P, usable, 0, occupied, true);
    if (!lay.fits) {                         /* (how much more room: the save laid out alone in an empty run) */
        const empty = [];
        for (let i = 0; i < 1024; i++) empty.push({ block: 0x10000 + i, region: -1 });
        const alone = place(P, empty, 0, new Map(), true).blocksUsed;
        if (alone <= usable.length) {
            const most = P.write.filter(f => isAudio(f.kind)).reduce((n, f) => Math.max(n, size(f)), 0);
            P.space.piece = Math.ceil(most / BLOCK) * 4;
            P.problems.push("it does not fit in the free blocks: they hold it, but in pieces; " + P.space.piece + " KB in one piece needed");
        } else {
            P.space.short = alone - usable.length;
            P.problems.push("it does not fit in the free blocks: " + P.space.short * 4 + " KB more room needed");
        }
    }
    P.space.need = lay.blocksUsed;
    P.space.after = lay.freeAfter;
    if (!P.problems.length) {
        P.blocks = images(P, lay);
        P.ok = true;
        P.low = P.reserveKept ? null : lowSpace(P.space.after, layout.reserve);
    }
    P.summary = summary(P, layout);
    return P;
}

/* Lays the records out: audio first, packed over runs of consecutive blocks of one region; then the other
   records and the deleted ones, each within one block, into the blocks left. Free blocks before blocks
   written over (occupied: those holding records now), so as few as can be are written over. firstFit (the partial
   plan's, whose free blocks lie scattered): the biggest audio first, each into the first run with room left, so the
   room a run leaves is used. */
function place(P, usable, reserve, occupied, firstFit) {
    const audio = P.write.filter(f => isAudio(f.kind));
    const recs = P.write.filter(f => !isAudio(f.kind)).concat(P.del);
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
    const noRoom = { fits: false, why: "it does not fit: the samples' audio needs more room in the user area", blocksUsed: 0, freeAfter: 0 };
    if (firstFit) {
        const fill = runs.map(r => r.start * BLOCK);
        for (const f of audio.slice().sort((a, b) => size(b) - size(a) || key(a) - key(b))) {
            const n = size(f), k = runs.findIndex((r, i) => fill[i] + n <= r.end * BLOCK);
            if (k < 0) return noRoom;
            at.push({ rec: f, addr: fill[k] });
            for (let b = fill[k] >>> 12; b <= (fill[k] + n - 1) >>> 12; b++) used.add(b);
            fill[k] += n;
        }
    }
    let ri = 0, pos = runs.length ? runs[0].start * BLOCK : 0;
    for (const f of firstFit ? [] : audio) {
        const n = size(f);
        while (ri < runs.length && pos + n > runs[ri].end * BLOCK) {
            ri++;
            if (ri < runs.length) pos = runs[ri].start * BLOCK;
        }
        if (ri >= runs.length) return noRoom;
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
    const nonAudio = lay.at.filter(x => !isAudio(x.rec.kind));
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
    s.push("WRITE: " + P.write.length + " files, " + kb(bytes) + " (" + P.write.filter(f => isAudio(f.kind)).length +
        " samples' audio)" + (P.ok ? ", into " + S.need + " blocks; " + S.after + " left free." : "."));
    if (P.keep.length) s.push("KEEP: " + P.keep.length + " files the device already holds, unchanged.");
    if (P.del.length) s.push("DELETE: " + P.del.length + (P.partial ? " files: " : " files the device holds and this does not: ") +
        P.del.map(d => d.label).join(", ") + ".");
    if (P.partial) s.push("KEEP: everything else the device holds, as it is.");
    if (P.differ && P.differ.length) s.push("MISSING: " + P.differ.join(", ") + ": " + (P.differ.length === 1 ? "its entry and audio differ" :
        "their entries and audio differ") + ", so the FM-1 reads " + (P.differ.length === 1 ? "it" : "them") + " as missing.");
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
        let cancel = null;
        const w = new Promise((resolve, reject) => {
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
            cancel = e => { clearTimeout(t); waiter = null; reject(e); };
        });
        w.cancel = cancel;
        return w;
    }
    /* sends, then the answer waited for: a send that fails (the port gone) is that wait's failure, not a time-out later */
    const sendFor = (w, cmd, payload) => { try { send(cmd, payload); } catch (e) { w.cancel(e); } return w; };
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
            return sendFor(w, CMD.HELLO, [1]);
        },
        async write(P, onProgress) {
            if (!P.ok || P.upToDate) throw new Error(P.upToDate ? "nothing to write" : "the plan has problems");
            const b = []; put32(b, P.saveNumber); put16(b, P.blocks.length);
            await sendFor(ack(CMD.BEGIN, 0), CMD.BEGIN, b);
            let done = 0;
            for (const blk of P.blocks) {
                for (let off = 0; off < BLOCK; off += MSG_DATA) {
                    const part = blk.image.subarray(off, off + MSG_DATA);
                    if (part.every(x => x === 0xFF)) continue;
                    const c = []; put16(c, blk.block); put16(c, off);
                    await sendFor(ack(CMD.CHUNK, blk.block), CMD.CHUNK, c.concat(Array.from(part)));
                }
                const c = []; put16(c, blk.block); put32(c, blk.crc);
                await sendFor(ack(CMD.BLOCK, blk.block, blockTimeout), CMD.BLOCK, c);
                done++;
                log("block " + hex2(blk.block >> 8) + hex2(blk.block & 0xFF) + " written");
                if (onProgress) onProgress({ done, total: P.blocks.length, block: blk.block });
            }
            const d = await sendFor(ack(CMD.END, 0, endTimeout), CMD.END, []);
            return { records: u16(d, 4), bad: u16(d, 6), lastSave: u32(d, 8) };
        },
        async abort() {
            await sendFor(ack(CMD.ABORT, 0, endTimeout), CMD.ABORT, []);
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
            return sendFor(w, CMD.READ, b);
        },
        async readRecord(r, onBytes) {         /* a record the layout lists, read back: its payload, checked against its
                                                  header (kind, number, sub, length, CRC) */
            const rec = new Uint8Array(HDR + r.len);
            for (let o = 0; o < rec.length; o += READ_MAX) {
                const n = Math.min(READ_MAX, rec.length - o);
                rec.set(await this.read(r.addr + o, n), o);
                if (onBytes) onBytes(n);
            }
            const len = rec[4] | rec[5] << 8 | rec[6] << 16;
            const ok = rec[0] === r.kind && rec[1] === r.num && rec[2] === r.sub && len === r.len &&
                crc32(rec.subarray(HDR), crc32(rec.subarray(0, 12))) === u32(rec, 12);
            if (!ok) {                          /* (stale: not there now, moved by a save on the FM-1 since the layout) */
                const e = new Error((r.label || describe(r)) + " read back wrong");
                e.stale = true;
                throw e;
            }
            return rec.slice(HDR);
        },
        async backup(L, onProgress) {
            const live = L.records.filter(r => !r.deleted);
            const total = live.reduce((n, r) => n + HDR + r.len, 0);
            let done = 0;
            const files = [];
            for (const r of live) {
                const payload = await this.readRecord(r, n => { done += n; if (onProgress) onProgress({ done, total }); });
                files.push({ kind: r.kind, num: r.num, sub: r.sub, ver: r.ver, payload });
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
    /* (a sample, 0x11, sorts as the old audio, 0x10 does: with the audio, last) */
    const rank = k => { const i = DISK_ORDER.indexOf(k === 0x11 ? 0x10 : k); return i < 0 ? 100 + k : i; };
    all.sort((a, b) => rank(a.kind) - rank(b.kind) || a.num - b.num || a.sub - b.sub || a.kind - b.kind);
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

/* ---- the old samplers converted (specs/files/disk.md#the-samplers-conversion; tools/ptfm_records.py is the
 * twin the tests compare with) ---- */
const SAMPLES_MAX = 256, SAMPLER_SLOT = 40, SAMPLE_ENTRY = 16;
function bitsAt(b, at, n) { let v = 0; for (let i = 0; i < n; i++, at++) v |= ((b[at >> 3] >> (at & 7)) & 1) << i; return v; }
function bytesEq(a, b) { if (a.length !== b.length) return false; for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false; return true; }
/* an old instrument's 40 bytes -> version 2's: SAMPLE `number` in 8 bits, every later bit 2 places further (the spare 86
 * -> 84); null: an emptied slot (its name an empty slot's) */
function slotV1toV2(slot, number) {
    const out = new Uint8Array(SAMPLER_SLOT);
    if (number === null) { out[22] = 0x3F; return out; }
    out[0] = number;
    for (let k = 8; k < SAMPLER_SLOT * 8; k++) { const o = k - 2; if ((slot[o >> 3] >> (o & 7)) & 1) out[k >> 3] |= 1 << (k & 7); }
    return out;
}
const slotV1Empty = slot => bitsAt(slot, 174, 6) === 63;
const slotV2Empty = slot => bitsAt(slot, 176, 6) === 63;
function namedBy(p) {                        /* the sample numbers a sampler of version 2 names (its instruments' SAMPLE) */
    const out = [], ni = p[9];
    if (p.length === 10 + ni * SAMPLER_SLOT) for (let i = 0; i < ni; i++) {
        const slot = p.subarray(10 + i * SAMPLER_SLOT, 10 + (i + 1) * SAMPLER_SLOT);
        if (!slotV2Empty(slot)) out.push(slot[0]);
    }
    return out;
}
/* One sampler at a time (the install page converts an FM-1's so). existing: { samples: [{ num, len, payload? }] the
 * samples already there (a merge compares only payloads of the same length: the caller fetches those), named: Set of the
 * numbers version 2 instruments name }. -> { full, convert(num, v1Payload, audioBySub) } where
 *   convert -> { sampler: Uint8Array (version 2), samples: [{ num, payload, merged }] each sample the sampler's
 *       instruments name, once (merged: an equal one was there already: nothing to write), dropped: old audio the
 *       table has no entry for } or null when its new samples do not fit in the numbers left (full is set then: the
 *       sampler and every later one stay in the old form); throws for a record that is not an old sampler. */
function samplerConverter(existing) {
    const ex = existing || {};
    const have = (ex.samples || []).map(s => ({ num: s.num, len: s.len !== undefined ? s.len : s.payload.length, payload: s.payload || null }))
        .sort((a, b) => a.num - b.num);
    const taken = new Set(have.map(s => s.num));
    for (const n of ex.named || []) taken.add(n);
    const conv = { full: false, convert };
    function find(p) { return have.find(s => s.len === p.length && s.payload && bytesEq(s.payload, p)); }
    function convert(num, v1, audioBySub) {
        const ni = v1[9], ns = v1[10];
        if (v1.length !== 11 + ni * SAMPLER_SLOT + ns * SAMPLE_ENTRY) throw new Error("an old sampler: " + v1.length + " B for " + ni + " instruments, " + ns + " samples");
        const audio = i => audioBySub instanceof Map ? audioBySub.get(i) : audioBySub && audioBySub[i];
        const subs = audioBySub instanceof Map ? [...audioBySub.keys()] : Object.keys(audioBySub || {}).map(Number);
        const table = 11 + ni * SAMPLER_SLOT, made = new Map(), fresh = [];
        for (let i = 0; i < ns; i++) {
            const e = v1.subarray(table + i * SAMPLE_ENTRY, table + (i + 1) * SAMPLE_ENTRY), a = audio(i);
            if (bitsAt(e, 42, 6) === 63 || !a) continue;
            const p = new Uint8Array(SAMPLE_ENTRY + a.length);
            p.set(e); p.set(a, SAMPLE_ENTRY);
            made.set(i, p);
            if (!find(p) && !fresh.some(q => bytesEq(q, p))) fresh.push(p);
        }
        const free = [];
        for (let n = 0; n < SAMPLES_MAX && free.length < fresh.length; n++) if (!taken.has(n)) free.push(n);
        if (free.length < fresh.length) { conv.full = true; return null; }
        const out = [], numOf = new Map();
        fresh.forEach((p, k) => { taken.add(free[k]); have.push({ num: free[k], len: p.length, payload: p }); numOf.set(p, free[k]); out.push({ num: free[k], payload: p, merged: false }); });
        have.sort((a, b) => a.num - b.num);
        const nums = new Map();
        for (const [i, p] of made) {
            const hit = find(p);
            nums.set(i, hit.num);
            if (!out.some(s => s.num === hit.num)) out.push({ num: hit.num, payload: hit.payload, merged: true });
        }
        const sampler = new Uint8Array(10 + ni * SAMPLER_SLOT);
        sampler.set(v1.subarray(0, 10));
        for (let i = 0; i < ni; i++) {
            const slot = v1.subarray(11 + i * SAMPLER_SLOT, 11 + (i + 1) * SAMPLER_SLOT);
            const use = slotV1Empty(slot) ? undefined : nums.get(bitsAt(slot, 0, 6));
            sampler.set(slotV1toV2(slot, use === undefined ? null : use), 10 + i * SAMPLER_SLOT);
        }
        return { sampler, samples: out, dropped: subs.filter(i => !made.has(i)).length };
    }
    return conv;
}
/* Records [{ kind, num, sub, ver, payload, ... }] (a disk, a backup, a read, kept tables, the emulator's flash) -> { recs,
 * converted: [sampler numbers], older: [numbers kept in the old form: their samples did not fit in the 256], samples: how
 * many samples were made, dropped: old audio with no entry (deleted), bad: [old samplers that would not read: kept],
 * olderSamples: the numbers the older ones need (their audio records), free: the sample numbers left free }.
 * The old samplers (kind 4, version 1), in number order, become version 2 where they are, their old audio (kind 0x10) samples
 * (kind 0x11, appended in number order); an old sampler that does not fit stays, marked older (with its audio), and so do all
 * after it. opts is reserved. */
function convertSamplers(recs, opts) {
    const olds = recs.filter(r => r.kind === 0x04 && r.ver === 1 && r.sub === 0 && r.payload).sort((a, b) => a.num - b.num);
    const audio = new Map(), existing = [], named = new Set();
    for (const r of recs) {
        if (!r.payload) continue;
        if (r.kind === 0x10 && r.ver === 1) { if (!audio.has(r.num)) audio.set(r.num, new Map()); audio.get(r.num).set(r.sub, r.payload); }
        else if (r.kind === 0x11) existing.push({ num: r.num, len: r.payload.length, payload: r.payload });
        else if (r.kind === 0x04 && r.ver === 2) for (const n of namedBy(r.payload)) named.add(n);
    }
    const conv = samplerConverter({ samples: existing, named });
    const done = new Map(), made = [], older = new Set(), bad = [];
    let dropped = 0;
    for (const r of olds) {
        if (conv.full) { older.add(r.num); continue; }
        let c;
        try { c = conv.convert(r.num, r.payload, audio.get(r.num) || new Map()); } catch (e) { bad.push(r.num); continue; }
        if (!c) { older.add(r.num); continue; }
        done.set(r.num, c.sampler);
        for (const s of c.samples) if (!s.merged) made.push(s);
        dropped += c.dropped;
    }
    const oldNums = new Set(olds.map(r => r.num)), out = [];
    for (const r of recs) {
        if (r.kind === 0x04 && r.ver === 1 && r.sub === 0 && r.payload && done.has(r.num)) {
            const payload = done.get(r.num);
            out.push(Object.assign({}, r, { ver: 2, len: payload.length, crc: crc32(payload), payload }));
        } else if (r.kind === 0x04 && r.ver === 1 && r.sub === 0 && older.has(r.num)) out.push(Object.assign({}, r, { older: true }));
        else if (r.kind === 0x10 && r.ver === 1 && r.payload) {
            if (done.has(r.num) || !oldNums.has(r.num)) { if (!done.has(r.num)) dropped++; }    /* converted, or no old sampler there: gone */
            else out.push(older.has(r.num) ? Object.assign({}, r, { older: true }) : r);
        } else out.push(r);
    }
    made.sort((a, b) => a.num - b.num);
    for (const s of made) out.push({ kind: 0x11, num: s.num, sub: 0, ver: 1, len: s.payload.length, crc: crc32(s.payload), payload: s.payload });
    /* (for the words past 256: the numbers the samplers left old need, a sample an audio record, and the numbers free) */
    let olderSamples = 0;
    for (const n of older) olderSamples += (audio.get(n) || new Map()).size;
    const used = new Set([...existing.map(s => s.num), ...named, ...made.map(s => s.num)]);
    return { recs: out, converted: [...done.keys()], older: [...older], samples: made.length, dropped, bad, olderSamples,
        free: SAMPLES_MAX - used.size };
}

/* ---- the conversion on an FM-1 (blob.md, Converting after an install; the emulator's kept flash: emulator.md) ---- */
const OLD_SAMPLER = r => r.kind === 0x04 && r.ver === 1 && r.sub === 0;
const OLD_SOUND = r => r.kind === AUDIO_OLD && r.ver === 1;
const takes = (L, kind, ver) => L.accepts.some(a => a.kind === kind && a.ver === ver);
const takesNewOnly = L => takes(L, 0x04, 2) && takes(L, 0x11, 1) && !takes(L, 0x04, 1);
/* What of the old form a layout lists that the firmware no longer reads (it takes samplers of version 2 and samples, not
 * the old samplers): { samplers: [numbers], audio: old audio records }, or null when there is nothing to convert (the
 * firmware another's or an older PT/FM, or nothing in the old form). The loops have nothing to convert: their record kept
 * version 4. */
function needsConversion(L) {
    if (!takesNewOnly(L)) return null;
    const live = L.records.filter(r => !r.deleted);
    const samplers = live.filter(OLD_SAMPLER).map(r => r.num).sort((a, b) => a - b), audio = live.filter(OLD_SOUND).length;
    return samplers.length || audio ? { samplers, audio } : null;
}
/* Converts what an FM-1 holds in the old form, through a seeding session with it (the install page's after INSTALL FIRMWARE,
 * the emulator's at its load): whole samplers a save, in number order, as many in a save as fit together keeping the
 * reserve (the first alone may go without it), each save a partial plan in free blocks only (planPartial): the samplers of
 * version 2 under their numbers, their new samples, their old audio deleted, and the old audio no sampler names deleted.
 * Before each save the layout is read again and the converter made anew from what the FM-1 holds (samplerConverter), so a
 * run cut short at any moment goes on in the next, numbering as one run would. (The blocks old audio frees are written
 * into by later saves: what is left there of it is no damaged record to the store, specs/files/store.md#power-on.) It reads
 * back (READ) only the old samplers and their audio, the samplers of version 2 (the numbers they name are passed over) and
 * the samples of a length a new one has (a merge compares only those). A SAVE on the FM-1 meanwhile (CHANGED: refused at
 * BEGIN, or a record read back wrong, moved by its compaction) has it read the layout and plan again.
 *   opts: { layout (the session's last hello, else it asks), onStep({ from, done, total, block, blocks }) at the start, as
 *         each save starts and after each of its blocks (from: the samplers converted before that save, done: once it is
 *         written), most: the samplers a save holds at most (the tests' 1) }
 *   -> { nothing: true when there was nothing to convert, total: old samplers at first, converted: [numbers], samples: new
 *        samples written, dropped: old audio with no sample entry deleted, older: [numbers left in the old form, their samples
 *        finding no free number], olderSamples, bad: [old samplers that would not parse: kept], saves, free: the free
 *        blocks it leaves, low: what so few cost the FM-1's saves when it leaves fewer than the reserve and than it found
 *        (or null), damaged: the last mount's damaged records, stopped: null, or { num, kb, piece, records, mapRoom,
 *        noDelete } when the next sampler does not fit even alone (kb more room in the free blocks; piece: they hold it
 *        but in pieces, so many KB needed in one; records more than the store's list holds; noDelete: the FM-1 left
 *        unable to save even a delete) }
 *   Throws what the session throws (hello refused: .code RESULT.UNSAVED; no answer: .timeout), with .result the above so far. */
async function convertDevice(ses, opts) {
    opts = opts || {};
    const step = opts.onStep || (() => {});
    let L = opts.layout || await ses.hello();
    const found = needsConversion(L);
    const res = { nothing: !found, total: found ? found.samplers.length : 0, converted: [], samples: 0, dropped: 0, older: [],
        olderSamples: 0, bad: [], saves: 0, low: null, damaged: 0, stopped: null };
    if (!found) return res;
    const most = opts.most || Infinity;
    const cache = new Map();                 /* (read once: a record never moves while it lives at that address) */
    const get = async r => {
        const k = r.addr + "." + r.len + "." + r.crc;
        if (!cache.has(k)) cache.set(k, await ses.readRecord(r));
        return cache.get(k);
    };
    const gone = r => ({ kind: r.kind, num: r.num, sub: r.sub, ver: r.ver });
    const freeOf = l => { const occ = occupiedBlocks(l); return userBlocks(l).filter(b => !occ.has(b.block)).length; };
    const freeBefore = freeOf(L);
    step({ from: 0, done: 0, total: res.total, block: 0, blocks: 0 });
    try {
        for (let round = 0, changed = 0; round < 1024; round++) {
            const live = L.records.filter(r => !r.deleted);
            const olds = live.filter(OLD_SAMPLER).sort((a, b) => a.num - b.num), audio = new Map();
            for (const r of live) if (OLD_SOUND(r)) { if (!audio.has(r.num)) audio.set(r.num, []); audio.get(r.num).push(r); }
            const oldNums = new Set(olds.map(r => r.num)), orphans = [];
            for (const [n, list] of audio) if (!oldNums.has(n)) orphans.push(...list);
            const todo = olds.filter(r => !res.bad.includes(r.num));
            if (!todo.length && !orphans.length) break;
            let write = [], del = orphans.map(gone), batch = [], P = null, stop = null, fullAt = -1;
            try {
                /* what it reads back: the numbers the samplers of version 2 name; the samples of a length a new one will have */
                const named = new Set(), lens = new Set(), there = [];
                for (const r of live) if (r.kind === 0x04 && r.ver === 2 && r.sub === 0) for (const n of namedBy(await get(r))) named.add(n);
                for (const s of todo) for (const a of audio.get(s.num) || []) lens.add(SAMPLE_ENTRY + a.len);
                for (const r of live) if (r.kind === 0x11) there.push(lens.has(r.len) ? { num: r.num, len: r.len, payload: await get(r) } : { num: r.num, len: r.len });
                const conv = samplerConverter({ samples: there, named });
                /* this save: whole samplers in number order while they fit together */
                for (const s of todo) {
                    if (batch.length >= most) break;
                    const auds = audio.get(s.num) || [], subs = new Map();
                    for (const a of auds) subs.set(a.sub, await get(a));
                    const v1 = await get(s);
                    let c;
                    try { c = conv.convert(s.num, v1, subs); } catch (e) { res.bad.push(s.num); continue; }   /* (it will not parse) */
                    if (!c) { fullAt = s.num; break; }
                    const fresh = c.samples.filter(x => !x.merged);
                    const w = write.concat([{ kind: 0x04, num: s.num, sub: 0, ver: 2, payload: c.sampler }],
                        fresh.map(x => ({ kind: 0x11, num: x.num, sub: 0, ver: 1, payload: x.payload })));
                    const d = del.concat(auds.map(gone));
                    const Q = planPartial(L, w, d);
                    if (Q.ok && (Q.reserveKept || !batch.length)) {
                        write = w; del = d; P = Q;
                        batch.push({ num: s.num, samples: fresh.length, dropped: c.dropped });
                        if (!Q.reserveKept) break;       /* (without the reserve: alone) */
                    } else {
                        if (!batch.length) stop = { num: s.num, kb: Q.space.short * 4, piece: Q.space.piece, records: Q.space.mapShort,
                            mapRoom: L.mapRoom };
                        break;
                    }
                }
            } catch (e) {                            /* (a record read back wrong: moved meanwhile, as CHANGED) */
                if (e && e.stale && changed++ < 8) { L = await ses.hello(); continue; }
                throw e;
            }
            if (!batch.length) {
                if (orphans.length) P = planPartial(L, [], del);     /* (the old audio no sampler names goes first: room for
                                                                        the next) */
                if (!P || !P.ok) {
                    if (fullAt >= 0) for (const s of todo) if (s.num >= fullAt && !res.bad.includes(s.num)) {
                        res.older.push(s.num);
                        res.olderSamples += (audio.get(s.num) || []).length;
                    }
                    res.stopped = stop;
                    break;
                }
            }
            const from = res.converted.length, done = from + batch.length;
            step({ from, done, total: res.total, block: 0, blocks: P.blocks.length });
            let r;
            try {
                r = await ses.write(P, p => step({ from, done, total: res.total, block: p.done, blocks: p.total }));
            } catch (e) {
                if (e && e.code === RESULT.CHANGED && changed++ < 8) { L = await ses.hello(); continue; }   /* (refused at BEGIN: nothing written) */
                throw e;
            }
            res.saves++;
            for (const b of batch) { res.converted.push(b.num); res.samples += b.samples; res.dropped += b.dropped; }
            res.dropped += orphans.length;
            res.damaged = r.bad;
            L = await ses.hello();
        }
        /* what the run leaves, from the last layout (every save's old audio freed by then; a save's own plan.low is the room
           before its deletions count): low-space words only when it leaves fewer free blocks than the reserve and than it
           found; a stop's remedies by what the FM-1 can still save (under SAVE_MIN_FREE free blocks, not even a delete) */
        res.free = freeOf(L);
        res.low = res.saves && res.free < L.reserve && res.free < freeBefore ? lowSpace(res.free, L.reserve) : null;
        if (res.stopped) res.stopped.noDelete = res.free < SAVE_MIN_FREE;
    } catch (e) {
        if (e && typeof e === "object") e.result = res;
        throw e;
    }
    return res;
}
/* A disk read for an upload (parseData's), made ready for the device whose layout this is: in the old form while the device
 * takes the new form only, it is converted as it loads (convertSamplers), .converted then saying { samplers, samples,
 * dropped, older, bad }; samplers left in the old form (past 256 samples) are a problem then, as the device would not
 * take them. Else the same data, untouched. */
function upgradeFor(layout, data) {
    if (!takesNewOnly(layout) || !data.files.some(f => OLD_SAMPLER(f) || OLD_SOUND(f))) return data;
    const c = convertSamplers(data.files);
    const files = c.recs.map(r => { const f = Object.assign({}, r); delete f.older; if (!f.label) f.label = describe(f); return f; });
    const problems = data.problems.slice();
    if (c.older.length) {
        const one = c.older.length === 1;
        problems.push((one ? "sampler " : "samplers ") + c.older.map(hex2).join(", ") + " of the older form: " + (one ? "its" : "their") +
            " samples find no free sample number (256 at most); the EDITOR page converts the rest and lists " + (one ? "it" : "them") +
            " as OLDER FORM");
    }
    return { kind: data.kind, files, notes: data.notes, problems,
        converted: { samplers: c.converted.length, samples: c.samples, dropped: c.dropped, older: c.older, bad: c.bad } };
}

function midiTransport(input, output) {
    const cbs = [];
    input.addEventListener("midimessage", e => {
        const d = e.data;
        if (d[0] === 0xF0 && d[1] === 0x7D && d[2] === 0x50 && d[3] === 0x54) for (const cb of cbs) cb(d);
    });
    return { send(bytes) { output.send(Array.from(bytes)); }, onMessage(fn) { cbs.push(fn); } };
}

const S = { parseData, parseLayout, plan, planClean, planPartial, describe, session, midiTransport, makeDisk, crc32, message, parseMessage,
    convertSamplers, samplerConverter, needsConversion, convertDevice, upgradeFor, sampleOk,
    pack7, unpack7, CMD, RESULT, RESULT_TEXT, KIND_NAME, BLOCK };
if (typeof module !== "undefined" && module.exports) module.exports = S;
else root.PTFM_seed = S;
})(typeof window !== "undefined" ? window : globalThis);

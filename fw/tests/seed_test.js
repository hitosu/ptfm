/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Seeding end to end: fw/web/seed.js, the uploader, against the device's seeding code built natively
 * (seed_bridge.c on a fake flash). The demo disk seeded into an empty FM-1 and read back record for
 * record; seeding again (nothing to do); the device's own records deleted; one changed record; power
 * cuts while writing (the library before stays whole); refusals; the time-out; the map's room. Then, with OLD.ptfmdsk
 * (the demo disk in the old form, as an older PT/FM's: pack.py --old-form), the conversion after an install
 * (blob.md, Converting after an install: convertDevice), through the device's own code.
 *   node seed_test.js BRIDGE DISK.ptfmdsk [OLD.ptfmdsk] */
"use strict";
const fs = require("fs");
const { spawn } = require("child_process");
const S = require("../web/seed.js");

const [bridgePath, diskPath, oldPath, convPath] = process.argv.slice(2);
let checks = 0, fails = 0;
function check(c, msg) { checks++; if (!c) { fails++; console.log("FAIL " + msg); } }

/* ---- the bridge: a command, then its answer's lines until "." ---- */
const br = spawn(bridgePath, [], { stdio: ["pipe", "pipe", "inherit"] });
let buf = "", listeners = [], waiting = [];
const onMsg = bytes => { for (const f of listeners) f(bytes); };
br.stdout.on("data", d => {
    buf += d.toString();
    let i;
    while ((i = buf.indexOf("\n")) >= 0) {
        const line = buf.slice(0, i); buf = buf.slice(i + 1);
        if (line.startsWith("S ")) {
            const bytes = Uint8Array.from(line.slice(2).match(/../g).map(h => parseInt(h, 16)));
            setImmediate(() => onMsg(bytes));
        } else if (line === ".") {
            const w = waiting.shift();
            if (w) w.resolve(w.lines);
        } else if (waiting.length) {
            if (line.startsWith("E ")) { check(false, line); }
            waiting[0].lines.push(line);
        }
    }
});
const DEBUG = !!process.env.SEED_DEBUG, T0 = Date.now();
function cmd(line) {
    if (DEBUG) console.log((Date.now() - T0) + " > " + line.slice(0, 40));
    return new Promise(resolve => { waiting.push({ lines: [], resolve }); br.stdin.write(line + "\n"); });
}
br.on("exit", (code, sig) => { if (waiting.length) console.log("FAIL the bridge stopped: " + (sig || code)); });
const hex = b => Array.from(b, x => (x < 16 ? "0" : "") + x.toString(16).toUpperCase()).join("");
let sent = 0, biggest = 0;
const transport = {
    send(bytes) { sent++; biggest = Math.max(biggest, bytes.length); cmd("M " + hex(bytes)); },
    onMessage(cb) { listeners.push(cb); }
};

async function dump() {
    const lines = await cmd("DUMP");
    return lines.filter(l => l.startsWith("R ")).map(l => {
        const [kind, num, sub, ver, len, crc] = l.slice(2).split(" ").map(x => parseInt(x, 16));
        return { kind, num, sub, ver, len, crc: crc >>> 0 };
    });
}
const key = r => r.kind << 16 | r.num << 8 | r.sub;
function same(dev, files, what) {
    const want = new Map(files.map(f => [key(f), f]));
    check(dev.length === files.length, what + ": the device holds " + dev.length + " records, the disk " + files.length);
    let bad = 0;
    for (const r of dev) {
        const f = want.get(key(r));
        if (!f || f.ver !== r.ver || f.len !== r.len || f.crc !== r.crc) bad++;
    }
    check(bad === 0, what + ": " + bad + " records differ from the disk");
}
function changed(data, n) {               /* a copy of the disk with n patterns' payloads changed */
    const files = data.files.map(f => Object.assign({}, f));
    let k = 0;
    for (const f of files) {
        if (f.kind !== 0x20 || k >= n) continue;
        f.payload = Uint8Array.from(f.payload);
        f.payload[f.payload.length - 1] ^= 0x01;
        f.crc = S.crc32(f.payload);
        k++;
    }
    return { kind: "disk", files, notes: null, problems: [] };
}

async function seed(data, sess, what) {
    const L = await sess.hello();
    const P = S.plan(L, data);
    check(P.ok, what + ": the plan: " + P.problems.join("; "));
    if (!P.ok || P.upToDate) return { L, P, res: null };
    let last = 0;
    const res = await sess.write(P, p => { last = p.done; });
    check(last === P.blocks.length, what + ": progress reached every block");
    return { L, P, res };
}

/* ---- the old samplers' converter (no device): one sampler at a time numbers exactly as the whole set at once ---- */
function bitsInto(b, at, v, n) { for (let i = 0; i < n; i++, at++) if ((v >>> i) & 1) b[at >> 3] |= 1 << (at & 7); }
function oldEntry(len, name) {                       /* length 20, rate 18, coding 4, name 60, spare 26 */
    const b = new Uint8Array(16);
    bitsInto(b, 0, len, 20); bitsInto(b, 20, 22050, 18); bitsInto(b, 38, 1, 4);
    for (let i = 0; i < 10; i++) bitsInto(b, 42 + 6 * i, i < name.length ? 1 + name.charCodeAt(i) - 65 : 0, 6);
    return b;
}
function oldSlot(sample, name) {                     /* SAMPLE 6 bits, the name at bit 174 (version 1) */
    const b = new Uint8Array(40);
    if (sample === null) { bitsInto(b, 174, 63, 6); return b; }
    bitsInto(b, 0, sample, 6); bitsInto(b, 6, 99, 20); bitsInto(b, 174, 1, 6); bitsInto(b, 180, name.charCodeAt(0) - 64, 6);
    bitsInto(b, 300, 0x5555 & 0xFFFF, 16);           /* some spare bits set: they must travel */
    return b;
}
function oldSampler(slots, entries) {
    const b = new Uint8Array(11 + 40 * slots.length + 16 * entries.length);
    b[9] = slots.length; b[10] = entries.length;
    slots.forEach((s, i) => b.set(s, 11 + 40 * i));
    entries.forEach((e, i) => b.set(e, 11 + 40 * slots.length + 16 * i));
    return b;
}
const aud = k => Uint8Array.from([1, 8, 0, 0, k & 255, k >> 8, 7, 7]);
const rec = (kind, num, sub, ver, payload) => ({ kind, num, sub, ver, len: payload.length, crc: S.crc32(payload), payload,
    label: S.describe({ kind, num, sub }) });
/* two old samplers sharing a sample (02's SNARE = 05's, entry and audio), old audio no sampler names (09), a sample already
 * there (01, 05's CLAP), a version 2 sampler naming 00 */
const sharedSet = () => [rec(4, 2, 0, 1, oldSampler([oldSlot(0, "K"), oldSlot(1, "S"), oldSlot(null)], [oldEntry(8, "KICK"), oldEntry(9, "SNARE")])),
    rec(0x10, 2, 0, 1, aud(1)), rec(0x10, 2, 1, 1, aud(2)),
    rec(4, 5, 0, 1, oldSampler([oldSlot(0, "S"), oldSlot(1, "C")], [oldEntry(9, "SNARE"), oldEntry(5, "CLAP")])),
    rec(0x10, 5, 0, 1, aud(2)), rec(0x10, 5, 1, 1, aud(3)), rec(0x10, 9, 0, 1, aud(4)),
    rec(0x11, 1, 0, 1, Uint8Array.from([...oldEntry(5, "CLAP"), ...aud(3)])),
    /* a version 2 sampler: one instrument naming sample 00 (its name at bit 176) */
    rec(4, 7, 0, 2, Uint8Array.from([0, 0, 0, 0, 0, 0, 0, 0, 0, 1, ...Array.from({ length: 40 }, (_, i) => i === 22 ? 1 : 0)]))];
/* old samplers 01-05, 64 samples each but 05's 3 (259 in all); with4: 01-04 alone (256); big: each sample's audio 300 B
 * (crossing blocks: 05's shares a block with 04's) instead of 8 */
function fullSet(with4, big) {
    const out = [];
    for (let s = 1; s <= (with4 ? 4 : 5); s++) {
        const n = s === 5 ? 3 : 64, entries = [], slots = [];
        for (let i = 0; i < n; i++) { entries.push(oldEntry(big ? 300 : 8, "S" + i)); slots.push(oldSlot(i, "I")); }
        out.push(rec(4, s, 0, 1, oldSampler(slots, entries)));
        for (let i = 0; i < n; i++)
            out.push(rec(0x10, s, i, 1, big ? Uint8Array.from({ length: 300 }, (_, j) => (j * (i + 3) + s * 7) & 255) : aud(s * 100 + i)));
    }
    return out;
}
function converterTest() {
    const sets = { shared: sharedSet(), full: fullSet() };
    for (const [name, recs] of Object.entries(sets)) {
        const all = S.convertSamplers(recs);
        const ex = recs.filter(x => x.kind === 0x11).map(x => ({ num: x.num, len: x.payload.length, payload: x.payload }));
        const named = new Set();
        for (const x of recs) if (x.kind === 4 && x.ver === 2) for (let i = 0; i < x.payload[9]; i++) { const s = x.payload.subarray(10 + 40 * i, 50 + 40 * i); if ((s[22] & 63) !== 63) named.add(s[0]); }
        const conv = S.samplerConverter({ samples: ex, named });
        const old = recs.filter(x => x.kind === 4 && x.ver === 1).sort((a, b) => a.num - b.num), samplers = new Map(), made = new Map();
        for (const o of old) {
            if (conv.full) break;
            const audio = new Map(recs.filter(x => x.kind === 0x10 && x.num === o.num).map(x => [x.sub, x.payload]));
            const c = conv.convert(o.num, o.payload, audio);
            if (!c) continue;
            samplers.set(o.num, c.sampler);
            for (const s of c.samples) if (!s.merged) made.set(s.num, s.payload);
        }
        const hx = b => Array.from(b, x => (x < 16 ? "0" : "") + x.toString(16)).join("");
        check(JSON.stringify([...samplers].map(([n, p]) => n + ":" + hx(p))) ===
            JSON.stringify(all.recs.filter(x => x.kind === 4 && x.ver === 2 && samplers.has(x.num)).map(x => x.num + ":" + hx(x.payload))), name + ": the samplers converted one at a time equal the set's");
        const want = all.recs.filter(x => x.kind === 0x11 && !ex.some(e => e.num === x.num)).map(x => x.num + ":" + hx(x.payload));
        check(JSON.stringify([...made].sort((a, b) => a[0] - b[0]).map(([n, p]) => n + ":" + hx(p))) === JSON.stringify(want), name + ": and so do the samples, numbered alike (" + want.length + ")");
        check(samplers.size === all.converted.length, name + ": the same samplers converted (" + all.converted + ")");
        if (name === "shared") {
            const nums = all.recs.filter(x => x.kind === 0x11).map(x => x.num).join();
            check(nums === "1,2,3", "shared: 00 is named by the version 2 sampler, 01 is there; the new samples take 02 and 03, the shared SNARE once (" + nums + ")");
            check(all.dropped === 1 && !all.recs.some(x => x.kind === 0x10), "shared: the audio of no sampler dropped, the rest gone");
        } else check(all.samples === 256 && all.older.join() === "5" && conv.full, "full: 256 samples, the sampler that does not fit stays old");
    }
}

const isAudio = k => (k & 0xFE) === 0x10;      /* both kinds of audio: the samples (0x11) and the old form's (0x10) */

/* ---- the conversion after an install (specs/files/blob.md#converting-after-an-install): an FM-1 holding the old form, as
 * an older PT/FM's seeding laid it, converted by seed.js's convertDevice through the device's own code ---- */
const OLD_PAIRS = [{ kind: 0x04, ver: 1 }, { kind: 0x10, ver: 1 }];
const olderLayout = (L, more) => Object.assign({}, L, { accepts: L.accepts.concat(OLD_PAIRS, more || []) });
const spans = (r, b) => { const end = r.addr + 16 + (r.deleted ? 0 : (r.len + 3) & ~3); return r.addr >>> 12 <= b && b <= (end - 1) >>> 12; };
/* what the conversion may read (blob.md): the old samplers and their audio, the version 2 samplers, the samples of a length
 * an old sample would have (16 + its audio) */
function readAllowed(L, a, n) {
    const live = L.records.filter(r => !r.deleted);
    const lens = new Set(live.filter(r => r.kind === 0x10 && r.ver === 1).map(r => 16 + r.len));
    return live.some(r => a >= r.addr && a + n <= r.addr + 16 + r.len &&
        ((r.kind === 4 && (r.ver === 1 || r.ver === 2)) || (r.kind === 0x10 && r.ver === 1) || (r.kind === 0x11 && lens.has(r.len))));
}
/* a session watched: every read one the conversion may make, every write a partial plan into blocks no record of the layout
 * before it touches; a cut (the cable pulled) after a number of saves; hooks after a save and after a layout */
function watched(opts) {
    const ses = S.session(transport, opts || { timeout: 1500 });
    const w = { ses, L: null, reads: 0, badReads: 0, writes: 0, over: 0, whole: 0, cutAfter: 0, afterWrite: null, afterHello: null,
        spoil: null };
    const hello = ses.hello.bind(ses), read = ses.read.bind(ses), write = ses.write.bind(ses);
    ses.hello = async () => {
        if (w.cutAfter && w.writes >= w.cutAfter) throw new Error("the cable pulled after save " + w.writes);
        w.L = await hello();
        if (w.afterHello) await w.afterHello(w);
        return w.L;
    };
    ses.read = async (a, n) => {
        w.reads++;
        if (!readAllowed(w.L, a, n)) w.badReads++;
        const d = await read(a, n);
        if (w.spoil && w.spoil(a)) { const x = Uint8Array.from(d); x[x.length - 1] ^= 0x55; return x; }   /* (read back wrong) */
        return d;
    };
    ses.write = async (P, cb) => {
        if (!P.partial) w.whole++;
        for (const b of P.blocks) if (w.L.records.some(r => spans(r, b.block))) w.over++;
        const r = await write(P, cb);
        w.writes++;
        if (w.afterWrite) await w.afterWrite(w);
        return r;
    };
    return w;
}
async function layOld(files, more) {            /* the user area emptied, then the files laid as an older PT/FM's seeding did */
    const ses = S.session(transport, { timeout: 1500 });
    const L = await ses.hello();
    const P = S.planClean(olderLayout(L, more), { files, problems: [] });
    check(P.ok, "the old form laid: " + P.problems.join("; "));
    if (P.ok) await ses.write(P, () => {});
    ses.close();
}
const named = p => { const out = []; for (let i = 0; i < p[9]; i++) if ((p[10 + 40 * i + 22] & 63) !== 63) out.push(p[10 + 40 * i]); return out; };
const sameRec = (a, b) => !!a && !!b && a.ver === b.ver && a.len === b.len && a.crc === b.crc;
/* after a cut: each old sampler either as it was (all its audio there) or converted whole (as one run makes it, every sample
 * it names there), and everything else as it was */
function wholeAfterCut(dev, oldFiles, want, what) {
    const got = new Map(dev.map(r => [key(r), r])), wants = new Map(want.map(r => [key(r), r]));
    let bad = 0, conv = 0;
    for (const f of oldFiles) {
        if (f.kind === 0x04 && f.ver === 1) {
            const g = got.get(key(f));
            if (g && g.ver === 1) {
                if (!sameRec(g, f) || oldFiles.some(a => a.kind === 0x10 && a.num === f.num && !sameRec(got.get(key(a)), a))) bad++;
            } else if (g && g.ver === 2) {
                conv++;
                const w = wants.get(key(f));
                if (!sameRec(g, w) || named(w.payload).some(n => { const k = 0x11 << 16 | n << 8; return !sameRec(got.get(k), wants.get(k)); })) bad++;
            } else bad++;
        } else if (f.kind !== 0x10 && !sameRec(got.get(key(f)), f)) bad++;
    }
    check(bad === 0, what + ": " + bad + " objects neither as they were nor converted whole (" + conv + " converted)");
}
async function conversionTests(oldDisk, newDisk) {
    console.log("  the conversion after an install:");
    const want = S.convertSamplers(oldDisk.files).recs;
    /* the layout's map room; a partial plan: records written and deleted, nothing else, into free blocks only; the map's room
     * in every plan */
    {
        await layOld(oldDisk.files);
        const L = await sess0.hello();
        check(L.mapRoom === 560, "the layout says the map's room: " + L.mapRoom);
        const s = rec(0x11, 0x40, 0, 1, new Uint8Array(9000).fill(3)), gone = L.records.find(r => r.kind === 0x10 && !r.deleted);
        let P = S.planPartial(L, [s], [gone]);
        check(P.ok && P.partial && P.reserveKept && !P.keep.length && P.write.length === 1 && P.del.length === 1 &&
              P.blocks.length >= 3 && P.blocks.every(b => !L.records.some(r => spans(r, b.block))),
              "a partial plan: one sample written, one old audio deleted, in free blocks only (" + P.blocks.length + " blocks; " + P.problems.join("; ") + ")");
        const tight = Object.assign({}, L, { mapRoom: L.records.length });
        P = S.planPartial(tight, [s], []);
        check(!P.ok && P.space.mapShort === 1 && P.problems.some(p => p.includes("list of records")),
              "a partial plan past the map's room: refused (" + P.problems.join("; ") + ")");
        P = S.planPartial(tight, [], [gone]);
        check(P.ok, "a deletion adds nothing to the map: planned at its full room");
        P = S.planPartial(L, [rec(0x11, 0x40, 0, 2, new Uint8Array(40))], []);
        check(!P.ok && P.problems.some(p => p.includes("version 2")), "a partial plan checks what it writes: " + P.problems.join("; "));
        P = S.planPartial(L, [rec(0x20, 0x41, 0, 9, new Uint8Array(40))], []);
        check(!P.ok, "(a pattern of version 9 refused there)");
        P = S.plan(Object.assign({}, L, { mapRoom: 50 }), newDisk);
        check(!P.ok && P.problems.some(p => p.includes("list of records")), "a seeding past the map's room: refused (" + P.problems.join("; ") + ")");
        P = S.planClean(Object.assign({}, L, { mapRoom: 50 }), newDisk);
        check(!P.ok && P.problems.some(p => p.includes("list of records")), "a clean seeding past the map's room: refused");
        check(S.plan(L, newDisk).ok && S.planClean(L, newDisk).ok, "within it: planned");
        /* a sample whose entry and audio differ (coding or length): written as it is (the device takes it), named as one the
         * FM-1 reads as missing, in every plan; one whose two agree is not */
        {
            const okS = rec(0x11, 0x41, 0, 1, Uint8Array.from([...oldEntry(8, "OK"), ...aud(1)])), odd = rec(0x11, 0x42, 0, 1, Uint8Array.from([...oldEntry(9, "ODD"), ...aud(2)]));
            const d = { kind: "disk", files: [okS, odd], problems: [] };
            const plans = [S.plan(L, d), S.planClean(L, d), S.planPartial(L, [okS, odd], [])];
            check(S.sampleOk(okS.payload) && !S.sampleOk(odd.payload) && plans.every(P => P.ok && P.differ.join() === "SAMPLE 42" &&
                  P.summary.some(t => t.startsWith("MISSING: SAMPLE 42: its entry and audio differ"))),
                  "a sample whose entry and audio differ: every plan writes it and names it as missing on the FM-1 (" + plans[0].summary.find(t => t.startsWith("MISSING")) + ")");
        }
        /* free room in pieces: every third block holding a pattern, a 20 KB sample: enough room in amount, none in one piece */
        {
            const regions = L.regions, recs = [];
            let n = 0;
            for (const g of regions) for (let a = g.start; a < g.end; a += 3 * 4096)
                recs.push({ kind: 0x20, num: n & 255, sub: n >> 8, ver: 1, crc: 1, addr: a, len: 2000, save: 1, deleted: false, label: "P" + n++ });
            const Lp = Object.assign({}, L, { records: recs });
            const Pp = S.planPartial(Lp, [rec(0x11, 0, 0, 1, new Uint8Array(20000).fill(5))], []);
            check(!Pp.ok && Pp.space.piece === 20 && !Pp.space.short && Pp.space.free > 60 && Pp.problems.some(p => p.includes("20 KB in one piece")),
                  "free room in pieces too small: " + Pp.space.free * 4 + " KB free, 20 KB needed in one piece (" + Pp.problems.join("; ") + ")");
        }
        /* an old-form disk uploaded is converted as it loads, where the FM-1 takes the new form only */
        const up = S.upgradeFor(L, oldDisk);
        same(up.files, newDisk.files, "an old-form disk converted as it loads");
        check(up.converted && up.converted.samplers === 5 && up.converted.samples === 33 && !up.problems.length,
              "  ... 5 samplers, 33 samples (" + JSON.stringify(up.converted) + ")");
        check(S.upgradeFor(olderLayout(L), oldDisk) === oldDisk && S.upgradeFor(L, newDisk) === newDisk,
              "an FM-1 that takes the old form, a disk of the new form: as they are");
        const upf = S.upgradeFor(L, { kind: "disk", files: fullSet(), notes: null, problems: [] });
        check(upf.problems.some(p => p.includes("older form")), "an old-form disk past 256 samples: refused, saying why (" + upf.problems[0] + ")");
    }
    /* the shapes: two old samplers sharing a sample, old audio no sampler names, a sample there, a version 2 sampler naming 00,
     * and records of other kinds and versions (a song, a pattern of version 9, the loops of version 3, an unknown kind): each
     * converted as one run converts the set, everything else kept where it was; in one save, then one sampler a save (05's
     * SNARE merged with the sample 02's save wrote, the numbers taken by then passed over) */
    const more = [rec(0x21, 3, 0, 1, Uint8Array.of(1, 2, 3, 4, 5, 6)), rec(0x20, 4, 0, 9, Uint8Array.of(9, 9, 9, 9)),
        rec(0x22, 0, 0, 3, Uint8Array.of(3, 3, 3, 3)), rec(0x7E, 1, 0, 1, Uint8Array.of(7, 7))];
    const shapes = sharedSet().concat(more), oddPairs = [{ kind: 0x20, ver: 9 }, { kind: 0x22, ver: 3 }, { kind: 0x7E, ver: 1 }];
    for (const most of [0, 1]) {
        const how = most ? "one sampler a save" : "in one save";
        await layOld(shapes, oddPairs);
        const w = watched();
        const L0 = await w.ses.hello(), found = S.needsConversion(L0);
        check(found && found.samplers.join() === "2,5" && found.audio === 5, "the old samplers found in the layout: " + JSON.stringify(found));
        check(!S.needsConversion(olderLayout(L0)), "nothing to convert where the firmware takes the old form");
        const res = await S.convertDevice(w.ses, { layout: L0, most });
        const L1 = await w.ses.hello();
        same(await dump(), S.convertSamplers(shapes).recs, "the shapes converted as one run, " + how);
        check(res.total === 2 && res.converted.join() === "2,5" && res.samples === 2 && res.dropped === 1 && !res.older.length &&
              !res.stopped && res.saves === (most ? 2 : 1), "  ... 2 samplers, 2 samples, 1 old audio deleted, " + how + ": " + JSON.stringify(res));
        check(more.every(m => { const a = L0.records.find(r => key(r) === key(m)), b = L1.records.find(r => key(r) === key(m));
            return a && b && a.addr === b.addr && a.crc === b.crc && b.ver === m.ver; }),
              "  ... records of other kinds and versions kept where they were, as they were");
        check(w.reads > 0 && !w.badReads, "  ... it read only what the spec lists (" + w.reads + " reads)");
        check(!w.over && !w.whole && w.writes === res.saves && res.damaged === 0 && (await badNow()) === 0,
              "  ... partial plans only, into free blocks only, nothing damaged");
        check(S.needsConversion(L1) === null, "  ... then nothing is left to convert");
        const writes = w.writes, again = await S.convertDevice(w.ses);
        check(again.nothing && w.writes === writes, "  ... run again: nothing to do, nothing written");
        w.ses.close();
    }
    /* a record read back wrong (moved meanwhile by a save on the FM-1, as its compaction moves records): the layout read
     * again and planned again, as CHANGED; an old sampler, a sampler of version 2 and a sample each so once */
    {
        await layOld(shapes, oddPairs);
        const w = watched(), spoilt = new Set();
        w.spoil = a => {
            const r = w.L.records.find(x => !x.deleted && a >= x.addr && a < x.addr + 16 + x.len);
            const k = r && (r.kind === 4 ? "4." + r.ver : r.kind === 0x11 ? "11" : "");
            if (!k || spoilt.has(k)) return false;
            spoilt.add(k);
            return true;
        };
        const res = await S.convertDevice(w.ses);
        same(await dump(), S.convertSamplers(shapes).recs, "records read back wrong (" + [...spoilt].join(" ") + "): planned again, as one run");
        check(spoilt.size === 3 && !res.bad.length && res.converted.join() === "2,5", "  ... no sampler taken for one that will not read: " +
              JSON.stringify(res.bad));
        w.ses.close();
    }
    /* a sampler saved on the FM-1 between two saves, naming the number the next sample would take: the layout read again
     * (CHANGED), the numbers its instruments name read again and passed over */
    {
        const third = [rec(4, 8, 0, 1, oldSampler([oldSlot(0, "N")], [oldEntry(7, "NEW")])), rec(0x10, 8, 0, 1, aud(77))];
        await layOld(shapes.concat(third), oddPairs);
        const w = watched();
        let named = -1;
        w.afterWrite = async () => {
            if (w.writes !== 1) return;
            const L = await sess0.hello(), taken = new Set(L.records.filter(r => r.kind === 0x11 && !r.deleted).map(r => r.num));
            taken.add(0);                                    /* (sampler 07 names 00) */
            named = 0;
            while (taken.has(named)) named++;
            const p = Uint8Array.from([0, 0, 0, 0, 0, 0, 0, 0, 0, 1, ...Array.from({ length: 40 }, (_, i) => i === 0 ? named : i === 22 ? 1 : 0)]);
            await cmd("SAVE 4 30 0 2 " + hex(p));
        };
        const res = await S.convertDevice(w.ses, { most: 1 });
        const d = await dump();
        check(named >= 0 && res.converted.join() === "2,5,8" && !d.some(r => r.kind === 0x11 && r.num === named) &&
              d.some(r => r.kind === 0x11 && r.num === named + 1) && d.some(r => r.kind === 4 && r.num === 0x30) && res.samples === 3,
              "a sampler saved on the FM-1 between saves naming sample " + named + ": that number passed over (" +
              d.filter(r => r.kind === 0x11).map(r => r.num).join(",") + ")");
        w.ses.close();
    }
    /* the old demo disk (pack.py --old-form, the released one): more audio than the free room, so several saves, each keeping
     * the reserve; then the FM-1 holds exactly what the new demo disk uploads */
    let saves = 0;
    {
        await layOld(oldDisk.files);
        const w = watched(), steps = [];
        const L0 = await w.ses.hello();
        const res = await S.convertDevice(w.ses, { layout: L0, onStep: p => steps.push(p) });
        saves = res.saves;
        same(await dump(), newDisk.files, "the old demo disk converted: the new demo disk's records");
        check(S.plan(await w.ses.hello(), newDisk).upToDate, "  ... the new demo disk uploaded then: nothing to write");
        check(res.total === 5 && res.converted.join() === "160,161,162,163,164" && res.samples === 33 && res.saves > 1 && !res.low &&
              !res.dropped && !res.stopped, "  ... 5 samplers, 33 samples, in " + res.saves + " saves, the reserve kept");
        check(res.damaged === 0, "  ... the last mount found no damaged record (" + res.damaged + ")");
        check(steps.length > 0 && steps.every(p => p.total === 5 && p.done <= 5) && steps[steps.length - 1].done === 5 &&
              steps.some(p => p.done > 0 && p.done < 5), "  ... its steps, N of 5: " + [...new Set(steps.map(p => p.done))].join(" "));
        check(w.reads > 0 && !w.badReads && !w.over && !w.whole, "  ... read only what the spec lists, wrote into free blocks only");
        console.log("    the old demo disk: " + L0.records.length + " records, " + res.converted.length + " samplers and " +
            res.samples + " samples in " + res.saves + " saves, " + w.reads + " reads; steps " + [...new Set(steps.map(p => p.done))].join(" "));
        await cmd("BOOT");
        same(await dump(), newDisk.files, "  ... and so after a power-cycle");
        w.ses.close();
    }
    /* cut short after each save (the cable pulled before the next layout), the last one too: what is converted is whole, the
     * rest as it was; the next run goes on, numbered as one run (after the last: nothing to do); and the device's mount, an
     * empty seeding's, counts nothing damaged, then or after that run */
    for (let k = 1; k <= saves; k++) {
        await layOld(oldDisk.files);
        const w = watched();
        w.cutAfter = k;
        let e = null;
        try { await S.convertDevice(w.ses); } catch (x) { e = x; }
        check(e && e.result && e.result.converted.length > 0 && e.result.converted.length <= 5, "cut after save " + k + ": stopped there, " +
              (e && e.result ? e.result.converted.length : "?") + " samplers converted");
        wholeAfterCut(await dump(), oldDisk.files, want, "cut after save " + k);
        await cmd("BOOT");
        const bad1 = await badNow();
        const w2 = watched();
        const r2 = await S.convertDevice(w2.ses);
        same(await dump(), newDisk.files, "cut after save " + k + ", run again: as one run");
        const bad2 = await badNow();
        check(!w2.over && !w2.badReads && bad1 === 0 && bad2 === 0 && r2.nothing === (k === saves),
              "  ... into free blocks, reading only what it may; damaged at the power-on after the cut " + bad1 + ", after the run " + bad2);
        w.ses.close(); w2.ses.close();
    }
    /* power cuts inside saves: the cut save counts for nothing; the next run goes on, numbered as one run, and the device's
     * mount (an empty seeding's) counts nothing damaged after it */
    let cuts = 0;
    const leftBad = [];
    for (const [ops, torn] of [[1, 0], [9, 1], [30, 0], [70, 1], [130, 0], [200, 1], [300, 0], [420, 1], [560, 0], [720, 1], [900, 0]]) {
        await layOld(oldDisk.files);
        await cmd("CUT " + ops + " " + torn);
        const w = watched({ timeout: 300, blockTimeout: 300, endTimeout: 300 });
        let e = null;
        try { await S.convertDevice(w.ses); } catch (x) { e = x; }
        w.ses.close();
        await cmd("BOOT");
        if (!e) { same(await dump(), newDisk.files, "power cut after " + ops + ": the conversion had finished"); continue; }
        cuts++;
        wholeAfterCut(await dump(), oldDisk.files, want, "power cut after " + ops + " flash operations" + (torn ? " (torn)" : ""));
        const w2 = watched();
        const r2 = await S.convertDevice(w2.ses);
        w2.ses.close();
        same(await dump(), newDisk.files, "power cut after " + ops + (torn ? " (torn)" : "") + ", run again: as one run (" +
             (r2.nothing ? "nothing left to do" : r2.saves + " saves") + ")");
        leftBad.push(await badNow());
    }
    check(cuts >= 6, "conversions cut by power cuts inside saves: " + cuts);
    check(leftBad.every(x => x === 0), "  ... and the runs after them leave nothing the device counts damaged: " + leftBad.join(" "));
    console.log("    cut short after each of saves 1-" + saves + " and by " + cuts + " power cuts inside saves: each finished by the " +
        "next run as one run, nothing counted damaged");
    /* edits made on the FM-1 meanwhile: it stops (SAVE OR REVERT FIRST); the next run goes on */
    {
        await layOld(oldDisk.files);
        const w = watched();
        w.afterWrite = async () => { if (w.writes === 1) await cmd("UNSAVED 1"); };
        let e = null;
        try { await S.convertDevice(w.ses); } catch (x) { e = x; }
        check(e && e.code === S.RESULT.UNSAVED && e.result && e.result.converted.length >= 1,
              "edits not saved on the FM-1 meanwhile: stopped (" + (e && e.message) + ")");
        await cmd("UNSAVED 0");
        w.afterWrite = null;
        await S.convertDevice(w.ses);
        same(await dump(), newDisk.files, "  ... then run again: as one run");
        w.ses.close();
    }
    /* a SAVE on the FM-1 between a layout and BEGIN: refused (CHANGED), planned again; the save kept */
    {
        await layOld(oldDisk.files);
        const w = watched();
        let saved = false;
        w.afterHello = async () => { if (!saved && w.writes === 1) { saved = true; await cmd("SAVE 21 77 0 1 0102"); } };
        const res = await S.convertDevice(w.ses);
        same(await dump(), newDisk.files.concat([rec(0x21, 0x77, 0, 1, Uint8Array.of(1, 2))]),
             "a SAVE on the FM-1 meanwhile: planned again, converted, the save kept (" + res.converted.length + " samplers)");
        check(saved && !w.over, "  ... (the SAVE made, no block written over)");
        w.ses.close();
    }
    /* past 256 samples: whole samplers while their samples find numbers; the rest stay in the old form (the audio small, then
     * crossing blocks: 04's freed audio beside 05's, kept, counts nothing damaged) */
    for (const big of [false, true]) {
        const files = fullSet(false, big);
        await layOld(files);
        const w = watched();
        const res = await S.convertDevice(w.ses);
        same(await dump(), S.convertSamplers(files).recs, "past 256 samples" + (big ? ", audio crossing blocks" : "") +
             ": as one run (sampler 05 stays in the old form)");
        check(res.converted.join() === "1,2,3,4" && res.older.join() === "5" && res.olderSamples === 3 && res.samples === 256 &&
              !res.stopped, "  ... 4 converted, 256 samples, 05 older with its 3: " + JSON.stringify(res));
        const writes = w.writes, again = await S.convertDevice(w.ses);
        check(!again.nothing && again.older.join() === "5" && !again.converted.length && w.writes === writes,
              "  ... run again: 05 stays, nothing written");
        const bad = await badNow();
        check(res.damaged === 0 && bad === 0, "  ... the device counts nothing damaged (" + res.damaged + ", " + bad + ")");
        w.ses.close();
    }
    /* the map's room: 256 old samples and 50 songs; the samplers converted while the store's list holds their records, then
     * the next stops it, saying how many records more */
    {
        const files = fullSet(true).concat(Array.from({ length: 50 }, (_, i) => rec(0x21, i, 0, 1, Uint8Array.of(i, 1, 2, 3))));
        await layOld(files);
        const w = watched();
        const res = await S.convertDevice(w.ses);
        const L1 = await w.ses.hello(), d = await dump();
        check(res.converted.join() === "1,2,3" && res.stopped && res.stopped.num === 4 &&
              res.stopped.records === L1.records.length + 64 - L1.mapRoom && L1.records.length <= L1.mapRoom &&
              d.some(r => r.kind === 4 && r.num === 4 && r.ver === 1),
              "the map's room: 01-03 converted, 04 stopped, " + (res.stopped && res.stopped.records) + " records more needed (" +
              L1.records.length + " listed of " + L1.mapRoom + ")");
        w.ses.close();
    }
    /* low space said from what the run leaves (the final review's shapes): one old sampler of 4 x 12 KB on a nearly full FM-1,
     * its save without the reserve, then its old audio freed: as much free after as before, no words; two old samplers
     * whose audio shares a block: the run leaves 2 blocks fewer, the words then true of the end; and the stop when the FM-1
     * is left unable to save even a delete (fewer than 3 free blocks) */
    {
        const big = n => Uint8Array.from({ length: n }, (_, i) => (i * 7 + n) & 255);
        const pats = n => Array.from({ length: n }, (_, i) => rec(0x20, i, 0, 1, big(4000 - i)));
        const freeOf = L => { const occ = new Set(); for (const r of L.records) for (let b = r.addr >>> 12; b <= (r.addr + 16 + (r.deleted ? 0 : (r.len + 3) & ~3) - 1) >>> 12; b++) occ.add(b);
            let n = 0; for (const g of L.regions) for (let a = g.start; a + 4096 <= g.end; a += 4096) if (!occ.has(a >>> 12)) n++; return n; };
        const run = async files => {
            await layOld(files);
            const ses = S.session(transport, { timeout: 1500 }), L0 = await ses.hello();
            const res = await S.convertDevice(ses, { layout: L0 });
            const L1 = await ses.hello();
            ses.close();
            return { res, before: freeOf(L0), after: freeOf(L1), reserve: L1.reserve };
        };
        const one = [rec(4, 1, 0, 1, oldSampler([0, 1, 2, 3].map(i => oldSlot(i, "A")), [0, 1, 2, 3].map(i => oldEntry(12000 + i, "A" + i))))]
            .concat([0, 1, 2, 3].map(i => rec(0x10, 1, i, 1, big(12000 + i))));
        let r = await run(one.concat(pats(77)));
        check(r.res.converted.join() === "1" && r.before === 13 && r.after === 13 && r.res.low === null,
              "the room as it found it (" + r.before * 4 + " KB, then " + r.after * 4 + " KB): no low-space words (" + r.res.low + ")");
        const two = [rec(4, 1, 0, 1, oldSampler([0, 1].map(i => oldSlot(i, "A")), [0, 1].map(i => oldEntry(5000 + i, "A" + i)))),
            rec(4, 2, 0, 1, oldSampler([0, 1].map(i => oldSlot(i, "B")), [0, 1].map(i => oldEntry(9000 + i, "B" + i))))]
            .concat([0, 1].map(i => rec(0x10, 1, i, 1, big(5000 + i))), [0, 1].map(i => rec(0x10, 2, i, 1, big(9000 + i))));
        r = await run(two.concat(pats(86)));
        check(r.res.converted.join() === "1,2" && r.after < r.before && r.after < r.reserve &&
              r.res.low === "only " + r.after * 4 + " KB will be free on the FM-1: it can't save new things (its saves keep 40 KB free); it can still delete",
              "a run leaving less room than it found (" + r.before * 4 + " KB, then " + r.after * 4 + " KB): the words of the end (" + r.res.low + ")");
        r = await run(two.concat(pats(89)));
        check(r.res.stopped && r.res.stopped.num === 2 && r.after >= 3 && r.res.stopped.noDelete === false,
              "stopped with " + r.after + " free blocks left: the FM-1 can still delete (" + JSON.stringify(r.res.stopped) + ")");
        r = await run(two.concat(pats(91)));
        check(r.res.stopped && r.res.stopped.num === 2 && r.before === 4 && r.after === 2 && r.res.stopped.noDelete === true,
              "stopped with " + r.after + " free blocks left (" + r.before + " before): not even a delete, the EDITOR's way alone (" +
              JSON.stringify(r.res.stopped) + ")");
    }
    /* the free room: a sampler whose samples do not fit in the free blocks even alone stops it, saying how much more room */
    {
        const big = n => Uint8Array.from({ length: n }, (_, i) => (i * 7 + n) & 255);
        const files = [rec(4, 1, 0, 1, oldSampler([oldSlot(0, "A")], [oldEntry(6000, "A")])), rec(0x10, 1, 0, 1, big(6000)),
            rec(4, 2, 0, 1, oldSampler([0, 1, 2].map(i => oldSlot(i, "B")), [0, 1, 2].map(i => oldEntry(14000 + i, "B" + i)))),
            ...[0, 1, 2].map(i => rec(0x10, 2, i, 1, big(14000 + i)))]
            .concat(Array.from({ length: 80 }, (_, i) => rec(0x20, i, 0, 1, big(4000 - i))));
        await layOld(files);
        const w = watched();
        const L0 = await w.ses.hello();
        const res = await S.convertDevice(w.ses, { layout: L0 });
        const d = await dump();
        check(res.converted.join() === "1" && res.stopped && res.stopped.num === 2 && res.stopped.kb >= 16 && res.low &&
              d.some(r => r.kind === 4 && r.num === 2 && r.ver === 1) && !w.over,
              "the free room: 01 converted (the reserve not kept), 02 stopped, " + (res.stopped && res.stopped.kb) + " KB more needed");
        console.log("    the free room: " + L0.records.filter(r => !r.deleted).length + " records; 01 converted, 02 stopped: " +
            (res.stopped && res.stopped.kb) + " KB more needed (" + res.low + ")");
        w.ses.close();
    }
}
const sess0 = S.session(transport, { timeout: 1500 });

/* ---- what counts as damaged (specs/files/store.md#power-on): what CHECK STORAGE names. A deleted sample keeps its bytes
 * in its blocks, free now; an ordinary upload writing into part of them, or the device's own saves reaching them, leave
 * its header with a payload changed and its tail at a block's start: no damaged record at the next mount ---- */
async function badNow() {                       /* what the device's mount counts damaged now: an empty seeding */
    const ses = S.session(transport, { timeout: 1500 });
    const L = await ses.hello();
    const r = await ses.write({ ok: true, upToDate: false, saveNumber: (L.lastSave + 1) >>> 0, blocks: [] }, () => {});
    ses.close();
    return r.bad;
}
async function damagedTests(newDisk) {
    console.log("  what counts as damaged:");
    const ses = S.session(transport, { timeout: 3000 });
    const upload = async data => {
        const L = await ses.hello(), P = S.plan(L, data);
        if (!P.ok || P.upToDate) return { P, r: null };
        return { P, r: await ses.write(P, () => {}) };
    };
    const span = r => { const out = []; for (let b = r.addr >>> 12; b <= (r.addr + 16 + r.len - 1) >>> 12; b++) out.push(b); return out; };
    let L = await ses.hello();
    await ses.write(S.planClean(L, newDisk), () => {});
    L = await ses.hello();
    const victim = L.records.filter(r => r.kind === 0x11 && !r.deleted && span(r).length >= 3).sort((a, b) => b.len - a.len)[0];
    const vb = new Set(span(victim));
    const minus = { kind: "disk", problems: [], notes: null, files: newDisk.files.filter(f => !(f.kind === 0x11 && f.num === victim.num)) };
    let u = await upload(minus);
    check(u.r && u.r.bad === 0 && u.P.del.some(d => d.kind === 0x11 && d.num === victim.num), "a sample over " + vb.size + " blocks deleted by an upload");
    /* an ordinary upload: new patterns of a block each, till the plan writes into the deleted sample's blocks */
    const pat = (num, k) => { const p = Uint8Array.from({ length: 3900 }, (_, i) => (i * k + num) & 255); return rec(0x20, num, 0, 1, p); };
    let into = 0, bad = -1;
    for (let k = 1; k <= 16 && !into; k++) {
        u = await upload({ kind: "disk", problems: [], notes: null, files: minus.files.concat(Array.from({ length: k }, (_, i) => pat(0x60 + i, k + 1))) });
        if (u.r && u.P.blocks.some(b => vb.has(b.block))) { into = k; bad = u.r.bad; }
    }
    check(into && bad === 0 && (await badNow()) === 0, "an ordinary upload writing into part of the deleted sample's blocks (" + into +
          " patterns): the mount counts " + bad + " damaged, and so after it");
    /* the device's own saves: the same deletion, then SAVEs on the device till one lands in the sample's blocks */
    L = await ses.hello();
    await ses.write(S.planClean(L, newDisk), () => {});
    u = await upload(minus);
    let landed = -1;
    for (let i = 0; i < 200 && landed < 0; i++) {
        const p = Uint8Array.from({ length: 2000 }, (_, j) => (j * 3 + i) & 255);
        await cmd("SAVE 21 " + (0x40 + (i % 8)).toString(16) + " 0 1 " + hex(p));
        L = await ses.hello();
        const at = L.records.find(r => r.kind === 0x21 && r.num === 0x40 + (i % 8) && !r.deleted);
        if (at && vb.has(at.addr >>> 12)) landed = i + 1;
    }
    await cmd("BOOT");
    bad = await badNow();
    check(landed > 0 && bad === 0, "the device's own saves reaching the deleted sample's blocks (save " + landed + "): the mount counts " +
          bad + " damaged");
    console.log("    a deleted sample's leftovers written into: by an upload (" + into + " patterns), by the device's save " + landed);
    ses.close();
}

async function main() {
    converterTest();
    const disk = new Uint8Array(fs.readFileSync(diskPath));
    const read = S.parseData(disk);
    check(read.kind === "disk" && !read.problems.length, "the demo disk reads: " + read.problems.join("; "));
    check(read.notes && read.notes.length > 1000, "its notes (the credits) are there");
    const sess = S.session(transport, { timeout: 1500 });
    /* the device takes the new form only (samplers of version 2, samples): a disk in the old form is refused as it is,
     * and seeded converted */
    let data = read;
    if (read.files.some(f => f.kind === 0x10 || (f.kind === 0x04 && f.ver === 1))) {
        const L0 = await sess.hello();
        const P0 = S.plan(L0, read);
        check(!P0.ok && P0.problems.some(p => p.includes("take SAMPLER CART version 1")) && P0.problems.some(p => p.includes("take SAMPLE version 1")),
              "the old form refused (samplers of version 1, the old audio): " + P0.problems.slice(0, 2).join("; "));
        const conv = S.convertSamplers(read.files);
        data = { kind: "disk", files: conv.recs.map(r => Object.assign({ label: S.describe(r) }, r)), notes: read.notes, problems: [] };
        console.log("  the demo disk in the old form: converted, " + conv.converted.length + " samplers, " + conv.samples + " samples");
    }
    check(data.files.some(f => f.kind === 0x11 && f.len > 4096 - 16), "samples over 4 KB among the files (" +
          data.files.filter(f => f.kind === 0x11).length + " samples)");

    /* the 7-in-8 packing both ways */
    const all = Uint8Array.from({ length: 300 }, (_, i) => (i * 37) & 0xFF);
    check(hex(S.unpack7(Uint8Array.from(S.pack7(all)))) === hex(all), "7 bytes in 8, both ways");

    /* an empty FM-1 */
    let L = await sess.hello();
    check(L.records.length === 0 && L.lastSave === 0, "an empty device: no records");
    check(L.regions.length === 2 && L.regions[0].start === 0x093000 && L.regions[1].start === 0x0E5000 &&
          L.regions.every(g => g.area !== "rom"), "two regions, the user area's (FWS is never written)");
    check(L.reserve === 16 && L.maxData === 512 && L.accepts.length === 14 && L.accepts.some(a => a.kind === 0x07 && a.ver === 1),
          "the reserve, the message size, the kinds (FAVORITES among them)");
    check(L.mapRoom === 560, "the records the store can list (the map's room): " + L.mapRoom);
    check(L.builtins.length === 6 && [0x20, 0x21].every(k => L.builtins.some(b => b.kind === k && b.num === 0xFE)) &&
          L.builtins.some(b => b.kind === 0x07 && b.num === 0xFB),
          "the built-ins reported, the firmware's own among them (the EDITOR's pattern FE, the keys' song FE)");
    check(L.fwText === "TEST 1", "the firmware's version: " + JSON.stringify(L.fwText));
    let P = S.plan(L, data);
    check(P.ok && P.write.length === data.files.length && !P.del.length, "the plan writes every file");
    console.log("  plan for an empty device:\n    " + P.summary.join("\n    "));
    const audioBlocks = new Set();
    for (const b of P.blocks) if (b.block * 4096 < 0x0E0000) audioBlocks.add(b.block);
    check(P.space.after >= 16, "the reserve is kept: " + P.space.after + " blocks left");
    const t0 = Date.now();
    let res = await sess.write(P, () => {});
    console.log("  seeded " + P.blocks.length + " blocks in " + (Date.now() - t0) + " ms, " + sent + " messages, the biggest " + biggest + " B");
    check(biggest <= 640, "every message within 640 bytes");
    check(res.records === data.files.length && res.bad === 0, "the device mounted " + res.records + " records, " + res.bad + " bad");
    same(await dump(), data.files, "after seeding");
    await cmd("BOOT");
    same(await dump(), data.files, "after a power-cycle");

    /* again: nothing to do */
    L = await sess.hello();
    P = S.plan(L, data);
    check(P.ok && P.upToDate, "seeding the same disk again: nothing to write");

    /* the device's own work: deleted, and the plan says so */
    let r = await cmd("SAVE 30 0 0 1 0102030405");
    check(r[0] === "R 0", "a settings record saved on the device");
    L = await sess.hello();
    P = S.plan(L, data);
    check(P.ok && P.del.length === 1 && P.del[0].kind === 0x30, "the plan deletes the device's settings");
    check(P.summary.some(s => s.startsWith("DELETE:") && s.includes("SETTINGS 00")), "and names it");
    res = await sess.write(P, () => {});
    same(await dump(), data.files, "the device's settings deleted");

    /* one pattern changed: only it is written */
    const one = changed(data, 1);
    L = await sess.hello();
    P = S.plan(L, one);
    check(P.ok && P.write.length === 1 && P.keep.length === data.files.length - 1 && P.blocks.length === 1, "one change, one block");
    await sess.write(P, () => {});
    same(await dump(), one.files, "one pattern changed");
    await cmd("BOOT");
    same(await dump(), one.files, "one pattern changed, after a power-cycle");

    /* the backup: the library read back as a disk */
    L = await sess.hello();
    let bytesRead = 0;
    const backup = await sess.backup(L, p => { bytesRead = p.done; });
    const back = S.parseData(backup);
    check(!back.problems.length, "the backup reads as a disk: " + back.problems.join("; "));
    same(back.files.map(f => ({ kind: f.kind, num: f.num, sub: f.sub, ver: f.ver, len: f.len, crc: f.crc })), one.files, "the backup");
    console.log("  backup: " + back.files.length + " files, " + bytesRead + " bytes read, a disk of " + backup.length + " B");
    /* the EDITOR's own pattern, kept by SAVE on the device: in the backup, and a plan putting the backup back (a
     * restore after an install) takes it, as a changed built-in */
    r = await cmd("SAVE 20 FE 0 1 013F010100803C00");
    check(r[0] === "R 0", "the EDITOR's own pattern saved on the device");
    L = await sess.hello();
    const withFe = S.parseData(await sess.backup(L, () => {}));
    check(withFe.files.some(f => f.kind === 0x20 && f.num === 0xFE), "the backup holds it");
    P = S.plan(L, { files: withFe.files.filter(f => !(f.kind === 0x20 && f.num === 0xFE)), problems: [] });
    check(P.ok && P.del.length === 1 && P.del[0].num === 0xFE, "a disk without it: deleted (the firmware's empty one back)");
    await sess.write(P, () => {});
    L = await sess.hello();
    P = S.plan(L, withFe);
    check(P.ok && P.write.length === 1 && P.write[0].num === 0xFE, "the backup put back: it is written again (" + P.problems.join("; ") + ")");
    await sess.write(P, () => {});
    same(await dump(), withFe.files, "the backup put back, the EDITOR's pattern with it");
    L = await sess.hello();
    P = S.plan(L, back);
    await sess.write(P, () => {});
    same(await dump(), one.files, "as before it");
    /* a disk made again from the demo disk's own files: byte for byte the packer's */
    const again = S.makeDisk(read.files, read.notes);
    check(again.length === disk.length && again.every((x, i) => x === disk[i]), "makeDisk gives the packer's bytes for the same files");

    /* power cuts while seeding: the library before stays whole (the new records went into free blocks) */
    const five = changed(data, 5);
    const quick = S.session(transport, { timeout: 300, blockTimeout: 300, endTimeout: 300 });
    let cutsDone = 0;
    for (const [cut, torn] of [[0, 0], [1, 0], [2, 1], [3, 0], [5, 1], [7, 0], [9, 1], [12, 0]]) {
        L = await quick.hello();
        P = S.plan(L, five);
        if (P.upToDate) break;
        await cmd("CUT " + cut + " " + torn);
        let failed = false;
        try { await quick.write(P, () => {}); } catch (e) { failed = true; }
        await cmd("BOOT");
        const now = await dump();
        if (failed) { cutsDone++; same(now, one.files, "cut after " + cut + " flash operations" + (torn ? " (torn)" : "")); }
        else same(now, five.files, "cut after " + cut + " (the seeding had finished)");
    }
    check(cutsDone >= 5, "seedings cut: " + cutsDone);
    console.log("  " + cutsDone + " seedings cut short: the library before stayed whole");
    /* and seeding again mends it */
    L = await sess.hello();
    P = S.plan(L, five);
    if (!P.upToDate) await sess.write(P, () => {});
    same(await dump(), five.files, "seeded again after the cuts");

    /* every sample changed: too much for the free blocks alone, so blocks whose records all go are written over */
    const allAudio = { kind: "disk", problems: [], files: five.files.map(f => {
        if (!isAudio(f.kind)) return f;
        const p = Uint8Array.from(f.payload); p[0] ^= 0x80;
        return Object.assign({}, f, { payload: p, crc: S.crc32(p) });
    }) };
    L = await sess.hello();
    P = S.plan(L, allAudio);
    check(P.ok && P.rewrites > 0, "a rewrite: " + P.rewrites + " blocks written over; " + P.problems.join("; "));
    console.log("  every sample changed:\n    " + P.summary.join("\n    "));
    if (P.ok) await sess.write(P, () => {});
    same(await dump(), allAudio.files, "every sample changed");
    await cmd("BOOT");
    same(await dump(), allAudio.files, "every sample changed, after a power-cycle");
    L = await sess.hello();
    P = S.plan(L, five);
    if (P.ok && !P.upToDate) await sess.write(P, () => {});
    same(await dump(), five.files, "back again");

    /* refusals */
    await cmd("UNSAVED 1");
    let code = null;
    try { await sess.hello(); } catch (e) { code = e.code; }
    check(code === S.RESULT.UNSAVED, "edits unsaved: SAVE OR REVERT FIRST (" + code + ")");
    await cmd("UNSAVED 0");
    r = await cmd("EDIT 21 5 0 1 0A0B0C");
    check(r[0] === "R 0", "an edit waits in the store's edit memory");
    code = null;
    try { await sess.hello(); } catch (e) { code = e.code; }
    check(code === S.RESULT.UNSAVED, "an edit unsaved: SAVE OR REVERT FIRST (" + code + ")");
    await cmd("REVERT");
    L = await sess.hello();
    const withOne = (kind, num, label) => ({ kind: "disk", files: five.files.concat([{ kind, num, sub: 0, ver: 1, len: 4,
        crc: S.crc32(Uint8Array.of(1, 2, 3, 4)), payload: Uint8Array.of(1, 2, 3, 4), label }]), problems: [] });
    P = S.plan(L, withOne(0x01, 0xF0, "DX7 CART F0"));
    check(!P.problems.some(p => p.includes("F0-FF")), "a built-in number taken (the built-in DX7 F0's change: the user area)");
    P = S.plan(L, withOne(0x04, 0xF0, "SAMPLER CART F0"));
    check(!P.ok && P.problems.some(p => p.includes("F0-FF")), "a built-in number of another kind (a sampler at the DX7's F0) refused");
    P = S.plan(L, withOne(0x01, 0xF3, "DX7 CART F3"));
    check(!P.ok && P.problems.some(p => p.includes("F0-FF")), "a built-in number with nothing built in there (F3 here) refused");
    const v2 = { kind: "disk", files: [Object.assign({}, five.files[0], { ver: 9 })], problems: [] };
    P = S.plan(L, v2);
    check(!P.ok && P.problems.some(p => p.includes("version 9")), "a version the device does not take refused");
    /* more than the reserve leaves: planned to the user area's last block, LOW SPACE saying what fewer free blocks
       cost the device's saves; more than the user area holds: refused */
    const big = n => {
        const d = { kind: "disk", files: five.files.slice(), problems: [] };
        for (let i = 0; i < n; i++) d.files.push({ kind: 0x11, num: 0xFF - i, sub: 0, ver: 1, len: 8000,
            crc: S.crc32(new Uint8Array(8000)), payload: new Uint8Array(8000), label: "SAMPLE" });
        return d;
    };
    let n = 1;                                          /* (more till the reserve is not kept: ~2 blocks a sample, 16 kept) */
    P = S.plan(L, big(n));
    while (P.ok && !P.low && n < 120) P = S.plan(L, big(++n));
    check(P.ok && P.low && P.space.after < L.reserve &&
          P.summary.some(t => t.startsWith("LOW SPACE: only " + P.space.after * 4 + " KB will be free")),
          n + " samples, more than the reserve leaves: planned to the last block, said (" + P.low + ")");
    P = S.plan(L, big(n + 30));
    check(!P.ok && P.problems.some(p => p.includes("does not fit")), "too much for the user area: " + P.problems.join("; "));

    /* CLEAN INSTALL's seeding: no files, every block of the user area written empty; a region not the user area's
       (an area-1 one, which a layout may list) never written */
    {
        const odd = Object.assign({}, L, { regions: L.regions.concat([{ start: 0x082000, end: 0x092000, area: "rom", inPlace: false }]) });
        const user = b => L.regions.some(g => b.block * 4096 >= g.start && b.block * 4096 < g.end);
        const nuser = L.regions.reduce((n, g) => n + (g.end - g.start) / 4096, 0);
        P = S.planClean(odd, { files: [], problems: [] });
        check(P.ok && !P.write.length && P.blocks.length === nuser && P.blocks.every(user) && P.blocks.every(b => b.image.every(x => x === 0xFF)),
              "planClean with no files: every block of the user area empty (" + P.blocks.length + "), nothing else written");
    }
    /* a SAVE on the FM-1 between the plan and the writing: BEGIN refused (CHANGED), the plan made again works */
    L = await sess.hello();
    P = S.plan(L, one);
    check(P.ok && !P.upToDate, "a plan to write");
    r = await cmd("SAVE 21 7 0 1 0102");
    code = null;
    try { await sess.write(P, () => {}); } catch (e) { code = e.code; }
    check(code === S.RESULT.CHANGED, "a save since the layout: BEGIN refused, the layout changed (" + code + ")");
    const keep = await dump();
    check(keep.some(x => x.kind === 0x21 && x.num === 7), "nothing written: the device's save stands");
    L = await sess.hello();
    P = S.plan(L, one);
    await sess.write(P, () => {});
    same(await dump(), one.files, "planned again and written");
    /* the player: told all sound off at BEGIN and at the end, and that the store changed before it runs again */
    const pl = (await cmd("PLAYER"))[0].split(" ").map(Number);
    check(pl[1] >= 2 && pl[2] >= 1 && pl[3] === pl[2], "the player stopped at BEGIN and told before the end: " + pl.slice(1).join(" "));

    /* the time-out: BEGIN, then nothing for 10 s; the device mounts what is there */
    const next = n => { const s = n + 1; return [s & 255, s >> 8 & 255, s >> 16 & 255, s >>> 24, 1, 0]; };
    L = await sess.hello();
    transport.send(S.message(S.CMD.BEGIN, next(L.lastSave)));
    await new Promise(r => setTimeout(r, 50));
    let a = await cmd("ACTIVE");
    check(a[0] === "A 1", "seeding under way after BEGIN");
    await cmd("T 9000");
    a = await cmd("ACTIVE");
    check(a[0] === "A 1", "still after 9 s");
    await cmd("T 2000");
    a = await cmd("ACTIVE");
    check(a[0] === "A 0", "ended after 10 s without a message");
    same(await dump(), one.files, "the library after the time-out");

    /* a CHUNK outside the regions */
    const chunkBad = [0x40, 0x00, 0, 0, 1, 2, 3];   /* block 0x40: the firmware */
    L = await sess.hello();
    transport.send(S.message(S.CMD.BEGIN, next(L.lastSave)));
    await new Promise(r => setTimeout(r, 50));
    let got = null;
    listeners.push(m => { const p = S.parseMessage(m); if (p.cmd === S.CMD.ACK && p.data[0] === S.CMD.CHUNK) got = p.data[3]; });
    transport.send(S.message(S.CMD.CHUNK, chunkBad));
    await new Promise(r => setTimeout(r, 100));
    check(got === S.RESULT.BAD_BLOCK, "a block outside the regions refused (" + got + ")");
    got = null;
    transport.send(S.message(S.CMD.CHUNK, [0x88, 0x00, 0, 0, 1, 2, 3]));   /* block 0x88: FWS's free part */
    await new Promise(r => setTimeout(r, 100));
    check(got === S.RESULT.BAD_BLOCK, "a block in FWS's free part refused (" + got + ")");
    transport.send(S.message(S.CMD.ABORT, []));
    await new Promise(r => setTimeout(r, 100));

    /* what counts as damaged; the conversion after an install, from the old demo disk */
    await damagedTests(data);
    if (oldPath) await conversionTests(S.parseData(new Uint8Array(fs.readFileSync(oldPath))),
                                       S.parseData(new Uint8Array(fs.readFileSync(convPath))));   /* (what it converts to: its samples from 00) */
    else console.log("  SKIP the conversion after an install: no old-form disk given");

    br.stdin.write("Q\n");
    console.log("seed: " + checks + " checks, " + fails + " failed");
    process.exit(fails ? 1 : 0);
}
main().catch(e => { console.log("FAIL", e); process.exit(1); });

/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The two panels' items and their rules (specs/editor.md: Items, references, hashes; Moving items; DELETE and DELETE
 * ALL; UNDO): no DOM here, so the tests run it in Node.
 *
 * An item is one of PT/FM's objects with all its records, immutable (a change makes a new item):
 *   { id, side "L" (TABLE) | "R" (FM-1), kind (the record's kind), num (the FM-1 panel's number; null on the TABLE),
 *     orig (the number it had where it came from, or null), recs [{ sub, ver, payload }] by sub,
 *     link { "c05": id | BUILTIN | null, ... } (its references, as numbers in its records, to the items they name;
 *     "m17": a sampler's instrument naming sample 17),
 *     name (the TABLE's name for a kind with none), src (where it came from), builtin (a changed built-in: its number
 *     is the firmware's), dim (a version the page cannot read: kept as it is), older (a sampler of the old form the
 *     page could not convert, past 256 samples: dim, listed as OLDER FORM; its old audio, kind AUDIO items with owner
 *     its id and idx their place, are never listed and go with it), sound (a sample's original and its cut), entry
 *     (a sample tab's draft of its 16-byte entry), long (a TABLE sample too long for the FM-1: its frames; no record) }
 * A sample (kind SAMPLE, 0x11) is an item of its own: its record holds its entry and its audio, its number the
 * panel's (null on the TABLE); the samples have a numbers' space "m" of 256, no built-ins.
 * A TABLE item's numbers are those of where it came from (its own frame: nothing else reads them); an FM-1 item's are
 * the panel's, its link naming the items at those numbers. Moving rewrites the numbers in place, byte for byte, so
 * what does not change keeps its bytes (and its CRC: WRITE leaves it on the FM-1).
 *
 *   ED.model.fromRecords(recs, { side, src, builtins }) -> { items, skipped }
 *   ED.model.toRecords(items) -> the FM-1 panel's records [{ kind, num, sub, ver, payload }]
 *   refs(item), closure(items, ids), users(items), hashes(items), size(item), sizeWith(items, id)
 *   transfer(st, from, ids, opts) -> { ok, st, msg, ids (the moved ones on the other side), need }
 *   blockers(items, ids), exclusive(items, sel), remove(st, side, ids, also) -> { st, msg }, deleteAll(st, side)
 *   canRenumber(item), renumber(st, id, number) -> { ok, st, msg, plan, other }
 *   space(items, layout) -> { blocks, used, data, audio, free, reserve (advised, not kept), fits, missing, low (the
 *     words of lowSpace(free, reserve): what fewer free blocks cost the FM-1's own saves, or null) }
 *   pending(st) -> { ids: Set, removed: [], count }; History: push(st), undo(st), redo(st) */
(function (ED) {
"use strict";
const R = ED.rec, K = R.KIND;
const BUILTIN = "B";                                /* a link to the firmware's own (a built-in, never moved) */
const H2 = v => (v < 16 ? "0" : "") + (v & 0xFF).toString(16).toUpperCase();
const align4 = n => (n + 3) & ~3;
const FIRST_FW = 0xF0, FE = 0xFE;
const SYSTEM = [K.SETTINGS, K.MIDIMAP, K.LOOPS, K.DJ];            /* one of each on the FM-1 */
const isCart = k => R.CART_KINDS.includes(k);
const space = k => isCart(k) ? "c" : k === K.PATTERN ? "p" : k === K.SONG ? "s" : k === K.SAMPLE ? "m" : null;     /* numbers' spaces */
const SPACE_NAME = { c: "CARTRIDGE", p: "PATTERN", s: "SONG", m: "SAMPLE" };
const SPACE_MOST = { c: FIRST_FW, p: FIRST_FW, s: FIRST_FW, m: 256 };       /* (the samples have all 256 numbers) */
let seq = 1;
const newId = () => "i" + (seq++).toString(36);
function seen(id) { const n = parseInt(String(id).slice(1), 36); if (n >= seq) seq = n + 1; }

/* ---- the built-in instruments: the firmware's (ed_builtin), set once the WebAssembly is there ---- */
let builtins = [];                                  /* [{ kind, num, sub, ver, payload }] */
const builtinAt = (sp, num) => builtins.find(b => space(b.kind) === sp && b.num === num) || null;
function setBuiltins(list) { builtins = list.slice(); }

/* ---- references: every number in an item's records naming another object ---- */
/* [{ sp, num, r (the record's index), at (the byte), soft }]; soft: the settings' current instrument and the
 * EDITOR's pattern, DJ's decks' songs: remembered places, not uses (gone, they are cleared) */
const refsCache = new WeakMap();
function refs(it) {
    if (refsCache.has(it)) return refsCache.get(it);
    const out = it.dim ? [] : refsOf(it);
    refsCache.set(it, out);
    return out;
}
function refsOf(it) {
    const out = [];
    const recs = it.recs;
    if (it.kind === K.SONG) {
        const b = recs[0].payload, n = b[20] + 1;
        for (let i = 0; i < n; i++) if (b[21 + i] !== 0xFF) out.push({ sp: "p", num: b[21 + i], r: 0, at: 21 + i });   /* (FF: no pattern) */
    } else if (it.kind === K.PATTERN) {
        recs.forEach((rec, r) => patternRefs(rec.payload, r, out));
    } else if (it.kind === K.FUSION) {
        const b = recs[0].payload, n = b[9];
        for (let i = 0; i < n; i++) {
            const o = 10 + 16 * i;
            if (isEmptySlot(K.FUSION, b, o)) continue;
            if (b[o] !== 0xFF) out.push({ sp: "p", num: b[o], r: 0, at: o });
        }
    } else if (it.kind === K.FAVORITES) {
        /* each favourite names its source's cartridge (fav: a use that keeps it, never one DELETE takes along) */
        const b = recs[0].payload, n = b[9];
        for (let i = 0; i < n; i++) if (b[11 + 3 * i] !== 0xFF) out.push({ sp: "c", num: b[10 + 3 * i], r: 0, at: 10 + 3 * i, fav: true });
    } else if (it.kind === K.SAMPLER) {
        /* each instrument names its sample by number (version 2: the slot's first byte) */
        const b = recs[0].payload, n = b[9];
        for (let i = 0; i < n && b.length >= 10 + 40 * (i + 1); i++) {
            const o = 10 + 40 * i;
            if (R.unpackSamplerInst(b.subarray(o, o + 40), 2)) out.push({ sp: "m", num: b[o], r: 0, at: o });
        }
    } else if (it.kind === K.LOOPS) {
        const b = recs[0].payload, n = b[5];
        for (let i = 0; i < n; i++) out.push({ sp: "c", num: b[80 + 12 * i], r: 0, at: 80 + 12 * i });
    } else if (it.kind === K.DJ) {
        const b = recs[0].payload;
        for (const d of [88, 112]) if (b.length > d && b[d] !== 0xFF) out.push({ sp: "s", num: b[d], r: 0, at: d, soft: true, deck: d });
    } else if (it.kind === K.SETTINGS) {
        const b = recs[0].payload;
        if (b.length > 123 && (b[123] & 1)) out.push({ sp: "c", num: b[121], r: 0, at: 121, soft: true, flag: [123, 1] });
        if (b.length > 127 && (b[126] & 1)) out.push({ sp: "p", num: b[127], r: 0, at: 127, soft: true, flag: [126, 1] });
    }
    return out;
}
/* a pattern part's cells: the instrument's cartridge byte of every cell naming one in full (the others name the
 * track's last one, so they follow); a part cut short ends where its bytes do */
function patternRefs(b, r, out) {
    const mask = b[3] | b[4] << 8;
    let at = 5;
    for (let t = 0; t < 16; t++) {
        if (!(mask >> t & 1)) continue;
        for (;;) {
            if (at >= b.length) return;
            const head = b[at++];
            if (head === 0) break;
            if (head & 0x80) at++;
            const mode = (head >> 5) & 3;
            if (mode === 2) at++;
            else if (mode === 3) { out.push({ sp: "c", num: b[at], r, at }); at += 2; }
            if (head & 0x10) at++;
            if (head & 0x08) {
                let n = 1;
                if (b[at] === 0xFF) { n = b[at + 1]; at += 2; }
                at += 2 * n;
            }
            if ((head & 7) === 7) at++;
        }
    }
}
function isEmptySlot(kind, b, o) {                  /* a cartridge slot whose name's first code is 63 */
    const size = R.SLOT[kind][1], spare = R.SLOT[kind][2], bit = size * 8 - 60 - 8 - spare;
    const r = new R.BitReader(b.subarray(o, o + size));
    r.skip(bit);
    return r.get(6) === 63;
}
const lk = (sp, num) => sp + H2(num);

/* the records with every reference rewritten: fn(ref, target) -> the new number, or -1 to clear a soft one */
function rewrite(it, fn) {
    let recs = it.recs;
    const copied = new Set();
    for (const f of refs(it)) {
        const n = fn(f, it.link[lk(f.sp, f.num)]);
        if (n === f.num) continue;
        if (!copied.has(f.r)) {
            if (recs === it.recs) recs = recs.slice();
            recs[f.r] = Object.assign({}, recs[f.r], { payload: recs[f.r].payload.slice() });
            copied.add(f.r);
        }
        const b = recs[f.r].payload;
        if (n < 0) {
            if (f.flag) b[f.flag[0]] &= ~f.flag[1];
            else if (f.deck) { b.fill(0, f.deck, f.deck + 24); b[f.deck] = 0xFF; }   /* (no song: the deck's other bytes 0) */
        } else b[f.at] = n;
    }
    return recs;
}

/* ---- records in, items out ---- */
function fromRecords(recs, o) {
    o = o || {};
    const side = o.side || "L", items = [], skipped = [];
    /* (the old samplers and their audio converted here, once, as the records load: samplers of version 2 and samples of
     * their own; a sampler past the 256 samples stays in the old form, marked `older`) */
    recs = recs.filter(r => r.payload && r.kind < 0xE0);
    /* (on the TABLE, whose items have no numbers, there is no cap of 256 samples and no OLDER FORM: the old samplers
     * are held back and converted whole by upgradeKept below) */
    const oldRecs = side === "L" ? recs.filter(r => (r.kind === K.SAMPLER && r.ver === 1) || (r.kind === K.AUDIO && r.ver === 1)) : [];
    if (oldRecs.length) recs = recs.filter(r => !oldRecs.includes(r));
    recs = R.upgrade(recs);
    let conversion = recs.conversion || null;
    const groups = new Map();                       /* "kind.num" -> records */
    for (const r of recs) {
        const g = (r.kind === K.AUDIO ? "a" : r.kind) + "." + r.num;
        if (!groups.has(g)) groups.set(g, []);
        groups.get(g).push(r);
    }
    const byNum = { c: new Map(), p: new Map(), s: new Map(), m: new Map() };
    const known = r => R.VERSION[r.kind] === r.ver;
    for (const [g, rs] of groups) {
        if (g[0] === "a") continue;
        const r0 = rs[0];
        /* a kind this page does not know: left out of the TABLE (only what the FM-1 holds is taken); on the FM-1
         * panel kept as it is, never written nor removed */
        if (!(r0.kind in R.VERSION) || r0.kind === K.NOTES) {
            if (side !== "R" || r0.kind >= 0xE0) { skipped.push(r0); continue; }
        }
        rs.sort((a, b) => a.sub - b.sub);
        const it = { id: newId(), side, kind: r0.kind, num: side === "R" ? r0.num : null, orig: r0.num, link: {},
            recs: rs.map(r => ({ sub: r.sub, ver: r.ver, payload: r.payload })), src: o.src || null };
        if (!rs.every(known)) it.dim = true;
        if (rs[0].older) { it.older = true; it.dim = true; }
        /* a sample whose entry and audio differ: read as missing, as the FM-1 reads it (specs/files/instruments.md#samples):
         * dim, kept as it is, its number taken, and no instrument links to it (a reference to nothing, grey) */
        if (r0.kind === K.SAMPLE && !it.dim && globalThis.PTFM_seed && !globalThis.PTFM_seed.sampleOk(r0.payload)) {
            it.dim = true;
            it.differ = true;
        }
        if (space(r0.kind) && space(r0.kind) !== "m" && r0.num >= FIRST_FW) {
            if (r0.num === FE && (r0.kind === K.PATTERN || r0.kind === K.SONG)) {
                if (side === "L") it.name = r0.kind === K.PATTERN ? "EDITOR FE" : "KEYS FE";
            } else it.builtin = true;               /* a changed built-in */
        }
        items.push(it);
        if (space(r0.kind) && !it.differ) byNum[space(r0.kind)].set(r0.num, it);
    }
    /* the old audio left (kind AUDIO: an old sampler the page could not convert keeps it): under its sampler (its
     * number), never listed; audio of no sampler is left out */
    for (const [g, rs] of groups) {
        if (g[0] !== "a") continue;
        for (const r of rs) {
            const owner = byNum.c.get(r.num);
            if (!owner || owner.kind !== K.SAMPLER) { skipped.push(r); continue; }
            const s = { id: newId(), side, kind: K.AUDIO, num: side === "R" ? r.num : null, orig: r.num, link: {},
                recs: [{ sub: r.sub, ver: r.ver, payload: r.payload }], owner: owner.id, idx: r.sub, src: o.src || null };
            if (!known(r) || owner.dim) s.dim = true;      /* (a sampler this page cannot read: its audio with it) */
            items.push(s);
        }
    }
    /* links: within the set, else the firmware's built-in, else nothing (a reference to nothing, as on the FM-1) */
    for (const it of items) {
        if (it.dim) continue;
        for (const f of refs(it)) {
            const t = byNum[f.sp].get(f.num);
            it.link[lk(f.sp, f.num)] = t ? t.id : f.num >= FIRST_FW && builtinAt(f.sp, f.num) ? BUILTIN : null;
        }
    }
    if (oldRecs.length) {
        const owners = new Map();
        for (const r of oldRecs) if (r.kind === K.SAMPLER && r.sub === 0 && !owners.has(r.num))
            owners.set(r.num, { id: newId(), side, kind: K.SAMPLER, num: null, orig: r.num, link: {}, src: o.src || null, recs: [{ sub: 0, ver: 1, payload: r.payload }] });
        const old = [...owners.values()];
        let dropped = 0;
        for (const r of oldRecs) if (r.kind === K.AUDIO) {
            const ow = owners.get(r.num);
            if (!ow) { dropped++; continue; }
            old.push({ id: newId(), side, kind: K.AUDIO, num: null, orig: r.num, link: {}, owner: ow.id, idx: r.sub, src: o.src || null, recs: [{ sub: r.sub, ver: 1, payload: r.payload }] });
        }
        const had = items.filter(i => i.kind === K.SAMPLE).length;
        const all = upgradeKept(items.concat(old), "L");
        items.length = 0; items.push(...all);
        conversion = { converted: [...owners.keys()], older: [], samples: items.filter(i => i.kind === K.SAMPLE).length - had, dropped, bad: [] };
    }
    if (side === "L") nameTable(items, o.src);
    return { items, skipped, conversion };
}
/* the TABLE's names for kinds with none: MYSONG P07 */
function nameTable(items, src) {
    const base = String(src || "").replace(/\.[^.]*$/, "").toUpperCase().replace(/[^A-Z0-9 ]+/g, " ").trim().slice(0, 10) || "PATTERN";
    for (const it of items) {
        if (it.name) continue;
        if (it.kind === K.PATTERN) it.name = base + " P" + H2(it.orig);
    }
}

/* ---- what the browser kept from an older page: converted once, at the first load (specs/editor.md) ----
 * The old samplers (version 1, the samples in their tables) become samplers of version 2 and samples of their own, as a
 * disk's would (R.upgrade over their records); the audio under them goes; a loose sample of the TABLE (kind AUDIO with
 * no owner: its entry its own) becomes a sample item. A sampler that did not fit in 256 samples stays, `older`.
 * The items keep their ids (the converted samplers, the loose samples), so nothing that names them is lost. On the TABLE
 * the samplers have no numbers: they are converted whole, in chunks under numbers of their own. -> the items (the same
 * array when there was nothing to convert) */
function upgradeKept(items, side) {
    const isOld = i => i.kind === K.SAMPLER && !i.older && i.recs.length && i.recs[0].ver === 1 && R.VERSION[K.SAMPLER] !== 1;
    const olds = items.filter(isOld), loose = items.filter(i => i.kind === K.AUDIO && !i.owner);
    if (!olds.length && !loose.length) return items;
    const replace = new Map(), drop = new Set(), added = [];
    /* (a sampler of the FM-1 panel is at its number; on the TABLE one of its own in each chunk) */
    const chunks = [];
    if (side === "R") chunks.push(olds.map(it => [it, it.num]));
    else {
        /* (the TABLE has no numbers, so no cap of 256 samples: chunks of whole samplers holding 256 samples at most, a
         * table's count bounding them; equal samples merge inside a chunk, and again when a move numbers them) */
        let cur = [], count = 0;
        for (const it of olds) {
            const n = it.recs[0].payload[10] || 0;
            if (cur.length && (count + n > 256 || cur.length >= 255)) { chunks.push(cur); cur = []; count = 0; }
            cur.push([it, cur.length]); count += n;
        }
        if (cur.length) chunks.push(cur);
    }
    for (const chunk of chunks) {
        if (!chunk.length) continue;
        const recs = [], have = new Map();
        if (side === "R") for (const i of items) {
            if (i.dim || !i.recs.length) continue;
            if (i.kind === K.SAMPLE || (i.kind === K.SAMPLER && !isOld(i) && i.recs[0].ver === 2)) {
                recs.push({ kind: i.kind, num: i.num, sub: 0, ver: i.recs[0].ver, payload: i.recs[0].payload });
                if (i.kind === K.SAMPLE) have.set(i.num, i.id);
            }
        }
        for (const [it, n] of chunk) {
            recs.push({ kind: K.SAMPLER, num: n, sub: 0, ver: 1, payload: it.recs[0].payload });
            for (const a of items) if (a.kind === K.AUDIO && a.owner === it.id) recs.push({ kind: K.AUDIO, num: n, sub: a.idx, ver: 1, payload: a.recs[0].payload });
        }
        const res = globalThis.PTFM_seed.convertSamplers(recs);
        const ids = new Map(have);                  /* sample number -> its item (this chunk's frame) */
        for (const r of res.recs) if (r.kind === K.SAMPLE && !ids.has(r.num)) {
            const first = chunk[0][0];
            const s = { id: newId(), side, kind: K.SAMPLE, num: side === "R" ? r.num : null, orig: r.num, link: {},
                recs: [{ sub: 0, ver: r.ver, payload: r.payload }], src: first.src || null };
            ids.set(r.num, s.id);
            added.push(s);
        }
        const done = new Set(res.converted);
        for (const [it, n] of chunk) {
            if (done.has(n)) {
                const rec = res.recs.find(r => r.kind === K.SAMPLER && r.ver === 2 && r.num === n);
                const c = Object.assign({}, it, { recs: [{ sub: 0, ver: 2, payload: rec.payload }], link: {} });
                for (const f of refs(c)) c.link[lk(f.sp, f.num)] = ids.get(f.num) || null;
                replace.set(it.id, c);
                for (const a of items) if (a.kind === K.AUDIO && a.owner === it.id) drop.add(a.id);
            } else {                                // (past 256 samples, or unreadable: kept, in the old form)
                replace.set(it.id, Object.assign({}, it, { older: true, dim: true }));
                for (const a of items) if (a.kind === K.AUDIO && a.owner === it.id) replace.set(a.id, Object.assign({}, a, { dim: true }));
            }
        }
    }
    for (const it of loose) {
        const s = Object.assign({}, it, { kind: K.SAMPLE, owner: null });
        if (it.recs.length && it.entry) {
            s.recs = [{ sub: 0, ver: R.VERSION[K.SAMPLE], payload: R.packSample(R.unpackSample(it.entry).entry, it.recs[0].payload) }];
            delete s.entry;
        }
        replace.set(it.id, s);
    }
    return items.filter(i => !drop.has(i.id)).map(i => replace.get(i.id) || i).concat(added);
}

/* the FM-1 panel's records: by its numbers (a sample's: its sampler's, its place); dim items too, as they are */
function toRecords(items, all) {
    const out = [], byId = new Map((all || items).map(i => [i.id, i]));
    for (const it of items) {
        const o = it.kind === K.AUDIO ? byId.get(it.owner) : null;
        const num = o ? o.num : it.num;
        for (const r of it.recs) out.push({ kind: it.kind, num, sub: it.kind === K.AUDIO ? it.idx : r.sub, ver: r.ver, payload: r.payload });
    }
    return out;
}

/* records in a disk's order (specs/files/disk.md): settings, cartridges, songs, patterns, the loops, then the audio */
const DISK_ORDER = [0x30, 0x31, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x21, 0x20, 0x22, 0x23, 0xE0, 0x10, 0x11];
function diskOrder(recs) {
    const rank = k => { const i = DISK_ORDER.indexOf(k); return i < 0 ? 100 + k : i; };
    return recs.slice().sort((a, b) => rank(a.kind) - rank(b.kind) || a.num - b.num || a.sub - b.sub);
}

/* ---- what items are ---- */
const byIdMap = items => { const m = new Map(); for (const i of items) m.set(i.id, i); return m; };
/* a sample's entry: its own when it carries one (a tab's draft of it, or a sample too long, which has no record), else
 * the record's first 16 bytes */
function sampleEntry(it, items) {
    try {
        if (it.entry) return R.unpackSample(it.entry).entry;
        if (it.kind === K.SAMPLE && !it.dim && it.recs.length) return R.unpackSample(it.recs[0].payload).entry;
    } catch (e) { /* (a record that does not read: no entry) */ }
    return null;
}
function sampleName(it, items) { const e = sampleEntry(it, items); return e ? e.name : null; }
/* a loose sample on the TABLE too long for the FM-1 (specs/editor.md, Too long for the FM-1): its original and its cut,
 * no record, its entry the name, rate and depth with no length, it.long its frames at the cut's rate */
const MAX_FRAMES = (1 << 20) - 1;
const isLong = it => !!it && it.kind === K.SAMPLE && it.long > 0;
function asLong(it, entry, frames) {
    return Object.assign({}, it, { recs: [], entry: packedEntry(Object.assign({}, entry, { length: 0 })), long: frames });
}
function longRate(it) { return it.sound && it.sound.cut && it.sound.cut.rate || 44100; }
function longText(it) {
    const r = longRate(it);
    return "TOO LONG FOR THE FM-1: " + (it.long / r).toFixed(1) + " S, " + (MAX_FRAMES / r).toFixed(1) + " S AT MOST";
}
const SYS_NAME = { [K.SETTINGS]: "SETTINGS", [K.MIDIMAP]: "MIDI MAP", [K.LOOPS]: "LOOPS", [K.DJ]: "DJ SETUP" };
function nameOf(it, items) {
    if (it.older) return R.unpackName(it.recs[0].payload.subarray(0, 9), 12) || "SAMPLER " + H2(it.num != null ? it.num : it.orig);
    if (it.differ) return "SAMPLE " + H2(it.num != null ? it.num : it.orig);
    if (it.dim) return "VERSION " + it.recs[0].ver;
    if (SYS_NAME[it.kind]) return SYS_NAME[it.kind];
    if (it.side === "R" && it.num === FE) return it.kind === K.PATTERN ? "EDITOR'S PATTERN" : "KEYS' SONG";
    if (it.kind === K.SAMPLE) return sampleName(it, items) || it.name || "SAMPLE " + H2(it.num != null ? it.num : it.orig || 0);
    if (it.name) return it.name;
    if (isCart(it.kind)) return R.unpackName(it.recs[0].payload.subarray(0, 9), 12) || "CART " + H2(it.num != null ? it.num : it.orig);
    if (it.kind === K.SONG) return R.unpackName(it.recs[0].payload.subarray(0, 12), 16) || "SONG";
    if (it.kind === K.AUDIO) return "OLD AUDIO " + H2(it.idx);
    if (it.kind === K.PATTERN) return "PATTERN " + H2(it.num != null ? it.num : it.orig);
    return "?";
}
/* its group: SONGS, PATTERNS, CARTRIDGES, SAMPLERS, SAMPLES, SYSTEM */
function groupOf(it) {
    if (SYS_NAME[it.kind] || (it.side === "R" && it.num === FE)) return "SYSTEM";
    if (it.kind === K.SONG) return "SONGS";
    if (it.kind === K.PATTERN) return "PATTERNS";
    if (it.kind === K.SAMPLER) return "SAMPLERS";
    if (it.kind === K.SAMPLE) return "SAMPLES";
    if (it.kind === K.AUDIO) return "OLD AUDIO";             /* (never listed: it goes with its older sampler) */
    return "CARTRIDGES";
}
/* one of its group, in a count: SONG, PATTERN, CARTRIDGE, SAMPLER, SAMPLE, SYSTEM ITEM */
const oneOf = it => { const g = groupOf(it); return g === "SYSTEM" ? "SYSTEM ITEM" : g.replace(/S$/, ""); };
const ENGINE = { [K.DX7]: "DX7", [K.JUNO]: "JUNO-106", [K.TB303]: "TB-303", [K.SAMPLER]: "SAMPLER", [K.FUSION]: "FUSION",
    [K.CHIP]: "CHIP", [K.FAVORITES]: "FAVORITES" };
/* its kind in words: 4 TRACKS × 64, SAMPLER · 10 INSTR · 7 SMP, 8-BIT · 22.1 KHZ · 0.42 S */
function kindText(it, items) {
    if (it.older) return "SAMPLER · OLDER FORM · " + needText(olderNeeds(it, items || []));
    if (it.differ) return "SAMPLE · ENTRY AND AUDIO DIFFER";
    if (it.dim) return (SYS_NAME[it.kind] || ENGINE[it.kind] || "KIND " + H2(it.kind)) + " · NOT READ";
    const b = it.recs.length ? it.recs[0].payload : null;     /* (a sample too long has none) */
    if (it.kind === K.PATTERN) return b[0] + " TRACKS × " + (b[1] + 1);
    if (it.kind === K.SONG) return (b[20] + 1) + (b[20] ? " POSITIONS" : " POSITION") + " · TEMPO " + (b[12] | b[13] << 8);
    if (it.kind === K.SAMPLER) {                    /* (SMP: the samples its instruments name) */
        const ins = R.unpackSampler(b).instruments.filter(Boolean);
        return "SAMPLER · " + ins.length + " INSTR · " + new Set(ins.map(s => s.sample)).size + " SMP";
    }
    if (isCart(it.kind)) return ENGINE[it.kind] + " · " + R.unpackCart(it.kind, b).slots.filter(Boolean).length + " INSTR";
    if (it.kind === K.SAMPLE) {
        const e = sampleEntry(it, items), len = isLong(it) ? it.long : e && e.length;
        if (!e) return "SAMPLE";
        return (e.coding === 2 ? "16" : "8") + "-BIT · " + (e.rate / 1000).toFixed(1) + " KHZ · " + (e.rate ? (len / e.rate).toFixed(2) : "?") + " S";
    }
    if (it.kind === K.LOOPS) return b[5] + " LOOPS";
    return SYS_NAME[it.kind] || "";
}

/* ---- sizes: the records as the FM-1 stores them ---- */
const recBytes = r => 16 + align4(r.payload.length);
function size(it) { let n = 0; for (const r of it.recs) n += recBytes(r); return n; }
/* (a panel's arrays are never changed, only replaced: what is worked out from one is kept with it) */
const swCache = new WeakMap();
function sizeWith(items, id) {
    let c = swCache.get(items);
    if (!c) { c = { m: byIdMap(items), owned: ownedMap(items), memo: new Map() }; swCache.set(items, c); }
    if (c.memo.has(id)) return c.memo.get(id);
    const set = new Set();
    const walk = x => {
        if (set.has(x) || !c.m.has(x)) return;
        set.add(x);
        for (const t of hardUses(c.m.get(x), c.m)) walk(t);
        for (const s of c.owned.get(x) || []) walk(s);
    };
    walk(id);
    let n = 0;
    for (const x of set) n += size(c.m.get(x));
    c.memo.set(id, n);
    return n;
}

/* ---- uses: what an item uses (links, a sampler's samples), what uses it ---- */
function uses(it, m) {
    const out = [];
    for (const k in it.link) { const t = it.link[k]; if (t && t !== BUILTIN && m.has(t)) out.push(t); }
    return out;
}
const isSoft = (it, key) => refs(it).some(f => f.soft && lk(f.sp, f.num) === key);
function hardUses(it, m) {
    const soft = new Set(refs(it).filter(f => f.soft).map(f => lk(f.sp, f.num)));
    const out = [];
    for (const k in it.link) { const t = it.link[k]; if (!soft.has(k) && t && t !== BUILTIN && m.has(t)) out.push(t); }
    return out;
}
/* the items and everything they use: a sampler with all its samples */
function closure(items, ids) {
    const m = byIdMap(items), owned = ownedMap(items), set = new Set();
    const walk = id => {
        if (set.has(id) || !m.has(id)) return;
        set.add(id);
        const it = m.get(id);
        for (const t of hardUses(it, m)) walk(t);
        for (const s of owned.get(id) || []) walk(s);
    };
    ids.forEach(walk);
    return set;
}
function ownedMap(items) {
    const o = new Map();
    for (const i of items) if (i.owner) { if (!o.has(i.owner)) o.set(i.owner, []); o.get(i.owner).push(i.id); }
    return o;
}
/* id -> the items using it (hard references; a sampler uses a sample only when an instrument plays it) */
const usersCache = new WeakMap();
function users(items) {
    if (usersCache.has(items)) return usersCache.get(items);
    const u = usersOf(items);
    usersCache.set(items, u);
    return u;
}
function usersOf(items) {
    const m = byIdMap(items), u = new Map();
    const add = (t, id) => { if (!u.has(t)) u.set(t, []); if (!u.get(t).includes(id)) u.get(t).push(id); };
    for (const it of items) {
        for (const t of hardUses(it, m)) add(t, it.id);          /* (a sampler uses the samples its instruments name) */
    }
    return u;
}

/* ---- content hashes: references replaced by what they refer to ---- */
function hstr(bytes, seed) {                        /* 53 bits (cyrb53) */
    let h1 = 0xdeadbeef ^ seed, h2 = 0x41c6ce57 ^ seed;
    for (let i = 0; i < bytes.length; i++) {
        const c = bytes[i];
        h1 = Math.imul(h1 ^ c, 2654435761);
        h2 = Math.imul(h2 ^ c, 1597334677);
    }
    h1 = Math.imul(h1 ^ (h1 >>> 16), 2246822507) ^ Math.imul(h2 ^ (h2 >>> 13), 3266489909);
    h2 = Math.imul(h2 ^ (h2 >>> 16), 2246822507) ^ Math.imul(h1 ^ (h1 >>> 13), 3266489909);
    return (4294967296 * (2097151 & h2) + (h1 >>> 0)).toString(36);
}
const origCache = new WeakMap();                    /* an original's channels -> their hash (hashed once) */
function origHash(snd) {
    let v = origCache.get(snd.ch);
    if (!v) {
        v = snd.rate + "." + snd.ch.map(c => { const f = c instanceof Float32Array ? c : Float32Array.from(c); return hstr(new Uint8Array(f.buffer, f.byteOffset, f.byteLength), 3); }).join(".");
        origCache.set(snd.ch, v);
    }
    return v;
}
const ownCache = new WeakMap();                     /* item -> its records with references numbered by first use */
function ownHash(it) {
    if (ownCache.has(it)) return ownCache.get(it);
    const order = [];
    const recs = it.dim ? it.recs : rewrite(it, f => {
        const key = lk(f.sp, f.num);
        let i = order.indexOf(key);
        if (i < 0) { i = order.length; order.push(key); }
        return i & 0xFF;
    });
    let s = it.kind + ":";
    for (const r of recs) s += (it.kind === K.AUDIO ? "" : r.sub + ".") + r.ver + "." + hstr(r.payload, 0) + hstr(r.payload, 7) + ";";
    if (isLong(it)) s += "o" + origHash(it.sound) + "." + JSON.stringify(it.sound.cut) + ";";   /* (no record: its original's) */
    const v = { s, order };
    ownCache.set(it, v);
    return v;
}
/* id -> hash, for one panel's items */
function hashes(items) {
    const m = byIdMap(items), out = new Map(), busy = new Set();
    const h = id => {
        if (out.has(id)) return out.get(id);
        const it = m.get(id);
        if (!it) return "?";
        if (busy.has(id)) return "loop";
        busy.add(id);
        const own = ownHash(it);
        let s = own.s + "|";
        for (const key of own.order) {
            const t = it.link[key];
            s += t === BUILTIN ? "B" + key : t ? h(t) : "-" + key;
            s += ",";
        }
        if (it.kind === K.SAMPLE && it.entry) s += "e" + hstr(it.entry, 0);      /* (a draft's, or a long one's: not in the record) */
        const v = hstr(new TextEncoder().encode(s), 1) + hstr(new TextEncoder().encode(s), 2);
        busy.delete(id);
        out.set(id, v);
        return v;
    };
    for (const it of items) h(it.id);
    return out;
}

/* ---- numbers on the FM-1 panel ---- */
function taken(items) {
    const t = { c: new Set(), p: new Set(), s: new Set(), m: new Set() };
    for (const it of items) { const sp = space(it.kind); if (sp && it.num != null) t[sp].add(it.num); }
    return t;
}
/* a free number: want when free, else the lowest (most: the space's size, FIRST_FW for the built-ins' space, 256 samples) */
function pickNum(set, want, most) {
    most = most || FIRST_FW;
    if (want != null && want < most && !set.has(want)) return want;
    for (let n = 0; n < most; n++) if (!set.has(n)) return n;
    return -1;
}
/* the sample numbers instruments name that no sample holds (a reference to nothing): new samples pass over them, so an
 * instrument naming a missing sample never starts playing an unrelated one (specs/files/references.md) */
function namedNothing(items) {
    const m = byIdMap(items), out = new Set();
    for (const it of items) if (!it.dim) for (const f of refs(it)) if (f.sp === "m" && !m.has(it.link[lk(f.sp, f.num)])) out.add(f.num);
    return out;
}
/* an OLDER FORM sampler: the sample numbers it needs (its old audio, a sample each); the numbers free on a panel (256 less
 * the samples there and the numbers references name) */
const olderNeeds = (it, items) => items.filter(a => a.kind === K.AUDIO && a.owner === it.id).length;
const needText = n => n === 1 ? "1 SAMPLE NEEDS A NUMBER" : n + " SAMPLES NEED NUMBERS";
function freeSampleNumbers(items) {
    const t = taken(items).m;
    for (const n of namedNothing(items)) t.add(n);
    return 256 - t.size;
}

/* ---- the space: blocks as the uploader's planner places the records on an empty user area ---- */
const USER = [{ start: 0x093000, end: 0x0E0000, area: "user", inPlace: true }, { start: 0x0E5000, end: 0x0FF000, area: "user", inPlace: true }];
const RESERVE = 16;
function spaceOf(items, layout) {
    const regions = layout && layout.regions ? layout.regions.filter(r => r.area === "user") : USER;
    const reserve = layout && layout.reserve != null ? layout.reserve : RESERVE;
    /* (what the page cannot read is counted as it is, bytes and all) */
    const recs = diskOrder(toRecords(items));
    const files = recs.map(r => ({ kind: r.kind, num: r.num, sub: r.sub, ver: r.ver, len: r.payload.length, crc: 0, payload: r.payload, label: "" }));
    const accepts = Object.keys(R.VERSION).map(k => ({ kind: +k, ver: R.VERSION[k] }));
    for (const f of files) if (!accepts.some(a => a.kind === f.kind && a.ver === f.ver)) accepts.push({ kind: f.kind, ver: f.ver });
    /* the firmware's numbers: its built-in instruments, the EDITOR's pattern FE (the FM-1's own list when read) */
    const bl = layout && layout.builtins ? layout.builtins.slice()
        : builtins.map(b => ({ kind: b.kind, num: b.num, sub: b.sub, ver: b.ver })).concat([{ kind: K.PATTERN, num: FE, sub: 0, ver: 1 }]);
    const total = regions.reduce((n, r) => n + Math.floor((r.end - r.start) / 4096), 0);
    const P = globalThis.PTFM_seed.planClean({ regions, reserve: 0, accepts, lastSave: 0, records: [], builtins: bl, fwText: "" },
        { files: files.filter(f => !(f.num >= FIRST_FW && (isCart(f.kind) || f.kind === K.PATTERN) && !bl.some(b => b.kind === f.kind && b.num === f.num))), problems: [] });
    let audio = 0, data = 0;
    for (const f of files) if (f.kind === K.AUDIO || f.kind === K.SAMPLE) audio += 16 + align4(f.len); else data += 16 + align4(f.len);
    const used = P.ok ? P.space.need : Math.ceil((audio + data) / 4096) + 1;
    const audioBlocks = Math.min(used, Math.ceil(audio / 4096));
    const free = total - used;
    /* (the reserve is advice, not kept: what is refused is only what the planner cannot place; lowSpace says what
     * fewer free blocks cost the FM-1) */
    return { blocks: total, used, audioBlocks, dataBlocks: used - audioBlocks, free, reserve, fits: P.ok,
        missing: P.ok ? 0 : Math.max(1, used - total), low: P.ok ? lowSpace(free, reserve) : null, bytes: audio + data };
}
/* what few free blocks cost the FM-1's own saves (files/store.md): a SAVE keeps 2 free (compaction's) and a record
 * written needs one more, so under 3 it saves nothing, a delete included; new things keep the EDITOR's room too, 10;
 * the uploader's reserve (16) is what is advised. -> the words, or null when there is enough */
const SAVE_MIN_FREE = 3, SAVE_ROOM = 10;
function lowSpace(free, reserve) {
    const left = "ONLY " + Math.max(0, free) * 4 + " KB FREE ON THE FM-1: ";
    if (free < SAVE_MIN_FREE) return left + "IT CAN SAVE NOTHING, NOT EVEN A DELETE, UNTIL A WRITE FREES ROOM";
    if (free < SAVE_ROOM) return left + "IT CAN'T SAVE NEW THINGS (ITS SAVES KEEP " + SAVE_ROOM * 4 + " KB FREE); IT CAN STILL DELETE";
    const advised = reserve != null ? reserve : RESERVE;
    if (free < advised) return left + "LESS THAN THE " + advised * 4 + " KB ADVISED FOR ITS OWN SAVES";
    return null;
}

/* ---- moving items (specs/editor.md, Moving items) ---- */
const SYS_KINDS = new Set(SYSTEM);
function systemOf(it) { return SYS_KINDS.has(it.kind) ? it.kind : null; }
/* an item in a sentence: SONG 03 CITY LIGHTS, PATTERN 0A, SAMPLE KICK, MYSONG P07 (the TABLE's) */
function label(it, items) {
    if (SYS_NAME[it.kind] || (it.side === "R" && it.num === FE)) return nameOf(it, items);
    const onR = it.side === "R" && it.num != null;
    if (it.kind === K.PATTERN) return onR ? "PATTERN " + H2(it.num) : nameOf(it, items);
    const w = it.kind === K.SONG ? "SONG " : it.kind === K.SAMPLE ? "SAMPLE " : it.kind === K.SAMPLER ? "SAMPLER " : "";
    return (w + (onR ? H2(it.num) + " " : "") + nameOf(it, items)).trim();
}
/* st { L: [items], R: [items], ... }; opts { layout, stamp: the source label for items landing on the TABLE, builtins:
 * the FM-1 panel's built-in cartridges picked to the TABLE (the firmware's own as the panel lists them, or changed ones) }
 * (samples dropped on a sampler's item get their instruments from ED.files.transfer's `target`, after this) */
function transfer(st, from, ids, opts) {
    opts = opts || {};
    const to = from === "L" ? "R" : "L", dst = st[to];
    /* a built-in cartridge picked itself and moved to the TABLE is a copy there, a cartridge of its own: its contents (its
     * changes too) and name, no number to remember (moved back it takes a free number). What uses a built-in is
     * unchanged: a changed one it takes along still remembers its number */
    let src = st[from];
    const copyOf = new Map();                       /* the copy's source item -> the built-in's number */
    if (to === "L" && opts.builtins) {
        const twin = new Map(), twins = [];
        for (const b of opts.builtins) {
            if (!(b.fw || b.builtin) || !isCart(b.kind) || b.dim || !ids.includes(b.id) || twin.has(b.id)) continue;
            const c = { id: newId(), side: from, kind: b.kind, num: null, orig: null, link: Object.assign({}, b.link),
                recs: b.recs.map(r => ({ sub: r.sub, ver: r.ver, payload: r.payload.slice() })) };
            twin.set(b.id, c.id);
            copyOf.set(c.id, b.num);
            twins.push(c);
        }
        src = src.concat(twins);
        ids = ids.map(id => twin.get(id) || id);
    }
    const sm = byIdMap(src), dm = byIdMap(dst);
    ids = ids.filter(id => sm.has(id));
    const dims = ids.filter(id => sm.get(id).dim);
    ids = ids.filter(id => !sm.get(id).dim);
    if (!ids.length) return { ok: false, msg: !dims.length ? "SELECT SOMETHING TO MOVE FIRST" : dims.every(id => sm.get(id).older)
        ? "A SAMPLER IN THE OLDER FORM STAYS WHERE IT IS: " + needText(dims.reduce((n, id) => n + olderNeeds(sm.get(id), src), 0)) +
          ", " + freeSampleNumbers(src) + " FREE: DELETE SAMPLES, OR IT, TO FREE NUMBERS"
        : dims.every(id => sm.get(id).differ) ? "A SAMPLE WHOSE ENTRY AND AUDIO DIFFER STAYS WHERE IT IS: THE FM-1 READS IT AS MISSING"
        : "A VERSION THIS PAGE CANNOT READ STAYS WHERE IT IS" };
    /* a sample too long for the FM-1 goes nowhere until it fits (nothing names a loose one: only one picked) */
    const long = ids.map(id => sm.get(id)).filter(isLong);
    if (long.length) return { ok: false, msg: (ids.length > 1 ? nameOf(long[0], src) + " IS " : "") + longText(long[0]) +
        (long.length > 1 ? " (AND " + (long.length - 1) + " MORE)" : "") + ": NOTHING MOVED" };
    const set = closure(src, ids);
    for (const id of [...set]) if (sm.get(id).dim) set.delete(id);    /* (what the page cannot read stays where it is) */
    const sh = hashes(src), dh = hashes(dst);
    const dByHash = new Map();                      /* kind|hash -> the other side's items of that content */
    for (const d of dst) if (!d.dim) { const k = d.kind + "|" + dh.get(d.id); if (!dByHash.has(k)) dByHash.set(k, []); dByHash.get(k).push(d); }
    /* the item there with the same content and the same kind of use: a cartridge that remembers a built-in's number (a
     * changed built-in) and a plain one are never the same item there, nor is what uses one and what uses the other, so
     * a built-in's copy stays a cartridge of its own and a changed built-in a song takes along keeps its number; on the
     * FM-1 panel a changed built-in whose number holds another change is the plain one it took a free number as before */
    const at = x => x.side === "R" ? x.num : x.orig;
    const held = new Set(dst.filter(d => d.num != null && space(d.kind)).map(d => space(d.kind) + d.num));
    const fits = (it, d) => !it.builtin ? !d.builtin : d.builtin || (to === "R" && held.has(space(it.kind) + it.orig));
    const agree = new Map();                        /* "src id|dst id" -> the same item (their references, in turn, too) */
    /* the same when every pair reached through their references, in step, has the same kind of use; a pair met again
     * (a loop of references) is already being checked and adds nothing. A pair found the same makes every pair it
     * reached the same too (what they reach passed); one found not the same says nothing of the pairs on the way */
    const same = (it, d) => {
        const k0 = it.id + "|" + d.id;
        if (agree.has(k0)) return agree.get(k0);
        const seen = new Set([k0]), todo = [[it, d]];
        let ok = true;
        while (ok && todo.length) {
            const [x, y] = todo.pop(), known = agree.get(x.id + "|" + y.id);
            if (known === true) continue;
            if (known === false || !fits(x, y)) { ok = false; break; }
            const a = ownHash(x).order, b = ownHash(y).order;
            for (let i = 0; i < a.length; i++) {
                const t = sm.get(x.link[a[i]]), u = dm.get(y.link[b[i]]);
                if (!t || !u) continue;
                const k = t.id + "|" + u.id;
                if (!seen.has(k)) { seen.add(k); todo.push([t, u]); }
            }
        }
        if (ok) for (const k of seen) agree.set(k, true);
        else agree.set(k0, false);
        return ok;
    };
    const sameAs = it => {
        const c = (dByHash.get(it.kind + "|" + sh.get(it.id)) || []).filter(d => same(it, d));
        return c.find(d => d.builtin && at(d) === at(it)) || c.find(d => d.builtin) || c[0] || null;
    };
    /* (equal items within the move are one item there, the same kind of use counted as above) */
    const keys = new Map();
    const twinKey = it => {
        if (keys.has(it.id)) return keys.get(it.id);
        keys.set(it.id, "");
        let k = it.kind + "|" + sh.get(it.id) + (it.builtin ? "|b" + at(it) : "");
        for (const r of ownHash(it).order) { const t = sm.get(it.link[r]); k += "," + (t ? twinKey(t) : ""); }
        keys.set(it.id, k);
        return k;
    };
    const map = new Map();                          /* src id -> dst id */
    const made = [], reused = [], replaced = [], leftOut = [];
    const order = [...set].map(id => sm.get(id));
    /* equal items within the move are one item there (two disks' same song, a sample two samplers name); one system item
     * of a kind */
    const madeByHash = new Map(), sysMade = new Map();
    for (const it of order) {
        const hh = sh.get(it.id);
        if (to === "R" && systemOf(it)) {
            if (sysMade.has(it.kind)) { leftOut.push(it); continue; }     /* (the FM-1 keeps one of each: the first moved) */
            sysMade.set(it.kind, it.id);
            const ex = dst.find(d => d.kind === it.kind);
            if (ex && dh.get(ex.id) === hh) { map.set(it.id, ex.id); reused.push(it.id); continue; }
            map.set(it.id, newId()); made.push(it);
            if (ex) replaced.push(ex);
            continue;
        }
        const same = sameAs(it);
        if (same) { map.set(it.id, same.id); reused.push(it.id); continue; }
        const twin = madeByHash.get(twinKey(it));
        if (twin) { map.set(it.id, map.get(twin.id)); continue; }
        map.set(it.id, newId()); made.push(it);
        madeByHash.set(twinKey(it), it);
    }
    for (const it of leftOut) set.delete(it.id);
    /* numbers on the FM-1 panel */
    const numOf = new Map(), renum = [];
    if (to === "R") {
        const keep = dst.filter(d => !replaced.includes(d)), tk = taken(keep);
        /* (the sample numbers references name with no sample there, the panel's and those the move brings: passed over) */
        const nothing = namedNothing(keep);
        for (const it of made) if (!it.dim) for (const f of refs(it)) if (f.sp === "m" && !sm.has(it.link[lk(f.sp, f.num)])) nothing.add(f.num);
        /* (more samples than there are free sample numbers: refused, saying how many it needs) */
        const needed = made.filter(it => it.kind === K.SAMPLE).length;
        if (needed) {
            let free = 0;
            for (let n = 0; n < 256; n++) if (!tk.m.has(n) && !nothing.has(n)) free++;
            if (needed > free) return { ok: false, msg: "NOT ENOUGH SAMPLE NUMBERS: " + needed + " NEEDED, " + free + " FREE" };
        }
        for (const it of made) {
            const sp = space(it.kind);
            if (!sp || systemOf(it)) continue;
            let n;
            if (it.builtin && it.orig >= FIRST_FW) {
                n = tk[sp].has(it.orig) ? pickNum(tk[sp], null) : it.orig;
            } else {
                const want = it.side === "R" ? it.num : it.orig, most = SPACE_MOST[sp];
                const busy = sp === "m" ? new Set([...tk.m, ...nothing]) : tk[sp];
                n = pickNum(busy, want != null && want < most ? want : null, most);
                if (want != null && n !== want && n >= 0) renum.push([it, n]);
            }
            if (n < 0) return { ok: false, msg: sp === "m" ? "NO FREE SAMPLE NUMBER: DELETE A SAMPLE FIRST" : "THE FM-1 HAS NO FREE " + SPACE_NAME[sp] + " NUMBER LEFT: DELETE ONE FIRST" };
            tk[sp].add(n); numOf.set(it.id, n);
        }
    }
    /* the copies; on the FM-1 panel each reference names its target's number there */
    const stamp = opts.stamp || "FM-1";
    const numByDst = new Map();
    for (const d of dst) if (d.num != null) numByDst.set(d.id, d.num);
    for (const it of made) if (numOf.has(it.id)) numByDst.set(map.get(it.id), numOf.get(it.id));
    const atDst = { c: new Map(), p: new Map(), s: new Map(), m: new Map() };
    for (const d of dst) { const sp = space(d.kind); if (sp && d.num != null && !replaced.includes(d)) atDst[sp].set(d.num, d.id); }
    /* (an item the other side holds already, by content: where a soft reference left behind still finds its own) */
    const there = id => { const s = sm.get(id); const d = s && sameAs(s); return d ? d.id : null; };
    const ctx = { map, numOf, numByDst, atDst, sm, stamp, there };
    const copies = made.map(it => copyItem(it, to, ctx));
    const cm = byIdMap(copies);
    const next = dst.filter(d => !replaced.includes(d));
    if (to === "R") {
        const after = next.concat(copies);
        const sp = spaceOf(after, opts.layout);
        if (!sp.fits) {
            const what = ids.length === 1 ? label(sm.get(ids[0]), src) : ids.length + " ITEMS";
            return { ok: false, need: sp.missing * 4096, msg: "DOESN'T FIT: " + what + " NEEDS " + sp.missing * 4 + " KB MORE ON THE FM-1. NOTHING MOVED." };
        }
    }
    const out = Object.assign({}, st, { [to]: to === "R" ? linkChanged(next.concat(copies)) : next.concat(copies) });
    const moved = ids.map(id => map.get(id)).filter(Boolean);
    /* the status line */
    const rest = copies.filter(c => !ids.some(id => map.get(id) === c.id));
    const cnt = {};
    for (const c of rest) { const k = oneOf(c); cnt[k] = (cnt[k] || 0) + 1; }
    const parts = [(to === "R" ? "→ FM-1: " : "← TABLE: ") + (ids.length === 1 ? label(cm.get(map.get(ids[0])) || sm.get(ids[0]), out[to]) : ids.length + " ITEMS")];
    const extra = Object.entries(cnt).map(([k, n]) => n + " " + k + (n > 1 ? "S" : "")).join(", ");
    if (extra) parts.push("+ " + extra);
    const copied = made.filter(it => copyOf.has(it.id)).map(it => H2(copyOf.get(it.id)));
    if (copied.length) parts.push((copied.length > 1 ? "COPIES OF BUILT-INS " : "A COPY OF BUILT-IN ") + copied.join(", "));
    if (reused.length) parts.push(reused.length + (reused.length > 1 ? " ITEMS" : " ITEM") + " ALREADY THERE");
    if (renum.length) parts.push(renum.slice(0, 2).map(([it, n]) => nameOf(it, src) + " IS NOW " + H2(n)).join(", ") +
        (renum.length > 2 ? " AND " + (renum.length - 2) + " MORE RENUMBERED" : ""));
    if (replaced.length) parts.push(replaced.map(r => nameOf(r, dst)).join(", ") + " REPLACED");
    if (leftOut.length) parts.push("THE FM-1 KEEPS ONE OF EACH: " + leftOut.map(r => nameOf(r, src) + " FROM " + (r.src || "THE TABLE")).join(", ") + " NOT MOVED");
    if (!copies.length) parts.push("NOTHING NEW TO COPY");
    return { ok: true, st: out, msg: parts.join(" · "), ids: moved, replaced: replaced.map(r => r.id) };
}
function packedEntry(e) { return R.packSample(e, new Uint8Array(0)); }       /* (a sample's 16-byte entry) */
function copyItem(it, to, ctx) {
    const { map, numOf, numByDst, atDst, sm, stamp, there } = ctx;
    const c = Object.assign({}, it, { id: map.get(it.id), side: to });
    if (to === "R") {
        delete c.src; delete c.name; delete c.sound;    /* (no original on the FM-1 panel: a cut is final once written) */
        c.num = numOf.has(it.id) ? numOf.get(it.id) : null;
        if (SYS_KINDS.has(it.kind)) c.num = 0;
        /* the records' numbers become the panel's: a target's number there (moved or already there); a built-in's
         * stays, naming the panel's changed one when it has one */
        const link = {};
        c.recs = rewrite(it, (f, t) => {
            if (t === BUILTIN) { link[lk(f.sp, f.num)] = atDst[f.sp].get(f.num) || BUILTIN; return f.num; }
            const tid = t ? map.get(t) || (f.soft ? there(t) : null) : null;
            const nn = tid ? numByDst.get(tid) : null;
            if (nn == null) { if (f.soft) return -1; link[lk(f.sp, f.num)] = atDst[f.sp].get(f.num) || null; return f.num; }
            link[lk(f.sp, nn)] = tid;
            return nn;
        });
        c.link = link;
        if (it.builtin && c.num < FIRST_FW) delete c.builtin;
    } else {
        c.num = null;
        c.orig = it.side === "R" ? it.num : it.orig;
        c.src = it.side === "R" ? stamp : it.src;
        const link = {};
        for (const k in it.link) {
            const t = it.link[k];
            link[k] = t === BUILTIN || t == null ? t : map.get(t) || there(t) || null;
        }
        /* a soft reference whose item stays behind, and is not there already: cleared */
        const gone = t => t && t !== BUILTIN && !map.has(t) && !there(t);
        c.recs = rewrite(it, (f, t) => f.soft && gone(t) ? -1 : f.num);
        for (const f of refs(it)) if (f.soft && gone(it.link[lk(f.sp, f.num)])) delete link[lk(f.sp, f.num)];
        c.link = link;
        if (it.side === "R" && it.num === FE && (it.kind === K.PATTERN || it.kind === K.SONG))
            c.name = it.kind === K.PATTERN ? "EDITOR FE" : "KEYS FE";
        else if (it.side === "R" && it.kind === K.PATTERN) c.name = c.name || patternPlace(it, sm) || "PATTERN " + H2(it.num);
    }
    return c;
}
/* where a pattern of the FM-1 panel plays: CITY LIGHTS · POS 02 */
function patternPlace(it, items) {
    const list = items instanceof Map ? [...items.values()] : items;
    for (const s of list) {
        if (s.kind !== K.SONG || s.side !== "R") continue;
        const b = s.recs[0].payload, n = b[20] + 1;
        for (let i = 0; i < n; i++) if (b[21 + i] === it.num) return nameOf(s, list) + " · POS " + H2(i);
    }
    return null;
}

/* ---- DELETE ---- */
/* what must go first: [[user, item]] for items something outside the set still uses */
function blockers(items, ids) {
    const sel = deleteSet(items, ids), us = users(items), m = byIdMap(items), out = [];
    for (const id of sel) {
        if (m.get(id) && m.get(id).builtin) continue;   /* (a changed built-in: what uses it plays the firmware's own then) */
        /* (a FAVORITES cartridge never goes along: its favourites keep naming what goes, MISSING) */
        /* (on the FM-1 panel a sample's users, the samplers, never stand in its way: DELETE takes the instruments naming it,
         * not the sampler. On the TABLE, whose items have no numbers, they do, as before: DELETE takes the sampler and
         * what uses that, to the end) */
        const smp = m.get(id) && m.get(id).kind === K.SAMPLE && m.get(id).side === "R";
        for (const u of us.get(id) || []) if (!sel.has(u) && !(m.get(u) && m.get(u).kind === K.FAVORITES) && !(smp && m.get(u) && m.get(u).kind === K.SAMPLER)) out.push([u, id]);
    }
    return out;
}
/* the instruments the samples among the set name, in the samplers outside it: DELETE of a sample on the FM-1 panel takes
 * them too (the page's question says how many; on the TABLE the samplers go instead) -> { instruments, samplers:
 * [sampler id], names: [instrument names] } */
function instrumentsNaming(items, ids, also) {
    const m = byIdMap(items), gone = deleteSet(items, ids);
    if (also) for (const x of exclusive(items, ids)) gone.add(x);
    const out = { instruments: 0, samplers: [], names: [] };
    for (const it of items) {
        if (it.kind !== K.SAMPLER || it.dim || gone.has(it.id) || it.side !== "R") continue;
        let any = false;
        for (const f of refs(it)) if (f.sp === "m" && gone.has(it.link[lk(f.sp, f.num)]) && m.get(it.link[lk(f.sp, f.num)]).kind === K.SAMPLE) {
            any = true; out.instruments++;
            const s = R.unpackSamplerInst(it.recs[0].payload.subarray(f.at, f.at + R.SAMPLER_SLOT), 2);
            if (s) out.names.push(s.name);
        }
        if (any) out.samplers.push(it.id);
    }
    return out;
}
/* what uses the items, and what uses that, to the end: what DELETE takes with them. { ids: the users, pairs: [user,
 * what it uses] in the order found (the nearest first) } */
function usersAll(items, ids) {
    const set = new Set(ids), out = [], pairs = [];
    for (;;) {
        const bl = blockers(items, [...set]);
        if (!bl.length) return { ids: out, pairs };
        for (const [u, id] of bl) {
            pairs.push([u, id]);
            if (!set.has(u)) { set.add(u); out.push(u); }
        }
    }
}
function deleteSet(items, ids) {
    const sel = new Set(ids), owned = ownedMap(items);
    for (const id of ids) for (const s of owned.get(id) || []) sel.add(s);
    return sel;
}
/* what only the set uses (ALSO WHAT ONLY IT USES): patterns, cartridges, samples nothing else uses */
function exclusive(items, ids) {
    const m = byIdMap(items), sel = deleteSet(items, ids), us = users(items), owned = ownedMap(items), extra = new Set();
    const goes = id => sel.has(id) || extra.has(id);
    let grew = true;
    while (grew) {
        grew = false;
        for (const id of [...sel, ...extra]) {
            const it = m.get(id);
            if (!it || it.kind === K.FAVORITES) continue;   /* (a FAVORITES cartridge never takes its sources along) */
            for (const t of hardUses(it, m)) {
                if (goes(t)) continue;
                const ti = m.get(t);
                if (!ti || SYS_KINDS.has(ti.kind) || ti.builtin) continue;    /* (a changed built-in goes only when asked) */
                if ((us.get(t) || []).every(goes)) {
                    extra.add(t);
                    for (const s of owned.get(t) || []) extra.add(s);
                    grew = true;
                }
            }
        }
    }
    return extra;
}
/* remove the items (with what only they use when also); a sampler's samples go with it; a sample alone leaves its
 * sampler's entry empty; a changed built-in goes back to the firmware's (references to it then name that) */
function remove(st, side, ids, also) {
    const items = st[side], m = byIdMap(items);
    ids = ids.filter(id => m.has(id));
    const gone = deleteSet(items, ids);
    if (also) for (const x of exclusive(items, ids)) gone.add(x);
    let bytes = 0;
    for (const id of gone) bytes += size(m.get(id));
    const backToFw = new Set([...gone].filter(id => m.get(id).builtin));
    let next = items.filter(i => !gone.has(i.id));
    next = next.map(i => {
        let j = i;
        if (i.kind === K.SAMPLER && !i.dim) {
            /* the instruments naming a sample that goes go with it: their slots become empty ones, their links dropped */
            const hit = refs(i).filter(f => f.sp === "m" && gone.has(i.link[lk(f.sp, f.num)]));
            if (hit.length) {
                const b = i.recs[0].payload.slice(), link = Object.assign({}, i.link);
                for (const f of hit) { b.set(R.packSamplerInst(null, 2), f.at); delete link[lk(f.sp, f.num)]; }
                j = Object.assign({}, i, { recs: [Object.assign({}, i.recs[0], { payload: b })], link });
            }
        }
        if (backToFw.size && Object.values(i.link).some(t => backToFw.has(t))) {
            const link = {};
            for (const k in j.link) link[k] = backToFw.has(j.link[k]) ? BUILTIN : j.link[k];
            j = Object.assign({}, j, { link });
        }
        return j;
    });
    return { st: Object.assign({}, st, { [side]: next }), gone, bytes };
}
function deleteAll(st, side) { return Object.assign({}, st, { [side]: [] }); }

/* ---- what WRITE would change: the FM-1 panel against what it held after the last READ or WRITE ---- */
const recKey = (it, items) => it.kind + "." + (it.kind === K.AUDIO ? (items.get(it.owner) || {}).num + "." + it.idx : it.num);
function sameRecs(a, b) {
    if (a.recs.length !== b.recs.length) return false;
    for (let i = 0; i < a.recs.length; i++) {
        const x = a.recs[i], y = b.recs[i];
        if (x.sub !== y.sub || x.ver !== y.ver || x.payload.length !== y.payload.length) return false;
        if (x.payload !== y.payload) for (let k = 0; k < x.payload.length; k++) if (x.payload[k] !== y.payload[k]) return false;
    }
    return true;
}
function pending(st) {
    const now = st.R, base = st.base || [];
    const nm = byIdMap(now), bm = byIdMap(base);
    const bk = new Map(base.map(b => [recKey(b, bm), b]));
    const ids = new Set(), keys = new Set();
    for (const it of now) {
        const k = recKey(it, nm);
        keys.add(k);
        const b = bk.get(k);
        if (!b || !sameRecs(b, it)) ids.add(it.id);
    }
    const removed = base.filter(b => !keys.has(recKey(b, bm)));
    return { ids, removed, count: ids.size + removed.length };
}

/* ---- UNDO: whole states (items are shared between them, never changed) ---- */
class History {
    constructor(most) { this.most = most || 100; this.back = []; this.fwd = []; }
    push(st, what) { this.back.push({ st, what }); if (this.back.length > this.most) this.back.shift(); this.fwd = []; }
    undo(cur) { const e = this.back.pop(); if (!e) return null; this.fwd.push({ st: cur, what: e.what }); return e; }
    redo(cur) { const e = this.fwd.pop(); if (!e) return null; this.back.push({ st: cur, what: e.what }); return e; }
    get canUndo() { return this.back.length > 0; }
    get canRedo() { return this.fwd.length > 0; }
}

/* the item with its link keeping only what its records name (an edit dropped a reference) */
function pruned(it) {
    const keep = new Set(refs(it).map(f => lk(f.sp, f.num))), link = {};
    for (const k in it.link) if (keep.has(k)) link[k] = it.link[k];
    const out = Object.assign({}, it, { link });
    delete out.cutting;                             /* (a SAMPLE tab's slices: the tab's alone, never stored) */
    delete out.moved;                               /* (a cartridge tab's map of places: written into what names them at SAVE) */
    delete out.pending;                             /* (and the samples a paste brings: added as items at SAVE) */
    return out;
}
/* a number for target (an item id, or BUILTIN with its number) in the item's own numbering: the one it has there
 * already, else on the FM-1 panel the target's own, else a free one of the item's */
function numberFor(it, sp, target, side, targetNum, orig) {
    for (const k in it.link) if (k[0] === sp && it.link[k] === target && target !== BUILTIN) return parseInt(k.slice(1), 16);
    if (target === BUILTIN || side === "R") return targetNum;
    const used = new Set(Object.keys(it.link).filter(k => k[0] === sp).map(k => parseInt(k.slice(1), 16)));
    for (const f of refs(it)) if (f.sp === sp) used.add(f.num);
    let n = orig != null && orig < FIRST_FW && !used.has(orig) ? orig : 0;
    while (used.has(n) && n < FIRST_FW) n++;
    return n;
}

/* an item changed in a tab: new records (and links: a pattern naming another cartridge); the same id */
function replaceItem(st, side, it) {
    return Object.assign({}, st, { [side]: st[side].map(i => i.id === it.id ? it : i) });
}
/* a cartridge's number on the FM-1 panel: a free one of 00-EF, or a held one, the two swapping (specs/editor.md, Numbers on
 * the FM-1 panel). Every reference follows, those naming nothing too, as the FM-1's MOVE and SWAP answered YES
 * (specs/files/references.md: a MOVE onto an empty place is a SWAP with it).
 * -> { ok, st, msg (done), plan (what it would do), other (what took the old number) }, or { ok: false, msg } */
const canRenumber = it => isCart(it.kind) && it.num != null && it.num < FIRST_FW && !it.builtin && !it.fw && !it.dim;
const NAMES_NO_CART = new Set([K.SAMPLE, K.AUDIO, K.SAMPLER]);
function renumber(st, id, to) {
    const items = st.R, it = items.find(i => i.id === id);
    if (!it || !canRenumber(it)) return { ok: false, msg: "ONLY A CARTRIDGE OF YOUR OWN ON THE FM-1 PANEL TAKES ANOTHER NUMBER" };
    if (!Number.isInteger(to) || to < 0 || to > 0xFF) return { ok: false, msg: "A NUMBER FROM 00 TO EF" };
    if (to >= FIRST_FW) return { ok: false, msg: "F0-FF ARE THE BUILT-IN INSTRUMENTS': A NUMBER FROM 00 TO EF" };
    if (to === it.num) return { ok: false, msg: nameOf(it, items) + " IS " + H2(to) + " ALREADY" };
    const from = it.num, other = items.find(i => isCart(i.kind) && i.num === to) || null;
    /* (what the page cannot read keeps its bytes: what it names could not follow, nor can it take another number) */
    if (items.some(i => i.dim && !NAMES_NO_CART.has(i.kind)) || (other && !canRenumber(other)))
        return { ok: false, msg: "THIS PANEL HOLDS A VERSION THIS PAGE CANNOT READ: ITS NUMBERS COULD NOT FOLLOW. NOTHING CHANGED" };
    const swapped = n => n === from ? to : n === to ? from : n;
    let count = 0;
    const next = items.map(i => {
        let out = i;
        if (refs(i).some(f => f.sp === "c" && swapped(f.num) !== f.num)) {
            const recs = rewrite(i, f => {
                if (f.sp !== "c" || swapped(f.num) === f.num) return f.num;
                count++;
                return swapped(f.num);
            });
            const link = {};
            for (const k in i.link) link[k[0] === "c" ? lk("c", swapped(parseInt(k.slice(1), 16))) : k] = i.link[k];
            out = Object.assign({}, i, { recs, link });
        }
        return i === it ? Object.assign({}, out, { num: to }) : i === other ? Object.assign({}, out, { num: from }) : out;
    });
    const name = nameOf(it, items), follow = count === 1 ? " · 1 REFERENCE FOLLOWS" : count ? " · " + count + " REFERENCES FOLLOW" : "";
    const theirs = other ? ", " + nameOf(other, items) + " " + H2(from) : "";
    return { ok: true, st: Object.assign({}, st, { R: next }), other,
        msg: name + " IS NOW " + H2(to) + theirs + follow + " · THE FM-1 CHANGES WITH WRITE",
        plan: (other ? H2(to) + " HOLDS " + nameOf(other, items) + ": THE TWO SWAP NUMBERS" : H2(to) + " IS FREE") + follow };
}

/* the FM-1 panel: a reference to a built-in's number names the changed one the panel holds there, as on the FM-1
 * (after a move or a SAVE lands one; DELETE gives the firmware's own back) */
function linkChanged(items) {
    const at = new Map();
    for (const i of items) if (i.builtin && i.num != null && i.num >= FIRST_FW && space(i.kind)) at.set(lk(space(i.kind), i.num), i.id);
    if (!at.size) return items;
    let any = false;
    const out = items.map(i => {
        let link = null;
        for (const k in i.link) {
            const id = i.link[k] === BUILTIN && at.get(k);
            if (id && id !== i.id) (link = link || Object.assign({}, i.link))[k] = id;
        }
        if (!link) return i;
        any = true;
        return Object.assign({}, i, { link });
    });
    return any ? out : items;
}

ED.model = { BUILTIN, SYSTEM, FE, FIRST_FW, newId, seen, setBuiltins, builtinAt, get builtins() { return builtins; }, refs,
    rewrite, lk, fromRecords, toRecords, nameOf, groupOf, oneOf, kindText, label, ENGINE, size, sizeWith, closure, users, uses,
    hardUses, isSoft, hashes, taken, pickNum, space: spaceOf, lowSpace, USER, RESERVE, transfer, blockers, usersAll, exclusive, remove,
    deleteAll, pending, History, replaceItem, linkChanged, renumber, canRenumber, pruned, numberFor, sampleEntry, packedEntry, patternPlace, byIdMap, ownedMap,
    numSpace: space, isCart, H2, MAX_FRAMES, isLong, asLong, longText, upgradeKept, instrumentsNaming, namedNothing, freeSampleNumbers,
    spaceMost: SPACE_MOST };
})(globalThis.ED = globalThis.ED || {});

/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The two panels' items and their rules (specs/editor.md: Items, references, hashes; Moving items; DELETE and DELETE
 * ALL; UNDO): no DOM here, so the tests run it in Node.
 *
 * An item is one of PT/FM's objects with all its records, immutable (a change makes a new item):
 *   { id, side "L" (TABLE) | "R" (FM-1), kind (the record's kind), num (the FM-1 panel's number; null on the TABLE),
 *     orig (the number it had where it came from, or null), recs [{ sub, ver, payload }] by sub,
 *     link { "c05": id | BUILTIN | null, ... } (its references, as numbers in its records, to the items they name),
 *     owner, idx (a sample: its sampler's id and its place there; a loose sample on the TABLE: owner null, entry its
 *     16-byte entry), name (the TABLE's name for a kind with none), src (where it came from), builtin (a changed
 *     built-in: its number is the firmware's), dim (a version the page cannot read: kept as it is), orig audio }
 * A TABLE item's numbers are those of where it came from (its own frame: nothing else reads them); an FM-1 item's are
 * the panel's, its link naming the items at those numbers. Moving rewrites the numbers in place, byte for byte, so
 * what does not change keeps its bytes (and its CRC: WRITE leaves it on the FM-1).
 *
 *   ED.model.fromRecords(recs, { side, src, builtins }) -> { items, skipped }
 *   ED.model.toRecords(items) -> the FM-1 panel's records [{ kind, num, sub, ver, payload }]
 *   refs(item), closure(items, ids), users(items), hashes(items), size(item), sizeWith(items, id)
 *   transfer(st, from, ids, opts) -> { ok, st, msg, ids (the moved ones on the other side), need }
 *   blockers(items, ids), exclusive(items, sel), remove(st, side, ids, also) -> { st, msg }, deleteAll(st, side)
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
const space = k => isCart(k) ? "c" : k === K.PATTERN ? "p" : k === K.SONG ? "s" : null;     /* numbers' spaces */
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
    recs = R.upgrade(recs.filter(r => r.payload && r.kind < 0xE0));
    const groups = new Map();                       /* "kind.num" -> records */
    for (const r of recs) {
        const g = (r.kind === K.AUDIO ? "a" : r.kind) + "." + r.num;
        if (!groups.has(g)) groups.set(g, []);
        groups.get(g).push(r);
    }
    const byNum = { c: new Map(), p: new Map(), s: new Map() };
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
        if (space(r0.kind) && r0.num >= FIRST_FW) {
            if (r0.num === FE && (r0.kind === K.PATTERN || r0.kind === K.SONG)) {
                if (side === "L") it.name = r0.kind === K.PATTERN ? "EDITOR FE" : "KEYS FE";
            } else it.builtin = true;               /* a changed built-in */
        }
        items.push(it);
        if (space(r0.kind)) byNum[space(r0.kind)].set(r0.num, it);
    }
    /* samples: under their sampler (its number); audio of no sampler: loose on the TABLE, else left out */
    for (const [g, rs] of groups) {
        if (g[0] !== "a") continue;
        for (const r of rs) {
            const owner = byNum.c.get(r.num);
            if (!owner || owner.kind !== K.SAMPLER) { skipped.push(r); continue; }
            const s = { id: newId(), side, kind: K.AUDIO, num: side === "R" ? r.num : null, orig: r.num, link: {},
                recs: [{ sub: r.sub, ver: r.ver, payload: r.payload }], owner: owner.id, idx: r.sub, src: o.src || null };
            if (!known(r) || owner.dim) s.dim = true;      /* (a sampler this page cannot read: its samples with it) */
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
    if (side === "L") nameTable(items, o.src);
    return { items, skipped };
}
/* the TABLE's names for kinds with none: MYSONG P07, SMP 0C */
function nameTable(items, src) {
    const base = String(src || "").replace(/\.[^.]*$/, "").toUpperCase().replace(/[^A-Z0-9 ]+/g, " ").trim().slice(0, 10) || "PATTERN";
    for (const it of items) {
        if (it.name) continue;
        if (it.kind === K.PATTERN) it.name = base + " P" + H2(it.orig);
        else if (it.kind === K.AUDIO && !sampleName(it, items)) it.name = "SMP " + H2(it.idx);
    }
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
const DISK_ORDER = [0x30, 0x31, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x21, 0x20, 0x22, 0x23, 0xE0, 0x10];
function diskOrder(recs) {
    const rank = k => { const i = DISK_ORDER.indexOf(k); return i < 0 ? 100 + k : i; };
    return recs.slice().sort((a, b) => rank(a.kind) - rank(b.kind) || a.num - b.num || a.sub - b.sub);
}

/* ---- what items are ---- */
const byIdMap = items => { const m = new Map(); for (const i of items) m.set(i.id, i); return m; };
/* a sample's entry: its own when it carries one (loose, or a tab's draft of it), else its sampler's */
function sampleEntry(it, items) {
    try {
        if (it.entry) return R.unpackSampler(samplerBytes("", [], [it.entry])).samples[0];
        if (it.owner) {
            const o = (items instanceof Map ? items : byIdMap(items)).get(it.owner);
            if (o && !o.dim) return R.unpackSampler(o.recs[0].payload).samples[it.idx] || null;
        }
    } catch (e) { /* (a sampler that does not read: no entry) */ }
    return null;
}
function samplerBytes(name, insts, entries) {       /* a sampler record from packed instruments and entries */
    const b = new Uint8Array(11 + insts.length * R.SAMPLER_SLOT + entries.length * R.SAMPLE_ENTRY);
    b.set(R.packName(name, 12), 0);
    b[9] = insts.length; b[10] = entries.length;
    insts.forEach((s, i) => b.set(s, 11 + i * R.SAMPLER_SLOT));
    const at = 11 + insts.length * R.SAMPLER_SLOT;
    entries.forEach((e, i) => b.set(e || R.packSampler({ name: "", instruments: [], samples: [null] }).subarray(11, 27), at + i * R.SAMPLE_ENTRY));
    return b;
}
function sampleName(it, items) { const e = sampleEntry(it, items); return e ? e.name : null; }
const SYS_NAME = { [K.SETTINGS]: "SETTINGS", [K.MIDIMAP]: "MIDI MAP", [K.LOOPS]: "LOOPS", [K.DJ]: "DJ SETUP" };
function nameOf(it, items) {
    if (it.dim) return "VERSION " + it.recs[0].ver;
    if (SYS_NAME[it.kind]) return SYS_NAME[it.kind];
    if (it.side === "R" && it.num === FE) return it.kind === K.PATTERN ? "EDITOR'S PATTERN" : "KEYS' SONG";
    if (it.name) return it.name;
    if (isCart(it.kind)) return R.unpackName(it.recs[0].payload.subarray(0, 9), 12) || "CART " + H2(it.num != null ? it.num : it.orig);
    if (it.kind === K.SONG) return R.unpackName(it.recs[0].payload.subarray(0, 12), 16) || "SONG";
    if (it.kind === K.AUDIO) return (items && sampleName(it, items)) || "SMP " + H2(it.idx);
    if (it.kind === K.PATTERN) return "PATTERN " + H2(it.num != null ? it.num : it.orig);
    return "?";
}
/* its group: SONGS, PATTERNS, CARTRIDGES, SAMPLERS, SAMPLES, SYSTEM */
function groupOf(it) {
    if (SYS_NAME[it.kind] || (it.side === "R" && it.num === FE)) return "SYSTEM";
    if (it.kind === K.SONG) return "SONGS";
    if (it.kind === K.PATTERN) return "PATTERNS";
    if (it.kind === K.SAMPLER) return "SAMPLERS";
    if (it.kind === K.AUDIO) return "SAMPLES";
    return "CARTRIDGES";
}
/* one of its group, in a count: SONG, PATTERN, CARTRIDGE, SAMPLER, SAMPLE, SYSTEM ITEM */
const oneOf = it => { const g = groupOf(it); return g === "SYSTEM" ? "SYSTEM ITEM" : g.replace(/S$/, ""); };
const ENGINE = { [K.DX7]: "DX7", [K.JUNO]: "JUNO-106", [K.TB303]: "TB-303", [K.SAMPLER]: "SAMPLER", [K.FUSION]: "FUSION",
    [K.CHIP]: "CHIP" };
/* its kind in words: 4 TRACKS × 64, SAMPLER · 10 INSTR · 7 SMP, 8-BIT · 22.1 KHZ · 0.42 S */
function kindText(it, items) {
    if (it.dim) return (SYS_NAME[it.kind] || ENGINE[it.kind] || "KIND " + H2(it.kind)) + " · NOT READ";
    const b = it.recs[0].payload;
    if (it.kind === K.PATTERN) return b[0] + " TRACKS × " + (b[1] + 1);
    if (it.kind === K.SONG) return (b[20] + 1) + (b[20] ? " POSITIONS" : " POSITION") + " · TEMPO " + (b[12] | b[13] << 8);
    if (it.kind === K.SAMPLER) {
        const s = R.unpackSampler(b);
        return "SAMPLER · " + s.instruments.filter(Boolean).length + " INSTR · " + s.samples.filter(Boolean).length + " SMP";
    }
    if (isCart(it.kind)) return ENGINE[it.kind] + " · " + R.unpackCart(it.kind, b).slots.filter(Boolean).length + " INSTR";
    if (it.kind === K.AUDIO) {
        const e = sampleEntry(it, items);
        if (!e) return "SAMPLE";
        return (e.coding === 2 ? "16" : "8") + "-BIT · " + (e.rate / 1000).toFixed(1) + " KHZ · " + (e.rate ? (e.length / e.rate).toFixed(2) : "?") + " S";
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
        for (const t of hardUses(it, m)) add(t, it.id);
        if (it.kind === K.SAMPLER && !it.dim) {
            let played;
            try { played = new Set(R.unpackSampler(it.recs[0].payload).instruments.filter(Boolean).map(s => s.sample)); }
            catch (e) { continue; }
            for (const s of items) if (s.owner === it.id && played.has(s.idx)) add(s.id, it.id);
        }
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
    const v = { s, order };
    ownCache.set(it, v);
    return v;
}
/* id -> hash, for one panel's items */
function hashes(items) {
    const m = byIdMap(items), owned = ownedMap(items), out = new Map(), busy = new Set();
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
        if (it.kind === K.AUDIO) {
            const e = sampleEntry(it, m);
            if (e) s += "e" + hstr(R.packSampler({ name: "", instruments: [], samples: [e] }).subarray(11), 0);
        }
        if (it.kind === K.SAMPLER) for (const sid of (owned.get(id) || []).slice().sort((a, b) => m.get(a).idx - m.get(b).idx))
            s += "s" + m.get(sid).idx + "=" + h(sid);
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
    const t = { c: new Set(), p: new Set(), s: new Set() };
    for (const it of items) { const sp = space(it.kind); if (sp && it.num != null) t[sp].add(it.num); }
    return t;
}
function pickNum(set, want) {
    if (want != null && want < FIRST_FW && !set.has(want)) return want;
    for (let n = 0; n < FIRST_FW; n++) if (!set.has(n)) return n;
    return -1;
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
    for (const f of files) if (f.kind === K.AUDIO) audio += 16 + align4(f.len); else data += 16 + align4(f.len);
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
    const w = it.kind === K.SONG ? "SONG " : it.kind === K.AUDIO ? "SAMPLE " : it.kind === K.SAMPLER ? "SAMPLER " : "";
    return (w + (onR && it.kind !== K.AUDIO ? H2(it.num) + " " : "") + nameOf(it, items)).trim();
}
/* st { L: [items], R: [items], ... }; opts { target: a sampler's id on the FM-1 panel (loose samples join it), layout,
 * stamp: the source label for items landing on the TABLE } */
function transfer(st, from, ids, opts) {
    opts = opts || {};
    const to = from === "L" ? "R" : "L", src = st[from], dst = st[to];
    const sm = byIdMap(src), dm = byIdMap(dst);
    ids = ids.filter(id => sm.has(id));
    const dims = ids.filter(id => sm.get(id).dim);
    ids = ids.filter(id => !sm.get(id).dim);
    if (!ids.length) return { ok: false, msg: dims.length ? "A VERSION THIS PAGE CANNOT READ STAYS WHERE IT IS" : "SELECT SOMETHING TO MOVE FIRST" };
    const set = closure(src, ids);
    for (const id of [...set]) if (sm.get(id).dim) set.delete(id);    /* (what the page cannot read stays where it is) */
    const sh = hashes(src), dh = hashes(dst);
    const dByHash = new Map();
    for (const d of dst) if (!d.dim && !d.owner) dByHash.set(d.kind + "|" + dh.get(d.id), d);
    const map = new Map();                          /* src id -> dst id */
    const made = [], reused = [], replaced = [], leftOut = [];
    let loose = [], newSampler = null;
    const target = opts.target && dm.get(opts.target) && dm.get(opts.target).kind === K.SAMPLER ? dm.get(opts.target) : null;
    const order = [...set].map(id => sm.get(id)).sort((a, b) => (a.kind === K.AUDIO) - (b.kind === K.AUDIO));
    /* equal items within the move are one item there (two disks' same song); one system item of a kind */
    const madeByHash = new Map(), canon = new Map(), sysMade = new Map(), deferred = [];
    for (const it of order) {
        const hh = sh.get(it.id);
        if (it.kind === K.AUDIO) {
            if (it.owner && canon.has(it.owner)) { deferred.push(it); continue; }
            if (it.owner && set.has(it.owner)) {
                const host = map.get(it.owner);
                if (reused.includes(it.owner)) {
                    const same = dst.find(d => d.owner === host && dh.get(d.id) === hh);
                    if (same) { map.set(it.id, same.id); continue; }
                }
                map.set(it.id, newId()); made.push(it);
                continue;
            }
            if (to === "R") {
                const hostId = target ? target.id : null;
                const same = hostId ? dst.find(d => d.owner === hostId && dh.get(d.id) === hh) : null;
                if (same) { map.set(it.id, same.id); reused.push(it.id); continue; }
                map.set(it.id, newId()); made.push(it); loose.push(it);
            } else {
                const looseHash = hashOfLoose(it, sm);
                const same = dst.find(d => d.kind === K.AUDIO && !d.owner && hashes([d]).get(d.id) === looseHash);
                if (same) { map.set(it.id, same.id); reused.push(it.id); continue; }
                map.set(it.id, newId()); made.push(it);
            }
            continue;
        }
        if (to === "R" && systemOf(it)) {
            if (sysMade.has(it.kind)) { leftOut.push(it); continue; }     /* (the FM-1 keeps one of each: the first moved) */
            sysMade.set(it.kind, it.id);
            const ex = dst.find(d => d.kind === it.kind);
            if (ex && dh.get(ex.id) === hh) { map.set(it.id, ex.id); reused.push(it.id); continue; }
            map.set(it.id, newId()); made.push(it);
            if (ex) replaced.push(ex);
            continue;
        }
        const same = dByHash.get(it.kind + "|" + hh);
        if (same) { map.set(it.id, same.id); reused.push(it.id); continue; }
        const twin = madeByHash.get(it.kind + "|" + hh);
        if (twin) { map.set(it.id, map.get(twin.id)); canon.set(it.id, twin.id); continue; }
        map.set(it.id, newId()); made.push(it);
        madeByHash.set(it.kind + "|" + hh, it);
    }
    /* a twin sampler's samples are its twin's (the same content, sample for sample) */
    for (const it of deferred) {
        const co = canon.get(it.owner), tw = order.find(x => x.owner === co && x.idx === it.idx);
        if (tw && map.has(tw.id)) map.set(it.id, map.get(tw.id));
    }
    for (const it of leftOut) set.delete(it.id);
    /* numbers on the FM-1 panel */
    const numOf = new Map(), renum = [];
    if (to === "R") {
        const tk = taken(dst.filter(d => !replaced.includes(d)));
        for (const it of made) {
            const sp = space(it.kind);
            if (!sp || systemOf(it)) continue;
            let n;
            if (it.builtin && it.orig >= FIRST_FW) {
                n = tk[sp].has(it.orig) ? pickNum(tk[sp], null) : it.orig;
            } else {
                const want = it.side === "R" ? it.num : it.orig;
                n = pickNum(tk[sp], want != null && want < FIRST_FW ? want : null);
                if (want != null && n !== want && n >= 0) renum.push([it, n]);
            }
            if (n < 0) return { ok: false, msg: "THE FM-1 HAS NO FREE " + (sp === "c" ? "CARTRIDGE" : sp === "p" ? "PATTERN" : "SONG") + " NUMBER LEFT: DELETE ONE FIRST" };
            tk[sp].add(n); numOf.set(it.id, n);
        }
        if (loose.length && !target) {
            const n = pickNum(tk.c, null);
            if (n < 0) return { ok: false, msg: "THE FM-1 HAS NO FREE CARTRIDGE NUMBER LEFT: DELETE ONE FIRST" };
            tk.c.add(n);
            newSampler = { id: newId(), side: "R", kind: K.SAMPLER, num: n, orig: null, link: {},
                recs: [{ sub: 0, ver: R.VERSION[K.SAMPLER], payload: samplerBytes(looseName(loose, sm), [], []) }] };
        }
    }
    /* the copies; on the FM-1 panel each reference names its target's number there */
    const stamp = opts.stamp || "FM-1";
    const numByDst = new Map();
    for (const d of dst) if (d.num != null) numByDst.set(d.id, d.num);
    for (const it of made) if (numOf.has(it.id)) numByDst.set(map.get(it.id), numOf.get(it.id));
    const atDst = { c: new Map(), p: new Map(), s: new Map() };
    for (const d of dst) { const sp = space(d.kind); if (sp && d.num != null && !replaced.includes(d)) atDst[sp].set(d.num, d.id); }
    /* (an item the other side holds already, by content: where a soft reference left behind still finds its own) */
    const there = id => { const s = sm.get(id); const d = s && dByHash.get(s.kind + "|" + sh.get(id)); return d ? d.id : null; };
    const ctx = { map, numOf, numByDst, atDst, sm, stamp, there };
    const copies = made.map(it => copyItem(it, to, ctx));
    const cm = byIdMap(copies);
    let next = dst.filter(d => !replaced.includes(d) && !(replaced.length && replaced.some(r => d.owner === r.id)));
    if (to === "R") {
        for (const c of copies) if (c.kind === K.AUDIO && c.owner && cm.has(c.owner)) c.num = cm.get(c.owner).num;
        /* loose samples into the sampler dropped on, or a new one */
        if (loose.length) {
            const host = target || newSampler;
            const s = R.unpackSampler(host.recs[0].payload);
            const raw = host.recs[0].payload;
            const insts = s.instruments.map((_, i) => raw.subarray(11 + i * R.SAMPLER_SLOT, 11 + (i + 1) * R.SAMPLER_SLOT));
            const at = 11 + s.instruments.length * R.SAMPLER_SLOT;
            const entries = s.samples.map((e, i) => e ? raw.slice(at + i * R.SAMPLE_ENTRY, at + (i + 1) * R.SAMPLE_ENTRY) : null);
            for (const it of loose) {
                let idx = entries.findIndex(e => !e);
                if (idx < 0) idx = entries.length;
                if (idx >= 64) return { ok: false, msg: "THAT SAMPLER IS FULL: 64 SAMPLES AT MOST" };
                const c = cm.get(map.get(it.id));
                entries[idx] = it.entry ? it.entry.slice() : packedEntry(sampleEntry(it, sm));
                Object.assign(c, { owner: host.id, idx, num: host.num });
                delete c.entry;
            }
            const h2 = Object.assign({}, host, { recs: [{ sub: 0, ver: host.recs[0].ver, payload: samplerBytes(s.name, insts, entries) }] });
            if (target) next = next.map(d => d.id === host.id ? h2 : d);
            else next.push(h2);
        }
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
    if (reused.length) parts.push(reused.length + (reused.length > 1 ? " ITEMS" : " ITEM") + " ALREADY THERE");
    if (renum.length) parts.push(renum.slice(0, 2).map(([it, n]) => nameOf(it, src) + " IS NOW " + H2(n)).join(", ") +
        (renum.length > 2 ? " AND " + (renum.length - 2) + " MORE RENUMBERED" : ""));
    if (target && loose.length) parts.push("INTO SAMPLER " + H2(target.num) + " " + nameOf(target, dst));
    if (newSampler) parts.push("INTO A NEW SAMPLER " + H2(newSampler.num));
    if (replaced.length) parts.push(replaced.map(r => nameOf(r, dst)).join(", ") + " REPLACED");
    if (leftOut.length) parts.push("THE FM-1 KEEPS ONE OF EACH: " + leftOut.map(r => nameOf(r, src) + " FROM " + (r.src || "THE TABLE")).join(", ") + " NOT MOVED");
    if (!copies.length && !loose.length) parts.push("NOTHING NEW TO COPY");
    return { ok: true, st: out, msg: parts.join(" · "), ids: moved, replaced: replaced.map(r => r.id) };
}
function looseName(loose, sm) {
    const n = loose.length === 1 ? nameOf(loose[0], sm) : "NEW SAMPLES";
    return R.cleanName(n, 12) || "NEW SAMPLES";
}
function packedEntry(e) { return R.packSampler({ name: "", instruments: [], samples: [e] }).slice(11, 27); }
function hashOfLoose(it, sm) {                      /* a sample's hash as it would be loose (its entry its own) */
    const e = sampleEntry(it, sm);
    const loose = { id: "x", kind: K.AUDIO, side: "L", link: {}, recs: it.recs, owner: null, entry: e ? packedEntry(e) : it.entry };
    return hashes([loose]).get("x");
}
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
        if (it.kind === K.AUDIO) { c.owner = it.owner ? map.get(it.owner) || null : null; c.idx = it.idx; }
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
        if (it.kind === K.AUDIO) {
            if (it.owner && map.has(it.owner)) { c.owner = map.get(it.owner); c.idx = it.idx; }
            else { const e = sampleEntry(it, sm); c.owner = null; c.entry = e ? packedEntry(e) : it.entry; c.name = c.name || (e && e.name) || "SMP " + H2(it.idx); }
        }
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
        for (const u of us.get(id) || []) if (!sel.has(u)) out.push([u, id]);
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
            if (!it) continue;
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
        if (i.kind === K.SAMPLER) {
            const lost = items.filter(s => s.owner === i.id && gone.has(s.id));
            if (lost.length) {
                const b = i.recs[0].payload, s = R.unpackSampler(b);
                const at = 11 + s.instruments.length * R.SAMPLER_SLOT;
                const insts = s.instruments.map((_, k) => b.subarray(11 + k * R.SAMPLER_SLOT, 11 + (k + 1) * R.SAMPLER_SLOT));
                const entries = s.samples.map((e, k) => e && !lost.some(x => x.idx === k) ? b.slice(at + k * R.SAMPLE_ENTRY, at + (k + 1) * R.SAMPLE_ENTRY) : null);
                while (entries.length && !entries[entries.length - 1]) entries.pop();
                j = Object.assign({}, i, { recs: [{ sub: 0, ver: i.recs[0].ver, payload: samplerBytes(s.name, insts, entries) }] });
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
    return Object.assign({}, it, { link });
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
    deleteAll, pending, History, replaceItem, linkChanged, pruned, numberFor, sampleEntry, samplerBytes, packedEntry, patternPlace, byIdMap, ownedMap,
    numSpace: space, isCart, H2 };
})(globalThis.ED = globalThis.ED || {});

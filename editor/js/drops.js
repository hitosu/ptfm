/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Drops and conversions (specs/editor.md): files dropped on the TABLE, or picked with + ADD FILES, become its items, all
 * of each file: disks as they are, MOD / S3M / XM through IBXM's loaders, MIDI files, DX7 .syx, sounds (a sample at the
 * lowest rate that loses nothing heard, 8 bits, the file's own audio kept beside it). The status line says what each
 * made and what could not be kept.
 *   ED.drops.add(files) (the page); ED.drops.convert(name, bytes) -> Promise<{ items, what, warnings }> (no DOM) */
(function (ED) {
"use strict";
const M = ED.model, R = ED.rec, K = R.KIND, C = () => ED.conv;
const ext = n => (String(n).match(/\.([a-z0-9]+)$/i) || [, ""])[1].toLowerCase();
const magic = (b, s) => s.split("").every((c, i) => b[i] === c.charCodeAt(0));
const SOUND = ["wav", "flac", "mp3", "ogg", "oga", "opus", "m4a", "aac", "aif", "aiff", "webm"];

async function convert(name, bytes) {
    const src = String(name).toUpperCase();
    const e = ext(name);
    if (magic(bytes, "PTFMDISK") || magic(bytes, "PTFMDATA")) {
        const S = globalThis.PTFM_seed, d = S.parseData(bytes);
        const good = d.files.filter(f => S.crc32(f.payload) === f.crc);      /* (a damaged record left out: said below) */
        const r = M.fromRecords(good, { side: "L", src });
        const kept = r.items.filter(i => !i.dim), dims = r.items.length - kept.length;
        const w = d.problems.slice();
        if (dims) w.push(dims + " OF A VERSION THIS PAGE CANNOT READ LEFT OUT");
        if (r.skipped.length) w.push(r.skipped.length + " RECORDS THE FM-1 CANNOT HOLD LEFT OUT");
        return { items: kept, what: count(kept), warnings: w };
    }
    if (magic(bytes, "MThd")) {
        const m = C().readMidi(bytes), o = C().midiDefaults(m);
        return fromRecs(C().midiToSong(m, o, src.replace(/\.[^.]*$/, "")), src);
    }
    if (e === "syx" || (bytes[0] === 0xF0 && bytes[1] === 0x43)) return fromRecs(C().syx(bytes, src), src);
    if (e === "mod" || e === "s3m" || e === "xm" || magic(bytes, "Extended Module:") || (bytes.length > 48 && magic(bytes.subarray(44), "SCRM")))
        return fromRecs(C().module(bytes, src.replace(/\.[^.]*$/, "")), src);
    if (e === "it" || magic(bytes, "IMPM")) throw new Error("IT MODULES ARE NOT READ (MOD, S3M AND XM ARE)");
    if (SOUND.includes(e) || magic(bytes, "RIFF") || magic(bytes, "fLaC") || magic(bytes, "OggS") || magic(bytes, "ID3")) return sound(name, bytes, src);
    throw new Error("NOT A FILE THIS PAGE READS");
}
function fromRecs(r, src) {
    const t = M.fromRecords(r.recs, { side: "L", src });
    return { items: t.items, what: count(t.items), warnings: (r.warnings || []).map(w => String(w).toUpperCase()) };
}
function count(items) {
    const cnt = {};
    for (const it of items) { const k = M.oneOf(it); cnt[k] = (cnt[k] || 0) + 1; }
    return Object.entries(cnt).map(([k, n]) => n + " " + k + (n > 1 ? "S" : "")).join(", ") || "NOTHING";
}
/* a sound: a loose sample on the TABLE at the rate proposed, 8 bits, mixed to mono; the file's own audio kept */
async function sound(name, bytes, src) {
    const snd = await C().decodeSound(name, bytes);
    const rate = C().suggestRate(snd);
    const base = String(name).replace(/\.[^.]*$/, "");
    const s = C().soundToSample(snd, { start: 0, end: snd.length, chan: "mix", rate, bits: 8, name: base });
    const entry = M.packedEntry(s.entry);
    const it = { id: M.newId(), side: "L", kind: K.AUDIO, num: null, orig: null, link: {}, owner: null, entry,
        recs: [{ sub: 0, ver: R.VERSION[K.AUDIO], payload: s.audio }], name: s.entry.name, src,
        sound: { rate: snd.rate, ch: snd.ch, bits: snd.bits, cut: { start: 0, end: snd.length, chan: "mix", rate, bits: 8 } } };
    return { items: [it], what: "A SAMPLE, " + (rate / 1000).toFixed(1) + " KHZ, 8 BITS", warnings: [] };
}

/* the page: each file in turn, all of them one step to UNDO */
async function add(files) {
    const U = ED.ui, app = ED.app;
    let st = app.st;
    const said = [], bad = [];
    let n = 0, all = [];
    for (const f of files) {
        n++;
        U.busy("ADDING " + f.name.toUpperCase(), n / files.length);
        try {
            const r = await convert(f.name, new Uint8Array(await f.arrayBuffer()));
            all = all.concat(r.items);
            said.push(f.name.toUpperCase() + ": " + r.what + (r.warnings.length ? " (" + r.warnings.slice(0, 3).join("; ") + (r.warnings.length > 3 ? "; …" : "") + ")" : ""));
        } catch (e) {
            bad.push(f.name.toUpperCase() + ": " + String(e && e.message || e).toUpperCase());
        }
    }
    if (all.length) {
        st = Object.assign({}, st, { L: st.L.concat(all) });
        app.commit(st, "ADDED " + said.join(" · ") + (bad.length ? " · NOT READ: " + bad.join(" · ") : ""), { ok: !bad.length,
            select: { side: "L", ids: all.filter(i => !i.owner).map(i => i.id) } });
    } else U.status(bad.length ? "NOT READ: " + bad.join(" · ") : "NOTHING ADDED", { err: !!bad.length });
}

ED.drops = { add, convert };
})(globalThis.ED = globalThis.ED || {});

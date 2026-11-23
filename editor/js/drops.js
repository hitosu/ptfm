/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Drops and conversions (specs/editor.md): files dropped on the TABLE, or picked with + ADD FILES, become its items, all
 * of each file: disks as they are, MOD / S3M / XM through IBXM's loaders, MIDI files, DX7 .syx, Juno-106 banks (.syx,
 * .106, .pat), sounds (a sample at the lowest rate that loses nothing heard, 8 bits, the file's own audio kept beside it;
 * one that looks like a Juno's tape asked about: CARTRIDGE or SAMPLE). The status line says what each made and what
 * could not be kept; tapes that did not read as cartridges are listed in a dialog once the rest are in.
 *   ED.drops.add(files) (the page); ED.drops.convert(name, bytes, { ask }) -> Promise<{ items, what, warnings }> (no
 *   DOM; ask(name, machine) -> Promise<"CARTRIDGE" | "SAMPLE"> for a tape, SAMPLE when not given; a tape answered
 *   CARTRIDGE that does not read throws an error marked `tape`) */
(function (ED) {
"use strict";
const M = ED.model, C = () => ED.conv;
const ext = n => (String(n).match(/\.([a-z0-9]+)$/i) || [, ""])[1].toLowerCase();
const magic = (b, s) => s.split("").every((c, i) => b[i] === c.charCodeAt(0));
const SOUND = ["wav", "flac", "mp3", "ogg", "oga", "opus", "m4a", "aac", "aif", "aiff", "webm"];

async function convert(name, bytes, o) {
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
    /* (Juno-106 banks by their content, before the DX7's .syx: a Juno-106 .syx is no DX7 dump; a file a sound's or a
     * module's header names is neither, whatever bytes its data holds) */
    const module = magic(bytes, "Extended Module:") || (bytes.length > 48 && magic(bytes.subarray(44), "SCRM"));
    const soundish = magic(bytes, "RIFF") || magic(bytes, "fLaC") || magic(bytes, "OggS") || magic(bytes, "ID3");
    const named = SOUND.includes(e) || ["mod", "s3m", "xm", "it"].includes(e);   /* (a sound's or a module's extension) */
    const jk = module || soundish || named ? null : C().juno.kind(bytes);
    if (jk === "106" || jk === "pat") return fromRecs(C().juno.recsOf(bytes, src, { kind: jk }), src);
    if (jk === "syx" || (!module && !soundish && (e === "syx" || (bytes[0] === 0xF0 && bytes[1] === 0x43)))) return syx(bytes, src, jk === "syx");
    if (e === "mod" || e === "s3m" || e === "xm" || module) return fromRecs(C().module(bytes, src.replace(/\.[^.]*$/, "")), src);
    if (e === "it" || magic(bytes, "IMPM")) throw new Error("IT MODULES ARE NOT READ (MOD, S3M AND XM ARE)");
    if (SOUND.includes(e) || soundish) return sound(name, bytes, src, o);
    throw new Error("NOT A FILE THIS PAGE READS");
}
/* a .syx: DX7 dumps, Juno-106 patch messages, or both (a cartridge of each) */
function syx(bytes, src, juno) {
    let dx = null;
    try { if (!juno || bytes.some((v, i) => v === 0xF0 && bytes[i + 1] === 0x43)) dx = C().syx(bytes, src); }
    catch (err) { /* (no DX7 dump in it) */ }
    if (!juno) {
        if (dx) return fromRecs(dx, src);
        throw new Error("NO DX7 VOICE DUMP (A 32-VOICE BULK DUMP OR A SINGLE VOICE) OR JUNO-106 PATCH IN IT");
    }
    const j = C().juno.recsOf(bytes, src, { dx7: !!dx, kind: "syx" });   /* (the DX7's own messages not counted as skipped) */
    return fromRecs({ recs: (dx ? dx.recs : []).concat(j.recs), warnings: j.warnings }, src);
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
/* a sound: a loose sample on the TABLE at the rate proposed, 8 bits, mixed to mono; the file's own audio kept (one
 * longer than a sample can be: TOO LONG, its original alone, till a cut makes it fit). One that looks like a Juno's
 * tape (a leader and a dump's first bytes in its first 20 s) is asked about first: CARTRIDGE reads it as a Juno-106
 * cartridge, or nothing when it does not read */
async function sound(name, bytes, src, o) {
    const snd = await C().decodeSound(name, bytes);
    /* (looked for first, read whole only once CARTRIDGE is the answer) */
    const found = o && o.ask ? ED.junoTape.read(snd, { window: 20, detect: true }) : null;
    if (found && await o.ask(src, found.machine) === "CARTRIDGE") {
        const tape = ED.junoTape.read(snd, { window: 20 });
        if (!tape.ok) { const err = new Error("NOT READ: " + C().juno.tapeWhy(tape)); err.tape = true; throw err; }
        return fromRecs(C().juno.tapeRecs(tape, src.replace(/\.[^.]*$/, "")), src);
    }
    const rate = C().suggestRate(snd);
    const base = String(name).replace(/\.[^.]*$/, "");
    const it = ED.cutting.sampleOf(snd, { chan: "mix", rate, bits: 8 }, base, src, true);
    it.side = "L";
    const what = "A SAMPLE, " + (rate / 1000).toFixed(1) + " KHZ, 8 BITS" + (M.isLong(it) ? ", " + M.longText(it) : "");
    return { items: [it], what, warnings: [] };
}

/* a dialog of the page's own (the recorder, a question, another drop's) waited out before one of a drop opens: one
 * would wipe the other */
function dialogFree() {
    return new Promise(done => { const t = () => ED.ui.dialogOpen() ? setTimeout(t, 150) : done(); t(); });
}
/* a sound that looks like a Juno's tape: JUNO-106 TAPE? READ IT AS A CARTRIDGE, CARTRIDGE or SAMPLE (Esc: SAMPLE), FOR
 * ALL beside them when several sounds were dropped (the rest of this drop's tapes answered the same) */
function asker(several) {
    let all = null;
    return async (name, machine) => {
        if (all) return all;
        await dialogFree();
        return new Promise(done => {
            const U = ED.ui, h = U.h;
            let forAll = false;
            const say = machine === 106 ? "JUNO-106 TAPE? READ IT AS A CARTRIDGE" : "JUNO-60 TAPE? READ IT AS A CARTRIDGE, CONVERTED TO JUNO-106";
            const answer = a => { if (forAll) all = a; done(a); };
            U.dialog({ title: name, body: h("div", null, h("p", null, say),
                    several ? h("label", { class: "sw" }, h("input", { type: "checkbox", onchange: e => { forAll = e.target.checked; } }), "FOR ALL") : null),
                buttons: [{ label: "CARTRIDGE", cls: "primary", onClick: () => answer("CARTRIDGE") }, { label: "SAMPLE", onClick: () => answer("SAMPLE") }],
                onCancel: () => answer("SAMPLE") });
        });
    };
}

/* the page: drops one after another (a drop's questions wait for the one before), each file in turn, all of a drop's
 * one step to UNDO; tapes that did not read listed in a dialog after */
let queue = Promise.resolve();
function add(files) {
    queue = queue.then(() => addNow(files)).catch(e => ED.ui.status("NOT READ: " + String(e && e.message || e).toUpperCase(), { err: true }));
    return queue;
}
async function addNow(files) {
    const U = ED.ui, app = ED.app, h = U.h;
    const said = [], bad = [], tapes = [];
    const ask = asker(files.filter(f => SOUND.includes(ext(f.name))).length > 1);
    let n = 0, all = [];
    for (const f of files) {
        n++;
        U.busy("ADDING " + f.name.toUpperCase(), n / files.length);
        try {
            const r = await convert(f.name, new Uint8Array(await f.arrayBuffer()), { ask });
            all = all.concat(r.items);
            said.push(f.name.toUpperCase() + ": " + r.what + (r.warnings.length ? " (" + r.warnings.slice(0, 3).join("; ") + (r.warnings.length > 3 ? "; …" : "") + ")" : ""));
        } catch (e) {
            (e && e.tape ? tapes : bad).push(f.name.toUpperCase() + ": " + String(e && e.message || e).toUpperCase());
        }
    }
    if (all.length) {
        const st = Object.assign({}, app.st, { L: app.st.L.concat(all) });
        app.commit(st, "ADDED " + said.join(" · ") + (bad.length ? " · NOT READ: " + bad.join(" · ") : ""), { ok: !bad.length,
            select: { side: "L", ids: all.filter(i => !i.owner).map(i => i.id) } });
    } else U.status(bad.length ? "NOT READ: " + bad.join(" · ") : "NOTHING ADDED", { err: !!bad.length });
    if (tapes.length) {
        await dialogFree();
        await new Promise(done => U.dialog({ title: tapes.length > 1 ? "TAPES NOT READ" : "A TAPE NOT READ",
            body: h("ul", null, tapes.map(t => h("li", { class: "rm" }, t))), buttons: [{ label: "OK", cls: "primary", onClick: done }], onCancel: done }));
    }
}

ED.drops = { add, convert };
})(globalThis.ED = globalThis.ED || {});

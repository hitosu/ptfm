/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The preview (specs/editor.md, The preview): the firmware's own sound code in an AudioWorklet at 44.1 kHz, fed with a
 * flash image of what the item needs laid out as the FM-1's (a TABLE item numbered as a move to an empty FM-1 would
 * number it). What plays, then a second of silence, then again, until STOP: a song from its start to its end, a pattern
 * once through at the tempo of a song that plays it, a sample once at its own rate, an instrument a melodic arpeggio
 * (C-4 E-4 G-4 C-5 G-4 E-4 C-4 in eighths at 120 BPM, the last held a second), a cartridge its instruments one after
 * another, a FUSION slot its pattern, LOOPS all its loops together. The worklet keeps the time: the repeat, the
 * arpeggios' notes and the end of a song or a pattern are its own, so they keep time whatever the page does.
 *   ED.player.canPlay(it), isPlaying(it), toggle(it, o), play(it, o), stop(), update(it) (a tab's draft heard at once),
 *   key(on, note, cart, slot) (a tab's keyboard), plan(it, st, o) -> { recs, play, label } (no DOM: the tests) */
(function (ED) {
"use strict";
const M = ED.model, R = ED.rec, K = R.KIND;

/* ---- the worklet, run in its own scope (a page from the file system loads a worklet only from a data: URL) ---- */
function worklet() {
    const RATE = 44100, Q = 128, GAP = RATE;
    class Preview extends AudioWorkletProcessor {
        constructor() {
            super();
            this.ex = null; this.gain = 0.7 / 8388608; this.step = RATE / sampleRate;
            this.ring = new Float32Array(4096 * 2); this.written = 0; this.pos = 0; this.sent = -1;
            this.job = null;
            this.port.onmessage = e => this.msg(e.data);
        }
        async msg(m) {
            try {
                if (m.t === "load") {
                    const r = await WebAssembly.instantiate(m.wasm, { env: { web_clock_us: () => currentTime * 1e6 } });
                    const ex = r.instance.exports;
                    ex.web_flash_reset();
                    ex.web_master(1023);
                    ex.web_init();
                    ex.ed_preview_room(1);              /* (the preview's room: the flash from the slot's start) */
                    this.ex = ex;
                    this.port.postMessage({ t: "ready" });
                } else if (!this.ex) return;
                else if (m.t === "flash") {
                    this.job = null;
                    const f = new Uint8Array(this.ex.memory.buffer, this.ex.web_flash(), 1 << 20);
                    f.fill(0xFF, 0x004000, 0x0FF000);
                    for (const b of m.blocks) f.set(b.image, b.block << 12);
                    this.ex.ed_remount();
                } else if (m.t === "edits") {          /* changes through the edit memory: what plays goes on */
                    const ex = this.ex;
                    let ok = true;
                    for (const p of m.puts) {
                        new Uint8Array(ex.memory.buffer, ex.ed_edit_buf(), p.payload.length).set(p.payload);
                        if (ex.ed_edit_put(p.kind, p.num, p.sub, p.ver, p.payload.length) !== 0) { ok = false; break; }
                    }
                    if (ok) for (const d of m.dels) ex.ed_edit_del(d.kind, d.num, d.sub);
                    if (!ok) this.port.postMessage({ t: "editsFailed" });
                    else if (m.how === "cells") { ex.ed_cells_changed(); for (const s of m.songs) ex.ed_song_changed(s); }
                    else ex.ed_store_changed();
                } else if (m.t === "play") { this.ex.ed_all_off(); this.job = Object.assign({ phase: "run", t0: this.written, evi: 0, last: -1 }, m.job); this.begin(); }
                else if (m.t === "stop") { this.job = null; this.ex.ed_stop(); this.ex.ed_keys_off(); this.ex.ed_all_off(); }
                else if (m.t === "jump") this.jump(m);
                else if (m.t === "call") this.ex[m.fn](...(m.args || []));
                else if (m.t === "gain") this.gain = m.v / 8388608;
            } catch (err) { this.port.postMessage({ t: "fault", text: String(err) }); }
        }
        begin() {
            const j = this.job, ex = this.ex;
            j.phase = "run"; j.t0 = this.written; j.evi = j.from || 0; j.last = -1; j.lastRow = -1; j.lastPos = -1; j.started = false; j.clean = false;
            if (j.kind === "song") ex.ed_play_song(j.song, j.pos || 0, 0);
            else if (j.kind === "pattern") ex.ed_play_pattern(j.song, j.pat, 0);
            else if (j.kind === "loops") ex.ed_play_loops();
            if (j.kind === "seq" && j.from) j.t0 = this.written - Math.round(j.events[j.from][0] * RATE);
            j.pos = 0;
        }
        jump(m) {
            const j = this.job, ex = this.ex;
            if (!j) return;
            if (j.kind === "song") { ex.ed_all_off(); ex.ed_play_song(j.song, m.pos, 0); j.phase = "run"; j.lastPos = m.pos; }
            else if (j.kind === "pattern") { ex.ed_play_pattern(j.song, j.pat, m.row); j.phase = "run"; j.lastRow = m.row; }
            else if (j.kind === "seq") {
                ex.ed_keys_off();
                let i = 0;
                while (i < j.events.length && j.events[i][0] < m.sec) i++;
                j.evi = i; j.phase = "run"; j.t0 = this.written - Math.round(m.sec * RATE);
            }
        }
        tick() {
            const j = this.job, ex = this.ex;
            if (!j) return;
            if (j.phase === "gap") { if (this.written >= j.gapEnd) this.begin(); return; }
            const el = (this.written - j.t0) / RATE;
            if (j.kind === "seq") {
                while (j.evi < j.events.length && j.events[j.evi][0] <= el) { const e = j.events[j.evi++]; ex[e[1]](...e[2]); }
                if (el >= j.length) this.rest();
                return;
            }
            if (el < 0.05) return;                  /* (the player takes the request at its next half: what it says before
                                                     * is what played before) */
            const st = new Uint8Array(ex.memory.buffer, ex.ed_state(), 16);
            const playing = st[1], ended = st[2], pos = st[4], row = st[6];
            if (playing) j.started = true;
            if (!ended) j.clean = true;             /* (an end said before this play began is not its own) */
            let end = j.started && j.clean && (ended || !playing);
            /* (a song looping from its last position: back to its LOOP FROM, or its last pattern again) */
            if (j.kind === "song" && j.lastPos === j.npos - 1 && (pos < j.lastPos || (pos === j.lastPos && j.lastRow > 0 && row < j.lastRow))) end = true;
            if ((j.kind === "pattern" || j.kind === "loops") && j.lastRow > 0 && row < j.lastRow) end = true;
            j.lastPos = pos; j.lastRow = row;
            if (end || el > 600) this.rest();
        }
        rest() { const j = this.job; this.ex.ed_stop(); this.ex.ed_keys_off(); j.phase = "gap"; j.gapEnd = this.written + GAP; }
        pull() {
            const ex = this.ex;
            this.tick();
            ex.web_render(Q);
            const s = new Int32Array(ex.memory.buffer, ex.web_out(), Q * 2);
            for (let k = 0; k < Q; k++) { const i = ((this.written + k) % 4096) * 2; this.ring[i] = s[2 * k]; this.ring[i + 1] = s[2 * k + 1]; }
            this.written += Q;
        }
        process(inputs, outputs) {
            const out = outputs[0], L = out[0], R2 = out[1] || out[0];
            if (!this.ex) return true;
            try {
                const n = L.length, g = this.gain, ring = this.ring;
                /* a limiter after VOL (loud enough turned up never clips): at once down to 0.95, back in ~0.2 s */
                const rel = this.rel || (this.rel = 1 - Math.exp(-1 / (0.2 * sampleRate)));
                let gr = this.gr || 1;
                for (let k = 0; k < n; k++) {
                    const i = Math.floor(this.pos), f = this.pos - i;
                    while (this.written < i + 2) this.pull();
                    const a = (i % 4096) * 2, b = ((i + 1) % 4096) * 2;
                    const l = (ring[a] + (ring[b] - ring[a]) * f) * g, r = (ring[a + 1] + (ring[b + 1] - ring[a + 1]) * f) * g;
                    const pk = Math.max(Math.abs(l), Math.abs(r));
                    if (pk * gr > 0.95) gr = 0.95 / pk; else gr += (1 - gr) * rel;
                    L[k] = l * gr;
                    R2[k] = r * gr;
                    this.pos += this.step;
                }
                this.gr = gr;
                if (currentTime - this.sent >= 1 / 30) {
                    this.sent = currentTime;
                    const st = new Uint8Array(this.ex.memory.buffer, this.ex.ed_state(), 96).slice();
                    const j = this.job;
                    this.port.postMessage({ t: "state", st, job: j ? { phase: j.phase, el: (this.written - j.t0) / RATE, evi: j.evi } : null });
                }
            } catch (err) { this.ex = null; this.port.postMessage({ t: "fault", text: String(err) }); }
            return true;
        }
    }
    registerProcessor("ptfm-editor-preview", Preview);
}
const SOURCE = "(" + worklet.toString() + ")();\n";

function parseState(st) {
    const dv = new DataView(st.buffer, st.byteOffset, st.byteLength);
    return { what: st[0], playing: !!st[1], ended: !!st[2], song: st[3], pos: st[4], pat: st[5], row: st[6], rows: st[7] + 1,
        tracks: st[8], voices: st[9], bpm: dv.getUint16(10, true) / 10 };
}

/* ---- what an item plays: its flash image and the job ---- */
const ARP = [60, 64, 67, 72, 67, 64, 60], EIGHTH = 0.25, ARP_LEN = 6 * EIGHTH + 1.0, BETWEEN = 0.4;
const SYS = [K.SETTINGS, K.MIDIMAP, K.DJ];
function slotsOf(it) {
    const b = it.recs[0].payload;
    if (it.kind === K.SAMPLER) return R.unpackSampler(b).instruments.map((s, i) => s ? i : -1).filter(i => i >= 0);
    return R.unpackCart(it.kind, b).slots.map((s, i) => s ? i : -1).filter(i => i >= 0);
}
function canPlay(it) {
    if (!it || it.dim || it.isDefault || SYS.includes(it.kind)) return false;
    if (it.kind === K.SONG) return it.recs[0].payload[20] >= 0 && !(it.side === "R" && it.num === M.FE);
    if (it.kind === K.LOOPS) return it.recs[0].payload[5] > 0;
    if (M.isCart(it.kind)) { try { return slotsOf(it).length > 0; } catch (e) { return false; } }
    return it.kind === K.PATTERN || it.kind === K.AUDIO;
}
/* the items it needs, numbered as on an FM-1: an FM-1 panel's as they are, a TABLE's as a move to an empty one whose
 * room is the preview's (~1 MB, ED.flash.PREVIEW), so one bigger than an FM-1 holds plays too */
function imageItems(st, side, ids) {
    if (side === "R") {
        const set = M.closure(st.R, ids), m = M.byIdMap(st.R);
        return { items: [...set].map(id => m.get(id)), map: id => id };
    }
    const r = M.transfer({ L: st.L, R: [], base: [], fm1: {} }, "L", ids, { layout: { regions: ED.flash.PREVIEW, reserve: 0 } });
    if (!r.ok) throw new Error(r.msg);
    const back = new Map();
    ids.forEach((id, i) => back.set(id, r.ids[i]));
    return { items: r.st.R, map: id => back.get(id) };
}
/* an extra instrument at the end of a sampler, playing one of its samples at its own rate (BASE NOTE C-4) */
function withSampleInst(sampler, idx, entry) {
    const b = sampler.recs[0].payload, s = R.unpackSampler(b);
    const insts = s.instruments.map((_, i) => b.subarray(11 + i * R.SAMPLER_SLOT, 11 + (i + 1) * R.SAMPLER_SLOT));
    const at = 11 + s.instruments.length * R.SAMPLER_SLOT;
    const entries = s.samples.map((e, i) => e ? b.slice(at + i * R.SAMPLE_ENTRY, at + (i + 1) * R.SAMPLE_ENTRY) : null);
    while (entries.length <= idx) entries.push(null);
    entries[idx] = M.packedEntry(entry);            /* (the entry the sample's audio goes with: a draft's own) */
    const slot = Object.assign(ED.defs.newSlot(K.SAMPLER, "PREVIEW"), { sample: idx, start: 0, end: entry.length, loop_start: 0,
        loop_end: entry.length, loop: 0, base_note: 60, tune: 128, volume: 127, level: 0 });
    insts.push(R.packSamplerInst(slot));
    return { item: Object.assign({}, sampler, { recs: [{ sub: 0, ver: sampler.recs[0].ver, payload: M.samplerBytes(s.name, insts, entries) }] }), slot: insts.length - 1 };
}
function arpEvents(t, cart, slot, out) {
    ARP.forEach((n, i) => {
        out.push([t + i * EIGHTH, "ed_key_on", [n, cart, slot, 0x64]]);
        out.push([t + (i === ARP.length - 1 ? ARP_LEN : i * EIGHTH + EIGHTH * 0.9), "ed_key_off", [n]]);
    });
    return t + ARP_LEN;
}
function fusionRows(st, side, it, slot) {
    const t = it.link[M.lk("p", R.unpackCart(K.FUSION, it.recs[0].payload).slots[slot].pattern)];
    const p = t && st[side].find(x => x.id === t);
    return p ? p.recs[0].payload[1] + 1 : 16;
}
/* o: { slot } one instrument of a cartridge */
function plan(it, st, o) {
    o = o || {};
    const side = it.side;
    let recs = [], job = null, marks = null, slots = null;
    if (it.fw) {                                    /* a built-in instrument: the firmware's own, nothing to load; a tab's
                                                     * draft of it: its change, as an edit at its number */
        slots = o.slot != null ? [o.slot] : slotsOf(it);
        job = seqJob(st, side, it, it.num, slots);
        const own = M.builtins.find(b => b.kind === it.kind && b.num === it.num);
        const changed = own && own.payload !== it.recs[0].payload;
        return { recs: changed ? [{ kind: it.kind, num: it.num, sub: 0, ver: it.recs[0].ver, payload: it.recs[0].payload }] : [], job, slots };
    }
    if (it.kind === K.PATTERN) {
        const song = st[side].find(s => s.kind === K.SONG && Object.values(s.link).includes(it.id) && !(side === "R" && s.num === M.FE));
        const ids = song ? [it.id, song.id] : [it.id];
        const im = imageItems(st, side, ids), m = M.byIdMap(im.items);
        const p = m.get(im.map(it.id)), s = song ? m.get(im.map(song.id)) : null;
        recs = M.toRecords(im.items);
        job = { kind: "pattern", song: s ? s.num : 0xFF, pat: p.num, rows: p.recs[0].payload[1] + 1 };
    } else if (it.kind === K.SONG) {
        const im = imageItems(st, side, [it.id]), s = M.byIdMap(im.items).get(im.map(it.id));
        recs = M.toRecords(im.items);
        const npos = s.recs[0].payload[20] + 1;
        job = { kind: "song", song: s.num, npos, pos: o.pos || 0 };
        marks = npos;
    } else if (it.kind === K.LOOPS) {
        const im = imageItems(st, side, [it.id]);
        recs = M.toRecords(im.items);
        job = { kind: "loops" };
    } else if (it.kind === K.AUDIO) {
        /* a sample in a sampler: the sampler with its samples (an instrument added for it below); a loose one alone */
        const im = imageItems(st, side, [it.owner || it.id]), m = M.byIdMap(im.items);
        const s0 = it.owner ? im.items.find(x => x.owner === im.map(it.owner) && x.idx === it.idx) : m.get(im.map(it.id));
        if (!s0) throw new Error("its sampler is not here");
        /* (a tab's draft of it: its own audio and entry, in a copy: the panel's items stay as they are) */
        const s = it.owner ? Object.assign({}, s0, { recs: it.recs, entry: it.entry || null }) : s0;
        im.items = im.items.map(x => x === s0 ? s : x);
        const host = m.get(s.owner);
        const e = M.sampleEntry(s, m);
        const w = withSampleInst(host, s.idx, e);
        recs = M.toRecords(im.items.map(x => x.id === host.id ? w.item : x));
        const dur = e.rate ? e.length / e.rate : 1;
        job = { kind: "seq", events: [[0, "ed_key_on", [60, host.num, w.slot, 0x7F]], [dur, "ed_key_off", [60]]], length: dur + 0.05 };
    } else if (M.isCart(it.kind)) {
        const im = imageItems(st, side, [it.id]), c = M.byIdMap(im.items).get(im.map(it.id));
        recs = M.toRecords(im.items);
        slots = o.slot != null ? [o.slot] : slotsOf(it);
        job = seqJob(st, side, it, c.num, slots);
    }
    return { recs, job, slots, marks };
}
function seqJob(st, side, it, num, slots) {
    const events = [], starts = [];
    let t = 0;
    for (const s of slots) {
        starts.push(t);
        if (it.kind === K.FUSION) {
            const len = Math.min(8, fusionRows(st, side, it, s) * 0.125);
            events.push([t, "ed_key_on", [60, num, s, 0x64]], [t + len, "ed_key_off", [60]]);
            t += len + BETWEEN;
        } else t = arpEvents(t, num, s, events) + BETWEEN;
    }
    events.sort((a, b) => a[0] - b[0]);
    return { kind: "seq", events, length: t, starts };
}

/* ---- the worklet's node, and what plays now ---- */
const VOL_MIN = -24, VOL_MAX = 24;
const volText = db => (db > 0 ? "+" : "") + db + " DB";
class Engine {
    constructor() { this.ctx = null; this.node = null; this.ready = null; this.cur = null; this.editBytes = 0; this.listeners = []; this.vol = 0; }
    /* the footer's VOL in dB, -24 to +24 around the worklet's own level (0.7); its limiter keeps it from clipping */
    volume(db) { this.vol = db; this.post({ t: "gain", v: 0.7 * Math.pow(10, db / 20) }); }
    start() {
        if (this.ready) return this.ready;
        this.ready = (async () => {
            try { this.ctx = new AudioContext({ sampleRate: 44100, latencyHint: "interactive" }); }
            catch (_) { this.ctx = new AudioContext({ latencyHint: "interactive" }); }
            await this.ctx.audioWorklet.addModule("data:text/javascript;charset=utf-8," + encodeURIComponent(SOURCE));
            this.node = new AudioWorkletNode(this.ctx, "ptfm-editor-preview", { outputChannelCount: [2] });
            this.node.connect(this.ctx.destination);
            await new Promise((resolve, reject) => {
                this.node.port.onmessage = e => {
                    const m = e.data;
                    if (m.t === "ready") resolve();
                    else if (m.t === "fault") { reject(new Error(m.text)); for (const f of this.listeners) f(null, m.text); }
                    else if (m.t === "state") for (const f of this.listeners) f(parseState(m.st), m.job);
                    else if (m.t === "editsFailed") { this.cur = null; if (this.lastRecs) this.load(this.lastRecs); }
                };
                const w = Uint8Array.from(atob(PTFM_ED_WASM.wasm), c => c.charCodeAt(0)).buffer;
                this.node.port.postMessage({ t: "load", wasm: w }, [w]);
            });
            if (this.ctx.state !== "running") await this.ctx.resume();
            this.volume(this.vol);
            return this;
        })();
        return this.ready;
    }
    post(m) { if (this.node) this.node.port.postMessage(m); }
    /* the records it plays from: laid out and mounted the first time or after big changes (what plays stops); after
     * that what changed goes in through the edit memory and what plays goes on. Built-ins' changes always as edits. */
    load(recs) {
        this.lastRecs = recs;
        const key = r => r.kind + "." + r.num + "." + r.sub;
        const want = new Map();
        for (const r of recs) want.set(key(r), r);
        const isB = r => r.num >= 0xF0 && (M.isCart(r.kind) || r.kind === K.PATTERN || r.kind === K.SONG);
        const same = (c, r) => c && c.payload === r.payload;
        if (this.cur) {
            const puts = [], dels = [];
            for (const [k, r] of want) if (!same(this.cur.get(k), r)) puts.push(r);
            for (const [k, c] of this.cur) if (!want.has(k)) dels.push(c);
            if (!puts.length && !dels.length) return false;
            const bytes = puts.reduce((n, r) => n + r.payload.length + 16, 0);
            const big = puts.some(r => r.kind === K.AUDIO || r.payload.length > 8192) || dels.some(r => r.kind === K.AUDIO);
            if (!big && puts.length + dels.length <= 48 && this.editBytes + bytes < 40 * 1024) {
                const sameSize = r => { const c = this.cur.get(key(r)); return c && r.kind === K.PATTERN && r.payload[0] === c.payload[0] && r.payload[1] === c.payload[1]; };
                const cells = !dels.length && puts.every(r => sameSize(r) || r.kind === K.SONG);
                this.post({ t: "edits", how: cells ? "cells" : "store", songs: puts.filter(r => r.kind === K.SONG).map(r => r.num),
                    puts: puts.map(r => ({ kind: r.kind, num: r.num, sub: r.sub, ver: r.ver, payload: r.payload })),
                    dels: dels.map(r => ({ kind: r.kind, num: r.num, sub: r.sub })) });
                for (const r of puts) this.cur.set(key(r), r);
                for (const r of dels) this.cur.delete(key(r));
                this.editBytes += bytes;
                return false;
            }
        }
        const base = [...want.values()].filter(r => !isB(r)), edits = [...want.values()].filter(isB);
        this.post({ t: "flash", blocks: ED.flash.image(base.map(r => Object.assign({ crc: 0 }, r)), { preview: true }) });
        if (edits.length) this.post({ t: "edits", how: "store", songs: [], dels: [],
            puts: edits.map(r => ({ kind: r.kind, num: r.num, sub: r.sub, ver: r.ver, payload: r.payload })) });
        this.cur = want;
        this.editBytes = edits.reduce((n, r) => n + r.payload.length + 16, 0);
        return true;
    }
}

const eng = new Engine();
let now = null;                                     /* { it, side, job, label, slots, slotNames } */
function isPlaying(it) { return !!now && now.it.id === it.id && now.side === it.side; }
async function play(it, o) {
    o = o || {};
    const U = ED.ui, app = ED.app;
    if (!canPlay(it)) { U.status("NOTHING TO PLAY IN " + ED.files.label(it, app.st[it.side]), { err: true }); return; }
    let p;
    try { p = plan(it, o.st || app.st, o); } catch (e) { U.status("CANNOT PLAY IT: " + String(e.message || e).toUpperCase(), { err: true }); return; }
    try { await eng.start(); } catch (e) { U.status("THE SOUND DID NOT START: " + String(e.message || e).toUpperCase(), { err: true }); return; }
    eng.load(p.recs);
    if (keyImage && keyImage.it !== it) keyImage = null;   /* (another item's image: the keys load theirs again) */
    eng.post({ t: "play", job: p.job });
    now = { it, side: it.side, job: p.job, slots: p.slots, label: ED.files.label(it, app.st[it.side]) + " · " + (it.side === "R" ? "FM-1" : "TABLE") };
    paint();
    if (ED.files) ED.files.render();
}
function stop() {
    if (!now) return;
    eng.post({ t: "stop" });
    now = null;
    paint();
    if (ED.files) ED.files.render();
}
function toggle(it, o) { if (isPlaying(it)) stop(); else play(it, o); }
/* a tab's draft: what plays goes on with the change (the same item's records, changed) */
function update(it, st) {
    if (!now || now.it.id !== it.id) return;
    try {
        const p = plan(it, st, { slot: now.slots && now.slots.length === 1 ? now.slots[0] : undefined });
        /* mounted again (a big change: a sample's audio): what played starts again, as it now is */
        if (eng.load(p.recs)) { eng.post({ t: "play", job: p.job }); now.job = p.job; now.slots = p.slots; paint(); }
    } catch (e) { /* (kept as it was) */ }
}
/* a tab's keyboard: a note on an instrument, the item's image loaded first */
async function key(on, note, it, slot, st) {
    await eng.start();
    if (on) {
        if (!keyImage || keyImage.it !== it) {
            if (now) stop();
            const p = plan(it, st || ED.app.st, { slot });
            eng.load(p.recs);
            keyImage = { it, num: it.fw ? it.num : numIn(p, it) };
        }
        eng.post({ t: "call", fn: "ed_key_on", args: [note, keyImage.num, slot, 0x64] });
    } else eng.post({ t: "call", fn: "ed_key_off", args: [note] });
}
let keyImage = null;
function numIn(p, it) { const e = p.job && p.job.events && p.job.events.find(x => x[1] === "ed_key_on"); return e ? e[2][1] : 0; }
function skip(d) {
    if (!now || !now.job.starts) return;
    const j = now.job, el = now.el || 0;
    let i = j.starts.findIndex((s, k) => k === j.starts.length - 1 || j.starts[k + 1] > el);
    i = Math.max(0, Math.min(j.starts.length - 1, i + d));
    eng.post({ t: "jump", sec: j.starts[i] });
}

/* ---- the mini player in the footer ---- */
let lastState = null;
function paint() {
    const $ = ED.ui.$;
    const lab = $("#plabel"), btn = $("#pbtn");
    if (!now) {
        lab.textContent = "NOTHING PLAYING"; lab.className = "plabel idle"; btn.textContent = "▶"; btn.title = "PLAY THE PICKED ITEM (SPACE)";
        $("#pfill").style.width = "0"; $("#ptime").textContent = ""; $("#pprev").hidden = $("#pnext").hidden = true;
        for (const s of [...$("#prog").querySelectorAll("s")]) s.remove();
        return;
    }
    lab.textContent = now.label; lab.className = "plabel"; btn.textContent = "■"; btn.title = "STOP";
    const multi = !!(now.job.starts && now.job.starts.length > 1);
    $("#pprev").hidden = $("#pnext").hidden = !multi;
    const prog = $("#prog");
    for (const s of [...prog.querySelectorAll("s")]) s.remove();
    if (now.job.kind === "song") for (let i = 1; i < now.job.npos; i++) prog.append(ED.ui.h("s", { style: { left: (100 * i / now.job.npos) + "%" } }));
}
function onState(st, job) {
    lastState = st;
    if (!now || !st) return;
    const $ = ED.ui.$, j = now.job;
    let frac = 0, txt = "";
    const gap = job && job.phase === "gap";
    if (j.kind === "song") { frac = (st.pos + st.row / Math.max(1, st.rows)) / j.npos; txt = "POS " + ED.model.H2(st.pos) + " ROW " + ED.model.H2(st.row); }
    else if (j.kind === "pattern" || j.kind === "loops") { frac = st.row / Math.max(1, st.rows); txt = "ROW " + ED.model.H2(st.row); }
    else if (job) { now.el = job.el; frac = Math.min(1, job.el / j.length); txt = job.el.toFixed(1) + " / " + j.length.toFixed(1) + " S"; }
    if (gap) { frac = 1; txt = "AGAIN IN A SECOND"; }
    $("#pfill").style.width = (100 * Math.max(0, Math.min(1, frac))) + "%";
    $("#ptime").textContent = txt + " · " + st.voices + (st.voices === 1 ? " VOICE" : " VOICES");
}
const VOL_KEY = "ptfm-editor-volume-db";
function wire() {
    const $ = ED.ui.$;
    eng.listeners.push(onState);
    /* VOL: the preview's level in dB, this viewer's own (kept in this browser) */
    const vol = $("#pvol"), volv = $("#pvolv");
    let v = 0;
    try { const s = localStorage.getItem(VOL_KEY); if (s !== null && s !== "" && !isNaN(+s)) v = Math.max(VOL_MIN, Math.min(VOL_MAX, Math.round(+s))); } catch (e) { /* (no storage: 0 DB) */ }
    const paintVol = () => { vol.style.setProperty("--p", (100 * (+vol.value - VOL_MIN) / (VOL_MAX - VOL_MIN)) + "%"); volv.textContent = volText(+vol.value); };
    vol.value = v;
    eng.vol = v;
    paintVol();
    vol.addEventListener("input", () => {
        paintVol();
        eng.volume(+vol.value);
        try { localStorage.setItem(VOL_KEY, vol.value); } catch (e) { /* (kept for this page only) */ }
    });
    vol.addEventListener("keydown", e => e.stopPropagation());
    /* a double-click: the plain level, 0 DB */
    vol.title = "THE PREVIEW'S VOLUME · DOUBLE-CLICK: 0 DB";
    vol.addEventListener("dblclick", () => { vol.value = 0; vol.dispatchEvent(new Event("input")); });
    $("#pbtn").addEventListener("click", () => {
        if (now) { stop(); return; }
        const side = ED.files.active(), sel = ED.files.selected(side);
        if (sel.length === 1) play(sel[0]); else ED.ui.status("PICK ONE ITEM TO PLAY");
    });
    $("#pprev").addEventListener("click", () => skip(-1));
    $("#pnext").addEventListener("click", () => skip(1));
    $("#prog").addEventListener("click", e => {
        if (!now) return;
        const r = $("#prog").getBoundingClientRect(), f = Math.max(0, Math.min(0.999, (e.clientX - r.left) / r.width)), j = now.job;
        if (j.kind === "song") eng.post({ t: "jump", pos: Math.floor(f * j.npos) });
        else if (j.kind === "pattern") eng.post({ t: "jump", row: Math.floor(f * j.rows) });
        else if (j.kind === "seq") eng.post({ t: "jump", sec: f * j.length });
    });
}

ED.player = { canPlay, isPlaying, play, stop, toggle, update, key, plan, wire, slotsOf, SOURCE, get engine() { return eng; }, get state() { return lastState; },
    get now() { return now; }, ARP, EIGHTH };
})(globalThis.ED = globalThis.ED || {});

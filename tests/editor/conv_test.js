/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The editor's converters: DX7 cartridges through .syx and back (every built-in DX7 voice's fields); a sound (a WAV
 * made here: its rate proposed, its pitch found, coded into a sampler); a MIDI file made here (tracks for a chord,
 * drums on the kit, tempo changes, note offs, patterns alike kept once); and tracker modules given as arguments (MOD,
 * S3M, XM): every cell's instrument in its cartridge, every sample decoding, the song played by the firmware's own
 * code in its WebAssembly and heard.
 *
 *   node tests/editor/conv_test.js [MODULE...] */
"use strict";
const fs = require("fs"), path = require("path");
const { ED, S, ROOT, demo, wasm, lib: recLib, check, done } = require("./load.js");
const R = ED.rec, K = R.KIND;

(async () => {
    /* ---- .syx ---- */
    const ex = wasm(), mem = () => new Uint8Array(ex.memory.buffer), built = [];
    for (let i = 0; i < ex.ed_builtin_count(); i++) {
        const h = mem().slice(ex.ed_builtin(i), ex.ed_builtin(i) + 12), dv = new DataView(h.buffer);
        built.push({ kind: h[0], num: h[1], sub: h[2], ver: h[3], payload: mem().slice(dv.getUint32(8, true), dv.getUint32(8, true) + dv.getUint32(4, true)) });
    }
    let voices = 0;
    for (const b of built.filter(x => x.kind === K.DX7)) {
        const c = R.unpackCart(K.DX7, b.payload);
        const back = ED.conv.syx(ED.conv.toSyx(c), "x.syx");
        const c2 = R.unpackCart(K.DX7, back.recs[0].payload);
        let same = true;
        c.slots.forEach((s, i) => {
            if (!s) return;
            const t = c2.slots[i];
            const strip = x => JSON.stringify({ ops: x.ops, voice: Object.assign({}, x.voice, { transpose: Math.min(63, x.voice.transpose) }) });
            if (strip(s) !== strip(t) || t.name !== s.name) same = false;
            voices++;
        });
        check(same, `DX7 cartridge ${R.hex2(b.num)} through .syx and back: every field`);
    }
    check(voices > 200, "DX7 voices compared: " + voices);

    /* ---- a sound ---- */
    const rate = 22050, n = rate, wav = new Uint8Array(44 + 2 * n), dv = new DataView(wav.buffer);
    const put = (o, s) => { for (let i = 0; i < 4; i++) wav[o + i] = s.charCodeAt(i); };
    put(0, "RIFF"); dv.setUint32(4, 36 + 2 * n, true); put(8, "WAVE"); put(12, "fmt "); dv.setUint32(16, 16, true);
    dv.setUint16(20, 1, true); dv.setUint16(22, 1, true); dv.setUint32(24, rate, true); dv.setUint32(28, rate * 2, true);
    dv.setUint16(32, 2, true); dv.setUint16(34, 16, true); put(36, "data"); dv.setUint32(40, 2 * n, true);
    for (let i = 0; i < n; i++) dv.setInt16(44 + 2 * i, Math.round(20000 * Math.sin(2 * Math.PI * 440 * i / rate)), true);
    const snd = await ED.conv.decodeSound("a440.wav", wav);
    check(snd.rate === rate && snd.length === n && snd.bits === 16, "the WAV read at its own rate and depth");
    check(ED.conv.suggestRate(snd) === 8000, "a 440 Hz sine: 8000 Hz proposed (" + ED.conv.suggestRate(snd) + ")");
    check(ED.conv.guessNote(snd) === 69, "its pitch: A-4 (" + ED.conv.guessNote(snd) + ")");
    const made = ED.conv.soundToSample(snd, { start: 0, end: n, chan: "l", rate: 11025, bits: 8, name: "SINE" });
    check(made.entry.rate === 11025 && made.entry.length === n / 2 && made.entry.coding === 1 && made.entry.name === "SINE", "a sample: " + JSON.stringify(made.entry));
    const dropped = await ED.drops.convert("a440.wav", wav);
    const ds = dropped.items[0];
    check(dropped.items.length === 1 && ds.kind === K.AUDIO && !ds.owner && ds.sound && ds.sound.rate === rate && ED.model.sampleEntry(ds, dropped.items).rate === 8000,
        "a sound dropped on the TABLE: a sample alone at the rate proposed, its own audio kept beside it (" + dropped.what + ")");
    const au = R.ptrDecode(made.audio);
    let peak = 0;
    for (const v of au.samples) peak = Math.max(peak, Math.abs(v));
    check(au.bits === 8 && peak > 70 && peak <= 127, "its audio, 8 bits, peak " + peak);

    /* ---- OPTIMIZE's levels: a 16-bit sound of a 440 Hz tone and its third harmonic, then a second of silence; its
     * instruments play 0.2-0.8 s of it ---- */
    {
        const m = 2 * rate, ch = new Float32Array(m), tone = i => 0.6 * Math.sin(2 * Math.PI * 440 * i / rate) + 0.2 * Math.sin(2 * Math.PI * 1320 * i / rate);
        for (let i = 0; i < rate; i++) ch[i] = tone(i);
        const s2 = { rate, channels: 1, length: m, ch: [ch], bits: 16 };
        const cut = { start: 0, end: m, chan: "mix", rate, bits: 16 };
        const lv = ED.conv.optimize(s2, cut, { first: Math.round(0.2 * rate), last: Math.round(0.8 * rate) }, true);
        const size = c => ED.conv.soundToSample(s2, Object.assign({ name: "T" }, c)).audio.length;
        const sz = [size(cut), size(lv.lossless), size(lv.smaller), size(lv.smallest)];
        check(lv.lossless.start === Math.round(0.2 * rate) && lv.lossless.end === Math.round(0.8 * rate) && lv.lossless.rate === rate && lv.lossless.bits === 16 && !lv.lossless.gain,
            "OPTIMIZE LOSSLESS: what no instrument plays cut off, nothing else (" + lv.lossless.start + "-" + lv.lossless.end + ")");
        const un = ED.conv.optimize(s2, cut, null, false).lossless;
        check(un.start === 0 && un.end <= rate + 64 && un.end >= rate - 64, "with no instrument: only the silent tail cut (" + un.end + ")");
        check(lv.smaller.rate < rate && lv.smallest.bits === 8 && lv.smallest.soften === 0.25 && lv.smallest.gain === -6 && !ED.conv.optimize(s2, cut, null, false).smallest.gain,
            "SMALLER the proposed rate (" + lv.smaller.rate + " Hz); SMALLEST 8 bits, softer, 6 dB lower only when LEVEL makes it up");
        check(sz[1] < sz[0] && sz[2] < sz[1] && sz[3] < sz[2], "each level smaller: " + sz.join(" > ") + " B");
        /* (a softer top codes smaller where there is a top: the tone with noise over it) */
        const nz = Float32Array.from(ch, (v, i) => i < rate ? v + 0.05 * Math.sin(i * i % 7919) : 0), s3 = Object.assign({}, s2, { ch: [nz] });
        const plain = ED.conv.soundToSample(s3, Object.assign({ name: "T" }, cut)).audio.length;
        const soft = ED.conv.soundToSample(s3, Object.assign({ name: "T", soften: 0.25 }, cut)).audio.length, low = ED.conv.soundToSample(s3, Object.assign({ name: "T", gain: -6 }, cut)).audio.length;
        check(soft < plain && low < plain && nz[0] === ch[0] + 0.05 * Math.sin(0) && Math.abs(ch[100] - tone(100)) < 1e-6,
            "a softer top and a lower level each code smaller (" + plain + " > " + soft + ", " + low + " B), the sound's own audio untouched");
    }

    /* ---- OPTIMIZE SAMPLES on the TABLE: the demo disk's samplers picked, every level planned, SMALLEST applied as one
     * state (the one before untouched), each sample as its tab makes it, its instruments following ---- */
    {
        const M = ED.model, O = ED.optimize;
        const L0 = M.fromRecords(S.parseData(demo()).files, { side: "L", src: "DEMO" }).items;
        const st0 = { L: L0, R: [], base: [] };
        const carts = L0.filter(i => i.kind === K.SAMPLER), ids = carts.map(c => c.id);
        const n = O.samplesOf(L0, ids).length;
        const t0 = Date.now(), plans = O.LEVELS.map(l => O.plan(L0, ids, l)), ms = Date.now() - t0;
        check(n === L0.filter(i => i.kind === K.AUDIO).length && plans.every(p => p.picked === n && p.failed === 0 && p.after <= p.before),
            "OPTIMIZE SAMPLES: the samplers picked, their " + n + " samples at each level, none failed (" + ms + " ms for all three): " +
            plans.map(p => p.level + " " + p.results.length + " smaller, " + p.before + " -> " + p.after + " B").join("; "));
        check(plans[2].after < plans[1].after && plans[1].after <= plans[0].after && plans[2].results.length > 0, "each level saves more than the one before");
        const p = plans[2], st1 = O.apply(st0, "L", p);
        const byId = items => new Map(items.map(i => [i.id, i]));
        const m0 = byId(st0.L), m1 = byId(st1.L);
        const same = p.results.every(r => M.size(m1.get(r.id)) === M.size(r.item) && M.size(r.item) < M.size(m0.get(r.id)) && !m1.get(r.id).entry);
        check(same && st0.L === L0 && L0.every(i => m0.get(i.id) === i), "applied: each picked sample smaller, its entry in its sampler; the state before untouched");
        /* one sample, against what its tab makes: the same audio, the same entry, the instruments' LEVEL up 6 dB */
        const r0 = p.results.find(r => r.cut.gain === -6) || p.results[0], s0 = m0.get(r0.id);
        const ctx = O.context(s0, L0), tab = O.remake(s0, ctx, ED.conv.optimize(ctx.snd, ctx.cut0, ctx.used, ctx.players.length > 0).smallest);
        const e1 = M.sampleEntry(m1.get(r0.id), st1.L), eT = M.sampleEntry(tab, L0);
        const a = R.unpackSampler(m0.get(s0.owner).recs[0].payload).instruments, b = R.unpackSampler(m1.get(s0.owner).recs[0].payload).instruments;
        const ups = a.map((x, k) => x && x.sample === s0.idx ? b[k].level - x.level : null).filter(v => v != null);
        check(R.ptrDecode(m1.get(r0.id).recs[0].payload).samples.join() === R.ptrDecode(tab.recs[0].payload).samples.join() && e1.length === eT.length && e1.rate === eT.rate &&
            e1.name === eT.name && (r0.cut.gain !== -6 || ups.every(v => v === Math.min(12, 127 - a.find(x => x && x.sample === s0.idx).level) || v === 12)),
            "a sample as its tab's SMALLEST makes it (" + e1.name + ", " + e1.length + " at " + e1.rate + " Hz), its instruments' LEVEL up " + ups.join(", "));
        check(O.samplesOf(st1.L, [r0.id]).length === 1 && O.samplesOf(st1.L, []).length === 0, "a sample picked alone: just it; nothing picked: none");
    }

    /* ---- a MIDI file ---- */
    const trk = (events) => {                                     /* [delta, ...bytes] */
        const b = [];
        for (const [d, ...e] of events) { const v = []; let x = d; v.unshift(x & 0x7F); while ((x >>= 7)) v.unshift(0x80 | (x & 0x7F)); b.push(...v, ...e); }
        b.push(0, 0xFF, 0x2F, 0);
        return [0x4D, 0x54, 0x72, 0x6B, b.length >>> 24, (b.length >> 16) & 255, (b.length >> 8) & 255, b.length & 255, ...b];
    };
    const Q = 96;
    const t0 = trk([[0, 0xFF, 0x51, 3, 0x07, 0xA1, 0x20], [0, 0xFF, 0x58, 4, 4, 2, 24, 8], [Q * 8, 0xFF, 0x51, 3, 0x05, 0xB8, 0xD8]]);   /* 120, then 160 BPM */
    const t1 = trk([[0, 0xC0, 0], [0, 0x90, 60, 100], [0, 0x90, 64, 90], [0, 0x90, 67, 80], [Q * 2, 0x80, 60, 0], [0, 0x80, 64, 0], [0, 0x80, 67, 0],
        [Q, 0x90, 62, 110], [Q, 0x80, 62, 0], [0, 0xE0, 0, 70]]);
    const t2 = trk([[0, 0x99, 36, 120], [0, 0x99, 42, 90], [Q / 2, 0x89, 42, 0], [Q / 2, 0x99, 38, 100], [0, 0x89, 36, 0], [Q, 0x89, 38, 0]]);
    const smf = Uint8Array.from([0x4D, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 1, 0, 3, 0, Q, ...t0, ...t1, ...t2]);
    const mid = ED.conv.readMidi(smf);
    check(mid.format === 1 && mid.notes === 7 && Math.round(mid.tempo) === 120, `the MIDI file: format ${mid.format}, ${mid.notes} notes, ${mid.tempo} BPM`);
    const ch1 = mid.channels.find(c => c.ch === 0);
    check(ch1 && ch1.poly === 3 && ch1.program === 0, "channel 1: a chord of 3, program 1");
    const opts = ED.conv.midiDefaults(mid);
    check(opts.map[0] === 0xF007 && opts.map[9] === 0xF600, "programs on the built-ins: a grand piano F0:07, the drums F6");
    const song = ED.conv.midiToSong(mid, opts, "test.mid");
    const lib = recLib(song.recs);
    const sg = lib.decode("S:00"), p0 = lib.decode("P:00");
    check(sg && sg.tempo === 120 && sg.rpb === 4 && sg.ticks === 6, "the song: TEMPO 120, 4 rows a beat, 6 ticks");
    check(p0 && p0.tracks === 5 && p0.rows === 64, `pattern 00: ${p0 && p0.tracks} tracks (3 for the chord, 2 for the drums), ${p0 && p0.rows} rows`);
    const at = (r, tr) => p0.cells.get(R.cellKey(r, tr)) || {};
    check(at(0, 0).note === 60 && at(0, 1).note === 64 && at(0, 2).note === 67 && at(0, 0).ins === 0xF007 && at(0, 0).vol === 100, "the chord on row 00, each note its track");
    check(at(8, 0).note === R.NOTE_OFF && at(12, 0).note === 62, "a note off at its end (row 08), the next note at row 0C");
    const kick = [...p0.cells.values()].find(c => c.ins === 0xF600), hat = [...p0.cells.values()].find(c => c.ins === 0xF60D);
    check(kick && kick.note === 60 && hat, "the drums on FM DRUM KIT: kick F6:00, hat F6:0D at C-4");
    const tempoCell = [...lib.decode("P:00").cells.entries(), ...(lib.decode("P:01") ? lib.decode("P:01").cells.entries() : [])].find(([k, c]) => c.cmds.some(x => x[0] === 0x0F));
    check(tempoCell && tempoCell[1].cmds.find(x => x[0] === 0x0F)[1] === 160, "the tempo change as F A0 (160 BPM)");
    check(song.warnings.some(w => /PITCH BEND/.test(w)), "pitch bend counted as left out");
    const long = ED.conv.midiToSong(mid, Object.assign({}, opts, { bars: 16 }), "test.mid");   /* (16 bars of 16 rows asked) */
    const longRows = long.recs.filter(r => r.kind === K.PATTERN && r.sub === 0).map(r => r.payload[1] + 1);
    check(longRows.length && longRows.every(n => n <= 128), "a MIDI file: patterns of 128 rows at most (" + longRows.join() + ")");

    /* ---- a module's pattern of more than 128 rows: two patterns, one after the other (specs/files/patterns.md#size);
     * a break into its second part pointed there (B and D), one into its first left as it is ---- */
    {
        const smp = { data: Int8Array.from({ length: 64 }, (_, i) => (i % 16) * 8 - 64), name: "S", relNote: 0, loopStart: 0, loopLength: 0,
                      pingPong: false, fineTune: 0, volume: 64, bits: 8, panning: 0 };
        const pat = (rows, cells) => {                            /* IBXM's pattern: 5 bytes a cell (key, ins, vol, fx, fp) */
            const data = new Uint8Array(rows * 5);
            for (const [r, key, ins, fx, fp] of cells) data.set([key, ins, 0, fx, fp], r * 5);
            return { rows, channels: 1, data };
        };
        const m = { kind: "XM", name: "SPLIT", warnings: [], channels: 1, c2Rate: 8363, gain: 64, tempo: 125, speed: 6, linear: true,
                    fastVol: false, restart: 0, gvol: 64, panning: [128],
                    instruments: [{ name: "", samples: [] }, { name: "I", samples: [smp], keyToSample: [] }],
                    patterns: [pat(64, [[0, 49, 1, 0, 0], [63, 0, 0, 0x0D, 0xF0]]),            /* D, row 150 (as IBXM reads F0) */
                               pat(200, [[0, 49, 1, 0, 0], [150, 51, 1, 0, 0]]),
                               pat(64, [[0, 49, 1, 0, 0], [10, 0, 0, 0x0D, 0x40]])],          /* D, row 40 */
                    sequence: [0, 1, 2, 1] };
        const r = ED.conv.fromModule(m, "split.xm");
        const ml = recLib(r.recs);
        const P = n => ml.decode("P:" + R.hex2(n)), ms = ml.decode("S:00");
        check(P(1) && P(1).rows === 128 && P(2) && P(2).rows === 72 && P(3) && P(3).rows === 64 && !ml.objects().has("P:04"),
              "a pattern of 200 rows: 128 and 72 rows, the later numbers shifting (" + [0, 1, 2, 3].map(n => P(n) && P(n).rows).join() + ")");
        check(ms && ms.positions.join() === "0,1,2,3,1,2", "the order lists both where it was: " + (ms && ms.positions.join()));
        const at = (n, row) => P(n) && P(n).cells.get(R.cellKey(row, 0));
        check(at(2, 22) && at(2, 22).note === 51 + 11, "row 150's note on the second part's row 22");
        const cmd = (c, k) => c && c.cmds.find(x => x[0] === k);
        check(cmd(at(0, 63), 0x0B) && cmd(at(0, 63), 0x0B)[1] === 2 && cmd(at(0, 63), 0x0D)[1] === 22,
              "a break to row 150 lands on the second part's row 22 (B02 D16): " + JSON.stringify(at(0, 63) && at(0, 63).cmds));
        check(cmd(at(3, 10), 0x0D) && cmd(at(3, 10), 0x0D)[1] === 40 && !cmd(at(3, 10), 0x0B), "a break to row 40 stays on the first part (D28 alone)");
        check(r.warnings.some(w => /^PATTERN 1: 200 ROWS, SPLIT IN TWO/.test(w)), "the warning: " + r.warnings.join(" / "));
    }

    /* ---- modules given as arguments ---- */
    const files = process.argv.slice(2);
    const module = files.length ? await WebAssembly.compile(fs.readFileSync(path.join(ROOT, "build", "editor", "wasm", "ptfm.wasm"))) : null;
    for (const f of files) {
        const name = path.basename(f);
        const r = ED.conv.module(new Uint8Array(fs.readFileSync(f)), name);
        /* dropped on the TABLE: its items, every cell's instrument linked to its sampler */
        const items = (await ED.drops.convert(name, new Uint8Array(fs.readFileSync(f)))).items, M = ED.model;
        const songs = items.filter(o => o.kind === K.SONG);
        check(songs.length === 1, `${name}: ${r.what}`);
        let missing = 0, cells = 0;
        for (const o of items) if (o.kind === K.PATTERN) {
            for (const c of R.unpackPattern(o.recs.map(x => x.payload)).cells.values()) {
                cells++;
                if (c.ins >= 0) {
                    const t = o.link[M.lk("c", c.ins >> 8)], cart = items.find(x => x.id === t);
                    if (!cart || !R.unpackSampler(cart.recs[0].payload).instruments[c.ins & 255]) missing++;
                }
            }
        }
        check(missing === 0 && cells > 100, `${name}: ${cells} cells, every instrument in its cartridge`);
        const tb = { recs: new Map(r.recs.map((x, i) => [i, x])), totalBytes: () => r.recs.reduce((s, x) => s + x.payload.length, 0) };
        songs[0].num = r.recs.find(x => x.kind === K.SONG).num;
        let decoded = 0, badDec = 0;
        for (const x of tb.recs.values()) if (x.kind === K.AUDIO) { try { R.ptrDecode(x.payload); decoded++; } catch (e) { badDec++; } }
        check(badDec === 0 && decoded > 0, `${name}: ${decoded} samples' audio decodes`);
        for (const w of r.warnings) console.log("  (" + name + ") " + w);
        /* played by the firmware */
        const fw = await new ED.Firmware(module).start(null);
        let blocks;
        try { blocks = ED.flash.image([...tb.recs.values()], fw.ex.web_fw_end()); }
        catch (e) {
            check(/fit/.test(e.message) && tb.totalBytes() > 400 * 1024, `${name}: ${ED.util.kb(tb.totalBytes())}, more than an FM-1 holds: not played (${e.message})`);
            continue;
        }
        fw.mount(blocks);
        fw.ex.ed_play_song(songs[0].num, 0, 0);
        let pk = 0, voicesSeen = 0;
        for (let i = 0; i < 160; i++) {
            fw.ex.web_render(1024);
            const out = new Int32Array(fw.ex.memory.buffer, fw.ex.web_out(), 2048);
            for (const v of out) pk = Math.max(pk, Math.abs(v));
            voicesSeen = Math.max(voicesSeen, fw.mem()[fw.ex.ed_state() + 9]);
        }
        const st = fw.mem().slice(fw.ex.ed_state(), fw.ex.ed_state() + 16);
        check(st[1] === 1 && pk > 100000 && voicesSeen > 0, `${name}: the firmware plays it (position ${st[4]}, row ${st[6]}, ${voicesSeen} voices at most, peak ${pk})`);
    }
    done("conv");
    process.exit(0);
})().catch(e => { console.error("FAILED:", e.stack || e.message); process.exit(1); });

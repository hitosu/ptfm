/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* MIDI files (formats 0 and 1) into a song (specs/editor.md, MIDI files), a first version:
 *   - the notes put on the nearest row (ROWS A BEAT picked: 3, 4, 6 or 8; TICKS A ROW 24 / ROWS A BEAT, so TEMPO is
 *     the file's BPM), velocity as the cell's volume, a note's end as a note off (not the drums');
 *   - each channel on as many tracks as it plays notes at once, 16 tracks in all (what does not fit is counted);
 *   - the first tempo as the song's TEMPO, every later change as Fxx where it happens (32-255 BPM);
 *   - patterns of 1-8 bars (the file's time signature: its first), each pattern a position; patterns alike kept once;
 *   - each channel's first program onto a built-in instrument by the General MIDI table below (every PT/FM carries
 *     them); channel 10 onto FM DRUM KIT, a sound for each drum note, played at C-4; any of them changed by hand;
 *   - the sustain pedal holds notes' ends; pitch bend, modulation and the other controllers are left out (counted).
 *   readMidi(bytes) -> { format, division, tracks, channels: [{ ch, notes, poly, program }], notes, tempo, ... }
 *   midiDefaults(m) -> the options; midiToSong(m, opts, name) -> { recs, what, warnings } */
(function (ED) {
"use strict";
const R = ED.rec, K = R.KIND;
ED.conv = ED.conv || {};

/* General MIDI's 128 programs on the built-in instruments (cartridge << 8 | slot) */
const GM = [
    0xF007, 0xF00A, 0xF003, 0xF008, 0xF000, 0xF00D, 0xF011, 0xF014,          /* pianos */
    0xF10C, 0xF10A, 0xF813, 0xF112, 0xF100, 0xF113, 0xF102, 0xF119,          /* chromatic percussion */
    0xF01A, 0xF016, 0xF01C, 0xF018, 0xF01E, 0xF01F, 0xF310, 0xF01F,          /* organs */
    0xF204, 0xF20A, 0xF20B, 0xF205, 0xF207, 0xF206, 0xF206, 0xF208,          /* guitars */
    0xF51B, 0xF51A, 0xF513, 0xF51D, 0xF518, 0xF518, 0xF514, 0xF512,          /* basses */
    0xF214, 0xF216, 0xF217, 0xF218, 0xF219, 0xF21A, 0xF209, 0xF703,          /* strings */
    0xF210, 0xF212, 0xF820, 0xF822, 0xF402, 0xF401, 0xF403, 0xF416,          /* ensembles */
    0xF302, 0xF303, 0xF303, 0xF304, 0xF305, 0xF306, 0xF828, 0xF82C,          /* brass */
    0xF311, 0xF312, 0xF313, 0xF314, 0xF30F, 0xF307, 0xF315, 0xF309,          /* reeds */
    0xF317, 0xF308, 0xF319, 0xF31A, 0xF31B, 0xF31C, 0xF704, 0xF31D,          /* pipes */
    0xF501, 0xF500, 0xF50D, 0xF50D, 0xF50E, 0xF404, 0xF50F, 0xF506,          /* synth leads */
    0xF40F, 0xF406, 0xF411, 0xF405, 0xF412, 0xF413, 0xF414, 0xF415,          /* synth pads */
    0xF716, 0xF717, 0xF106, 0xF715, 0xF718, 0xF719, 0xF71A, 0xF71B,          /* synth effects */
    0xF20D, 0xF20E, 0xF200, 0xF200, 0xF11B, 0xF31E, 0xF21F, 0xF31F,          /* ethnic */
    0xF11C, 0xF706, 0xF707, 0xF708, 0xF61E, 0xF61E, 0xF616, 0xF612,          /* percussive */
    0xF71C, 0xF71C, 0xF71D, 0xF71E, 0xF71F, 0xF710, 0xF716, 0xF712];         /* sound effects */
const GM_FAMILY = ["PIANO", "CHROMATIC PERCUSSION", "ORGAN", "GUITAR", "BASS", "STRINGS", "ENSEMBLE", "BRASS", "REED", "PIPE",
    "SYNTH LEAD", "SYNTH PAD", "SYNTH EFFECTS", "ETHNIC", "PERCUSSIVE", "SOUND EFFECTS"];
/* channel 10's notes on FM DRUM KIT (F6): [slot, the note it plays] */
const DRUMS = { 35: [0x00, 60], 36: [0x00, 60], 37: [0x08, 60], 38: [0x05, 60], 39: [0x0C, 60], 40: [0x0B, 60], 41: [0x16, 55],
    42: [0x0D, 60], 43: [0x16, 57], 44: [0x0E, 60], 45: [0x16, 60], 46: [0x0F, 60], 47: [0x16, 62], 48: [0x16, 64], 49: [0x10, 60],
    50: [0x16, 67], 51: [0x11, 60], 52: [0x12, 60], 53: [0x11, 64], 54: [0x09, 60], 55: [0x10, 64], 56: [0x17, 60], 57: [0x10, 62],
    59: [0x11, 62], 60: [0x1F, 64], 61: [0x1F, 60], 62: [0x19, 64], 63: [0x19, 60], 64: [0x19, 55], 65: [0x1B, 64], 66: [0x1B, 60],
    67: [0x17, 67], 68: [0x17, 64], 75: [0x18, 60], 76: [0x18, 64], 77: [0x18, 60] };
const DRUM_KIT = 0xF6;

function readMidi(b) {
    const s = String.fromCharCode(...b.subarray(0, 4));
    if (s !== "MThd") throw new Error("not a MIDI file");
    const u32 = o => (b[o] << 24 | b[o + 1] << 16 | b[o + 2] << 8 | b[o + 3]) >>> 0, u16 = o => b[o] << 8 | b[o + 1];
    const format = u16(8), ntrk = u16(10), division = u16(12);
    if (division & 0x8000) throw new Error("a MIDI file timed in SMPTE frames: not read (only ticks a quarter note)");
    let at = 8 + u32(4);
    const notes = [], tempos = [], programs = {}, sig = [], tracks = [];
    const counted = {};
    const count = n => { counted[n] = (counted[n] || 0) + 1; };
    for (let t = 0; t < ntrk && at + 8 <= b.length; t++) {
        const id = String.fromCharCode(...b.subarray(at, at + 4)), len = u32(at + 4);
        let p = at + 8;
        const end = Math.min(b.length, p + len);
        at = p + len;
        if (id !== "MTrk") { t--; continue; }
        let tick = 0, status = 0, name = "";
        const on = new Map(), sustain = new Array(16).fill(false), held = new Array(16).fill(null).map(() => []);
        const vlq = () => { let v = 0, c; do { c = b[p++]; v = (v << 7) | (c & 0x7F); } while (c & 0x80 && p < end); return v; };
        const close = (ch, n, endTick) => {
            const k = ch * 128 + n, list = on.get(k);
            if (!list || !list.length) return;
            const st = list.shift();
            notes.push({ ch, note: n, vel: st.vel, start: st.tick, end: Math.max(endTick, st.tick + 1), track: t });
        };
        while (p < end) {
            tick += vlq();
            let st = b[p];
            if (st & 0x80) p++; else st = status;
            if (st === 0xFF) {
                const type = b[p++], l = vlq(), d = b.subarray(p, p + l);
                p += l;
                if (type === 0x51 && l === 3) tempos.push({ tick, us: d[0] << 16 | d[1] << 8 | d[2] });
                else if (type === 0x58 && l >= 2) sig.push({ tick, num: d[0], den: 1 << d[1] });
                else if (type === 0x03) name = String.fromCharCode(...d);
                else if (type === 0x2F) break;
                continue;
            }
            if (st === 0xF0 || st === 0xF7) { p += vlq(); continue; }
            status = st;
            const type = st & 0xF0, ch = st & 15;
            const d1 = b[p++], d2 = type === 0xC0 || type === 0xD0 ? 0 : b[p++];
            if (type === 0x90 && d2 > 0) {
                const k = ch * 128 + d1;
                if (!on.has(k)) on.set(k, []);
                on.get(k).push({ tick, vel: d2 });
            } else if (type === 0x80 || type === 0x90) {
                if (sustain[ch]) held[ch].push(d1); else close(ch, d1, tick);
            } else if (type === 0xC0) { if (programs[ch] === undefined) programs[ch] = d1; else if (programs[ch] !== d1) count("PROGRAM CHANGES AFTER THE FIRST"); }
            else if (type === 0xB0 && d1 === 64) {
                const down = d2 >= 64;
                if (!down && sustain[ch]) { for (const n of held[ch]) close(ch, n, tick); held[ch] = []; }
                sustain[ch] = down;
            } else if (type === 0xB0) { if (d1 !== 0 && d1 !== 32 && d1 !== 7 && d1 !== 10 && d1 !== 121 && d1 !== 123) count("CONTROLLER " + d1); }
            else if (type === 0xE0) count("PITCH BEND");
            else if (type === 0xD0 || type === 0xA0) count("PRESSURE");
        }
        for (const [k, list] of on) for (const st of list) notes.push({ ch: k >> 7, note: k & 127, vel: st.vel, start: st.tick, end: tick, track: t });
        tracks.push({ name, events: 0 });
    }
    notes.sort((a, b2) => a.start - b2.start || a.ch - b2.ch || a.note - b2.note);
    tempos.sort((a, b2) => a.tick - b2.tick);
    const chans = [...new Set(notes.map(n => n.ch))].sort((a, b2) => a - b2).map(ch => {
        const mine = notes.filter(n => n.ch === ch);
        let poly = 0;
        const ends = [];
        for (const n of mine) { while (ends.length && ends[0] <= n.start) ends.shift(); ends.push(n.end); ends.sort((x, y) => x - y); poly = Math.max(poly, ends.length); }
        return { ch, notes: mine.length, poly, program: programs[ch] !== undefined ? programs[ch] : -1 };
    });
    const tempo = tempos.length ? 60e6 / tempos[0].us : 120;
    return { format, division, tracks, notes: notes.length, list: notes, channels: chans, tempos, sig: sig[0] || { num: 4, den: 4 }, tempo, counted };
}

function midiDefaults(m) {
    const map = {};
    for (const c of m.channels) map[c.ch] = c.ch === 9 ? (DRUM_KIT << 8) : GM[Math.max(0, c.program)];
    return { rpb: 4, bars: 4, vel: "vol", offs: "off", map };
}

function midiToSong(m, o, name) {
    const warnings = [], rpb = o.rpb, ticks = Math.max(1, Math.round(24 / rpb)), perRow = m.division / rpb;
    const beatsBar = Math.max(1, Math.round(m.sig.num * 4 / m.sig.den));
    const rowsPat = Math.min(R.PAT_ROWS_MAX, o.bars * beatsBar * rpb);
    const row = t => Math.round(t / perRow);
    for (const n in m.counted) warnings.push(`LEFT OUT: ${m.counted[n]} × ${n}`);
    /* tracks: each channel's notes onto the first of its tracks free at their row */
    const chans = m.channels.filter(c => o.map[c.ch] >= 0);
    let budget = 16;
    const want = chans.map(c => ({ ch: c.ch, n: Math.min(c.poly, 16) }));
    while (want.reduce((a, w) => a + w.n, 0) > budget) { const w = want.reduce((a, x) => x.n > a.n ? x : a); w.n--; }
    const tracks = [];                              /* { ch, busyTo } */
    const trackOf = {};
    for (const w of want) { trackOf[w.ch] = []; for (let i = 0; i < w.n; i++) { trackOf[w.ch].push(tracks.length); tracks.push({ ch: w.ch, busyTo: -1, last: null }); } }
    if (!tracks.length) throw new Error("no channel left to make tracks of");
    const events = [];                              /* { row, track, cell } */
    let dropped = 0;
    for (const n of m.list) {
        if (!(n.ch in trackOf)) continue;
        const r0 = row(n.start);
        let r1 = Math.max(r0 + 1, row(n.end));
        const t = trackOf[n.ch].find(i => tracks[i].busyTo <= r0);
        if (t === undefined) { dropped++; continue; }
        const T = tracks[t];
        /* the note off of the track's last note, if it ended before this one starts */
        if (T.last && T.last.off < r0 && o.offs === "off" && n.ch !== 9) events.push({ row: T.last.off, track: t, cell: { note: R.NOTE_OFF, ins: -1, vol: -1, cmds: [] } });
        let note = n.note, ins = o.map[n.ch];
        if (n.ch === 9) {
            const d = DRUMS[n.note];
            if (!d) { dropped++; continue; }
            ins = (o.map[9] & 0xFF00) === (DRUM_KIT << 8) ? (DRUM_KIT << 8 | d[0]) : o.map[9];
            note = (o.map[9] & 0xFF00) === (DRUM_KIT << 8) ? d[1] : n.note;
            r1 = r0 + 1;
        }
        if (note < 12) note = 12;
        events.push({ row: r0, track: t, cell: { note, ins, vol: o.vel === "vol" ? Math.max(1, Math.min(127, n.vel)) : -1, cmds: [] } });
        T.busyTo = r1;
        T.last = { off: r1 };
    }
    for (const [t, T] of tracks.entries()) if (T.last && o.offs === "off" && T.ch !== 9) events.push({ row: T.last.off, track: t, cell: { note: R.NOTE_OFF, ins: -1, vol: -1, cmds: [] } });
    if (dropped) warnings.push(`${dropped} NOTES LEFT OUT (MORE AT ONCE THAN THE TRACKS, OR A DRUM NOTE WITH NO SOUND)`);
    /* tempo changes after the first: Fxx on track 0's row */
    const tempoAt = [];
    for (const tp of m.tempos.slice(1)) {
        const bpm = Math.round(60e6 / tp.us);
        if (bpm < 32 || bpm > 255) { warnings.push(`A TEMPO OF ${bpm} BPM: Fxx TAKES 32-255`); }
        tempoAt.push({ row: row(tp.tick), cmd: [0x0F, Math.max(32, Math.min(255, bpm))] });
    }
    const last = events.reduce((a, e) => Math.max(a, e.row), 0) + 1;
    const npat = Math.max(1, Math.ceil(last / rowsPat));
    if (npat > 0xF0) throw new Error(`${npat} patterns: more than 240 (fewer bars a pattern, or a shorter file)`);
    const pats = Array.from({ length: npat }, () => ({ tracks: tracks.length, rows: rowsPat, step: 1, cells: new Map() }));
    for (const e of events) {
        const p = pats[Math.floor(e.row / rowsPat)];
        if (!p) continue;
        const k = R.cellKey(e.row % rowsPat, e.track), was = p.cells.get(k);
        if (was && was.note > 1 && e.cell.note === R.NOTE_OFF) continue;       /* (a new note there already ends it) */
        p.cells.set(k, Object.assign({}, e.cell, { cmds: was ? was.cmds : [] }));
    }
    for (const t of tempoAt) {
        const p = pats[Math.floor(t.row / rowsPat)];
        if (!p) continue;
        const k = R.cellKey(t.row % rowsPat, 0), c = p.cells.get(k) || { note: 0, ins: -1, vol: -1, cmds: [] };
        c.cmds = c.cmds.filter(x => x[0] !== 0x0F).concat([t.cmd]);
        p.cells.set(k, c);
    }
    /* patterns alike kept once */
    const recs = [], positions = [], seen = new Map();
    for (const p of pats) {
        const parts = R.packPattern(p), keyStr = parts.map(b => Array.from(b).join(",")).join("|");
        if (seen.has(keyStr)) { positions.push(seen.get(keyStr)); continue; }
        const num = seen.size;
        seen.set(keyStr, num);
        positions.push(num);
        parts.forEach((b, sub) => recs.push({ kind: K.PATTERN, num, sub, ver: 1, payload: b }));
    }
    if (positions.length > 256) throw new Error("more than 256 positions");
    const song = R.newSong(R.cleanName(String(name || "MIDI").replace(/\.[^.]*$/, ""), 16) || "MIDI SONG");
    song.positions = positions;
    song.rpb = rpb;
    song.ticks = ticks;
    song.tempo = Math.max(32, Math.min(300, Math.round(m.tempo * ticks * rpb / 24)));
    song.flags = R.SONG_LINEAR | R.SONG_STOP;
    /* each channel's tracks panned to the middle; drums a little louder in the mix than their notes' velocities */
    recs.push({ kind: K.SONG, num: 0, sub: 0, ver: 1, payload: R.packSong(song) });
    const what = `A SONG OF ${positions.length} POSITIONS (${seen.size} PATTERNS OF ${rowsPat} ROWS), ${tracks.length} TRACKS FOR ${chans.length} CHANNELS, TEMPO ${song.tempo}`;
    return { recs, what, warnings };
}

ED.conv.readMidi = readMidi;
ED.conv.midiDefaults = midiDefaults;
ED.conv.midiToSong = midiToSong;
ED.conv.GM = GM;
ED.conv.GM_FAMILY = GM_FAMILY;
})(globalThis.ED = globalThis.ED || {});

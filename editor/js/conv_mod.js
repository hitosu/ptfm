/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Tracker modules (MOD, S3M, XM) into PT/FM's records (specs/files/commands.md#tracker-effects, songs.md, patterns.md,
 * instruments.md): read as IBXM reads them (ibxm_load.js: its loaders), then
 *   - the song: TEMPO and TICKS A ROW from the module's tempo and speed, its order, restart, global volume, each
 *     channel's pan, the slides' kind (Amiga periods for MOD and S3M, XM's flag), S3M's fast volume slides;
 *   - its patterns (one of more than 128 rows as two: splitLong), a channel a track (16 at most), each cell's note (IBXM's key + 11 as MIDI; key 97 a note off),
 *     instrument, volume (the volume column's 0-64 as 2v, 64 as 7F), XM's volume-column effect first (0x24-0x2D),
 *     then the effect: XM's own numbers, S3M's as their XM twins (FINE VIBRATO 0x1E), Dxx's row in hex; F00 and
 *     A00, which IBXM ignores, left out; XM samples' pans as 8xx where an instrument starts (IBXM sets them there);
 *   - a sampler cartridge (more when over 64 instruments or samples): an instrument for each module instrument and
 *     sample (an XM instrument with several samples: one for each, the cells pointing at the one its key picks), its
 *     loop, VOLUME (the sample's), BASE NOTE and TUNE so it plays at IBXM's pitch: the module's C-4 rate (8287 Hz
 *     for 4-channel MODs, else 8363) at BASE NOTE 60 - the sample's relative note, TUNE 128 + its finetune; and its
 *     audio in PTR at its own depth.
 * What PT/FM does not keep is listed: XM's envelopes and auto-vibrato, IBXM's gain, channels past 16, the effects
 * without a twin. ED.conv.module(bytes, name) -> { recs, what, warnings }; ED.conv.fromModule(module, name) the same
 * from a module as read; ED.conv.loadModule(bytes) the module. */
(function (ED) {
"use strict";
const R = ED.rec, K = R.KIND;
ED.conv = ED.conv || {};

/* ---- effects: XM's own numbers; S3M's as their twins (IBXM runs both in one case) ---- */
const S3M_TWIN = { 0x82: 0x0B, 0x83: 0x0D, 0x84: 0x0A, 0x85: 0x02, 0x86: 0x01, 0x87: 0x03, 0x88: 0x04, 0x89: 0x1D, 0x8B: 0x06,
    0x8C: 0x05, 0x8F: 0x09, 0x91: 0x1B, 0x92: 0x07, 0x96: 0x10 };
const S3M_S_TWIN = { 0x2: 0x50, 0x3: 0x40, 0x4: 0x70, 0xB: 0x60, 0xC: 0xC0, 0xD: 0xD0, 0xE: 0xE0 };
function effect(fx, fp, count) {                  /* -> [code, value] or null; count(name) for the ones left out */
    if (!fx && !fp) return null;
    if (fx < 0x80) {
        if (fx === 0x0D) return [0x0D, (fp >> 4) * 10 + (fp & 15)];        /* the row: decimal -> hex */
        if (fx === 0x0F && fp === 0) return null;                          /* (IBXM ignores F00; PT/FM's means the song's) */
        if (fx === 0x15) { count("XM's L (envelope position)"); return null; }
        if (fx >= 0x40) return null;
        return [fx, fp];
    }
    if (S3M_TWIN[fx] !== undefined) { const c = S3M_TWIN[fx]; return c === 0x0D ? [0x0D, (fp >> 4) * 10 + (fp & 15)] : [c, fp]; }
    if (fx === 0x81) { if (!fp) return null; if (fp >= 32) { count("S3M's A from 20 (a speed XM has not)"); return null; } return [0x0F, fp]; }
    if (fx === 0x94) return fp > 32 ? [0x0F, fp] : null;
    if (fx === 0x8A) { if (!fp) { count("S3M's J00 (the last arpeggio again)"); return null; } return [0x00, fp]; }
    if (fx === 0x95) return [0x1E, fp];                                    /* U: FINE VIBRATO */
    if (fx === 0x93) {
        const x = fp >> 4, y = fp & 15;
        if (S3M_S_TWIN[x] !== undefined) return [0x0E, S3M_S_TWIN[x] | y];
        if (x === 8) return [0x08, y * 17];
        return null;
    }
    return null;                                                           /* (IBXM does nothing with it) */
}
/* XM's volume column (IBXM's byte): a set volume, or an effect as codes 0x24-0x2D */
function vcol(v) {
    if (v >= 0x10 && v <= 0x50) return { vol: v - 0x10 };
    if (v >= 0x60) return { cmd: [0x24 + ((v - 0x60) >> 4), v & 15] };
    return {};
}
const vol127 = v => v >= 64 ? 0x7F : 2 * v;

/* A pattern of more than 128 rows (patterns.md#size) becomes two, one after the other: rows 0-127 and the rest; the
 * later patterns' numbers shift, the order lists both where it was, and every B, the song's LOOP FROM and the breaks
 * follow the new positions. A row's B and D are played as IBXM plays them (channel by channel: B sets the position
 * and row 0, D the row and, with no B before it, the next position); where that lands in a second part, or where
 * the next position is no longer the one after (a break in a first part), a B to the new position goes before the
 * row's last D, its row in the part. A pattern played at several positions has no one position to name: a warning.
 * pats: the patterns' cells at full length; seq: the module's order (entries past the patterns IBXM skips).
 *   -> { pats, positions, posOf: the module's position -> the new one } */
function splitLong(pats, seq, nPat, warnings) {
    const parts = pats.map(p => p.rows > 128 ? 2 : 1), first = [];
    for (let i = 0, n = 0; i < pats.length; n += parts[i++]) first.push(n);
    if (first.length && first[first.length - 1] + parts[parts.length - 1] > 0xF0) throw new Error("more than 240 patterns");
    const positions = [], posOf = [];
    for (let i = 0; i < seq.length; i++) {
        posOf.push(positions.length);
        if (seq[i] < nPat) for (let k = 0; k < parts[seq[i]]; k++) positions.push(first[seq[i]] + k);
    }
    posOf.push(positions.length);
    const valid = i => { while (i < seq.length && seq[i] >= nPat) i++; return i; };   /* (as IBXM skips them) */
    const at = pi => seq.map((x, i) => x === pi ? i : -1).filter(i => i >= 0);
    pats.forEach((p, pi) => {
        if (parts[pi] > 1) {
            let note = "";
            /* E6x loops back across the split (its start, the channel's last E60 or row 0, in the first part) */
            for (let c = 0; c < p.tracks && !note; c++) {
                let start = 0;
                for (let r = 0; r < p.rows; r++) {
                    const e = (p.cells.get(R.cellKey(r, c)) || { cmds: [] }).cmds.find(x => x[0] === 0x0E && (x[1] >> 4) === 6);
                    if (!e) continue;
                    if (!(e[1] & 15)) start = r;
                    else if (r >= 128 && start < 128) note = "; ITS E6x LOOP ACROSS THE SPLIT IS LOST";
                }
            }
            warnings.push(`PATTERN ${pi}: ${p.rows} ROWS, SPLIT IN TWO` + note);
        }
        for (let r = 0; r < p.rows; r++) {
            const cmds = [];                       /* the row's B and D in channel order: [cell, index] */
            for (let c = 0; c < p.tracks; c++) {
                const cell = p.cells.get(R.cellKey(r, c));
                if (cell) cell.cmds.forEach((x, k) => { if (x[0] === 0x0B || x[0] === 0x0D) cmds.push([cell, k]); });
            }
            if (!cmds.length) continue;
            let jump = -1, row = 0, lastD = null, lastB = null;
            for (const [cell, k] of cmds) {
                const [code, v] = cell.cmds[k];
                if (code === 0x0B) { jump = v; row = 0; lastB = cell.cmds[k]; }
                else { row = v; lastD = [cell, k]; }
            }
            /* where it lands: (the module's position, its row) -> (the new position, the row in the part); null past
               the song's end (it starts again, as before) */
            const land = (T, rw) => {
                T = valid(T);
                if (T >= seq.length) return null;
                const tp = seq[T], two = parts[tp] > 1 && rw >= 128 && rw < pats[tp].rows;
                return { pos: posOf[T] + (two ? 1 : 0), row: two ? rw - 128 : rw };
            };
            const past = Math.min(255, positions.length);
            for (const [cell, k] of cmds) if (cell.cmds[k] !== lastB && cell.cmds[k][0] === 0x0B) {
                const to = land(cell.cmds[k][1], 0);
                cell.cmds[k] = [0x0B, to ? to.pos : past];
            }
            if (lastB) {                            /* (a D after it set the row: in the part's rows) */
                const to = land(lastB[1], row);
                lastB[1] = to ? to.pos : past;
                if (to && to.row !== row) lastD[0].cmds[lastD[1]] = [0x0D, to.row];
                continue;
            }
            /* D alone: the next position; for each position the pattern plays at */
            const part = r >= 128 ? 1 : 0;
            const want = at(pi).map(P => ({ to: land(P + 1, row), natural: posOf[P] + part + 1 }));
            if (want.every(w => w.to ? w.to.pos === w.natural && w.to.row === row : w.natural >= positions.length)) continue;
            const one = want.every(w => w.to && w.to.pos === want[0].to.pos && w.to.row === want[0].to.row);
            const [cell, k] = lastD;
            if (!one || cell.cmds.length >= 4) {
                warnings.push(`PATTERN ${pi}, ROW ${r}: ITS BREAK CANNOT FOLLOW THE SPLIT` + (want.some(w => !w.to) ? " (AT THE SONG'S END)" :
                              !one ? " (THE PATTERN PLAYS AT SEVERAL POSITIONS)" : " (4 COMMANDS IN ITS CELL)"));
                continue;
            }
            cell.cmds[k] = [0x0D, want[0].to.row];
            cell.cmds.splice(k, 0, [0x0B, want[0].to.pos]);
        }
    });
    /* the parts */
    const out = [];
    pats.forEach((p, pi) => {
        for (let k = 0; k < parts[pi]; k++) {
            const cells = new Map(), r0 = k * 128, rows = Math.min(128, p.rows - r0);
            for (const [key, c] of p.cells) { const [r, t] = key.split(",").map(Number); if (r >= r0 && r < r0 + rows) cells.set(R.cellKey(r - r0, t), c); }
            out.push({ tracks: p.tracks, rows, step: p.step, cells });
        }
    });
    return { pats: out, positions, posOf };
}

ED.conv.loadModule = b => ED.ibxm.loadModule(b);
ED.conv.module = (b, name) => ED.conv.fromModule(ED.ibxm.loadModule(b), name);
/* a module as IBXM's loader gives it (ED.ibxm.loadModule) */
ED.conv.fromModule = function (m, name) {
    const warnings = m.warnings.slice(), dropped = {};
    const count = n => { dropped[n] = (dropped[n] || 0) + 1; };
    if (m.channels > 16) warnings.push(`${m.channels} CHANNELS: THE FIRST 16 KEPT (A PATTERN HAS 16 TRACKS AT MOST)`);
    /* the instruments: one PT/FM instrument for each (instrument, sample) a cell plays, in cartridges of 64 */
    const insts = [];                              /* { ins, sam, sample, name } */
    const slotOf = new Map();                      /* ins * 256 + sam -> index in insts */
    const want = (ins, sam) => {
        const k = ins * 256 + sam;
        if (!slotOf.has(k)) {
            const s = m.instruments[ins] && m.instruments[ins].samples[sam];
            if (!s || !s.data.length) return -1;
            slotOf.set(k, insts.length);
            insts.push({ ins, sam, sample: s, name: m.instruments[ins].samples.length > 1 ? (s.name || m.instruments[ins].name) : (m.instruments[ins].name || s.name) });
        }
        return slotOf.get(k);
    };
    /* MOD and S3M: every instrument with a sample, in order, so their numbers stay as near as they can */
    if (m.kind !== "XM") for (let i = 1; i < m.instruments.length; i++) want(i, 0);
    const nch = Math.min(16, m.channels);
    const insOf = slot => (Math.floor(slot / 64) << 8) | (slot % 64);
    const pats = m.patterns.map((p, pi) => {
        const cells = new Map(), lastKey = new Array(nch).fill(0), lastIns = new Array(nch).fill(0);
        for (let r = 0; r < Math.min(256, p.rows); r++) for (let c = 0; c < nch; c++) {
            const o = (r * p.channels + c) * 5;
            const key = p.data[o], ins = p.data[o + 1], v = p.data[o + 2], fx = p.data[o + 3], fp = p.data[o + 4];
            const cell = { note: 0, ins: -1, vol: -1, cmds: [] };
            let pan;
            if (key > 0 && key < 97) { cell.note = key + 11; lastKey[c] = key; }
            else if (key >= 97) cell.note = R.NOTE_OFF;
            if (ins > 0 && ins < m.instruments.length) {
                lastIns[c] = ins;
                const I = m.instruments[ins], k = key > 0 && key < 97 ? key : lastKey[c];
                const sam = I.samples.length > 1 ? I.keyToSample[k || 1] : 0;
                const slot = want(ins, sam);
                if (slot >= 0) cell.ins = insOf(slot);
                const smp = I.samples[sam];
                if (m.kind === "XM" && smp && smp.panning > 0 && cell.ins >= 0) pan = (smp.panning - 1) & 255;
            } else if (m.kind === "XM" && key > 0 && key < 97 && lastIns[c] && m.instruments[lastIns[c]].samples.length > 1) {
                /* a key alone on an XM instrument of several samples: the sample its key picks, named */
                const I = m.instruments[lastIns[c]], slot = want(lastIns[c], I.keyToSample[key]);
                if (slot >= 0) cell.ins = insOf(slot);
            }
            const vc = vcol(v);
            if (vc.vol !== undefined) cell.vol = vol127(vc.vol);
            if (vc.cmd) cell.cmds.push(vc.cmd);
            const e = effect(fx, fp, count);
            if (e && !cell.cmds.some(x => x[0] === e[0])) cell.cmds.push(e);
            if (pan !== undefined && !cell.cmds.some(x => x[0] === 0x08)) cell.cmds.push([0x08, pan]);
            if (cell.note || cell.ins >= 0 || cell.vol >= 0 || cell.cmds.length) cells.set(R.cellKey(r, c), cell);
        }
        if (p.rows > 256) warnings.push(`PATTERN ${pi}: ${p.rows} ROWS, THE FIRST 256 KEPT`);
        return { tracks: nch, rows: Math.min(256, p.rows), step: 1, cells };
    });
    const split = splitLong(pats, m.sequence, m.patterns.length, warnings);
    for (const n in dropped) warnings.push(`LEFT OUT: ${dropped[n]} × ${n}`);
    if (m.gain !== 64 && m.gain) warnings.push(`IBXM'S GAIN ${m.gain} (OF 64) IS NOT KEPT: AUTO LEVEL OR THE SONG'S MIXER EVENS IT`);
    /* the records: cartridges 0.., patterns 0.., song 0 (the import places them at free numbers) */
    const recs = [], ncart = Math.max(1, Math.ceil(insts.length / 64));
    if (insts.length > 64) warnings.push(`${insts.length} INSTRUMENTS: ${ncart} SAMPLER CARTRIDGES`);
    const base = R.cleanName(m.name || String(name || "").replace(/\.[^.]*$/, ""), 12) || "MODULE";
    for (let c = 0; c < ncart; c++) {
        const mine = insts.slice(c * 64, c * 64 + 64);
        const instruments = [], samples = [];
        mine.forEach((x, i) => {
            const s = x.sample, n = s.data.length;
            let baseNote = 60 - s.relNote, rate = m.c2Rate;
            while (baseNote > 127) { baseNote -= 12; rate *= 2; }
            while (baseNote < 0) { baseNote += 12; rate /= 2; }
            rate = Math.max(1, Math.min(262143, Math.round(rate)));
            const looped = s.loopLength > 0;
            const nm = R.cleanName(x.name, 10) || "SAMPLE " + (c * 64 + i + 1);
            samples.push({ length: n, rate, coding: s.bits === 16 ? 2 : 1, name: nm });
            instruments.push({ name: nm, level: 0, sample: i, start: 0, end: n, loop_start: looped ? s.loopStart : 0,
                loop_end: looped ? s.loopStart + s.loopLength : n, loop: looped ? (s.pingPong ? 2 : 1) : 0, base_note: baseNote,
                tune: Math.max(0, Math.min(255, 128 + s.fineTune)), volume: Math.min(64, s.volume),
                env: { r1: 99, r2: 99, r3: 99, r4: 99, l1: 99, l2: 99, l3: 99, l4: 0 } });
            recs.push({ kind: K.AUDIO, num: c, sub: i, ver: 1, payload: R.ptrEncode(s.data, s.bits) });
        });
        recs.push({ kind: K.SAMPLER, num: c, sub: 0, ver: 1, payload: R.packSampler({ name: ncart > 1 ? R.cleanName(base.slice(0, 9) + " " + (c + 1), 12) : base, instruments, samples }) });
    }
    split.pats.forEach((p, i) => R.packPattern(p).forEach((bytes, sub) => recs.push({ kind: K.PATTERN, num: i, sub, ver: 1, payload: bytes })));
    const song = R.newSong(R.cleanName(m.name || base, 16) || "MODULE");
    const order = split.positions;
    song.positions = order.length ? order.slice(0, 256) : [0];
    if (order.length > 256) warnings.push("MORE THAN 256 POSITIONS: THE FIRST 256 KEPT");
    song.tempo = Math.max(32, Math.min(300, m.tempo || 125));
    song.ticks = Math.max(1, Math.min(31, m.speed || 6));
    if (m.speed > 31) warnings.push(`A SPEED OF ${m.speed}: ${song.ticks} KEPT`);
    song.rpb = 4;
    song.flags = (m.linear ? R.SONG_LINEAR : 0) | (m.fastVol ? R.SONG_FAST_VOL : 0);   /* (AT THE END: LOOP, as modules do) */
    song.loopFrom = Math.min(split.posOf[Math.min(m.restart || 0, m.sequence.length)], song.positions.length - 1);
    song.gvol = Math.max(0, Math.min(64, m.gvol));
    for (let c = 0; c < nch; c++) song.mixer[c][1] = m.panning[c] !== undefined ? m.panning[c] & 255 : 128;
    recs.push({ kind: K.SONG, num: 0, sub: 0, ver: 1, payload: R.packSong(song) });
    const bytes = recs.reduce((n, r) => n + 16 + ((r.payload.length + 3) & ~3), 0);
    if (bytes > 348 * 1024) warnings.push(`IT TAKES ${Math.round(bytes / 1024)} KB: MORE THAN AN FM-1'S USER AREA HOLDS (ABOUT 348 KB); THE PREVIEW CANNOT PLAY IT`);
    const what = `${m.kind}: A SONG OF ${song.positions.length} POSITIONS, ${split.pats.length} PATTERNS OF ${nch} TRACKS, ${insts.length} SAMPLE INSTRUMENTS (${ncart} CARTRIDGE${ncart > 1 ? "S" : ""}), ${Math.round(bytes / 1024)} KB`;
    return { recs, what, warnings, module: m };
};
})(globalThis.ED = globalThis.ED || {});

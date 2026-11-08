/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground: LOOPS, a loop station (specs/ui/screens/loops.md).
 * - The length is always set (4 bars at first; GLO's LENGTH and TEMPO work with no loops); PLAY plays the loops, empty
 *   ones too (the METRONOME over the length).
 * - THE STAGE: one loop of the length, belonging to no loop, cycling with the loops and heard in the current instrument
 *   and mode. While the loops play the keys overdub it, quantized: a note replaces, over the span its key is held, the
 *   stage's notes on every lane (POLY; notes played together share the lanes) or on its lane (MONO); the rest of the cycle
 *   keeps its notes. LFO's tap switches to OVERWRITE and back: the line erases all it passes. REC (at its press) sends
 *   the stage where the selection is (+ NEW LOOP: a new loop; a loop: its notes replaced, instrument and mode written,
 *   SHIFT 0) and empties it; empty, REC blinks; stopped and empty, REC arms (a note or PLAY starts); for 500 ms after REC
 *   does a job it does nothing. EDIT copies the selected loop (as it plays, and its sound) onto the stage, in OVERDUB; on
 *   + NEW LOOP, or held, it empties the stage (SEL's CLEAR STAGE too), and the stage is on. The loop selected is silent while the stage holds notes
 *   (STAGED); moving the selection keeps the stage. ARP's tap switches the stage off and on (off: kept, dimmed, not
 *   heard, the loop selected heard; the keys sound at once in its sound, written nowhere). ARP held + REC (or SEL's
 *   RECORD PATTERN) opens the pattern recorder, which writes onto the stage.
 * - ALGORITHM is the play position (a row a click, SEL held a bar): playing, the loops move at once; stopped, PLAY starts
 *   there; STOP (PLAY while playing) and PLAY held put it at the start.
 * - 4 loops in view (SELECT scrolls), the 16 in a column at the right; VIEW 1, 2 or 4 bars a screen; a note's attack
 *   cell and the release cells after it dim, the sustain full; bar lines; the stage in a frame of its own.
 * - K1-K4 each a job from GLO; ENV held (the pipette) and SEL's TAKE ITS INSTRUMENT (with its MONO / POLY); SEQ held +
 *   keys as in CONTROL mode; SEL's ADJUST (a SHIFT a loop keeps, no note moved), DELETE LOOP, QUANTIZE.
 * - RECORD LIVE, SAVE TO / LOAD FROM PATTERN and CONTROL mode's mutes and solos work on the loops as they are. */
(function () {
    "use strict";
    const D = PTFM.draw, F = D.F, M = PTFM.model, U = PTFM.ui, B = U.B, E = U.E, P = PTFM.player;
    const Y0 = U.BODY_Y;
    const GRIDS = [["1/16", 4], ["1/16T", 6], ["1/32", 8]];
    const LS = () => M.state.loops, list = () => LS().list;
    /* sel: the selection (SAVE writes it into the record; of: the record it came from); top: the first slot in view;
     * control / solo: the keys' mode, CONTROL's MUTE or SOLO; pend: mutes and solos waiting for the bar line ("i/mute":
     * {i, kind, due}); arm: stopped, waiting for a note or PLAY ({wait: true}); song: RECORD LIVE armed or recording (null:
     * none; below), with pdefer / phold (PLAY pressed while it records: acted on as it is let go; its hold came), sat (its
     * row heard last), hrow (the row HOME was pressed in, null none); free: keys playing freely; held: CONTROL's
     * white keys down (a hold waits for the bar line); adj: ADJUST open {L, i, was}; startRow: the row the loops started at;
     * recAt: when REC last did a job (for 500 ms after it REC does nothing) */
    const lp = { sel: 0, top: 0, of: null, control: false, solo: false, pend: new Map(), arm: null, song: null,
                 pdefer: false, phold: false, sat: 0, hrow: null, free: {}, held: {}, adj: null, startRow: 0, recAt: -1e9 };
    /* REC's wait (specs/ui/screens/loops.md#rec-edit-and-the-arm): for 500 ms after REC does a job on LOOPS (sends, arms,
     * disarms, cancels a count-in, the pattern recorder's pattern onto the stage, RECORD LIVE armed or started) it does
     * nothing, no blink, no toast; a refused REC starts no wait; REC with LFO held is outside it */
    const REC_QUIET = 500;
    const recQuiet = () => U.now() - lp.recAt < REC_QUIET;
    const recDone = () => { lp.recAt = U.now(); };
    /* THE STAGE: one loop of the length (it belongs to no loop), cycling with the loops and heard in the current
     * instrument and mode. cells: its notes (rows x 4 lanes; null: empty); a cell written by an overdub carries wp, the pass
     * it was written in (its own: no span of that pass replaces it), and sub while a note asked for at its row stands in
     * for it (the pass: the stage does not play it then, that once). keys: each key down: its note (rec: lane, row, pass,
     * held, erasedTo: the rows its span has replaced so far; null once it stopped writing); silent: the loop silent (the
     * selected one while the stage holds notes: silentNow; -1 none), as last seen; setup: the instrument and mode the
     * keys play; erasers: OVERWRITE's; over: OVERWRITE on (LFO's tap; EDIT's and the pattern recorder's fills end it; off
     * at every power-on, never saved); jumps: the last jump of the loops seen; off: the stage
     * switched off (ARP's tap; on at every power-on, never saved); ol: the 16 lanes of the keys played while it is off
     * (each null, or the key it holds and whether MONO), onext the lane an overflow takes. Kept in the edit memory as a
     * piece of its own, never saved (M.stagePiece) */
    const OFF_LANES = 16;
    const st = { cells: null, keys: {}, silent: -1, setup: null, run: 0, line: -1, erasers: [], over: false, jumps: 0, off: false,
                 ol: Array(OFF_LANES).fill(null), onext: 0 };

    /* --------------------------------------------------------------- time --- */
    const beats = () => ((LS().fx.knobs || {}).METRONOME || [])[1] || 4;    /* the METRONOME's BEATS A BAR */
    const barRows = () => LS().rpb * beats();
    const maxBars = () => Math.max(1, Math.floor(M.PAT_ROWS_MAX / barRows()));   /* (128 rows: 8 bars at 1/16) */
    const maxRows = () => maxBars() * barRows();
    const barsText = b => b + (b === 1 ? " BAR" : " BARS");
    /* the length as shown: whole bars (`4 BARS`, `1 BAR`); a part bar (BEATS A BAR changed under it) in its beats, whole
     * as the length's rows always are (`21 BEATS`) */
    const lengthText = rows => rows % barRows() === 0 ? barsText(rows / barRows())
                             : rows % LS().rpb === 0 ? rows / LS().rpb + (rows === LS().rpb ? " BEAT" : " BEATS") : rows + " ROWS";
    const playingHere = () => P.playing && P.what === "loops";
    /* the loops heard now (tm: at that time instead, a moment ago): their row, pass, the replay's row count (n), place in
     * rows (pos), where in the row (frac) and tick, the jumps landed so far and the last one's landing {row, pass}; null
     * while they do not play (or before their first row is heard) */
    function here(tm) {
        if (!playingHere() || P.lpRestState().on) return null;   /* (silent for a song: not playing, the stage's line standing) */
        const h = P.at(tm);
        if (!h || h.pass == null) return null;
        const frac = U.clamp(((tm == null ? P.time() : tm) - h.t) / h.dur, 0, .999);
        return { row: h.row, pass: h.pass, n: h.n, pos: h.row + frac, frac, tick: Math.min(LS().ticks - 1, Math.floor(frac * LS().ticks)),
                 jumps: h.jumps || 0, land: h.land || null };
    }
    /* the row playing (the strips' play line; silent for a song: their place), else where PLAY starts (the cursor) */
    function playRow() {
        const n = LS().rows, rs = P.lpRestState(), h = playingHere() ? P.at() : null;
        if (h && h.pass != null && (rs.on === 1 || rs.on === 2)) return n ? rs.kr % n : 0;
        return h && h.pass != null ? (n ? h.row % n : h.row) : (P.kept.loops.row || 0) % Math.max(1, n);
    }
    /* a row as BAR.BEAT (.STEP within the beat when not on it) */
    const barBeat = r => { const bR = barRows(), rpb = LS().rpb, b = Math.floor(r / bR) + 1, k = Math.floor((r % bR) / rpb) + 1, s = r % rpb;
                           return b + "." + k + (s ? "." + (s + 1) : ""); };

    /* the player's side (player.js, LOOPS' lanes): the rows its replay goes round (the preview alone: its own), the loop
     * that is silent, the header's ● (armed, or a song) */
    P.lpRows = r => r && r.alone ? pvRows() : LS().rows;
    /* the selected loop is silent while the stage holds notes (the stage is what REC would put in its place); with an
     * empty stage it sounds, and so it does while the stage is off. syncSilent: a loop falling silent lets its notes go at
     * once */
    const silentNow = () => selLoop() && stageHas() && !st.off ? lp.sel : -1;
    P.lpSilent = i => silentNow() === i;
    function syncSilent() {
        const s = silentNow();
        if (s !== st.silent && s >= 0) P.lpEnd(s);
        st.silent = s;
    }
    P.lpRecording = () => !!lp.arm || songOn();

    /* ------------------------------------------------------------- the loops --- */
    const selLoop = () => list()[lp.sel] || null;              /* null: the placeholder, + NEW LOOP */
    const topSel = () => Math.min(list().length, M.LOOPS_MAX - 1);
    const slots = () => list().length + (list().length < M.LOOPS_MAX ? 1 : 0);   /* (the loops and + NEW LOOP) */
    function syncSel() {
        const j = LS();
        if (lp.of !== j) { lp.of = j; lp.sel = j.sel || 0; lp.pend.clear(); st.silent = -1; }   /* (power-on, REVERT: as saved; the stage stays.
                                                         * A record from before has its new fields filled where it is loaded:
                                                         * M.loopsFilled) */
        lp.sel = U.clamp(lp.sel, 0, topSel());
        lp.top = Math.min(U.clamp(lp.top, lp.sel - 3, lp.sel), Math.max(0, slots() - 4));
    }
    /* the mixers past the last loop: the next loop's mix (the stage on + NEW LOOP and the preview sound through it) */
    function syncMix(j) { for (let k = j.list.length; k < 16; k++) j.mixer[k] = Object.assign({}, j.nextMix); }
    const nextCol = () => { const L = list(); return L.length ? (L[L.length - 1].col + 1) % 12 : 0; };
    const colOf = L => M.LOOP_COLS[L.col % 12];
    const anySolo = () => list().some(l => l.soloed);
    const silenced = L => anySolo() ? !L.soloed : L.muted;
    const name = i => "LOOP " + M.hex(i);
    const isNote = c => !!c && c.note != null && c.note !== M.OFF;
    const cmdOf = (c, code, sub) => { const x = c.cmds.find(y => y.c === code && (sub == null || y.v >> 4 === sub)); return x ? x.v : null; };
    /* a change of the loops refused: while counting in */
    function refused(btn) {
        if (P.countingIn()) { U.refuse(btn); return true; }
        return false;
    }

    /* ------------------------------------------------------------- the stage --- */
    /* where REC sends the stage: the loop selected, or a new one (its number: the loops' count) */
    const tgt = () => selLoop() ? lp.sel : list().length;
    const setupNow = () => U.inst + "/" + (LS().nextMono ? "M" : "P");
    const isMono = () => !!LS().nextMono;
    const stageHas = () => !!st.cells && st.cells.some(isNote);
    const usedLanes = cells => { let n = 1; cells.forEach((c, k) => { if (c) n = Math.max(n, (k & 3) + 1); }); return n; };
    /* the stage as the player and the strips read a loop: its base the length, once, in the current instrument, through
     * the mix REC puts it in */
    const stageLoop = cells => ({ inst: U.inst, base: LS().rows, repeats: 1, cells, lanes: usedLanes(cells), mono: isMono(), track: tgt(), shift: 0 });
    function stageCells() {
        fitStage();
        if (!st.cells) st.cells = new Array(LS().rows * 4).fill(null);
        return st.cells;
    }
    const liveKeys = () => Object.values(st.keys).filter(k => k.rec && k.rec.held && k.live);
    /* the lanes a key holds: MONO its one; POLY its own in OVERDUB, all four in OVERWRITE (and OVERWRITE's eraser) */
    const lanesOf = rec => rec.mono ? [0] : rec.lane == null || overwriteOn() ? [0, 1, 2, 3] : [rec.lane];
    /* the stage row `row` of pass q plays (asked as the replay schedules it, a moment ahead): a note asked for at that row
     * stands in for its cell that once (heard once as played, then from the stage each time the line reaches it); while
     * a key is held, what its span replaces (POLY every lane, MONO its own) is not heard from its row on. Not heard while
     * it is off */
    P.lpStagePlay = (q, row) => {
        if (P.what !== "loops" || st.run !== P.lpRun() || st.off || !st.cells || st.cells.length !== LS().rows * 4) return null;
        const held = liveKeys().map(k => k.rec).concat(st.erasers);
        const cells = stageNow(st.cells).map((c, k) => {
            if (!c) return null;
            const r = k >> 2, l = k & 3;
            if (r === row && c.sub === q) return null;
            for (const h of held)
                if (lanesOf(h).includes(l) && (h.pitch == null || c.note === h.pitch) && (h.pass < q || (h.pass === q && r >= h.row))) return null;
            return c;
        });
        for (let l = 0; l < 4; l++) { const c = st.cells[row * 4 + l]; if (c && c.sub === q) delete c.sub; }   /* (stood in for: done) */
        return stageLoop(cells);
    };
    /* the keys' notes sound live in the current instrument, through the mix REC puts it in */
    P.lpNowLoop = () => ({ inst: U.inst, track: tgt(), base: LS().rows, repeats: 1, cells: [] });
    /* the keys down stop writing whenever the stage's rows or sound change under them (SELECT, a new instrument or mode,
     * REC, EDIT, the pattern recorder's REC, a length change, FX, leaving LOOPS, a stop, a jump): each note ends as when
     * its key is let go, its span what the line passed (the loops not playing: its sound alone ends); its key let go
     * later does nothing more */
    function stopWriting() {
        const h = here();
        for (const n of Object.keys(st.keys)) {
            const k = st.keys[n];
            if (k.rec && k.live && h) recKeyUp(+n);
            else if (k.rec) P.lpNote(P.LP_NOW, k.rec.lane, M.newCell(M.OFF, null, null), 0, -1, +n);
            st.keys[n] = { rec: null, live: false };
        }
        st.cur = null;
    }
    /* the stage emptied (and what of it sounds let go); the selected loop heard again */
    function clearStage() {
        stopWriting();
        st.cells = null;
        st.erasers = [];
        if (P.lpStageMute) P.lpStageMute();
        st.silent = -1;
        M.version++;
    }
    /* a new run of the loops' replay (PLAY): its passes counted from 0; nothing held (OVERWRITE starts again at the line,
     * overdubTick) */
    function syncRun() {
        const r = P.lpRun();
        if (st.run !== r) { st.run = r; st.line = -1; st.jumps = 0; st.erasers = []; for (const n in st.keys) st.keys[n] = { rec: null, live: false }; }
    }
    /* a change of the instrument (PRESETS, ENV, the pipette) or MONO / POLY: the stage's notes kept, played in the new
     * sound (a POLY stage in MONO: one note at a time); the keys down stop writing */
    function checkSetup() {
        const s = setupNow();
        if (st.setup !== s) { if (st.setup != null) stopWriting(); st.setup = s; }
    }
    /* the stage in the edit memory (specs/files/ram.md#the-stage-in-the-edit-memory): a piece of its own while it holds
     * notes, encoded as a loop's stream (at most 3,800 B), never saved and no change. A change of it refused says why:
     * the store busy (a save being written), the stream past its most, the edit memory full (extra: bytes kept for a
     * note's end, which then always fits) */
    const STAGE_MOST = 3800;
    /* a stream's bytes as the firmware encodes it (play/loops.c's enc_put): each cell its head, its note, its volume, its
     * commands (2-4 with their count's 2 bytes before them), a long skip's byte (7 rows or more since the cell before); a
     * gap past 255 rows bridged (an ED0 alone: 4 bytes); its 0x00 at the end. A cell with nothing in it is not stored */
    const streamOf = cells => {
        let n = 1, prev = -1;
        cells.forEach((c, k) => {
            if (!c || (c.note == null && c.vol == null && !c.cmds.length)) return;
            while (k - prev - 1 > 255) { n += 4; prev += 256; }
            const nc = Math.min(4, c.cmds.length);
            n += 1 + (c.note != null ? 1 : 0) + (c.vol != null ? 1 : 0) + (nc > 1 ? 2 : 0) + 2 * nc + (k - prev - 1 >= 7 ? 1 : 0);
            prev = k;
        });
        return n;
    };
    const pieceOf = cells => cells && cells.some(isNote) ? M.pieceRoom(streamOf(cells)) : 0;
    M.stagePiece = () => pieceOf(st.cells);
    function stageRefusal(cells, extra = 0) {
        if (U.saving) return "STAGE NOT WRITTEN: STORAGE BUSY";
        if (cells && cells.some(isNote) && streamOf(cells) + extra > STAGE_MOST) return "THE STAGE IS FULL";
        if (M.editsWithout() + (cells && cells.some(isNote) ? M.pieceRoom(streamOf(cells) + extra) : 0) > M.EDITS_MAX) return "MEMORY FOR EDITS IS FULL";
        return null;
    }
    /* a power cycle drops it, and the stage is on again */
    M.onPowerOn = () => { st.keys = {}; st.cur = null; st.cells = null; st.erasers = []; st.over = false; st.silent = -1; lp.arm = null;
                          st.off = false; st.ol = Array(OFF_LANES).fill(null); st.onext = 0; };
    /* where a note lands by QUANTIZE: the nearest line of Q rows (halves up), waiting for it when it is
     * ahead; REAL TIME the row it falls in, its tick a note delay, at once */
    function landing(pos, tick) {
        const q = LS().quantize;
        if (q === "REAL TIME") return { row: Math.floor(pos), delay: Math.min(15, tick), wait: false };
        const Q = { "2 ROWS": 2, "3 ROWS": 3, "4 ROWS": 4 }[q] || 1, L = Math.floor(pos / Q + .5) * Q;
        return { row: L, delay: 0, wait: L > pos };
    }
    /* THE OVERDUB (the keys while the loops play): a note goes on the stage at the row it lands in, and while its key is
     * held it replaces what the stage had over its span: POLY every lane (notes played together share the lanes, 4 at
     * most), MONO its one lane. Rows not played keep what they had, so each cycle can fix parts. A key held at the line is
     * released there and goes on as pressed again at the next pass's first row */
    const ownCell = (c, pass) => !!c && c.wp === pass;     /* (written by this pass's overdub: never replaced by it) */
    const offCell = pass => Object.assign(M.newCell(M.OFF, null, null), { wp: pass });
    /* lane l's note from before row r still sounding there (not this pass's own): its cell, else null */
    function soundingAt(cells, l, r, pass) {
        for (let k = r - 1; k >= 0; k--) {
            const c = cells[k * 4 + l];
            if (!c) continue;
            return !ownCell(c, pass) && isNote(c) && cmdOf(c, 0x14) == null ? c : null;
        }
        return null;
    }
    const soundsAt = (cells, l, r, pass) => !!soundingAt(cells, l, r, pass);
    /* rows [a, b) of the stage replaced on these lanes: what was there goes (pitch: the notes of that pitch alone), this
     * pass's own stays */
    function replaceRows(a, b, lanes, pass, pitch) {
        const cells = stageCells();
        for (let r = Math.max(0, a); r < Math.min(b, LS().rows); r++) for (const l of lanes) {
            const c = cells[r * 4 + l];
            if (c && !ownCell(c, pass) && (pitch == null || (isNote(c) && c.note === pitch))) cells[r * 4 + l] = null;
        }
    }
    /* OVERWRITE at work, in place of OVERDUB (st.over): on LOOPS itself, not with the stage off, in ADJUST or the pattern
     * recorder. The play line erases everything on the stage it passes, every lane (notes starting there removed, a note
     * running into it cut where the line came in; what of the stage sounds there stops), while the notes played meanwhile
     * are written: that stretch becomes what is played, silence included. An eraser running with the line (st.erasers,
     * "LFO"); stopped, nothing (the line does not move) */
    const overwriteOn = () => st.over && U.screen === U.screens.LOOPS && !st.off && !lp.adj && !prOpen();
    /* OVERDUB (specs/ui/screens/loops.md#the-stage): a key holds its own lane alone and what was there stays: a note
     * sounding where it lands is cut there, a note starting while it is held has its start moved on with the key, put down
     * where it is let go (one it covers whole gone). OVERWRITE: a key clears every lane over its span, as ever */
    const dub = () => !overwriteOn();
    /* the row of the next cell on lane l after row r (not this pass's own), the length if none */
    function laneNext(cells, l, r, pass) {
        for (let k = r + 1; k < LS().rows; k++) { const c = cells[k * 4 + l]; if (c && !ownCell(c, pass)) return k; }
        return LS().rows;
    }
    /* key rec's lane (OVERDUB) from erasedTo to `to`: what was there cleared (this pass's own kept), a note starting there
     * moved on to `to` (a later one starting there: the earlier covered whole; a note's end there: it covered whole),
     * unless a cell is at `to` already or `to` is the length's end (covered whole) */
    function dubOn(rec, to) {
        const cells = stageCells(), rows = LS().rows, l = rec.mono ? 0 : rec.lane;
        to = Math.min(to, rows);
        if (to <= rec.erasedTo) return;
        let p = null;
        for (let r = rec.erasedTo; r < to; r++) {
            const c = cells[r * 4 + l];
            if (!c || ownCell(c, rec.pass)) continue;
            if (isNote(c)) p = c;
            else if (c.note != null || cmdOf(c, 0x14) != null) p = null;   /* (its end) */
        }
        replaceRows(rec.erasedTo, to, [l], rec.pass);
        rec.pushed = false;
        if (p && to < rows && !cells[to * 4 + l]) { cells[to * 4 + l] = p; rec.pushed = true; }
    }
    function startEraser(pitch, key) {
        const h = here();
        if (!h || !stageHas()) return;
        syncRun();
        const row = Math.floor(h.pos), pass = h.pass, cells = stageCells();
        replaceRows(row, row + 1, [0, 1, 2, 3], pass, pitch);
        for (let l = 0; l < 4; l++) {
            const c = soundingAt(cells, l, row, pass);
            if (c && (pitch == null || c.note === pitch) && !cells[row * 4 + l]) cells[row * 4 + l] = offCell(pass);
        }
        st.erasers.push({ pitch, key, pass, row, erasedTo: row + 1, mono: false });
        P.lpStageCut([0, 1, 2, 3], pass, -1);             /* (the stage's sounding notes there stopped, at once) */
        M.version++;
    }
    /* key n's note m on the stage at row (delay: REAL TIME's ticks) in pass `pass`, heard live at that row (at: -1 at
     * once): the row replaced on its lanes, a note from before sounding into it cut there; MONO legato as MONO LEGATO
     * says; what the stage plays on the lanes it replaces silenced from that row (the other lanes and the rows before play
     * on). Not taken (a fifth POLY note; one the stage has no room for: why, in the header): neither heard nor written
     * (null) */
    function writeNote(n, m, row, delay, pass, at) {
        const j = LS(), mono = isMono(), dubbing = dub();
        const busy = liveKeys().map(k => k.rec).filter(r => !r.mono).map(r => r.lane);
        let lane = mono ? 0 : [0, 1, 2, 3].find(l => !busy.includes(l));
        if (!mono && dubbing) {                            /* OVERDUB: a lane free there, the one free longest; none free,
                                                            * the one whose note ends soonest (cut there) */
            const cs = st.cells ? stageCells() : null;     /* (none yet: every lane free) */
            let best = -1;
            lane = null;
            for (let l = 0; l < 4; l++) {
                if (busy.includes(l)) continue;
                const c = cs && cs[row * 4 + l], nxt = cs ? laneNext(cs, l, row, pass) : LS().rows, here_ = isNote(c) && !ownCell(c, pass);
                const v = !here_ && !(cs && soundsAt(cs, l, row, pass)) ? 512 + nxt : 256 - nxt;
                if (v > best) { best = v; lane = l; }
            }
        }
        if (lane == null) return null;
        const lanes = mono ? [0] : dubbing ? [lane] : [0, 1, 2, 3];
        const was = st.cells;
        if (mono && st.cells && usedLanes(st.cells) > 1) st.cells = monoOf(st.cells);   /* (a POLY stage overdubbed in MONO: as it plays) */
        st.cells = stageCells().slice();
        const cells = st.cells;
        const cell = Object.assign(M.newCell(m, null, U.set("KEYS VOLUME")), { wp: pass });
        if (delay) cell.cmds.push({ c: 0x0E, v: 0xD0 | delay });
        let slide = false;
        if (mono) {
            const cur = st.cur;
            if (cur && cur.held && (cur.row !== row || cur.pass !== pass) && j.legato !== "JUMP") {
                if (M.instType(U.inst) === "TB303") {      /* a TB-303 slides: SLD on the note held */
                    const c = cur.pass === pass ? cells[cur.row * 4] : null;
                    if (isNote(c) && !c.cmds.some(x => x.c === 0xC1 || x.c === 0xC2)) {
                        const d = cells[cur.row * 4] = Object.assign(M.copyCell(c), { wp: c.wp }), acc = d.cmds.findIndex(x => x.c === 0xC0);
                        if (acc >= 0) d.cmds[acc] = { c: 0xC2, v: 0 }; else if (d.cmds.length < 4) d.cmds.push({ c: 0xC1, v: 0 });
                    }
                    slide = true;
                } else if (j.legato === "LEGATO") cell.cmds.push({ c: 0xD3, v: 0 });
                else cell.cmds.push({ c: 0x03, v: 0x20 });  /* (a glide, 2 semitones a tick) */
            }
        }
        let pushed = false;                                /* (OVERDUB: a note from before starting there, moved on a row) */
        if (dubbing) {
            const p = cells[row * 4 + lane];
            if (isNote(p) && !ownCell(p, pass) && row + 1 < LS().rows && !cells[(row + 1) * 4 + lane]) { cells[(row + 1) * 4 + lane] = p; pushed = true; }
            replaceRows(row, row + 1, lanes, pass);
        } else {
            replaceRows(row, row + 1, lanes, pass);
            for (const l of lanes) if (l !== lane && !busy.includes(l) && soundsAt(cells, l, row, pass)) cells[row * 4 + l] = offCell(pass);
        }
        cells[row * 4 + lane] = cell;
        const why = stageRefusal(cells, 3);
        if (why) { st.cells = was; U.toast(why); return null; }
        const rec = { lane, row, delay, pass, held: true, mono, note: m, erasedTo: row + 1, pushed };
        if (mono) st.cur = rec;
        if (slide) P.lpSlide(P.LP_NOW, 0);
        P.lpStageCut(lanes, pass, at);
        if (at < 0) landAt(lane, row, delay);
        if (P.lpNote(P.LP_NOW, lane, cell, pass, at, n)) cell.sub = pass;   /* (asked for at its row: it stands in for it there) */
        P.lpLand(4 * P.LP_NOW + lane, null);
        M.version++;
        return rec;
    }
    /* a song recording takes the stage's keys as they land: what is heard now on lane l is written at (row, tick) of the
     * pass heard */
    function landAt(l, row, tick) {
        const h = here(), sp = LS().ticks;
        if (h && songOn()) P.lpLand(4 * P.LP_NOW + l, P.lpTn() - (h.row * sp + h.tick - (row * sp + tick)));
    }
    /* the held keys' spans grow with the play position: what they pass is replaced. OVERWRITE goes on from where a jump
     * lands */
    function overdubTick() {
        if (playingHere()) syncRun();                      /* (a new run: OVERWRITE starts again where they start) */
        /* OVERWRITE: its eraser runs while the mode is on and the loops play (started where the line is), gone with it */
        if (!overwriteOn()) st.erasers = st.erasers.filter(e => e.key !== "LFO");
        else if (playingHere() && !st.erasers.some(e => e.key === "LFO")) startEraser(null, "LFO");
        const h = here();
        if (h && h.jumps !== st.jumps) {                   /* (the loops landed from a jump) */
            st.jumps = h.jumps;
            if (h.land) for (const e of st.erasers) Object.assign(e, { pass: h.land.pass, row: h.land.row, erasedTo: h.land.row });
        }
        if (!h || !st.cells) return;
        for (const r of liveKeys().map(k => k.rec).concat(st.erasers)) {
            const to = Math.floor(h.pos) + 1;
            if (r.pass !== h.pass || to <= r.erasedTo || (r.mono && st.cur !== r)) continue;
            if (r.key == null && dub()) dubOn(r, to);      /* (OVERDUB: its lane, a note there moved on with it) */
            else replaceRows(r.erasedTo, to, lanesOf(r), r.pass, r.pitch);
            r.erasedTo = to;
            M.version++;
        }
    }
    /* a key down while the loops play: on the stage where it lands (past the length: the next pass's first rows); what the
     * stage plays on the lanes it replaces is let go from that row; the loop selected silent once the stage holds notes */
    function recKeyDown(n, m) {
        syncRun();
        checkSetup();
        overdubTick();
        const len = LS().rows, h = here();
        const hp = h ? h.pass : 0, at = landing(h ? h.pos : lp.startRow, h ? h.tick : 0);
        let row = at.row, pass = hp;
        if (row >= len) { row -= len; pass++; }
        const rec = writeNote(n, m, row, at.delay, pass, at.wait || pass !== hp ? row : -1);
        st.keys[n] = { rec, live: !!rec };
        syncSilent();                                      /* (the stage holds notes now: the selected loop silent) */
    }
    /* a key let go: its span replaced up to its end, its note off on the nearest row, at least a row after the note (REAL
     * TIME a key off at its tick); past the length nothing is written (the note ends at the line: a note off at row 0
     * where that lane is free). A MONO key ends its note only while it has the lane. A key no longer writing: its sound
     * ends now */
    function recKeyUp(n) {
        const k = st.keys[n];
        delete st.keys[n];
        if (!k || !k.rec) return;
        const rec = k.rec, off0 = M.newCell(M.OFF, null, null);
        rec.held = false;
        if (rec.mono && st.cur !== rec) return;
        if (rec.mono) st.cur = null;
        const h = here(), len = LS().rows;
        if (!k.live || !st.cells || (h && h.pass !== rec.pass && h.pass !== rec.pass - 1)) { P.lpNote(P.LP_NOW, rec.lane, off0, 0, -1, n); return; }
        let pos = h ? h.pos : 0;
        if (h && h.pass === rec.pass - 1) pos -= len;      /* (its note in the next pass: before its start) */
        let row, cell;
        if (LS().quantize === "REAL TIME") {
            const tick = h ? h.tick : 0;
            row = Math.floor(pos);
            if (row < rec.row || (row === rec.row && tick <= rec.delay)) row = rec.row + 1;
            cell = Object.assign(M.newCell(null, null, null), { wp: rec.pass });
            cell.cmds.push({ c: 0x14, v: tick });
        } else {
            row = Math.max(Math.floor(pos + .5), rec.row + 1);
            cell = offCell(rec.pass);
        }
        const cells = st.cells;
        if (dub() && !(h && h.pass === rec.pass - 1)) {   /* OVERDUB: its lane to the end, a note moved on with it put down at
                                                            * its end (let go in the row before: moved back to it) */
            const l = rec.lane;
            let moved = rec.pushed;
            if (row >= rec.erasedTo) { dubOn(rec, row); moved = rec.pushed; }
            else if (moved && row > rec.row && cells[rec.erasedTo * 4 + l]) { cells[row * 4 + l] = cells[rec.erasedTo * 4 + l]; cells[rec.erasedTo * 4 + l] = null; }
            else moved = false;
            rec.pushed = false;
            M.version++;
            if (moved && row < len) {                       /* (the note moved there ends this one) */
                if (row <= pos) landAt(l, row, 0);
                P.lpNote(P.LP_NOW, l, off0, rec.pass, row > pos ? row : -1, n);
                P.lpLand(4 * P.LP_NOW + l, null);
                return;
            }
        } else {
            rec.pushed = false;
            replaceRows(rec.erasedTo, Math.min(row, len), lanesOf(rec), rec.pass);
        }
        M.version++;
        if (row >= len) {
            const fresh = !cells[rec.lane];
            if (fresh) cells[rec.lane] = offCell(rec.pass);
            if (P.lpNote(P.LP_NOW, rec.lane, off0, rec.pass + 1, 0, n) && fresh) cells[rec.lane].sub = rec.pass + 1;
            return;
        }
        const there = cells[row * 4 + rec.lane];
        if (cell.note == null && row === rec.row && there && !there.cmds.some(x => x.c === 0x14)) {   /* (let go in its note's row) */
            if (there.cmds.length < 4) there.cmds.push(cell.cmds[0]);
            P.lpNote(P.LP_NOW, rec.lane, cell, rec.pass, -1, n);
            return;
        }
        const put = !isNote(there);
        if (put) {                                         /* (a note there ends it) */
            if (there && cell.note == null) { if (there.cmds.length < 4) there.cmds.push(cell.cmds[0]); cell = there; }
            cells[row * 4 + rec.lane] = cell;
        }
        if (row <= pos) landAt(rec.lane, row, h && LS().quantize === "REAL TIME" ? h.tick : 0);
        if (P.lpNote(P.LP_NOW, rec.lane, cell.note == null && !cell.cmds.length ? off0 : cell, rec.pass, row > pos ? row : -1, n) && put) cell.sub = rec.pass;
        P.lpLand(4 * P.LP_NOW + rec.lane, null);
    }
    /* the line of pass p heard (the loops' row 0 of pass p): a key still held stops writing there: its note ends at the
     * line (its span replaced to the end, a note off at row 0 where the lane is free, its sound let go) and the key is
     * forgotten (nothing written for it in the next pass, its release does nothing), so its note plays again where it
     * was written */
    function lineAt(p) {
        syncRun();
        if (st.line >= p) return;
        st.line = p;
        if (!st.cells) return;
        const len = LS().rows;
        for (const key of Object.keys(st.keys)) {
            const k = st.keys[key], r = k.rec;
            if (!k.live || !r || !r.held || r.pass !== p - 1 || (r.mono && st.cur !== r)) continue;
            replaceRows(r.erasedTo, len, lanesOf(r), r.pass);   /* (a note moved on with it: covered whole) */
            r.pushed = false;
            if (!st.cells[r.lane]) st.cells[r.lane] = offCell(p);
            P.lpNote(P.LP_NOW, r.lane, M.newCell(M.OFF, null, null), 0, -1, +key);
            r.held = false;
            if (r.mono) st.cur = null;
            st.keys[key] = { rec: null, live: false };      /* (forgotten: its release does nothing) */
        }
        for (const e of st.erasers) {                      /* (an eraser held on: into the next pass from its first row) */
            if (e.pass !== p - 1) continue;
            replaceRows(e.erasedTo, len, [0, 1, 2, 3], e.pass, e.pitch);
            Object.assign(e, { pass: p, row: 0, erasedTo: 0 });
        }
        M.version++;
    }
    /* MONO: one note at a time over all the lanes (the later of overlapping notes takes over, on lane 0; a note's end kept
     * while it is the one sounding): how a POLY stage switched to MONO plays, and how REC writes it */
    function monoOf(cells) {
        const n = cells.length >> 2, out = new Array(cells.length).fill(null);
        let from = -1;                                     /* (the lane the note sounding came from) */
        for (let r = 0; r < n; r++) {
            let note = null, end = null;
            for (let l = 0; l < 4; l++) {
                const c = cells[r * 4 + l];
                if (!c) continue;
                if (isNote(c)) { note = c; from = l; }
                else if (l === from && !note) end = c;
            }
            const keep = c => Object.assign(M.copyCell(c), c.wp != null ? { wp: c.wp } : {}, c.sub != null ? { sub: c.sub } : {});   /* (an overdub's marks kept) */
            if (note) out[r * 4] = keep(note);
            else if (end) { out[r * 4] = keep(end); from = -1; }
        }
        return out;
    }
    /* the stage as it plays now (MONO: one note at a time) */
    const stageNow = cells => isMono() && usedLanes(cells) > 1 ? monoOf(cells) : cells;
    /* a length changed: the stage cut at the new end (a note sounding there ends at the line) or longer with empty rows */
    function fitStage() {
        const n = LS().rows;
        if (!st.cells || st.cells.length === n * 4) return;
        const old = st.cells, held = old.length > n * 4 ? heldAt({ cells: old, base: old.length >> 2, repeats: 1 }, n) : 0;
        st.cells = old.slice(0, n * 4).concat(new Array(Math.max(0, n * 4 - old.length)).fill(null));
        stopAt(st.cells, held, 0);
    }
    /* REC: the stage sent to LOOPS at its press, where the selection is: on + NEW LOOP a new loop (the current instrument
     * and mode, the next colour, the next loop's mix as heard, its sends the KEYS column's; the selection on + NEW LOOP
     * again), on a loop its notes replaced
     * (the instrument and mode written, SHIFT back to 0); the stage emptied, what of it sounds going on as the loop's;
     * an arm ends with it. An empty stage: REC blinks; stopped, it arms (again: disarms). True: a job done */
    function recPress() {
        if (lp.adj) { U.refuse(B.REC); return false; }
        if (stageHas()) return send();
        if (playingHere()) { U.refuse(B.REC); U.toast("THE STAGE IS EMPTY"); return false; }
        if (lp.arm) { lp.arm = null; U.toast("CANCELLED"); return true; }
        lp.arm = { wait: true };
        return true;
    }
    /* what REC writes from the stage: what was heard. No wrap for the notes played onto it (an overdub's cell, wp: one
     * sounding at the length's end ends at the line); the notes EDIT brought as they are (one crossing the line keeps
     * its end past it); no note off or key off that ends nothing (its lane silent there, counted round from the length's
     * end). The stage's marks (wp, sub) are not written */
    function sentCells(cells, rows) {
        let wrap = 0;                                      /* (the lanes whose note at the end was played onto the stage) */
        for (let l = 0; l < 4; l++) {
            let on = null;
            for (let r = 0; r < rows; r++) { const c = cells[r * 4 + l]; if (c) on = !sounding(!!on, c) ? null : isNote(c) ? c : on; }
            if (on && on.wp != null) wrap |= 1 << l;
        }
        stopAt(cells, wrap, 0);
        const held = heldAt({ cells, base: rows, repeats: 1 }, rows);
        for (let l = 0; l < 4; l++) {
            let on = !!(held >> l & 1);
            for (let r = 0; r < rows; r++) {
                const k = r * 4 + l, c = cells[k];
                if (!c) continue;
                if (!isNote(c) && !on) { cells[k] = null; continue; }
                on = sounding(on, c);
            }
        }
        for (const c of cells) if (c) { delete c.wp; delete c.sub; }
        return cells;
    }
    function send() {                                      /* (true: sent) */
        if (U.saving) { U.refuse(B.REC); U.toast("STAGE NOT WRITTEN: STORAGE BUSY"); return false; }
        stopWriting();                                     /* (the keys down: their notes' ends on the stage first) */
        fitStage();
        const j = LS(), rows = j.rows, i = tgt(), L = selLoop(), mono = isMono(), inst = U.inst, was = st.cells;
        const marked = c => c && Object.assign(M.copyCell(c), c.wp != null ? { wp: c.wp } : {});   /* (an overdub's mark kept for sentCells) */
        const cells = sentCells(mono ? monoOf(st.cells) : st.cells.map(marked), rows);
        const lanes = mono ? 1 : usedLanes(cells);
        /* (a loop past its 3,800 B: the ends at the line added to a full stage) */
        if (streamOf(cells) > STAGE_MOST) { U.refuse(B.REC); U.toast("THE STAGE IS FULL"); return false; }
        st.cells = null;                                   /* (its room the loop's now, not counted twice) */
        if (L) {
            if (!U.edit(() => Object.assign(L, { inst, mono, lanes, base: rows, repeats: 1, cells, shift: 0 }))) { st.cells = was; return false; }
            U.toast("STAGE INTO " + name(i));
        } else {
            if (j.list.length >= M.LOOPS_MAX) { st.cells = was; U.refuse(B.REC); return false; }
            const nl = { inst, col: nextCol(), mono, lanes, base: rows, repeats: 1, muted: false, soloed: false, cells, shift: 0 };
            /* (its mix the next loop's as heard: its sends the KEYS column's) */
            if (!U.edit(() => { j.mixer[j.list.length] = M.nextLoopMix(j); j.list.push(nl); syncMix(j); })) { st.cells = was; return false; }
            lp.sel = topSel();                              /* (+ NEW LOOP again) */
            P.mutesChanged();
            U.toast("STAGE AS " + name(i));
        }
        if (playingHere()) P.lpMoveLanes(P.LP_STAGE, i); else P.lpStageMute();
        st.erasers = [];
        st.silent = -1;
        lp.arm = null;                                     /* (an arm ends with it) */
        st.off = true;                                     /* (off till REC or ARP turns it on) */
        syncSel();
        PTFM.audio.changed();
        return true;
    }
    /* EDIT: the selected loop copied onto the stage as it is heard (its repeats written out, a note off where it rests or
     * a repeat is cut, its SHIFT applied), in place of what the stage held, and its sound (instrument, MONO / POLY) made
     * the current one, as ENV held does; the stage on, if it was off; OVERDUB, so the line keeps them; the loop silent at
     * once (the stage holds notes). On + NEW LOOP the stage's clear; a stage it would not fit: refused, saying why */
    function editFill() {
        const L = selLoop(), j = LS(), rows = j.rows;
        if (!L) { stageClear(B.EDIT); return; }
        if (refused(B.EDIT)) return;
        const cells = asHeard(L, rows), why = stageRefusal(cells);
        if (why) { U.refuse(B.EDIT); U.toast(why); return; }
        stopWriting();
        P.lpStageMute();
        U.setInst(L.inst);
        if (!!j.nextMono !== !!L.mono) U.edit(() => { j.nextMono = !!L.mono; });
        st.setup = setupNow();
        st.cells = cells;
        st.erasers = [];
        st.off = false;                                    /* (an explicit fill of the stage turns it on) */
        st.over = false;
        syncSilent();                                      /* (the stage holds its notes: the loop silent at once) */
        M.version++;
        U.toast(name(lp.sel) + " ON THE STAGE");
    }
    /* the stage's clear (EDIT held, EDIT's tap on + NEW LOOP, SEL's CLEAR STAGE): emptied, the selected loop heard again,
     * and on, as EDIT on a loop leaves it. Already empty: off, it just turns on; on, b blinks. Refused during a count-in */
    function stageClear(b) {
        if (refused(b)) return;
        if (stageHas()) {
            clearStage();
            st.off = false;
            syncSilent();
            U.toast("STAGE CLEARED");
        } else if (st.off) stageSwitch();
        else U.refuse(b);
    }
    /* REC with the stage off (not in ADJUST): the stage on, nothing sent; the loops stopped, armed at once as REC arms on
     * an empty stage, whatever the stage holds (STAGE ON: ARMED: a key or PLAY starts them writing); playing, the keys
     * write from there (STAGE ON) */
    function stageOnRec() {
        st.off = false;
        syncSilent();
        if (playingHere()) U.toast("STAGE ON");
        else { lp.arm = { wait: true }; U.toast("STAGE ON: ARMED"); }
        M.version++;
    }
    /* ARP's tap: the stage off and on (STAGE OFF, STAGE ON), on the screen whatever else is going on (REC with it off:
     * stageOnRec, above). Off, the stage keeps its notes, drawn
     * dimmed, not heard, and the selected loop is heard in its place; the keys down stop writing; an arm (the stage's: its
     * first key would start the loops writing) ends, a count-in with it (CANCELLED). On, the stage is heard again from the
     * next row, the selected loop silent again while it holds notes */
    function stageSwitch() {
        if (st.off) {
            st.off = false;
            syncSilent();
            U.toast("STAGE ON");
        } else {
            overdubTick();
            stopWriting();
            st.off = true;
            st.erasers = [];
            if (P.lpStageMute) P.lpStageMute();            /* (not heard from now: what it scheduled goes, what sounds lets go) */
            syncSilent();
            if (lp.arm) {
                if (P.countingIn() && !lp.song) P.stopCountIn();
                lp.arm = null;
                U.toast("CANCELLED");
            } else U.toast("STAGE OFF");
        }
        M.version++;
    }
    /* the keys while the stage is off and the loops play: each sounds at once in the stage's sound (the keys' instrument
     * and MONO / POLY, through the stage's column and LOOPS' effects), no QUANTIZE wait, written nowhere. There are 16
     * lanes (the keys' own, P.LP_NOW's 4 to 19: the stage-on keys' four stay theirs), each holding the key it was given:
     * POLY a key takes the lowest free lane; all 16 held, the lane `onext`, which then steps round (a counter from 0 at
     * power-on, stepped only by such an overflow): the note there is let go (it rings out its release), its key stays
     * down but is forgotten, its release does nothing. MONO the lane flagged MONO if one is held (legato as MONO LEGATO
     * says; a TB-303 slides), else the lowest free lane as POLY; only the key whose note the lane holds ends it. A key
     * let go ends its note at once */
    function offKeyDown(n, m) {
        const j = LS(), mono = isMono(), ol = st.ol;
        let l = mono ? ol.findIndex(e => e && e.mono) : -1;
        const cur = l >= 0;
        if (l < 0) l = ol.findIndex(e => !e);
        if (l < 0) { l = st.onext; st.onext = (l + 1) % OFF_LANES; }
        const lane = 4 + l, cell = M.newCell(m, null, U.set("KEYS VOLUME"));
        if (cur && j.legato !== "JUMP") {
            if (M.instType(U.inst) === "TB303") P.lpSlide(P.LP_NOW, lane);
            else cell.cmds.push(j.legato === "LEGATO" ? { c: 0xD3, v: 0 } : { c: 0x03, v: 0x20 });   /* (LEG; a glide, 2 semitones a tick) */
        }
        ol[l] = { n, mono };
        P.lpNote(P.LP_NOW, lane, cell, 0, -1, n);
    }
    const offHeld = n => st.ol.some(e => e && e.n === n);   /* (key n holds a lane: not forgotten) */
    function offKeyUp(n) {
        const l = st.ol.findIndex(e => e && e.n === n);
        if (l < 0) return;
        st.ol[l] = null;
        P.lpNote(P.LP_NOW, 4 + l, M.newCell(M.OFF, null, null), 0, -1, n);
    }
    /* the keys down while the stage is off, ended at once (leaving LOOPS, FX taking the keys); the counter stays */
    function offKeysEnd() {
        st.ol.forEach(e => { if (e) offKeyUp(e.n); });
    }
    /* PLAY (or a note, armed): the loops from the cursor (t0: when), their passes counted afresh */
    function startPlay(t0) {
        const n = LS().rows, row = ((P.kept.loops.row || 0) % n + n) % n;
        P.pvStop();
        P.playLoops(row, t0);
        lp.startRow = row;
        lp.arm = null;
        syncRun();
    }
    function countThen() {
        if (P.playing) P.pause();                          /* (what played on into LOOPS pauses as the count starts) */
        P.countIn(t0 => startPlay(t0), M.bpm(LS()), Math.max(0, M.LOOP_COUNT_INS.indexOf(LS().countIn)));
    }

    /* ------------------------------------------------------------ RECORD LIVE --- */
    /* RECORD LIVE (specs/ui/screens/loops.md#record-live): everything that sounds, written as a new song at the first
     * free number: a layer before the output, LOOPS working as ever meanwhile. Its panel (SEL's menu: the name, LOOPS'
     * tempo) arms it, the player's tap running from then (P.lpTap); it starts with REC, a note, or PLAY (the loops not
     * playing as it was armed: after COUNT-IN); HOME ends it, leaving LOOPS or the loops' replay pausing anywhere too. Its
     * rows are its own, fixed at its start, its time the replay's ticks from its row 0: while it records the replay never
     * stops, PLAY silencing the loops or playing them on again on the next row, held taking them to their start (P.lpRest).
     * The tap's events (P.lpTapTake, each as it sounds) go on a track (noteAt: one whose mix is the note's source's, a
     * channel of the replay: the loop's, or the stage's track, 16 the keys' on another screen) and into the log, kept in
     * rows' order, the changes polled (liveChanges) with them; a pattern every 128 rows, a position each, none shared,
     * written from the log once its rows are heard (livePoll, on every screen: FX keeps it). What finds no track is heard,
     * left out and counted.
     * lp.song: null, or {state: "armed" (its panel's REC; splay: the loops played then, so PLAY does not start it),
     * "count" (PLAY started it, counting in), "on" (recording)}; lv: the recording's own state, never stored */
    let LOG_N = 2000;                                  /* the log's entries at most (a full log drops one: the song goes on, a
                                                         * note dropped counted as left out; liveLogMax sets it for the checks) */
    const LIVE_ROWS = 128;                             /* a pattern's rows */
    const LIVE_OK = 0, LIVE_FULL = 1, LIVE_PATTERNS = 2, SF_DONE = 0, SF_256 = 3;   /* (a pattern written; not: the edit
                                                         * memory full, no pattern number free; how it ended) */
    const MIXC = [["lvl", 0x80], ["pan", 0x08], ["dly", 0x82], ["rev", 0x83], ["cho", 0x81], ["dck", 0x84]];
    const MIX_DEF = { lvl: 127, pan: 128, dck: 0, cho: 0, dly: 0, rev: 0 };
    const songOn = () => !!lp.song && lp.song.state === "on";
    const songArmed = () => !!lp.song && lp.song.state !== "on";
    let lv = null, slName = "";                        /* (slName: the panel's NAME) */
    /* a source's mix: channel c's (a loop's, the next loop's as heard past them), 16 the keys' on another screen */
    const srcMix = c => c === 16 ? lv.km : c < LS().list.length ? LS().mixer[c] : M.nextLoopMix(LS());
    /* DUCK's and FILTER's envelope's SOURCE name a loop in LOOPS (its track 16 + i): in the song, the first track its notes
     * took */
    function srcOf(n, v) {
        const tr = i => { const t = lv.tr.findIndex(x => x.owner === i); return t < 0 ? i : t; };
        if (n === "DUCK") return v < 16 ? tr(v) : v;
        return v >= 1 && v <= 16 ? 1 + tr(v - 1) : v;
    }
    const isSrc = (n, i) => (n === "DUCK" && i === 0) || (n === "FILTER" && i === 3);
    /* an FX knob's value as its command's: its step from the bottom of its range (cmds.js fxKnob's other way) */
    const fxStep = (n, i, v) => { const k = M.FX[n].knobs[i]; return k.list ? v : Math.round((v - k.min) / (k.step || 1)); };
    const songNumber = () => M.freeSong();               /* the first song number free, -1 none */
    /* armed: the tap on (what it hears before the song's row 0 dropped: livePoll) */
    function songArm(state) {
        lp.song = state;
        P.lpTap(true);
    }
    /* it begins at its row 0, the replay's tick tn0 (known: its row 0 heard; else, a replay just started for it, known at
     * its first row): the loops' setup at the start is the song's; a punch-in running then is not written. True: begun */
    function songBegin(tn0, known) {
        const n = songNumber(), j = LS();
        if (n < 0) { lp.song = null; P.lpTap(false); U.toast("NO SONG NUMBER FREE"); return false; }
        const km = U.set("KEYS MIX"), fx = structuredClone(j.fx);
        lv = { song: n, name: slName, t0: P.time(), tn0, known, ticks: j.ticks || 1, setup: { tempo: j.tempo, ticks: j.ticks, rpb: j.rpb, swing: j.swing, fx },
               km: { lvl: 127, pan: km.pan, dck: km.dck, cho: km.cho, dly: km.dly, rev: km.rev },   /* (a note recorded plays without KEYS GAIN) */
               tr: Array.from({ length: 16 }, () => ({ on: false, lane: -1, id: null, owner: -1, nrow: -1, ntick: 0, have: Object.assign({}, MIX_DEF), mix0: null })),
               pk: U.punch == null ? -1 : U.punch, pabs: 0, pw: null, done: 0, left: 0, made: false, log: [],
               sil: j.list.map(silenced), last: { tempo: j.tempo, on: Object.assign({}, j.fx.on), knobs: structuredClone(j.fx.knobs) } };
        lp.song = { state: "on" };
        lp.pdefer = lp.phold = false;
        lp.sat = 0;
        lp.hrow = null;
        syncRun();
        return true;
    }
    /* its start with the loops not playing (REC, a key): their replay from row 0, silent from its first row, their place
     * kept for PLAY to take them on from */
    function songBeginSilent() {
        const k = P.kept.loops.row || 0;
        P.playLoopsSilent(k < LS().rows ? k : 0);
        return songBegin(0, false);
    }
    /* armed, REC or a key: the loops playing, it begins at the row playing (they play on as they were); not playing,
     * their replay starts silent for it */
    function songStart() {
        if (!playingHere()) return songBeginSilent();
        const h = P.at();
        return songBegin(h ? h.tn : 0, !!h);
    }
    /* armed, PLAY (the loops not playing as it was armed): the loops from their start, the song with them, after
     * COUNT-IN */
    function songCount() {
        const ci = Math.max(0, M.LOOP_COUNT_INS.indexOf(LS().countIn));
        if (P.playing) P.pause();                          /* (what played on into LOOPS pauses as the count starts) */
        if (!ci) { P.playLoops(0); songBegin(0, false); return; }
        lp.song.state = "count";
        P.countIn(t0 => { P.playLoops(0, t0); if (lp.song && lp.song.state === "count") songBegin(0, false); }, M.bpm(LS()), ci);
    }
    function songCancel() {                                /* armed or counting in: given up */
        if (lp.song && lp.song.state === "count") P.stopCountIn();
        lp.song = null;
        P.lpTap(false);
    }
    /* the song's row heard now, its tick (row 0 until its row 0 is heard); tm: at that time instead */
    function songNow(tm) {
        const d = lv && lv.known && playingHere() ? P.lpTn(tm) - lv.tn0 : 0;
        return d < 0 ? { row: 0, tick: 0 } : { row: Math.floor(d / lv.ticks), tick: d % lv.ticks };
    }
    /* the song's row heard now (tm: when the change was made), to the nearest row (a change made then is written there),
     * never in a pattern written; null: not heard (the loops' replay not playing) */
    function heardRow(tm) {
        if (!lv || !lv.known || !playingHere()) return null;
        const h = songNow(tm);
        return Math.max(h.row + (2 * h.tick >= lv.ticks ? 1 : 0), lv.done * LIVE_ROWS);
    }
    /* ---- the log: the pattern being written, in rows' order (row: within it, past 127 the next's), their order kept within
     * a row: "note" (t, tick, id, note, vol, inst, acc, sld, leg, glide), "off" (t, tick), "mix" (t, code, v: a mixer
     * command), "cmd" a global command, "punch" a punch-in's command still running (its value set to the rows it ran as it
     * ends) ---- */
    const lost = () => { if (lv.left < 65535) lv.left++; };   /* a note left out (the count stops at its top) */
    /* a place in the log for an entry, null when there is none (full, or the row past 255): nothing counted here, the
     * caller counts a note */
    function logPut(e) {
        if (lv.log.length >= LOG_N || e.row > 255) return null;
        let k = lv.log.length;
        while (k && lv.log[k - 1].row > e.row) k--;
        lv.log.splice(k, 0, e);
        return e;
    }
    /* a command's key: the last at a row wins (an effect's ON and OFF one; any punch-in one) */
    const cmdKey = (code, v) => code === 0xAE || code === 0xAF ? "fx" + v : code >= 0xC3 && code <= 0xD2 ? "punch" : code;
    function logCmd(at, k, code, v) {
        const row = at - lv.done * LIVE_ROWS, e = lv.log.find(x => x.row === row && (x.k === "cmd" || x.k === "punch") && cmdKey(x.code, x.v) === cmdKey(code, v));
        if (e) Object.assign(e, { k, code, v });
        else logPut({ row, k, code, v });
    }
    /* the commands track t's cell at the row still has room for: a key off kept room for after a note */
    function cellRoom(t, row) {
        let n = 0, note = false, off = false;
        for (const e of lv.log) {
            if (e.row !== row || e.t !== t) continue;
            if (e.k === "note") { note = true; n += (e.tick ? 1 : 0) + (e.glide || e.leg ? 1 : 0) + (e.acc || e.sld ? 1 : 0); }
            else if (e.k === "off") { off = true; n++; }
            else n++;
        }
        return 4 - n - (note && !off ? 1 : 0);
    }
    /* track t's mix brought up to its source's at the row, as far as the cell has room (mixer commands in MIXC's order); the
     * rest at a row after (liveChanges) */
    function mixSync(t, row) {
        const x = lv.tr[t], to = srcMix(x.owner);
        for (const [k, code] of MIXC) {
            if (x.have[k] === to[k]) continue;
            const e = lv.log.find(y => y.k === "mix" && y.t === t && y.row === row && y.code === code);
            if (e) e.v = to[k];
            else if (cellRoom(t, row) <= 0 || !logPut({ row, k: "mix", t, code, v: to[k] })) continue;
            x.have[k] = to[k];
        }
    }
    /* ---- the tap's events on the song's rows ---- */
    /* the track holding the event's lane's note (a key's, by its id), -1 none */
    const heldBy = e => lv.tr.findIndex(x => x.on && x.lane === e.k && (e.k < 96 || x.id === e.id));
    /* the track's note ends at the row, tick: a tick after it starts at least */
    function noteEnd(t, row, tick) {
        const x = lv.tr[t];
        x.on = false;
        if (x.nrow === lv.done * LIVE_ROWS + row && tick <= x.ntick) {
            tick = x.ntick + 1;
            if (tick >= lv.ticks) { row++; tick = 0; }
        }
        logPut({ row, k: "off", t, tick });
    }
    /* a track for the note at the row (a track takes one note a row): free, the lane's first, then any with its source's
     * mix, then one unused (in the first pattern its mix the song's from there), then the one whose mix differs least; -1
     * none */
    function trackFor(e, row) {
        const abs = lv.done * LIVE_ROWS + row, s = lv.tr, free = x => !x.on && x.nrow !== abs;
        let t = s.findIndex(x => free(x) && x.lane === e.k && x.owner === e.src);
        if (t < 0) t = s.findIndex(x => free(x) && x.owner === e.src);
        if (t < 0) t = s.findIndex(x => free(x) && x.owner < 0);
        if (t < 0) {
            const mix = srcMix(e.src);
            let best = 99;
            s.forEach((x, k) => {
                if (!free(x)) return;
                const n = MIXC.filter(([q]) => x.have[q] !== mix[q]).length;
                if (n < best) { best = n; t = k; }
            });
        }
        return t;
    }
    /* a note as it sounds: on the track holding its lane's note (ending that one there) when its mix is the note's and the
     * track has no note at the row, else on a track of its own (a glide, a legato or a slide kept only on the same track);
     * none free: left out, counted */
    function noteAt(e, row, tick) {
        const abs = lv.done * LIVE_ROWS + row;
        let t = heldBy(e), joined = false;
        if (t >= 0) {
            if (lv.tr[t].owner === e.src && lv.tr[t].nrow !== abs) joined = true;
            else { noteEnd(t, row, tick); t = -1; }
        }
        if (t < 0) t = trackFor(e, row);
        if (t < 0) { lost(); return; }
        const x = lv.tr[t], mix = srcMix(e.src);
        if (x.owner < 0 && !lv.done) { x.have = Object.assign({}, mix); x.mix0 = Object.assign({}, mix); }
        if (e.slid && joined) {                            /* (a TB-303's slide: SLD on the note it comes from, if not written yet) */
            for (let k = lv.log.length - 1; k >= 0; k--) if (lv.log[k].k === "note" && lv.log[k].t === t) { lv.log[k].sld = true; break; }
        }
        Object.assign(x, { on: true, lane: e.k, id: e.id, owner: e.src, nrow: abs, ntick: Math.min(tick, 15) });
        if (!logPut({ row, k: "note", t, tick: x.ntick, id: e.id, note: e.note, vol: e.vol, inst: e.inst, acc: !!e.acc, sld: !!e.sld,
                      leg: joined && !!e.leg, glide: joined ? e.glide || 0 : 0 })) { lost(); return; }
        mixSync(t, row);
    }
    function tapEvent(e) {
        if (e.kind === "move") {                           /* (lanes going on as others: the notes on them with them) */
            for (const x of lv.tr) if (x.on && x.lane === e.k) { x.lane = e.to; x.owner = e.src; }
            return;
        }
        if (e.kind === "mute") lv.sil = e.sil;             /* (the loops silent from here) */
        const unheard = e.none || (e.k < 64 && lv.sil[e.k >> 2]);   /* (a note: no voice given, or a loop not heard: not written) */
        const d = e.tn - lv.tn0;
        if (d < 0) return;                                 /* (before its row 0) */
        let row = Math.floor(d / lv.ticks) - lv.done * LIVE_ROWS, tick = d % lv.ticks;
        if (row < 0) { row = 0; tick = 0; }                /* (a pattern written: its next row) */
        if (row > 255) { if (e.kind === "on" && !unheard) lost(); return; }
        if (e.kind === "mute") {                           /* (their notes end there) */
            lv.tr.forEach((x, t) => { if (x.on && x.lane < 64 && e.sil[x.lane >> 2]) noteEnd(t, row, tick); });
            return;
        }
        if (e.kind === "off") { const t = heldBy(e); if (t >= 0) noteEnd(t, row, tick); return; }
        if (e.kind === "lost") {                           /* (it ends there; its lane's own end then finds nothing) */
            const t = lv.tr.findIndex(x => x.on && x.id === e.id);
            if (t >= 0) noteEnd(t, row, tick);
            return;
        }
        if (!unheard) noteAt(e, row, tick);
    }
    /* the worklet's word, late: note id given no voice after all (gone from the log: not written) */
    P.lpVoice = id => {
        if (!songOn()) return;
        const t = lv.tr.findIndex(x => x.on && x.id === id);
        if (t < 0) return;
        lv.tr[t].on = false;
        lv.tr[t].nrow = -1;
        lv.log = lv.log.filter(e => !(e.k === "note" && e.id === id));
    };
    /* what changed live since the last look, logged at the row heard (not heard: dropped): TEMPO as F00 back to the song's
     * own, else Fxx held within 20-FF; an effect switched as FX ON / OFF; an FX knob with a command as its command
     * (SOURCE as the song has it; the METRONOME's knobs have none); the tracks' mixes brought up to their sources'. tm:
     * when the change was made (an edit's start) */
    function liveChanges(tm) {
        if (!songOn()) return;
        const j = LS(), last = lv.last, at = heardRow(tm), log = f => { if (at != null) f(at); };
        if (j.tempo !== last.tempo) {
            last.tempo = j.tempo;
            log(a => logCmd(a, "cmd", 0x0F, j.tempo === lv.setup.tempo ? 0 : U.clamp(j.tempo, 0x20, 0xFF)));
        }
        M.FX_ORDER.forEach((n, fi) => {
            if (!!j.fx.on[n] !== !!last.on[n]) { last.on[n] = !!j.fx.on[n]; log(a => logCmd(a, "cmd", j.fx.on[n] ? 0xAE : 0xAF, fi)); }
            (j.fx.knobs[n] || []).forEach((v, i) => {
                if (v === last.knobs[n][i]) return;
                last.knobs[n][i] = v;
                const code = M.fxCode(n, i);
                if (!Number.isNaN(code)) log(a => logCmd(a, "cmd", code, fxStep(n, i, isSrc(n, i) ? srcOf(n, v) : v)));
            });
        });
        if (lv.log.length < LOG_N)                         /* (a full log takes no mix: no walk each pass) */
            log(a => lv.tr.forEach((x, t) => { if (x.owner >= 0) mixSync(t, a - lv.done * LIVE_ROWS); }));
    }
    /* the punch-in running ends at the song's row `at`: its command's value the rows it ran (1-FF), in its pattern if
     * written already */
    function livePunchEnd(at) {
        const v = U.clamp(at - lv.pabs, 1, 255);
        if (lv.pk < 0) return;
        const x = lv.log.find(y => y.k === "punch");
        if (x) { x.k = "cmd"; x.v = v; }                   /* (its pattern not written yet: its value there) */
        else if (lv.pw) {
            const p = M.pattern(lv.pw.num), c = p && p.cells[lv.pw.r * 16 + lv.pw.t];
            for (const y of c ? c.cmds : []) if (y.c === 0xC3 + lv.pk) y.v = v;
            M.settle();
            M.version++;
        }
        lv.pk = -1;
        lv.pw = null;
    }
    /* the punch-in running now (0-15, -1 none), as heard */
    function livePunch(k) {
        if (!songOn() || k === lv.pk) return;
        const at = heardRow();
        if (at == null) return;
        livePunchEnd(at);
        if (k < 0) return;
        logCmd(at, "punch", 0xC3 + k, 1);
        lv.pk = k;
        lv.pabs = at;
    }
    /* ---- writing a pattern ---- */
    /* the cell of track t's log entries at a row: its note in its instrument, note delay, glide, legato, ACC, SLD; a note's
     * own end (it came after it) a key off at its tick; an end before a note a key off before its delay (the note ends
     * what sounds, at its tick or sooner); alone a note off on the row's first tick, else a key off; its mixer commands */
    function cellOf(row, t, es) {
        const n = es.find(e => e.k === "note");
        const c = n ? M.newCell(n.note, n.inst, n.vol == null ? null : n.vol) : M.newCell(null, null, null);
        if (n) {
            if (n.tick) c.cmds.push({ c: 0x0E, v: 0xD0 | n.tick });
            if (n.leg) c.cmds.push({ c: 0xD3, v: 0 });
            else if (n.glide) c.cmds.push({ c: 0x03, v: n.glide });
            if (n.acc || n.sld) c.cmds.push({ c: n.acc && n.sld ? 0xC2 : n.acc ? 0xC0 : 0xC1, v: 0 });
        }
        es.forEach((e, k) => {
            if (e.k !== "off") return;
            if (n && k > es.indexOf(n)) c.cmds.push({ c: 0x14, v: e.tick });
            else if (n) { if (e.tick < n.tick) c.cmds.push({ c: 0x14, v: e.tick }); }
            else if (e.tick) c.cmds.push({ c: 0x14, v: e.tick });
            else c.note = M.OFF;
        });
        for (const e of es) if (e.k === "mix" && c.cmds.length < 4) c.cmds.push({ c: e.code, v: e.v });
        return c;
    }
    /* the pattern lv.done, its rows up to end (LIVE_ROWS whole; less: the song ended there, its last pattern that long),
     * at the first free pattern number; the song made with the first (its name, the loops' setup at the start, its
     * tracks' mixes, it plays once and stops), then a position more each pattern. LIVE_OK; not (nothing of it written):
     * LIVE_FULL, the edit memory full; LIVE_PATTERNS, no pattern number free */
    function liveChunk(end) {
        end = U.clamp(end, 1, LIVE_ROWS);
        const num = M.freePattern();
        if (num < 0) return LIVE_PATTERNS;
        const p = M.newPattern(num, 16, end), S = lv.setup, n = lv.song, cells = {};
        let hi = 1, prow = -1, ptrk = -1;
        for (let r = 0; r < end; r++) {
            const es = lv.log.filter(e => e.row === r);
            if (!es.length) continue;
            for (let t = 0; t < 16; t++) {
                const mine = es.filter(e => e.t === t);
                if (mine.length) cells[r * 16 + t] = cellOf(r, t, mine);
            }
            for (const v of es) {                          /* the row's commands: each in the first cell with room, from track 0 */
                if (v.k !== "cmd" && v.k !== "punch") continue;
                for (let t = 0; t < 16; t++) {
                    const c = cells[r * 16 + t];
                    if (c && (c.cmds.length >= 4 || c.cmds.some(y => y.c === v.code))) continue;
                    (cells[r * 16 + t] = c || M.newCell(null, null, null)).cmds.push({ c: v.code, v: v.v });
                    if (v.k === "punch") { prow = r; ptrk = t; }
                    break;
                }
            }
        }
        for (const k in cells) { p.cells[k] = cells[k]; hi = Math.max(hi, (k & 15) + 1); }
        p.tracks = hi;
        if (!lv.made) {
            S.fx.knobs.DUCK[0] = srcOf("DUCK", S.fx.knobs.DUCK[0]);
            S.fx.knobs.FILTER[3] = srcOf("FILTER", S.fx.knobs.FILTER[3]);
            M.state.songs[n] = Object.assign(M.newSong(), { name: lv.name, tempo: S.tempo, ticks: S.ticks, rpb: S.rpb, swing: S.swing, atEnd: "STOP", fx: S.fx,
                                                            mixer: lv.tr.map(x => Object.assign({}, x.mix0 || MIX_DEF)), order: [] });
        }
        M.state.patterns[num] = p;
        M.state.songs[n].order.push(num);
        M.settle();
        if (M.editsUsed > M.EDITS_MAX) {                  /* (nothing of it kept: the song keeps the patterns before) */
            M.state.songs[n].order.pop();
            M.state.patterns[num] = null;
            if (!lv.made) M.state.songs[n] = null;
            M.settle();
            return LIVE_FULL;
        }
        if (prow >= 0) lv.pw = { num, r: prow, t: ptrk };
        lv.log = lv.log.filter(e => e.row >= end);         /* (the next one's become the current's) */
        for (const e of lv.log) e.row -= end;
        lv.done++;
        lv.made = true;
        M.version++;
        PTFM.audio.changed();
        return LIVE_OK;
    }
    /* it is over: the loops silent, their replay stops at their place; the toast says how it went (why: LIVE_FULL,
     * LIVE_PATTERNS, SF_256, SF_DONE) */
    function songFinish(why) {
        const n = lv.done, left = lv.left, s = M.hex(lv.song), secs = Math.max(0, Math.floor(P.time() - lv.t0));
        lp.song = null;
        lv = null;
        P.lpTap(false);
        if (P.what === "loops" && P.playing && P.lpRestState().on) P.pause();
        const tm = Math.floor(secs / 60) + ":" + M.dec(secs % 60);
        U.toast(why === LIVE_FULL ? "SONG " + s + " STOPPED: MEMORY FOR EDITS IS FULL"
              : why === LIVE_PATTERNS ? "SONG " + s + " STOPPED: ALL " + (M.USER_LAST + 1) + " PATTERNS USED"
              : why === SF_256 ? "SONG " + s + " STOPPED: 256 POSITIONS"
              : !n ? "CANCELLED"
              : left ? "SONG " + s + ": " + left + (left === 1 ? " NOTE LEFT OUT" : " NOTES LEFT OUT")
              : "SONG " + s + " RECORDED: " + tm + ", " + n + (n === 1 ? " PATTERN" : " PATTERNS"));
    }
    /* the patterns heard to their end written (each once its rows are heard): true, it stopped (256 positions, no room) */
    function songChunks(now) {
        while (lv.done < Math.floor(now / LIVE_ROWS) && lv.done < 256) {
            const r = liveChunk(LIVE_ROWS);
            if (r !== LIVE_OK) { songFinish(r); return true; }
        }
        if (lv.done >= 256) { songFinish(SF_256); return true; }
        return false;
    }
    /* it ends at once at the song's row `at`: the rows heard written, that row the last; nothing heard (at < 0): no song */
    function songEnd(at) {
        if (!songOn()) { lp.song = null; P.lpTap(false); return; }
        if (at >= 0) liveDrain(true);
        if (at >= 0 && songChunks(at)) return;
        livePunchEnd(at + 1);
        if (at >= lv.done * LIVE_ROWS && lv.done < 256) {
            const r = liveChunk(at % LIVE_ROWS + 1);
            if (r !== LIVE_OK) { songFinish(r); return; }
        }
        songFinish(SF_DONE);
    }
    function songEndHere() {                               /* ... at the row heard now (leaving LOOPS) */
        songEnd(!lv.known ? -1 : playingHere() ? songNow().row : lp.sat);
    }
    /* the tap's events that have settled (all: that are due), on the song's rows */
    function liveDrain(all) { for (const e of P.lpTapTake(all)) tapEvent(e); }
    /* on every screen (the player's tick, a key, a pause): armed, what the tap heard before the row playing dropped; its
     * row 0 known (its replay's first row played), the replay paused (it ends where it was heard last), the tap's events,
     * what changed, each pattern written once its rows are heard (not while HOME's press may yet cut them back), a
     * punch-in */
    function livePoll() {
        if (lp.song && !songOn()) {
            const h = playingHere() ? P.at() : null;
            if (h && h.tn != null) P.lpTapDrop(h.tn);
            return;
        }
        if (!songOn()) return;
        const on = playingHere();
        if (!lv.known) { if (on && !P.lpPlayed()) return; lv.known = true; }
        if (!on) { songEnd(lp.sat); return; }
        syncRun();
        liveDrain();
        liveChanges();
        if (lp.hrow != null && !U.pending(B.HOME)) lp.hrow = null;   /* (a combination, not a tap) */
        if (lp.hrow == null && songChunks(songNow(P.lpSettled()).row)) return;   /* (as far as what has settled) */
        livePunch(U.punch == null ? -1 : U.punch);
        lp.sat = songNow().row;
    }
    P.lpPoll = livePoll;
    /* the RECORD LIVE panel (specs/ui/screens/loops.md#the-record-live-panel): NAME (EDIT: the text entry), LOOPS' tempo
     * on K1 (and ALGORITHM, as on the screen); REC arms it, HOME cancels; the keys play as on the screen */
    function slDefault() {                                 /* LOOPS 01, or one past the highest LOOPS ## a song has */
        let hi = 0;
        for (const s of M.state.songs) { const d = s && /^LOOPS (\d\d)$/.exec(s.name); if (d && +d[1] > hi) hi = +d[1]; }
        slName = "LOOPS " + M.dec(hi < 99 ? hi + 1 : 99);
    }
    function slOpen() {
        slDefault();
        U.open({
            kind: "live", opener: B.SEL,
            knob(e, s, coarse) { if (e === E.K1 || e === E.ALGORITHM) tempoTurn(s, coarse); },
            tap(b) {
                if (b === B.EDIT) U.textEntry("NAME", slName, 16, B.EDIT, nm => { if (nm) slName = nm; });
                return true;
            },
            rec() {                                        /* REC (ARM): armed, closed */
                if (recQuiet()) return true;
                recDone();
                U.close(this);
                songArm({ state: "armed", splay: playingHere() });
                return true;
            },
            cancel() { U.close(this); },
            lit(b) { return b === B.REC ? (U.blink(250) ? 1 : 0) : undefined; },
            draw() {
                const h = 108, y = U.BODY_Y + Math.floor((U.BODY_H - h) / 2), value = (yy, v) => D.text(226 - D.tw(F.T, v), yy, F.T, v, D.WHITE);
                U.panel(8, y, 224, h, null);
                D.text(D.centre(F.T, "RECORD LIVE", 8, 224), y + 6, F.T, "RECORD LIVE", D.WHITE);
                D.text(14, y + 24, F.T, "NAME", D.LGREY);
                value(y + 24, slName);
                D.runs(14, y + 36, F.T, [["K1", D.YELLOW], [" TEMPO", D.LGREY]]);
                value(y + 36, M.bpm(LS()) + " BPM");
                D.runs(14, y + 52, F.T, [["EDIT", D.YELLOW], [": NAME", D.LGREY]]);
                U.askFoot(y + 68, y + h, ["ARM", "CANCEL"], [], [B.HOME, B.REC]);
            },
        });
    }
    /* each row the loops' replay schedules: a pass's line (its row 0) heard at its time (the stage); mutes and solos
     * waiting for the bar line act on it (each at its time) */
    P.loopsRow = function (row, t, dur, pass) {
        if (P.what !== "loops") return;
        const ms = Math.max(0, (t - P.time()) * 1000), run = P.lpRun();
        if (row === 0 && pass > 0) setTimeout(() => { if (P.lpRun() === run && playingHere()) lineAt(pass); }, ms);
        if (row % barRows() === 0) for (const [k, p] of lp.pend) if (!p.due) { p.due = true; setTimeout(() => applyPend(k, t), ms); }
    };

    /* --------------------------------------------------------- mutes and solos --- */
    /* a solo, one loop at a time: loop i soloed alone, the others' solos off; on a soloed loop, its solo off. At once, or
     * (atBar, the loops playing) at the next bar line, every solo that changes waiting with it; held again calling the
     * waits off */
    function soloOne(i, atBar) {
        const ls = list(), on = !ls[i].soloed, word = on ? " SOLO" : " SOLO OFF";
        const flip = ls.map((l, j) => j === i || (on && l.soloed));      /* (the loops whose solo changes) */
        const dropWaits = () => { for (const k of [...lp.pend.keys()]) if (k.endsWith("/solo")) lp.pend.delete(k); };
        if (atBar && playingHere()) {
            const was = lp.pend.has(i + "/solo");
            dropWaits();
            if (was) return;
            flip.forEach((f, j) => { if (f) lp.pend.set(j + "/solo", { i: j, kind: "solo", due: false }); });
            U.toast(name(i) + word + " AT THE BAR");
            return;
        }
        dropWaits();
        U.edit(() => { ls.forEach((l, j) => { if (flip[j]) l.soloed = !l.soloed; }); });
        P.mutesChanged();
        U.toast(name(i) + word);
    }
    /* loop i's mute (or solo: soloOne) switched: at once, or (atBar, the loops playing) at the next bar line, held again
     * calling the wait off */
    function toggle(i, kind, atBar) {
        const L = list()[i];
        if (!L) return;
        if (kind === "solo") { soloOne(i, atBar); return; }
        const k = i + "/" + kind, on = kind === "mute" ? !L.muted : !L.soloed;
        const word = kind === "mute" ? (on ? " MUTED" : " ON") : on ? " SOLO" : " SOLO OFF";
        if (atBar && playingHere()) {
            if (lp.pend.has(k)) { lp.pend.delete(k); return; }
            lp.pend.set(k, { i, kind, due: false });
            U.toast(name(i) + word + " AT THE BAR");
            return;
        }
        lp.pend.delete(k);
        U.edit(() => { if (kind === "mute") L.muted = on; else L.soloed = on; });
        P.mutesChanged();
        U.toast(name(i) + word);
    }
    function applyPend(k, t) {
        const p = lp.pend.get(k), L = p && list()[p.i];
        lp.pend.delete(k);
        if (!L) return;
        U.edit(() => { if (p.kind === "mute") L.muted = !L.muted; else L.soloed = !L.soloed; });
        P.mutesChanged(t);                                 /* (from its line) */
    }
    const waiting = i => lp.pend.has(i + "/mute") ? "mute" : lp.pend.has(i + "/solo") ? "solo" : null;
    /* loop i gone: the waits of the loops after it move up with them */
    function rekeyPend(i) {
        const was = [...lp.pend.values()];
        lp.pend.clear();
        for (const p of was) if (p.i !== i) { const q = Object.assign({}, p, { i: p.i > i ? p.i - 1 : p.i }); lp.pend.set(q.i + "/" + q.kind, q); }
    }
    /* CONTROL mode's keys: white keys the loops (a tap at once, held at the bar line), black keys by their print: OP1-OP5
     * QUANTIZE, PIT all back on, GLO MUTE / SOLO, MONO and POLY the next loops' mode */
    const BLACK_PRINT = ["OP1", "OP2", "OP3", "OP4", "OP5", "OP6", "PIT", "GLO", "MONO", "POLY", ""];
    function controlKey(n, down) {
        const wi = U.whiteIndex(n), bi = U.blackIndex(n), j = LS();
        if (wi >= 0) {
            if (down) {
                if (!list()[wi]) return;
                const kind = lp.solo ? "solo" : "mute";
                const k = lp.held[n] = { kind, done: false };
                k.timer = setTimeout(() => { k.done = true; toggle(wi, kind, true); }, U.set("HOLD DELAY"));
                return;
            }
            const k = lp.held[n];
            delete lp.held[n];
            if (k) { clearTimeout(k.timer); if (!k.done) toggle(wi, k.kind, false); }
            return;
        }
        if (!down) return;
        const pr = BLACK_PRINT[bi];
        if (bi >= 0 && bi <= 4) {
            const q = M.QUANTIZES[bi];
            if (j.quantize !== q) U.edit(() => { j.quantize = q; });
            U.toast("QUANTIZE: " + q);
        } else if (pr === "PIT") {
            if (list().some(l => l.muted || l.soloed)) U.edit(() => { for (const l of j.list) { l.muted = false; l.soloed = false; } });
            lp.pend.clear();
            P.mutesChanged();
            U.toast("ALL LOOPS ON");
        } else if (pr === "GLO") {
            lp.solo = !lp.solo;
            U.toast(lp.solo ? "SEQ: SOLO" : "SEQ: MUTE");
        } else if (pr === "MONO" || pr === "POLY") {           /* (the keys' mode: how they play, how REC keeps the stage) */
            const mono = pr === "MONO";
            if (j.nextMono !== mono) U.edit(() => { j.nextMono = mono; });
            checkSetup();
            U.toast("KEYS: " + pr);
        }
    }
    /* the white keys' lights, a loop a key: full while it sounds, dim muted or silenced, blinking while a mute or solo
     * waits for the bar line, dark with no loop */
    function loopLeds(keys) {
        U.WHITE.forEach((n, i) => { const L = list()[i]; keys[n] = !L ? 0 : waiting(i) ? (U.blink(250) ? 1 : 0) : silenced(L) ? .25 : 1; });
    }

    /* ----------------------------------------------------------------- the keys --- */
    /* PLAY mode: a song armed or recording takes the key; armed (stopped), the key starts the loops at the cursor and its
     * note is the pass's first; the loops playing, the key goes on the stage (quantized, MONO / POLY, through the mix REC
     * puts it in), or with the stage off sounds at once in its sound, written nowhere; else the keys play freely */
    function playKey(n, down) {
        const m = U.midi(n), s = lp.song;
        if (m < 12 || m > 127) return;
        if (s && s.state === "armed" && !songStart()) return;
        if ((st.off || (songOn() && P.lpRestState().on)) && playingHere() && !P.countingIn()) { offKeyDown(n, m); return; }   /* (a song
                                                         * keeping the loops silent: the stage's line stands) */
        if (lp.arm && !P.countingIn() && !playingHere()) { startPlay(P.time()); recKeyDown(n, m); return; }
        if (playingHere() && !P.countingIn()) { recKeyDown(n, m); return; }
        U.keyOn(n, U.inst);
        lp.free[n] = true;
    }

    /* --------------------------------------------------------------- the knobs --- */
    /* TEMPO (SEL held: by 10) */
    function tempoTurn(s, coarse, quiet) {
        const j = LS(), t = U.clamp(j.tempo + s * (coarse ? 10 : 1), M.TEMPO_MIN, M.TEMPO_MAX);
        if (t !== j.tempo) U.edit(() => { j.tempo = t; });
        if (!quiet) U.showKnob("TEMPO", M.hex(j.tempo) + " (" + M.bpm(j) + " BPM)");
    }
    /* VIEW: 1, 2 or 4 bars a screen */
    const VIEWS = [1, 2, 4];
    function viewTurn(s, quiet) {
        const j = LS(), v = VIEWS[U.clamp(VIEWS.indexOf(j.view) + Math.sign(s), 0, VIEWS.length - 1)];
        if (v !== j.view) U.edit(() => { j.view = v; });
        if (!quiet) U.showKnob("VIEW", barsText(j.view));
    }
    /* K1-K4: each the job GLO gives it (M.LOOP_KNOBS): the selected loop's mix value (on + NEW LOOP the next loop's, whose
     * sends are the KEYS column's: a send turned there turns FX MIXER's KEYS column, a setting; SEL held: by 16), the
     * loops' TEMPO, or VIEW */
    const MIXK = { LEVEL: "lvl", PAN: "pan", DUCK: "dck", "CHORUS SEND": "cho", "DELAY SEND": "dly", "REVERB SEND": "rev" };
    function knobTurn(e, s, coarse) {
        const job = LS().knobs[e - E.K1];
        if (job === "TEMPO") { tempoTurn(s, coarse); return; }
        if (job === "VIEW") { viewTurn(s); return; }
        const k = MIXK[job], j = LS(), L = selLoop(), send = !L && ["cho", "dly", "rev"].includes(k);
        const row = L ? j.mixer[lp.sel] : send ? U.set("KEYS MIX") : j.nextMix;
        if (!k) return;
        const v = U.clamp(row[k] + s * (coarse ? 16 : 1), 0, k === "pan" ? 255 : 127);
        if (send) { if (v !== row[k]) U.changeSetting("KEYS MIX", { ...row, [k]: v }); }   /* (unsaved till SAVE; heard at once) */
        else if (v !== row[k]) U.edit(() => { row[k] = v; if (!L) syncMix(j); });
        if (L) PTFM.audio.mixUnset(16 + lp.sel, k);
        U.showKnob(job, U.mixWords(k, v));
    }
    /* ALGORITHM: the play position, a row a click (SEL held: a bar); playing, the loops jump there at once (a key held
     * stops writing; a loop landing in its rest falls silent; OVERWRITE goes on from where they land); stopped, where
     * PLAY starts */
    function algTurn(s, coarse) {
        const n = LS().rows, d = s * (coarse ? barRows() : 1);
        if (playingHere()) {
            overdubTick();
            stopWriting();
            P.lpJump(d);
            U.showKnob("JUMP", (d > 0 ? "+" : "") + d + (Math.abs(d) === 1 ? " ROW" : " ROWS"));
            return;
        }
        P.kept.loops.row = (((P.kept.loops.row || 0) + d) % n + n) % n;
        U.showKnob("PLAY FROM", barBeat(P.kept.loops.row));
    }
    /* the pipette (ENV held; SEL's TAKE ITS INSTRUMENT): the selected loop's instrument and MONO / POLY become the
     * current ones; nothing on + NEW LOOP */
    function takeItsInstrument(from) {
        const L = selLoop();
        if (!L) { U.refuse(from); return; }
        U.setInst(L.inst);
        if (!!LS().nextMono !== !!L.mono) U.edit(() => { LS().nextMono = !!L.mono; });
        checkSetup();
        U.toast("KEYS: " + M.refText(L.inst) + " " + U.instName(L.inst) + ", " + (L.mono ? "MONO" : "POLY"));
    }
    /* SEL → ADJUST: K1 shifts the selected loop a row, K2 a bar (round the length; the loops play on, the shift heard and
     * drawn at once); SEL keeps, HOME cancels. The SHIFT is the loop's own: no note moves */
    function adjustOpen() {
        const L = selLoop(), i = lp.sel;
        if (!L) return;
        lp.adj = { L, i, was: L.shift || 0 };
        const shiftBy = d => { const n = LS().rows; U.edit(() => { L.shift = (((L.shift || 0) + d) % n + n) % n; }); };
        U.open({
            kind: "adjust", opener: B.SEL,
            knob(e, s, coarse) {
                if (e === E.K1) shiftBy(s);
                else if (e === E.K2) shiftBy(s * barRows());
                else if (e === E.ALGORITHM) algTurn(s, coarse);
            },
            tap(b) {
                if (b === B.SEL) { U.close(this); lp.adj = null; U.toast(name(i) + ": SHIFT " + shiftText(L.shift || 0)); }
                return true;
            },
            cancel() {
                U.close(this);
                if ((L.shift || 0) !== lp.adj.was) U.edit(() => { L.shift = lp.adj.was; });
                lp.adj = null;
                U.toast("CANCELLED");
            },
            lit(b) { return b === B.SEL ? 1 : b === B.HOME ? .25 : b === B.REC || b === B.LFO || b === B.EDIT || b === B.GLO || b === B.ENV || b === B.SEQ || b === B.ARP ? 0 : undefined; },
            draw() {},
        });
    }
    /* a SHIFT shown signed, the nearer way round: -3 ROWS, +1 ROW */
    const shiftSigned = v => { const n = LS().rows; v = ((v % n) + n) % n; return v > n / 2 ? v - n : v; };
    const shiftText = v => { const s = shiftSigned(v); return (s > 0 ? "+" : "") + s + (Math.abs(s) === 1 ? " ROW" : " ROWS"); };

    /* ---------------------------------------------------------------- dialogs --- */
    const edit = fn => U.edit(() => fn(LS()));
    const step = (L, v, d) => L[U.clamp(L.indexOf(v) + Math.sign(d), 0, L.length - 1)];
    function props() {
        lp.lenPlan = null;
        const dlg = U.props("LOOPS PROPERTIES", [
            { name: "TEMPO", get: () => M.hex(LS().tempo) + " (" + M.bpm(LS()) + " BPM)", change: (d, c) => tempoTurn(d, c, true) },
            { name: "SWING", get: () => LS().swing + " %", change: d => edit(j => { j.swing = U.clamp(j.swing + Math.sign(d), 50, 75); }) },
            { name: "LENGTH", get: () => lp.lenPlan == null ? lengthText(LS().rows)
                                              : barsText(lp.lenPlan / barRows()) + " (NOW " + lengthText(LS().rows) + ")",
              col: on => lp.lenPlan == null ? (on ? D.WHITE : D.LGREY) : D.YELLOW, change: lengthTurn },
            { name: "GRID", get: () => (GRIDS.find(g => g[1] === LS().rpb) || GRIDS[0])[0], get fixed() { return list().length > 0 || stageHas(); }, change: gridTurn },
            { name: "QUANTIZE", get: () => LS().quantize, change: d => edit(j => { j.quantize = step(M.QUANTIZES, j.quantize, d); }) },
            { name: "MONO LEGATO", get: () => LS().legato, change: d => edit(j => { j.legato = step(["GLIDE", "LEGATO", "JUMP"], j.legato, d); }) },
            { name: "COUNT-IN", get: () => LS().countIn, change: d => edit(j => { j.countIn = step(M.LOOP_COUNT_INS, j.countIn, d); }) },
            { name: "VIEW", get: () => barsText(LS().view) + " A SCREEN", change: d => viewTurn(d, true) },
            ...[0, 1, 2, 3].map(k => ({ name: "K" + (k + 1) + " TURNS", get: () => LS().knobs[k],
                                        change: d => edit(j => { j.knobs[k] = step(M.LOOP_KNOBS, j.knobs[k], d); }) })),
        ]);
        /* a shorter LENGTH is only planned while GLO is open: it cuts when GLO closes (GLO or HOME), asking then, once,
         * if notes would go */
        const tap = dlg.tap;
        dlg.tap = function (b) { if (b === this.opener) { U.close(this); lengthApply(); return true; } return tap.call(this, b); };
        dlg.cancel = function () { U.close(this); lengthApply(); };
    }
    /* LENGTH: a bar more or less, with loops or none; longer adds silence at the end at once, shorter is planned
     * (lp.lenPlan, shown yellow) and cuts when GLO closes, asking first when notes would go. The stage is cut or made
     * longer with the loops */
    function lengthTurn(d) {
        const j = LS();
        const bR = barRows(), from = lp.lenPlan == null ? j.rows : lp.lenPlan, b = from / bR;
        const n = U.clamp(d > 0 ? Math.floor(b + 1e-9) + 1 : Math.ceil(b - 1e-9) - 1, 1, maxBars()) * bR;   /* (whole bars: from a part bar
                                                         * the next whole bar each way) */
        if (n === from) return;
        if (n < j.rows) { lp.lenPlan = n; return; }        /* (shorter: planned, nothing cut yet) */
        lp.lenPlan = null;
        if (n > j.rows) lengthSet(j, n);
    }
    /* the length n rows: a shifted loop first written out as it is heard over the old length (its SHIFT 0), so the bars
     * kept play as they did; then cut, or silence added at the end: before a longer length a loop whose last repeat the
     * old length cut is written out over it first (as DOUBLE does), its note sounding at the old line ending there as
     * it rests, and repeats past it dropped, so the new bars are silence. The stage cut or made longer with them; the
     * keys down stop writing */
    function lengthSet(j, n) {
        stopWriting();
        if (!U.edit(() => {
            for (const L of j.list) {
                if (L.shift) { if (L.shift % j.rows) heardOut(L, j.rows); else L.shift = 0; }
                if (n > j.rows && L.base * L.repeats > j.rows) {
                    if (j.rows % L.base) Object.assign(L, { cells: flatOf(L, j.rows, false), base: j.rows, repeats: 1 });
                    else L.repeats = j.rows / L.base;
                }
            }
            if (n > j.rows) j.rows = n; else cutTo(j, n);
        })) return;
        P.lpLength(n);
        fitStage();
        M.version++;
    }
    /* GLO closed: the planned shorter length cut, asking once when notes would go (KEEP: the length stays) */
    function lengthApply() {
        const j = LS(), n = lp.lenPlan;
        lp.lenPlan = null;
        if (n == null || n >= j.rows) return;
        if (notesGo(j, n)) U.ask(["CUT TO " + barsText(n / barRows()) + "?", "NOTES IN THE BARS CUT GO"], ["CUT", "KEEP"], a => { if (a === "CUT") lengthSet(j, n); });
        else lengthSet(j, n);
    }
    /* the loops (unshifted: lengthSet) cut at n rows: a base past it cut there and played once (a note still sounding at
     * the cut stops at the wrap, as the player stopped it: a note off at row 0); else as many repeats as reach it */
    function cutTo(j, n) {
        for (const L of j.list) {
            if (L.base > n) { const held = heldAt(L, n); L.base = n; L.cells.length = n * 4; L.repeats = 1; stopAt(L.cells, held, 0); }
            else L.repeats = Math.min(L.repeats, Math.ceil(n / L.base));
        }
        j.rows = n;
    }
    /* a note heard at row n or later (each loop as it is heard: its repeats, its SHIFT) */
    const notesGo = (j, n) => j.list.some(L => asHeard(L, j.rows).some((c, k) => isNote(c) && (k >> 2) >= n));
    /* GRID: only with no loops and the stage empty; the length keeps its bars (as many as the new grid allows; a part
     * bar's whole bars rounded down, at least 1) */
    function gridTurn(d) {
        const j = LS();
        if (j.list.length) { U.toast("GRID: ONLY WITH NO LOOPS"); return; }
        if (stageHas()) { U.toast("GRID: ONLY WITH THE STAGE EMPTY"); return; }
        const i = U.clamp(GRIDS.findIndex(g => g[1] === j.rpb) + Math.sign(d), 0, GRIDS.length - 1);
        if (GRIDS[i][1] === j.rpb) return;
        const bars = Math.max(1, Math.floor(j.rows / barRows() + 1e-9));
        stopWriting();
        lp.lenPlan = null;                                 /* (a planned LENGTH dropped: the rows change) */
        U.edit(() => { j.rpb = GRIDS[i][1]; j.rows = Math.min(bars, maxBars()) * barRows(); });
        P.lpLength(j.rows);
        fitStage();
    }
    /* ENV: the cartridges; the current instrument becomes the picked one's first */
    function carts() {
        U.cartList("NEW LOOP: A CARTRIDGE", U.place >> 8, c => {
            const first = c.slots.findIndex((s, i) => M.slotUsed(c, i, true));   /* (FAVORITES: its first favourite's source) */
            if (first < 0) { U.toast("CARTRIDGE " + M.hex(c.id) + " IS EMPTY"); return; }
            U.setInst(c.id << 8 | first);
        });
    }
    /* SEL → DELETE LOOP: the selected loop goes at once (REVERT the way back); the loops below move up, the selection on
     * the one that moved into its place; the length stays */
    function deleteLoop() {
        const j = LS(), i = lp.sel, was = j.list.slice();
        if (!selLoop() || refused(B.SEL)) return;
        if (!U.edit(() => { j.list.splice(i, 1); j.mixer.splice(i, 1); j.mixer.push({}); syncMix(j); })) return;
        P.lpPeeled(i, was);
        rekeyPend(i);
        st.silent = st.silent === i ? -1 : st.silent > i ? st.silent - 1 : st.silent;   /* (the stage stays) */
        lp.sel = U.clamp(lp.sel, 0, topSel());
        syncSel();
        P.mutesChanged();
        U.toast(name(i) + " DELETED");
    }
    /* SEL → QUANTIZE: the selected loop's notes to the nearest line of the size picked (2, 3, 4 ROWS, ROW), the lines
     * counted as the loop is heard (from the loops' start, its SHIFT counted, its first repeat), back in its base; REAL
     * TIME's note delays and key offs dropped; each note off moved with its note (its length kept, a row at least); two
     * notes landing on one lane's row: the later wins; a note rounded past the base's end goes to its start (before it, to
     * its end). In whole ticks (sp a row), a half rounded up: exact, as the firmware's */
    function quantizeLoop(L, i, q) {
        const Q = { "2 ROWS": 2, "3 ROWS": 3, "4 ROWS": 4 }[q] || 1, sp = LS().ticks, base = L.base, cells = new Array(base * 4).fill(null);
        const rows = LS().rows, sh = (((L.shift || 0) % rows) + rows) % rows;
        const bare = c => { const d = M.copyCell(c); d.cmds = d.cmds.filter(x => x.c !== 0x14 && !(x.c === 0x0E && x.v >> 4 === 0xD)); return d; };
        const half = (a, b) => Math.floor((2 * a + b) / (2 * b));   /* (a / b to the nearest, a half up; a >= 0) */
        for (let l = 0; l < 4; l++) {
            const notes = [];
            let cur = null;
            for (let r = 0; r < base; r++) {
                const c = L.cells[r * 4 + l];
                if (!c) continue;
                const note = isNote(c), ed = note ? cmdOf(c, 0x0E, 0xD) : null, d = ed == null ? 0 : ed & 15, k = cmdOf(c, 0x14);
                if (k != null && (!note || k < d) && cur) { cur.end = r * sp + k; cur = null; }
                if (c.note === M.OFF && cur) { cur.end = r * sp; cur = null; }
                if (!note) continue;
                cur = { start: r * sp + d, end: null, cell: c };
                notes.push(cur);
                if (k != null && k > d) { cur.end = r * sp + k; cur = null; }
            }
            for (const nt of notes) {
                const heard = (nt.start + sh * sp) % (rows * sp), at = half(heard, Q * sp) * Q;   /* (its line, where it is heard) */
                const ns = (((nt.start + at * sp - heard) / sp % base) + base) % base;   /* (start - heard: whole rows) */
                cells[ns * 4 + l] = bare(nt.cell);
                if (nt.end == null) continue;
                const ne = ns + Math.max(1, nt.end > nt.start ? half(nt.end - nt.start, sp) : 0);
                if (ne < base && !cells[ne * 4 + l]) cells[ne * 4 + l] = M.newCell(M.OFF, null, null);
            }
        }
        if (!U.edit(() => { L.cells = cells; })) return;
        U.toast(name(i) + " QUANTIZED: " + q);
    }
    function menu() {
        const L = selLoop();
        const canDouble = LS().list.length > 0 && LS().rows * 2 <= maxRows();   /* (shown only when it can be done) */
        U.menu("loops", L ? name(lp.sel) + " " + U.instName(L.inst) : "LOOPS",
               [L && "TAKE ITS INSTRUMENT", L && "APPLY INSTRUMENT", L && "ADJUST", L && "QUANTIZE", L && "DELETE LOOP", canDouble && "DOUBLE",
                "RECORD PATTERN", songArmed() ? "CANCEL RECORD LIVE" : !songOn() && "RECORD LIVE", "SAVE TO PATTERN", "LOAD FROM PATTERN",
                stageHas() && "CLEAR STAGE", "CLEAR LOOPS", M.unsavedThing("loops") && "REVERT"], a => {
            const none = () => { if (LS().list.length) return false; U.toast("NO LOOPS YET"); return true; };
            if (a === "APPLY INSTRUMENT") {                /* (the keys' MONO / POLY with it) */
                const i = list().indexOf(L);
                if (i < 0 || !U.edit(() => { L.inst = U.inst; L.mono = !!LS().nextMono; })) return;
                U.toast(name(i) + ": " + M.refText(U.inst) + " " + U.instName(U.inst) + ", " + (L.mono ? "MONO" : "POLY"));
            }
            else if (a === "TAKE ITS INSTRUMENT") takeItsInstrument(B.SEL);
            else if (a === "ADJUST") { if (!refused(B.SEL)) adjustOpen(); }
            else if (a === "QUANTIZE") {
                if (refused(B.SEL)) return;
                const i = lp.sel;
                U.menu("loops-q", "QUANTIZE " + name(i), ["2 ROWS", "3 ROWS", "4 ROWS", "ROW"], q => { if (list()[i] === L) quantizeLoop(L, i, q); });
            }
            else if (a === "DELETE LOOP") deleteLoop();
            else if (a === "DOUBLE") {                     /* (the stage doubled with the loops: played twice) */
                if (none() || refused(B.SEL)) return;
                stopWriting();
                fitStage();
                const was = st.cells, twice = was && was.concat(was.map(c => c && M.copyCell(c))), why = twice && stageRefusal(twice);
                if (why) { U.refuse(B.SEL); U.toast(why); return; }
                st.cells = twice;                          /* (counted as the loops double: one edit's room) */
                if (!double()) st.cells = was;
            }
            else if (a === "RECORD PATTERN") prStart([B.SEL]);   /* (ARP held + REC's tap route) */
            else if (a === "RECORD LIVE") {                /* its panel */
                if (none() || refused(B.SEL)) return;
                if (songNumber() < 0) { U.refuse(B.SEL); U.toast("NO SONG NUMBER FREE"); return; }
                slOpen();
            }
            else if (a === "CANCEL RECORD LIVE") { songCancel(); U.toast("CANCELLED"); }
            else if (a === "SAVE TO PATTERN") { if (!none()) saveTo(); }
            else if (a === "LOAD FROM PATTERN") { if (!refused(B.SEL)) loadFrom(); }
            else if (a === "CLEAR STAGE") stageClear(B.SEL);
            else if (a === "CLEAR LOOPS") { if (!none() && !refused(B.SEL)) clearAll(); }
            else if (a === "REVERT") { if (!refused(B.SEL)) revert(); }
        });
    }
    /* DOUBLE: the length x2; a loop filling the length exactly doubles its repeats, any other has its notes written out
     * to the old length first and plays twice; a SHIFT kept (the loop plays the same). True: done */
    function double() {
        const j = LS();
        if (j.rows * 2 > maxRows()) { U.refuse(B.SEL); U.toast("DOUBLE: AT MOST " + barsText(maxBars())); return false; }
        if (!U.edit(() => {
            for (const L of j.list) {
                if (L.base * L.repeats === j.rows) L.repeats *= 2;
                else { writeOut(L, j.rows); L.repeats = 2; }
            }
            j.rows *= 2;
        })) return false;
        P.lpLength(j.rows);
        U.toast("DOUBLED: " + lengthText(j.rows));
        return true;
    }
    /* a loop's notes written out over n rows: its repeats one after another, the last cut at n. Where the player stops its
     * lanes, a note still sounding there gets a note off: where its repeats end short of n (its rest), or at row 0 when n
     * cuts the last repeat (the stop at the wrap; not when wrap is false: a longer length, where its rest stops it) */
    function flatOf(L, n, wrap = true) {
        const cells = new Array(n * 4).fill(null), held = heldAt(L, n), end = L.base * L.repeats;
        for (let r = 0; r < n && Math.floor(r / L.base) < L.repeats; r++)
            for (let l = 0; l < 4; l++) { const c = L.cells[(r % L.base) * 4 + l]; if (c) cells[r * 4 + l] = M.copyCell(c); }
        stopAt(cells, held, end < n ? end : n % L.base && wrap ? 0 : -1);
        return cells;
    }
    function writeOut(L, n) { L.cells = flatOf(L, n); L.base = n; }
    /* loop L as it is heard over the length (rows): written out (flatOf), then turned SHIFT rows round it. Whatever reads a
     * loop reads it so: EDIT, RECORD LIVE's song (laneAt, as the player), SAVE TO PATTERN, QUANTIZE's lines,
     * LENGTH */
    function asHeard(L, rows) {
        const flat = flatOf(L, rows), sh = (((L.shift || 0) % rows) + rows) % rows;
        if (!sh) return flat;
        const cells = new Array(rows * 4).fill(null);
        for (let k = 0; k < rows * 4; k++) cells[(((k >> 2) + sh) % rows) * 4 + (k & 3)] = flat[k];
        return cells;
    }
    /* a shifted loop written out as it is heard, its SHIFT 0 (LENGTH: the bars kept play as they did) */
    function heardOut(L, rows) { Object.assign(L, { cells: asHeard(L, rows), base: rows, repeats: 1, shift: 0 }); }
    /* 1: a lane's note still sounds after cell c (was: before it): a note starts one, a note off or a key off ends it */
    const sounding = (was, c) => c.cmds.some(x => x.c === 0x14) ? false : c.note != null ? c.note !== M.OFF : was;
    /* the lanes (bit l) of loop L whose note still sounds at row n, its repeats written out to there */
    function heldAt(L, n) {
        let held = 0;
        for (let r = 0; r < n && Math.floor(r / L.base) < L.repeats; r++)
            for (let l = 0; l < 4; l++) { const c = L.cells[(r % L.base) * 4 + l]; if (c) held = sounding(held >> l & 1, c) ? held | 1 << l : held & ~(1 << l); }
        return held;
    }
    /* a note off at row r (-1: none) on the lanes held (bits): a cell there without a note takes it */
    function stopAt(cells, held, r) {
        for (let l = 0; l < 4 && r >= 0; l++) {
            const c = cells[r * 4 + l];
            if (!(held >> l & 1) || isNote(c) || (c && c.note === M.OFF)) continue;
            cells[r * 4 + l] = c ? Object.assign(M.copyCell(c), { note: M.OFF }) : M.newCell(M.OFF, null, null);
        }
    }
    /* CLEAR LOOPS: every loop goes, and the stage's notes, after a question; the length stays, the loops play on empty */
    function clearAll() {
        const j = LS(), n = j.list.length;
        U.ask(["CLEAR ALL " + n + (n === 1 ? " LOOP?" : " LOOPS?"), "THE STAGE TOO; THE LENGTH STAYS"], ["CLEAR", "KEEP"], a => {
            if (a !== "CLEAR" || !U.edit(() => { j.list = []; syncMix(j); })) return;
            P.lpCleared();
            lp.pend.clear();
            clearStage();                                  /* (the stage emptied too; the length stays) */
            lp.sel = 0;
            syncSel();
            P.mutesChanged();
            U.toast("LOOPS CLEARED");
        });
    }
    /* REVERT: LOOPS as saved (the flash's plays again, from the next row); the stage kept, fitted to the length */
    function revert() {
        U.askRevert("REVERT LOOPS TO SAVED?", null, () => {
            stopWriting();
            M.revertThing("loops");
            syncSel();
            lp.pend.clear();
            st.silent = -1;
            fitStage();
            P.mutesChanged();
        });
    }

    /* ------------------------------------------------- storing in a pattern, loading one --- */
    /* SAVE TO PATTERN: the pattern list, NEW first and picked; a pattern not empty asks first */
    function saveTo() {
        U.patternList("SAVE THE LOOPS TO", null, n => {
            if (n === "NEW") { n = M.freePattern(); if (n < 0) { U.toast("ALL " + (M.USER_LAST + 1) + " PATTERNS USED"); return; } }
            if (M.patternHolds(n)) U.ask(["PATTERN " + M.hex(n) + " IS NOT EMPTY", "REPLACE IT WITH THE LOOPS?"], ["YES", "NO"], a => { if (a === "YES") store(n); });
            else store(n);
        }, B.SEL, { plus: true, plusText: "NEW" });
    }
    /* the loops written out into pattern n, as long as they are: every loop as it is heard (asHeard: its repeats, a note
     * off where it goes to rest with a note still sounding, the cut at the wrap, turned SHIFT rows), each note naming its
     * instrument; each lane onto the first track free over all its rows (a lane sounding at the length's end rings into
     * its start: the track whole; what does not fit left out, counted); a loop's mix as commands (= LEVEL and the sends,
     * 8xx PAN) with its first note on a track, where the track's mix there differs (as the lanes before it in time left
     * it). The stage is not written */
    function store(n) {
        const j = LS(), rows = j.rows, p = M.newPattern(n, 1, rows), taken = [], mixOn = [], lanes = [];
        let left = 0, used = 0;
        j.list.forEach((L, i) => {
            const heard = asHeard(L, rows);
            for (let l = 0; l < 4; l++) {
                const ev = [];
                for (let r = 0; r < rows; r++) { const c = heard[r * 4 + l]; if (c) ev.push([r, c]); }
                if (!ev.some(([, c]) => isNote(c))) continue;
                const held = ev.reduce((h, [, c]) => sounding(h, c), false);
                const from = held ? 0 : ev[0][0], to = held ? rows : ev[ev.length - 1][0] + 1;
                let t = 0;
                while (t < 16 && (taken[t] || []).some(([a, b]) => from < b && a < to)) t++;
                if (t >= 16) { left += ev.filter(([, c]) => isNote(c)).length; continue; }
                (taken[t] = taken[t] || []).push([from, to]);
                used = Math.max(used, t + 1);
                lanes.push({ i, L, t, from, ev });
            }
        });
        for (const { i, L, t, ev } of lanes.sort((a, b) => a.t - b.t || a.from - b.from)) {   /* (each track's lanes in time order) */
            const want = j.mixer[i], have = mixOn[t] = mixOn[t] || { lvl: 127, pan: 128, dck: 0, cho: 0, dly: 0, rev: 0 };
            let first = true;
            for (const [r, c] of ev) {
                const d = M.copyCell(c);
                if (isNote(d)) {
                    d.inst = L.inst;
                    if (first) for (const [k, code] of [["lvl", 0x80], ["pan", 0x08], ["dly", 0x82], ["rev", 0x83], ["cho", 0x81], ["dck", 0x84]])
                        if (want[k] !== have[k] && d.cmds.length < 4) { d.cmds.push({ c: code, v: want[k] }); have[k] = want[k]; }
                    first = false;
                }
                p.cells[r * 16 + t] = d;
            }
        }
        p.tracks = Math.max(1, used);
        const was = M.pattern(n);
        if (was && was.builtin) p.builtin = true;            /* (a built-in pattern takes them as any: SAVE keeps the change) */
        if (!U.edit(() => { M.state.patterns[n] = p; })) return;
        U.toast(left ? "PATTERN " + M.hex(n) + ": " + left + (left === 1 ? " NOTE LEFT OUT" : " NOTES LEFT OUT") : "LOOPS STORED IN PATTERN " + M.hex(n));
    }
    /* LOAD FROM PATTERN's timing, as the pattern plays in its song (the current song if it plays it, else the first that
     * does): its TEMPO, TICKS A ROW, ROWS A BEAT and SWING; ROWS A BEAT that GRID lacks keeps the loops' GRID, the TEMPO
     * scaled to the same BPM; a pattern in no song keeps the loops' own. The list's PLAY plays each pattern at it */
    function patTiming(n) {
        const j = LS(), cur = M.state.songs[U.song];
        const s = cur && cur.order.includes(n) ? cur : M.songList().find(x => x.order.includes(n));
        if (!s) return { tempo: j.tempo, ticks: j.ticks, rpb: j.rpb, swing: j.swing, gvol: 64 };
        const rpb = GRIDS.some(g => g[1] === s.rpb) ? s.rpb : j.rpb;
        return { tempo: U.clamp(Math.round(s.tempo * rpb / s.rpb), M.TEMPO_MIN, M.TEMPO_MAX), ticks: s.ticks, rpb, swing: U.clamp(s.swing, 50, 75), gvol: 64 };
    }
    function loadFrom() {
        U.patternList("LOAD INTO LOOPS", U.pattern, n => {
            if (n === "NEW" || !M.pattern(n)) return;
            if (list().length) U.ask(["REPLACE THE LOOPS", "WITH PATTERN " + M.hex(n) + "?"], ["YES", "NO"], a => { if (a === "YES") load(n); });
            else load(n);
        }, B.SEL, { timing: patTiming });
    }
    /* a pattern in place of the loops: its rows in whole bars (128 at most); a loop an instrument, in the order they first
     * play, its notes from every track that plays it, up to 4 at once as its lanes (more at once: another loop of it);
     * each POLY, once, its mix from the commands with its notes; glides, note delays and key offs kept, other commands
     * left out; 16 loops at most. Where a track's note ends (a note, a note off, a key off alone), its lane gets a note off
     * at the same tick (EDx with a note delay, a key off as itself); ticks into the row, the lane is free in that row only
     * for the track's own next note */
    function load(n) {
        const p = M.pattern(n), j = LS(), tm = patTiming(n), bR = tm.rpb * beats();
        const most = Math.max(1, Math.floor(M.PAT_ROWS_MAX / bR)), rows = Math.min(most, Math.ceil(p.rows / bR)) * bR, lim = Math.min(rows, p.rows);
        const KEEP = x => x.c === 0x14 || (x.c === 0x0E && x.v >> 4 === 0xD) || x.c === 0x03 || x.c === 0xC0 || x.c === 0xC1 || x.c === 0xC2 ||
                          x.c === 0xD3;                     /* (LEG too) */
        const MIX = { 0x80: "lvl", 0x08: "pan", 0x81: "cho", 0x82: "dly", 0x83: "rev", 0x84: "dck" };
        const notes = [];
        let cmds = 0;
        for (let t = 0; t < p.tracks; t++) {
            let inst = null, last = null;
            for (let r = 0; r < lim; r++) {
                const c = p.cells[r * 16 + t];
                if (!c) continue;
                if (c.inst != null) inst = c.inst;
                const note = isNote(c), DL = x => c.note != null && x.c === 0x0E && x.v >> 4 === 0xD;   /* (a note off's too) */
                const k = c.cmds.filter(x => x.c === 0x14).pop(), d = c.cmds.filter(DL).pop();
                let prev = null;
                if ((c.note != null || k) && last) {
                    const tick = c.note != null ? (d ? d.v & 15 : 0) : k.v;
                    last.end = r;
                    last.tick = tick;
                    last.off = c.note != null ? { note: M.OFF, inst: null, vol: null, cmds: d && tick ? [{ c: 0x0E, v: 0xD0 | tick }] : [] }
                                              : { note: null, inst: null, vol: null, cmds: [{ c: 0x14, v: k.v }] };
                    prev = last;
                    last = null;
                }
                for (const x of c.cmds) if (!(note && (KEEP(x) || MIX[x.c] != null)) && !(x.c === 0x14) && !DL(x)) cmds++;
                if (!note || inst == null) continue;
                const mix = {};
                for (const x of c.cmds) if (MIX[x.c] != null) mix[MIX[x.c]] = x.v;
                last = { r, inst, end: rows, tick: 0, off: null, prev, mix, c: { note: c.note, inst: null, vol: c.vol, cmds: c.cmds.filter(KEEP).map(x => ({ c: x.c, v: x.v })) } };
                notes.push(last);
            }
        }
        notes.sort((a, b) => a.r - b.r);
        const loops = [], by = new Map(), out = new Set();
        const place = (L, l, nt) => {
            L.cells[nt.r * 4 + l] = nt.c;
            if (nt.end < rows && !L.cells[nt.end * 4 + l]) L.cells[nt.end * 4 + l] = nt.off;
            L.busy[l] = nt.tick ? nt.end + 1 : nt.end;
            nt.at = [L, l];
            L.lanes = Math.max(L.lanes, l + 1);
            if (!L.mix && Object.keys(nt.mix).length) L.mix = nt.mix;
        };
        for (const nt of notes) {
            const ls = by.get(nt.inst) || [];
            let done = false;
            const own = (L, l) => nt.prev && nt.prev.at && nt.prev.at[0] === L && nt.prev.at[1] === l;   /* (its track's note's lane) */
            for (const L of ls) for (let l = 0; l < 4 && !done; l++) if (L.busy[l] <= nt.r || own(L, l)) { place(L, l, nt); done = true; }
            if (done) continue;
            if (loops.length >= M.LOOPS_MAX) { out.add(nt.inst); continue; }
            const L = { inst: nt.inst, busy: [0, 0, 0, 0], cells: new Array(rows * 4).fill(null), mix: null, lanes: 1 };
            loops.push(L); ls.push(L); by.set(nt.inst, ls);
            place(L, 0, nt);
        }
        stopWriting();
        if (!U.edit(() => {
            j.list = loops.map((L, k) => ({ inst: L.inst, col: k % 12, mono: false, lanes: L.lanes, base: rows, repeats: 1, muted: false, soloed: false, cells: L.cells,
                                            shift: 0 }));
            j.mixer = loops.map(L => Object.assign({ lvl: 127, pan: 128, dck: 0, cho: 0, dly: 0, rev: 0 }, L.mix || {}));
            while (j.mixer.length < 16) j.mixer.push({});
            syncMix(j);
            Object.assign(j, { rows, tempo: tm.tempo, ticks: tm.ticks, rpb: tm.rpb, swing: tm.swing });   /* (with notes or none) */
        })) return;
        P.lpCleared();
        lp.pend.clear();
        st.silent = -1;
        fitStage();
        if (songOn()) { P.lpRest(1); P.lpRest(2); }        /* (the song's time runs on: the loops silent, at their start) */
        else if (playingHere()) P.pause();
        P.kept.loops.row = 0;
        lp.sel = 0;
        P.mutesChanged();
        const say = (k, w) => k + " " + w + (k === 1 ? "" : "S");
        U.toast("PATTERN " + M.hex(n) + " IN LOOPS: " + (out.size ? say(out.size, "INSTRUMENT") + " LEFT OUT" : cmds ? say(cmds, "COMMAND") + " LEFT OUT" : say(loops.length, "LOOP")));
    }

    /* ---------------------------------------------------- the pattern recorder (ARP) --- */
    /* 4 rows of up to 16 steps, a step a grid row, a note a row (a row never set: the note of the row above it, the first
     * C at the keyboard's octave as it opens), NOTE LENGTH, LENGTH 4-16 steps; all clean at every open, never saved.
     * Its preview is the loop it would write (P.lpPreview, loop 16 of the player's lanes), repeated over the length: in
     * the loops' time while they play (ARP switches its sound), else alone from its first step */
    const rec = { o: null };
    const pr = { rows: [0, 1, 2, 3].map(() => ({ note: null, steps: new Array(16).fill(false) })), sel: 0, noteLen: "1/16", len: 16,
                 keys: false };
    const prOpen = () => !!rec.o && U.overlays.includes(rec.o);
    const prPress = {};                                    /* (a step's key down: a tap or a hold, decided later) */
    /* no orphan held steps: a held step whose step before is off becomes a plain note (a held run starts with one) */
    function prNoOrphans(st) { for (let k = 0; k < st.length; k++) if (st[k] === "hold" && (k === 0 || !st[k - 1])) st[k] = true; }
    /* K1 NOTE LENGTH, how long each step's note is held: the lengths GRID makes whole rows of (beats: 1/32 an eighth
     * ... 1 BAR), and TO THE NEXT (rings until its row's next note) */
    const NOTE_LENS = [["1/32", 1 / 8], ["1/16T", 1 / 6], ["1/16", 1 / 4], ["1/8T", 1 / 3], ["1/8", 1 / 2], ["1/4", 1], ["1/2", 2], ["1 BAR", 0]];
    const prLens = () => NOTE_LENS.map(([n, b]) => [n, b ? LS().rpb * b : barRows()]).filter(([, r]) => Math.abs(r - Math.round(r)) < 1e-9 && r >= 1)
        .map(([n, r]) => [n, Math.round(r)]).concat([["TO THE NEXT", null]]);
    const prLenRows = () => { const s = prLens().find(x => x[0] === pr.noteLen); return s ? s[1] : 1; };   /* (null: to the next) */
    const prBase = () => pr.len;
    const prNote = r => { for (let k = r; k >= 0; k--) if (pr.rows[k].note != null) return pr.rows[k].note; return U.clamp(60 + 12 * U.octave, 12, 127); };
    const prFilled = r => pr.rows[r].steps.slice(0, pr.len).some(x => x === true);   /* (a row of ties alone: no note) */
    const prLanes = () => [0, 1, 2, 3].filter(prFilled);
    const pvRows = () => LS().rows;                        /* (the preview alone goes round the length) */
    const prTarget = () => rec.o && list()[rec.o.target] ? rec.o.target : -1;
    const prInst = () => prTarget() >= 0 ? list()[prTarget()].inst : U.inst;
    const prCol = () => prTarget() >= 0 ? colOf(list()[prTarget()]) : M.LOOP_COLS[nextCol()];
    /* the loop it writes: the filled rows its lanes in order, a note at KEYS VOLUME at each step on; its base the steps,
     * as many repeats as cover the length, the last cut */
    function prLoop() {
        const base = prBase(), cells = new Array(base * 4).fill(null), vol = U.set("KEYS VOLUME"), ls = prLanes();
        const nl = prLenRows();
        ls.forEach((r, l) => {
            /* a step on: a note, held NOTE LENGTH rows, its held steps (ties) after it adding theirs on top; the row's next
             * note always ends it (the pattern round: the first note of the next repeat); TO THE NEXT: no end */
            const st = pr.rows[r].steps, note = prNote(r), on = [];
            for (let k = 0; k < pr.len; k++) if (st[k] === true) { cells[k * 4 + l] = M.newCell(note, null, vol); on.push(k); }
            if (nl == null) return;
            on.forEach((k, x) => {
                let ties = 0;
                while (ties < pr.len - 1 && st[(k + 1 + ties) % pr.len] === "hold") ties++;
                const end = k + ties + nl, next = x + 1 < on.length ? on[x + 1] : on[0] + pr.len;
                if (end < next) { const e = end % base; if (!cells[e * 4 + l]) cells[e * 4 + l] = M.newCell(M.OFF, null, null); }
            });
        });
        const t = prTarget();
        return { inst: prInst(), base, repeats: Math.ceil(LS().rows / base), cells, lanes: Math.max(1, ls.length),
                 mono: ls.length === 1, track: t >= 0 ? t : list().length };
    }
    P.lpPreview = () => prOpen() && rec.o.pv ? prLoop() : null;
    const prPlaying = () => prOpen() && rec.o.pv && (playingHere() || P.pvOn());
    /* the step playing: where the loops are (or the preview alone), in its frame; -1 none */
    function prStep() {
        if (!prPlaying()) return -1;
        let r;
        if (playingHere()) { const h = P.at(); if (!h || h.pass == null) return -1; r = h.row % Math.max(1, LS().rows); }
        else { const h = P.pvAt(); if (!h) return -1; r = h.row; }
        return r % prBase();
    }
    /* a row's note heard, on the stage's column: through the mix the loop takes, never its mute or a solo */
    function prHear(note) {
        const t = P.time();
        PTFM.audio.stageCol(16 + (prTarget() >= 0 ? prTarget() : list().length));
        P.noteOff("rv", t);
        P.noteOn("rv", note, prInst(), U.set("KEYS VOLUME"), P.STAGE_T, t, { song: true });
        P.noteOff("rv", t + Math.max(.15, P.rowSeconds() * (prLenRows() || 1)));   /* (heard NOTE LENGTH long; TO THE NEXT a row) */
    }
    function prSetNote(n) { pr.rows[pr.sel].note = U.clamp(n, 12, 127); prHear(pr.rows[pr.sel].note); }
    /* it opens (ARP held + REC, at REC's release; SEL's RECORD PATTERN): refused while armed and during a count-in, the
     * buttons it came from blinking; with the stage off it opens, its REC turning the stage on */
    function prStart(from) {
        const no = () => { for (const b of from) U.refuse(b); };
        if (lp.arm || P.countingIn()) { no(); return; }
        for (const r of pr.rows) { r.note = null; r.steps.fill(false); }   /* (clean every time: nothing of the last) */
        pr.sel = 0;
        pr.len = 16;
        pr.keys = false;
        const lens = prLens();                             /* (1/16 at first; a GRID without it: a row's) */
        pr.noteLen = (lens.find(s => s[0] === "1/16") || lens.find(s => s[1] === 1) || lens[0])[0];
        pr.rows[0].note = prNote(0);                       /* (the first row's C kept from here: OCT moves the selected row's) */
        rec.o = U.open(Object.assign({ kind: "recorder", opener: B.ARP, target: selLoop() ? lp.sel : -1, pv: true }, RECORDER));
        if (!playingHere()) {                              /* (the preview plays from the moment it opens: alone, what played
                                                            * on into LOOPS paused for it, its place kept) */
            if (P.playing) P.pause();
            P.pvStart(0);
        }
    }
    function prClose() {
        if (rec.o) U.close(rec.o);
        rec.o = null;
        P.pvStop();
        P.lpEnd(16);
    }
    /* REC: the pattern onto the stage, replacing what it held (as EDIT does), its steps repeated over the
     * length (the last repeat cut); REC on the screen then sends the stage to LOOPS. The stage plays in the sound the
     * preview had (opened on a loop: that loop's instrument becomes the current one), POLY when the pattern fills more
     * than one row; the stage on, if it was off; OVERDUB, as EDIT's fill. Going on from where the preview was */
    function prWrite(o) {
        if (!prLanes().length) { U.refuse(B.REC); U.toast("NO STEPS ON"); return false; }
        const j = LS(), rows = j.rows, W = prLoop(), was = playingHere(), alone = !was && o.pv && P.pvOn();
        const cells = new Array(rows * 4).fill(null);
        for (let r = 0; r < rows; r++) for (let l = 0; l < 4; l++) { const c = W.cells[(r % W.base) * 4 + l]; if (c) cells[r * 4 + l] = M.copyCell(c); }
        stopAt(cells, heldAt({ cells, base: rows, repeats: 1 }, rows), 0);
        const why = stageRefusal(cells);
        if (why) { U.refuse(B.REC); U.toast(why); return false; }
        if (prInst() !== U.inst) U.setInst(prInst());
        if (W.lanes > 1 && j.nextMono) U.edit(() => { j.nextMono = false; });
        st.setup = setupNow();
        P.lpStageMute();
        stopWriting();
        st.cells = cells;
        st.erasers = [];
        st.off = false;                                    /* (an explicit fill of the stage turns it on) */
        st.over = false;
        syncSilent();
        if (was || alone) P.lpMoveLanes(16, P.LP_STAGE);  /* the preview's notes go on as the stage's */
        if (alone) { P.pvAdopt(); syncRun(); }            /* (alone: its replay the loops' from where it is) */
        else if (!was) P.pvStop();
        M.version++;
        U.toast("PATTERN ON THE STAGE");
        return true;
    }
    /* PLAY: the loops played or paused, as on the screen; paused, a preview playing goes on alone from where it was;
     * started, the preview jumps to their place */
    function prPlay(o) {
        if (playingHere()) {
            const nx = P.lpNext(), n = Math.max(1, LS().rows);
            P.pause();
            if (o.pv && nx) P.pvStart(nx.row % n, nx.t);
            return;
        }
        P.pvStop();
        P.lpEnd(16);
        startPlay();
    }
    /* ARP: the loops playing, the preview's sound off and on; else it starts and stops alone, from its first step */
    function prArp(o) {
        if (playingHere()) { o.pv = !o.pv; if (!o.pv) P.lpEnd(16); return; }
        if (P.pvOn()) { P.pvStop(); o.pv = false; return; }
        o.pv = true;
        P.pvStart(0);
    }
    /* "what it makes": its repeats over the length; its lanes */
    function prMakes() {
        const base = prBase(), k = prLanes().length, lanes = k + (k === 1 ? " LANE" : " LANES");
        const len = LS().rows, whole = Math.floor(len / base), rest = len % base;
        return whole + " X " + base + (rest ? " + " + rest : "") + " = " + len + " ROWS, " + lanes;
    }
    const SX = k => 30 + 12 * k + 2 * Math.floor(k / 4);    /* step k's x */
    const RECORDER = {
        play() { prPlay(this); },
        rec() { if (!recQuiet() && prWrite(this)) { prClose(); recDone(); } return true; },   /* (REC's job: then 500 ms of nothing) */
        cancel() { prClose(); },                           /* HOME: nothing written */
        knob(e, s, coarse) {
            const d = Math.sign(s);
            if (e === E.SELECT) pr.sel = U.clamp(pr.sel + d, 0, 3);
            else if (e === E.ALGORITHM) tempoTurn(s, coarse);
            else if (e === E.K1) { const L = prLens(); pr.noteLen = L[U.clamp(L.findIndex(x => x[0] === pr.noteLen) + d, 0, L.length - 1)][0]; }
            else if (e === E.K2) pr.len = U.clamp(pr.len + 4 * d, 4, 16);
            else if (e === E.K3) prSetNote(prNote(pr.sel) + s);
        },
        tap(b) {
            if (b === B.ARP) prArp(this);
            else if (b === B.SEL) { pr.keys = !pr.keys; U.toast(pr.keys ? "KEYS: THE NOTE" : "KEYS: THE STEPS"); }
            else if (b === B.OCTM || b === B.OCTP) {           /* the keyboard's octave, the row's note with it */
                const d = b === B.OCTP ? 1 : -1, n = prNote(pr.sel) + 12 * d;
                if (U.octave + d < -4 || U.octave + d > 4 || n < 12 || n > 127) { U.refuse(b); return true; }
                U.octave += d;
                prSetNote(n);
                U.toast("NOTE " + M.noteName(n));
            }
            else if (b === B.SAVE) return false;              /* (SAVE as everywhere) */
            return true;
        },
        /* a white key: tapped, its step on or off, at its release; held past the hold delay, the step held (a
         * tie) when the step before is on or held, else a plain note (a held run starts with a plain note). A step on is
         * heard while the preview is stopped. After SEL, or SEL held, a key sets the row's note at the keyboard's octave */
        key(n, down) {
            const k = U.WHITE.indexOf(n), row = pr.sel, st = pr.rows[row].steps;
            const set = v => { st[k] = v; prNoOrphans(pr.rows[row].steps); if (st[k] === true && !prPlaying()) prHear(prNote(row)); };
            if (!down) {
                const p = prPress[n];
                delete prPress[n];
                if (p && !p.done) { clearTimeout(p.timer); if (p.row === row && k < pr.len) set(st[k] ? false : true); }   /* (past a
                                                         * LENGTH made shorter meanwhile: nothing) */
                return true;
            }
            if (pr.keys || U.held(B.SEL)) {
                const m = U.midi(n);
                if (m >= 12 && m <= 127) prSetNote(m);
                pr.keys = false;
                return true;
            }
            if (k < 0 || k >= pr.len) return true;
            const p = prPress[n] = { row, done: false };
            p.timer = setTimeout(() => { p.done = true; if (pr.sel === row && k < pr.len) set(k > 0 && st[k - 1] ? "hold" : true); }, U.set("HOLD DELAY"));
            return true;
        },
        /* a step on full, off dark (steps 1, 5, 9, 13 dim), past LENGTH dark, the one playing turned over; setting the
         * note: its key */
        keyLeds(keys) {
            keys.fill(0);
            if (pr.keys || U.held(B.SEL)) { const k = prNote(pr.sel) - U.midi(0); if (k >= 0 && k < 27) keys[k] = 1; return true; }
            const pl = prStep(), st = pr.rows[pr.sel].steps;
            U.WHITE.forEach((n, k) => { keys[n] = k >= pr.len ? 0 : k === pl ? (st[k] ? 0 : 1) : st[k] === "hold" ? (U.blink(250) ? 1 : .25) : st[k] ? 1 : k % 4 === 0 ? .25 : 0; });
            return true;
        },
        draw() {
            const col = prCol(), pl = prStep();
            D.rect(4, 36, 232, 186, D.BLACK);
            D.box(4, 36, 232, 186, D.LGREY);
            D.text(10, 40, F.T, "RECORD PATTERN: TO STAGE", D.CYAN);
            D.text(188, 40, F.T, "PREVIEW", prPlaying() ? D.GREEN : D.DGREY);
            for (let k = 0; k < pr.len; k += 4) D.text(SX(k) + 3, 56, F.T, String(k / 4 + 1), D.DGREY);
            for (let r = 0; r < 4; r++) {
                const on = r === pr.sel, st = pr.rows[r].steps;
                D.text(8, 71 + 17 * r, F.T, M.noteName(prNote(r)), on ? D.WHITE : prFilled(r) ? col : D.DGREY);
                for (let k = 0; k < pr.len; k++) {
                    const y = 70 + 17 * r;
                    D.rect(SX(k), y, 11, 13, st[k] === "hold" ? (k === pl ? D.LGREY : D.DEEP + col) : st[k] ? (k === pl ? D.WHITE : col) : k === pl ? D.DEEP + D.BLUE : D.DEEP + D.DGREY);
                    if (st[k] === "hold" && k > 0) D.rect(SX(k - 1) + 6, y + 5, SX(k) - SX(k - 1) + 1, 3, col);   /* (a tie: a bar from the step before) */
                }
                if (on) D.box(6, 68 + 17 * r, 228, 17, D.WHITE);
            }
            [["K1 NOTE LENGTH", pr.noteLen], ["K2 LENGTH", pr.len + " STEPS"], ["K3 NOTE", M.noteName(prNote(pr.sel))],
             ["REPEATS", "OVER " + lengthText(LS().rows)]].forEach(([n, v], i) => {
                const dim = i === 3, y = 140 + 12 * i;            /* (REPEATS: no knob, told dim) */
                D.text(12, y, F.T, n, dim ? D.DGREY : D.CYAN);
                D.text(228 - D.tw(F.T, v), y, F.T, v, dim ? D.DGREY : i === 2 ? D.WHITE : D.LGREY);
            });
            D.text(12, 192, F.T, prMakes(), D.LGREY);
            D.text(12, 206, F.T, "KEY: A STEP  HELD: A TIE  SEL: NOTE", D.DGREY);
        },
    };

    /* ---------------------------------------------------------- the transport --- */
    /* PLAY: the loops play from the play position, with loops or none (the METRONOME over the length); armed, after
     * COUNT-IN; playing, they stop and the play position goes to the start (the stage kept, the keys down no longer
     * writing), PLAY's red then dim: at the start. A song armed with the loops not playing as it was: the loops and the
     * song after COUNT-IN; a song recording: the loops silent, or on again, on its next row (it goes on) */
    function screenPlay() {
        if (P.countingIn()) { P.stopCountIn(); lp.arm = null; songCancel(); U.toast("CANCELLED"); return; }
        if (songOn()) { const on = P.lpRestState().on; P.lpRest(on === 1 || on === 3 ? 0 : 1); return; }
        if (lp.song && lp.song.state === "armed" && !lp.song.splay && !playingHere()) { songCount(); return; }
        if (playingHere()) { overdubTick(); stopWriting(); P.pause(); P.kept.loops.row = 0; P.atStart = true; return; }
        if (lp.arm) { countThen(); return; }
        startPlay();
    }

    /* -------------------------------------------------------------- drawing --- */
    /* each loop's notes as spans on its lanes, in its base's rows (a note delay and a key off at their ticks): from a note
     * to its lane's next note (gap: a pixel before it), note off, key off or the base's end */
    const spanCache = new WeakMap();
    function spans(L) {
        const key = M.version + "/" + LS().ticks, was = spanCache.get(L);
        if (was && was.key === key) return was.out;
        const out = spansOf(L);
        spanCache.set(L, { key, out });
        return out;
    }
    function spansOf(L) {
        const sp = LS().ticks, out = [[], [], [], []];
        for (let l = 0; l < 4; l++) {
            let cur = null;
            const end = (at, gap) => { if (cur) { cur.to = at; cur.gap = gap; out[l].push(cur); cur = null; } };
            for (let r = 0; r < L.base; r++) {
                const c = L.cells[r * 4 + l];
                if (!c) continue;
                const note = isNote(c), ed = note ? cmdOf(c, 0x0E, 0xD) : null, d = ed == null ? 0 : ed & 15, k = cmdOf(c, 0x14);
                if (k != null && (!note || k < d)) end(r + Math.min(k, sp) / sp, false);
                if (c.note === M.OFF) end(r, false);
                if (note) { end(r + d / sp, true); cur = { from: r + d / sp }; }
            }
            end(L.base, false);
        }
        return out;
    }
    /* the cells a loop shows over the length, a lane a row: 0 empty, 1 release, 2 attack, 3 sustain (a note's attack
     * cell and the release cells after it dim, the sustain full; a note one cell long full). Its base
     * repeated as its count says, its SHIFT applied; the release a few cells by the engine (the firmware would take it
     * from the instrument's envelope), up to the lane's next note */
    const RELEASE = { DX7: 2, JUNO: 2, CHIP: 1, TB303: 1, SAMPLER: 1, FUSION: 1 };
    const gridCache = new WeakMap();
    function grid(L, fresh) {
        const len = LS().rows, rel = RELEASE[M.instType(L.inst)] || 1, sh = L.shift || 0;
        const key = M.version + "/" + len + "/" + sh + "/" + LS().ticks;
        if (!fresh) { const c = gridCache.get(L); if (c && c.key === key) return c.g; }
        const g = new Uint8Array(len * 4), sp = fresh ? spansOf(L) : spans(L);
        const put = (r, l, v) => { const k = ((((r + sh) % len) + len) % len) * 4 + l; if (g[k] < v) g[k] = v; };
        for (let rep = 0; rep < (L.repeats || 1); rep++) {
            const off = rep * L.base;
            if (off >= len) break;
            for (let l = 0; l < 4; l++) sp[l].forEach((s, x) => {
                const a = off + s.from, b = Math.min(off + s.to, len), r0 = Math.floor(a), r1 = Math.min(len, Math.max(r0 + 1, Math.ceil(b)));
                if (r0 >= len) return;
                for (let r = r0; r < r1; r++) put(r, l, r === r0 && r1 - r0 > 1 ? 2 : 3);
                const nx = x + 1 < sp[l].length ? off + sp[l][x + 1].from : len;
                for (let r = r1; r < Math.min(r1 + rel, Math.floor(nx), len); r++) put(r, l, 1);
            });
        }
        if (!fresh) gridCache.set(L, { key, g });
        return g;
    }
    /* the window: VIEW's bars (at most the length), the page the play position is on (the last page ends at the length's
     * end) */
    function win() {
        const len = LS().rows, v = Math.min(len, LS().view * barRows()), play = playRow();
        return { start: Math.max(0, Math.min(Math.floor(play / v) * v, len - v)), v };
    }
    const GX = 28, GW = 180, GR = GX + GW;               /* the strips (the loops', the stage's, the length's bar): x, width */
    const SLOT_Y = 33, SLOT_H = 32, STRIP_DY = 13, STRIP_H = 14, OV_X = 229;
    const cellX = (r, w) => GX + Math.floor((r - w.start) * GW / w.v);
    /* a lane's place in a strip of h px with n lanes */
    const laneBox = (h, n, k) => { const each = Math.max(1, Math.floor((h - (n - 1)) / n)); return [k * (each + 1), each]; };
    /* a strip: L's cells (g: its grid) in the window w at y, h px high, in colour col; the play line at play. Empty cells
     * deep grey, a beat's first deep light grey; the sustain in col, the attack and the release its deep shade. look.off:
     * the stage off, much fainter: nothing behind its notes, the sustain and the bar lines deep light grey, the attack and
     * the release deep grey, the play line dark grey */
    function strip(L, g, y, h, w, look) {
        const bR = barRows(), rpb = LS().rpb, n = U.clamp(L.lanes || 1, 1, 4), off = look.off, play = look.play;
        const sus = off ? D.DEEP + D.LGREY : look.col, edge = off ? D.DEEP + D.DGREY : D.DEEP + look.col;
        for (let r = w.start; r < w.start + w.v; r++) {
            const x0 = cellX(r, w), x1 = cellX(r + 1, w), cw = Math.max(1, x1 - x0 - (x1 - x0 >= 4 ? 1 : 0));
            const bg = off ? D.BLACK : r % rpb === 0 ? D.DEEP + D.LGREY : D.DEEP + D.DGREY;
            for (let k = 0; k < n; k++) {
                const [ly, lh] = laneBox(h, n, k);
                D.rect(x0, y + ly, cw, lh, bg);
                const s = g ? g[r * 4 + k] : 0;
                if (s) D.rect(x0, y + ly, cw, lh, s === 3 ? sus : edge);
            }
        }
        /* the bar lines, in the gap before each bar's first cell, a pixel over and under the strip */
        for (let r = Math.ceil(w.start / bR) * bR; r < w.start + w.v; r += bR) D.vline(cellX(r, w) - 1, y - 1, h + 2, off ? D.DEEP + D.LGREY : D.LGREY);
        if (play != null && play >= w.start && play < w.start + w.v) D.vline(cellX(play, w) - 1, y - 1, h + 2, off ? D.DGREY : D.WHITE);
    }
    /* the top line; ADJUST: the loop and its SHIFT; a song recording: `SONG`, the time recorded (M:SS, MM:SS from 10
     * minutes), its patterns so far (the one being written counted), the tempo */
    function top() {
        const j = LS(), at = playingHere() ? P.at() : null, bR = barRows(), y = Y0 + 2;   /* (the beats go on while the loops rest) */
        if (lp.adj) D.text(2, y, F.T, name(lp.adj.i) + ": SHIFT " + shiftText(lp.adj.L.shift || 0), D.WHITE);
        else if (songOn()) {
            const sec = Math.max(0, Math.floor(P.time() - lv.t0)), min = Math.floor(sec / 60), np = lv.done + 1;
            D.text(2, y, F.T, "SONG", D.RED);
            D.text(sec >= 600 ? 26 : 32, y, F.T, min + ":" + M.dec(sec % 60), D.LGREY);
            D.text(68, y, F.T, np === 1 ? "1 PATTERN" : M.dec(np, 1) + " PATTERNS", D.LGREY);
            D.text(146, y, F.T, M.bpm(j) + " BPM", D.LGREY);
        } else {
            D.text(2, y, F.T, M.bpm(j) + " BPM", D.LGREY);
            D.text(50, y, F.T, lengthText(j.rows), D.LGREY);   /* (a part bar in beats: `32 BEATS` at most, 8 characters) */
            D.text(98, y, F.T, "Q " + (j.quantize === "REAL TIME" ? "REAL" : j.quantize), D.CYAN);
            const r = lp.song ? ["SONG", D.RED] : lp.arm || P.countingIn() ? ["ARMED", D.RED] : ["REC", D.DGREY];
            D.text(146, y, F.T, r[0], r[1]);
        }
        const nb = beats(), w = Math.floor(44 / nb), on = at && at.pass != null ? Math.floor((at.row % bR) / j.rpb) : -1;
        for (let k = 0; k < nb; k++) D.rect(194 + k * w, Y0 + 4, w - 2, 8, k === on ? (k ? D.GREEN : D.YELLOW) : D.DEEP + D.GREEN);
        D.hline(0, Y0 + 14, 240, D.DGREY);
    }
    const keysShown = () => lp.control || U.held(B.SEQ);   /* (the white keys mute: CONTROL mode, or SEQ held) */
    /* a loop's slot k (y): its number in the gutter (CONTROL mode: its key), its label line (sound, P / M, repeats,
     * SHIFT, its state), its strip; selected while the stage holds notes: silent, grey, STAGED */
    function loopSlot(L, i, y, w, play) {
        const staged = silentNow() === i, dim = staged || silenced(L), wt = waiting(i);
        D.text(i >= 9 && keysShown() ? 2 : 5, y + 8, F.S, keysShown() ? String(i + 1).padStart(2) : M.hex(i),   /* (keys 10-16 at 2) */
               keysShown() ? D.WHITE : dim ? D.DGREY : D.CYAN);
        D.text(GX, y, F.T, U.instName(L.inst).slice(0, 9), staged ? D.RED : dim ? D.DGREY : colOf(L));
        D.text(GX + 60, y, F.T, L.mono ? "M" : "P", dim ? D.DGREY : D.LGREY);
        if (L.repeats > 1) D.text(GX + 72, y, F.T, L.repeats > 9 ? "X+" : "X" + L.repeats, dim ? D.DGREY : D.LGREY);
        if (shiftSigned(L.shift || 0)) D.text(GX + 90, y, F.T, "SHIFT " + (shiftSigned(L.shift) > 0 ? "+" : "") + shiftSigned(L.shift), dim ? D.DGREY : D.LGREY);
        const s = staged ? ["STAGED", D.RED] : wt ? [wt === "mute" ? "M" : "S", D.YELLOW] : L.soloed ? ["S", D.YELLOW] : L.muted ? ["M", D.DGREY] : null;
        if (s) D.text(226 - D.tw(F.T, s[0]), y, F.T, s[0], s[1]);
        levelText(LS().mixer[i].lvl, y, dim ? D.DGREY : D.LGREY);
        strip(L, grid(L), y + STRIP_DY, STRIP_H, w, { col: dim ? D.DGREY : colOf(L), play });
    }
    /* a slot's LEVEL (a loop's; + NEW LOOP's the next loop's) in hex, as K1 and the MIXER show it, at the strip's right */
    function levelText(v, y, col) { const t = M.hex(v); D.text(226 - D.tw(F.T, t), y + 14, F.T, t, col); }
    /* + NEW LOOP's slot: the current instrument at the right; a dashed strip saying where REC puts the stage */
    function newSlot(y) {
        const n = list().length;
        D.text(9, y + 8, F.S, "+", D.WHITE);
        D.text(GX, y, F.T, "NEW LOOP " + M.hex(n), D.WHITE);
        const it = M.refText(U.inst) + " " + U.instName(U.inst).slice(0, 10);
        D.text(226 - D.tw(F.T, it), y, F.T, it, M.instCol(U.inst));
        levelText(LS().nextMix.lvl, y, D.LGREY);
        const sy = y + STRIP_DY;
        for (let x = GX; x < GR - 1; x += 4) { D.px(x, sy, D.DGREY); D.px(x, sy + STRIP_H - 1, D.DGREY); }
        D.vline(GX, sy, STRIP_H, D.DGREY);
        D.vline(GR - 2, sy, STRIP_H, D.DGREY);
        D.text(GX + 6, sy + 1, F.T, stageHas() && !selLoop() && !st.off ? "REC: THE STAGE GOES HERE" : "REC: A NEW LOOP", D.DGREY);
    }
    /* the overview column: a cell a loop in its colour (grey muted or silenced, red staged, a yellow frame soloed, a
     * yellow dot while a mute or solo waits), a grey + for + NEW LOOP; the slots in view framed white */
    function overview() {
        const L = list(), n = L.length;
        for (let i = 0; i < 16; i++) {
            const y = SLOT_Y + i * 8;
            if (i < n) {
                const l = L[i];
                D.rect(OV_X + 2, y + 1, 6, 6, silentNow() === i ? D.RED : silenced(l) ? D.DGREY : colOf(l));
                if (l.soloed) D.box(OV_X + 1, y, 8, 8, D.YELLOW);
                if (waiting(i)) D.rect(OV_X + 4, y + 3, 2, 2, D.YELLOW);
            } else if (i === n) {                          /* (+ NEW LOOP: a plus filling the 6 x 6 cell, 2 px strokes, centred) */
                D.rect(OV_X + 2, y + 3, 6, 2, D.DGREY);
                D.rect(OV_X + 4, y + 1, 2, 6, D.DGREY);
            }
        }
        /* the selection's cell: a small circle, a white ring round black, its loop's colour a dot in the middle (grey
         * muted or silenced, red staged; on + NEW LOOP light grey) */
        {
            const x = OV_X + 2, y = SLOT_Y + lp.sel * 8 + 1, l = L[lp.sel];
            D.rect(x, y, 6, 6, D.BLACK);
            D.hline(x + 1, y, 4, D.WHITE); D.hline(x + 1, y + 5, 4, D.WHITE); D.vline(x, y + 1, 4, D.WHITE); D.vline(x + 5, y + 1, 4, D.WHITE);
            D.rect(x + 2, y + 2, 2, 2, !l ? D.LGREY : silentNow() === lp.sel ? D.RED : silenced(l) ? D.DGREY : colOf(l));
        }
        const shown = Math.min(4, slots() - lp.top);
        D.box(OV_X - 1, SLOT_Y + lp.top * 8 - 1, 11, shown * 8 + 2, D.WHITE);
    }
    /* the stage's title on its frame's top edge: STAGE and its mode (OVERDUB in the stage's colour, OVERWRITE red, then a
     * dim LFO where LFO switches them), OFF in its place while the stage is off; at the right where REC sends it (off: REC:
     * STAGE ON, as that is what REC does then). The stage in the selected loop's colour; on + NEW LOOP the colour the new loop
     * will get; off, all of it dark grey */
    function stageHead() {
        const scol = selLoop() ? colOf(selLoop()) : M.LOOP_COLS[nextCol()];
        const runs = st.off ? [["STAGE", D.DGREY], [" ", D.LGREY], ["OFF", D.DGREY]]
                   : [["STAGE", D.CYAN], [" ", D.LGREY], st.over ? ["OVERWRITE", D.RED] : ["OVERDUB", scol], [lp.adj ? "" : " LFO", D.DGREY]];
        const to = st.off ? [["REC", D.DGREY], [": STAGE ON", D.DGREY]] : [["REC", D.YELLOW], [": " + (selLoop() ? "INTO " : "NEW ") + name(tgt()), D.LGREY]];
        return { scol, runs, to };
    }
    /* THE STAGE, a panel of its own in a dark grey frame: its title (stageHead); one strip, a little taller than a
     * loop's, its notes as they play (MONO: one at a time). In the gutter a dot: red while keys overdub, deep red while
     * the loops play, grey stopped or off. While the stage is off all of it much fainter: the frame deep light grey, the
     * texts dark grey, the strip's own (strip). sh: the strip's height */
    function stage(y0, sh, w, play) {
        const ry = y0 + 14, yb = ry + sh + 2, on = playingHere();
        D.box(0, y0 + 6, 240, yb - y0 - 5, st.off ? D.DEEP + D.LGREY : D.DGREY);
        const { scol, runs, to } = stageHead(), tw = to.reduce((a, r) => a + D.tw(F.T, r[0]), 0);
        D.rect(3, y0, runs.reduce((a, r) => a + D.tw(F.T, r[0]), 0) + 6, 12, D.BLACK);   /* (the frame's edge broken for them) */
        D.rect(234 - tw - 3, y0, tw + 6, 12, D.BLACK);
        D.runs(6, y0, F.T, runs);
        D.runs(234 - tw, y0, F.T, to);
        D.disc(13, ry + (sh >> 1), 4, st.off ? D.DEEP + D.DGREY : on && liveKeys().length ? D.RED : on ? D.DEEP + D.RED : D.DEEP + D.DGREY);
        fitStage();
        const cells = st.cells ? stageNow(st.cells) : null, sl = cells ? stageLoop(cells) : { lanes: 1 };
        const g = cells ? grid(sl, true) : null;
        if (g) heldUpToLine(g, cells, play);
        strip(sl, g, ry, sh, w, { col: scol, off: st.off, play });
        if (!stageHas()) D.text(GR, ry + ((sh - 12) >> 1), F.T, "EMPTY", D.DGREY);   /* (at 208: its last pixel two clear of the frame) */
        return yb;
    }
    /* a held key's note drawn only up to the play row (cell by cell as the line moves): ahead of the line its lane shows
     * what the stage had, up to the next note there (replaced only as the line passes); a note landing ahead (quantized
     * forward, or the next pass's) not drawn until the line reaches it */
    function heldUpToLine(g, cells, play) {
        const h = here(), len = LS().rows;
        if (!h) return;
        for (const k of liveKeys()) {
            const r = k.rec, l = isMono() ? 0 : r.lane;
            if (r.pass < h.pass) continue;
            for (let x = r.pass > h.pass || r.row > play ? r.row : play + 1; x < len; x++) {
                if (x !== r.row && isNote(cells[x * 4 + l])) break;
                g[x * 4 + l] = 0;
            }
        }
    }
    /* the length's bar: a segment a bar with its number (white in view, dark grey out of it), the play position's tick */
    function lengthBar(y, w, play) {
        const len = LS().rows, bR = barRows(), bars = len / bR;
        for (let b = 0; b < bars; b++) {
            const x0 = GX + Math.floor(b * GW / bars), x1 = GX + Math.floor((b + 1) * GW / bars), r = b * bR, inView = r >= w.start && r < w.start + w.v;
            D.text(x0, y, F.T, String(b + 1), inView ? D.WHITE : D.DGREY);
            D.rect(x0 + 7, y + 5, Math.max(1, x1 - x0 - 9), 3, inView ? D.LGREY : D.DEEP + D.LGREY);
        }
        D.vline(GX + Math.floor(play * GW / len), y + 1, 11, D.WHITE);
    }
    /* the hint line, only where a state changes the keys, REC or HOME: ADJUST, a song recording (HOME ends it), a song
     * counting in or armed (HOME cancels it), armed, CONTROL mode or SEQ held, the stage off (not while a song is armed or
     * recorded: the keys write the song then); the first that holds; null: hidden */
    function hint() {
        const Y = D.YELLOW, G = D.LGREY;
        if (lp.adj) return [["K1", Y], [" ROW  ", G], ["K2", Y], [" BAR  ", G], ["SEL", Y], [" KEEP  ", G], ["HOME", Y], [" CANCEL", G]];
        if (songOn()) return [["RECORDING: ", D.RED], ["HOME", Y], [" ENDS AND KEEPS IT", G]];
        if (songArmed() && lp.song.state === "count") return [["COUNTING IN: ", D.RED], ["HOME", Y], [" CANCELS", G]];
        if (songArmed()) return [["SONG ARMED: ", D.RED], ["A NOTE STARTS  ", G], ["HOME", Y], [" CANCELS", G]];
        if (lp.arm && !playingHere()) return [["ARMED: ", D.RED], ["A NOTE OR ", G], ["PLAY", Y], [" STARTS AT " + barBeat(playRow()), G]];
        if (keysShown()) return [["WHITE KEYS", Y], [lp.solo ? ": SOLO  " : ": MUTE  ", G], ["HELD", Y], [": AT THE BAR", G]];
        if (st.off && !lp.song) return [["STAGE OFF", D.CYAN], [": THE KEYS ARE NOT KEPT", G]];
        return null;
    }
    function draw() {
        syncSel();
        checkSetup();
        overdubTick();                                     /* (the held keys' and OVERWRITE's spans grow with the line) */
        syncSilent();                                      /* (the stage emptied or filled by anything: the loop follows) */
        top();
        const L = list(), n = L.length, w = win(), play = playRow();
        for (let k = 0; k < 4; k++) {
            const i = lp.top + k, y = SLOT_Y + k * SLOT_H;
            if (i >= slots()) break;
            if (i < n) loopSlot(L[i], i, y, w, play); else newSlot(y);
            if (i === lp.sel) D.box(0, y - 1, 228, 29, D.WHITE);
        }
        overview();
        if (!n) {                                          /* no loops yet: how the first comes */
            (st.off ? [["PLAY, THEN PLAY NOTES:", G0], ["THEY ARE NOT KEPT", G0], ["REC: THE STAGE BACK ON", G0]]
                    : [["PLAY, THEN PLAY NOTES:", G0], ["THEY GO ON THE STAGE", G0], ["REC KEEPS THE STAGE AS A LOOP", G0]])
                .forEach(([s, c], k) => D.text(2, 70 + 12 * k, F.T, s, c));
            D.runs(2, 112, F.T, [["PRESETS", D.YELLOW], [", ", D.LGREY], ["ENV", D.YELLOW], [": THE SOUND", D.LGREY]]);
            D.runs(2, 124, F.T, [["GLO", D.YELLOW], [": LENGTH, TEMPO, VIEW", D.LGREY]]);
        }
        const hn = hint(), yb = stage(162, 18, w, play);  /* (the hint line's row kept free: nothing moves when it shows) */
        lengthBar(yb + 3, w, play);
        if (hn) D.runs(2, 214, F.T, hn);
    }
    const G0 = D.LGREY;
    /* the loops' pattern as they are heard (a loop a track, over the length: its repeats written out, its SHIFT applied;
     * each row its lowest lane's cell), read by EDIT, RECORD LIVE, SAVE TO PATTERN and QUANTIZE */
    const pbCache = { v: -1, p: null, j: null };
    U.loopsPattern = function () {
        const j = LS();
        if (pbCache.v === M.version && pbCache.j === j) return pbCache.p;
        const rows = Math.max(1, j.rows), p = M.newPattern(-1, Math.max(1, j.list.length), rows);
        j.list.forEach((L, t) => {
            const heard = asHeard(L, rows);
            for (let r = 0; r < rows; r++)
                for (let l = 0; l < 4; l++) {
                    const c = heard[r * 4 + l];
                    if (c && c.note != null) { p.cells[r * 16 + t] = Object.assign(c, { inst: isNote(c) ? L.inst : null }); break; }
                }
        });
        Object.assign(pbCache, { v: M.version, p, j });
        return p;
    };

    /* ---------------------------------------------------------------- screen --- */
    U.register({
        name: "LOOPS", group: "PLAY",
        does: [B.GLO, B.SEQ, B.ENV, B.SEL, B.ARP, B.REC, B.EDIT, B.LFO],
        ownArp: "pattern",                                 /* ARP: the stage's switch; held + REC, the pattern recorder (no
                                                            * arpeggiator here) */
        longHold: B.EDIT,                                  /* (EDIT's hold, the stage's clear: 2 s) */
        tapNow: [B.LFO],                                   /* (LFO's tap at its release: the switch lands as it is let go) */
        /* ARP forms a combination here (ARP held + REC): its tap waits the combination grace, on the screen itself */
        get combines() { return U.top() ? [] : [B.ARP]; },
        /* ARP held (or let go within the grace) + REC: the pattern recorder at REC's release, REC's press doing none of
         * its own jobs */
        recWith(m) {
            if (m !== B.ARP) return null;
            const h = U.holdOf(B.ARP);
            if (h && h.used && h.lock !== "rec") return null;   /* (ARP taken by something else already) */
            return () => prStart([B.REC, B.ARP]);
        },
        /* ARP held alone past the hold delay, on the screen itself: its layer; let go, nothing */
        arpLayer: () => U.top() ? null : ["REC: RECORD PATTERN"],
        seq: () => lp.control ? (lp.solo ? "SOLO" : "MUTE") : null,   /* the header's SEQ place in CONTROL mode */
        lit(b) {
            if (prOpen()) {                                /* the pattern recorder: ARP full, REC writes, SEL full waiting for a key */
                if (b === B.ARP) return 1;
                if (b === B.REC) return .25;
                if (b === B.SEL) return pr.keys ? 1 : .25;
                if (b === B.GLO || b === B.SEQ || b === B.EDIT || b === B.LFO || b === B.ENV) return 0;
                return undefined;
            }
            /* REC blinking while armed, full while the stage holds notes (REC sends them; not while it is off), else dim;
             * LFO full while OVERWRITE is on (a mode), else dim; EDIT dim on a loop (it copies it onto the stage), on + NEW LOOP
             * dim while the stage holds notes or is off (its tap clears it, on), else dark; SEQ full in CONTROL mode or held;
             * ARP full while the stage is on, dim off */
            if (b === B.REC) return lp.arm || songArmed() || P.countingIn() ? (U.blink(250) ? 1 : 0) : songOn() || (stageHas() && !st.off) ? 1 : .25;
            if (b === B.LFO) return st.over ? 1 : .25;
            if (b === B.EDIT) return selLoop() || stageHas() || st.off ? .25 : 0;
            if (b === B.SEQ) return keysShown() ? 1 : .25;
            if (b === B.ARP) return st.off ? .25 : 1;
            if (b === B.ENV) return .25;
        },
        sub: () => lp.adj ? "ADJUST" : null,               /* (the header's subpage while ADJUST is open) */
        /* ENV held: the pipette (a shortcut; its tap the cartridge list); EDIT held 2 s (longHold): the stage's clear; SEQ
         * held: the keys as in CONTROL mode (no layer); ARP held: its layer (arpLayer), let go nothing. LFO has no hold */
        hold(b, h) {
            if (b === B.ENV) { if (h && !U.top()) { h.used = true; takeItsInstrument(B.ENV); } return true; }
            if (b === B.EDIT && h) { if (!U.top()) { h.used = true; stageClear(B.EDIT); } return true; }
            if (b === B.SEQ) { if (h) h.used = true; return true; }
            if (b === B.HOME) lp.hrow = null;              /* (HOME held: its combination, no tap) */
            return false;
        },
        /* HOME's press, a song recording: the row a tap would end it in */
        holdStart(b) {
            if (b === B.HOME && songOn()) { liveDrain(true); lp.hrow = songNow().row; }
        },
        enter() { syncSel(); },
        /* HOME during a count-in cancels it (otherwise HOME's combination, as everywhere) */
        back() {
            if (P.countingIn()) { P.stopCountIn(); lp.arm = null; songCancel(); U.toast("CANCELLED"); return true; }
            if (songOn()) {                                /* (ended at the row HOME was pressed in) */
                const at = lp.hrow;
                lp.hrow = null;
                songEnd(!lv.known ? -1 : at != null ? at : playingHere() ? songNow().row : lp.sat);
                return true;
            }
            if (lp.song) { songCancel(); U.toast("CANCELLED"); return true; }
            return false;
        },
        /* leaving for any screen but FX: the keys down stop writing onto the stage (it stays), OVERWRITE's eraser rests (it
         * acts on LOOPS alone), armed or a song armed disarmed, a song's recording ends at once (what was heard written) */
        leave() {
            if (P.countingIn()) P.stopCountIn();
            lp.arm = null;
            overdubTick();
            stopWriting();                                 /* (the keys elsewhere never write onto the stage) */
            offKeysEnd();                                  /* (nor sound in its sound: the stage off) */
            st.erasers = [];
            const a = U.overlays.find(o => o.kind === "adjust");
            if (a) a.cancel();
            if (songOn()) songEndHere(); else songCancel();
            prClose();
            for (const n in lp.held) clearTimeout(lp.held[n].timer);
            lp.held = {};
        },
        reverted() { stopWriting(); syncSel(); lp.pend.clear(); st.silent = -1; fitStage(); P.mutesChanged(); },   /* (REVERT ALL: the
                                                         * stage kept, fitted to the length) */
        paused() { livePoll(); stopWriting(); },          /* (the loops paused, PLAY on FX, a stop: a song recording ends
                                                         * where it was heard last; the keys down stop writing) */
        /* FX from LOOPS (not leaving LOOPS): the keys down stop writing (on FX the keys are FX's), the eraser rests */
        toFx() { overdubTick(); stopWriting(); offKeysEnd(); st.erasers = []; },
        live(t) { liveChanges(t); livePunch(U.punch == null ? -1 : U.punch); },   /* (an edit, a punch-in: RECORD LIVE
                                                         * writes what changed, at its row) */
        toSave() { syncSel(); LS().sel = lp.sel; },     /* (SAVE: the selection with the record, no change of its own) */
        play: screenPlay,
        /* PLAY: at once; a song recording: as it is let go, unless held (held: the loops to their start, playing from the
         * top on its next row, silent waiting there; the song going on) */
        playDown() {
            if (!songOn()) return false;
            lp.pdefer = true;
            lp.phold = false;
            return true;
        },
        playHold() {
            if (U.top()) return;
            if (songOn() && lp.pdefer) { P.lpRest(2); lp.phold = true; return; }
            P.stopToStart();
        },
        playUp() {
            if (!lp.pdefer) return;
            lp.pdefer = false;
            if (!lp.phold && songOn()) screenPlay();
            lp.phold = false;
        },
        canPlay: () => true,                               /* (the loops play with none, over the length) */
        /* REC, at its press: a count-in cancelled; RECORD LIVE armed: it starts (the loops as they are); the stage off: it
         * turns on, nothing sent, and the loops stopped it arms (stageOnRec); else the stage to LOOPS (recPress). Each job
         * done, nothing for 500 ms (recQuiet); a refused REC waits for nothing. (ARP held + REC: recWith, before any of
         * this) */
        rec() {
            if (P.countingIn()) { if (recQuiet()) return; P.stopCountIn(); lp.arm = null; songCancel(); U.toast("CANCELLED"); recDone(); return; }
            if (lp.song && lp.song.state === "armed") { if (!recQuiet() && songStart()) recDone(); return; }
            if (recQuiet()) return;
            if (lp.adj) { U.refuse(B.REC); return; }       /* (ADJUST: REC does nothing) */
            if (st.off) { stageOnRec(); recDone(); return; }
            if (recPress()) recDone();
        },
        /* HOME's combination: white key n mutes loop n at once, held (or after SEQ) solos it */
        muteKey(wi) { toggle(wi, "mute", false); },
        soloKey(wi) { toggle(wi, "solo", false); },
        muteLeds: loopLeds,
        keyLeds(keys) {
            if (!keysShown()) return false;                /* PLAY mode: the keys light with their notes */
            loopLeds(keys);
            const j = LS(), qi = M.QUANTIZES.indexOf(j.quantize);
            U.BLACK.forEach((n, bi) => {
                const p = BLACK_PRINT[bi];
                keys[n] = bi <= 4 ? (bi === qi ? 1 : .25) : p === "PIT" ? .25 : p === "GLO" ? (lp.solo ? 1 : .25)
                        : p === "MONO" ? (j.nextMono ? 1 : .25) : p === "POLY" ? (j.nextMono ? .25 : 1) : 0;
            });
            return true;
        },
        selLines: () => ["K1-K4: BIG STEPS"],
        /* SELECT: the loops, then + NEW LOOP (clockwise down; the view scrolls past 4; the stage stays, the loop left heard
         * again); ALGORITHM: the play position; K1-K4: their jobs (GLO) */
        knob(e, s, coarse) {
            if (e === E.SELECT) {
                const to = U.clamp(lp.sel + s, 0, topSel());
                if (to !== lp.sel) { overdubTick(); stopWriting(); lp.sel = to; syncSel(); syncSilent(); }   /* (the stage holding notes: the loop
                                                         * moved to silent, the one left heard again) */
                return;
            }
            if (e === E.ALGORITHM) { algTurn(s, coarse); return; }
            if (e >= E.K1 && e <= E.K4) knobTurn(e, s, coarse);
        },
        tap(b) {
            if (b === B.SEQ) { lp.control = !lp.control; U.toast(lp.control ? "KEYS: CONTROL" : "KEYS: PLAY"); return true; }
            if (b === B.GLO) { props(); return true; }
            if (b === B.ENV) { carts(); return true; }
            if (b === B.SEL) { menu(); return true; }
            if (b === B.ARP) { stageSwitch(); return true; }
            if (b === B.EDIT) { editFill(); return true; }
            if (b === B.LFO) {                             /* (at its release: tapNow; ADJUST, the recorder, the stage off: nothing) */
                if (lp.adj || prOpen() || st.off) return true;
                st.over = !st.over;
                U.toast(st.over ? "OVERWRITE" : "OVERDUB");
                overdubTick();
                return true;
            }
            return false;
        },
        /* a key: PLAY mode plays (and the stage keeps it); CONTROL mode, or SEQ held, the loops' mutes, QUANTIZE, MONO /
         * POLY (SEQ's tap then taken: it stays in PLAY mode) */
        key(n, down) {
            livePoll();
            if (!down) {
                if (lp.free[n]) { delete lp.free[n]; U.keyOff(n); }
                else if (lp.held[n]) controlKey(n, false);
                else if (offHeld(n)) offKeyUp(n);
                else recKeyUp(n);
                return true;
            }
            if (U.held(B.SEQ)) U.consume(B.SEQ);
            if (keysShown()) controlKey(n, true);
            else playKey(n, true);
            return true;
        },
        footer() {
            if (prOpen()) return ["REC: TO STAGE", D.LGREY];
            if (songOn()) return ["MEMORY " + Math.floor(Math.max(0, M.EDITS_MAX - M.editsUsed) * 100 / M.EDITS_MAX) + "% FREE", D.RED];   /* (the edit memory left) */
            return [LS().nextMono ? "MONO" : "POLY", D.LGREY];
        },
        /* the footer's instrument: the preview's in the pattern recorder */
        footInst() { return prOpen() ? prInst() : null; },
        draw,
        selected: () => lp.sel,                            /* (for the checks) */
        state: () => lp,
        stage: () => st,                                   /* (the stage, for the checks) */
        liveState: () => lv,                               /* (RECORD LIVE's recording, for the checks) */
        liveLogMax: n => { LOG_N = n; },                   /* (the log's size, for the checks) */
        songRecording: () => songOn(),                     /* (the pattern lists' PLAY: no preview meanwhile) */
        /* the song's row heard while it records, -99 none */
        liveRow: () => songOn() && lv.known && playingHere() ? songNow().row : -99,
        recorder: () => pr,
        makes: () => prMakes(),
        lengthShown: () => lengthText(LS().rows),          /* (the top line's length, for the checks) */
        hintText: () => (hint() || []).map(r => r[0]).join(""),   /* (the hint line's words, for the checks) */
        stageTitle: () => { const h = stageHead(); return h.runs.map(r => r[0]).join("").trimEnd() + "|" + h.to.map(r => r[0]).join(""); },   /* (the
                                                         * stage's title and its right, for the checks) */
    });
})();

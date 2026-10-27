/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground: LOOPS, a loop station (specs/ui/screens/loops.md, specs/files/loops.md). Up to 16 loops over one
 * length the first sets (whole bars; then GLO's LENGTH, SEL's DOUBLE), each one instrument and one colour, MONO or POLY
 * (1-4 lanes), its notes once (its base) and a repeat count. REC records at the selection, one pass of the length: a new
 * loop on + NEW LOOP, a re-record on a loop (its notes gone at once); while the loops play it arms, the take starting at
 * their start; from silence a key starts both at once, PLAY after COUNT-IN. While recording the keys sound exactly as
 * written (the player's lanes, asked for at their rows): quantized, MONO legato, POLY 4 notes at most, through the loop's
 * mix. SEQ: the keys PLAY or CONTROL (mutes and solos, now or at the bar; QUANTIZE; MONO / POLY); EDIT re-records, LFO
 * peels; ARP the pattern recorder (4 rows of steps, heard in the loops' time, written as a loop); GLO the properties;
 * SEL the menu (RECORD LIVE: the performance over the loops written as a new song, 128 rows a pattern). A screen of the round
 * (HOME + POLY, or HOME + SELECT); the loops play on outside it, and a song plays on into it until the loops start
 * (specs/ui/navigation.md). */
(function () {
    "use strict";
    const D = PTFM.draw, F = D.F, M = PTFM.model, U = PTFM.ui, B = U.B, E = U.E, P = PTFM.player;
    const Y0 = U.BODY_Y;
    const GRIDS = [["1/16", 4], ["1/16T", 6], ["1/32", 8]];
    const LS = () => M.state.loops, list = () => LS().list;
    /* sel: the selection (SAVE writes it into the record; of: the record it came from); control / solo: the keys' mode,
     * CONTROL's MUTE or SOLO; pend: mutes and solos waiting for the bar line ("i/mute": {i, kind, due}); arm: a take
     * armed {loop (-1: a new one), re, first, wait (from silence)}; take: a take recording (beginTake); skip: the take's
     * loop and pass, whose stored notes the player leaves out; song: RECORD LIVE armed or recording (null: none; below),
     * with sw (the player's rest switches seen), pdefer / phold (PLAY pressed while it records: acted on as it is let go;
     * its hold came), sat (its row heard last), sk (its keys' notes); free: keys playing freely; held: CONTROL's white
     * keys down (a hold waits for the bar line) */
    const lp = { sel: 0, of: null, control: false, solo: false, pend: new Map(), arm: null, take: null, skip: null, song: null,
                 sw: 0, pdefer: false, phold: false, sat: 0, sk: null, free: {}, held: {} };

    /* --------------------------------------------------------------- time --- */
    const beats = () => ((LS().fx.knobs || {}).METRONOME || [])[1] || 4;    /* the METRONOME's BEATS A BAR */
    const barRows = () => LS().rpb * beats();
    const maxBars = () => Math.max(1, Math.floor(M.PAT_ROWS_MAX / barRows()));   /* (128 rows: 8 bars at 1/16) */
    const maxRows = () => maxBars() * barRows();
    const barsText = b => b + (b === 1 ? " BAR" : " BARS");
    const playingHere = () => P.playing && P.what === "loops";
    const takeLen = tk => tk.first ? maxRows() : LS().rows;
    /* the loops heard now (tm: at that time instead, a moment ago): their row, pass, the replay's row count (n), place in
     * rows (pos), where in the row (frac) and tick; null while they do not play (or before their first row is heard) */
    function here(tm) {
        if (!playingHere()) return null;
        const h = P.at(tm);
        if (!h || h.pass == null) return null;
        const frac = U.clamp(((tm == null ? P.time() : tm) - h.t) / h.dur, 0, .999);
        return { row: h.row, pass: h.pass, n: h.n, pos: h.row + frac, frac, tick: Math.min(LS().ticks - 1, Math.floor(frac * LS().ticks)) };
    }
    /* a take: heard (its pass reached), or still armed in the ear (the replay runs ahead) */
    const takeHeard = () => { const tk = lp.take; if (!tk) return false; const h = here(); return !h || h.pass >= tk.pass; };
    const recording = () => !!lp.take && takeHeard();
    const armShown = () => !!lp.arm || (!!lp.take && !takeHeard());
    const firstRec = () => !!lp.take && lp.take.first;
    /* the first loop's bars so far while it records, and the rows that makes */
    const recBars = () => { const h = here(), tk = lp.take; return Math.min(maxBars(), h && tk && h.pass === tk.pass ? Math.floor(h.row / barRows()) + 1 : 1); };
    const lenNow = () => firstRec() ? recBars() * barRows() : LS().rows;
    /* the row playing (the strips' play line; silent for a song: their place), else where the loops paused */
    function playRow() {
        const h = here(), n = LS().rows, rs = P.lpRestState();
        if (h && (rs.on === 1 || rs.on === 2)) return n ? rs.kr % n : 0;
        return h ? (n && !firstRec() ? h.row % n : h.row) : P.kept.loops.row || 0;
    }

    /* the player's side (player.js, LOOPS' lanes): the rows its replay goes round (the first loop recording: the most;
     * the preview alone: its own), the take's loop and pass not played from the stream, the take heard (the header's ●) */
    P.lpRows = r => r && r.alone ? pvRows() : firstRec() || (lp.arm && lp.arm.first) ? maxRows() : LS().rows;
    P.lpTake = () => lp.skip;
    P.lpRecording = () => recording() || songOn();

    /* ------------------------------------------------------------- the loops --- */
    const selLoop = () => list()[lp.sel] || null;              /* null: the placeholder, + NEW LOOP */
    const topSel = () => Math.min(list().length, M.LOOPS_MAX - 1);
    function syncSel() {
        if (lp.of !== LS()) { lp.of = LS(); lp.sel = LS().sel || 0; lp.pend.clear(); }   /* (power-on, REVERT: as saved) */
        lp.sel = U.clamp(lp.sel, 0, topSel());
    }
    /* the mixers past the last loop: the next loop's mix (a new loop's take and the preview sound through it) */
    function syncMix(j) { for (let k = j.list.length; k < 16; k++) j.mixer[k] = Object.assign({}, j.nextMix); }
    const nextCol = () => { const L = list(); return L.length ? (L[L.length - 1].col + 1) % 12 : 0; };
    const colOf = L => M.LOOP_COLS[L.col % 12];
    const anySolo = () => list().some(l => l.soloed);
    const silenced = L => anySolo() ? !L.soloed : L.muted;
    const name = i => "LOOP " + M.hex(i);
    const isNote = c => !!c && c.note != null && c.note !== M.OFF;
    const cmdOf = (c, code, sub) => { const x = c.cmds.find(y => y.c === code && (sub == null || y.v >> 4 === sub)); return x ? x.v : null; };
    /* a change of the loops refused: while a song is armed (RECORDING A SONG), or a take is armed or records */
    function refused(btn) {
        if (lp.song) { U.refuse(btn); U.toast("RECORDING A SONG"); return true; }
        if (lp.take || lp.arm || P.countingIn()) { U.refuse(btn); return true; }
        return false;
    }

    /* ------------------------------------------------------------- takes --- */
    /* REC (or EDIT) at loop i (-1: a new one): a re-record's notes go at once (armed included); playing, it arms for the
     * loops' next start, else it waits for a key or PLAY */
    function recordAt(i, btn) {
        if (lp.song) { U.refuse(btn); U.toast("RECORDING A SONG"); return; }
        const j = LS();
        if (i >= 0) {
            const L = j.list[i];
            if (!U.edit(() => { Object.assign(L, { cells: new Array(j.rows * 4).fill(null), base: j.rows, repeats: 1, lanes: 1 }); })) return;
            P.lpEnd(i);
            U.toast(name(i) + " RE-RECORDING");
        }
        lp.arm = { loop: i, re: i >= 0, first: !j.list.length, wait: !playingHere() };
    }
    /* the take begins at pass `pass` of the loops: a new loop made now (the current instrument, the next colour, the next
     * loops' mode and mix), selected while it records */
    function beginTake(pass) {
        const a = lp.arm, j = LS();
        if (!a) return;
        lp.arm = null;
        let i = a.loop;
        if (i < 0) {
            const n = j.list.length, base = a.first ? maxRows() : j.rows;
            const L = { inst: U.inst, col: nextCol(), mono: j.nextMono, lanes: 1, base, repeats: 1, muted: false, soloed: false,
                        cells: new Array(base * 4).fill(null) };
            if (!U.edit(() => { j.list.push(L); syncMix(j); })) return;
            i = n;
            lp.sel = n;
            P.mutesChanged();
        }
        lp.take = { loop: i, pass, first: a.first, re: a.re, mono: j.list[i].mono, notes: {}, busy: [[], [], [], []], cur: null, tail: [] };
        lp.skip = { loop: i, pass, run: P.lpRun() };      /* (the pass of this run of the loops) */
    }
    /* from silence: the loops from their start, the take with them (t0: when) */
    function startTake(t0) { P.playLoops(0, t0); beginTake(0); }
    function countThen() {
        if (P.playing) P.pause();                          /* (what played on into LOOPS pauses as the count starts) */
        P.countIn(startTake, M.bpm(LS()), Math.max(0, M.LOOP_COUNT_INS.indexOf(LS().countIn)));
    }
    /* where a note lands by QUANTIZE (behaviour 1): the nearest line of Q rows (halves up), waiting for it when it is
     * ahead; REAL TIME the row it falls in, its tick a note delay, at once */
    function landing(pos, tick) {
        const q = LS().quantize;
        if (q === "REAL TIME") return { row: Math.floor(pos), delay: Math.min(15, tick), wait: false };
        const Q = { "2 ROWS": 2, "3 ROWS": 3, "4 ROWS": 4 }[q] || 1, L = Math.floor(pos / Q + .5) * Q;
        return { row: L, delay: 0, wait: L > pos };
    }
    /* a POLY take's first lane free at row r: a lane is taken from its note to its note off (a held key's to the end) */
    const freeLane = (tk, r) => [0, 1, 2, 3].find(l => !tk.busy[l].some(n => n.row <= r && (n.off == null || r < n.off)));
    /* a cell the take wrote (asked to sound first): counted again; a full edit memory, or a loop's stream past its 3,800 B,
     * ends the take there (specs/files/loops.md#a-loops-stream) */
    function counted(L) {
        M.version++;
        M.settle();
        if ((M.editsUsed > M.EDITS_MAX || M.streamBytes(L) > 3800) && lp.take) { stopTake(); U.toast("MEMORY FOR EDITS IS FULL"); }
    }
    /* a key down while a take records (or is armed with the loops playing: a key landing on their start counts):
     * false when it does not land in the take (it plays freely then) */
    function takeKeyDown(n, m) {
        const j = LS(), h = here();
        let tk = lp.take;
        const len = tk ? takeLen(tk) : lp.arm.first ? maxRows() : j.rows;
        const pass = tk ? tk.pass : (h ? h.pass + 1 : 0), hp = h ? h.pass : pass;
        let pos = h ? h.pos : 0;
        if (hp === pass - 1) pos -= len;                   /* the pass before the take's: its start is ahead */
        else if (hp !== pass) return false;
        const at = landing(pos, h ? h.tick : 0);
        let row = at.row, wrapped = false;
        if (row < 0) return false;                         /* (before the take: played freely) */
        if (row >= len) { row = 0; wrapped = true; }       /* the length's end: on row 0, for the next pass */
        if (!tk) { beginTake(pass); tk = lp.take; if (!tk) return true; }
        const L = j.list[tk.loop];
        if (!L) return true;
        const cell = M.newCell(m, null, U.set("KEYS VOLUME"));
        if (at.delay) cell.cmds.push({ c: 0x0E, v: 0xD0 | at.delay });
        let lane = 0;
        if (tk.mono) {                                     /* MONO (behaviour 4): one lane; legato as MONO LEGATO says */
            const cur = tk.cur;
            if (cur && cur.held && (cur.row !== row || !cur.wrapped !== !wrapped) && j.legato !== "JUMP") {
                if (M.instType(L.inst) === "TB303") {      /* a TB-303 slides: SLD on the note held */
                    const c = L.cells[cur.row * 4];
                    if (isNote(c) && !c.cmds.some(x => x.c === 0xC1 || x.c === 0xC2)) {
                        const acc = c.cmds.findIndex(x => x.c === 0xC0);
                        if (acc >= 0) c.cmds[acc] = { c: 0xC2, v: 0 }; else if (c.cmds.length < 4) c.cmds.push({ c: 0xC1, v: 0 });
                    }
                    P.lpSlide(tk.loop, 0);
                } else if (j.legato === "LEGATO") cell.cmds.push({ c: 0xD3, v: 0 });   /* elsewhere LEGATO: LEG (on FUSION a
                                                                                        * plain note) */
                else cell.cmds.push({ c: 0x03, v: 0x20 });  /* or a glide, 2 semitones a tick */
            }
        } else {
            lane = freeLane(tk, row);
            if (lane == null) return true;                 /* a fifth note: neither heard nor written */
        }
        const rec = { lane, row, delay: at.delay, held: true, wrapped, off: null };
        tk.notes[n] = rec;
        tk.busy[lane].push(rec);
        if (tk.mono) tk.cur = rec;                         /* (keys on the same row: the last registered rewrites the cell) */
        L.cells[row * 4 + lane] = cell;
        /* heard as written: at its row (waiting for it, the take's start included), or at once; one for the next pass
         * only where the replay has scheduled that row already (else the loop's stream plays it there) */
        if (wrapped) { if (P.lpScheduled(pass + 1, 0)) P.lpNote(tk.loop, lane, cell, pass + 1, 0, n); }
        else P.lpNote(tk.loop, lane, cell, pass, at.wait ? row : -1, n);
        counted(L);
        return true;
    }
    /* a key let go in a take (behaviour 2): its note off on the nearest row, at least a row after the note (REAL TIME a
     * key off at its tick); past the length nothing is written, the note ending at the take's end. A MONO key ends its
     * note only while it is the lane's */
    function takeKeyUp(n) {
        const tk = lp.take, rec = tk && tk.notes[n];
        if (!rec) return;
        delete tk.notes[n];
        rec.held = false;
        if (tk.mono && tk.cur !== rec) return;
        if (tk.mono) tk.cur = null;
        const j = LS(), L = j.list[tk.loop], len = takeLen(tk), h = here();
        let pos = h ? h.pos : 0, pass = tk.pass;
        if (h && h.pass === tk.pass - 1) pos -= len;
        else if (h && h.pass !== tk.pass) return;
        if (rec.wrapped) { pos -= len; pass++; }           /* (its note is the next pass's row 0) */
        let row, cell;
        if (j.quantize === "REAL TIME") {
            const tick = h ? h.tick : 0;
            row = Math.floor(pos);
            if (row < rec.row || (row === rec.row && tick <= rec.delay)) row = rec.row + 1;
            cell = M.newCell(null, null, null);
            cell.cmds.push({ c: 0x14, v: tick });
        } else {
            row = Math.max(Math.floor(pos + .5), rec.row + 1);
            cell = M.newCell(M.OFF, null, null);
        }
        rec.off = row;
        if (row >= len || !L) { tk.tail.push(rec); return; }
        const there = L.cells[row * 4 + rec.lane];
        if (cell.note == null && row === rec.row && there && !there.cmds.some(x => x.c === 0x14)) {   /* (let go in its note's row, after its
                                                                                                * delay: that cell's key off, heard now) */
            if (there.cmds.length < 4) there.cmds.push(cell.cmds[0]);
            P.lpNote(tk.loop, rec.lane, cell, pass, -1, n);
            counted(L);
            return;
        }
        if (isNote(there)) return;                         /* (a note there ends it) */
        if (there && cell.note == null) { if (there.cmds.length < 4) there.cmds.push(cell.cmds[0]); cell = there; }
        L.cells[row * 4 + rec.lane] = cell;
        if (rec.wrapped && pass > tk.pass) { if (P.lpScheduled(pass, row)) P.lpNote(tk.loop, rec.lane, cell, pass, row, n); }
        else P.lpNote(tk.loop, rec.lane, cell, pass, row > pos ? row : -1, n);
        counted(L);
    }
    /* the take ends (its pass over, REC, a full edit memory, leaving): notes still sounding from it end now (the length's
     * end); a new loop with nothing played goes (NOTHING PLAYED: NO LOOP). True: a loop stays */
    function endTake() {
        const tk = lp.take;
        if (!tk) return true;
        lp.take = null;
        const j = LS(), L = j.list[tk.loop], off = M.newCell(M.OFF, null, null);
        for (const r of Object.values(tk.notes).concat(tk.tail)) if (!r.wrapped && (!tk.mono || tk.cur === r || tk.tail.includes(r))) P.lpNote(tk.loop, r.lane, off, 0, -1);
        if (!L) { M.settle(); return false; }
        let lanes = 1;
        L.cells.forEach((c, k) => { if (c) lanes = Math.max(lanes, (k & 3) + 1); });
        L.lanes = tk.mono ? 1 : lanes;
        if (!L.cells.some(isNote) && !tk.re) {
            j.list.splice(tk.loop, 1);
            syncMix(j);
            P.lpPeeled(tk.loop);
            if (!j.list.length) { j.rows = 0; if (playingHere()) P.pause(); P.kept.loops.row = 0; }
            lp.sel = U.clamp(lp.sel, 0, topSel());
            P.mutesChanged();
            M.settle(); M.version++;
            U.toast("NOTHING PLAYED: NO LOOP");
            return false;
        }
        if (!tk.re && lp.sel === tk.loop) lp.sel = Math.min(tk.loop + 1, topSel());   /* on + NEW LOOP again */
        M.settle(); M.version++;
        PTFM.audio.changed();
        return true;
    }
    /* the first loop closes on a bar line (behaviour 5): before a line it runs on to it, up to a beat after one it is cut
     * back to it (what was played after it goes); at the most rows by itself. The bars are the length */
    function closeFirst(atMost) {
        const tk = lp.take;
        if (!tk || !tk.first) return;
        const j = LS(), L = j.list[tk.loop], bR = barRows(), h = here();
        let bars = maxBars();
        if (!atMost) {
            if (h && h.pass === tk.pass) {
                const b = Math.floor(h.pos / bR), into = h.pos - b * bR;
                bars = Math.min(maxBars(), into < j.rpb && b >= 1 ? b : b + 1);
            } else bars = 1;
        }
        const n = bars * bR;
        if (L) { L.cells.length = n * 4; L.base = n; }
        j.rows = n;
        P.lpLength(n);
        if (endTake()) U.toast("LOOP 00: " + barsText(bars));
    }
    const stopTake = () => { if (lp.take && lp.take.first) closeFirst(false); else endTake(); };
    /* REC while a take is armed in the ear (begun in the replay, not heard yet): disarmed, its new loop gone */
    function cancelTake() {
        const tk = lp.take, j = LS();
        lp.take = null;
        lp.skip = null;
        if (tk && !tk.re && j.list[tk.loop]) { j.list.splice(tk.loop, 1); syncMix(j); P.lpPeeled(tk.loop); P.mutesChanged(); M.settle(); M.version++; }
        lp.sel = U.clamp(lp.sel, 0, topSel());
    }

    /* ------------------------------------------------------------ RECORD LIVE --- */
    /* RECORD LIVE (specs/ui/screens/loops.md#record-live): a performance over the loops as they are, written as a new
     * song at the first free number. Its panel (SEL's menu: the name, LOOPS' tempo) arms it; it starts with REC, a note,
     * or PLAY (the loops not playing as it was armed: after COUNT-IN); REC ends it at once, leaving LOOPS or the loops'
     * replay pausing anywhere too. Its time is the loops' replay's rows (their count from its row 0): while it records
     * the replay never stops, PLAY silencing the loops or playing them on again on the next row, held taking them to
     * their start (P.lpRest; each switch's row logged). A pattern every 128 rows of its time from its start, a position
     * each, none shared, written once its rows are heard (livePoll, on every screen: FX keeps it). A change waits, with
     * the song row it was heard at, until its pattern is written: the loops' notes as their lanes played them at the
     * loops' rows heard (repeats written out, silent rows empty, a loop left out from the row its mute took effect, a note
     * off for a note sounding where they fall silent), the keys on tracks of their own (landed as a take's: QUANTIZE,
     * MONO / POLY as the next loops record; heard as written on the player's lanes 17), the changes as commands at their
     * rows. The tracks are planned at the start and kept (songPlan); what fits none is heard, left out and counted.
     * lp.song: null, or {state: "armed" (its panel's REC; splay: the loops played then, so PLAY does not start it),
     * "count" (PLAY started it, counting in), "on" (recording)}; lv: the recording's own state, never stored */
    const LIVE_ROWS = 128, LOG_N = 255, SILENT = -1;   /* a pattern's rows; the changes waiting at most (a full log drops
                                                         * one, counted as left out: the song goes on) */
    const LIVE_OK = 0, LIVE_FULL = 1, LIVE_PATTERNS = 2, SF_DONE = 0, SF_256 = 3;   /* (a pattern written; not: the edit
                                                         * memory full, no pattern number free; how it ended) */
    const MIXC = [["lvl", 0x80], ["pan", 0x08], ["dly", 0x82], ["rev", 0x83], ["cho", 0x81], ["dck", 0x84]];
    const MIX_DEF = { lvl: 127, pan: 128, dck: 0, cho: 0, dly: 0, rev: 0 };
    const songOn = () => !!lp.song && lp.song.state === "on";
    const songArmed = () => !!lp.song && lp.song.state !== "on";
    let lv = null, slName = "";                        /* (slName: the panel's NAME) */
    /* what lane l of loop L does at row r of a pass, as player.js's lanes play it: `ends`, its note ends there (a repeat
     * cut at the length's end, at row 0; the rest's start), and its cell (within its repeats) */
    function laneAt(L, l, r, rows) {
        const k = Math.floor(r / L.base), br = r % L.base;
        if (k >= L.repeats) return { ends: k === L.repeats && !br, cell: null };
        return { ends: r === 0 && rows % L.base > 0 && Math.floor((rows - 1) / L.base) < L.repeats, cell: L.cells[br * 4 + l] || null };
    }
    /* a cell ends its lane's note: a note off, a key off; a note's own key off past its delay (REAL TIME's key let go in
     * its row) ends that note in its row */
    const ends = c => {
        const k = cmdOf(c, 0x14);
        if (!isNote(c)) return c.note === M.OFF || k != null;
        const ed = cmdOf(c, 0x0E, 0xD);
        return k != null && k > (ed == null ? 0 : ed & 15);
    };
    /* the rows a lane takes on its track in a pass: from its first cell to its last; a lane still sounding at the pass's
     * end (it rings on into the next) takes its track whole; null with no notes */
    function laneSpan(L, l, rows) {
        let first = -1, last = -1, snd = false, notes = 0;
        for (let r = 0; r < rows; r++) {
            const a = laneAt(L, l, r, rows), c = a.cell;
            if (!c && !(a.ends && snd)) continue;
            if (first < 0) first = r;
            last = r;
            if (c && isNote(c)) notes++;
            snd = c ? (isNote(c) && !ends(c)) || (snd && !ends(c)) : false;
        }
        return !notes ? null : snd ? { from: 0, to: rows } : { from: first, to: last + 1 };
    }
    /* the song's tracks, kept from its start: each loop's lanes in order on the first track free over their rows (a
     * loop's mix the track's mix at the start where its rows come first), then up to 4 tracks of their own for the keys'
     * lanes (their mix the KEYS column's, at LEVEL 7F: a note recorded plays without KEYS GAIN); -1: left out */
    function songPlan() {
        const j = LS(), rows = j.rows, taken = Array.from({ length: 16 }, () => []), first = [], loops = [], keys = [];
        j.list.forEach((L, i) => {
            loops[i] = [-1, -1, -1, -1];
            for (let l = 0; l < 4; l++) {
                const sp = laneSpan(L, l, rows);
                if (!sp) continue;
                let t = 0;
                while (t < 16 && taken[t].some(([a, b]) => sp.from < b && a < sp.to)) t++;
                if (t >= 16) continue;
                taken[t].push([sp.from, sp.to]);
                loops[i][l] = t;
                if (!first[t] || sp.from < first[t].from) first[t] = { from: sp.from, i };
            }
        });
        for (let t = 0; t < 16 && keys.length < 4; t++) if (!taken[t].length) { keys.push(t); taken[t].push([0, rows]); }
        while (keys.length < 4) keys.push(-1);
        const mixer = M.newMixer(), owner = new Array(16).fill(-1), km = U.set("KEYS MIX");
        first.forEach((f, t) => { if (f) { owner[t] = f.i; mixer[t] = Object.assign({}, j.mixer[f.i]); } });
        for (const t of keys) if (t >= 0) mixer[t] = { lvl: 127, pan: km.pan, dck: km.dck, cho: km.cho, dly: km.dly, rev: km.rev };
        return { loops, keys, mixer, owner };
    }
    /* DUCK's and FILTER's envelope's SOURCE name a loop in LOOPS (its track 16 + i): in the song, its first lane's track */
    function srcOf(n, v) {
        const tr = i => { const t = (lv.plan.loops[i] || []).find(x => x >= 0); return t == null ? i : t; };
        if (n === "DUCK") return v < 16 ? tr(v) : v;
        return v >= 1 && v <= 16 ? 1 + tr(v - 1) : v;
    }
    const isSrc = (n, i) => (n === "DUCK" && i === 0) || (n === "FILTER" && i === 3);
    /* an FX knob's value as its command's: its step from the bottom of its range (cmds.js fxKnob's other way) */
    const fxStep = (n, i, v) => { const k = M.FX[n].knobs[i]; return k.list ? v : Math.round((v - k.min) / (k.step || 1)); };
    /* the first song number free, -1 none (the songs' numbers are their places) */
    const songNumber = () => M.state.songs.length <= M.SONG_LAST ? M.state.songs.length : -1;
    /* its keys' notes: each key's lane, MONO and note delay (info), its song row (abs); each lane's last note and its end
     * (busy: song rows; null none, or held); MONO: the key whose note the lane holds (cur) and its instrument (kinst) */
    function songKeysClear() { lp.sk = { info: {}, abs: {}, busy: [[null, null], [null, null], [null, null], [null, null]], cur: null, kinst: null }; }
    songKeysClear();
    /* it begins at its row 0, the loops' replay's row count rows0 (0: not known yet, its first row's), the loops heard at
     * their row there (SILENT: silent): the loops' setup at the start is the song's, its tracks planned; a punch-in
     * running then is not written. True: begun */
    function songBegin(rows0, loopsRow) {
        const n = songNumber(), j = LS();
        if (n < 0) { lp.song = null; U.toast("NO SONG NUMBER FREE"); return false; }
        const len = j.rows || 1, plan = songPlan(), fx = structuredClone(j.fx), four = () => [false, false, false, false];
        lv = { song: n, name: slName, t0: P.time(), rows0, len, lr0: loopsRow < 0 || loopsRow >= len ? SILENT : loopsRow, nl: j.list.length,
               kvol: U.set("KEYS VOLUME"), plan, have: plan.mixer.map(x => Object.assign({}, x)), owner: plan.owner.slice(),
               setup: { tempo: j.tempo, ticks: j.ticks, rpb: j.rpb, swing: j.swing, fx }, sil: j.list.map(silenced), snd: j.list.map(four),
               ksnd: four(), pk: U.punch == null ? -1 : U.punch, pabs: 0, pw: null, done: 0, left: 0, made: false, ev: [],
               last: { tempo: j.tempo, on: Object.assign({}, j.fx.on), knobs: structuredClone(j.fx.knobs),
                       mix: j.list.map((_, i) => Object.assign({}, j.mixer[i])), sil: j.list.map(silenced) } };
        fx.knobs.DUCK[0] = srcOf("DUCK", fx.knobs.DUCK[0]);
        fx.knobs.FILTER[3] = srcOf("FILTER", fx.knobs.FILTER[3]);
        lp.song = { state: "on" };
        lp.sw = P.lpRestState().sw;                        /* (the player's switches before it: not its) */
        lp.pdefer = lp.phold = false;
        lp.sat = 0;
        songKeysClear();
        return true;
    }
    /* its start with the loops not playing (REC, a key): their replay from row 0, silent from its first row, their place
     * kept for PLAY to take them on from */
    function songBeginSilent() {
        const k = P.kept.loops.row || 0;
        P.playLoopsSilent(k < LS().rows ? k : 0);
        return songBegin(0, SILENT);                       /* (its row 0's count: the first row's, livePoll) */
    }
    /* armed, REC or a key: the loops playing, it begins at the row playing (they play on as they were); not playing,
     * their replay starts silent for it */
    function songStart() {
        if (!playingHere()) return songBeginSilent();
        const h = P.at();
        return songBegin(h ? h.n : 0, P.lpRestState().on ? SILENT : h ? h.row : 0);
    }
    /* armed, PLAY (the loops not playing as it was armed): the loops from their start, the song with them, after
     * COUNT-IN */
    function songCount() {
        const ci = Math.max(0, M.LOOP_COUNT_INS.indexOf(LS().countIn));
        if (P.playing) P.pause();                          /* (what played on into LOOPS pauses as the count starts) */
        if (!ci) { P.playLoops(0); songBegin(0, 0); return; }
        lp.song.state = "count";
        P.countIn(t0 => { P.playLoops(0, t0); if (lp.song && lp.song.state === "count") songBegin(1, 0); }, M.bpm(LS()), ci);
    }
    function songCancel() {                                /* armed or counting in: given up */
        if (lp.song && lp.song.state === "count") P.stopCountIn();
        lp.song = null;
    }
    /* the song's row heard now (the rows played since its row 0), its tick and place in rows: the time a key lands in
     * (row 0 until its row 0 is known) */
    function songNow() {
        const h = lv && lv.rows0 && playingHere() ? here() : null, s = h ? h.n - lv.rows0 : 0;
        return h && s >= 0 ? { row: s, tick: h.tick, pos: s + h.frac } : { row: Math.max(0, s), tick: 0, pos: Math.max(0, s) };
    }
    /* the song's row heard now (tm: when the change was made), to the nearest row (a change made then is written there),
     * never in a pattern written; null: not heard (the loops' replay not playing) */
    function heardRow(tm) {
        const h = lv && lv.rows0 && playingHere() ? here(tm) : null;
        return h ? Math.max(h.n - lv.rows0 + (2 * h.tick >= LS().ticks ? 1 : 0), lv.done * LIVE_ROWS) : null;
    }
    /* The changes waiting for their pattern (lv.ev), in the order they came: each its row in the pattern being written,
     * or (nx) the one after it, and what: "mix" a loop's mix value (a its new value, b the one before), "sil" the loops
     * not heard from there, "cmd" a global command, "punch" a punch-in's command still running (its value set to the
     * rows it ran as it ends), "loops" the loops heard from there at their row (SILENT: silent), "key" a keys' cell (its
     * lane, note: a note, M.OFF or none, its note delay, its 320 glide, its SLD, its instrument, its key off's tick) */
    const atOf = at => ({ nx: Math.floor(at / LIVE_ROWS) > lv.done, r: at % LIVE_ROWS });
    const sameAt = (x, a) => x.nx === a.nx && x.r === a.r;
    function evNew(a) {
        if (lv.ev.length >= LOG_N) { lv.left++; return null; }
        const e = { nx: a.nx, r: a.r };
        lv.ev.push(e);
        return e;
    }
    /* a command's key: the last at a row wins (an effect's ON and OFF one; any punch-in one) */
    const cmdKey = (code, v) => code === 0xAE || code === 0xAF ? "fx" + v : code >= 0xC3 && code <= 0xD2 ? "punch" : code;
    function logCmd(at, k, code, v) {
        const a = atOf(at), e = lv.ev.find(x => sameAt(x, a) && (x.k === "cmd" || x.k === "punch") && cmdKey(x.code, x.v) === cmdKey(code, v)) || evNew(a);
        if (e) Object.assign(e, { k, code, v });
    }
    function logMix(at, i, key, was, now) {
        const a = atOf(at), e = lv.ev.find(x => sameAt(x, a) && x.k === "mix" && x.i === i && x.key === key);
        if (e) { e.a = now; return; }                      /* (its first's before kept) */
        const n = evNew(a);
        if (n) Object.assign(n, { k: "mix", i, key, a: now, b: was });
    }
    function logOne(at, k, o) {                            /* a row's silent loops or the loops' row: the last at a row wins */
        const a = atOf(at), e = lv.ev.find(x => sameAt(x, a) && x.k === k) || evNew(a);
        if (e) Object.assign(e, { k }, o);
    }
    /* from the song's row `at` the loops are heard at their row `row` (SILENT: silent from there): a switch of the
     * player's rest, at the song row its row count gives */
    function liveLoops(at, row) {
        logOne(Math.max(at, lv.done * LIVE_ROWS), "loops", { row: row < 0 || row >= lv.len ? SILENT : row });
    }
    /* what changed live since the last look, logged at the row heard (not heard: dropped): TEMPO as F00 back to the song's
     * own, else Fxx held within 20-FF; an effect switched as FX ON / OFF; an FX knob with a command as its command
     * (SOURCE as the song has it; the METRONOME's knobs have none); the loops' mixes; which loops are silenced. tm: when
     * the change was made (an edit's start) */
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
        j.list.forEach((L, i) => {
            for (const [k] of MIXC) {
                const was = last.mix[i][k], v = j.mixer[i][k];
                if (v !== was) { last.mix[i][k] = v; log(a => logMix(a, i, k, was, v)); }
            }
        });
        const sil = j.list.map(silenced);
        if (sil.some((v, i) => v !== last.sil[i])) { last.sil = sil; log(a => logOne(a, "sil", { sil })); }
    }
    /* the punch-in running ends at the song's row `at`: its command's value the rows it ran (1-FF), in its pattern if
     * written already */
    function livePunchEnd(at) {
        const v = U.clamp(at - lv.pabs, 1, 255);
        if (lv.pk < 0) return;
        const x = lv.ev.find(y => y.k === "punch");
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
    /* ---- the keys' cells ---- */
    const keyAt = (a, lane) => lv.ev.find(x => sameAt(x, a) && x.k === "key" && x.lane === lane);
    /* a key's note at the song's row `at` (in the pattern being written or the next) on a keys' lane, in instrument
     * inst (delay: REAL TIME's EDx; glide: "GLIDE" a 320 with it, "LEG" a LEG; never on a TB-303's note), replacing what
     * that lane has at the row: false, no room */
    function liveKey(at, lane, note, delay, glide, inst) {
        const a = atOf(at), e = keyAt(a, lane) || evNew(a);
        if (!e) return false;
        Object.assign(e, { k: "key", lane, note, delay, glide, sld: false, inst, koff: -1 });
        return true;
    }
    function liveKeySlide(at, lane) {                      /* the note there slides into the next (a TB-303's SLD) */
        const e = Math.floor(at / LIVE_ROWS) < lv.done ? null : keyAt(atOf(at), lane);
        if (e && isNote(e) && !e.glide) e.sld = true;      /* (a joined note is never a TB-303's) */
    }
    /* a key let go: its note's end at the row (kxx >= 0: REAL TIME's key off at that tick, else a note off; own: in its
     * note's own row): 0 nothing written (a note there ends it), 1 into its note's cell (heard now), 2 written there */
    function liveKeyOff(at, lane, kxx, own) {
        const a = atOf(at);
        let e = keyAt(a, lane);
        if (e && kxx >= 0 && own && e.koff < 0) { e.koff = kxx; return 1; }   /* (let go in its note's row: that cell's key off) */
        if (e && isNote(e)) return 0;
        if (e && kxx >= 0) { if (e.koff < 0) e.koff = kxx; return 2; }
        if (!e && !(e = evNew(a))) return 0;
        Object.assign(e, { k: "key", lane, note: kxx >= 0 ? null : M.OFF, delay: 0, glide: false, sld: false, inst: null, koff: kxx >= 0 ? kxx : -1 });
        return 2;
    }
    /* a keys' cell as written: a note in its instrument at the keys' volume, its note delay, its glide or SLD; a note
     * off; its key off */
    function keyCell(e) {
        const c = M.newCell(e.note, null, null);
        if (isNote(e)) {
            c.inst = e.inst;
            c.vol = lv.kvol;
            if (e.delay) c.cmds.push({ c: 0x0E, v: 0xD0 | e.delay });
            if (e.glide) c.cmds.push(e.glide === "LEG" ? { c: 0xD3, v: 0 } : { c: 0x03, v: 0x20 });
            else if (e.sld) c.cmds.push({ c: 0xC1, v: 0 });
        }
        if (e.koff >= 0) c.cmds.push({ c: 0x14, v: e.koff });
        return c;
    }
    /* ---- writing a pattern ---- */
    /* the pattern being written: its changes in rows' order (their order kept within a row), the loops' mixes at its
     * start (the value before each one's first change waiting), its rows (end), the commands put in a cell so far (a
     * sweep over the tracks from 0), where the punch-in still running went (-1 nowhere) */
    function passStart(end) {
        const j = LS(), idx = lv.ev.filter(x => !x.nx).sort((a, b) => a.r - b.r);
        const mix0 = j.list.slice(0, lv.nl).map((_, i) => Object.assign({}, j.mixer[i]));
        for (let x = lv.ev.length - 1; x >= 0; x--) { const e = lv.ev[x]; if (e.k === "mix") mix0[e.i][e.key] = e.b; }
        return { idx, end, mix0, placed: new Set(), prow: -1, ptrk: -1 };
    }
    /* the track's mix brought up to the loop's, as far as the cell has room (mixer commands in MIXC's order) */
    const catchUp = (c, have, mix) => {
        for (const [k, code] of MIXC) if (have[k] !== mix[k] && c.cmds.length < 4) { c.cmds.push({ c: code, v: mix[k] }); have[k] = mix[k]; }
    };
    /* track t of the pattern (t < 0: what fits no track, counted): row by row the cell of the lanes on it as they played
     * at the loops' row heard there (none while they were silent, their notes ending where they fell silent; a loop
     * silenced left out, its notes ending there; a note bringing the track's mix to its loop's), a loop's mix turned on
     * the track it holds, the keys' cells, the row's commands where they find room first. Its cells, the notes left out,
     * and the track's state at the end (kept for the next pattern once this one is written) */
    function build(pe, t) {
        const L = LS().list, nl = lv.nl, four = () => [false, false, false, false], off = () => M.newCell(M.OFF, null, null);
        const mix = pe.mix0.map(m => Object.assign({}, m)), have = Object.assign({}, t >= 0 ? lv.have[t] : MIX_DEF);
        const snd = lv.snd.map(a => a.slice()), ksnd = lv.ksnd.slice(), on = [], kon = [], cells = [];
        const sounding = i => on[i].some((o, l) => o && snd[i][l]);
        let owner = t >= 0 ? lv.owner[t] : -1, left = 0, x = 0, lr = lv.lr0, sil = lv.sil.slice();
        for (let i = 0; i < nl; i++) on[i] = [0, 1, 2, 3].map(l => lv.plan.loops[i][l] === t);
        for (let l = 0; l < 4; l++) kon[l] = lv.plan.keys[l] === t;
        for (let r = 0; r < pe.end; r++) {
            let c = null, q = x, nsil = null, nlr = null;
            const turned = new Set();
            while (q < pe.idx.length && pe.idx[q].r === r) q++;
            for (let k = x; k < q; k++) {                  /* the loops' mixes turned, the loops silenced, where they are heard */
                const v = pe.idx[k];
                if (v.k === "mix") { mix[v.i][v.key] = v.a; turned.add(v.i); }
                else if (v.k === "sil") nsil = v.sil;
                else if (v.k === "loops") nlr = v.row;
            }
            if (nlr != null) {                             /* (silent from here: their notes end; on again: from that row,
                                                            * nothing sounding) */
                if (nlr === SILENT && lr !== SILENT) for (let i = 0; i < nl; i++) { if (sounding(i) && !c) c = off(); snd[i] = four(); }
                lr = nlr;
            }
            if (nsil) {
                for (let i = 0; i < nl; i++) if (nsil[i] && !sil[i]) { if (sounding(i) && !c) c = off(); snd[i] = four(); }
                sil = nsil.slice();
            }
            for (let i = 0; lr !== SILENT && i < nl; i++) {
                if (sil[i]) continue;                      /* (silenced: left out) */
                for (let l = 0; l < 4; l++) {
                    if (!on[i][l]) continue;
                    const a = laneAt(L[i], l, lr, lv.len), d = a.cell;
                    if (!d) { if (a.ends && snd[i][l]) { if (!c) c = off(); snd[i][l] = false; } continue; }
                    snd[i][l] = !ends(d) && (isNote(d) || snd[i][l]);
                    if (t < 0) { if (isNote(d)) left++; continue; }
                    c = M.copyCell(d);
                    if (isNote(c)) { c.inst = L[i].inst; catchUp(c, have, mix[i]); owner = i; }
                }
            }
            for (let i = 0; t >= 0 && i < nl; i++) {       /* a loop's mix turned, on the track it holds */
                if (!turned.has(i) || owner !== i || !on[i].some(Boolean)) continue;
                const y = c || M.newCell(null, null, null);
                catchUp(y, have, mix[i]);
                if (!M.cellEmpty(y)) c = y;
            }
            for (let k = x; k < q; k++) {                  /* the keys' cells */
                const v = pe.idx[k];
                if (v.k !== "key" || !kon[v.lane]) continue;
                const w = keyCell(v);
                ksnd[v.lane] = !ends(w) && (isNote(w) || ksnd[v.lane]);
                if (t < 0) { if (isNote(w)) left++; continue; }
                c = w;
            }
            for (let k = x; t >= 0 && k < q; k++) {        /* the row's commands: each in the first cell with room, from track 0 */
                const v = pe.idx[k];
                if ((v.k !== "cmd" && v.k !== "punch") || pe.placed.has(v)) continue;
                if (c && (c.cmds.length >= 4 || c.cmds.some(y => y.c === v.code))) continue;
                (c = c || M.newCell(null, null, null)).cmds.push({ c: v.code, v: v.v });
                pe.placed.add(v);
                if (v.k === "punch") { pe.prow = r; pe.ptrk = t; }
            }
            if (c) cells[r] = c;
            if (lr !== SILENT) lr = (lr + 1) % lv.len;    /* (the loops' next row: their start again past the length) */
            x = q;
        }
        return { cells, left, have, owner, snd, ksnd, on, kon, lr };
    }
    /* the pattern lv.done, its rows up to end (LIVE_ROWS whole; less: the song ended there, its last pattern that long),
     * at the first free pattern number; the song made with the first (its name, the loops' setup at the start, its
     * tracks' mixes, it plays once and stops), then a position more each pattern. LIVE_OK; not (nothing of it written):
     * LIVE_FULL, the edit memory full; LIVE_PATTERNS, no pattern number free */
    function liveChunk(end) {
        end = U.clamp(end, 1, LIVE_ROWS);
        const pe = passStart(end), b = [];
        for (let t = 0; t < 16; t++) b.push(build(pe, t));
        lv.left += build(pe, -1).left;
        const num = M.freePattern();
        if (num < 0) return LIVE_PATTERNS;
        const p = M.newPattern(num, 16, end), S = lv.setup, n = lv.song;
        let hi = 1;
        b.forEach((o, t) => o.cells.forEach((c, r) => { if (c) { p.cells[r * 16 + t] = c; hi = Math.max(hi, t + 1); } }));
        p.tracks = hi;
        if (!lv.made) M.state.songs[n] = Object.assign(M.newSong(), { name: lv.name, tempo: S.tempo, ticks: S.ticks, rpb: S.rpb, swing: S.swing,
                                                                       atEnd: "STOP", fx: S.fx, mixer: lv.have.map(x => Object.assign({}, x)), order: [] });
        M.state.patterns[num] = p;
        M.state.songs[n].order.push(num);
        M.settle();
        if (M.editsUsed > M.EDITS_MAX) {                  /* (nothing of it kept: the song keeps the patterns before) */
            M.state.songs[n].order.pop();
            M.state.patterns[num] = null;
            if (!lv.made) M.state.songs.length = n;
            M.settle();
            return LIVE_FULL;
        }
        b.forEach((o, t) => {                             /* its tracks' state kept for the next */
            lv.have[t] = o.have;
            lv.owner[t] = o.owner;
            for (let i = 0; i < lv.nl; i++) for (let l = 0; l < 4; l++) if (o.on[i][l]) lv.snd[i][l] = o.snd[i][l];
            for (let l = 0; l < 4; l++) if (o.kon[l]) lv.ksnd[l] = o.ksnd[l];
        });
        for (const e of pe.idx) if (e.k === "sil") lv.sil = e.sil.slice();
        lv.lr0 = b[15].lr;
        if (pe.prow >= 0) lv.pw = { num, r: pe.prow, t: pe.ptrk };
        lv.ev = lv.ev.filter(x => x.nx);                   /* (the next one's changes become the current's) */
        for (const x of lv.ev) x.nx = false;
        lv.done++;
        lv.made = true;
        M.version++;
        PTFM.audio.changed();
        return LIVE_OK;
    }
    /* it is over: the keys' notes sounding end; the loops silent, their replay stops at their place; the toast says how
     * it went (why: LIVE_FULL, LIVE_PATTERNS, SF_256, SF_DONE) */
    function songFinish(why) {
        const n = lv.done, left = lv.left, s = M.hex(lv.song), secs = Math.max(0, Math.floor(P.time() - lv.t0));
        lp.song = null;
        lv = null;
        P.lpEnd(P.LP_KEYS);
        songKeysClear();
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
        if (!songOn()) { lp.song = null; return; }
        if (at >= 0 && songChunks(at)) return;
        livePunchEnd(at + 1);
        if (at >= lv.done * LIVE_ROWS && lv.done < 256) {
            const r = liveChunk(at % LIVE_ROWS + 1);
            if (r !== LIVE_OK) { songFinish(r); return; }
        }
        songFinish(SF_DONE);
    }
    function songEndHere() {                               /* ... at the row heard now (REC, leaving LOOPS) */
        songEnd(!lv.rows0 ? -1 : playingHere() ? songNow().row : lp.sat);
    }
    /* on every screen (the player's tick, a key, a pause): its row 0 known (its replay's first row played), the replay
     * paused (it ends where it was heard last), the player's rest switches (the loops silent or on again, from their
     * row), what changed, each pattern written once its rows are heard, a punch-in */
    function livePoll() {
        if (!songOn()) return;
        const on = playingHere();
        if (!lv.rows0) { if (on && !P.lpPlayed()) return; lv.rows0 = 1; }
        if (!on) { songEnd(lp.sat); return; }
        const rs = P.lpRestState();
        if (rs.sw !== lp.sw) { lp.sw = rs.sw; liveLoops(rs.swN - lv.rows0, rs.swRow); }
        liveChanges();
        const now = songNow().row;
        if (songChunks(now)) return;
        livePunch(U.punch == null ? -1 : U.punch);
        lp.sat = now;
    }
    P.lpPoll = livePoll;
    /* the song's replay row count where its row `at` is heard */
    const songN = at => (lv.rows0 || 1) + at;
    /* a key while the song records (or is armed: the key starts it, its first note): landed as a take's (behaviour 1) on
     * the song's time, on a keys' lane (MONO lane 0, legato as MONO LEGATO says; POLY the first free, a fifth neither
     * heard nor written), written at its row and heard there (or at once); false when it does not land in the song (it
     * plays freely then) */
    function songKeyDown(n, m) {
        if (lp.song && lp.song.state === "armed" && !songStart()) return true;
        if (!songOn()) return false;
        const j = LS(), nw = songNow(), at = landing(nw.pos, nw.tick), K = lp.sk, mono = j.nextMono, inst = U.inst;
        let abs = Math.max(0, at.row), lane = 0, glide = 0;
        if (mono) {
            const k = K.cur;
            if (k != null && K.abs[k] !== abs && j.legato !== "JUMP") {
                if (M.instType(K.kinst) === "TB303") { liveKeySlide(K.abs[k], 0); glide = 1; }   /* a TB-303 slides: SLD on the note held */
                else if (M.instType(inst) !== "TB303") glide = j.legato === "LEGATO" ? "LEG" : 0x20;   /* (a TB-303's note after
                                       * another instrument's starts, as it is heard: never joined; LEGATO: LEG, else a
                                       * glide, 2 semitones a tick) */
            }
        } else {
            while (lane < 4 && K.busy[lane][0] != null && K.busy[lane][0] <= abs && (K.busy[lane][1] == null || abs < K.busy[lane][1])) lane++;
            if (lane >= 4) return true;                    /* a fifth note: neither heard nor written */
        }
        K.info[n] = { lane, mono, delay: at.delay };
        K.abs[n] = abs;
        K.busy[lane] = [abs, null];
        if (mono) { K.cur = n; K.kinst = inst; }
        if (!liveKey(abs, lane, m, at.delay, glide === 0x20 ? "GLIDE" : glide === "LEG" ? "LEG" : null, inst)) return true;
        const c = M.newCell(m, inst, U.set("KEYS VOLUME"));
        if (glide === 0x20) c.cmds.push({ c: 0x03, v: 0x20 });
        else if (glide === "LEG") c.cmds.push({ c: 0xD3, v: 0 });
        else if (glide) P.lpSlide(P.LP_KEYS, 0);
        P.lpNoteAt(P.LP_KEYS, lane, c, at.wait ? songN(abs) : -1, n);
        return true;
    }
    /* a key let go while the song records: its end as a take's (behaviour 2), written and heard at its row; a MONO key
     * ends its note only while it is the lane's. False: no note of the song's */
    function songKeyUp(n) {
        const K = lp.sk, info = K.info[n];
        if (!info) return false;
        delete K.info[n];
        if (!songOn() || (info.mono && K.cur !== n)) return true;
        if (info.mono) K.cur = null;
        const nw = songNow(), nabs = K.abs[n];
        let abs, kxx = -1;
        if (LS().quantize === "REAL TIME") {
            abs = Math.floor(nw.pos);
            if (abs < nabs || (abs === nabs && nw.tick <= info.delay)) abs = nabs + 1;
            kxx = nw.tick;
        } else abs = Math.max(Math.floor(nw.pos + .5), nabs + 1);
        const b = K.busy[info.lane];
        if (b[0] === nabs && b[1] == null) b[1] = abs;
        if (Math.floor(abs / LIVE_ROWS) < lv.done) return true;
        const r = liveKeyOff(abs, info.lane, kxx, abs === nabs), off = M.newCell(M.OFF, null, null);
        if (r === 1) P.lpNoteAt(P.LP_KEYS, info.lane, off, -1, n);
        else if (r === 2) P.lpNoteAt(P.LP_KEYS, info.lane, off, abs > nw.pos ? songN(abs) : -1, n);
        return true;
    }
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
            rec() { U.close(this); lp.song = { state: "armed", splay: playingHere() }; return true; },   /* REC (ARM): armed, closed */
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
    /* each row the loops' replay schedules: a take armed begins at their start, one recording ends one pass later (the
     * first loop closes there at its most); mutes and solos waiting for the bar line act on it (each at its time) */
    P.loopsRow = function (row, t, dur, pass) {
        if (P.what !== "loops") return;
        const ms = Math.max(0, (t - P.time()) * 1000), tk = lp.take;
        if (row === 0) {
            if (lp.arm && !lp.arm.wait) beginTake(pass);
            else if (tk && pass === tk.pass + 1) setTimeout(() => { if (lp.take === tk) { if (tk.first) closeFirst(true); else endTake(); } }, ms);
        }
        if (row % barRows() === 0) for (const [k, p] of lp.pend) if (!p.due) { p.due = true; setTimeout(() => applyPend(k), ms); }
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
     * calling the wait off (behaviour 11) */
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
    function applyPend(k) {
        const p = lp.pend.get(k), L = p && list()[p.i];
        lp.pend.delete(k);
        if (!L) return;
        U.edit(() => { if (p.kind === "mute") L.muted = !L.muted; else L.soloed = !L.soloed; });
        P.mutesChanged();
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
        } else if (pr === "MONO" || pr === "POLY") {
            const mono = pr === "MONO";
            if (j.nextMono !== mono) U.edit(() => { j.nextMono = mono; });
            U.toast("NEXT LOOPS: " + pr);
        }
    }
    /* the white keys' lights, a loop a key: full while it sounds, dim muted or silenced, blinking while a mute or solo
     * waits for the bar line, dark with no loop */
    function loopLeds(keys) {
        U.WHITE.forEach((n, i) => { const L = list()[i]; keys[n] = !L ? 0 : waiting(i) ? (U.blink(250) ? 1 : 0) : silenced(L) ? .25 : 1; });
    }

    /* ----------------------------------------------------------------- the keys --- */
    /* PLAY mode: a take armed from silence starts with the key's note, a song armed with it; a take writes it, a song
     * records it (as heard); else the keys play freely, a re-record's loop's instrument from its REC on */
    function playKey(n, down) {
        const m = U.midi(n), s = lp.song;
        if (m < 12 || m > 127) return;
        if (s && (s.state === "on" || s.state === "armed") && songKeyDown(n, m)) return;
        if (lp.arm && lp.arm.wait && !P.countingIn()) { startTake(P.time()); takeKeyDown(n, m); return; }
        if ((lp.take || (lp.arm && !lp.arm.wait)) && takeKeyDown(n, m)) return;
        const L = reLoop();
        U.keyOn(n, L ? L.inst : U.inst);
        lp.free[n] = true;
    }
    /* the loop a re-record's REC is on, armed or recording, until its take ends */
    const reLoop = () => { const i = lp.take && lp.take.re ? lp.take.loop : lp.arm && lp.arm.re ? lp.arm.loop : -1; return i >= 0 ? list()[i] || null : null; };

    /* --------------------------------------------------------------- the knobs --- */
    /* ALGORITHM: TEMPO, any time (SEL held: by 10) */
    function tempoTurn(s, coarse, quiet) {
        const j = LS(), t = U.clamp(j.tempo + s * (coarse ? 10 : 1), M.TEMPO_MIN, M.TEMPO_MAX);
        if (t !== j.tempo) U.edit(() => { j.tempo = t; });
        if (!quiet) U.showKnob("TEMPO", M.hex(j.tempo) + " (" + M.bpm(j) + " BPM)");
    }
    /* K1-K4: the selected loop's LEVEL, PAN, DELAY SEND, REVERB SEND; on + NEW LOOP the next loop's (SEL held: by 16) */
    const MIXK = [["lvl", "LEVEL"], ["pan", "PAN"], ["dly", "DELAY SEND"], ["rev", "REVERB SEND"]];
    function mixTurn(e, s, coarse) {
        const [k, nm] = MIXK[e - E.K1], j = LS(), L = selLoop(), row = L ? j.mixer[lp.sel] : j.nextMix;
        const v = U.clamp(row[k] + s * (coarse ? 16 : 1), 0, k === "pan" ? 255 : 127);
        if (v !== row[k]) U.edit(() => { row[k] = v; if (!L) syncMix(j); });
        if (L) PTFM.audio.mixUnset(16 + lp.sel, k);
        U.showKnob(nm, U.mixWords(k, v));
    }

    /* ---------------------------------------------------------------- dialogs --- */
    const edit = fn => U.edit(() => fn(LS()));
    const step = (L, v, d) => L[U.clamp(L.indexOf(v) + Math.sign(d), 0, L.length - 1)];
    function props() {
        U.props("LOOPS PROPERTIES", [
            { name: "TEMPO", get: () => M.hex(LS().tempo) + " (" + M.bpm(LS()) + " BPM)", change: (d, c) => tempoTurn(d, c, true) },
            { name: "SWING", get: () => LS().swing + " %", change: d => edit(j => { j.swing = U.clamp(j.swing + Math.sign(d), 50, 75); }) },
            { name: "LENGTH", get: () => LS().rows ? barsText(LS().rows / barRows()) : "THE FIRST LOOP SETS IT", get fixed() { return !LS().rows; },
              change: lengthTurn },
            { name: "GRID", get: () => (GRIDS.find(g => g[1] === LS().rpb) || GRIDS[0])[0], change: gridTurn },
            { name: "QUANTIZE", get: () => LS().quantize, change: d => edit(j => { j.quantize = step(M.QUANTIZES, j.quantize, d); }) },
            { name: "MONO LEGATO", get: () => LS().legato, change: d => edit(j => { j.legato = step(["GLIDE", "LEGATO", "JUMP"], j.legato, d); }) },
            { name: "COUNT-IN", get: () => LS().countIn, change: d => edit(j => { j.countIn = step(M.LOOP_COUNT_INS, j.countIn, d); }) },
        ]);
    }
    /* LENGTH: a bar more or less; longer adds silence at the end, shorter cuts (behaviour 9), asking first when notes
     * would go */
    function lengthTurn(d) {
        const j = LS();
        if (lp.song) { U.refuse(B.GLO); U.toast("RECORDING A SONG"); return; }
        if (!j.rows || lp.take || lp.arm) { U.refuse(B.GLO); return; }
        const bR = barRows(), bars = U.clamp(j.rows / bR + Math.sign(d), 1, maxBars()), n = bars * bR;
        if (n === j.rows) return;
        if (n > j.rows) { U.edit(() => { j.rows = n; }); return; }
        const go = () => { if (U.edit(() => cutTo(j, n))) P.lpLength(n); };
        if (notesGo(j, n)) U.ask(["CUT TO " + barsText(bars) + "?", "NOTES IN THE BARS CUT GO"], ["CUT", "KEEP"], a => { if (a === "CUT") go(); });
        else go();
    }
    /* the loops cut at n rows: a base past it cut there and played once (a note still sounding at the cut stops at the
     * wrap, as the player stopped it: a note off at row 0); else as many repeats as reach it */
    function cutTo(j, n) {
        for (const L of j.list) {
            if (L.base > n) { const held = heldAt(L, n); L.base = n; L.cells.length = n * 4; L.repeats = 1; stopAt(L.cells, held, 0); }
            else L.repeats = Math.min(L.repeats, Math.ceil(n / L.base));
        }
        j.rows = n;
    }
    /* a note that plays (within its loop's repeats and the length) starting at row n or later */
    const notesGo = (j, n) => j.list.some(L => L.cells.some((c, k) => {
        if (!isNote(c)) return false;
        for (let r = 0; r < L.repeats; r++) { const row = r * L.base + (k >> 2); if (row >= j.rows) break; if (row >= n) return true; }
        return false;
    }));
    function gridTurn(d) {
        const j = LS();
        if (lp.song) { U.refuse(B.GLO); U.toast("RECORDING A SONG"); return; }
        if (j.list.length || lp.take || lp.arm) { U.toast("GRID: ONLY BEFORE THE FIRST LOOP"); return; }
        const i = U.clamp(GRIDS.findIndex(g => g[1] === j.rpb) + Math.sign(d), 0, GRIDS.length - 1);
        if (GRIDS[i][1] !== j.rpb) U.edit(() => { j.rpb = GRIDS[i][1]; });
    }
    /* ENV: the cartridges; the current instrument becomes the picked one's first */
    function carts() {
        U.cartList("NEW LOOP: A CARTRIDGE", U.inst >> 8, c => {
            const first = c.slots.findIndex(Boolean);
            if (first < 0) { U.toast("CARTRIDGE " + M.hex(c.id) + " IS EMPTY"); return; }
            U.setInst(c.id << 8 | first);
        });
    }
    /* EDIT: the selected loop recorded again (on + NEW LOOP the last), armed as REC arms */
    function reRecord() {
        const n = list().length;
        if (!n) { U.refuse(B.EDIT); U.toast("NO LOOPS YET"); return; }
        if (refused(B.EDIT)) return;
        recordAt(selLoop() ? lp.sel : n - 1, B.EDIT);
    }
    /* LFO: PEEL, the selected loop goes (on + NEW LOOP the last); the selection stays where it was; the only loop takes
     * the length with it */
    function peel() {
        const j = LS(), n = j.list.length;
        if (!n) { U.refuse(B.LFO); U.toast("NO LOOPS YET"); return; }
        if (refused(B.LFO)) return;
        const i = selLoop() ? lp.sel : n - 1;
        if (!U.edit(() => { j.list.splice(i, 1); j.mixer.splice(i, 1); j.mixer.push({}); syncMix(j); if (!j.list.length) j.rows = 0; })) return;
        P.lpPeeled(i);
        rekeyPend(i);
        if (!j.list.length) { if (playingHere()) P.pause(); P.kept.loops.row = 0; }
        lp.sel = U.clamp(lp.sel, 0, topSel());
        P.mutesChanged();
        U.toast(name(i) + " PEELED");
    }
    function menu() {
        const L = selLoop();
        U.menu("loops", L ? name(lp.sel) + " " + U.instName(L.inst) : "LOOPS",
               [L && "APPLY INSTRUMENT", "DOUBLE", songArmed() ? "CANCEL RECORD LIVE" : "RECORD LIVE", "SAVE TO PATTERN", "LOAD FROM PATTERN",
                "CLEAR LOOPS", M.unsavedThing("loops") && "REVERT"], a => {
            const none = () => { if (LS().list.length) return false; U.toast("NO LOOPS YET"); return true; };
            if (a === "APPLY INSTRUMENT") {
                const i = list().indexOf(L);
                if (lp.song) { U.refuse(B.SEL); U.toast("RECORDING A SONG"); return; }
                if (i < 0 || !U.edit(() => { L.inst = U.inst; })) return;
                U.toast(name(i) + ": " + M.refText(U.inst) + " " + U.instName(U.inst));
            }
            else if (a === "DOUBLE") { if (!none() && !refused(B.SEL)) double(); }
            else if (a === "RECORD LIVE") {                /* its panel */
                if (none() || refused(B.SEL)) return;
                if (songNumber() < 0) { U.refuse(B.SEL); U.toast("NO SONG NUMBER FREE"); return; }
                slOpen();
            }
            else if (a === "CANCEL RECORD LIVE") { songCancel(); U.toast("CANCELLED"); }
            else if (a === "SAVE TO PATTERN") { if (!none()) saveTo(); }
            else if (a === "LOAD FROM PATTERN") { if (!refused(B.SEL)) loadFrom(); }
            else if (a === "CLEAR LOOPS") { if (!none() && !refused(B.SEL)) clearAll(); }
            else if (a === "REVERT") { if (!refused(B.SEL)) revert(); }
        });
    }
    /* DOUBLE (behaviour 8): the length x2; a loop filling the length exactly doubles its repeats, any other has its
     * notes written out to the old length first and plays twice */
    function double() {
        const j = LS();
        if (j.rows * 2 > maxRows()) { U.refuse(B.SEL); U.toast("DOUBLE: AT MOST " + barsText(maxBars())); return; }
        if (!U.edit(() => {
            for (const L of j.list) {
                if (L.base * L.repeats === j.rows) L.repeats *= 2;
                else { writeOut(L, j.rows); L.repeats = 2; }
            }
            j.rows *= 2;
        })) return;
        U.toast("DOUBLED: " + barsText(j.rows / barRows()));
    }
    /* a loop's notes written out over n rows: its repeats one after another, the last cut at n. Where the player stops its
     * lanes, a note still sounding there gets a note off: where its repeats end short of n (its rest), or at row 0 when n
     * cuts the last repeat (the stop at the wrap) */
    function writeOut(L, n) {
        const cells = new Array(n * 4).fill(null), held = heldAt(L, n), end = L.base * L.repeats;
        for (let r = 0; r < n && Math.floor(r / L.base) < L.repeats; r++)
            for (let l = 0; l < 4; l++) { const c = L.cells[(r % L.base) * 4 + l]; if (c) cells[r * 4 + l] = M.copyCell(c); }
        stopAt(cells, held, end < n ? end : n % L.base ? 0 : -1);
        L.cells = cells;
        L.base = n;
    }
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
    function clearAll() {
        const j = LS(), n = j.list.length;
        U.ask(["CLEAR ALL " + n + (n === 1 ? " LOOP?" : " LOOPS?"), "THE NEXT ONE SETS THE LENGTH"], ["CLEAR", "KEEP"], a => {
            if (a !== "CLEAR" || !U.edit(() => { j.list = []; j.rows = 0; syncMix(j); })) return;
            P.lpCleared();
            lp.pend.clear();
            if (playingHere()) P.pause();
            P.kept.loops.row = 0;
            lp.sel = 0;
            P.mutesChanged();
            U.toast("LOOPS CLEARED");
        });
    }
    /* REVERT: LOOPS as saved (the flash's plays again, from the next row) */
    function revert() {
        U.askRevert("REVERT LOOPS TO SAVED?", null, () => {
            M.revertThing("loops");
            syncSel();
            lp.pend.clear();
            if (!LS().rows && playingHere()) P.pause();
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
    /* the loops written out into pattern n, as long as they are: every loop's notes, repeats written out, each naming its
     * instrument, a note off where a loop goes to rest with a note still sounding (the player stops it there); each lane
     * onto the first track free over all its rows (what does not fit left out, counted); a loop's mix as commands (=
     * LEVEL and the sends, 8xx PAN) with its first note on a track, where the track's mix there differs (as the lanes
     * before it in time left it) */
    function store(n) {
        const j = LS(), rows = j.rows, p = M.newPattern(n, 1, rows), taken = [], mixOn = [], lanes = [];
        let left = 0, used = 0;
        j.list.forEach((L, i) => {
            for (let l = 0; l < 4; l++) {
                const ev = [], rest = L.base * L.repeats;
                for (let r = 0; r < rows && Math.floor(r / L.base) < L.repeats; r++) { const c = L.cells[(r % L.base) * 4 + l]; if (c) ev.push([r, c]); }
                if (!ev.some(([, c]) => isNote(c))) continue;
                let held = ev.reduce((h, [, c]) => sounding(h, c), false);
                if (held && rest < rows) { ev.push([rest, M.newCell(M.OFF, null, null)]); held = false; }
                const from = ev[0][0], to = held ? rows : ev[ev.length - 1][0] + 1;
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
        const s = cur && cur.order.includes(n) ? cur : M.state.songs.find(x => x.order.includes(n));
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
        if (!U.edit(() => {
            j.list = loops.map((L, k) => ({ inst: L.inst, col: k % 12, mono: false, lanes: L.lanes, base: rows, repeats: 1, muted: false, soloed: false, cells: L.cells }));
            j.mixer = loops.map(L => Object.assign({ lvl: 127, pan: 128, dck: 0, cho: 0, dly: 0, rev: 0 }, L.mix || {}));
            while (j.mixer.length < 16) j.mixer.push({});
            syncMix(j);
            j.rows = loops.length ? rows : 0;
            if (loops.length) Object.assign(j, { tempo: tm.tempo, ticks: tm.ticks, rpb: tm.rpb, swing: tm.swing });
        })) return;
        P.lpCleared();
        lp.pend.clear();
        if (playingHere()) P.pause();
        P.kept.loops.row = 0;
        lp.sel = 0;
        P.mutesChanged();
        const say = (k, w) => k + " " + w + (k === 1 ? "" : "S");
        U.toast("PATTERN " + M.hex(n) + " IN LOOPS: " + (out.size ? say(out.size, "INSTRUMENT") + " LEFT OUT" : cmds ? say(cmds, "COMMAND") + " LEFT OUT" : say(loops.length, "LOOP")));
    }

    /* ---------------------------------------------------- the pattern recorder (ARP) --- */
    /* 4 rows of up to 16 steps, a note a row (a row never set: the note of the row above it, the first C at the keyboard's
     * octave when it first opens), STEP SIZE (the lengths GRID makes whole rows of), LENGTH 4-16 steps, TIMES (the first loop's), the notes'
     * length; kept until power-off, never saved. Its preview is the loop it would write (P.lpPreview, loop 16 of the
     * player's lanes): in the loops' time while they play (ARP switches its sound), else alone from its first step */
    const rec = { o: null };
    const pr = { rows: [0, 1, 2, 3].map(() => ({ note: null, steps: new Array(16).fill(false) })), sel: 0, step: "1/16", len: 16, times: 1,
                 long: false, keys: false };
    const prOpen = () => !!rec.o && U.overlays.includes(rec.o);
    const prLens = () => U.stepLens(LS().rpb);
    const prStepRows = () => { const s = prLens().find(x => x[0] === pr.step); return s ? LS().rpb / s[1] : 1; };
    const prBase = () => pr.len * prStepRows();
    const prNote = r => { for (let k = r; k >= 0; k--) if (pr.rows[k].note != null) return pr.rows[k].note; return U.clamp(60 + 12 * U.octave, 12, 127); };
    const prFilled = r => pr.rows[r].steps.slice(0, pr.len).some(Boolean);
    const prLanes = () => [0, 1, 2, 3].filter(prFilled);
    const prFirst = () => !list().length;
    const prTimesMost = () => Math.max(1, Math.min(8, Math.floor(maxRows() / prBase())));
    const prTimes = () => Math.min(pr.times, prTimesMost());
    /* the first loop's length: TIMES patterns, rounded up to whole bars */
    const prFirstRows = () => Math.min(maxBars(), Math.ceil(prTimes() * prBase() / barRows())) * barRows();
    const pvRows = () => list().length ? LS().rows : prFirstRows();
    const prTarget = () => rec.o && list()[rec.o.target] ? rec.o.target : -1;
    const prInst = () => prTarget() >= 0 ? list()[prTarget()].inst : U.inst;
    const prCol = () => prTarget() >= 0 ? colOf(list()[prTarget()]) : M.LOOP_COLS[nextCol()];
    /* the loop it writes (behaviour 7): the filled rows its lanes in order, a note at KEYS VOLUME at each step on, with A
     * STEP LONG a note off a step on where the next step is off (round to the base's start); its base the steps; TIMES
     * repeats for the first loop, else as many as cover the length, the last cut */
    function prLoop() {
        const sr = prStepRows(), base = prBase(), cells = new Array(base * 4).fill(null), vol = U.set("KEYS VOLUME"), ls = prLanes();
        ls.forEach((r, l) => {
            const st = pr.rows[r].steps, note = prNote(r);
            for (let k = 0; k < pr.len; k++) {
                if (!st[k]) continue;
                cells[k * sr * 4 + l] = M.newCell(note, null, vol);
                const nk = (k + 1) % pr.len;
                if (pr.long && !st[nk]) cells[nk * sr * 4 + l] = M.newCell(M.OFF, null, null);
            }
        });
        const t = prTarget();
        return { inst: prInst(), base, repeats: prFirst() ? prTimes() : Math.ceil(LS().rows / base), cells, lanes: Math.max(1, ls.length),
                 mono: ls.length === 1, track: t >= 0 ? t : list().length };
    }
    P.lpPreview = () => prOpen() && rec.o.pv ? prLoop() : null;
    const prPlaying = () => prOpen() && rec.o.pv && (playingHere() || P.pvOn());
    /* the step playing: where the loops are (or the preview alone), in its frame; -1 none */
    function prStep() {
        if (!prPlaying()) return -1;
        let r;
        if (playingHere()) { const h = here(); if (!h) return -1; r = h.row % Math.max(1, LS().rows); }
        else { const h = P.pvAt(); if (!h) return -1; r = h.row; }
        if (prFirst() && Math.floor(r / prBase()) >= prTimes()) return -1;   /* (the first loop's silent end) */
        return Math.floor((r % prBase()) / prStepRows());
    }
    /* a row's note heard, through the mix the loop takes */
    function prHear(note) {
        const t = P.time(), track = 16 + (prTarget() >= 0 ? prTarget() : list().length);
        P.noteOff("rv", t);
        P.noteOn("rv", note, prInst(), U.set("KEYS VOLUME"), track, t, { song: true });
        P.noteOff("rv", t + Math.max(.15, P.rowSeconds() * prStepRows()));
    }
    function prSetNote(n) { pr.rows[pr.sel].note = U.clamp(n, 12, 127); prHear(pr.rows[pr.sel].note); }
    function prStart() {
        if (lp.song) { U.refuse(B.ARP); U.toast("RECORDING A SONG"); return; }
        if (lp.take || lp.arm || P.countingIn()) { U.refuse(B.ARP); return; }
        const lens = prLens();
        if (!lens.some(s => s[0] === pr.step)) pr.step = (lens.find(([, d]) => LS().rpb / d === 1) || lens[0])[0];
        pr.times = prTimes();
        pr.keys = false;
        if (pr.rows[0].note == null) pr.rows[0].note = prNote(0);   /* (the first row's C kept from here: OCT moves the selected row's) */
        rec.o = U.open(Object.assign({ kind: "recorder", opener: B.ARP, target: selLoop() ? lp.sel : -1, pv: true }, RECORDER));
        if (!playingHere()) P.pvStart(0);                  /* (the preview plays from the moment it opens) */
    }
    function prClose() {
        if (rec.o) U.close(rec.o);
        rec.o = null;
        P.pvStop();
        P.lpEnd(16);
    }
    /* REC: written at once (a new loop, or into the loop it opened on), going on from where the preview was */
    function prWrite(o) {
        if (!prLanes().length) { U.refuse(B.REC); U.toast("NO STEPS ON"); return false; }
        const j = LS(), first = prFirst(), t = prTarget(), W = prLoop(), was = playingHere(), nx = !was && o.pv && P.pvOn() ? P.pvNext() : null;
        let i = t;
        const ok = U.edit(() => {
            if (first) j.rows = prFirstRows();
            const body = { base: W.base, repeats: first ? W.repeats : Math.ceil(j.rows / W.base), cells: W.cells, lanes: W.lanes, mono: W.mono };
            if (t >= 0) Object.assign(j.list[t], body);
            else { i = j.list.length; j.list.push(Object.assign({ inst: U.inst, col: nextCol(), muted: false, soloed: false }, body)); syncMix(j); }
        });
        if (!ok) return false;
        if (was) P.lpTakeLanes(i);                         /* the preview's notes go on as the loop's */
        else { P.pvStop(); if (nx) P.playLoops(nx.row % j.rows, nx.t); }
        P.mutesChanged();
        if (t < 0) lp.sel = Math.min(i + 1, topSel());
        U.toast(t >= 0 ? "PATTERN INTO " + name(t) : "PATTERN: " + name(i) + ", " + barsText(j.rows / barRows()));
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
        if (!list().length) { screenPlay(); return; }
        P.pvStop();
        P.lpEnd(16);
        P.playLoops();
    }
    /* ARP: the loops playing, the preview's sound off and on; else it starts and stops alone, from its first step */
    function prArp(o) {
        if (playingHere()) { o.pv = !o.pv; if (!o.pv) P.lpEnd(16); return; }
        if (P.pvOn()) { P.pvStop(); o.pv = false; return; }
        o.pv = true;
        P.pvStart(0);
    }
    /* "what it makes": the first loop's rows and length, a later loop's repeats over the length; its lanes */
    function prMakes() {
        const base = prBase(), k = prLanes().length, lanes = k + (k === 1 ? " LANE" : " LANES");
        if (prFirst()) {
            const x = prTimes(), rows = base * x, len = prFirstRows();
            return base + " X " + x + " = " + rows + " ROWS " + (rows === len ? "= " : "IN ") + barsText(len / barRows()) + ", " + lanes;
        }
        const len = LS().rows, whole = Math.floor(len / base), rest = len % base;
        return whole + " X " + base + (rest ? " + " + rest : "") + " = " + len + " ROWS, " + lanes;
    }
    const SX = k => 30 + 12 * k + 2 * Math.floor(k / 4);    /* step k's x */
    const RECORDER = {
        play() { prPlay(this); },
        rec() { if (prWrite(this)) prClose(); return true; },
        cancel() { prClose(); },                           /* HOME: nothing written */
        knob(e, s, coarse) {
            const d = Math.sign(s);
            if (e === E.SELECT) pr.sel = U.clamp(pr.sel + d, 0, 3);
            else if (e === E.ALGORITHM) tempoTurn(s, coarse);
            else if (e === E.K1) { const L = prLens(); pr.step = L[U.clamp(L.findIndex(x => x[0] === pr.step) + d, 0, L.length - 1)][0]; pr.times = prTimes(); }
            else if (e === E.K2) { pr.len = U.clamp(pr.len + 4 * d, 4, 16); pr.times = prTimes(); }
            else if (e === E.K3) prSetNote(prNote(pr.sel) + s);
            else if (e === E.K4 && prFirst()) pr.times = U.clamp(pr.times + d, 1, prTimesMost());
        },
        tap(b) {
            if (b === B.ARP) prArp(this);
            else if (b === B.SEL) { pr.keys = !pr.keys; U.toast(pr.keys ? "KEYS: THE NOTE" : "KEYS: THE STEPS"); }
            else if (b === B.GLO) { pr.long = !pr.long; U.toast(pr.long ? "NOTES A STEP LONG" : "NOTES RING TO THE NEXT"); }
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
        /* a white key: its step on or off (heard while the preview is stopped); after SEL, or SEL held, a key sets the
         * row's note at the keyboard's octave */
        key(n, down) {
            if (!down) return true;
            if (pr.keys || U.held(B.SEL)) {
                const m = U.midi(n);
                if (m >= 12 && m <= 127) prSetNote(m);
                pr.keys = false;
                return true;
            }
            const k = U.WHITE.indexOf(n);
            if (k < 0 || k >= pr.len) return true;
            const st = pr.rows[pr.sel].steps;
            st[k] = !st[k];
            if (st[k] && !prPlaying()) prHear(prNote(pr.sel));
            return true;
        },
        /* a step on full, off dark (steps 1, 5, 9, 13 dim), past LENGTH dark, the one playing turned over; setting the
         * note: its key */
        keyLeds(keys) {
            keys.fill(0);
            if (pr.keys || U.held(B.SEL)) { const k = prNote(pr.sel) - U.midi(0); if (k >= 0 && k < 27) keys[k] = 1; return true; }
            const pl = prStep(), st = pr.rows[pr.sel].steps;
            U.WHITE.forEach((n, k) => { keys[n] = k >= pr.len ? 0 : k === pl ? (st[k] ? 0 : 1) : st[k] ? 1 : k % 4 === 0 ? .25 : 0; });
            return true;
        },
        draw() {
            const t = prTarget(), first = prFirst(), col = prCol(), pl = prStep();
            D.rect(4, 36, 232, 186, D.BLACK);
            D.box(4, 36, 232, 186, D.LGREY);
            D.text(10, 40, F.T, "RECORD PATTERN: " + (t >= 0 ? "INTO " + name(t) : "NEW LOOP"), D.CYAN);
            D.text(188, 40, F.T, "PREVIEW", prPlaying() ? D.GREEN : D.DGREY);
            for (let k = 0; k < pr.len; k += 4) D.text(SX(k) + 3, 56, F.T, String(k / 4 + 1), D.DGREY);
            for (let r = 0; r < 4; r++) {
                const on = r === pr.sel, st = pr.rows[r].steps;
                D.text(8, 71 + 17 * r, F.T, M.noteName(prNote(r)), on ? D.WHITE : prFilled(r) ? col : D.DGREY);
                for (let k = 0; k < pr.len; k++)
                    D.rect(SX(k), 70 + 17 * r, 11, 13, st[k] ? (k === pl ? D.WHITE : col) : k === pl ? D.DEEP + D.BLUE : D.DEEP + D.DGREY);
                if (on) D.box(6, 68 + 17 * r, 228, 17, D.WHITE);
            }
            [["K1 STEP SIZE", pr.step], ["K2 LENGTH", pr.len + " STEPS"], ["K3 NOTE", M.noteName(prNote(pr.sel))],
             ["K4 TIMES", first ? "X" + prTimes() : "FILLS " + barsText(LS().rows / barRows())]].forEach(([n, v], i) => {
                const dim = i === 3 && !first, y = 140 + 12 * i;
                D.text(12, y, F.T, n, dim ? D.DGREY : D.CYAN);
                D.text(228 - D.tw(F.T, v), y, F.T, v, dim ? D.DGREY : i === 2 ? D.WHITE : D.LGREY);
            });
            D.text(12, 192, F.T, prMakes(), D.LGREY);
            D.text(12, 206, F.T, "SELECT: ROW  SEL: NOTE  KEYS: STEPS", D.DGREY);
        },
    };

    /* ---------------------------------------------------------- the transport --- */
    /* PLAY: plays / pauses the loops (a take ends as REC ends it); with no loops pauses what plays, else refused; armed
     * with nothing playing: the loops and the take after COUNT-IN. A song armed with the loops not playing as it was:
     * the loops and the song after COUNT-IN; a song recording: the loops silent, or on again, on its next row (it goes
     * on) */
    function screenPlay() {
        if (P.countingIn()) { P.stopCountIn(); lp.arm = null; songCancel(); U.toast("CANCELLED"); return; }
        if (songOn()) { const on = P.lpRestState().on; P.lpRest(on === 1 || on === 3 ? 0 : 1); return; }
        if (lp.song && lp.song.state === "armed" && !lp.song.splay && !playingHere()) { songCount(); return; }
        if (playingHere()) {
            const tk = lp.take;
            if (tk && takeHeard()) stopTake();
            else if (tk) { cancelTake(); lp.arm = { loop: tk.re ? tk.loop : -1, re: tk.re, first: tk.first }; }   /* (still armed) */
            if (lp.arm) lp.arm.wait = true;                /* paused while armed: a key or PLAY starts it then */
            P.pause();
            return;
        }
        if (lp.arm && lp.arm.wait) { countThen(); return; }
        if (!list().length) {
            if (P.playing) { P.pause(); return; }          /* (a song played on into LOOPS: paused) */
            U.refuse(B.PLAY); U.toast("REC, THEN PLAY: THE FIRST LOOP"); return;
        }
        P.pvStop();
        P.playLoops();
    }

    /* -------------------------------------------------------------- drawing --- */
    /* each loop's notes as spans on its lanes, in its base's rows (a note delay and a key off at their ticks): from a note
     * to its lane's next note (gap: a pixel before it), note off, key off or the base's end */
    const spanCache = new WeakMap();
    function spans(L) {
        const key = M.version + "/" + LS().ticks, was = spanCache.get(L);
        if (was && was.key === key) return was.out;
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
        spanCache.set(L, { key, out });
        return out;
    }
    /* the strips' window: the length whole up to 4 bars; longer, 4 bars, the play line going to the middle, the strips
     * scrolling under it, on to the end at the end (the first loop recording: 4 bars, its end the bars so far) */
    function win() {
        const bR = barRows(), W4 = 4 * bR, len = lenNow(), play = playRow();
        if (firstRec()) return { start: U.clamp(play - 2 * bR, 0, Math.max(0, len - W4)), v: W4 };
        if (!len) return { start: 0, v: W4 };
        if (len <= W4) return { start: 0, v: len };
        return { start: U.clamp(play - 2 * bR, 0, len - W4), v: W4 };
    }
    const SX0 = 111, SW = 128;
    function strip(L, i, y, w, rec, dim) {
        const X = r => SX0 + Math.floor((r - w.start) * SW / w.v), bR = barRows();
        D.rect(SX0, y, SW, 11, D.DEEP + D.DGREY);
        let lanes = U.clamp(L.lanes || 1, 1, 4);
        if (rec) L.cells.forEach((c, k) => { if (c) lanes = Math.max(lanes, (k & 3) + 1); });   /* (recording: the lanes it uses so far) */
        const H = [11, 5, 3, 2][lanes - 1], sp = spans(L);
        const h = here(), end = rec ? (h && lp.take && h.pass === lp.take.pass ? h.pos : 0) : LS().rows || L.base * L.repeats;
        const col = rec ? D.RED : dim ? D.DGREY : colOf(L), deep = rec ? D.RED : dim ? D.DGREY : D.DEEP + colOf(L);
        for (let rep = 0; rep < L.repeats && rep * L.base < end; rep++) {
            const off = rep * L.base;
            for (let l = 0; l < lanes; l++) for (const s of sp[l]) {
                const a = off + s.from, b = Math.min(off + s.to, end);
                if (b <= a || b <= w.start || a >= w.start + w.v) continue;
                const ca = Math.max(a, w.start), cb = Math.min(b, w.start + w.v), nx = X(ca);
                D.rect(nx, y + l * (H + 1), Math.max(1, X(cb) - nx - (s.gap && cb === b ? 1 : 0)), H, rep ? deep : col);
            }
        }
        for (let r = Math.ceil(w.start / bR) * bR; r < w.start + w.v; r += bR) if (r > w.start) D.vline(X(r), y, 11, D.BLACK);
    }
    /* how long until an armed take starts: `ARMED: STARTS IN 3 BARS` ... `1 BEAT`; from silence `ARMED: A NOTE OR PLAY` */
    function armText() {
        const h = here();
        if (!h || (lp.arm && lp.arm.wait)) return "ARMED: A NOTE OR PLAY";
        const bR = barRows(), rem = Math.max(0, (LS().rows || bR) - h.pos);
        if (rem > bR) return "ARMED: STARTS IN " + Math.ceil(rem / bR) + " BARS";
        const b = Math.max(1, Math.ceil(rem / LS().rpb));
        return "ARMED: STARTS IN " + (b >= beats() ? "1 BAR" : b + (b === 1 ? " BEAT" : " BEATS"));
    }
    /* the loop an armed take is on: its place (the new loop's: the placeholder's) */
    const armedAt = () => lp.arm ? (lp.arm.loop >= 0 ? lp.arm.loop : list().length) : lp.take && !takeHeard() ? lp.take.loop : -1;
    /* the top line; a song recording: `SONG`, the time recorded (M:SS, MM:SS from 10 minutes), its patterns so far (the
     * one being written counted), the tempo */
    function top() {
        const j = LS(), h = here(), bR = barRows(), y = Y0 + 2, s = lp.song;
        if (songOn()) {
            const sec = Math.max(0, Math.floor(P.time() - lv.t0)), min = Math.floor(sec / 60), np = lv.done + 1;
            D.text(2, y, F.T, "SONG", D.RED);
            D.text(sec >= 600 ? 26 : 32, y, F.T, min + ":" + M.dec(sec % 60), D.LGREY);
            D.text(68, y, F.T, np === 1 ? "1 PATTERN" : M.dec(np, 1) + " PATTERNS", D.LGREY);
            D.text(146, y, F.T, M.bpm(j) + " BPM", D.LGREY);
        } else {
            D.text(2, y, F.T, M.bpm(j) + " BPM", D.LGREY);
            const fr = firstRec() && takeHeard();
            D.text(50, y, F.T, fr ? barsText(recBars()) : j.rows ? barsText(j.rows / bR) : "-- BARS", fr ? D.RED : D.LGREY);
            D.text(98, y, F.T, "Q " + (j.quantize === "REAL TIME" ? "REAL" : j.quantize), D.CYAN);
            let r = "REC", rc = D.DGREY;
            if (s) { r = "SONG"; rc = D.RED; }
            else if (recording() && h) { const b = Math.floor(h.row / bR) + 1; r = "REC " + (lp.take.first ? b : b + "/" + j.rows / bR); rc = D.RED; }
            else if (recording() || armShown() || P.countingIn()) rc = D.RED;
            D.text(152, y, F.T, r, rc);
        }
        const nb = beats(), w = Math.floor(44 / nb), on = h ? Math.floor((h.row % bR) / j.rpb) : -1;
        for (let k = 0; k < nb; k++) D.rect(194 + k * w, Y0 + 4, w - 2, 8, k === on ? (k ? D.GREEN : D.YELLOW) : D.DEEP + D.GREEN);
        D.hline(0, Y0 + 14, 240, D.DGREY);
    }
    function loopRow(L, i, w) {
        const y = 33 + 12 * i, rec = recording() && lp.take.loop === i, armed = armedAt() === i, dim = !rec && silenced(L), wt = waiting(i);
        D.text(2, y, F.T, lp.control ? String(i + 1).padStart(2) : M.hex(i), rec ? D.RED : dim ? D.DGREY : lp.control ? D.WHITE : D.CYAN);
        D.text(20, y, F.T, U.instName(L.inst).slice(0, 9), rec ? D.RED : dim ? D.DGREY : colOf(L));
        if (armed) D.text(80, y, F.T, armText(), D.RED);
        else {
            D.text(80, y, F.T, L.mono ? "M" : "P", dim ? D.DGREY : D.LGREY);
            if (L.repeats > 1) D.text(91, y, F.T, L.repeats > 9 ? "X+" : "X" + L.repeats, dim ? D.DGREY : D.LGREY);
            const st = rec ? ["R", D.RED] : wt ? [wt === "mute" ? "M" : "S", D.YELLOW] : L.soloed ? ["S", D.YELLOW] : L.muted ? ["M", D.DGREY] : null;
            if (st) D.text(104, y, F.T, st[0], st[1]);
            strip(L, i, y, w, rec, dim);
        }
        if (i === lp.sel) D.box(0, y - 1, 240, 13, D.WHITE);
    }
    function placeholder(n) {
        const y = 33 + 12 * n;
        if (armedAt() === n && !list()[n]) D.text(20, y, F.T, M.hex(n) + " " + armText(), D.RED);
        else {
            D.text(20, y, F.T, "+ NEW LOOP", recording() ? D.DGREY : D.WHITE);
            D.text(SX0, y, F.T, M.refText(U.inst) + " " + U.instName(U.inst).slice(0, 9), M.instCol(U.inst));
        }
        if (lp.sel === n) D.box(0, y - 1, 240, 13, D.WHITE);
    }
    function draw() {
        syncSel();
        top();
        const L = list(), n = L.length, w = win();
        L.forEach((l, i) => loopRow(l, i, w));
        if (n < M.LOOPS_MAX) placeholder(n);
        if (!n) {                                          /* no loops yet: how the first comes */
            const armed = !!lp.arm;
            D.text(2, 77, F.T, armed ? "PLAY OR A NOTE: THE FIRST LOOP" : "REC, THEN PLAY OR A NOTE:", D.LGREY);
            if (!armed) D.text(2, 89, F.T, "THE FIRST LOOP SETS THE LENGTH", D.LGREY);
            D.runs(2, 113, F.T, [["PRESETS", D.YELLOW], [", ", D.LGREY], ["ENV", D.YELLOW], [": ITS SOUND", D.LGREY]]);
            return;
        }
        const r = playRow();
        if (r >= w.start && r < w.start + w.v) D.vline(SX0 + Math.floor((r - w.start) * SW / w.v), 33, 12 * n - 1, D.WHITE);
    }
    /* PLAYBACK's view of the loops (specs/ui/screens/playback.md): a loop a track, its notes written out over the length
     * (each row its lowest lane's cell) */
    const pbCache = { v: -1, p: null, j: null };
    U.loopsPattern = function () {
        const j = LS();
        if (pbCache.v === M.version && pbCache.j === j) return pbCache.p;
        const rows = Math.max(1, j.rows), p = M.newPattern(-1, Math.max(1, j.list.length), rows);
        j.list.forEach((L, t) => {
            for (let r = 0; r < j.rows && Math.floor(r / L.base) < L.repeats; r++)
                for (let l = 0; l < 4; l++) {
                    const c = L.cells[(r % L.base) * 4 + l];
                    if (c && c.note != null) { p.cells[r * 16 + t] = Object.assign(M.copyCell(c), { inst: isNote(c) ? L.inst : null }); break; }
                }
        });
        Object.assign(pbCache, { v: M.version, p, j });
        return p;
    };

    /* ---------------------------------------------------------------- screen --- */
    U.register({
        name: "LOOPS", group: "PLAY",
        does: [B.GLO, B.SEQ, B.ENV, B.SEL, B.ARP, B.REC],
        ownArp: "pattern",                                 /* ARP: the pattern recorder; held, nothing (no arpeggiator here) */
        seq: () => lp.control ? (lp.solo ? "SOLO" : "MUTE") : null,   /* the header's SEQ place in CONTROL mode */
        lit(b) {
            if (prOpen()) {                                /* the pattern recorder: ARP full, REC writes, SEL full waiting for a key */
                if (b === B.ARP) return 1;
                if (b === B.REC) return .25;
                if (b === B.SEL) return pr.keys ? 1 : .25;
                if (b === B.GLO) return .25;
                if (b === B.SEQ || b === B.EDIT || b === B.LFO || b === B.ENV) return 0;
                return undefined;
            }
            if (b === B.REC) return lp.arm || songArmed() || P.countingIn() || armShown() ? (U.blink(250) ? 1 : 0) : recording() || songOn() ? 1 : .25;
            if (b === B.EDIT || b === B.LFO) return list().length ? .25 : 0;
            if (b === B.SEQ) return lp.control ? 1 : .25;
            if (b === B.ARP) return .25;
        },
        enter() { syncSel(); },
        /* HOME during a count-in cancels it (otherwise HOME's combination, as everywhere) */
        back() {
            if (!P.countingIn()) return false;
            P.stopCountIn(); lp.arm = null; songCancel(); U.toast("CANCELLED");
            return true;
        },
        /* leaving for any screen but FX: a take ends as REC ends it, an armed one or a song disarmed, a song's recording
         * ends at once (what was heard written) */
        leave() {
            if (P.countingIn()) P.stopCountIn();
            lp.arm = null;
            if (lp.take) { if (takeHeard()) stopTake(); else cancelTake(); }
            if (songOn()) songEndHere(); else songCancel();
            prClose();
            for (const n in lp.held) clearTimeout(lp.held[n].timer);
            lp.held = {};
        },
        reverted() { syncSel(); lp.pend.clear(); if (!LS().rows && playingHere()) P.pause(); P.mutesChanged(); },
        paused() { livePoll(); },                         /* (the loops paused, PLAY on FX, a stop: a song recording ends
                                                         * where it was heard last) */
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
        canPlay: () => list().length > 0 || !!lp.arm || P.playing,
        /* REC: a take (or its end, its disarming); RECORD LIVE armed: it starts (the loops as they are); recording: it
         * ends at once */
        rec() {
            if (P.countingIn()) { P.stopCountIn(); lp.arm = null; songCancel(); U.toast("CANCELLED"); return; }
            if (lp.song && lp.song.state === "armed") { songStart(); return; }
            if (songOn()) { songEndHere(); return; }
            if (lp.arm) { lp.arm = null; U.toast("CANCELLED"); return; }
            if (lp.take) { if (takeHeard()) stopTake(); else { cancelTake(); U.toast("CANCELLED"); } return; }
            recordAt(selLoop() ? lp.sel : -1, B.REC);
        },
        /* HOME's combination: white key n mutes loop n at once, held (or after SEQ) solos it */
        muteKey(wi) { toggle(wi, "mute", false); },
        soloKey(wi) { toggle(wi, "solo", false); },
        muteLeds: loopLeds,
        keyLeds(keys) {
            if (!lp.control) return false;                 /* PLAY mode: the keys light with their notes */
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
        knob(e, s, coarse) {
            if (e === E.SELECT) { lp.sel = U.clamp(lp.sel + s, 0, topSel()); return; }    /* clockwise down, + NEW LOOP last */
            if (e === E.ALGORITHM) { tempoTurn(s, coarse); return; }
            if (e >= E.K1 && e <= E.K4) mixTurn(e, s, coarse);
        },
        tap(b) {
            if (b === B.SEQ) { lp.control = !lp.control; U.toast(lp.control ? "KEYS: CONTROL" : "KEYS: PLAY"); return true; }
            if (b === B.GLO) { props(); return true; }
            if (b === B.ENV) { carts(); return true; }
            if (b === B.SEL) { menu(); return true; }
            if (b === B.ARP) { prStart(); return true; }
            if (b === B.EDIT) { reRecord(); return true; }
            if (b === B.LFO) { peel(); return true; }
            return false;
        },
        key(n, down) {
            livePoll();
            if (!down) {
                if (lp.free[n]) { delete lp.free[n]; U.keyOff(n); }
                else if (lp.held[n]) controlKey(n, false);
                else if (!songKeyUp(n)) takeKeyUp(n);
                return true;
            }
            if (lp.control) controlKey(n, true);
            else playKey(n, true);
            return true;
        },
        footer() {
            if (prOpen()) return ["REC: WRITE", D.LGREY];
            if (songOn()) return ["MEMORY " + Math.floor(Math.max(0, M.EDITS_MAX - M.editsUsed) * 100 / M.EDITS_MAX) + "% FREE", D.RED];   /* (the edit memory left) */
            if (list().length >= M.LOOPS_MAX) return ["16 OF 16", D.LGREY];
            return [LS().nextMono ? "MONO" : "POLY", D.LGREY];
        },
        /* the footer's instrument: the preview's in the pattern recorder; a re-record's loop's from its REC on */
        footInst() {
            if (prOpen()) return prInst();
            const L = reLoop();
            return L ? L.inst : null;
        },
        draw,
        selected: () => lp.sel,                            /* (for the checks) */
        state: () => lp,
        liveState: () => lv,                               /* (RECORD LIVE's recording, for the checks) */
        /* the song's row heard while it records, -99 none */
        liveRow: () => { const h = songOn() && lv.rows0 && playingHere() ? here() : null; return h ? h.n - lv.rows0 : -99; },
        recorder: () => pr,
        makes: () => prMakes(),
    });
})();

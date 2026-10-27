/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground: the transport (specs/ui/transport.md) and the replay of what plays: the
 * current song, the EDITOR's pattern loop, the loops (LOOPS). One thing plays at a time; each keeps its
 * place when paused. Ticks are scheduled a little ahead on the audio clock (audio.js), so edits land
 * on the rows still to come; the screens read where playback is from what was scheduled.
 *
 * The replay runs the commands as XM players do (specs/files/commands.md), after IBXM's rules:
 * a row a TICKS A ROW ticks, a tick 2.5 s / TEMPO; each track's pitch, volume and pan are worked out
 * tick by tick and drive the voice its note plays, a synth's as a sample's. A track's next note
 * releases its previous one, which rings on. TB-303 tracks play as the real one: a gate of half a
 * row, ACC accents, SLD glides into the next note. A FUSION instrument plays every cell of the row its
 * note names (with their instrument parameters). Parameter commands turn the track's instrument's
 * knobs, mixer commands the track's mixer, FX commands the FX, until the next song starts.
 * Not played: Amiga slides (played linear). Extensions to IBXM (commands.md, Tracker effects), as
 * FastTracker 2 plays them: E3x glissando, E5x finetune. */
PTFM.player = (function () {
    "use strict";
    const M = PTFM.model;
    const A = () => PTFM.audio;
    const U = () => PTFM.ui;
    const AHEAD = 0.12;                         /* seconds scheduled ahead */
    const P = { what: null, playing: false, atStart: true };
    const SINE = [0, 24, 49, 74, 97, 120, 141, 161, 180, 197, 212, 224, 235, 244, 250, 253,
                  255, 253, 250, 244, 235, 224, 212, 197, 180, 161, 141, 120, 97, 74, 49, 24];

    let R = null;                               /* the replay of what plays */
    let hist = [];                              /* rows scheduled: {t, pos, row, pat, dur, rpb} */
    const kept = { song: {}, loop: { pat: -1, row: 0 }, loops: { row: 0 } };
    let preview = null, repeat = null, metro = null, countIn = null;
    let idn = 0;

    const now = () => A().time();
    /* (with no song at all, the keys' song: its time and setup what the keys play by) */
    const song = () => M.songOrKeys(R && R.kind === "song" ? R.song : U().song);
    /* LOOPS on screen, or FX from it */
    P.loopsShown = () => { const u = U(); return !!(u.screen && (u.screen.name === "LOOPS" || u.screen.name === "FX" && u.fxLoops)); };
    /* what is held with nothing playing, as the firmware holds it (specs/ui/screens/fx.md#the-fx-setup), its time the
     * keys' (ARP, GATE, the DELAY's FOLLOW, FILTER's envelope, the METRONOME): the loops while LOOPS is shown or when
     * they played last; else the song played last (paused, stopped); else what played last's song; with nothing
     * played since the start (or since UNLOAD SONG), the keys' song, whatever songs there are */
    const held = () => P.what === "loops" || P.loopsShown() ? M.state.loops
                     : P.what === "song" && kept.lastSong != null && M.state.songs[kept.lastSong] ? M.state.songs[kept.lastSong]
                     : P.what ? song() : M.state.keys;
    const clamp = (v, a, b) => v < a ? a : v > b ? b : v;
    const has = (ch, c, sub) => ch.cmds.some(x => x.c === c && (sub == null || x.v >> 4 === sub));

    /* -------------------------------------------------------------- notes --- */
    /* a note from anywhere: the keys, a row, a preview. id names its voice; a FUSION instrument plays
     * its row's cells as voices id/0, id/1 ... */
    const fusionIds = {};
    function noteOn(id, midi, ref, vol, track, t, opts = {}) {
        const type = M.instType(ref), inst = M.inst(ref);
        t = t == null ? now() : t;
        if (!inst) return null;
        /* FILTER's envelope (specs/ui/screens/fx.md#filters-envelope): a note not a song's (opts.song: a replay's) is
         * the keys', its start a hit of SOURCE KEYS (a FUSION's once, not each layer); a song's track's hit is its
         * row's (start) */
        if (!opts.song && !opts.inFusion) A().fire(-1, t);
        if (type === "FUSION") {
            if (opts.inFusion) return null;     /* no nesting: a FUSION in a FUSION's row is silent */
            return fusionRow(id, M.pattern(inst.p.pattern), midi, vol * Math.pow(10, (inst.level || 0) / 20), track, t, !!opts.song);
        }
        /* an instrument being edited plays as edited: no parameter commands, its track's or its own */
        if (U().editing("inst", ref)) opts = Object.assign({}, opts, { params: null, asEdited: true });
        A().noteOn(id, t, midi, ref, vol, track, opts);
        return id;
    }
    /* row midi of pattern p played as a FUSION plays it: every cell, as voices id/0, id/1 ..., with its tick
     * commands (specs/files/commands.md: in FUSION rows only the global ones are ignored), a tick each
     * 2.5 s / TEMPO for as long as the note sounds: EDx starts the cell x ticks late, ECx cuts it at tick x,
     * E9x retriggers it every x ticks, 0xy arpeggiates it (its note, +x, +y, a tick each), Axy slides its
     * volume (up x, down y, each tick but the first) */
    const fusionRuns = {};
    function fusionRow(id, p, midi, vol, track, t, song) {
        if (!p || midi >= p.rows) return null;
        t = t == null ? now() : t;
        const cells = [];
        for (let k = 0; k < p.tracks; k++) {
            const c = p.cells[midi * 16 + k];
            const ins = c && c.inst != null ? c.inst : (c ? M.trackInst(p, midi, k) : null);
            if (!c || c.note == null || c.note === M.OFF || ins == null) continue;
            const cmd = (code, sub) => { const x = c.cmds.find(y => y.c === code && (sub == null || y.v >> 4 === sub)); return x ? x.v : null; };
            const ed = cmd(0x0E, 0xD), ec = cmd(0x0E, 0xC), e9 = cmd(0x0E, 9), arp = cmd(0x00), sl = cmd(0x0A);
            cells.push({
                sub: id + "/" + k, note: c.note, ins, vol: Math.round((c.vol == null ? 0x7F : c.vol) * vol / 127),
                params: c.cmds.filter(x => x.c >= 0x40 && x.c <= 0x7F), accent: c.cmds.some(x => x.c === 0xC0 || x.c === 0xC2),
                d: ed == null ? 0 : ed & 15, cut: ec == null ? null : ec & 15, retrig: e9 ? e9 & 15 : 0,
                arp: arp ? [0, arp >> 4, arp & 15] : null, up: sl ? sl >> 4 : 0, down: sl ? sl & 15 : 0,
                on: false, gone: false, onT: 0,
            });
        }
        fusionIds[id] = cells.map(x => x.sub);
        fusionRuns[id] = { cells, track, n: 0, t, song };
        runFusion(fusionRuns[id], t + AHEAD);
        return fusionIds[id];
    }
    const fusionTick = () => 2.5 / (R && P.playing ? R.tempo : held().tempo);
    /* a FUSION note's ticks, up to time `until` */
    function runFusion(f, until) {
        while (f.t < until && f.cells.some(x => !x.gone)) {
            const n = f.n, t = f.t;
            for (const x of f.cells) {
                if (x.gone || n < x.d) continue;
                if (x.cut != null && n >= x.cut) {          /* ECx before any start (at or before its EDx: silent) */
                    if (x.on) { A().ctl(x.sub, t, 0, 0); A().noteOff(x.sub, t + .004); }
                    x.gone = true;
                    continue;
                }
                if (n === x.d || (x.retrig && n > x.d && (n - x.d) % x.retrig === 0)) {
                    noteOn(x.sub, x.note + (x.arp ? x.arp[0] : 0), x.ins, x.vol, f.track, t, { inFusion: true, song: f.song, params: x.params, accent: x.accent });
                    x.on = true; x.onT = t; x.level = x.vol;
                } else if (x.on && (x.arp || x.up || x.down)) {
                    if (x.up || x.down) x.level = clamp(x.level + 2 * (x.up - x.down), 0, 127);
                    A().ctl(x.sub, t, x.arp ? x.arp[(n - x.d) % 3] : 0, x.level / 127);
                }
            }
            f.n++;
            f.t += fusionTick();
        }
    }
    /* a FUSION note let go: its ticks stop; a cell still to start never does */
    function fusionEnd(id, t) {
        const f = fusionRuns[id];
        if (!f) return;
        delete fusionRuns[id];
        for (const x of f.cells) if (x.on && x.onT > t) A().noteOff(x.sub, x.onT + .004);
    }
    /* the pattern list's keys on a FUSION slot: any pattern heard as that FUSION would play it */
    P.fusionRowOn = (id, pat, midi, vol) => fusionRow(id, M.pattern(pat), midi, vol, -1, null) && id;
    function noteOff(id, t) {
        t = t == null ? now() : t;
        if (fusionIds[id]) { for (const s of fusionIds[id]) A().noteOff(s, t); delete fusionIds[id]; fusionEnd(id, t); }
        A().noteOff(id, t);
    }
    function ctl(id, t, bend, vol) {
        if (fusionIds[id]) for (const s of fusionIds[id]) A().ctl(s, t, bend, vol);
        else A().ctl(id, t, bend, vol);
    }
    P.noteOn = noteOn;
    P.noteOff = noteOff;

    /* ------------------------------------------------------------ the replay --- */
    /* where its timing comes from: the song, the current song for a pattern loop, the loops' own */
    function timing(kind, s) {
        if (kind === "loops") { const j = M.state.loops; return { tempo: j.tempo, ticks: j.ticks, rpb: j.rpb, swing: j.swing, gvol: 64 }; }
        const sg = kind === "song" ? M.state.songs[s.song] : song();
        return sg ? { tempo: sg.tempo, ticks: sg.ticks, rpb: sg.rpb, swing: sg.swing, gvol: sg.gvol }
                  : { tempo: 120, ticks: 6, rpb: 4, swing: 50, gvol: 64 };
    }
    function newChannel(k, pan) {
        return { k, inst: null, note: 60, vid: null, vol: 64, pan, panAdd: 0, pitch: 60 * 64, target: 60 * 64, cmds: [],
                 cell: null, fx: 0, retrig: 0, vibAdd: 0, tremAdd: 0, arpAdd: 0, vibPh: 0, vibSpd: 0, vibDep: 0, vibType: 0,
                 tremPh: 0, tremSpd: 0, tremDep: 0, tremType: 0, panbPh: 0, panbSpd: 0, panbDep: 0,
                 mem: {}, slide: false, gliss: false, sent: { bend: 0, vol: -1, pan: -1 }, offset: 0, seed: (k + 1) * 0xABCDEF };
    }
    /* s.timing: a pattern played at a timing of its own (LOOPS' LOAD list: its song's). The loops' replay counts its
     * passes (pass: a wrap to row 0 makes the next); id: each replay its own; n: the rows it has played (its first 1:
     * a clock for the screens, RECORD LIVE's time) */
    let runs = 0;
    function newReplay(kind, s) {
        const tm = s.timing || timing(kind, s);
        const mix = kind === "loops" ? M.state.loops.mixer : song() ? (kind === "song" ? M.state.songs[s.song] : song()).mixer : null;
        const r = Object.assign({ kind, t: now() + .05, pos: 0, nextRow: 0, breakPos: -1, tick: 1, row: 0, plCount: -1, plChan: -1,
                                  base: kind === "loops" ? 16 : 0, idp: "s", pass: 0, lastRow: -1, id: ++runs, n: 0 }, s, tm);   /* idp: its notes' ids' first letter */
        r.speed = tm.ticks;
        r.nextRow = s.row || 0;
        r.ch = Array.from({ length: 16 }, (_, k) => newChannel(k, mix && mix[k] ? mix[k].pan : 128));
        return r;
    }
    /* PLAYBACK's K4, the playback speed, and K3, the songs' volume (specs/ui/screens/playback.md): the songs' tempo as
     * a share of each one's own (3 to 600 BPM: M.tempoRange), every tempo a song plays scaled (its set tempo, its
     * tempo commands, held within TEMPO's range); the
     * volume dB on each song's own level (-25 OFF), its voices alone (the worklet's songGain: the keys' as they are).
     * Heard at once; kept through pauses, jumps, starts and other songs, 1 and 0 dB again when anything but a song
     * starts (start_, djOn). Not stored */
    let speed = 1, vol = 0;
    P.setSpeed = v => { speed = Math.min(200, Math.max(.005, v)); };
    P.speed = () => speed;
    P.setVolume = db => { vol = Math.min(6, Math.max(-25, db)); songGain(); };
    P.volume = () => vol;
    const songGain = () => A().songGain(P.what === "song" && vol ? (vol <= -25 ? 0 : Math.pow(10, vol / 20)) : 1);
    const songSpeed = r => r.kind === "song" ? speed : 1;
    /* (a DJ deck's rate: its tempo's share of its song's, SYNC, HALF TIME, BRAKE: r.rate at the tick's time; a song's
     * its playback speed) */
    const heldTempo = t => Math.min(M.TEMPO_MAX, Math.max(M.TEMPO_MIN, t));
    const tickLen = r => 2.5 / (r.rate ? r.tempo * r.rate(r.t) : heldTempo(r.tempo * songSpeed(r))) * (r.row % 2 === 0 ? r.swing / 50 : (100 - r.swing) / 50);
    /* the pattern at a position, and how many positions there are (the loops': a clock of no tracks) */
    const LOOPS_CLOCK = { tracks: 0, rows: 0, cells: [] };
    function patAt(r, pos) {
        if (r.kind === "song") { const s = M.state.songs[r.song]; return s && pos < s.order.length ? M.pattern(s.order[pos]) : null; }
        if (r.kind === "loop") return M.pattern(r.pat);
        return LOOPS_CLOCK;
    }
    /* the loops: their length, as the screen says (the first loop recording: the most; the preview alone: its own) */
    const rowsOf = (r, p) => r.kind === "loops" ? (P.lpRows ? P.lpRows(r) : M.state.loops.rows) : p.rows;

    /* one tick at r.t; false: nothing more to play */
    /* while the song's properties are being edited (U.editing), it plays at them, live, and its F commands are
     * skipped: what is edited is heard. The loops always play at their setup (ALGORITHM turns their TEMPO any time) */
    const timeEdited = r => r.kind === "loops" || (r.kind === "song" && U().editing("time", r.song));
    function replayTick(r) {
        if (--r.tick <= 0) {
            if (timeEdited(r)) { const tm = timing(r.kind, r); r.tempo = tm.tempo; r.speed = tm.ticks; r.rpb = tm.rpb; r.swing = tm.swing; }
            r.tick = r.speed;
            if (!replayRow(r)) return false;
        } else if (r.kind === "loops") lanesTick(r);
        else if (!r.silent) for (const ch of r.ch) chTick(ch, r);
        r.t += tickLen(r);
        return true;
    }
    /* r.silent: a DJ deck's counted place (its ghost): the song's flow only (rows, jumps, breaks, tempo), no
     * sound, no commands but those */
    function replayRow(r) {
        if (r.skipTo != null) {                          /* PLAYBACK's K1: the row ended, a clean start there (tick()) */
            r.restartAt = r.skipTo; r.skipTo = null;
            return false;
        }
        if (r.deck && !r.silent && !r.held && !r.step && deckBeforeRow(r) === false) return false;   /* DJ: the pads, the hands */
        if (r.deck && r.silent && r.deck.repeat && r.started) {   /* the hands' counted place keeps REPEAT too */
            const to = r.breakPos >= 0 ? r.breakPos : r.nextRow < 0 ? r.pos + 1 : r.pos;
            if (to !== r.pos) { r.breakPos = r.pos; r.nextRow = 0; }
        }
        const gl = r.deck && r.silent && r.deck.loop && !r.deck.loop.roll ? r.deck.loop : null;
        if (gl) {                                        /* ... and a LOOP set (a ROLL's lands past it): round it */
            if (r.loopN == null) r.loopN = gl.n;
            if (r.loopN >= gl.len) { r.breakPos = gl.pos; r.nextRow = gl.row; r.loopN = 0; }
        }
        const t = r.t;
        if (r.nextRow < 0) { r.breakPos = r.pos + 1; r.nextRow = 0; }
        if (r.breakPos >= 0) {
            if (r.kind === "song") {
                const s = M.state.songs[r.song];
                if (!s || !s.order.length) return false;
                if (r.breakPos >= s.order.length) {          /* its end: a looping song loops from its LOOP FROM in
                                                              * every mode; a song that stops ends as SONGS' play mode
                                                              * says (REPEAT SONG: again from its start), else stops */
                    if (s.atEnd !== "STOP") r.breakPos = Math.min(s.loopFrom || 0, s.order.length - 1);
                    else if (P.endMode === "repeat") r.breakPos = 0;
                    else { r.done = true; r.endT = t; return false; }
                    r.nextRow = 0;
                }
                let guard = s.order.length;
                while (!M.pattern(s.order[r.breakPos]) && guard--) r.breakPos = (r.breakPos + 1) % s.order.length;
            } else {
                r.breakPos = 0;
                if (r.kind === "loop") { const tm = r.timing || timing("loop"); r.tempo = tm.tempo; r.speed = tm.ticks; r.tick = r.speed; }
            }
            r.pos = r.breakPos;
            for (const ch of r.ch) ch.plRow = 0;
            r.breakPos = -1;
        }
        const p = patAt(r, r.pos);
        if (!p) return false;
        const rows = rowsOf(r, p);
        if (r.kind === "loops" && !rows) return false;              /* (no loops left to play) */
        r.row = r.nextRow;
        if (r.row >= rows) r.row = 0;
        r.nextRow = r.row + 1;
        if (r.nextRow >= rows) r.nextRow = -1;
        if (r.kind === "loops" || r.kind === "loop") { if (r.row <= r.lastRow) r.pass++; r.lastRow = r.row; }   /* (back to an earlier row: the next pass) */
        r.n++;
        r.started = true;
        if (r.deck && r.deck.loop && !r.silent && !r.held && !r.step && !r.deck.reverse) deckLoopRow(r);     /* (a loop shorter than a row: fewer ticks) */
        if (gl) r.loopN++;
        r.rowDur = r.speed * tickLen(r);
        if (r.punch && --r.punch.left <= 0) { A().punchAt(r.punch.k, false, t, r.idp); r.punch = null; }  /* its rows are over */
        if (r.kind !== "loops") for (const ch of r.ch) {
            const c = ch.k < p.tracks ? p.cells[r.row * 16 + ch.k] : null;
            if (r.silent) ch.cmds = c ? c.cmds : [];
            else chRow(ch, c, r);
            for (const x of ch.cmds) globalCmd(r, ch, x);
        }
        const h = { t, pos: r.kind === "song" ? r.pos : -1, row: r.row, step: r.row, pat: r.kind === "song" ? M.state.songs[r.song].order[r.pos] : r.kind === "loop" ? r.pat : -1,
                    dur: r.tick * tickLen(r), rpb: r.rpb, pass: r.pass, n: r.n };
        if (r.alone) r.hist.push(h);                                   /* (the pattern recorder's preview alone: its own) */
        else if (!r.deck) hist.push(h);
        else {                                                         /* a DJ deck's rows; the beats' deck's in hist too */
            r.hist.push(h);
            if (!r.silent && beatDeck() === r.deck) hist.push(h);
        }
        if (r.silent) return true;
        if (r.row % ((r.rpb || 4) * 4) === 0) A().barAt(t, r.deck ? r.deck.i : undefined, r.idp);   /* a bar's start (4 beats): GATE locks to it */
        if (r.kind === "loops") {
            if (lpr.on >= 2 && !r.alone) lprRow(r);
            if (P.loopsRow && !r.alone) P.loopsRow(r.row, t, r.rowDur, r.pass);   /* LOOPS: a take's start and end, the bar's mutes */
            lanesRow(r, t);
        }
        return true;
    }
    /* the global commands (specs/files/commands.md, Commands together): jumps, breaks, tempo, the
     * pattern's loop and delay */
    /* A DJ deck (specs/ui/screens/dj.md#effects): its FX commands for its own effects only (CHORUS, DELAY,
     * REVERB and the LIMITER are DJ's); its punch-ins only while it alone is heard; its counted place
     * (silent) the flow alone */
    const DJ_SKIP = new Set(["CHORUS", "DELAY", "REVERB", "LIMITER"]);
    const FX_KNOB_NAMES = ["GATE", "GATE", "GATE", "GATE", "FILTER", "FILTER", "DRIVE", "DRIVE", "LO-FI", "LO-FI", "LO-FI",
                           "CHORUS", "CHORUS", "CHORUS", "CHORUS", "DELAY", "DELAY", "DELAY", "DELAY", "DELAY",
                           "REVERB", "REVERB", "REVERB", "REVERB", "LIMITER", "LIMITER", "DUCK", "DUCK", "DUCK"];
    const FX_SWITCHED = ["GATE", "FILTER", "DRIVE", "LO-FI", "CHORUS", "DELAY", "REVERB", "LIMITER", "DUCK", "METRONOME"];
    const djSkips = x => DJ_SKIP.has(x.c === 0xAE || x.c === 0xAF ? FX_SWITCHED[x.v] : FX_KNOB_NAMES[x.c - 0x90]);
    function globalCmd(r, ch, x) {
        const v = x.v;
        if (r.silent && !(x.c === 0x0B || x.c === 0x0D || x.c === 0x0F || (x.c === 0x0E && (v >> 4 === 6 || v >> 4 === 0xE)))) return;
        if (x.c === 0x0B && r.kind === "song" && r.plCount < 0) { r.breakPos = v; r.nextRow = 0; }
        else if (x.c === 0x0D && r.plCount < 0) {
            if (r.breakPos < 0) r.breakPos = r.kind === "song" ? r.pos + 1 : 0;
            r.nextRow = v;  /* the row in hex, as the row numbers (commands.md); converters rewrite XM's decimal */
        } else if (x.c === 0x0F) {
            if (timeEdited(r)) return;
            if (!v) { const tm = timing(r.kind, r); r.tempo = tm.tempo; r.speed = tm.ticks; }
            else if (v < 0x20) r.tick = r.speed = v;
            else r.tempo = v;
        } else if (x.c === 0x0E && v >> 4 === 6) {
            const y = v & 15;
            if (!y) ch.plRow = r.row;
            if (ch.plRow < r.row && r.breakPos < 0) {
                if (r.plCount < 0) { r.plCount = y; r.plChan = ch.k; }
                if (r.plChan === ch.k) {
                    if (r.plCount === 0) ch.plRow = r.row + 1;
                    else r.nextRow = ch.plRow;
                    r.plCount--;
                }
            }
        } else if (x.c === 0x0E && v >> 4 === 0xE) r.tick = r.speed + r.speed * (v & 15);
        else if (x.c === 0x10) r.gvol = Math.min(64, v);
        else if (x.c >= 0x90 && x.c <= 0xBF) {
            if (r.deck) { if (!djSkips(x)) A().fxCmd(x.c, v, r.t, r.deck.i); }
            else if (!U().editing("fx", M.fxOfCode(x.c, v))) A().fxCmd(x.c, v, r.t);
        }
        else if (x.c >= 0xC3 && x.c <= 0xD2 && v && (!r.deck || deckAlone(r.deck))) {
            /* a punch-in, as if its key were held v rows: one at a time, a later one replaces it */
            r.punch = { k: x.c - 0xC3, left: v };
            r.punchK = r.punch.k;                       /* the last one started: STOP ends it, its end posted or not */
            A().punchAt(r.punch.k, true, r.t, r.idp);
        }
    }
    /* a track's row: its cell's note, instrument, volume and commands (IBXM's channel_row) */
    function chRow(ch, c, r) {
        const t = r.t;
        ch.cell = c;
        ch.cmds = c ? c.cmds : [];
        ch.retrig++;
        ch.vibAdd = ch.tremAdd = ch.arpAdd = 0;
        ch.fx = 0;
        if (!ch.cmds.some(x => x.c === 0x0E && x.v >> 4 === 0xD && (x.v & 15) > 0)) trigger(ch, r, t);
        for (const x of ch.cmds) rowCmd(ch, x, r, t);
        out(ch, r, t);
    }
    /* LEG (specs/files/commands.md#legato), as the firmware decides it at the row's start: onto the track's held note, of
     * the row's instrument (the track's until this row: the cell names the same or none), on DX7, JUNO-106, CHIP or a
     * sampler. Whether that note still sounds only the worklet knows: it plays a plain note when not (dsp-core.js) */
    const LEGATO_TYPES = ["DX7", "JUNO", "SAMPLER", "CHIP"];
    function legOk(ch, before, c) {
        return !!ch.vid && before != null && ch.vref === before && (c.inst == null || c.inst === before) &&
               LEGATO_TYPES.includes(M.instType(before));
    }
    function trigger(ch, r, t) {
        const c = ch.cell;
        if (!c) return;
        const ins = c.inst, before = ch.inst;
        if (ins != null) {
            if (ins !== ch.inst) A().iclear(r.base + ch.k, t);       /* a new instrument drops the track's parameters */
            ch.inst = ins;
            const i = M.inst(ins);
            ch.vol = i && M.instType(ins) === "SAMPLER" ? Math.min(64, i.p.volume) : 64;
        }
        ch.offset = 0;
        for (const x of ch.cmds) if (x.c === 0x09) { if (x.v) ch.mem.offset = x.v; ch.offset = (ch.mem.offset || 0) * 256; }
        if (c.vol != null) ch.vol = Math.round(c.vol * 64 / 127);
        for (const x of ch.cmds) {
            const y = x.v & 15;
            if (x.c === 0x26) ch.vol = Math.max(0, ch.vol - y);
            else if (x.c === 0x27) ch.vol = Math.min(64, ch.vol + y);
            else if (x.c === 0x28) { if (y) ch.vibSpd = y; }
            else if (x.c === 0x29) { if (y) ch.vibDep = y; vibrato(ch, 5); }
            else if (x.c === 0x2A) ch.pan = y * 17;
            else if (x.c === 0x2D) { if (y) ch.mem.tone = y; }
        }
        const note = c.note;
        if (note === M.OFF) { keyOff(ch, t); return; }
        if (note == null) return;
        const leg = has(ch, 0xD3) && legOk(ch, before, c);
        const porta = leg || ch.cmds.some(x => x.c === 0x03 || x.c === 0x05 || x.c === 0x2D);
        let ft = 0;
        for (const x of ch.cmds) if (x.c === 0x0E && x.v >> 4 === 5) ft = (((x.v & 15) << 4) - 128) >> 1;
        ch.target = note * 64 + ft;
        if (!porta || !ch.vid) {
            ch.pitch = ch.target;
            ch.note = note;
            if (ch.vibType < 4) ch.vibPh = 0;
            if (ch.tremType < 4) ch.tremPh = 0;
            ch.retrig = 0;
            start(ch, r, t);
        } else if (leg) {                               /* a slide to the note that arrives at once */
            ch.pitch = ch.target;
            ch.note = note;
            legato(ch, r, t);
        }
    }
    /* a legato note (LEG): the held voice goes on at ch.note under a new id (no FILTER envelope hit: nothing starts) */
    function legato(ch, r, t) {
        const old = ch.vid, track = r.base + ch.k, vol = Math.round(ampl(ch, r) * 127);
        ch.vid = r.idp + track + "." + (idn++ % 1000);
        noteOn(ch.vid, ch.note, ch.inst, vol, track, t, { song: true, offset: ch.offset, leg: old });
        ch.sent = { bend: 0, vol: vol / 127, pan: ch.sent.pan };
    }
    /* the voice for the track's note */
    function start(ch, r, t) {
        const ref = ch.inst;
        if (ref == null || !M.inst(ref)) return;
        const track = r.base + ch.k, vol = Math.round(ampl(ch, r) * 127);
        const acc = has(ch, 0xC0) || has(ch, 0xC2), sld = has(ch, 0xC1) || has(ch, 0xC2);
        A().fire(track, t, r.idp);                      /* (FILTER's envelope: the track's note, heard or not) */
        if (M.instType(ref) === "TB303") {
            const id = r.idp + track + "tb";
            if (ch.vid && ch.vid !== id) noteOff(ch.vid, t);
            noteOn(id, ch.note, ref, vol, track, t, { song: true, accent: acc, glide: ch.slide && ch.live === id });
            ch.vid = ch.live = id;
            ch.slide = sld;
            if (!sld) A().noteOff(id, t + r.rowDur * .5);
        } else {
            if (ch.vid) noteOff(ch.vid, t);
            ch.vid = r.idp + track + "." + (idn++ % 1000);
            ch.vref = ref;                              /* (its instrument: LEG's rule) */
            ch.slide = false;
            noteOn(ch.vid, ch.note, ref, vol, track, t, { song: true, offset: ch.offset, accent: acc });
        }
        ch.sent = { bend: 0, vol: vol / 127, pan: ch.sent.pan };
    }
    function keyOff(ch, t) {
        if (ch.vid) noteOff(ch.vid, t);
        ch.vid = null;
        ch.live = null;
    }
    function ampl(ch, r) { return clamp(ch.vol + ch.tremAdd, 0, 64) / 64 * r.gvol / 64; }
    /* what the voice gets each tick: its bend (semitones from its note), its level, the track's pan */
    function out(ch, r, t) {
        if (ch.vid) {
            let pitch = ch.pitch;
            /* glissando (E3x on): a slide to the note is heard in semitones, on the note's own finetune */
            if (ch.gliss && ch.cmds.some(x => x.c === 0x03 || x.c === 0x05 || x.c === 0x2D)) {
                const off = ((ch.target % 64) + 64) % 64;
                pitch = Math.round((pitch - off) / 64) * 64 + off;
            }
            const bend = (pitch + ch.vibAdd) / 64 + ch.arpAdd - ch.note, vol = ampl(ch, r);
            if (Math.abs(bend - ch.sent.bend) > 1e-3 || Math.abs(vol - ch.sent.vol) > 1e-3) {
                ctl(ch.vid, t, bend, vol);
                ch.sent.bend = bend; ch.sent.vol = vol;
            }
        }
        const pan = clamp(ch.pan + ch.panAdd, 0, 255);
        if (pan !== ch.sent.pan) { A().trackMix(r.base + ch.k, "pan", pan, t); ch.sent.pan = pan; }
    }
    function wave(ch, ph, type) {
        switch (type & 3) {
            case 1: return 255 - (((ph + 0x20) & 0x3F) << 3);
            case 2: return (ph & 0x20) ? 255 : -255;
            case 3: { const a = (ch.seed >> 20) - 255; ch.seed = (ch.seed * 65 + 17) & 0x1FFFFFFF; return a; }
            default: { const a = SINE[ph & 0x1F]; return ph & 0x20 ? -a : a; }
        }
    }
    function vibrato(ch, shift) { ch.vibAdd = -(wave(ch, ch.vibPh, ch.vibType) * ch.vibDep >> shift); }
    function tremolo(ch) { ch.tremAdd = wave(ch, ch.tremPh, ch.tremType) * ch.tremDep >> 6; }
    function volSlide(ch, r) {
        const p = ch.mem.vs || 0, up = p >> 4, down = p & 15;
        if (down === 15 && up) { if (!ch.fx) ch.vol += up; }
        else if (up === 15 && down) { if (!ch.fx) ch.vol -= down; }
        else if (ch.fx || (r.kind === "song" && M.state.songs[r.song].fastVol)) ch.vol += up - down;
        ch.vol = clamp(ch.vol, 0, 64);
    }
    function portaUp(ch, p) {
        if ((p & 0xF0) === 0xE0) { if (!ch.fx) ch.pitch += p & 15; }
        else if ((p & 0xF0) === 0xF0) { if (!ch.fx) ch.pitch += (p & 15) << 2; }
        else if (ch.fx) ch.pitch += p << 2;
        ch.pitch = Math.min(ch.pitch, 127 * 64 + 63);
    }
    function portaDown(ch, p) {
        if ((p & 0xF0) === 0xE0) { if (!ch.fx) ch.pitch -= p & 15; }
        else if ((p & 0xF0) === 0xF0) { if (!ch.fx) ch.pitch -= (p & 15) << 2; }
        else if (ch.fx) ch.pitch -= p << 2;
        ch.pitch = Math.max(ch.pitch, 0);
    }
    function tonePorta(ch) {
        const s = (ch.mem.tone || 0) << 2;
        if (ch.pitch < ch.target) ch.pitch = Math.min(ch.target, ch.pitch + s);
        else ch.pitch = Math.max(ch.target, ch.pitch - s);
    }
    function retrigVol(ch, r) {
        if (ch.retrig < (ch.mem.rt || 0)) return;
        ch.retrig = 0;
        const v = ch.vol;
        ch.vol = [v, v - 1, v - 2, v - 4, v - 8, v - 16, v * 2 / 3 | 0, v >> 1, v, v + 1, v + 2, v + 4, v + 8, v + 16, v * 3 / 2 | 0, v << 1][ch.mem.rv || 0];
        ch.vol = clamp(ch.vol, 0, 64);
        start(ch, r, r.t);
    }
    function tremor(ch) {
        if (ch.retrig >= (ch.mem.ton || 0)) ch.tremAdd = -64;
        if (ch.retrig >= (ch.mem.ton || 0) + (ch.mem.toff || 0)) ch.tremAdd = ch.retrig = 0;
    }
    /* a command on its row (tick 0) */
    function rowCmd(ch, x, r, t) {
        const v = x.v, hi = v >> 4, lo = v & 15, m = ch.mem;
        switch (x.c) {
            case 0x00: if (v) m.arp = v; break;
            case 0x01: if (v) m.up = v; portaUp(ch, m.up || 0); break;
            case 0x02: if (v) m.down = v; portaDown(ch, m.down || 0); break;
            case 0x03: if (v) m.tone = v; break;
            case 0x04: if (hi) ch.vibSpd = hi; if (lo) ch.vibDep = lo; vibrato(ch, 5); break;
            case 0x05: case 0x06: if (v) m.vs = v; if (x.c === 0x06) vibrato(ch, 5); volSlide(ch, r); break;
            case 0x07: if (hi) ch.tremSpd = hi; if (lo) ch.tremDep = lo; tremolo(ch); break;
            case 0x08: ch.pan = v; break;
            case 0x0A: if (v) m.vs = v; volSlide(ch, r); break;
            case 0x0C: ch.vol = Math.min(64, v); break;
            case 0x0E:
                if (hi === 1) { if (lo) m.fu = lo; portaUp(ch, 0xF0 | (m.fu || 0)); }
                else if (hi === 2) { if (lo) m.fd = lo; portaDown(ch, 0xF0 | (m.fd || 0)); }
                else if (hi === 3) ch.gliss = lo !== 0;                 /* an extension to IBXM (commands.md) */
                else if (hi === 4) { if (lo < 8) ch.vibType = lo; }
                else if (hi === 7) { if (lo < 8) ch.tremType = lo; }
                else if (hi === 0xA) { if (lo) m.fvu = lo; ch.vol = Math.min(64, ch.vol + (m.fvu || 0)); }
                else if (hi === 0xB) { if (lo) m.fvd = lo; ch.vol = Math.max(0, ch.vol - (m.fvd || 0)); }
                else if (hi === 0xC && !lo) cut(ch, t);
                break;
            case 0x11: if (v) m.gs = v; break;
            case 0x14: if (!v) keyOff(ch, t); break;
            case 0x19: if (v) m.ps = v; break;
            case 0x1B: if (hi) m.rv = hi; if (lo) m.rt = lo; retrigVol(ch, r); break;
            case 0x1D: if (hi) m.ton = hi; if (lo) m.toff = lo; tremor(ch); break;
            case 0x1E: if (hi) ch.vibSpd = hi; if (lo) ch.vibDep = lo; vibrato(ch, 7); break;
            case 0x21: if (v) m.xf = v; if ((m.xf >> 4) === 1) portaUp(ch, 0xE0 | (m.xf & 15)); else if ((m.xf >> 4) === 2) portaDown(ch, 0xE0 | (m.xf & 15)); break;
            case 0x22: if (hi) ch.panbSpd = hi; if (lo) ch.panbDep = lo; break;
            default:
                /* skipped while what they change is being edited (U.editing) */
                if (x.c >= 0x40 && x.c <= 0x7F) { if (!U().editing("inst", ch.inst)) A().iparam(r.base + ch.k, x.c, v, t); }
                else if (x.c >= 0x80 && x.c <= 0x84 && !U().editing("mix", r.base + ch.k))
                    A().trackMix(r.base + ch.k, ["lvl", "cho", "dly", "rev", "dck"][x.c - 0x80], v, t);
        }
    }
    /* a command on the row's later ticks (IBXM's channel_tick) */
    function chTick(ch, r) {
        const t = r.t, m = ch.mem;
        ch.vibAdd = 0;
        ch.fx++;
        ch.retrig++;
        const delayed = ch.cmds.some(x => x.c === 0x0E && x.v >> 4 === 0xD && ch.fx <= (x.v & 15));
        for (const x of ch.cmds) {
            const v = x.v, hi = v >> 4, lo = v & 15;
            if (!delayed) {
                if (x.c === 0x24) ch.vol = Math.max(0, ch.vol - lo);
                else if (x.c === 0x25) ch.vol = Math.min(64, ch.vol + lo);
                else if (x.c === 0x29) { ch.vibPh += ch.vibSpd; vibrato(ch, 5); }
                else if (x.c === 0x2B) ch.pan = Math.max(0, ch.pan - lo);
                else if (x.c === 0x2C) ch.pan = Math.min(255, ch.pan + lo);
                else if (x.c === 0x2D) tonePorta(ch);
            }
            switch (x.c) {
                case 0x00:
                    if (!m.arp) break;
                    if (ch.fx === 1) ch.arpAdd = m.arp >> 4;
                    else if (ch.fx === 2) ch.arpAdd = m.arp & 15;
                    else ch.arpAdd = ch.fx = 0;
                    break;
                case 0x01: portaUp(ch, m.up || 0); break;
                case 0x02: portaDown(ch, m.down || 0); break;
                case 0x03: tonePorta(ch); break;
                case 0x04: ch.vibPh += ch.vibSpd; vibrato(ch, 5); break;
                case 0x05: tonePorta(ch); volSlide(ch, r); break;
                case 0x06: ch.vibPh += ch.vibSpd; vibrato(ch, 5); volSlide(ch, r); break;
                case 0x07: ch.tremPh += ch.tremSpd; tremolo(ch); break;
                case 0x0A: volSlide(ch, r); break;
                case 0x0E:
                    if (hi === 9 && lo && ch.fx >= lo) { ch.fx = 0; start(ch, r, t); }
                    else if (hi === 0xC && lo === ch.fx) cut(ch, t);
                    else if (hi === 0xD && lo === ch.fx) trigger(ch, r, t);
                    break;
                case 0x11: r.gvol = clamp(r.gvol + ((m.gs || 0) >> 4) - ((m.gs || 0) & 15), 0, 64); break;
                case 0x14: if (v && v === ch.fx) keyOff(ch, t); break;
                case 0x19: ch.pan = clamp(ch.pan + ((m.ps || 0) >> 4) - ((m.ps || 0) & 15), 0, 255); break;
                case 0x1B: retrigVol(ch, r); break;
                case 0x1D: tremor(ch); break;
                case 0x1E: ch.vibPh += ch.vibSpd; vibrato(ch, 7); break;
                case 0x22: ch.panbPh += ch.panbSpd; ch.panAdd = wave(ch, ch.panbPh, 0) * ch.panbDep >> 5; break;
            }
        }
        out(ch, r, t);
    }
    /* note cut: silent at once, and the voice let go */
    function cut(ch, t) {
        ch.vol = 0;
        if (ch.vid) { ctl(ch.vid, t, ch.sent.bend, 0); ch.sent.vol = 0; noteOff(ch.vid, t + .004); ch.vid = null; }
    }
    function releaseAll(r, t) {
        if (!r) return;
        if (r.kind === "loops") { for (let i = r.alone ? 16 : 0; i <= (r.alone ? 16 : KEYS_LN); i++) lanesEnd(i, t); if (!r.alone) lnReq = []; }
        else for (const ch of r.ch) keyOff(ch, t);
    }

    /* ------------------------------------------------------------- LOOPS' lanes --- */
    /* LOOPS (specs/ui/screens/loops.md; the loop station proposal, section 13): the loops' replay only keeps the time
     * (TEMPO, TICKS A ROW, SWING, its rows and ticks), and each loop's lanes play its notes the way the keys play
     * theirs, a voice each, marked as the loop's track's (16 + its place), so the loop's mix, mute and solo act on
     * them. Each row a loop finds its row (within its base while its repeats last; past them it rests) and its lanes'
     * cells there: a note (after its EDx ticks) ends its lane's note before it, which rings on; a note off, or a key
     * off at its tick (Kxx), ends it; a TB-303 lane has its gate of half a row and SLD's slide, elsewhere 3xx glides
     * the lane's voice to the note (xx/16 semitone a tick) without a new start. A loop going into its rest, or a
     * repeat cut by the length's end, ends its notes. Loop 16 is the pattern recorder's preview (P.lpPreview: a loop
     * as the recorder would write it, its track given); 17 the keys while RECORD LIVE records (no stored notes: each
     * note asked for, naming its instrument, on the keys' own track). The screen says the rows (P.lpRows), the take's
     * loop and pass whose stored notes are not played (P.lpTake), and asks for notes heard as written (P.lpNote): at a
     * row of a pass, or at once, "as close to the line as it can"; RECORD LIVE's keys at a row of the replay's count
     * (P.lpNoteAt). RECORD LIVE's rest (P.lpRest): the replay runs on while the loops are silent */
    const KEYS_LN = 17;
    const newLanes = () => Array.from({ length: 4 }, () => ({ vid: null, ref: null, note: 60, bend: 0, slide: false, pend: null, koff: -1, glide: null }));
    let LN = Array.from({ length: 18 }, newLanes);
    let lnReq = [];                                 /* notes asked for at a row not scheduled yet: {i, l, c, pass, row, key} */
    const cmdOf = (c, code, sub) => { const x = c.cmds.find(y => y.c === code && (sub == null || y.v >> 4 === sub)); return x ? x.v : null; };
    const lpOf = i => i < 16 ? M.state.loops.list[i] || null : i === 16 && P.lpPreview ? P.lpPreview() : null;
    /* a lane's voice let go (its next note, a note off, a key off); laneEnd also drops a note waiting for its tick */
    function laneOff(ln, t) { if (ln.vid) noteOff(ln.vid, t); ln.vid = null; ln.glide = null; ln.slide = false; ln.koff = -1; }
    function laneEnd(ln, t) { laneOff(ln, t); ln.pend = null; }
    function lanesEnd(i, t) { for (const ln of LN[i]) laneEnd(ln, t); }
    /* lane l of loop i starts the note of cell c at t (rowDur: a TB-303's gate; pfx its replay's ids' letter; key: the
     * key it came from, whose light follows it) */
    function laneNote(i, l, c, t, rowDur, pfx, key) {
        const L = lpOf(i), ln = LN[i][l], ref = i === KEYS_LN ? c.inst : L && L.inst;
        if (ref == null || !M.inst(ref)) return;
        const track = i < 16 ? 16 + i : i === KEYS_LN ? -1 : 16 + L.track, vol = c.vol == null ? 127 : c.vol;
        const acc = c.cmds.some(x => x.c === 0xC0 || x.c === 0xC2), sld = c.cmds.some(x => x.c === 0xC1 || x.c === 0xC2), porta = cmdOf(c, 0x03);
        A().fire(track, t, pfx);                       /* (FILTER's envelope: the track's note) */
        ln.koff = -1;
        if (M.instType(ref) === "TB303") {
            const id = pfx + track + "." + (i < 16 ? "" : "p") + l + "tb";     /* (the preview's apart from its loop's) */
            if (ln.vid && ln.vid !== id) noteOff(ln.vid, t);
            noteOn(id, c.note, ref, vol, track, t, { song: true, accent: acc, glide: ln.slide && ln.vid === id });
            Object.assign(ln, { vid: id, ref, note: c.note, bend: 0, slide: sld, glide: null });
            if (!sld) A().noteOff(id, t + rowDur * .5);
            return;
        }
        if (c.cmds.some(x => x.c === 0xD3) && ln.vid && ln.ref === ref && LEGATO_TYPES.includes(M.instType(ref))) {
            const old = ln.vid;                         /* LEG: its voice on at the note (the worklet: if it sounds) */
            ln.vid = pfx + track + "." + l + "." + (idn++ % 1000) + (key != null ? "k" + key : "");
            Object.assign(ln, { note: c.note, bend: 0, glide: null });
            noteOn(ln.vid, c.note, ref, vol, track, t, { song: true, leg: old });
            return;
        }
        if (porta != null && ln.vid && ln.ref === ref) {
            ln.glide = { to: c.note - ln.note, step: (porta || 0x20) / 16, vol: vol / 127 };
            return;
        }
        if (ln.vid) noteOff(ln.vid, t);
        ln.vid = pfx + track + "." + l + "." + (idn++ % 1000) + (key != null ? "k" + key : "");
        Object.assign(ln, { ref, note: c.note, bend: 0, slide: false, glide: null });
        noteOn(ln.vid, c.note, ref, vol, track, t, { song: true, accent: acc });
    }
    /* a cell on lane l of loop i at its row's start t: its key off (before a delayed note: the note before it; after it:
     * that note's own end, REAL TIME's key let go in its row), note off, note (r: the replay, for EDx's ticks; none: at
     * once, its delay heard as it was played) */
    function laneCell(i, l, c, t, rowDur, pfx, key, r) {
        const ln = LN[i][l], note = c.note != null && c.note !== M.OFF;
        const ed = note && r ? cmdOf(c, 0x0E, 0xD) : null, d = ed == null ? 0 : ed & 15, k = cmdOf(c, 0x14);
        if (k != null && (!note || k < d)) { if (k === 0 || !r) laneOff(ln, t); else ln.koff = k; }
        if (c.note === M.OFF) laneOff(ln, t);
        if (!note) return;
        const own = k != null && k > d && r ? k : -1;
        if (d > 0) ln.pend = { c, at: d, key, koff: own };
        else { laneNote(i, l, c, t, rowDur, pfx, key); ln.koff = own; }
    }
    function lanesRow(r, t) {
        const j = M.state.loops, take = P.lpTake ? P.lpTake() : null, rows = rowsOf(r);
        for (let i = r.alone ? 16 : 0; i <= 16; i++) {
            const L = lpOf(i);
            if (!L || !L.base || (i < 16 && lpr.on === 1)) continue;     /* (RECORD LIVE's rest: the loops silent) */
            for (const ln of LN[i]) ln.pend = null;               /* (a delay past the row's last tick never plays) */
            if (i < 16 && take && take.loop === i && take.pass === r.pass && take.run === r.id) continue;   /* the take: its notes come asked for */
            if (r.row === 0 && r.pass > 0 && rows % L.base && Math.floor((rows - 1) / L.base) < L.repeats) lanesEnd(i, t);   /* cut at the end */
            const pass = Math.floor(r.row / L.base), br = r.row % L.base;
            if (pass >= L.repeats) { if (pass === L.repeats && !br) lanesEnd(i, t); continue; }      /* its rest */
            for (let l = 0; l < 4; l++) { const c = L.cells[br * 4 + l]; if (c) laneCell(i, l, c, t, r.rowDur, r.idp, null, r); }
        }
        if (r.alone) return;
        for (const ln of LN[KEYS_LN]) ln.pend = null;
        if (!lnReq.length) return;
        const due = lnReq.filter(q => q.n != null ? q.n === r.n : q.pass === r.pass && q.row === r.row);
        lnReq = lnReq.filter(q => q.n != null ? q.n > r.n : q.pass > r.pass || (q.pass === r.pass && q.row > r.row));   /* (the rest
                                                         * wait; passed ones go) */
        for (const q of due) laneCell(q.i, q.l, q.c, t, r.rowDur, r.idp, q.key, r);
    }
    /* RECORD LIVE's rest (specs/ui/screens/loops.md#record-live): on 1 the loops silent while their replay runs on, their
     * place kr; 2 a jump queued (on from their place, or their start: kr), landing at the next row; 3 silent from the
     * next row (zero: their place then their start; kr0, their place given: a silent start). pend: the next start's
     * on (a silent start's 3, its first row silent). Each switch counted (sw) with the row count it took effect at (swN)
     * and the loops' row from there (swRow, -1 silent) */
    const lpr = { on: 0, kr: 0, kr0: -1, zero: false, pend: 0, sw: 0, swRow: -1, swN: 0 };
    /* its switch at row r.row of the replay (3: the loops silent from here, their voices let go; 2: a jump landed, the
     * loops on from here) */
    function lprRow(r) {
        if (lpr.on === 3) {
            for (let i = 0; i < 16; i++) lanesEnd(i, r.t);
            lpr.kr = lpr.zero ? 0 : lpr.kr0 >= 0 ? lpr.kr0 : r.row;
            lpr.kr0 = -1;
            lpr.on = 1;
            lpr.swRow = -1;
        } else {
            lpr.on = 0;
            lpr.swRow = r.row;
        }
        lpr.zero = false;
        lpr.swN = r.n;
        lpr.sw++;
    }
    /* 1: the loops silent from the next row, their place kept (a replay just started: from its first row); 0: on again
     * from their place, on the next row; 2: to their start (playing: from the top on the next row; silent or paused:
     * waiting there). A pause or another start ends the rest (paused: at the loops' place) */
    P.lpRest = function (how) {
        if (P.what !== "loops" || pvR) return;
        lpr.kr0 = -1;
        if (!P.playing || !R || R.kind !== "loops") { if (how === 2) kept.loops.row = 0; return; }
        if (how === 1) { if (lpr.on !== 1) lpr.on = 3; return; }     /* (a jump queued lands first, and is silent from there) */
        if (how === 0 && lpr.on === 3) { lpr.on = 0; return; }      /* (silence not begun: called off) */
        if (how === 2 && lpr.on === 3) { lpr.zero = true; return; }
        if (how === 2 && lpr.on === 1) { lpr.kr = 0; return; }
        if (how === 0 && lpr.on !== 1) return;                       /* (playing, or on its way: nothing to do) */
        if (how === 2) lpr.kr = 0;
        R.nextRow = lpr.kr;
        lpr.on = 2;
    };
    P.lpRestState = () => ({ on: lpr.on, kr: lpr.kr, sw: lpr.sw, swRow: lpr.swRow, swN: lpr.swN });
    /* the rows the loops' replay has played (0: none yet, or not the loops') */
    P.lpPlayed = () => R && R.kind === "loops" ? R.n : 0;
    /* a note (or a note off) asked for on lane l of loop i (RECORD LIVE's keys: P.LP_KEYS) at the replay's row n (its
     * count: scheduled already, at that row's time, or at once if it has begun), or at once (n < 0) */
    P.lpNoteAt = function (i, l, c, n, key) {
        if (!R || R.kind !== "loops") return;
        const at = n < 0 ? null : hist.find(x => x.n === n);
        if (n >= 0 && !at) { lnReq.push({ i, l, c, n, key }); return; }
        const t = at && at.t > now() ? at.t : now() + .005;
        laneCell(i, l, c, t, at ? at.dur : P.rowSeconds(), R.idp, key, null);
    };
    function lanesTick(r) {
        const k = r.speed - r.tick, t = r.t;
        for (let i = r.alone ? 16 : 0; i <= (r.alone ? 16 : KEYS_LN); i++) LN[i].forEach((ln, l) => {
            if (ln.pend && ln.pend.at === k) { const p = ln.pend; ln.pend = null; laneNote(i, l, p.c, t, r.rowDur, r.idp, p.key); ln.koff = p.koff; }
            if (ln.koff === k) laneOff(ln, t);
            if (ln.glide && ln.vid) {
                const g = ln.glide;
                ln.bend = ln.bend < g.to ? Math.min(g.to, ln.bend + g.step) : Math.max(g.to, ln.bend - g.step);
                ctl(ln.vid, t, ln.bend, g.vol);
                if (ln.bend === g.to) ln.glide = null;
            }
        });
    }
    /* a note (or a note off, a key off) asked for on lane l of loop i: at row `row` of pass `pass` as the loops play
     * there (scheduled already: at that row's time, or at once if it has begun), or at once (row < 0) */
    P.lpNote = function (i, l, c, pass, row, key) {
        if (!R || R.kind !== "loops") return;
        const at = row < 0 ? null : hist.find(x => x.pass === pass && x.row === row);
        if (row >= 0 && !at) { lnReq.push({ i, l, c, pass, row, key }); return; }
        const t = at && at.t > now() ? at.t : now() + .005;
        laneCell(i, l, c, t, at ? at.dur : P.rowSeconds(), R.idp, key, null);
    };
    /* the loops' replay playing now (its id: a take's pass is counted in it), 0 none */
    P.lpRun = () => R && R.kind === "loops" ? R.id : 0;
    /* whether the loops' replay has scheduled row `row` of pass `pass` already (it runs a little ahead of what is heard) */
    P.lpScheduled = (pass, row) => !!R && R.kind === "loops" && hist.some(x => x.pass === pass && x.row === row);
    /* a TB-303 lane's note now slides into the next (a MONO take's legato: SLD written on the note held) */
    P.lpSlide = (i, l) => { LN[i][l].slide = true; };
    P.lpEnd = (i, t) => lanesEnd(i, t == null ? now() : t);
    P.LP_KEYS = KEYS_LN;                                /* (the keys' lanes while RECORD LIVE records) */
    /* loop i peeled: its notes end, the lanes of the loops after it move up with them (16, the preview's, stays) */
    P.lpPeeled = function (i) {
        lanesEnd(i, now());
        LN.splice(i, 1);
        LN.splice(15, 0, newLanes());                   /* (16 and 17 stay) */
        lnReq = lnReq.filter(q => q.i !== i).map(q => Object.assign(q, { i: q.i > i ? q.i - 1 : q.i }));
    };
    P.lpCleared = () => { for (let i = 0; i < 16; i++) lanesEnd(i, now()); lnReq = []; };
    /* the preview written as loop i: its lanes go on as that loop's, without a break */
    P.lpTakeLanes = function (i) { lanesEnd(i, now()); LN[i] = LN[16]; LN[16] = newLanes(); };
    /* the pattern recorder's preview alone (the loops stopped): a replay of its own at LOOPS' tempo, from `row` at t,
     * the loops silent and their place kept; its notes' ids "v" */
    let pvR = null;
    P.pvStart = function (row, t) {
        P.pvStop();
        pvR = newReplay("loops", { row: row || 0, alone: true, idp: "v" });
        pvR.hist = [];
        if (t) pvR.t = t;
    };
    P.pvStop = function () {
        if (!pvR) return;
        A().flush("v", now());
        lanesEnd(16, now());
        pvR = null;
    };
    P.pvOn = () => !!pvR;
    P.pvAt = () => { if (!pvR) return null; const t = now(); let h = null; for (const x of pvR.hist) if (x.t <= t) h = x; return h; };
    /* where the preview alone goes on: the row after the one heard, and its time (REC writing on from it) */
    P.pvNext = () => { const h = P.pvAt(); return h ? { row: h.row + 1, t: h.t + h.dur } : null; };
    /* the same for the loops: the row after the one heard and its time (PLAY pausing them, the preview going on) */
    P.lpNext = () => { const h = P.playing && R && R.kind === "loops" ? P.at() : null; return h ? { row: h.row + 1, t: h.t + h.dur, pass: h.pass } : null; };

    /* ----------------------------------------------------------- the schedule --- */
    /* PLAYBACK's K1 landed (its row over): every note of the song cut there, the effects' tails emptied, and the
     * song started afresh at that position's first row, at the landing's time (the beat goes on) */
    function restartClean(r) {
        const at = Math.max(r.t, now() + .02), pos = r.restartAt;
        A().cutAt(r.idp, at);
        A().fxEmptyAt(at);
        A().fxReset();
        A().songFx(M.state.songs[r.song]);
        kept.song[r.song] = { pos, row: 0 };
        R = newReplay("song", { song: r.song, pos, row: 0, breakPos: pos });
        R.t = at;
        metroStart();
    }
    function tick() {
        const t = now();
        if (R && P.playing && R.restartAt != null && t >= R.t - .03) restartClean(R);   /* (its effects' messages on time) */
        if (R && P.playing && R.restartAt == null) {
            if (R.t < t - .25) R.t = t + .02;                   /* the page was away: start again from now */
            let guard = 256;
            while (P.playing && R && !R.done && R.t < t + AHEAD && guard--) if (!replayTick(R) && !R.done) { if (R.restartAt == null) stop(); break; }
            if (R && R.done && R.kind === "song" && t >= R.endT - CHAIN_EARLY && chainNext(R)) { /* the next song plays on */ }
            else if (R && R.done && t >= R.endT) {
                const r = R;
                if (r.kind === "song") { stop(); kept.song[r.song] = { pos: 0, row: 0 }; }
                else stop();
                if (r.onEnd) r.onEnd();
            }
        }
        djTick(t);
        if (pvR) {                                          /* the pattern recorder's preview alone */
            if (pvR.t < t - .25) pvR.t = t + .02;
            let guard = 256;
            while (pvR && pvR.t < t + AHEAD && guard--) if (!replayTick(pvR)) P.pvStop();
            if (pvR) while (pvR.hist.length > 2 && pvR.hist[1].t <= t) pvR.hist.shift();
        }
        while (hist.length > 2 && hist[1].t <= t) hist.shift();
        if (P.lpPoll) P.lpPoll();                           /* (RECORD LIVE goes on on every screen) */
        for (const id in fusionRuns) runFusion(fusionRuns[id], t + AHEAD);    /* the FUSION notes' tick commands */
        tickPreview(t);
        tickMetro(t);
        tickPulse();
    }
    setInterval(tick, 20);
    /* SYNC PULSE (specs/connections.md#clock): a click at every half beat of what plays, from its rows' times */
    function tickPulse() {
        if (!P.playing || !A().pulseSideNow || A().pulseSideNow() <= 0) return;   /* (a page's audio without it: none) */
        for (const h of hist) {
            if (h.pulsed) continue;
            h.pulsed = true;
            const rpb = h.rpb || 4, m = h.row % rpb, odd = rpb % 2;
            if (m === 0) A().pulse(h.t);
            if (m === (rpb - odd) / 2) A().pulse(odd ? h.t + h.dur / 2 : h.t);   /* (the half beat: mid-row if ROWS A BEAT is odd) */
        }
    }

    /* where playback is now: the row being heard (tm: at that time instead, a moment ago) */
    P.at = function (tm) {
        const t = tm == null ? now() : tm;
        let h = null;
        for (const x of hist) if (x.t <= t) h = x;
        return h;
    };
    P.rowFrac = function () {
        const h = P.at();
        return h ? (now() - h.t) / h.dur : 0;
    };
    /* the row a key pressed now lands on: the nearest (specs/ui/screens/editor.md) */
    P.nearestRow = function () {
        const h = P.at();
        if (!h) return null;
        const late = P.rowFrac() >= .5;
        return { row: late ? h.row + 1 : h.row, pat: h.pat, step: late ? h.step + 1 : h.step };
    };
    P.onBeat = function () {
        const h = P.at();
        return h && h.row % (h.rpb || 4) === 0 && P.rowFrac() < .6;
    };
    /* the musical tempo of what plays, else of the current song (specs/files/commands.md, Time); the decks':
     * the main's (else the right's) */
    P.tempo = () => P.what === "dj" && djBeat() ? djBpm(djBeat()) : R ? R.tempo * songSpeed(R) * 24 / (R.speed * R.rpb) : M.bpm(held());
    P.rowSeconds = () => P.what === "dj" && djBeat() ? 60 / (djBpm(djBeat()) * dsong(djBeat()).rpb)
                       : R ? R.speed * 2.5 / (R.tempo * songSpeed(R)) : held().ticks * 2.5 / held().tempo;

    /* --------------------------------------------------------------- the transport --- */
    function start_(kind, s) {
        djOff();                                    /* the decks pause: a song, a pattern or the loops start */
        stop(true);
        if (kind !== "song") { speed = 1; vol = 0; P.endMode = "own"; }   /* (something else plays: PLAYBACK's speed and
                                                         * volume back, SONGS' play mode out of hold) */
        R = newReplay(kind, s);
        hist = [];
        lpr.on = kind === "loops" ? lpr.pend : 0;   /* (RECORD LIVE's rest: a silent start's from its first row) */
        lpr.pend = 0;
        P.what = kind;
        songGain();
        P.playing = true;
        P.atStart = false;
        A().fxReset();
        A().songFx(kind === "loops" ? M.state.loops : kind === "song" ? M.state.songs[s.song] : song());
        metroStart();
    }
    function stop(quiet) {
        if (R) {
            const h = P.at();
            if (R.kind === "song" && !R.done) kept.song[R.song] = h ? { pos: h.pos, row: h.row } : { pos: R.pos, row: Math.max(0, R.nextRow) };
            else if (R.kind === "loop") kept.loop = { pat: R.pat, row: h ? h.row : 0 };
            else if (R.kind === "loops") {                /* (RECORD LIVE's rest: the loops' place is theirs, not the
                                                           * replay's; a jump queued: where it went) */
                const n = rowsOf(R);
                kept.loops = { row: lpr.on === 1 || lpr.on === 2 ? lpr.kr : h && n ? h.row % n : 0 };
            }
        }
        lpr.on = 0;
        A().flush("s", now());
        A().flush("j", now());
        if (R && R.punchK != null) A().punch(R.punchK, false);  /* STOP ends a punch-in command */
        releaseAll(R, now());
        R = null;
        P.playing = false;
        if (!quiet) U().pausedHook();
        return false;
    }
    P.pause = () => {
        if (P.what === "dj") { if (P.playing) djPauseAll(); return; }   /* the decks that play, kept to be resumed */
        if (P.playing) { stop(); metroStop(); }
    };
    P.pauseAll = () => { P.pause(); P.take = null; };
    /* switching songs rewinds (specs/ui/transport.md): another song starts from its beginning, and the
     * one left goes back to its start; the same song goes on from its place. from: a position picked to
     * start at (SONGS' ALGORITHM), its first row */
    P.playSong = function (i, from) {
        const other = kept.lastSong != null && kept.lastSong !== i;
        const k = from != null ? { pos: from, row: 0 } : other ? { pos: 0, row: 0 } : kept.song[i] || { pos: 0, row: 0 };
        kept.lastSong = i;
        start_("song", { song: i, pos: k.pos, row: k.row, breakPos: k.pos });
        if (other) kept.song = {};                          /* (start_ kept the one left's place: gone) */
    };
    /* SONGS' play modes (specs/ui/screens/songs.md#the-play-modes): SEQ there turns P.playMode, 0 ONE PLAY, 1 REPEAT
     * SONG, 2 PLAY NEXT; PLAY there puts it in hold (P.endMode "once", "repeat", "next"), on every screen, until
     * anything but a song starts ("own": a song ends as its AT THE END says). A looping song loops in every mode; the
     * mode decides only a song that stops: ONE PLAY stops it, REPEAT SONG again. PLAY NEXT: at its end the next song in
     * the list with positions, from its start, on the old one's end, as if its first row came next (switched a moment
     * before, as the replay runs ahead); after the last song, it stops. Not stored: performance state */
    P.playMode = 0;
    P.endMode = "own";
    P.holdMode = () => { P.endMode = ["once", "repeat", "next"][P.playMode]; };
    const CHAIN_EARLY = .03;                             /* (more than a tick of the schedule: the first row on time) */
    function chainNext(r) {
        if (P.endMode !== "next") return false;
        const all = M.state.songs;
        let n = r.song + 1;
        while (n < all.length && !(all[n] && all[n].order.length)) n++;
        if (n >= all.length) return false;
        releaseAll(r, r.endT);                           /* its last notes end at its end, as at a STOP */
        kept.song = {};
        kept.lastSong = n;
        R = newReplay("song", { song: n, pos: 0, row: 0, breakPos: 0 });
        R.t = r.endT;
        A().fxReset();
        A().songFx(all[n]);
        metroStart();
        const sg = U().screens.SONGS;                    /* (the cursor follows it, on any screen) */
        if (sg && sg.chained) sg.chained(n);
        return true;
    }
    P.playLoop = function (pat, row, opts = {}) {
        const r = row != null ? row : kept.loop.pat === pat ? kept.loop.row : 0;
        start_("loop", Object.assign({ pat, row: r }, opts));
    };
    /* LOOPS: from where they paused (kept.loops.row), or from `row` (0: their start, a take's); t0: when (a count-in's
     * end, the preview's next row), else now */
    P.playLoops = (row, t0) => {
        const n = rowsOf({ kind: "loops" });
        if (row != null) kept.loops.row = row;
        start_("loops", { row: n ? kept.loops.row % n : 0 });
        if (t0) R.t = Math.max(t0, now());
    };
    /* RECORD LIVE: the loops' replay from row 0, the loops silent from its first row (the rest's 1, their place
     * `place`), so no row of theirs sounds first */
    P.playLoopsSilent = place => {
        lpr.pend = 3;
        lpr.zero = false;
        lpr.kr0 = place;
        P.playLoops(0);
    };
    /* the length changed while they play (the first loop closed): a row past the new end goes on as the next pass's */
    P.lpLength = n => { if (R && R.kind === "loops" && n && R.nextRow >= n) R.nextRow %= n; };
    P.playingSong = () => R && R.kind === "song" ? R.song : P.what === "dj" && P.playing && djBeat() ? djBeat().song : -1;
    /* a song moved in the list (SONGS, SEL, MOVE): the indexes the player holds follow it */
    P.songMoved = function (from, to) {
        const f = i => i === from ? to : from < to && i > from && i <= to ? i - 1 : from > to && i >= to && i < from ? i + 1 : i;
        if (R && R.kind === "song") R.song = f(R.song);
        if (stepR && stepR.kind === "song") stepR.song = f(stepR.song);
        if (kept.lastSong != null) kept.lastSong = f(kept.lastSong);
        const ks = {};
        for (const k in kept.song) ks[f(+k)] = kept.song[k];
        kept.song = ks;
        djSongs(f);
    };
    /* a song put in (d 1: NEW, CLONE) or deleted (d -1) at `at`: the indexes follow; a deleted song
     * stops if it plays, and its place is forgotten */
    P.songsShifted = function (at, d) {
        if (d < 0 && R && R.kind === "song" && R.song === at) P.pause();
        const f = i => i == null || i < at ? i : d < 0 && i === at ? null : i + d;
        if (R && R.kind === "song") R.song = f(R.song);
        if (stepR && stepR.kind === "song") { stepR.song = f(stepR.song); if (stepR.song == null) stepR = null; }
        if (kept.lastSong != null) kept.lastSong = f(kept.lastSong);
        const ks = {};
        for (const k in kept.song) { const n = f(+k); if (n != null) ks[n] = kept.song[k]; }
        kept.song = ks;
        djSongs(f);
    };
    /* the list changed in any way (a song moved, cloned or deleted, the others shifting): f(old) is a song's new
     * index, null when it is gone (a deleted song stops if it plays) */
    P.songsRemap = function (f) {
        if (R && R.kind === "song" && f(R.song) == null) P.pause();
        if (R && R.kind === "song") R.song = f(R.song);
        if (stepR && stepR.kind === "song") { stepR.song = f(stepR.song); if (stepR.song == null) stepR = null; }
        if (kept.lastSong != null) kept.lastSong = f(kept.lastSong);
        const ks = {};
        for (const k in kept.song) { const n = f(+k); if (n != null) ks[n] = kept.song[k]; }
        kept.song = ks;
        djSongs(f);
    };
    P.playingPattern = () => { const h = P.at(); return h ? h.pat : -1; };
    P.loopPattern = () => R && R.kind === "loop" ? R.pat : -1;
    P.kept = kept;

    /* PLAY (specs/ui/transport.md): the screen's own thing on SONGS, the EDITOR and LOOPS, else what
     * played last. A song that is not there (all deleted) or has no positions: refused, PLAY blinks */
    function playOrRefuse(i, from) {
        const s = M.state.songs[i];
        if (s && s.order.length) return P.playSong(i, from);
        U().refuse(U().B.PLAY);
        U().toast("NO SONG TO PLAY");
    }
    P.playPress = function (screen) {
        const U_ = U();
        if (screen === "SONGS") {
            if (P.playing && R && R.kind === "song" && R.song === U_.song) return P.pause();
            P.holdMode();                                    /* (the play mode takes hold: PLAY starts the song here) */
            return playOrRefuse(U_.song, U_.screen.playFrom ? U_.screen.playFrom() : null);   /* (ALGORITHM's position picked; the decks playing: they pause) */
        }
        if (screen === "EDITOR") {
            const pat = U_.pattern;
            if (P.playing && R && R.kind === "loop" && R.pat === pat) return P.pause();
            if (P.playing && R && R.kind === "song" && P.playingPattern() === pat) {   /* hand over on the same row */
                const h = P.at();
                return P.playLoop(pat, h ? h.row + 1 : 0);
            }
            return P.playLoop(pat, U_.editor ? U_.editor.row : 0);              /* from the cursor's row */
        }
        if (screen === "LOOPS" || screen === "DJ") return U_.screen.play();    /* the head and the first loop: LOOPS' own; DJ: both decks */
        if (P.playing) return P.pause();
        if (P.what === "dj") return djPlayButton();                             /* the decks played last: resumed */
        if (P.what === "loop") return P.playLoop(kept.loop.pat);
        if (P.what === "loops" && M.state.loops.list.length) return P.playLoops();       /* the loops played last */
        if (P.what === "song" && kept.lastSong != null && M.state.songs[kept.lastSong]) return playOrRefuse(kept.lastSong);
        return playOrRefuse(U_.song);
    };
    /* whether PLAY has something to play on this screen, as playPress decides (else it refuses): its
     * red LED's rule at the start and while paused */
    P.canPlay = function (screen) {
        const U_ = U(), has = i => { const s = M.state.songs[i]; return !!(s && s.order.length); };
        if (screen === "SONGS") return has(U_.song);
        if (screen === "EDITOR") return U_.pattern != null && !!M.state.patterns[U_.pattern];
        if (screen === "LOOPS" || screen === "DJ") return !U_.screen.canPlay || U_.screen.canPlay();
        if (P.playing || P.what === "loop") return true;
        if (P.what === "dj") return DJ.decks.some(d => d.song != null);
        if (P.what === "loops" && M.state.loops.list.length) return true;
        if (P.what === "song" && kept.lastSong != null && M.state.songs[kept.lastSong]) return has(kept.lastSong);
        return has(U_.song);
    };
    /* PLAY held (specs/ui/transport.md): stop, back to the start (the press already paused or started it); PLAY then plays
     * from the beginning */
    P.stopToStart = function () {
        if (!P.what) return;                             /* nothing has played: nothing to stop */
        if (P.playing) P.pause();
        P.rewind();
    };
    /* PLAYBACK's K1 (specs/ui/screens/playback.md): another position of the song. Playing, the jump
     * comes when the current row ends, as a Bxx would, and clicks within one row add up; paused, the
     * place PLAY resumes moves */
    P.skip = function (d) {
        if (R && R.kind === "song") {
            const s = M.state.songs[R.song], h = P.at();
            if (!s || !s.order.length) return null;
            if (R.restartAt != null) {                   /* (landed, its clean start a moment away: there instead) */
                R.restartAt = U().clamp(R.restartAt + d, 0, s.order.length - 1);
                return { song: R.song, pos: R.restartAt };
            }
            R.skipTo = U().clamp((R.skipTo != null ? R.skipTo : h ? h.pos : R.pos) + d, 0, s.order.length - 1);
            return { song: R.song, pos: R.skipTo };
        }
        if (P.playing) return null;                      /* a pattern loop or the loops: no positions */
        const i = kept.lastSong != null ? kept.lastSong : U().song, s = M.state.songs[i];
        if (!s || !s.order.length) return null;
        const k = kept.song[i] || { pos: 0, row: 0 };
        kept.song[i] = { pos: U().clamp(k.pos + d, 0, s.order.length - 1), row: 0 };
        kept.lastSong = i;
        P.what = "song";
        P.stepped = false;
        return { song: i, pos: kept.song[i].pos };
    };
    /* the pattern list's PLAY (specs/ui/dialogs.md): a pattern heard on its own, looped from its start;
     * whatever played pauses and keeps its place, and stays what PLAY resumes once the list closes */
    let beforeAudition = null;
    P.audition = function (pat, timingOf) {
        if (!beforeAudition) beforeAudition = { what: P.what, loop: Object.assign({}, kept.loop), atStart: P.atStart };
        P.playLoop(pat, 0, timingOf ? { timing: timingOf(pat) } : {});
    };
    P.auditioning = () => !!beforeAudition && P.playing && R && R.kind === "loop" ? R.pat : -1;
    P.auditionEnd = function () {
        if (!beforeAudition) return;
        if (P.playing && R && R.kind === "loop") P.pause();
        P.what = beforeAudition.what;
        kept.loop = beforeAudition.loop;
        P.atStart = beforeAudition.atStart;
        beforeAudition = null;
    };
    /* HOME + PLAY: back to the start; playing, it restarts from there */
    P.rewind = function () {
        if (P.what === "dj" || (U().screen && U().screen.name === "DJ")) { djToCues(); U().toast("BACK TO THE CUES"); return; }    /* DJ: both decks to their cue points, paused */
        const was = P.playing, kind = R ? R.kind : P.what;
        if (kind === "song") { const i = R ? R.song : kept.lastSong; kept.song[i] = { pos: 0, row: 0 }; if (was) P.playSong(i); }
        else if (kind === "loop") { kept.loop.row = 0; if (was) P.playLoop(kept.loop.pat, 0); }
        else if (kind === "loops") { kept.loops.row = 0; if (was) P.playLoops(); }
        if (!was) P.atStart = true;
        U().toast("BACK TO THE START");
    };
    P.allSoundOff = function () {
        A().allOff(); releaseAll(R, now());
        for (const d of DJ.decks) if (d.r) releaseAll(d.r, now());
        for (const id in fusionRuns) delete fusionRuns[id];
    };
    /* UNLOAD SONG (SONGS' menu, PLAYBACK's GLO): offered while anything but the keys' song is held, playing or not (a
     * song, the EDITOR's pattern on its setup, the loops, DJ's decks); let go, the sound cut at once as SOUND CUT cuts
     * (every voice, the effects' tails), nothing held, as at the start: the keys through the keys' song (DJ's stand
     * and the loops themselves kept) */
    P.held = () => P.what === "song" || P.what === "loop" || P.what === "loops" || P.what === "dj";
    P.heldName = () => P.what === "loops" ? "THE LOOPS" : P.what === "dj" ? "DJ"
                     : ((P.what === "song" && kept.lastSong != null ? M.state.songs[kept.lastSong]
                        : P.what === "loop" ? M.state.songs[U().song] : null) || { name: "" }).name;
    P.unload = function () {
        if (!P.held()) return;
        djOff();
        stop(true);
        metroStop();
        P.allSoundOff();
        kept.song = {};
        kept.lastSong = null;
        P.what = null;
        P.atStart = true;
        A().songFx(null);
        U().toast("SONG UNLOADED");
    };
    /* the songs' tracks by HOME + white key; the loops' tracks by their loop; on DJ each deck's own */
    P.mutesChanged = function () {
        if (P.what === "dj") {
            for (const d of DJ.decks) for (let t = 0; t < 16; t++) A().trackMute(d.base + t, deckMuted(d, t));
            return;
        }
        for (let t = 0; t < 16; t++) A().trackMute(t, U().muted(t));
        /* a loop silent: muted, or another soloed (its mute kept for when the solos clear) */
        const L = M.state.loops.list, solo = L.some(l => l.soloed);
        for (let t = 0; t < 16; t++) A().trackMute(16 + t, !!L[t] && (solo ? !L[t].soloed : L[t].muted));
    };

    /* PLAYBACK's SELECT: pause, then play row by row; back plays the row before as if it came next.
     * Each row is its tick 0, held until the next click */
    let stepR = null;
    P.step = function (dir) {
        if (P.what === "dj") { djOff(); P.what = kept.lastSong != null ? "song" : null; }    /* the decks pause: a song by hand */
        const was = P.playing, h = P.at();
        if (was) stop();
        let s;
        if (P.what === "loop") {
            const p = M.pattern(kept.loop.pat);
            if (!p) return;
            const r = U().clamp((was ? (h ? h.row : 0) : kept.loop.row + dir), 0, p.rows - 1);
            kept.loop.row = r;
            s = { kind: "loop", pat: kept.loop.pat, row: r, pos: 0 };
        } else {
            const i = P.what === "song" ? (kept.lastSong != null ? kept.lastSong : U().song) : U().song;
            const sng = M.state.songs[i], k = kept.song[i] || { pos: 0, row: 0 };
            if (!sng || !sng.order.length) return;
            let pos = was && h ? h.pos : k.pos, row = was && h ? h.row : k.row + dir;
            let p = M.pattern(sng.order[pos]);
            if (row < 0) { if (pos > 0) { pos--; p = M.pattern(sng.order[pos]); row = p ? p.rows - 1 : 0; } else row = 0; }
            if (p && row >= p.rows) { if (pos < sng.order.length - 1) { pos++; p = M.pattern(sng.order[pos]); row = 0; } else row = p.rows - 1; }
            kept.song[i] = { pos, row };
            kept.lastSong = i;
            P.what = "song";
            s = { kind: "song", song: i, pos, row };
        }
        if (!stepR || stepR.kind !== s.kind || stepR.song !== s.song || stepR.pat !== s.pat) stepR = newReplay(s.kind, s);
        stepR.pos = s.pos; stepR.nextRow = s.row; stepR.breakPos = -1; stepR.t = now(); stepR.done = false;
        A().songFx(s.kind === "song" ? M.state.songs[s.song] : song());
        replayRow(stepR);
        hist = [hist.pop()].filter(Boolean);
        if (hist[0]) hist[0].dur = 1e9;
        P.stepped = true;
    };

    /* ARP's rhythms (SETTINGS → ARP RHYTHM, or ARP held + SELECT): a row a step, # a note, _ a rest, round
     * and round. The last two are slow, for long sounds */
    P.RHYTHMS = M.RHYTHMS;
    const gapAfter = (rh, i) => { let g = 1; while (g < rh.length && rh[(i + g) % rh.length] !== "#") g++; return g; };

    /* the EDITOR's ARP: the cursor's row in the rhythm */
    P.preview = function (on, getRow, rhythm) {
        if (!on) { preview = null; releasePrev(); return; }
        preview = { getRow, rhythm, i: 0, t: now() + .03 };
    };
    P.rhythm = rh => { if (preview) { preview.rhythm = rh; preview.i = 0; } if (repeat) { repeat.rhythm = rh; repeat.i = 0; } };
    function releasePrev() { for (let k = 0; k < 16; k++) noteOff("p" + k, now()); noteOff("pr", now()); }
    function tickPreview(t) {
        while (preview && preview.t < t + AHEAD) {
            const { pat, row } = preview.getRow(), p = M.pattern(pat), rh = preview.rhythm;
            const dur = P.rowSeconds(), i = preview.i % rh.length;
            if (p && rh[i] === "#") {
                const gap = gapAfter(rh, i);
                for (let k = 0; k < p.tracks; k++) {
                    const c = p.cells[row * 16 + k];
                    if (!c || c.note == null || c.note === M.OFF || U().muted(k)) continue;
                    const nx = row + 1 < p.rows ? p.cells[(row + 1) * 16 + k] : null;
                    const ends = nx && nx.note != null;
                    const ref = c.inst != null ? c.inst : M.trackInst(p, row, k) != null ? M.trackInst(p, row, k) : U().inst;
                    noteOff("p" + k, preview.t);
                    noteOn("p" + k, c.note, ref, c.vol == null ? 0x7F : c.vol, k, preview.t,
                           { accent: c.cmds.some(x => x.c === 0xC0 || x.c === 0xC2) });
                    A().noteOff("p" + k, preview.t + (ends ? dur : dur * gap) * .95);
                }
            }
            preview.t += dur;
            preview.i++;
        }
        while (repeat && repeat.t < t + AHEAD) {
            const dur = P.rowSeconds(), rh = repeat.rhythm;
            if (rh[repeat.i % rh.length] === "#" && repeat.notes.length) {
                noteOff("pr", repeat.t);
                noteOn("pr", P.arpNote(repeat.notes, repeat.pattern, repeat.k), repeat.ref, U().set("KEYS VOLUME"), -1, repeat.t, {});
                A().noteOff("pr", repeat.t + dur * .9);
                repeat.k++;
            }
            repeat.t += dur;
            repeat.i++;
        }
    }
    /* ARP's arpeggio (specs/ui/controls.md), one for every screen but the EDITOR (its row preview) and
     * LOOPS (RECORD PATTERN): its notes, in the order the keys were pressed, take turns in ARP PATTERN, a
     * note a "#" of ARP RHYTHM. With LATCH n (1-8) they keep playing once their keys are let go, n at
     * most (the oldest goes past n), and a key pressed with none held starts a new set; with LATCH 0 (OFF)
     * a note stops with its key */
    P.arpAdd = (set, midi, alone, latch) => {
        if (alone && latch > 0) set.length = 0;
        if (!set.includes(midi)) set.push(midi);
        if (latch > 0) while (set.length > latch) set.shift();
    };
    P.arpLift = (set, midi, latch) => { if (latch === 0) { const i = set.indexOf(midi); if (i >= 0) set.splice(i, 1); } };
    P.arpNote = (set, pattern, k) => {
        const up = set.slice().sort((a, b) => a - b);
        if (pattern === "RANDOM") return up[Math.floor(Math.random() * up.length)];
        const seq = pattern === "DOWN" ? up.slice().reverse() : pattern === "UP-DOWN" ? up.concat(up.slice(1, -1).reverse())
                  : pattern === "AS PLAYED" ? set : up;
        return seq[k % seq.length];
    };
    /* on: from the note given (hands free), or none (the keys bring them) */
    P.repeat = function (on, midi, ref, rhythm, pattern, latch) {
        if (!on) { repeat = null; noteOff("pr", now()); return; }
        repeat = { notes: midi != null ? [midi] : [], k: 0, ref, rhythm, pattern: pattern || "UP", latch: latch == null ? 4 : latch, i: 0, t: now() + .03 };
    };
    P.repeating = () => !!(repeat || preview);
    /* a key pressed (alone: no other key held) and let go */
    P.repeatKey = (midi, ref, alone) => {
        if (!repeat) return;
        repeat.ref = ref;
        P.arpAdd(repeat.notes, midi, alone, repeat.latch);
    };
    P.repeatKeyUp = midi => { if (repeat) P.arpLift(repeat.notes, midi, repeat.latch); };
    P.repeatClear = () => { if (repeat) { repeat.notes = []; repeat.k = 0; noteOff("pr", now()); } };
    /* the settings changed while it runs: the pattern; the latch (fewer kept: the oldest go; OFF: only
     * the notes whose keys are held, `held`, stay) */
    P.repeatSet = (pattern, latch, held) => {
        if (!repeat) return;
        repeat.pattern = pattern;
        repeat.latch = latch;
        if (latch > 0) while (repeat.notes.length > latch) repeat.notes.shift();
        else repeat.notes = repeat.notes.filter(m => (held || []).includes(m));
    };
    P.repeatRef = ref => { if (repeat) repeat.ref = ref; };
    P.repeatInst = () => repeat ? repeat.ref : null;
    P.repeatNotes = () => repeat ? repeat.notes.slice() : [];

    /* the METRONOME (specs/ui/screens/fx.md): after the effects, never a voice. FOLLOW clicks with what
     * plays, on the beat rows (ROWS A BEAT); its own tempo clicks with nothing playing too */
    function metroSet() {
        const s = P.what === "dj" ? M.state.dj : R ? (R.kind === "loops" ? M.state.loops : song()) : held();
        return s ? { on: s.fx.on.METRONOME, k: s.fx.knobs.METRONOME } : { on: false };
    }
    function metroStart() { metro = null; }
    function metroStop() { metro = null; }
    let metroClicks = [];                 /* its clicks as scheduled, {t, beat}: the picture lights the one heard last */
    const metroClicked = (t, beat) => {
        metroClicks.push({ t, beat });
        if (metroClicks.length > 16) metroClicks.splice(0, metroClicks.length - 16);
    };
    /* the bar's beat the METRONOME clicked last (0: the first), -1 while it does not count (off, or FOLLOW with
     * nothing playing): its own TEMPO's with nothing playing too (the firmware's fx_met_beat) */
    P.metroBeat = function () {
        const t = now();
        let b = -1;
        for (const x of metroClicks) if (x.t <= t) b = x.beat;
        return b;
    };
    function tickMetro(t) {
        const m = metroSet();
        if (countIn) {
            while (countIn.t < t + AHEAD && countIn.n > 0) {
                A().click(countIn.t, countIn.beat === 0, COUNT_SOUND, COUNT_LEVEL);
                countIn.beat = (countIn.beat + 1) % 4;
                countIn.t += countIn.beatDur;
                countIn.n--;
            }
            if (countIn.n === 0 && t >= countIn.t - .01) { const c = countIn; countIn = null; c.then(c.t); }
            return;
        }
        if (!m.on) { metro = null; metroClicks = []; return; }
        const follow = m.k[0] === 29, keys = !P.playing && held() === M.state.keys;   /* (THE KEYS' setup held) */
        if (follow && !P.playing && !keys) { metro = null; metroClicks = []; return; }
        if (follow && !keys) {
            for (const h of hist) {
                const rpb = h.rpb || 4;
                if (h.clicked || h.row % rpb !== 0) continue;
                h.clicked = true;
                A().click(h.t, (h.row / rpb) % m.k[1] === 0, m.k[2], m.k[3]);
                metroClicked(h.t, (h.row / rpb) % m.k[1]);
            }
            return;
        }
        if (!metro) metro = { t: t + .05, beat: 0 };
        const bpm = follow ? P.tempo() : m.k[0];   /* (FOLLOW on THE KEYS: their tempo) */
        while (metro.t < t + AHEAD) {
            A().click(metro.t, metro.beat === 0, m.k[2], m.k[3]);
            metroClicked(metro.t, metro.beat);
            metro.beat = (metro.beat + 1) % m.k[1];
            metro.t += 60 / bpm;
        }
    }
    /* the count-in (specs/ui/transport.md, COUNT-IN), then `then(t)`: `bars` bars of 4 beats at the
     * tempo, whatever the METRONOME does, with a click of its own (BEEP, the bar's first beat higher) at a
     * fixed level. COUNT-IN KEY counts nothing: the screen arms the take, the first note starts it */
    const COUNT_SOUND = 1, COUNT_LEVEL = 60;
    P.COUNT_BARS = { OFF: 0, "1 BAR": 1, "2 BARS": 2, KEY: 0 };
    P.countIn = function (then, bpm, bars) {
        if (!bars) return then(now() + .03);
        const t0 = now() + .05;
        countIn = { t0, t: t0, n: bars * 4, bars, beat: 0, beatDur: 60 / (bpm || P.tempo()), then };
    };
    P.countingIn = () => !!countIn;
    /* where the count is, for its box: the bar (0-based) of how many, the beat sounding (0-3) */
    P.countInState = function () {
        if (!countIn) return null;
        const k = U().clamp(Math.floor((now() - countIn.t0) / countIn.beatDur), 0, countIn.bars * 4 - 1);
        return { bar: Math.floor(k / 4), bars: countIn.bars, beat: k % 4 };
    };

    /* the EDITOR's realtime take (specs/ui/screens/editor.md#realtime-takes): the shown pattern from the cursor's
     * row, round the whole pattern again and again until PLAY or REC stops it, after `bars` bars of count-in */
    P.startTake = function (pat, row, now_, bars) {
        const go = t0 => { start_("loop", { pat, row }); R.t = t0; };
        if (now_) go(now() + .005);
        else P.countIn(go, P.tempo(), bars);
    };
    /* the take's keys sound exactly as written (SETTINGS' QUANTIZE): a cell the take wrote on track `track` at row `row`
     * of pass `pass` is heard there by the track, as the replay would play it: at that row's time if the replay has
     * scheduled it already (it runs a little ahead), else nothing (it plays it from the pattern when it gets there); at
     * once with pass < 0 (a note just after its line, REAL TIME's, its delay heard as played; a note off) */
    P.takeCell = function (track, c, pass, row) {
        if (!R || R.kind !== "loop" || !P.playing) return;
        let t = now() + .005, tick = 0;
        if (pass >= 0) {
            const at = hist.find(x => x.pass === pass && x.row === row);
            if (!at) return;
            t = Math.max(at.t, t);
            tick = at.dur / R.speed;
        }
        const ch = R.ch[track], k = cmdOf(c, 0x14);
        if (c.note === M.OFF) { keyOff(ch, t); return; }
        if (c.note == null) { if (k != null) keyOff(ch, t + k * tick); return; }
        const cell = ch.cell, cmds = ch.cmds;
        ch.cell = c;
        ch.cmds = c.cmds.filter(x => x.c !== 0x14 && !(x.c === 0x0E && x.v >> 4 === 0xD));   /* (its row's start, its delay past) */
        trigger(ch, R, t);
        out(ch, R, t);
        ch.cell = cell;
        ch.cmds = cmds;
    };
    P.rowTicks = () => R ? R.speed : held().ticks;       /* (the ticks a row of what plays: REAL TIME's tick) */
    P.stopCountIn = () => { countIn = null; };
    /* the header's ●: the EDITOR's take, LOOPS' take recording (the screen's P.lpRecording), a count-in */
    P.recording = () => !!(P.take || countIn || (P.lpRecording && P.lpRecording()));

    /* ========================================================================== DJ === */
    /* DJ's decks (specs/ui/screens/dj.md): two songs played at once, each by a replay of its own, on tracks
     * of its own: deck 0 on 0-15, deck 1 on 16-31 (the loops', which never play with the decks), whichever
     * side each is on; SWAP exchanges the sides, so a deck's voices go with it. The left side is the main.
     * Each deck's rate is a share of its song's tempo (BPM set by its knob), or with SYNC the main's BPM (or
     * its half or double); its beats are pulled onto the main's a row at a time. The hands (SCRATCH, REVERSE,
     * HALF TIME) run a silent replay beside the deck, its counted place (the ghost: the song's flow alone, at the
     * deck's rate), and letting go lands the deck there; SYNC follows that counted place. A ROLL does not: let go,
     * the deck plays on from where its loop is.
     * Nothing here is written into a song: the decks are performance state, never stored. */
    /* an LFO's settings (the LFO pads, below): on, gen (its starts, counted: a new one sets off afresh), shape
     * (TRIANGLE at first: LFO_SHAPES), range, rev (the crossfader's: toward R), len (its speed: a note length of
     * the leading deck's tempo, LFO_LENS: one swing there and back, slow to fast, [name, bars, beats]; 1/2 at
     * first, 1 Hz at 120 BPM) */
    const LFO_LENS = [["32 BARS", 32, 0], ["16 BARS", 16, 0], ["8 BARS", 8, 0], ["4 BARS", 4, 0], ["2 BARS", 2, 0], ["1 BAR", 1, 0],
                      ["1/2", 0, 2], ["1/4", 0, 1], ["1/8", 0, .5]];     /* (no 1/16: past an LFO, the user's) */
    const LFO_LEN0 = LFO_LENS.findIndex(x => x[0] === "1/2");
    const newLfo = (range = 0) => ({ on: false, gen: 0, shape: 2, range, rev: false, len: LFO_LEN0 });
    const FADER_RANGE0 = 3;                     /* the crossfader's LFO at first: MIRROR (the others their first range) */
    function newDeck(base, idp) {
        return { base, idp, i: base >> 4, song: null, r: null, playing: false, place: { pos: 0, row: 0 }, cue: { pos: 0, row: 0 }, lfo: newLfo(),
                 bpm: 120, key: 0, vol: 0, filter: 0, filterSet: 0, filterOn: true, slide: null, mutes: new Array(16).fill(false), solo: -1, hist: [], over: null,
                 ghost: null, scratch: null, reverse: null, half: false, loop: null, brake: null, backspin: null, braked: false, spin: -1,
                 echo: null, echoStop: false, rings: {}, pauseAt: null, jump: null, cueHeld: false, cueKeep: false, startAt: 0, halted: false, atEnd: false, nudge: null,
                 loadTo: null, swapAt: null, repeat: false };
    }
    /* xf: the crossfader, 0 the left end .. 32 the right; at first all the way to the main; SYNC on at first */
    const DJ = { decks: [newDeck(0, "s"), newDeck(16, "d")], sync: true, mult: 1, xf: 0, resume: null,
                 lfo: { FADER: newLfo(FADER_RANGE0), FX: newLfo() } };   /* (the crossfader's and the FX LFO; each deck's filter's its own) */
    DJ.sides = [DJ.decks[0], DJ.decks[1]];
    P.dj = DJ;
    const dsong = d => d && d.song != null ? M.state.songs[d.song] : null;
    const ownBpm = s => s.tempo * 24 / (s.ticks * s.rpb);
    const beatsOf = s => ((s.fx.knobs || {}).METRONOME || [])[1] || 4;
    const barRowsOf = s => s.rpb * beatsOf(s);
    const other = d => DJ.decks[1 - d.i];
    const synced = d => DJ.sync && DJ.sides[1] === d;
    P.djSynced = synced;
    /* the BPM a deck plays at: its own set BPM, or synced the main's times the multiple (an empty main: its own) */
    function djBpm(d) {
        const m = DJ.sides[0];
        return synced(d) && m.song != null ? m.bpm * DJ.mult : d.bpm;
    }
    P.djBpm = djBpm;
    /* the deck whose beats lead (PLAY's green, the METRONOME, the shared DELAY's tempo): the main, else the right */
    function djBeat() {
        const [m, r] = DJ.sides;
        return m.song != null && (m.playing || !r.playing) ? m : r.song != null ? r : null;
    }
    const beatDeck = () => djBeat();
    const deckMuted = (d, t) => d.solo >= 0 ? t !== d.solo : d.mutes[t];
    /* only this deck heard: the other not playing, or the crossfader at this deck's end (its song's punch-ins
     * play only then) */
    const deckAlone = d => !other(d).playing || (!DJ.lfo.FADER.on && DJ.xf === (DJ.sides[0] === d ? 0 : 32));
    P.deckMuted = deckMuted;
    /* the deck's rate at time t: tempo (synced: the main's BPM over the song's tempo now, its tempo commands
     * overridden; else the share of its song's own, its tempo commands scaled), HALF TIME, BRAKE (to a stop in
     * 0.5 s) and the spin up after it (0.1 s). The ghost counts at the plain rate */
    function deckRate(d, r, t, plain) {
        const s = dsong(d);
        if (!s) return 1;
        let f = synced(d) && DJ.sides[0].song != null ? djBpm(d) / (r.tempo * 24 / (r.speed * r.rpb)) : d.bpm / ownBpm(s);
        f = heldTempo(r.tempo * f) / r.tempo;            /* (the tempo within TEMPO's range; a nudge, a stop past it) */
        f *= 1 + nudgeAt(d, t);                          /* (a nudge moves the deck's place for good: its counted place too) */
        if (plain) return f;
        if (d.half) f *= .5;
        if (d.brake) f *= Math.max(.02, 1 - (t - d.brake.t0) / .5);
        if (d.backspin) f *= U().clamp(SPIN_FAST * (1 - (t - d.backspin.t0) / SPIN_S), .02, SPIN_FAST);
        if (d.spin >= 0 && t < d.spin + .1) f *= Math.max(.05, (t - d.spin) / .1);
        return f;
    }
    /* NUDGE (PRESETS on DJ): the deck a little faster or slower while the knob turns, 2 % a click up to 10 %,
     * held 0.1 s after the last click, then easing back to its tempo over 0.25 s. The rows only, as the tempo */
    const NUDGE_STEP = .02, NUDGE_MOST = .1, NUDGE_HOLD = .1, NUDGE_EASE = .25;
    function nudgeAt(d, t) {
        const n = d.nudge;
        if (!n) return 0;
        const k = 1 - Math.max(0, t - n.t - NUDGE_HOLD) / NUDGE_EASE;
        return k > 0 ? n.b * Math.min(1, k) : 0;
    }
    P.djNudge = function (d, steps) {
        if (d.song == null) return "empty";
        if (synced(d) && DJ.sides[0].song != null) return "synced";
        if (!d.playing) return "paused";
        const t = now();
        d.nudge = { b: U().clamp(nudgeAt(d, t) + Math.sign(steps) * NUDGE_STEP, -NUDGE_MOST, NUDGE_MOST), t };
        return d.nudge.b;
    };
    function deckReplay(d, pos, row) {
        const r = newReplay("song", { song: d.song, pos, row, breakPos: pos });
        r.base = d.base; r.idp = d.idp; r.deck = d; r.hist = d.hist;
        r.ch.forEach((ch, k) => { ch.pan = dsong(d).mixer[k] ? dsong(d).mixer[k].pan : 128; });
        r.rate = t => deckRate(d, r, t);
        return r;
    }
    const cutHist = (list, t) => { while (list.length && list[list.length - 1].t >= t) list.pop(); };
    /* the row heard at t (the deck's rows as scheduled) */
    function heardOf(d, t = now()) {
        let h = null;
        for (const x of d.hist) if (x.t <= t) h = x;
        return h || d.hist[0] || null;
    }
    P.djHeard = d => d.playing || d.scratch ? heardOf(d) : null;
    /* the place a row away (dir ±1) through the song's order, or null past its ends */
    function stepPlace(s, pos, row, dir) {
        let p = M.pattern(s.order[pos]);
        row += dir;
        if (row < 0) { if (pos <= 0) return null; pos--; p = M.pattern(s.order[pos]); row = p ? p.rows - 1 : 0; }
        else if (!p || row >= p.rows) { if (pos >= s.order.length - 1) return null; pos++; row = 0; }
        return { pos, row };
    }
    function stepPlaces(s, at, n) {
        let x = at;
        for (let k = 0; k < Math.abs(n) && x; k++) x = stepPlace(s, x.pos, x.row, Math.sign(n));
        return x;
    }
    /* the rows left from a place to the song's end, and its bars */
    function rowsLeft(s, at) {
        let n = 0;
        for (let i = at.pos; i < s.order.length; i++) { const p = M.pattern(s.order[i]); n += p ? p.rows : 0; }
        return Math.max(0, n - at.row);
    }
    function songRows(s) { return s.order.reduce((a, n) => a + (M.pattern(n) ? M.pattern(n).rows : 0), 0); }
    /* the overview (specs/ui/screens/dj.md#the-overview): each bar's notes started, every track; the positions' first bars */
    function overview(s) {
        const br = barRowsOf(s), bars = [], starts = [];
        let row = 0;
        s.order.forEach((n, i) => {
            const p = M.pattern(n);
            starts.push(Math.floor(row / br));
            if (!p) return;
            for (let r = 0; r < p.rows; r++, row++) {
                const b = Math.floor(row / br);
                bars[b] = bars[b] || 0;
                for (let t = 0; t < p.tracks; t++) { const c = p.cells[r * 16 + t]; if (c && c.note != null && c.note !== M.OFF) bars[b]++; }
            }
        });
        for (let b = 0; b < bars.length; b++) bars[b] = bars[b] || 0;
        return { bars, starts, rows: row, max: Math.max(1, ...bars) };
    }
    /* a place as a bar of the song, from its start */
    function barOf(s, at) {
        let row = 0;
        for (let i = 0; i < at.pos && i < s.order.length; i++) { const p = M.pattern(s.order[i]); row += p ? p.rows : 0; }
        return (row + at.row) / barRowsOf(s);
    }
    P.djBarOf = (d, at) => barOf(dsong(d), at);
    /* what goes round (the overview's green), as spans [from, to) in bars, to the row: a loop's rows as the song's
     * flow plays them (into the next position, past the song's end to its LOOP FROM), or REPEAT PATTERN's pattern,
     * at the place `at` */
    P.djRoundSpans = function (d, at) {
        const s = dsong(d), spans = [];
        if (!s) return spans;
        const br = barRowsOf(s), row = 1 / br;
        const next = x => stepPlace(s, x.pos, x.row, 1) || (s.atEnd === "LOOP" ? { pos: Math.min(s.loopFrom || 0, s.order.length - 1), row: 0 } : null);
        if (d.loop) {
            let x = { pos: d.loop.pos, row: d.loop.row };
            for (let k = 0; k < Math.max(1, Math.ceil(d.loop.len)) && x; k++) {
                const f = barOf(s, x), last = spans[spans.length - 1];
                if (last && Math.abs(last[1] - f) < 1e-9) last[1] = f + row; else spans.push([f, f + row]);
                x = next(x);
            }
        } else if (d.repeat && at) {
            const p = M.pattern(s.order[at.pos]), f = barOf(s, { pos: at.pos, row: 0 });
            if (p) spans.push([f, f + p.rows * row]);
        }
        return spans;
    };

    /* --------------------------------------------------------- the sound's side --- */
    const djVoices = () => M.state.settings.VOICES;     /* (DJ has no VOICES of its own: SETTINGS') */
    function mixMsg() {
        return { on: true, left: DJ.sides[0].base, xf: DJ.xf, sharp: M.state.dj.xfader === "SHARP",
                 filt: DJ.decks.map(d => d.filter), key: DJ.decks.map(d => d.key), vol: DJ.decks.map(d => volGain(d.vol)),
                 reso: M.state.dj.reso, sendsFiltered: M.state.dj.filterSends === "ON", lfo: lfoMsg() };
    }
    /* the LFOs for the worklet, each null while off, else its swing: from where it sets off, to where it goes
     * first, u where the thing is now between the two (a new one starts there), its shape and frequency; gen
     * counts its starts, at when it sets off (a step). The crossfader 0-32 (the left end 0), a filter -64 (LP's
     * end) to 63 (HP's); the FX LFO's effect (fx: REVERB, DELAY or CHORUS, as DJ's properties say) and range,
     * the worklet knowing its MIX */
    function lfoMsg() {
        const x = (l, from, to, here) => !l.on ? null : { gen: l.gen, at: l.at || 0, shape: l.shape, hz: lfoHz(l), from, to,
                                                          u: to === from ? 0 : U().clamp((here - from) / (to - from), 0, 1) };
        /* the crossfader's SWING: around where it is, out to either side as far as the end nearer it (at 10: 0 to 20),
         * setting off toward R (reversed: toward L) */
        const swingAround = l => { const h = DJ.xf, a = Math.min(h, 32 - h); return l.rev ? x(l, h + a, h - a, h) : x(l, h - a, h + a, h); };
        const F = DJ.lfo.FADER, end = F.rev ? 32 : 0, fl = DJ.lfo.FX;
        return {
            fader: F.range === 2 ? swingAround(F)                                        /* SWING: around its place, as far as the nearer end */
                 : F.range === 3 ? x(F, DJ.xf, 32 - DJ.xf, DJ.xf)                      /* MIRROR: its place ↔ the same mirrored */
                 : F.range ? x(F, 32 - end, end, DJ.xf) : x(F, DJ.xf, end, DJ.xf),       /* the fader's place ↔ an end, or end to end */
            /* a filter: OFF ↔ the filter heard, OFF ↔ the end of its side (FULL), it ↔ that end, or SWING: it ↔ the
             * same mirrored (LP ↔ HP through OFF); its side is where it is set (OFF: LP's). Heard only while its
             * FILTER key has it on; off, it swings on unheard around the setting the key brings in (heard false, home:
             * where it comes back to), a preview the filter row shows, so the key brings it in mid-swing */
            filt: DJ.decks.map(d => { const l = d.lfo, h = d.filterOn || d.slide ? d.filter : d.filterSet, end = h > 0 ? 63 : -64;
                                      const y = (from, to) => { const a = x(l, from, to, h); return a && Object.assign(a, { home: h, heard: d.filterOn || !!d.slide }); };
                                      if (l.range === 0) return y(h, 0);
                                      if (l.range === 2) return y(h, end);
                                      if (l.range === 3) return y(h, U().clamp(-h, -64, 63));
                                      return Math.abs(end - h) >= Math.abs(h) ? y(0, end) : y(end, 0); }),   /* (toward the farther) */
            fx: fl.on ? Object.assign(x(fl, 0, 1, 0), { fx: lfoFx(), range: fl.range }) : null,
        };
    }
    /* the effect the FX LFO swings the MIX of (DJ's properties: REVERB MIX at first) */
    const lfoFx = () => (M.state.dj.lfoFx || "REVERB MIX").split(" ")[0];
    P.djLfoFx = lfoFx;
    /* what the worklet needs while the decks are what plays: DJ's mix, each deck's song's setup and mixer */
    P.djPost = function () {
        if (P.what !== "dj") return;
        A().dj(mixMsg());
        for (const d of DJ.decks) { const s = dsong(d); if (s) A().djDeck(d.i, s.fx, s.mixer); }
    };
    P.djMix = () => { if (P.what === "dj") A().dj(mixMsg()); };
    P.djTempos = () => DJ.decks.map(d => d.song != null ? djBpm(d) : 0);
    /* the decks' effects' cost (audio.js's CPU figure): each song's small effects on, a DJ FILTER not OFF, the
     * filtered sends */
    P.djCost = function (cost) {
        let c = 0;
        for (const d of DJ.decks) {
            const s = dsong(d);
            if (!s) continue;
            for (const n of ["GATE", "FILTER", "DRIVE", "LO-FI", "DUCK"]) if (s.fx.on[n]) c += cost[n];
            if (d.filter) c += cost.FILTER + (M.state.dj.filterSends === "ON" ? 1.75 : 0);
        }
        return c;
    };
    /* DJ's first start: the decks become what plays. What played stops keeping its place and its sound is cut
     * (every voice, the effects' tails: DJ starts dry); the worklet into DJ's mix, DJ's VOICES */
    function djOn() {
        if (P.what === "dj") return;
        if (R) { stop(true); metroStop(); }
        A().allOff();
        P.what = "dj";
        speed = 1; vol = 0; P.endMode = "own";           /* (PLAYBACK's speed and volume back, the play mode out: DJ) */
        songGain();
        hist = [];
        A().voices(djVoices());
        A().fxNow();                                     /* DJ's setup, then its mix and the decks' (djPost) */
        P.mutesChanged();
    }
    /* something else starts: the decks pause, keeping their places, and the worklet leaves DJ's mix */
    function djOff() {
        if (P.what !== "dj") return;
        for (const d of DJ.decks) { if (d.playing) deckPause(d); endHands(d); }
        DJ.resume = null;
        P.what = null;
        P.playing = false;
        A().dj({ on: false });
        A().voices(M.state.settings.VOICES);
        P.mutesChanged();
    }
    P.djVoicesChanged = () => { if (P.what === "dj") A().voices(djVoices()); };

    /* --------------------------------------------------------------- a deck --- */
    /* LOAD: the song at its start, its cue point there, its own tempo (synced: the main's), KEY 0, filter OFF.
     * On a deck playing, it waits for the deck's next bar line and plays on from there ("atbar"; again:
     * "cancel"); a deck waiting for its synced start gives the start up and loads paused */
    P.djLoad = function (d, i) {
        const s = M.state.songs[i];
        if (d.loadTo != null) { d.loadTo = null; d.loadOwn = false; return "cancel"; }
        if (!s || !s.order.length) return "empty";
        if (d.playing && d.startAt) P.djCancelStart(d);
        if (d.playing) { d.loadTo = i; return "atbar"; }
        endHands(d);
        if (d.braked) { A().djBrake(d.i, now(), false, true); d.braked = false; }
        Object.assign(d, { song: i, place: { pos: 0, row: 0 }, cue: { pos: 0, row: 0 }, atEnd: false, nudge: null, bpm: ownBpm(s), key: 0,
                           filter: 0, filterSet: 0, filterOn: true, slide: null, mutes: new Array(16).fill(false), solo: -1, hist: [], over: overview(s), jump: null, repeat: false, loop: null });
        d.lfo.on = false;                                /* (its filter on at OFF, its LFO off; a LOAD at the bar keeps both) */
        if (synced(d) || (DJ.sync && DJ.sides[0] === d)) pickMult();
        if (P.what === "dj") { A().djReset(d.i); A().djDeck(d.i, s.fx, s.mixer); P.djMix(); P.mutesChanged(); }
        return "ok";
    };
    /* a LOAD on a playing deck, at its bar line `at`: the song before stops there and the new one plays on from
     * its start, at the BPM the deck played (the beat goes on); KEY 0, mutes off, the filter as the knob is */
    function loadAtBar(d, at) {
        const i = d.loadTo, s = M.state.songs[i], bpm = d.bpm, own = d.loadOwn, f = { filter: d.filter, filterSet: d.filterSet, filterOn: d.filterOn, slide: d.slide };
        const lfoOn = d.lfo.on;
        deckPause(d, at);
        if (!s || !s.order.length) { djPaused(); return; }
        if (P.djLoad(d, i) !== "ok") return;
        if (!synced(d) && !own) d.bpm = bpm;             /* LOAD tapped: the tempo kept; held: the song's own */
        if (DJ.sync && DJ.sides[0] === d) pickMult();    /* (the main: SYNC's multiple for the tempo it plays at) */
        Object.assign(d, f);                             /* (the filter as its knob and key left it, its LFO too) */
        d.lfo.on = lfoOn;
        if (P.what === "dj") P.djMix();
        d.r = deckReplay(d, 0, 0);
        d.r.t = at;
        d.playing = true;
        P.playing = true;
    }
    /* SYNC's multiple: the main's BPM, its half or double, whichever is nearest the right song's own */
    function pickMult() {
        const [m, r] = DJ.sides, rs = dsong(r);
        if (!rs || m.song == null) { DJ.mult = 1; return; }
        const own = ownBpm(rs);
        DJ.mult = [.5, 1, 2].reduce((a, k) => Math.abs(m.bpm * k - own) < Math.abs(m.bpm * a - own) ? k : a, 1);
    }
    /* when a deck starting now would start: a synced deck waits for the other's bar line (the playing one never
     * moves for the one starting), else at once */
    function startTime(d) {
        const o = other(d), t0 = now() + .05;
        if (!(o.playing && o.r && !o.startAt && (synced(d) || synced(o)))) return t0;
        return barLine(o, t0);
    }
    /* the first bar line (every n rows: 1, a step) of deck x at or after t (its counted place's while the hands
     * are on it) */
    function barLine(x, t, n = barRowsOf(dsong(x))) {
        const s = dsong(x), H = x.ghost ? x.ghost.hist : x.hist;
        for (const e of H) if (e.t >= t && e.row % n === 0) return e.t;
        const h = H[H.length - 1];
        if (!h) return t;
        const rd = 60 / (djBpm(x) * s.rpb);
        let at = h.t + (n - h.row % n) * rd;
        while (at < t) at += n * rd;
        return at;
    }
    /* a deck's PLAY (atOnce: now, even a synced deck, not on the main's bar line: a HOLD let go) */
    P.djPlay = function (d, atOnce) {
        if (d.song == null) return "empty";
        if (d.atEnd) return "end";                       /* nothing more to play: CUE goes back */
        djOn();
        if (d.playing) return "playing";
        const t0 = atOnce ? now() + .05 : startTime(d);
        endHands(d);
        loopCount(d);                                    /* (a loop set or kept goes round from here) */
        d.hist.length = 0;
        d.r = deckReplay(d, d.place.pos, d.place.row);
        d.r.t = d.r.t0 = t0;
        d.startAt = t0 > now() + .1 ? t0 : 0;
        d.playing = true;
        d.halted = false;
        if (d.braked) { A().djBrake(d.i, t0, true); d.spin = t0; d.braked = false; }     /* back from a BRAKE: spinning up */
        if (d.echo) {                                    /* back from an ECHO / REVERB OUT: dry again, its tail ringing out */
            A().djEcho(d.i, "ring", d.echo.fx);
            d.rings[d.echo.fx] = d.echo.until || now() + RING[d.echo.fx];
            d.echo = null;
        }
        P.playing = true;
        P.atStart = false;
        if (beatDeck() === d) hist = [];
        return "ok";
    };
    /* paused: its notes let go, its place the row heard */
    function deckPause(d, at) {
        const t = at != null ? at : now();
        if (d.r) {
            const h = heardOf(d, t);
            if (h && d.r.started && h.t <= t) d.place = { pos: h.pos, row: h.row };
            A().flush(d.idp, t);
            releaseAll(d.r, t);
            if (d.r.punchK != null) A().punch(d.r.punchK, false);
        }
        d.r = null; d.playing = false; d.startAt = 0; d.pauseAt = null; d.halted = false; d.loadTo = null; d.swapAt = null; d.loadOwn = false;
        endHands(d);
        P.playing = DJ.decks.some(x => x.playing);
    }
    /* (nothing playing any more: a SAVE that waited for a pause goes ahead) */
    const djPaused = () => { if (!P.playing) U().pausedHook(); };
    P.djPause = d => { if (d.playing) { deckPause(d); djPaused(); } };
    /* a synced start waiting for the bar line: a second tap cancels it */
    P.djCancelStart = d => { if (d.startAt) { d.r = null; d.playing = false; d.startAt = 0; P.playing = DJ.decks.some(x => x.playing); } };
    /* the song's end (AT THE END: STOP): paused at its end, AT THE END; PLAY has nothing more there, CUE goes back
     * to the cue point (atEnd: until the deck is moved) */
    function deckEnd(d) {
        deckPause(d, d.r ? d.r.endT : undefined);
        const s = dsong(d), last = Math.max(0, s.order.length - 1), p = M.pattern(s.order[last]);
        d.place = { pos: last, row: p ? p.rows - 1 : 0 };
        d.atEnd = true;
        djPaused();
    }
    /* PLAY: every deck playing pauses (PLAY again starts every deck with a song, together) */
    function djPauseAll() {
        for (const d of DJ.decks) if (d.playing) deckPause(d);
        djPaused();
    }
    /* the PLAY button: every deck playing pauses; none playing, every deck with a song (not at its end) starts, all
     * at the same moment. Something else playing (DJ apart till its first start): it pauses, and the next PLAY
     * plays the decks */
    function djPlayButton() {
        if (P.playing && P.what !== "dj") { P.pause(); return "other"; }
        if (P.playing) { djPauseAll(); return "paused"; }
        DJ.resume = null;
        const list = DJ.decks.filter(d => d.song != null && !d.atEnd);
        if (!list.length) return DJ.decks.some(d => d.song != null) ? "end" : "empty";
        djOn();
        const t0 = now() + .05;
        for (const d of list) { P.djPlay(d); d.r.t = d.r.t0 = t0; d.startAt = 0; }     /* together */
        return "ok";
    }
    P.djPlayButton = djPlayButton;
    /* PLAY held, HOME + PLAY: both decks back to their cue points, paused */
    function djToCues() {
        for (const d of DJ.decks) { if (d.playing) deckPause(d); d.place = Object.assign({}, d.cue); d.atEnd = false; }
        DJ.resume = null;
    }
    P.djToCues = djToCues;
    /* a deck's PLAY key held: that deck back to its song's start, paused (its cue point stays) */
    P.djToStart = function (d) {
        if (d.song == null) return false;
        if (d.startAt) P.djCancelStart(d);
        if (d.playing) deckPause(d);
        endHands(d);
        d.place = { pos: 0, row: 0 };
        d.atEnd = false;
        return true;
    };
    /* CUE (specs/ui/screens/dj.md#playing-and-pausing): paused, the cue point where it stands, on the bar line at
     * or before it (and the deck there); playing, or at the song's end, back to the cue point, paused. Held at
     * the cue point while paused: it plays from there while held, back there when
     * let go */
    P.djAtCue = d => !d.playing && !d.atEnd && d.place.pos === d.cue.pos && d.place.row === d.cue.row;
    P.djCueDown = function (d) {
        if (d.song == null) return "empty";
        /* playing (or at the song's end): back to the cue point, playing from it while held, as at the cue */
        const back = d.playing || d.atEnd;
        if (back) { if (d.playing) deckPause(d); d.place = Object.assign({}, d.cue); d.atEnd = false; }
        if (back || P.djAtCue(d)) {
            djOn();
            d.cueHeld = true;
            d.cueKeep = false;
            d.place = Object.assign({}, d.cue);
            P.djPlay(d);
            d.r.t = now() + .02; d.startAt = 0;
            P.djMix();
            return "held";
        }
        const br = barRowsOf(dsong(d));
        d.cue = d.place = { pos: d.place.pos, row: Math.floor(d.place.row / br) * br };
        return "set";
    };
    /* SET CUE (PLAY's pad 2): playing, the cue point on the bar line nearest the row heard (a tap on the one, a
     * little early or late, lands on it), the deck playing on; paused, as CUE's tap, the deck there too; held
     * (HOLD), the bar line at or before the hand. The new cue point, or null (no song, at the song's end) */
    P.djCueSet = function (d) {
        const s = dsong(d);
        if (!s || d.atEnd) return null;
        const br = barRowsOf(s);
        if (d.scratch) { d.cue = { pos: d.scratch.at.pos, row: Math.floor(d.scratch.at.row / br) * br }; return d.cue; }
        if (!d.playing) { d.cue = d.place = { pos: d.place.pos, row: Math.floor(d.place.row / br) * br }; return d.cue; }
        const st = P.djState(d), p = M.pattern(s.order[st.at.pos]);
        let row = Math.round((st.at.row + st.rowFrac) / br) * br, pos = st.at.pos;
        if (!p || row >= p.rows) {                               /* (the next bar line is the next position's start) */
            if (pos < s.order.length - 1) { pos++; row = 0; }
            else row = Math.floor(st.at.row / br) * br;
        }
        d.cue = { pos, row };
        return d.cue;
    };
    /* SET CUE held (past the pads' 250 ms): the cue point on the start of the beat `at` is in (the place the press
     * found), finer than the tap's bar line; paused, the deck there too */
    P.djCueBeat = function (d, at) {
        const s = dsong(d);
        if (!s || !at) return null;
        d.cue = { pos: at.pos, row: Math.floor(at.row / s.rpb) * s.rpb };
        if (!d.playing && !d.scratch) d.place = Object.assign({}, d.cue);
        return d.cue;
    };
    /* SET CUE with MOVE held: the cue point on the bar line MOVE's cursor is in, the deck where it is */
    P.djCueAt = function (d, at) {
        const s = dsong(d);
        if (!s || !at) return null;
        const br = barRowsOf(s);
        d.cue = { pos: at.pos, row: Math.floor(at.row / br) * br };
        return d.cue;
    };
    P.djCueUp = function (d) {
        if (!d.cueHeld) return;
        d.cueHeld = false;
        if (d.cueKeep) { d.cueKeep = false; P.djMix(); return; }      /* (its PLAY key meanwhile: it plays on) */
        deckPause(d);
        d.place = Object.assign({}, d.cue);
        P.djMix();
    };
    /* the deck's PLAY key while CUE is held playing from the cue: letting go plays on from there, as a DJ player's
     * CUE then PLAY; again, back to the cue ("kept"; null when CUE is not held) */
    P.djCueKeep = d => d.cueHeld ? (d.cueKeep = !d.cueKeep, "kept") : null;
    /* SYNC on / off (the right side's). Off, the right keeps the BPM it played at */
    P.djSync = function () {
        const r = DJ.sides[1];
        if (DJ.sync) { if (r.song != null) r.bpm = djBpm(r); DJ.sync = false; }
        else { DJ.sync = true; pickMult(); }
        return DJ.sync;
    };
    /* SWAP: the sides exchanged; the crossfader mirrored; SYNC stays with the side; the new main keeps the BPM it
     * played at as its own */
    P.djSwap = function () {
        if (DJ.decks.every(d => d.song == null)) return false;
        const [m, r] = DJ.sides;
        if (r.song != null) r.bpm = djBpm(r);
        DJ.sides = [r, m];
        DJ.xf = 32 - DJ.xf;
        DJ.lfo.FADER.rev = !DJ.lfo.FADER.rev;            /* (its swing mirrored with it, toward the same deck) */
        if (DJ.sync) pickMult();
        hist = [];
        P.djMix();
        return true;
    };
    /* a tempo turned (0.1 BPM a step; 3 to 600 BPM whatever the song's own: M.tempoRange); null while synced (its
     * knob does nothing) */
    P.djTempo = function (d, steps) {
        if (d.song == null) return "empty";
        if (synced(d) && DJ.sides[0].song != null) return "synced";
        const [lo, hi] = M.tempoRange(dsong(d));
        d.bpm = Math.round(U().clamp(d.bpm + steps * .1, lo, hi) * 10) / 10;
        if (DJ.sync && DJ.sides[0] === d && dsong(DJ.sides[1]) && DJ.sides[1].playing) { /* the right follows at once */ }
        return "ok";
    };
    P.djTempoReset = d => { const s = dsong(d); if (s) d.bpm = ownBpm(s); };
    /* LOAD held on a playing deck: the song comes in at its own tempo (at the bar line, or now if it came already) */
    P.djLoadOwnTempo = d => {
        if (d.loadTo != null) { d.loadOwn = true; return "at the bar"; }
        if (!synced(d)) P.djTempoReset(d);
        if (DJ.sync && DJ.sides[0] === d) pickMult();    /* (the main: as at the bar line, loadAtBar) */
        return "now";
    };
    P.djOwnBpm = d => dsong(d) ? ownBpm(dsong(d)) : 0;
    /* the DJ FILTER: its knob sets it, heard while it is on; its key switches it on and off, sliding there over
     * FILTER_SLIDE; on at OFF at first. filterSet: the knob's setting; filterOn: switched on; filter: what is heard */
    const FILTER_SLIDE = .5;
    P.djFilter = (d, steps) => {
        d.filterSet = U().clamp((d.filterSet || 0) + steps, -64, 63);
        if (d.filterOn) { d.slide = null; d.filter = d.filterSet; }
        P.djMix();                                       /* (heard; or off, its LFO's preview swings around the setting) */
    };
    P.djFilterSwitch = function (d) {
        if (d.song == null) return "empty";
        d.filterOn = !d.filterOn;
        d.slide = { from: d.filter, to: d.filterOn ? d.filterSet : 0, t0: now() };
        P.djMix();                                       /* (its LFO comes and goes with it) */
        return d.filterOn ? "on" : "off";
    };
    /* the key let go: the slide ends there, at its end (a quick press switches at once, a held one slides) */
    P.djFilterSettle = d => { if (d.slide) { d.filter = d.slide.to; d.slide = null; P.djMix(); } };
    function slideFilter(d, t) {
        const sl = d.slide, x = Math.min(1, (t - sl.t0) / FILTER_SLIDE), v = Math.round(sl.from + (sl.to - sl.from) * x);
        if (v !== d.filter) { d.filter = v; P.djMix(); }
        if (x >= 1) d.slide = null;
    }
    P.djXf = steps => { DJ.xf = U().clamp(DJ.xf + steps, 0, 32); P.djMix(); };
    P.djKey = (d, k) => { d.key = U().clamp(k, -6, 6); P.djMix(); };
    /* a deck's VOLUME (specs/ui/screens/dj.md#volume): dB, 0 at first, -24 to +6, below -24 OFF (VOL_OFF): its sound
     * and its sends before the crossfader, as a mixer's channel fader */
    const VOL_OFF = -25, VOL_MOST = 6;
    const volGain = v => v <= VOL_OFF ? 0 : Math.pow(10, v / 20);
    P.VOL_OFF = VOL_OFF; P.VOL_MOST = VOL_MOST;
    P.djVol = (d, v) => { d.vol = U().clamp(v, VOL_OFF, VOL_MOST); P.djMix(); };
    P.djHush = on => A().hush(on);                       /* the CUT held (U.cut): nothing heard, the decks keeping time */

    /* ----------------------------------------------------------------- the LFOs --- */
    /* the LFO pads (specs/ui/screens/dj.md#the-lfo-pads): the crossfader's LFO, each deck's DJ FILTER's, the FX
     * LFO (the MIX of DJ's REVERB, DELAY or CHORUS, as its properties say), each swinging by itself at its own
     * speed. Here their settings; the swing runs in the worklet (dsp-fx.js), which reports where each one is.
     * The crossfader's range: its place ↔ L, or L ↔ R (rev: its place ↔ R, or setting off toward R), SWING around its
     * place as far as the nearer end, or MIRROR, its place ↔ the same mirrored (through the middle); a filter's:
     * OFF ↔ the filter heard, OFF ↔ the end of its side, it ↔ that end, it ↔ the same mirrored (SWING); the FX
     * LFO's: its MIX as set ↔ 100 %, or 0 ↔ 100 % */
    const LFO_SHAPES = ["SINE", "SQUARE", "TRIANGLE", "SAW UP", "SAW DOWN", "RANDOM"];
    /* its speed, a note length (LFO_LENS, above), follows the leading deck's tempo (no deck: 120 BPM); never
     * faster than LFO_MOST */
    const LFO_MOST = 10;
    function lenHz(i) {                                  /* the leading deck's BPM and bar */
        const b = beatDeck(), s = b && dsong(b), [, bars, beats] = LFO_LENS[i];
        return (s ? djBpm(b) : 120) / 60 / (bars ? bars * (s ? beatsOf(s) : 4) : beats);
    }
    const lfoHz = l => Math.min(LFO_MOST, lenHz(l.len));
    P.LFO_SHAPES = LFO_SHAPES;
    P.djLfoHz = lfoHz;
    P.djLfoSpeed = l => LFO_LENS[l.len][0];
    P.djLfo = (name, d) => name === "FILTER" ? d.lfo : DJ.lfo[name];
    /* switched on, it sets off on the next step (row) of the deck whose beats lead (at once with no deck
     * playing): at, on the audio clock */
    P.djLfoOn = function (l) {
        l.on = !l.on;
        if (l.on) {
            const b = beatDeck();
            l.gen++;
            l.at = b && b.playing && b.r && !b.startAt ? barLine(b, now() + .01, 1) : 0;
        }
        P.djMix();
        return l.on;
    };
    P.djLfoWaits = l => l.on && l.at > now();
    P.djLfoRange = function (l, n) { l.range = (l.range + 1) % n; P.djMix(); };
    P.djLfoShape = function (l) { l.shape = (l.shape + 1) % LFO_SHAPES.length; P.djMix(); };
    P.djLfoRev = function (l) { l.rev = !l.rev; P.djMix(); };
    /* ALGORITHM with its on / off pad held: a length a click (faster clockwise), the ones over LFO_MOST skipped */
    P.djLfoTurn = function (l, steps) {
        const fast = LFO_LENS.map((x, i) => i).filter(i => lenHz(i) <= LFO_MOST);
        if (!fast.length || !steps) return;
        l.len = fast[U().clamp(fast.indexOf(Math.min(l.len, fast[fast.length - 1])) + steps, 0, fast.length - 1)];
        P.djMix();
    };
    P.djLfoReset = l => { l.len = LFO_LEN0; P.djMix(); };   /* (SEL after: back to 1/2) */
    /* an LFO follows the tempo: sent again when its speed changes (a tempo turned, SYNC, a LOAD) */
    let lfoSent = "";
    function lfoTempo() {
        const k = [DJ.lfo.FADER, DJ.lfo.FX, ...DJ.decks.map(d => d.lfo)].map(l => l.on ? lfoHz(l).toFixed(4) : "").join();
        if (k !== lfoSent) { lfoSent = k; P.djMix(); }
    }
    /* every LFO off, each thing back where it was */
    P.djLfosOff = function () {
        for (const l of [DJ.lfo.FADER, DJ.lfo.FX, ...DJ.decks.map(d => d.lfo)]) l.on = false;
        P.djMix();
    };

    /* ----------------------------------------------------------------- the stand --- */
    /* DJ's stand (specs/files/dj.md#the-stand): what SAVE keeps of the decks, as they stand paused: each side's song,
     * place, cue, tempo, KEY, VOLUME, filter (its setting and its key), loop or REPEAT, mutes and solo, its filter
     * LFO's setup; the crossfader, SYNC (and its multiple), the crossfader's and the FX LFO's setups. Not
     * where an LFO is in its swing, nor anything held. Loaded at power-on (and by a REVERT), the decks paused */
    const lfoSetup = l => ({ on: !!l.on, shape: l.shape, range: l.range, rev: !!l.rev, len: l.len });
    function standOf(dj) {
        const deck = d => d.song == null ? null : {
            song: d.song, place: { pos: d.place.pos, row: d.place.row }, cue: { pos: d.cue.pos, row: d.cue.row }, atEnd: !!d.atEnd,
            bpm: Math.round(d.bpm * 10) / 10, key: d.key, vol: d.vol, filter: d.filterSet, filterOn: !!d.filterOn, repeat: !!d.repeat,
            loop: d.loop && !d.loop.roll ? { pos: d.loop.pos, row: d.loop.row, beats: d.loop.beats, bars: !!d.loop.bars, idx: d.loop.idx || 0 } : null,
            mutes: d.mutes.map(Boolean), solo: d.solo, lfo: lfoSetup(d.lfo) };
        return { decks: dj.sides.map(deck), xf: dj.xf, sync: !!dj.sync, mult: dj.mult,
                 fader: lfoSetup(dj.lfo.FADER), fx: lfoSetup(dj.lfo.FX) };
    }
    const BLANK = () => standOf({ sides: [newDeck(0, "s"), newDeck(16, "d")], xf: 0, sync: true, mult: 1,
                                  lfo: { FADER: newLfo(FADER_RANGE0), FX: newLfo() } });
    P.djStand = () => standOf(DJ);
    /* the stand as SAVE writes it: null when nothing is set (both decks empty, all as at first) */
    P.djStandToSave = () => { const s = standOf(DJ); return JSON.stringify(s) === JSON.stringify(BLANK()) ? null : s; };
    /* the stand differs from the one stored (SAVE's LED: something to save) */
    P.djStandChanged = stored => JSON.stringify(standOf(DJ)) !== JSON.stringify(stored || BLANK());
    /* the decks set as the stand says, paused (null: as at first); its LFOs set off afresh once the decks play */
    P.djRestore = function (st) {
        djOff();
        st = st || BLANK();
        DJ.sides = [DJ.decks[0], DJ.decks[1]];
        const lfo = (l, x) => Object.assign(l, x, { gen: l.gen + 1, at: 0, len: Math.min(x.len, LFO_LENS.length - 1) });
        st.decks.forEach((x, k) => {
            const d = DJ.sides[k], gen = d.lfo.gen;
            endHands(d);
            Object.assign(d, newDeck(d.base, d.idp));
            d.lfo.gen = gen;
            const s = x && M.state.songs[x.song];
            if (!s || !s.order.length || P.djLoad(d, x.song) !== "ok") return;
            const at = p => { const pos = Math.min(p.pos, s.order.length - 1), pat = M.pattern(s.order[pos]); return { pos, row: Math.min(p.row, pat ? pat.rows - 1 : 0) }; };
            Object.assign(d, { place: at(x.place), cue: at(x.cue), atEnd: !!x.atEnd, bpm: x.bpm, key: x.key, vol: x.vol, filterSet: x.filter,
                               filterOn: !!x.filterOn, filter: x.filterOn ? x.filter : 0, repeat: !!x.repeat, mutes: x.mutes.slice(), solo: x.solo });
            lfo(d.lfo, x.lfo);
            if (x.loop) d.loop = { pos: x.loop.pos, row: x.loop.row, len: x.loop.beats * s.rpb, n: 0, beats: x.loop.beats, roll: false,
                                   bars: !!x.loop.bars, idx: x.loop.idx };
        });
        Object.assign(DJ, { xf: st.xf, sync: !!st.sync, mult: st.mult, resume: null });
        lfo(DJ.lfo.FADER, st.fader);
        lfo(DJ.lfo.FX, st.fx);
        P.mutesChanged();
    };

    /* ----------------------------------------------------------------- the hands --- */
    /* the deck's counted place: a silent copy of its replay's flow, from where the replay stands */
    const FLOW = ["pos", "row", "nextRow", "breakPos", "tick", "plCount", "plChan", "tempo", "speed", "gvol", "t", "done", "endT", "started"];
    function ghostOf(d) {
        const r = d.r, g = newReplay("song", { song: d.song, pos: r.pos, row: r.row });
        for (const k of FLOW) g[k] = r[k];
        r.ch.forEach((ch, k) => { g.ch[k].plRow = ch.plRow; });
        Object.assign(g, { silent: true, deck: d, base: d.base, idp: d.idp, hist: d.hist.slice(), loopN: d.loop && !d.loop.roll ? d.loop.n : null });
        g.rate = t => deckRate(d, g, t, true);
        return g;
    }
    /* the replay starts over at t from a place: what it had scheduled from t goes, its notes end at t */
    function rewindTo(d, at, t) {
        const r = d.r;
        A().flush(d.idp, t, t);
        r.breakPos = at.pos; r.nextRow = at.row; r.tick = 1; r.t = t; r.done = false;
        for (const ch of r.ch) { ch.vid = null; ch.live = null; }
        cutHist(d.hist, t);
        if (beatDeck() === d) cutHist(hist, t);
    }
    /* letting go: the deck where it would have been, on the counted place's next row */
    function land(d) {
        const g = d.ghost, r = d.r;
        d.ghost = null;
        if (r) r.held = false;
        if (!g || !r) return;
        const t = now();
        let guard = 4096;
        while (!g.done && (g.t < t || g.tick !== 1) && guard--) replayTick(g);
        if (g.done) { deckEnd(d); return; }
        A().flush(d.idp, g.t, g.t);
        for (const k of FLOW) r[k] = g[k];
        r.ch.forEach((ch, k) => { ch.plRow = g.ch[k].plRow; ch.vid = null; ch.live = null; });
        if (d.loop && !d.loop.roll && g.loopN != null) d.loop.n = g.loopN;   /* (a LOOP set goes on round from there) */
        cutHist(d.hist, g.t);
        if (beatDeck() === d) cutHist(hist, g.t);
    }
    /* every hand off the deck (a pause, a load, a SWAP's other things): no landing */
    function endHands(d) {
        d.ghost = null; d.reverse = null; d.half = false; d.jump = null;
        if (d.loop && d.loop.roll) d.loop = null;        /* (a LOOP or REPEAT set stays: it plays when the deck plays) */
        d.brake = null; d.backspin = null; d.echoStop = false; d.spin = -1;
        if (d.scratch && d.r) d.r.held = false;
        d.scratch = null;
    }
    const ghosted = d => { if (!d.ghost && d.r) d.ghost = ghostOf(d); };

    /* SCRATCH (specs/ui/screens/dj.md#scratch): the deck's key held, its tempo knob a row a click, the row's
     * ticks spread over the time since the last click; back plays the rows before as if they came next. A
     * playing deck goes on counting and lands when let go; a paused one moves by hand, the rows heard */
    P.djScratchDown = function (d) {
        if (d.song == null) return "empty";
        djOn();
        if (d.braked) { A().djBrake(d.i, now(), false, true); d.braked = false; }
        const h = d.playing ? heardOf(d) : null;
        d.scratch = { at: h ? { pos: h.pos, row: h.row } : Object.assign({}, d.place), last: now() - .2, paused: !d.playing };
        if (d.playing) {
            ghosted(d);
            A().flush(d.idp, now(), now());
            cutHist(d.hist, now());
            if (beatDeck() === d) cutHist(hist, now());
            d.r.held = true;
        } else {
            d.r = deckReplay(d, d.place.pos, d.place.row);
            d.r.step = true;
        }
        return "ok";
    };
    P.djScratchTurn = function (d, dir) {
        const sc = d.scratch, s = dsong(d);
        if (!sc || !s || !d.r) return null;
        const to = stepPlace(s, sc.at.pos, sc.at.row, dir);
        if (!to) return sc.at;
        d.atEnd = false;                                 /* (moved off the song's end by hand) */
        sc.at = to;
        const t = now(), dt = U().clamp(t - sc.last, .02, .4), r = d.r;
        sc.last = t;
        r.breakPos = to.pos; r.nextRow = to.row; r.t = t; r.done = false; r.tick = r.speed;
        replayRow(r);
        for (let k = 1; k < r.speed; k++) { r.t = t + k * dt / r.speed; for (const ch of r.ch) chTick(ch, r); }
        return to;
    };
    P.djScratchUp = function (d) {
        const sc = d.scratch;
        if (!sc) return;
        d.scratch = null;
        if (sc.paused) {
            if (d.r) releaseAll(d.r, now());
            d.r = null;
            d.place = Object.assign({}, sc.at);
        } else land(d);
    };
    /* HOLD (the deck's key, OP2 / PIT): a hand on the record. Down, the deck stops where it is; its tempo knob then
     * moves it by hand, the rows heard (SCRATCH's hand on a paused deck); let go, it plays on from where the hand
     * left it, at once, the time held lost: the way to move a deck against the other. A deck not playing stays paused */
    P.djHoldDown = function (d) {
        if (d.song == null) return "empty";
        const was = d.playing && !d.startAt, loop = d.loop && !d.loop.roll ? Object.assign({}, d.loop) : null;
        if (d.startAt) P.djCancelStart(d);
        if (d.playing) deckPause(d);
        P.djScratchDown(d);
        const sc = d.scratch;
        sc.resume = was;
        sc.from = Object.assign({}, sc.at);
        if (loop) { sc.loop0 = Object.assign({}, loop); d.loop = loop; }   /* a LOOP stays, moved with the hand (a ROLL lands as ever) */
        return "ok";
    };
    /* the hand moved the deck: a loop goes with the head, enabled through the hold. REPEAT bars: the block of bars
     * around where the head is; a free loop: starting at the head; REPEAT PATTERN: the pattern the head is in (as
     * ever). The head back where it was: the loop as it was */
    function holdShift(d) {
        const sc = d.scratch, s = dsong(d);
        if (!sc || !sc.loop0 || !d.loop || !s) return;
        const at = sc.at, l = d.loop;
        if (at.pos === sc.from.pos && at.row === sc.from.row) { l.pos = sc.loop0.pos; l.row = sc.loop0.row; return; }
        l.pos = at.pos;
        l.row = l.bars ? Math.floor(at.row / l.len) * l.len : at.row;
    }
    P.djHoldTurn = function (d, dir) { const at = P.djScratchTurn(d, dir); holdShift(d); return at; };
    /* HOLD's other knob: the deck moved silently, a beat a click (what rang from the hand let go); the place it
     * stands at */
    P.djHoldMove = function (d, beats) {
        const sc = d.scratch, s = dsong(d);
        if (!sc || !s) return null;
        if (d.r) releaseAll(d.r, now());
        for (let k = 0; k < Math.abs(beats) * s.rpb; k++) {
            const to = stepPlace(s, sc.at.pos, sc.at.row, Math.sign(beats));
            if (!to) break;
            sc.at = to;
        }
        d.atEnd = false;
        holdShift(d);
        return sc.at;
    };
    P.djHoldUp = function (d) {
        const sc = d.scratch;
        if (!sc) return;
        const loop = sc.loop0 ? d.loop : null;
        P.djScratchUp(d);
        if (sc.resume) P.djPlay(d, true);
        else djPaused();
        if (loop) {                                      /* the loop goes on, counted from where the hand left the deck */
            const s = dsong(d), l = loop;
            let x = { pos: l.pos, row: l.row }, k = 0;
            while (x && k < l.len && !(x.pos === d.place.pos && x.row === d.place.row)) { x = stepPlace(s, x.pos, x.row, 1); k++; }
            l.n = x && k < l.len ? k : 0;
            d.loop = l;
        }
    };
    /* a LOOP (a tap) or a ROLL (held): from the last beat (1/4 and longer) or the last step of its size, the rows
     * round again; another size from the same start. A ROLL let go, as a LOOP's second tap, plays on from where the
     * loop is, out through its end */
    /* bars: the bars around the place heard, on the pattern's bar grid (4 BARS in bar 6: bars 5-8); else a free loop
     * from the last beat, or the last step of its size, never shorter than LOOP_LEAST (more voices than the FM-1
     * keeps up with: a longer note value instead) */
    const LOOP_LEAST = .15;
    P.djLoop = function (d, beats, roll, bars) {
        const s = dsong(d);
        if (!s || d.startAt || d.atEnd) return false;
        /* playing: around the row heard (played already); paused: set around where the deck stands, to go round
         * once it plays (a pad held there sets it as a tap does) */
        const playing = d.playing && !!d.r, h = playing ? heardOf(d) : null;
        if (playing && !h) return false;
        if (!playing) roll = false;
        const at = playing ? h : d.place, past = playing ? 1 : 0;
        if (!bars) while (beats * 60 / djBpm(d) < LOOP_LEAST) beats *= 2;
        const len = beats * s.rpb;
        const unit = bars ? len : beats >= 1 ? s.rpb : Math.max(1, Math.floor(len));
        if (d.loop) {                                    /* another length: free ones from the same start, bars re-aligned */
            d.loop.len = len; d.loop.beats = beats; d.loop.roll = d.loop.roll || roll; d.loop.bars = !!bars;
            if (bars && at.pos === d.loop.pos) { d.loop.row = Math.floor(at.row / unit) * unit; d.loop.n = at.row - d.loop.row + past; }
            return true;
        }
        const start = Math.floor(at.row / unit) * unit;
        d.loop = { pos: at.pos, row: start, len, n: at.row - start + past, beats, roll, bars: !!bars };
        const next = playing ? stepPlace(s, h.pos, h.row, 1) : null;
        if (next) rewindTo(d, next, h.t + h.dur);
        return true;
    };
    /* a loop kept through a pause or set while the deck stood, counted from where the deck plays from; a place
     * outside it takes it along, as a jump does */
    function loopCount(d) {
        const l = d.loop, s = dsong(d);
        if (!l || !s) return;
        let x = { pos: l.pos, row: l.row }, k = 0;
        while (x && k < l.len && !(x.pos === d.place.pos && x.row === d.place.row)) { x = stepPlace(s, x.pos, x.row, 1); k++; }
        if (x && k < l.len) l.n = k;
        else loopAt(d, d.place);
    }
    /* the pad let go within the hold delay: a LOOP, not a ROLL (it plays on through its end when tapped off) */
    P.djLoopKeep = d => { if (d.loop && d.loop.roll) d.loop.roll = false; };
    P.djLoopOff = function (d) { d.loop = null; };       /* (a LOOP's second tap, a ROLL let go: on from where it is) */
    /* REVERSE and HALF TIME (held): the rows backwards from the one heard, or at half speed; both land */
    P.djReverse = function (d, on) {
        if (!on) { if (d.reverse && !d.backspin) { d.reverse = null; land(d); } return; }   /* (a BACKSPIN's rows are its own) */
        if (!d.playing || !d.r || d.startAt || stopping(d)) return;
        const h = heardOf(d);
        if (!h) return;
        ghosted(d);
        d.reverse = { at: { pos: h.pos, row: h.row } };
        rewindTo(d, { pos: h.pos, row: h.row }, h.t + h.dur);
    };
    P.djHalf = function (d, on) {
        if (!on) { if (d.half) { d.half = false; land(d); } return; }
        if (!d.playing || !d.r || d.startAt) return;
        ghosted(d);
        d.half = true;
    };
    /* the stops (PLAY's pads 5-8), each held (`pad`): the stop runs while the pad is down, its counted place (the
     * ghost) going on meanwhile, and once it has stopped the deck stands silent there (`held`); let go
     * (P.djStopUp), the deck plays on where it would have been (`back`), or, the deck's PLAY key having turned
     * that (P.djStopKeep, a toggle), stays paused where the stop left it.
     * BRAKE: the rows slow and the pitch falls with them, to a stop in 0.5 s */
    const stopping = d => !!(d.brake || d.backspin || d.echo);
    P.djBrake = function (d) {
        if (!d.playing || d.startAt || stopping(d)) return false;
        ghosted(d);
        d.brake = { t0: now(), back: true, pad: true };
        A().djBrake(d.i, d.brake.t0);
        return true;
    };
    /* BACKSPIN: the record spun back by hand: the rows backwards from the one heard (round a LOOP or REPEAT set),
     * SPIN_FAST times as fast and slowing, the pitch high and falling with them (each note forwards, as REVERSE's),
     * to a stop in SPIN_S; PLAY spins up from where it stopped, as after a BRAKE */
    const SPIN_S = 1, SPIN_FAST = 3;
    P.djBackspin = function (d) {
        if (!d.playing || d.startAt || stopping(d) || !d.r) return false;
        const h = heardOf(d);
        if (!h) return false;
        ghosted(d);
        const t0 = h.t + h.dur;
        d.backspin = { t0, back: true, pad: true };
        d.reverse = { at: { pos: h.pos, row: h.row } };
        rewindTo(d, d.reverse.at, t0);
        A().djBrake(d.i, t0, false, false, SPIN_FAST, SPIN_S);
        return true;
    };
    /* ECHO OUT / REVERB OUT: the deck's sound into the DELAY or the REVERB; it stops on the next beat, the tail
     * ringing on for RING */
    const RING = { DELAY: 4, REVERB: 6 };
    P.djEchoOut = function (d, fx = "DELAY") {
        if (!d.playing || d.startAt || stopping(d)) return false;
        ghosted(d);
        d.echo = { until: 0, fx, back: true, pad: true };
        d.echoStop = true;
        delete d.rings[fx];
        A().djEcho(d.i, true, fx);
        return true;
    };
    /* a held stop at its end: the deck silent where the stop left it, its ghost counting on, till the pad is let go */
    function stopHold(d, at, st) {
        if (d.r) { A().flush(d.idp, at); releaseAll(d.r, at); }
        d.halted = true;
        st.held = true;
    }
    /* a stop's pad let go: `back`, the deck plays on where it would have been, spinning up after BRAKE or
     * BACKSPIN, dry under ECHO / REVERB OUT's tail; kept (its PLAY key), paused where the stop leaves it (one
     * still running runs to its end first) */
    P.djStopUp = function (d) {
        const b = d.brake || d.backspin, e = d.echo, st = b || e;
        if (!st || !st.pad) return false;
        st.pad = false;
        if (!st.back) {
            if (st.held) { deckPause(d); if (b) d.braked = true; djPaused(); }
            return true;
        }
        const t = now();
        if (b) { d.brake = d.backspin = null; d.reverse = null; A().djBrake(d.i, t, true); d.spin = t; }
        if (e) { A().djEcho(d.i, "ring", e.fx); d.rings[e.fx] = e.until || t + RING[e.fx]; d.echo = null; d.echoStop = false; }
        d.halted = false;
        land(d);
        return true;
    };
    /* the deck's PLAY key while a stop's pad is held: letting go stays stopped, or (again) plays on; the new
     * state, "kept" or "back", or null (no stop's pad held) */
    P.djStopKeep = function (d) {
        const st = d.brake || d.backspin || d.echo;
        if (!st || !st.pad) return null;
        st.back = !st.back;
        return st.back ? "back" : "kept";
    };
    /* REPEAT PATTERN (LOOPS' pad 1): the deck's current pattern over and over, until switched off; the new state */
    P.djRepeat = d => (d.repeat = !d.repeat);
    /* MOVE's cursor: a bar line of the song, put where the deck is (the bar it is in) and moved by the knobs only */
    P.djMoveStart = function (d) {
        const s = dsong(d), at = s ? P.djState(d).at : null, br = s ? barRowsOf(s) : 1;
        return at ? { pos: at.pos, row: Math.floor(at.row / br) * br } : null;
    };
    /* the cursor pats positions on (the same bar of that position's pattern, or its last), then bars bars; null
     * past the song's ends */
    P.djMoveStep = function (d, at, pats, bars) {
        const s = dsong(d);
        if (!s || !at) return null;
        const pos = at.pos + pats, br = barRowsOf(s);
        if (pos < 0 || pos >= s.order.length) return null;
        const p = M.pattern(s.order[pos]), x = { pos, row: p ? Math.min(at.row, Math.floor((p.rows - 1) / br) * br) : 0 };
        return bars ? stepPlaces(s, x, bars * br) : x;
    };
    /* a jump with a loop running takes the loop along, as HOLD does: REPEAT bars to the block around the place, a
     * free LOOP from it */
    function loopAt(d, to) {
        const l = d.loop;
        if (!l || l.roll) return;
        l.pos = to.pos;
        l.row = l.bars ? Math.floor(to.row / l.len) * l.len : to.row;
        l.n = to.row - l.row;
    }
    /* MOVE let go: playing, to the cursor's bar at the next beat, on the beat of the bar it plays (in phase), the
     * deck playing on; paused, there at once */
    P.djMove = function (d, to) {
        if (!dsong(d) || !to) return false;
        if (d.playing) { d.jump = { to, beat: true }; return true; }
        d.place = Object.assign({}, to);
        d.atEnd = false;
        loopAt(d, d.place);
        return true;
    };
    /* JUMP let go: playing, back to the cue point and on playing, at once after a tap, on the next beat after a
     * hold; paused (or at the song's end), the deck at the cue point and playing */
    P.djJump = function (d, onBeat) {
        if (d.song == null) return false;
        if (d.playing) { d.jump = { cue: true, beat: onBeat }; return true; }
        d.place = Object.assign({}, d.cue);
        d.atEnd = false;
        loopAt(d, d.place);
        return P.djPlay(d) !== "end";
    };

    /* before a deck's row is picked: a pending JUMP or MOVE, a loop going round, REVERSE, ECHO OUT's pause;
     * then SYNC's pull onto the main's beats. false: no row (the deck stops here) */
    function deckBeforeRow(r) {
        const d = r.deck, s = dsong(d);
        if (!s) return false;
        const br = barRowsOf(s);
        /* where the next row is, before this hook's changes */
        const at = () => r.breakPos >= 0 ? { pos: r.breakPos, row: Math.max(0, r.nextRow) } : r.nextRow < 0 ? { pos: r.pos + 1, row: 0 } : { pos: r.pos, row: r.nextRow };
        if (d.echoStop && r.started && at().row % s.rpb === 0) {
            d.echoStop = false;
            d.echo.until = r.t + RING[d.echo.fx];       /* the tail rings on */
            if (d.echo.pad) stopHold(d, r.t, d.echo);   /* (its pad held: silent till let go) */
            else { d.pauseAt = r.t; d.halted = true; }
            return false;
        }
        if (d.loadTo != null && r.started && at().row % br === 0) {   /* a LOAD waiting: the new song from this bar line */
            d.swapAt = r.t;
            d.halted = true;
            return false;
        }
        if (d.reverse) {
            /* backwards, inside a LOOP or REPEAT set: from its start round to its end */
            const cur = d.reverse.at, l = d.loop && !d.loop.roll ? d.loop : null, p = M.pattern(s.order[cur.pos]);
            const to = (l && cur.pos === l.pos && cur.row === l.row ? stepPlaces(s, { pos: l.pos, row: l.row }, Math.max(0, Math.ceil(l.len) - 1))
                       : d.repeat && cur.row === 0 && p ? { pos: cur.pos, row: p.rows - 1 }
                       : stepPlace(s, cur.pos, cur.row, -1)) || cur;
            d.reverse.at = to;
            r.breakPos = to.pos; r.nextRow = to.row;
        } else if (d.jump && (!d.jump.beat || at().row % s.rpb === 0) && at().pos < s.order.length) {
            /* JUMP (to the cue), at this row or this beat; MOVE (to its cursor's bar, this beat of it), at this beat;
             * a loop goes along */
            const to = Object.assign({}, d.jump.cue ? d.cue : d.jump.to);
            if (!d.jump.cue) {
                const p = M.pattern(s.order[to.pos]);
                to.row = Math.min(to.row + at().row % br, p ? p.rows - 1 : 0);
            }
            d.jump = null;
            r.breakPos = to.pos; r.nextRow = to.row; loopAt(d, to);
        } else if (d.loop) {
            if (d.loop.n >= d.loop.len) { r.breakPos = d.loop.pos; r.nextRow = d.loop.row; d.loop.n = 0; }
        } else if (d.repeat && r.started && at().pos !== r.pos) {   /* REPEAT: the pattern again, not the next */
            r.breakPos = r.pos; r.nextRow = 0;
        }
        phaseFix(d, r, at().row);
        return true;
    }
    /* a loop's row counted; one shorter than a row plays the row's first ticks only */
    function deckLoopRow(r) {
        const l = r.deck.loop;
        l.n++;
        if (l.len < 1) r.tick = Math.max(1, Math.round(r.speed * l.len));
    }
    /* SYNC: the right deck's row pulled onto the main's beats (at most half a beat of the finer of the two),
     * from the main's counted place; the main paused, the right only keeps its BPM */
    function phaseFix(d, r, row) {
        const m = DJ.sides[0];
        if (!synced(d) || !m.playing || !m.r || m.startAt || !r.started || ((m.brake || m.backspin) && !m.ghost)) return;   /* (a stop moves only the main: its counted place, or nothing) */
        const H = m.ghost ? m.ghost.hist : m.hist;
        if (!H.length) return;
        let e = H[0];
        for (const x of H) if (x.t <= r.t) e = x;
        const mBeat = (e.row + (r.t - e.t) / e.dur) / e.rpb, rBeat = row / r.rpb / DJ.mult, u = DJ.mult >= 1 ? 1 / DJ.mult : 1;
        let err = (rBeat - mBeat) / u;
        err = (err - Math.round(err)) * u;
        if (Math.abs(err) < .002) return;
        r.t = Math.max(now() + .005, r.t + err * 60 / djBpm(m));
    }

    /* the decks' schedule: each playing deck's rows a little ahead, its counted place up to now, BRAKE's stop,
     * ECHO OUT's pause and its echoes' end, the song's end */
    function djTick(t) {
        for (const d of DJ.decks) {
            const r = d.r;
            if (d.playing && r && !r.held && !d.halted) {
                if (r.t < t - .25 && !d.startAt) r.t = t + .02;
                if (d.startAt && t >= d.startAt) d.startAt = 0;
                let guard = 256, gone = false;
                while (!r.done && !d.halted && r.t < t + AHEAD && guard--) if (!replayTick(r)) { gone = !r.done && !d.halted; break; }
                if (gone) deckEnd(d);                           /* (a pattern missing: nothing more to play) */
                else if (r.done && t >= r.endT) deckEnd(d);
            }
            if (d.swapAt != null) loadAtBar(d, d.swapAt);
            if (d.slide) slideFilter(d, t);
            if (d.ghost) { const g = d.ghost; let guard = 256; while (!g.done && g.t < t && guard--) replayTick(g); }
            if (d.pauseAt != null && t >= d.pauseAt) { deckPause(d, d.pauseAt); djPaused(); }
            /* BRAKE's and BACKSPIN's stop: held, the deck silent there till let go; else paused there */
            for (const [st, dur] of [[d.brake, .5], [d.backspin, SPIN_S]]) {
                if (!st || st.held || t < st.t0 + dur) continue;
                if (st.pad) stopHold(d, st.t0 + dur, st);
                else { deckPause(d, st.t0 + dur); d.braked = true; djPaused(); }
            }
            if (d.echo && d.echo.until && t >= d.echo.until) { A().djEcho(d.i, false, d.echo.fx); d.echo = null; }
            for (const fx in d.rings) if (t >= d.rings[fx]) { A().djEcho(d.i, false, fx); delete d.rings[fx]; }
            while (d.hist.length > 2 && d.hist[1].t <= t - 2) d.hist.shift();
            if (d.ghost) while (d.ghost.hist.length > 2 && d.ghost.hist[1].t <= t - 2) d.ghost.hist.shift();
        }
        lfoTempo();                                      /* (a synced LFO's speed, with the tempo) */
    }
    /* songs moved in the list: the decks' follow; a deleted one leaves its deck empty */
    function djSongs(f) {
        for (const d of DJ.decks) {
            if (d.song == null) continue;
            if (d.loadTo != null) { const k = f(d.loadTo); d.loadTo = k == null ? null : k; }   /* (a LOAD waiting for the bar) */
            const n = f(d.song);
            if (n == null) { if (d.playing) deckPause(d); endHands(d); d.song = null; d.over = null; d.loop = null; d.repeat = false; }
            else d.song = n;
        }
    }
    /* (the screen's view of a deck) */
    P.djState = function (d) {
        const s = dsong(d);
        if (!s) return null;
        const h = d.playing || d.scratch ? heardOf(d) : null, at = d.scratch ? d.scratch.at : h && d.r && d.r.started && !d.startAt ? { pos: h.pos, row: h.row } : d.place;
        const end = d.atEnd && !d.scratch, left = end ? 0 : rowsLeft(s, at), br = barRowsOf(s);    /* (at the end: nothing left, the playhead at the end) */
        return { s, at, end, bar: end ? songRows(s) / br : barOf(s, at), bars: d.over ? d.over.bars.length : Math.ceil(songRows(s) / br), br, beats: beatsOf(s),
                 secsLeft: left * 60 / (djBpm(d) * s.rpb), barsLeft: left / br, rowFrac: h && d.playing ? U().clamp((now() - h.t) / h.dur, 0, 1) : 0 };
    };

    P.time = now;
    return P;
})();

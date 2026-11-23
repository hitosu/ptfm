/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground, sound: the core of the audio worklet. Every dsp-*.js file adds a function to
 * PTFM.dsp; audio.js puts their source text together into one worklet module when the page loads (a
 * page opened from the file system cannot load a worklet from a file), so there is no build: edit,
 * reload. Each function gets E, the worklet's shared object, and must use nothing from the page.
 *
 * Here: the pool, as the firmware's (specs/ui/screens/settings.md#voices): 16 engines, VOICES (8, 12 or 16, 16
 * counted as 12 while the second core does not run: audio.js sends the limit in force) of them taking notes, and 40
 * voices, a voice a note: the notes sounding in an engine and the ghosts, 24 at most. A new note takes a free engine of
 * the count, the first first; else one a ghost's return under way holds; else it takes one from a note: release tails
 * first, the new note's own track's first, then the quietest; then held song notes, the quietest first, a muted
 * track's as silent; the keys' held notes last, the oldest first; a note started since the last block, or fading back
 * in, the last of its kind. The note taken fades out in its engine over 64 frames from the take's frame (as many fades
 * a block as the FM-1 makes in as many of its blocks); a held one goes on as a ghost (not a TB-303's), its voice without
 * an engine: followed by every event as a sounding note (its note off, its pitch and level, its instrument parameters,
 * a legato, a cut), not rendered, and back when an engine is free, its note started again in it and worked forward to
 * where it would be (each engine's own forward work), fading in over 256 frames. A muted track's notes are ghosts from
 * the end of their mute's block, its new notes start as ghosts (not DUCK's source's, not a TB-303's, not the stage's
 * column's). The firmware's rules run on its 64-frame blocks; here, on blocks of 128 frames with events on their own
 * frame, each is counted per 64 frames: 8 returns come back in a block, the returns' budget is twice the FM-1's.
 *
 * Then events placed on their sample (note on / off, a note's pitch and level each tick, a track's instrument
 * parameters and mixer, an FX command, a metronome click), each engine's note rendered into its track's buffer,
 * then the mixer and effects (dsp-fx.js), and the figures the screens show (voices, scopes, levels, steals) posted
 * back about 25 times a second.
 *
 * Instrument parameters (the commands 0x40-0x7F): E.params[engine](p, code, value, proc) writes one
 * into a copy of the patch; a track's stay until its instrument changes, and new notes on the track
 * start with them; its latest note takes them on (an engine's refresh(st) for a sounding one; a ghost's
 * voice keeps them).
 *
 * DJ (specs/ui/screens/dj.md): two decks, one on tracks 0-15, one on 16-31 (deck 0 and deck 1 here,
 * whichever side each is on). The pool's priority: a note finding no free engine takes one from the deck
 * the crossfader favours less, never from the deck heard for a deck faded out; each deck's KEY and BRAKE
 * bend its voices, and its stops (BRAKE, BACKSPIN, the spin up after them) fade them. */
window.PTFM = window.PTFM || {};
PTFM.dsp = (PTFM.dsp || []).concat(function (E) {
    "use strict";
    const SR = sampleRate, BLK = 128, NTRK = 34, KEYS = 33, STAGE = 32;   /* (STAGE: LOOPS' stage's column) */
    /* a key's note's key, k<n> (a FUSION's layers k<n>/i); -1 another's (the arpeggio's "pr", a song's, a lane's) */
    const keyOf = id => { const r = /^k(\d+)(?:\/|$)/.exec(id || ""); return r ? +r[1] : -1; };
    const MOST = 16, NE = 16, NV = 40, NG = NV - NE;   /* VOICES at most (both cores), the engines, voices, ghosts */
    const U = 64, R = BLK / U;          /* the FM-1's block (frames), what its rules count by; its blocks in one of ours */
    const FADES = 8, FADE = U, BACK = 4 * U;   /* fades a FM-1 block at most; a take's fade, a return's fade in (frames) */
    const RETURNS = 4 * R, BUDGET = 44 * R, READY = 15, READY_S = 109, SEEK = 95;   /* returns a block, their work
                                         * (units), the units of a ghost's checks (a sampler's) and of a sampler's seek at
                                         * its first render */
    /* each engine's note start and forward step, in units of about 1 us of the FM-1's time: the firmware's */
    const COST = { DX7: [63, 2], JUNO: [32, 6], SAMPLER: [116, 3], CHIP: [56, 48] };
    /* the load guard: no return while the FM-1's half's work on its busier core, as counted (heavy), is over LOAD_CAP
     * units: each engine of the count with a note in it for the half's 4 blocks at its kind's weight (its block and its
     * mixing in); while the second core runs, the top engines on its side while its share and a fixed allowance for its
     * effects (LOAD_C2, units a block) stay under the first core's, 10 at most (C2_MOST: as the FM-1's block's split
     * starts); the first core's side also each note started and each fade begun in the half, DJ's own work while it
     * plays */
    const LOAD_CAP = 4200, LOAD_NOTE = 115, LOAD_FADE = 64, LOAD_DJ = 400, LOAD_C2 = 128, C2_MOST = 10, HALF_F = 4 * U;
    const LOADW = { DX7: 78, JUNO: 64, SAMPLER: 52, TB303: 76, CHIP: 40 };
    const readyW = (g) => (g.engine === "SAMPLER" ? READY_S : READY);
    const KEEP = Math.round(6891 * U * SR / 44100);   /* a ghost let go kept 10 s (frames) */
    const QUIET = 33 / 32768, QUIET_N = 4;   /* -60 dB: a note let go under it for 4 FM-1 blocks has ended, a ghost let go
                                         * under it is not brought back (its peak on the FM-1's scale: below) */
    const HALF = .5;                    /* the mix takes every note at half (dsp-fx.js's MIX), as the FM-1's voices' gains
                                         * carry it: the peaks the pool compares are there */
    const GHOST = 1, BACKING = 2;       /* a voice without an engine: a ghost; a ghost whose return is under way */
    const GR_LET = 0, GR_MUTED = 1, GR_SONG = 2, GR_KEY = 3;   /* the list's drop, the first first */
    const DEAF = {};                    /* (DJ: a deck faded out finding none of its own to take) */
    E.SR = SR;
    E.BLK = BLK;
    E.U = U;
    Object.assign(E, { BUDGET, READY, READY_S, SEEK, COST, budget: BUDGET, LOAD_CAP, loadCap: LOAD_CAP });   /* (budget,
                                         * loadCap: the ones in force; a test may set others) */   /* (the returns' work, for the tests that bound it) */
    E.engines = {};
    E.dB = v => Math.pow(10, v / 20);
    E.mtof = m => 440 * Math.pow(2, (m - 69) / 12);
    E.KEYS = KEYS;
    E.STAGE = STAGE;
    E.NTRK = NTRK;
    E.params = {};
    E.clone = p => JSON.parse(JSON.stringify(p));
    E.clamp = (v, a, b) => v < a ? a : v > b ? b : v;
    /* a sine table for the engines: 4096 points, read with interpolation */
    const SIN = new Float32Array(4097);
    for (let i = 0; i <= 4096; i++) SIN[i] = Math.sin(i / 4096 * Math.PI * 2);
    E.sin = ph => {                               /* ph in cycles */
        let x = (ph - Math.floor(ph)) * 4096;
        const i = x | 0;
        return SIN[i] + (SIN[i + 1] - SIN[i]) * (x - i);
    };
    /* a note's key among its track's: its id but a FUSION layer's /k (the layers of one note share it) */
    const noteKey = id => (id || "").split("/")[0];
    /* a parameter command into a list of 8, a track's or a note's (the firmware's): turned again, its value; else
     * added, the oldest going when the list is full */
    const parPut = (list, c, x) => {
        const i = list.findIndex(e => e[0] === c);
        if (i >= 0) list[i][1] = x;
        else { if (list.length >= 8) list.shift(); list.push([c, x]); }
    };

    class Proc extends AudioWorkletProcessor {
        constructor() {
            super();
            this.events = [];
            this.sorted = true;
            this.insts = {};
            this.samples = {};
            this.max = 12;
            this.c2 = false;                      /* (the second core runs: its share in the load guard's count) */
            /* the engines: kind (its state's engine, null fresh), st (the state its note renders from), voice (its
             * note's), wait (a ghost whose return there is under way), fading (the fades of taken notes running in it),
             * last (the state its last note left there, { kind, st, cut }: the next note of that engine goes on from it,
             * as the FM-1's engine keeps its state with no note; null where the FM-1 zeroes it, kind 255: a return's
             * start, one given up; cut: its note's sound cut there, as pl_e_cut leaves it, else ended by itself) */
            this.en = Array.from({ length: NE }, () => ({ kind: null, st: null, last: null, voice: null, wait: null, fading: 0, scope: new Float32Array(256), sw: 0 }));
            this.vs = [];                         /* the voices in use: sounding in an engine, and the ghosts */
            this.gl = [];                         /* the ghosts, in the order they were made */
            this.fds = [];                        /* the fades running: a taken note's last 64 frames in its engine */
            this.nfd = 0;                         /* fades begun in this block */
            this.byId = new Map();
            this.age = 0;
            this.ageSeen = 0;                     /* this.age at the last block's end: a note started since has no peak yet */
            this.frame = 0;                       /* frames made: this block's first */
            this.now = 0;                         /* the frame of what is done now (an event's; the block's start; the block's
                                                   * last event's, for the ghosts' ends and returns) */
            this.soon = Infinity;                 /* the soonest end of a ghost's (frames) */
            this.latest = {};                     /* a song's track: its latest note (noteKey), which follows it */
            this.tinst = {};                      /* a song's track: its instrument (what iclear names) */
            this.tfus = {};                       /* ... and that instrument's FUSION pattern (null: not a FUSION) */
            this.edited = new Set();              /* the instruments being edited (the page's) */
            this.nfdNext = 0;                     /* fades another count began between blocks: the next block's */
            this.hl = 0;                          /* (the load guard: the notes started and fades begun in the FM-1's half */
            this.hlH = -1;                        /* hlH, 256 frames, weighed) */
            this.fpats = {};                      /* FUSION patterns' cells as the page sends them (a layer's check at its return) */
            this.tracks = Array.from({ length: NTRK }, () => new Float32Array(BLK));
            this.tmp = new Float32Array(BLK);
            this.tpar = {};                       /* track -> [[code, value] ...]: its instrument's parameters, set by
                                                   * commands (8 at most, the oldest going: the firmware's track list) */
            this.steals = 0;
            this.renamed = 0;                     /* (notes before under an id a new note took: each named on its own) */
            this.wLoud = 0;                       /* (worth's loudness, beside the rank it returns) */
            this.bud = { n: 0 };                  /* (returns' units left in the block; forward's figures of a return:) */
            this.left = { on: 0, off: -1 };       /* (its units still to work, held and released) */
            this.spend = { n: 0 };                /* (its steps the budget allows) */
            this.statsAt = 0;
            this.dj = null;                       /* DJ's mix: {left (0 or 16), xf 0-32, sharp, preview, ...}, null off DJ */
            this.djTaken = 0;                     /* notes that took an engine from the quieter deck */
            this.shift = [0, 0];                  /* each deck's bend now: its KEY and its BRAKE, semitones */
            this.tbend = new Array(32).fill(0);   /* each of the decks' tracks' own bend (semitones from its note: "tbend",
                                                   * a track's "ctl") */
            this.songGain = 1;                    /* PLAYBACK's K3 on the song's voices (tracks 0-15) */
            this.keysGain = 1;                    /* SETTINGS' KEYS GAIN on the keys' voices */
            this.kpress = [];                     /* each key's pressure (ui.js): its own notes' level x it */
            this.brake = [null, null];            /* {t0, up}: a deck's BRAKE (0.5 s down) or its spin up (0.1 s) */
            this.stopG = [[1, 1, 1], [1, 1, 1]];  /* each deck's stop's gain on its voices: the last block's end, then this
                                                   * block's two 64-frame halves' ends (djShift) */
            this.fireT = new Uint8Array(NTRK);    /* FILTER's envelope's sources this block: a track's (or the keys', */
            this.heldT = new Uint8Array(NTRK);    /* KEYS) notes started, held */
            this.alC = -1;                        /* (alloc: the engine the note takes, -1 none) */
            this.alBorn = false;                  /* (alloc: it starts as a ghost) */
            this.fx = E.createFx(this);
            for (const k in E.engines) if (E.engines[k].fresh) E.engines[k].fresh();   /* (what the engines share, afresh) */
            this.port.onmessage = e => this.msg(e.data);
        }
        msg(m) {
            switch (m.t) {
                case "batch": for (const x of m.list) this.msg(x); return;
                case "on": case "off": case "ctl": case "click": case "pulse": case "iparam": case "iclear": case "tmix": case "fxcmd": case "tpunch": case "tbar": case "brake": case "fire":
                case "cutp": case "killp": case "fxempty": case "cut": case "tbend":
                    this.events.push(m); this.sorted = false; return;
                case "dj":
                    this.dj = m.on ? m : null;
                    if (!m.on) { this.brake = [null, null]; this.shift = [0, 0]; this.stopG = [[1, 1, 1], [1, 1, 1]]; }
                    this.fx.msg(m);
                    return;
                case "inst":                     /* (gone: deleted, or no longer an engine's: a ghost of it goes at its return) */
                    if (m.gone) { delete this.insts[m.ref]; return; }
                    this.insts[m.ref] = m;
                    /* (its ghosts on its engine take it as it is now, their own commands on it, a Juno-106's patch copy
                     * excepted: as the firmware reads a ghost's record at each walk and at its return. Nothing walked
                     * now: a sampler ghost's loop goes by it from its next walk or its return, never at the edit itself) */
                    for (const g of this.gl)
                        if (g.ref === m.ref && g.gh !== BACKING && g.engine === m.engine && g.engine !== "JUNO") {
                            g.p = this.build(m, g.par);
                            g.ownP = g.p !== m.p;
                        }
                    return;
                case "editing": this.edited = new Set(m.refs); return;   /* (the instruments being edited: a song's ghost of
                                                  * one comes back with none of its track's commands) */
                case "samples": for (const s of m.list) this.samples[s.key] = { data: Float32Array.from(s.data, v => v / 128), rate: s.rate }; return;
                case "fpat": this.fpats[m.pat] = m.cells; return;   /* (a FUSION pattern's cells: [note, instrument] a row's track) */
                case "voices":                    /* (the limit in force, and whether the second core runs: audio.js; a
                                                   * message without c2 means it runs at 16 alone) */
                    this.now = this.frame;
                    this.c2 = m.c2 !== undefined ? !!m.c2 : m.n > 12;
                    this.count(Math.min(MOST, m.n));
                    return;
                case "press": {                  /* (a key's pressure: its own notes louder, the FM-1's RQ_KEY_PRESS) */
                    this.kpress[m.k] = m.g;
                    for (const v of this.vs) if (v.held && keyOf(v.id) === m.k) v.press = m.g;
                    return;
                }
                case "songgain": this.songGain = m.g; return;   /* (PLAYBACK's K3: the song's voices, not the keys') */
                case "keysgain": this.keysGain = m.g; return;   /* (SETTINGS' KEYS GAIN: the keys' voices alone) */
                /* what was scheduled from `time` on goes; the notes held let go at once, or at `at` (a DJ deck jumping:
                 * its notes end where the new rows start) */
                case "flush": {
                    this.events = this.events.filter(e => !(e.id && e.id[0] === m.prefix && e.time >= m.time));
                    this.now = this.frame;
                    if (m.at != null) {
                        for (const v of this.vs) if (v.held && v.id && v.id[0] === m.prefix) this.events.push({ t: "off", id: v.id, time: m.at });
                        this.sorted = false;
                    } else for (const v of this.vs.slice()) if (v.id && v.id[0] === m.prefix) this.release(v);
                    return;
                }
                /* some notes by their ids (LOOPS' stage: the lanes a key replaces from its landing row): their events
                 * from `time` on go, those held let go there */
                case "flushids": {
                    const ids = new Set(m.ids);
                    this.events = this.events.filter(e => !(e.id && ids.has(e.id) && e.time >= m.time));
                    for (const v of this.vs) if (v.held && ids.has(v.id)) this.events.push({ t: "off", id: v.id, time: m.time });
                    this.sorted = false;
                    return;
                }
                /* notes moved to another track, sounding, ghosts and to come (LOOPS' stage kept as a loop: the loop's; a
                 * muted loop's makes them ghosts) */
                case "retrack": {
                    const ids = new Set(m.ids);
                    for (const v of this.vs) if (ids.has(v.pid || v.id)) { v.track = m.track; v.own = this.own(m.track); }
                    for (const e of this.events) if (e.t === "on" && ids.has(e.id)) e.track = m.track;
                    return;
                }
                case "alloff":                   /* (every note and the effects' tails, as the firmware's: at once, ghosts,
                                                  * the takes' fades and returns under way too; every engine as the cut
                                                  * leaves it, its next note of that engine going on from it: last) */
                    this.events = this.events.filter(e => e.t === "click" || e.t === "pulse");
                    for (const v of this.vs.slice()) this.free(v);
                    for (const e of this.en) { e.voice = e.wait = e.st = e.kind = null; e.fading = 0; }
                    this.fds.length = 0;
                    this.fx.silence();
                    this.fx.msg({ t: "empty" });
                    return;
                default: this.fx.msg(m);
            }
        }

        /* ---- RECORD LIVE's tap (specs/ui/screens/loops.md#record-live): the page's song takes every note that sounds
         * from its schedule; here it is told of those that do not: a note marked `tap` given no engine (started as a
         * ghost, or not played), or losing its engine to a take (by the note n, whose time it is) or past a smaller
         * count. A mute tells nothing (the mute is written), nor a ghost coming back */
        tapSay(id, what, n) { const m = { t: "tap", id, time: n ? n.time : currentTime, by: n && n.id }; m[what] = true; this.port.postMessage(m); }
        tapLost(v, n) { if (v.tap && v.held && !v.cutting) this.tapSay(v.id, "lost", n); }

        /* ---- the pool ---- */
        note(c) { const e = this.en[c]; return e.voice || e.wait; }   /* (engine c's note: its voice's, or a return's) */
        /* the fades a block may begin (takes, and returns into tails' engines): as many as the engines the count leaves
         * unused, 4 at the least, 8 at the most, each FM-1 block */
        fadeMost() { const n = MOST - this.max; return (n < 4 ? 4 : n > FADES ? FADES : n) * R; }
        /* the mixer a track's notes go through (DJ: its deck's): its mutes, its mute's ramp, its DUCK source */
        mixOf(t) { return this.fx.dj && this.fx.decks && t < 32 ? this.fx.decks[t >> 4] : this.fx; }
        /* track t muted (its own mute, another's solo), and not DUCK's source in force: its notes are ghosts. The
         * stage's column (STAGE) and the keys are never muted */
        mutedT(t) { return t < 32 && !!this.fx.mutes[t] && this.mixOf(t).duckSrc !== t; }
        mutedV(v) { return v.track < 32 && this.mutedT(v.track); }
        /* a note silent at its last block's end: its track's mute's ramp at 0 (DUCK's source's, a TB-303's: muted) */
        silentEnd(v) { return v.track < 32 && this.mixOf(v.track).trackGain[v.track] === 0; }
        bendOf(v) { return (v.bendIn || 0) + (v.leg || 0) + (this.dj && v.track < 32 ? this.shift[v.track >> 4] : 0); }   /* (its
                                                  * pitch: the replay's bend, a legato's interval, DJ: its deck's KEY and BRAKE,
                                                  * on every note of the deck, sounding or a ghost) */
        /* the track a note starting now counts as for "the new note's own track's first": the stage's column's, the
         * loop's track it stands for (as the firmware's notes there are that track's); -1 the keys. A note keeps it
         * (v.own), the stage moving on to another loop or not */
        own(t) { return t === KEYS ? -1 : t === STAGE && this.fx.stageCol != null ? this.fx.stageCol : t; }
        /* the 64-frame block a frame is in: the firmware's block clock, what its let-go ages are compared in */
        blk(f) { return Math.floor(f / U); }
        /* how much a note would be missed, the least first: its rank (the return) and its loudness (this.wLoud), as the
         * firmware's pl_worth packs the two (no object made: it is asked for each engine looked at) */
        worth(v, who) {
            let loud = !this.mutedV(v) && v.peak > 0 ? v.peak : 0, rank;
            if (!v.held || v.cutting) rank = who >= 0 && v.own === who ? 0 : 1;
            else if (v.track !== KEYS) rank = 2;
            else { rank = 3; loud = 0; }
            if (v.age > this.ageSeen || v.back) loud = Infinity;
            this.wLoud = loud;
            return rank;
        }
        /* the note to take in the count's engines (of a deck's: deck >= 0; tails: a release tail's alone, sounding in its
         * engine); ties: the oldest. A song's note (who >= 0) never takes a key's note coming back */
        victim(deck, who, tails) {
            let best = null, bRank = 0, bLoud = 0;
            for (let c = 0; c < this.max; c++) {
                const v = this.note(c);
                if (!v || (tails && !this.en[c].voice)) continue;
                if (deck >= 0 && (v.track >= 32 || v.track >> 4 !== deck)) continue;
                if (who >= 0 && v.track === KEYS && v.back) continue;
                const rank = this.worth(v, who), loud = this.wLoud;
                if (tails && rank > 1) continue;
                if (!best || rank < bRank || (rank === bRank && (loud < bLoud || (loud === bLoud && v.age < best.age)))) { best = v; bRank = rank; bLoud = loud; }
            }
            return best;
        }
        /* DJ's crossfader gain of deck d (0: tracks 0-15, 1: 16-31), 0-1; where its LFO has it now (xfNow,
         * dsp-fx.js), else where it is set */
        djGain(d) {
            const dj = this.dj, xf = dj.xfNow != null ? dj.xfNow : dj.xf, x = xf / 32, left = d * 16 === dj.left;
            if (dj.sharp) return left ? (xf <= 30 ? 1 : (32 - xf) / 2) : (xf >= 2 ? 1 : xf / 2);
            return left ? Math.cos(x * Math.PI / 2) : Math.sin(x * Math.PI / 2);
        }
        djHeard(d) { return this.djGain(d) * (this.dj.vol ? this.dj.vol[d] : 1) > 1e-6; }   /* (the crossfader and VOLUME
                                                  * give it some) */
        djLower() {                               /* the deck the crossfader favours less (in the middle, the right) */
            const g0 = this.djGain(0), g1 = this.djGain(1);
            return Math.abs(g0 - g1) < 1e-6 ? (this.dj.left === 0 ? 1 : 0) : g0 < g1 ? 0 : 1;
        }
        /* the note to take for a note of deck me's track who (-1, -1: the keys'), any or a release tail's alone; null
         * none. DJ (specs/ui/screens/dj.md#voices): the deck lower in the mix gives way; a deck faded out takes only its
         * own, never the heard deck's (none of its own: DEAF) */
        choose(me, who, tails) {
            let best = null;
            if (this.dj && me >= 0) {
                const heard = this.djHeard(me), from = heard ? this.djLower() : me;
                best = this.victim(from, who, tails);
                if (!best && from !== me) best = this.victim(me, who, tails);
                if (!best && !heard) return DEAF;
            }
            return best || this.victim(-1, who, tails);
        }

        /* ---- the ghosts' list: in the order made (a ghost going closes the gap); the drop when it is full ---- */
        ghAdd(v) {
            v.gh = GHOST;
            v.e = -1;
            this.gl.push(v);
            this.soon = Math.min(this.soon, v.tEnd);
        }
        ghDel(v) { const i = this.gl.indexOf(v); if (i >= 0) this.gl.splice(i, 1); v.gh = 0; }
        rank(g) { return !g.held ? GR_LET : g.track === KEYS ? GR_KEY : this.mutedV(g) ? GR_MUTED : GR_SONG; }
        /* room for a new ghost of rank `rank` (let go at `off`): full, the one to go is dropped now (the ghosts let go
         * first, let go longest ago first; then the muted, the songs' and loops' held, the keys' held, each a ghost
         * longest first); false: the new one ranks below every ghost, not kept */
        room(rank, off) {
            if (this.gl.length < NG) return true;
            let g = null, gr = GR_KEY + 1, ago = 0;
            const now = this.blk(this.now);                  /* (ages in the FM-1's blocks: let go in one block, a tie) */
            for (const o of this.gl) {
                const r = this.rank(o);
                if (r < gr || (r === GR_LET && now - this.blk(o.tOff) > ago)) { g = o; gr = r; ago = now - this.blk(o.tOff); }
            }
            if (rank < gr || (rank === GR_LET && now - this.blk(off) >= ago)) return false;
            this.free(g);
            return true;
        }

        /* ---- a sampler ghost's place (E.samp, dsp-synth.js), moved on at each change of its pitch by the time at the
         * pitch it had: its end and its place at its return from that one count ---- */
        sampOf(v) { return this.samples[v.p.key]; }
        /* the instrument a sampler ghost's walk and its end read, as the firmware's (pl_v_samp: the record as it is now,
         * the note's commands on it): its patch (v.p, which the "inst" message keeps so), a return under way's built
         * here (its engine keeps its claim's); null if the instrument is gone, no longer a sampler or its sample is not
         * there: no walk, as the firmware's */
        walkP(v) {
            const inst = this.insts[v.ref];
            if (!inst || inst.engine !== "SAMPLER") return null;
            const P = v.gh === BACKING ? this.build(inst, v.par) : v.p;
            return this.samples[P.key] ? P : null;
        }
        walk(v) {                                 /* to now, at the pitch it had, held or not as it stood; out of its loop
                                                   * first if its instrument as it is now has none to play (LOOP OFF, or
                                                   * no room: edited while it waited), as the firmware's; a loop turned on
                                                   * meanwhile never puts it into one. None to read (walkP): its place and
                                                   * its time as they were */
            const P = this.walkP(v);
            if (!P) return;
            const s = this.samples[P.key], n = this.now - v.tPl, F = E.samp.facts(P, s);
            if (!E.samp.loops(P, F)) v.pl.looping = false;
            if (n > 0) E.samp.go(v.pl, F, E.samp.inc(P, s, v.midi, v.pb), n, v.held);
            v.tPl = this.now;
            v.pb = v.bend;
        }
        /* its end by itself from its place now, at its pitch now (tEnd: the frame after the one it ends at); let go, 10 s
         * on at the latest. None to read (walkP): its end as it was (its return drops it) */
        sampEnd(v) {
            const P = this.walkP(v);
            if (P) {
                const s = this.samples[P.key];
                v.tEnd = this.now + E.samp.endFrames(v.pl, E.samp.facts(P, s), E.samp.inc(P, s, v.midi, v.bend), v.held);
            }
            if (!v.held) v.tEnd = Math.min(v.tEnd, v.tOff + KEEP);
            this.soon = Math.min(this.soon, v.tEnd);
        }
        /* a CHIP ghost's two figures from an engine's state st (nb: its blocks made since its note's start): its end
         * while held (its VOL table running out), its release's length once let go (as if let go then); worked out as
         * it becomes a ghost, and again as its return's note starts; its commands meanwhile act from its return */
        chipFig(v, st) {
            const k = E.chip.endBlocks(st, false), h = E.chip.endBlocks(st, true);
            v.crel = k === Infinity ? Infinity : k * U;
            /* (the frame after the 64-frame block it ends in: the block its sounding note would be freed after; ended:
             * now) */
            v.tEnd = h === Infinity ? Infinity : h === 0 ? this.now : v.tOn + U * (st.nb + h);
        }
        /* note v, about to lose its engine (its state st, as rendered up to now), as a ghost: its end by itself (one that
         * has ended in its engine: now) */
        figures(v, st) {
            v.tEnd = Infinity;
            if (v.ended) v.tEnd = this.now;
            else if (v.engine === "SAMPLER" && st && st.s) {
                v.pl = E.samp.of(st);
                v.tPl = this.now;
                v.pb = v.bend;
                this.sampEnd(v);
            } else if (v.engine === "CHIP" && st) this.chipFig(v, st);
        }
        /* note v (no engine, its patch set) as a ghost from its start: a sampler's place where it starts (its SAMPLE
         * OFFSET; out of its loop if its commands take it out: LOOP OFF, or a loop with no room), its end; a CHIP's
         * figures from a note started on a scratch state. False if it cannot sound (its sample not there) */
        ghBorn(v, m) {
            v.tEnd = Infinity;
            if (v.engine === "SAMPLER") {
                const s = this.sampOf(v);
                if (!s) return false;
                v.pl = E.samp.start(v.p, s, m.offset || 0);
                v.pl.looping = !!this.insts[v.ref] && this.loopAtStart(this.insts[v.ref], v.par);   /* (as a sounding note's start) */
                v.tPl = this.now;
                v.pb = v.bend;
                this.sampEnd(v);
            } else if (v.engine === "CHIP") {
                const st = { midi: v.midi, bend: 0 };
                E.engines.CHIP.start(st, v.p, v.midi, m, this);
                this.chipFig(v, st);
            }
            return true;
        }
        /* a ghost let go (its note off): its let-go's frame, its end then (a sampler's by its place, walked held up to
         * now; a CHIP's by its release; 10 s on at the latest). A return under way goes on to its note off at that frame */
        ghostOff(v) {
            if (!v.held) return;
            if (v.engine === "SAMPLER") this.walk(v);
            v.held = false;
            v.tOff = this.now;
            if (v.engine === "SAMPLER") { this.sampEnd(v); return; }
            if (v.engine === "CHIP" && v.crel !== Infinity) v.tEnd = Math.min(v.tEnd, v.tOff + v.crel);
            v.tEnd = Math.min(v.tEnd, v.tOff + KEEP);
            this.soon = Math.min(this.soon, v.tEnd);
        }
        /* a note's pitch now (bend, semitones): a sampler ghost's place on at the pitch it had up to now, at the new one
         * from now (its end again); one with none to read (walkP) due now, so gone at the ghosts' ends (the firmware's
         * walk there finds none); a return under way whose note has started there turns its engine too */
        pitch(v, bend) {
            if (v.e >= 0 || bend === v.bend) { v.bend = bend; return; }
            if (v.engine === "SAMPLER" && v.pl) {
                if (!this.walkP(v)) v.tEnd = this.now;
                this.walk(v); v.bend = v.pb = bend; this.sampEnd(v);
            }
            else v.bend = bend;
            const c = this.backEng(v);
            if (c >= 0) this.en[c].st.bend = bend;
        }
        backEng(v) {                              /* a return under way's engine, its note started there; -1 none */
            if (v.gh !== BACKING) return -1;
            const c = this.en.findIndex(e => e.wait === v);
            return c >= 0 && this.en[c].kind === v.engine ? c : -1;
        }

        /* ---- notes losing and leaving their engines ---- */
        /* the voice free: off its engine, out of the ghosts' list (its return under way given up). Its engine keeps the
         * state it left (last): cut if it still sounded (the FM-1 cuts it: NOTE CUT's fade, the quiet rule, all sound
         * off), else as its note ended */
        free(v) {
            if (v.e >= 0) { const e = this.en[v.e]; if (e.voice === v) { this.leave(e, v, !v.eng.active || v.eng.active(e.st)); } v.e = -1; }
            else if (v.gh) { if (v.gh === BACKING) this.backStop(v); this.ghDel(v); }
            v.gh = 0;
            const i = this.vs.indexOf(v);
            if (i >= 0) this.vs.splice(i, 1);
            if (v.id && this.byId.get(v.id) === v) this.byId.delete(v.id);
        }
        /* ghost v's return under way given up (a new note takes its engine, a command on it, its track muted, a
         * smaller count): its engine free, from zero for the next note; v a ghost in its place in the list, its patch its
         * instrument as it is now (as the "inst" message keeps a ghost's: edits made while its return was under way). Its
         * engine */
        backStop(v) {
            const c = this.en.findIndex(e => e.wait === v);
            if (c >= 0) { const e = this.en[c]; e.wait = null; e.kind = null; e.st = null; e.last = null; }
            v.gh = GHOST;
            const inst = this.insts[v.ref];
            if (inst && inst.engine === v.engine && v.engine !== "JUNO") { v.p = this.build(inst, v.par); v.ownP = v.p !== inst.p; }
            return c;
        }
        /* note v's state leaves engine e (its voice off it), kept there for the engine's next note: cut (its sound cut
         * there: pl_e_cut's) or ended by itself */
        leave(e, v, cut) {
            e.last = { kind: v.engine, st: e.st, cut };
            e.voice = null; e.st = null;
        }
        /* note v gives up its engine c: its last 64 frames fade out there (a fade note: its state, its track, its gain)
         * when it can be heard and the block has a fade left of `most`; else its engine is cut at once (a note never
         * rendered yet; one whose gain is 0, or silent at its last block's end: muted; past the block's fades). True:
         * it fades */
        give(v, c, most) {
            const e = this.en[c];
            const fade = v.heard && v.gMix > 0 && !this.silentEnd(v) && this.nfd < most;   /* (gMix: its gain with its
                                                  * track's LEVEL and DUCK, as the FM-1's) */
            if (fade) {
                this.fds.push({ c, st: e.st, eng: v.eng, kind: v.engine, track: v.track, g0: v.gLast, n: 0 });
                e.fading++;
                this.nfd++;
                this.hlAdd(LOAD_FADE);
            }
            if (e.voice === v) this.leave(e, v, true);   /* (its state the fade's, then cut: the next note there of its
                                                  * engine goes on from it, as the FM-1's after the fade; here the new
                                                  * note starts at once, from it as it stands before the fade) */
            e.kind = null;
            v.e = -1;
            return fade;
        }
        /* the load guard's count of the FM-1's half the frame now falls in (256 frames): w more; heavy: the busier core's
         * count over the cap (the engines of the count split by their weights alone, the second core taking the top ones
         * while it runs, as the firmware's pl_heavy; never the FM-1's own split, which follows its timing) */
        hlAdd(w) {
            const h = Math.floor(this.now / HALF_F);
            if (h !== this.hlH) { this.hlH = h; this.hl = 0; }
            this.hl += w;
        }
        heavy() {
            this.hlAdd(0);
            const w = [];
            let w1 = 0, w2 = 0;
            for (let c = 0; c < this.max; c++) {
                const v = this.en[c].voice || this.en[c].wait;
                w[c] = v ? LOADW[v.engine] : 0;
                w1 += w[c];
            }
            if (this.c2) {                  /* (from the top engine down, 10 at most) */
                for (let c = this.max, n = 0; c-- > 0 && n < C2_MOST;) {
                    if (!w[c]) continue;
                    if (2 * (w2 + w[c]) + LOAD_C2 >= w1 + w2) break;
                    w2 += w[c];
                    w1 -= w[c];
                    n++;
                }
                w2 = 4 * (w2 + LOAD_C2);
            }
            w1 = 4 * w1 + this.hl + (this.dj ? LOAD_DJ : 0);
            return Math.max(w1, w2) > E.loadCap;
        }
        /* note v gives up its engine for a new note (alloc; another count's for the notes above it): give's, so the new
         * note starts there fresh; held (but a TB-303's) it goes on as a ghost if the list keeps it, else it goes, and
         * so does one ended by then (its end come: a CHIP's VOL table run out, a one-shot sample past its END; a full
         * list drops no ghost for it). A ghost's return under way gives its engine up instead (backStop): the ghost
         * stays a ghost, the engine silent. The engine the new note takes in alC */
        take(v, most, n) {
            if (v.gh === BACKING) { this.alC = this.backStop(v); return true; }
            const keep = v.held && !v.cutting && v.engine !== "TB303", c = v.e;
            this.tapLost(v, n);
            if (keep) this.figures(v, this.en[c].st);
            this.give(v, c, most);
            this.alC = c;
            if (keep && !(v.tEnd <= this.now) && this.room(this.rank(v), -1)) { this.ghAdd(v); return true; }
            this.free(v);
            return true;
        }
        /* NOTE CUT, a FUSION layer's ECx, E9x's last start: a ghost goes at once; a sounding note fades out over the 64
         * frames from here (the FM-1's block) and goes, a tail meanwhile for the takes */
        cutNote(v) {
            if (v.e < 0) { this.free(v); return; }
            if (!v.cutting) { v.cutting = true; v.cutF = FADE; }
        }
        /* a song's note that may take no engine starts as a ghost (rank: GR_SONG, or GR_MUTED a muted track's): a
         * TB-303's is not played (it slides from note to note in its engine), nor one ranking below every ghost of a
         * full list */
        born(kind, rank) {
            if (kind === "TB303" || !this.room(rank, -1)) return false;
            this.alBorn = true;
            return true;
        }
        /* an engine for a new note (m: its track, -1 the keys; the stage's column never muted) of engine kind `kind`
         * among the first this.max (alC): a free one, the first first; else one a ghost's return under way holds
         * (silent: of several, the highest-numbered); else one taken. A muted track's note starts as a ghost (alBorn),
         * but a TB-303's and DUCK's source's. The keys' held notes are never taken for a song's note: with every engine
         * a key's, or a key's held ghost waiting for one, the song's note starts as a ghost, to come back after the
         * keys'. False: not played */
        alloc(m, kind) {
            this.alC = -1;
            this.alBorn = false;
            const who = m.track >= 0 ? this.own(m.track) : -1, me = this.dj && who >= 0 && who < 32 ? who >> 4 : -1;
            if (who >= 0 && m.track !== STAGE && kind !== "TB303" && this.mutedT(who)) return this.born(kind, GR_MUTED);
            if (who >= 0 && this.gl.some(g => g.track === KEYS && g.held)) return this.born(kind, GR_SONG);
            let r = null;
            for (let c = 0; c < this.max; c++) {
                const n = this.note(c);
                if (!n) { this.alC = c; return true; }
                if (n.gh === BACKING) r = n;
            }
            if (r) return this.take(r, 0, m);
            const best = this.choose(me, who, false);
            if (best === DEAF) return false;
            if (!best) return who >= 0 ? this.born(kind, GR_SONG) : false;
            if (who >= 0 && best.track === KEYS && best.held && !best.cutting) return this.born(kind, GR_SONG);
            if (me >= 0 && best.track < 32 && best.track >> 4 !== me) this.djTaken++;
            this.steals++;
            return this.take(best, this.fadeMost(), m);
        }
        /* another count: the held notes in the engines above it fade out there and go on as ghosts, in their engines'
         * order (a TB-303's let go; a return under way there given up); their tails ring on where they are. A larger
         * count's free engines take ghosts back as any (returns) */
        count(n) {
            const nfd = this.nfd;                     /* (between blocks: its fades are the next block's) */
            this.nfd = this.nfdNext;
            this.max = Math.max(1, Math.min(MOST, n));
            for (let c = this.max; c < NE; c++) {
                const e = this.en[c];
                if (e.wait) this.take(e.wait, FADES * R);
                const v = e.voice;
                if (!v || !v.held || v.cutting) continue;
                if (v.engine === "TB303") {           /* (let go where it is, its track's link to it cleared: its next
                                                       * note takes an engine of the count; one never heard yet goes) */
                    this.tapLost(v);
                    if (!v.heard) { this.free(v); continue; }
                    this.release(v);
                    v.tbOff = true;
                    continue;
                }
                this.take(v, FADES * R);
            }
            this.alC = -1;
            this.nfdNext = this.nfd;
            this.nfd = nfd;
        }
        /* note v, its track muted (mutedV), its mute's ramp made in this block: it leaves its engine at the block's end
         * and goes on as a ghost, held or let go (10 s after its let-go at the latest), its figures from its engine as
         * the block left it; one whose end has come goes, and so does one let go whose last block peaked under hearing;
         * the list full, as its drop says. Made in their engines' order. The tap is told nothing: the mute is written */
        muteGhost(v) {
            const e = this.en[v.e];
            this.figures(v, e.st);
            if (!v.held) v.tEnd = Math.min(v.tEnd, v.tOff + KEEP);
            this.leave(e, v, true);                   /* (its engine cut: pl_mute_ghost's) */
            v.e = -1;
            if (!(v.tEnd <= this.now) && (v.held || v.pk >= QUIET) && this.room(this.rank(v), v.tOff)) this.ghAdd(v);
            else this.free(v);
        }

        /* ---- ghosts coming back (specs/ui/screens/settings.md#coming-back) ---- */
        /* the ghosts ended by themselves (a one-shot sample past its END, a CHIP note's VOL table or release run out) or
         * let go 10 s ago: gone in the first block after the one their end falls in, where their sounding notes' engines
         * would be freed (a sampler's walked to now first, its end again: gone if its place has ended, whatever frame of
         * the block the walk was made at) */
        ghostEnds() {
            let soon = Infinity;
            for (let i = 0; i < this.gl.length;) {
                const g = this.gl[i];
                if (g.tEnd <= this.frame) {
                    if (g.engine === "SAMPLER" && g.pl && this.walkP(g)) { this.walk(g); this.sampEnd(g); }
                    if (g.tEnd <= this.frame || (g.pl && g.pl.ended)) { this.free(g); continue; }
                }
                soon = Math.min(soon, g.tEnd);
                i++;
            }
            this.soon = soon;
        }
        /* a song's ghost's parameter commands at its return. A note's: its track's as they are, while the track is
         * still on its instrument; none once it took another (a ghost let go then goes); a Juno-106's patch copy holds
         * its own. A FUSION layer keeps its own (its cell's, and its track's that turned it while its note was the
         * track's latest): one of an older note goes; while its note is the latest, with the track on its FUSION, its
         * cell is read again (gone if it no longer holds its note and instrument); with the track on another
         * instrument meanwhile, held it comes back as it is, let go it goes. False: the ghost goes */
        pars(g) {
            const f = g.fcell, tin = this.tinst[g.track];
            if (f) {
                if (noteKey(g.id) !== this.latest[g.track]) return false;
                const pat = this.tfus[g.track];      /* (the track's FUSION's pattern now; none named: the layer's) */
                if (tin !== undefined && pat == null) return g.held;
                const cells = this.fpats[tin === undefined ? f.pat : pat], c = cells && cells[f.row * 16 + f.k];
                return !cells || (!!c && c[0] === f.note && c[1] === g.ref);
            }
            const on = tin === undefined || tin === g.ref;   /* (none named: its own) */
            if (!on && !g.held) return false;
            if (g.engine === "JUNO") return true;
            g.par = [];                               /* (its track's list, none while its instrument is being edited) */
            if (on && !this.edited.has(g.ref)) for (const [c, x] of this.tpar[g.track] || []) parPut(g.par, c, x);
            return true;
        }
        /* ghost g about to come back: true if it can, else it is gone. Its instrument there and still of its kind; a
         * Juno-106 let go whose RELEASE has made it silent by now gone with no work; a sampler's place walked to now; a
         * song's note's parameter commands as they are now (the loops' lanes, the stage's column and the keys keep their
         * own); a sampler's place then out of its loop if its instrument as it comes back has none to play, not ending
         * within this block */
        ready(g) {
            const inst = this.insts[g.ref], eng = g.eng;
            if (!inst || inst.engine !== g.engine ||
                (g.engine === "JUNO" && !g.held && eng.units(this.now - g.tOn, false, g.tOn) - eng.units(g.tOff - g.tOn, true, g.tOn) >= eng.releaseTicks(g.p))) {
                this.free(g);
                return false;
            }
            /* (its instrument as it is now, its commands on top; a Juno-106 keeps its patch copy. A sampler's place walked
             * to now on it first, with the commands it had; a song's commands taken again (pars), the patch built again) */
            const song = !g.key && g.track !== STAGE && !g.lane;
            if (g.engine === "SAMPLER") {
                g.p = this.build(inst, g.par);
                g.ownP = g.p !== inst.p;
                if (!this.sampOf(g)) { this.free(g); return false; }
                this.walk(g);
            }
            if (song && !this.pars(g)) { this.free(g); return false; }
            if (g.engine !== "JUNO" && (song || g.engine !== "SAMPLER")) { g.p = this.build(inst, g.par); g.ownP = g.p !== inst.p; }
            if (g.engine === "SAMPLER") {
                if (!this.sampOf(g)) { this.free(g); return false; }
                /* (out of its loop if its instrument as it comes back, the record now with its commands on it, has none to
                 * play: LOOP OFF, or a loop with no room, edited while it waited or by those commands) */
                if (!E.samp.loops(g.p, E.samp.facts(g.p, this.sampOf(g)))) g.pl.looping = false;
                this.sampEnd(g);
                if (g.tEnd < this.frame + BLK) { this.free(g); return false; }
            }
            return true;
        }
        /* note g started from its voice in the fresh state st: its instrument with its patch, its note, its pitch, a
         * Juno-106's key, a sampler at its place. False if its instrument or sample is gone */
        startFrom(g, st) {
            const inst = this.insts[g.ref];
            if (!inst || inst.engine !== g.engine) return false;
            st.midi = g.midi; st.bend = g.bend;
            g.eng.start(st, g.p, g.midi, { accent: g.accent, offset: 0, vol: g.vel0 }, this);   /* (its own volume at
                                                  * its start: a DX7's modulators') */
            if (g.engine === "SAMPLER") { if (st.dead) return false; E.samp.set(st, g.pl); }
            if (g.engine === "JUNO" && g.jkey !== g.midi) g.eng.key(st, g.jkey);
            return true;
        }
        /* a ghost let go, back in its engine after its forward work, under hearing: the most its note still reaches
         * (its engine's heard) at the most gain its render takes (its own, and PLAYBACK's VOLUME or KEYS GAIN where over
         * 0 dB), under -60 dB */
        quiet(g, st) {
            const x = g.track < 16 ? Math.max(1, this.songGain) : g.track === KEYS ? Math.max(1, this.keysGain) : 1;
            return g.eng.heard(st) * g.gain * g.vol * x * HALF < QUIET;   /* (at the half every note enters the mix at, as
                                                  * the FM-1's voice gains carry it) */
        }
        /* the work a block of ghost g's return under way in engine c, spending from the block's budget bud.n: first its
         * note started there from its voice (a fresh engine), then the engine's forward work over its time (held since its
         * start, or held to its let-go and released since: units of the engine's own, from frames), in pieces under the
         * budget; what it has done in g.done, the rest worked out again each block from its times. Done, it comes back
         * (1): out of the list, bound, fading in; unless it has ended by now, or (let go) it is under hearing: gone (0,
         * not counted). A CHIP's figures are worked out again as its note starts */
        forward(g, c, bud) {
            const e = this.en[c], eng = g.eng, w = COST[g.engine][1], fresh = e.kind !== g.engine;
            if (fresh) {
                bud.n -= COST[g.engine][0];
                e.kind = g.engine;
                e.st = {};
                e.last = null;                   /* (a return's start: from zero, as the FM-1's kind 255) */
                if (!this.startFrom(g, e.st)) { this.free(g); return 0; }
                g.done = 0;
                if (g.engine === "CHIP") {       /* (its figures again, by the commands it came back with, from its start) */
                    this.chipFig(g, e.st);
                    if (!g.held) g.tEnd = Math.min(g.tEnd, g.crel === Infinity ? Infinity : g.tOff + g.crel, g.tOff + KEEP);
                    if (g.tEnd <= this.now) { this.free(g); return 0; }
                    this.soon = Math.min(this.soon, g.tEnd);
                }
            }
            if (bud.n <= 0) return 0;
            /* (its steps, the engine's units, before the one its frame now falls in, on its own grid from its start: those
             * before its let-go held, the rest released; let go within that step itself (`within`), held through the
             * forward work, its note off after resume's step. What is done is counted in g.done, held ones first) */
            const at = this.now - g.tOn, all = eng.units(at, false, g.tOn), letGo = g.held ? Infinity : eng.units(g.tOff - g.tOn, true, g.tOn);
            const within = !g.held && letGo > all;
            const heldN = g.held || within ? all : letGo, relN = g.held || within ? -1 : all - heldN, done = g.done;
            const left = this.left, spend = this.spend;
            left.on = done < heldN ? heldN - done : 0;
            left.off = relN < 0 ? -1 : relN - (done > heldN ? done - heldN : 0);
            spend.n = Math.ceil(bud.n / w);
            const n0 = spend.n;
            const finished = eng.forward(e.st, left, spend);
            bud.n -= (n0 - spend.n) * w;
            g.done = heldN - left.on + (relN < 0 ? 0 : relN - left.off);
            if (!finished) return 0;
            if (g.engine === "SAMPLER") bud.n -= SEEK;
            this.ghDel(g);                        /* (back: out of the list, into its engine) */
            e.wait = null;
            e.voice = g;
            g.e = c;
            g.gh = 0;
            if (!fresh && g.engine === "SAMPLER") {   /* (begun in an earlier block: its place walked to now by its instrument
                                                   * as it is now, its commands on it, as the firmware's; its engine keeps
                                                   * the patch its note started with) */
                const inst = this.insts[g.ref], p0 = g.p;   /* (none walked or set if it is gone, no longer a sampler or
                                                   * its sample is not there: the engine's place as its forward work left
                                                   * it, as the firmware's) */
                if (inst && inst.engine === g.engine) {
                    g.p = this.build(inst, g.par);
                    if (this.walkP(g)) { this.walk(g); E.samp.set(e.st, g.pl); }
                    g.p = p0;
                }
            }
            eng.resume(e.st, at, within, g.tOn);
            if (!g.eng.active(e.st) || (!g.held && this.quiet(g, e.st))) { this.free(g); return 0; }
            g.back = BACK;
            g.peak = 0;
            g.heard = false;
            g.gLast = 0;
            return 1;
        }
        /* at the block's last event's frame (process): the ghosts come back in their order (the keys' held first, then the songs' and loops' held,
         * the keys' let go, the songs' and loops' let go, within each the one made a ghost last first), spending the
         * block's budget on their work in that order, the returns under way among them, and on each ghost's checks; 8 come
         * back a block at most (4 each 64 frames; one dropped counts not). A return begins while the budget lasts: in a
         * free engine of the count, the first first; with none, a held ghost takes a release tail's engine (the tail its
         * new note would take; on DJ the deck rule, a faded-out deck's ghost with none of its own passed by) while the
         * block has a fade left: the tail fades out there, and the return begins in it in the next block. A ghost let go
         * takes only a free engine. A muted track's ghost never comes back while muted (passed by, its return under way
         * given up) */
        returns() {
            const bud = this.bud;                 /* (the block's units left: kept, one for every block) */
            bud.n = E.budget;
            let n = 0, more = true, deaf = 0;     /* (deaf: the decks faded out with none of their own to take, a bit each) */
            for (let k = 0; k < 4; k++)
                for (let i = this.gl.length; i-- > 0;) {
                    const g = this.gl[i];
                    if (!g) continue;
                    const me = g.track < 32 ? g.track >> 4 & 1 : -1;
                    if ((g.held ? 0 : 2) + (g.track === KEYS ? 0 : 1) !== k) continue;
                    if (n >= RETURNS || bud.n <= 0) return;
                    if (g.gh !== BACKING && (!more || (me >= 0 && (deaf >> me & 1)))) continue;
                    if (me >= 0 && this.mutedV(g)) { if (g.gh === BACKING) this.backStop(g); continue; }
                    let c;
                    if (g.gh === BACKING) {       /* (a return under way: on with the budget; one whose note's start is to
                                                   * come, in a tail's engine since its fade: checked again now) */
                        for (c = 0; c < NE && this.en[c].wait !== g; c++);
                        if (c === NE) continue;
                        if (this.en[c].kind === null) {
                            bud.n -= readyW(g);
                            if (!this.ready(g)) continue;
                        }
                        n += this.forward(g, c, bud);
                        continue;
                    }
                    for (c = 0; c < this.max && (this.note(c) || this.en[c].fading); c++);
                    let t = null, fade = false;
                    if (c === this.max) {         /* (none free) */
                        if (k > 1 || this.nfd >= this.fadeMost() || !(t = this.choose(this.dj ? me : -1, g.own, true))) { more = false; continue; }
                        if (t === DEAF) { deaf |= 1 << me; continue; }
                    }
                    bud.n -= readyW(g);
                    if (!this.ready(g)) continue;
                    if (t) { c = t.e; fade = this.give(t, c, FADES * R); this.free(t); }
                    this.en[c].wait = g;
                    g.gh = BACKING;
                    g.done = 0;
                    if (!fade) { this.en[c].kind = null; n += this.forward(g, c, bud); }
                }
        }

        /* ---- notes ---- */
        /* the commands a note starts with: its track's (none while its instrument is being edited), then its own (a
         * FUSION layer's cell's), 8 at most as the firmware's voice keeps them */
        parOf(m) {
            const par = [];
            if (m.track >= 0 && !m.asEdited) for (const [c, x] of this.tpar[m.track] || []) parPut(par, c, x);
            if (m.params) for (const x of m.params) parPut(par, x.c, x.v);
            return par;
        }
        /* an instrument's patch with a list of commands on it */
        build(inst, par) {
            const set = E.params[inst.engine];
            if (!set || !par || !par.length) return inst.p;
            const p = E.clone(inst.p);
            for (const [c, x] of par) set(p, c, x, this);
            return p;
        }
        /* a sampler note's loop at its start, as the firmware's: its instrument's own if that loops with room, then its
         * commands one by one, each taking it out as it comes (the loop rule, E.samp.unloops); none puts it in */
        loopAtStart(inst, par) {
            const s = this.samples[inst.p.key];
            if (!s) return false;
            let p = inst.p, on = E.samp.loops(p, E.samp.facts(p, s));
            if (par && par.length) p = E.clone(p);
            for (const [c, x] of par || []) {
                E.params.SAMPLER(p, c, x, this);
                if (E.samp.unloops(c, p, E.samp.facts(p, s))) on = false;
            }
            return on;
        }
        noteOn(m) {
            const inst = this.insts[m.ref];
            if (!inst || this.fx.hush) { if (m.tap) this.tapSay(m.id, "none", m); return; }   /* (CUT held: no note starts; the
                                                  * time goes on) */
            const eng = E.engines[inst.engine];
            if (!eng) return;
            const track = m.track < 0 ? KEYS : m.track;
            let v = this.byId.get(m.id);
            if (v && v.e >= 0 && v.e < this.max && !v.tbOff && !v.ended && !v.cutting && v.eng === eng && inst.engine === "TB303" && v.ref === m.ref) {
                                                 /* the 303's own engine: legato or retrigger (the same instrument, its
                                                  * engine within the count in force); else the one before let go, ringing
                                                  * out where it is, and the new one allocates, as the firmware's */
                const st = this.en[v.e].st;
                v.par = this.parOf(m);
                v.p = st.p = this.build(inst, v.par);
                v.ownP = v.p !== inst.p;
                eng.note(st, m.midi, m, this);
                v.midi = m.midi;
                v.held = true;
                v.tOff = -1;
                v.vol = m.vol / 127;
                v.bend = v.bendIn = st.bend = 0;
                v.age = ++this.age;
                v.own = this.own(track);
                if (!m.key) this.latest[track] = noteKey(m.id);
                return;
            }
            if (m.leg) {                                 /* LEG (commands.md#legato): the held note goes on at this one, a ghost too */
                const h = this.byId.get(m.leg);
                if (h && h.held && !h.cutting && (h.e < 0 ? !(h.tEnd <= this.now) : !h.ended) && h.ref === m.ref) {
                                                 /* (still held and sounding: not one ended by itself, its engine kept
                                                  * till the next block, nor a ghost whose end has come by the LEG's own
                                                  * frame, listed till a block's ends; the same instrument) */
                    this.byId.delete(m.leg);
                    h.id = m.id;
                    this.byId.set(m.id, h);
                    h.leg = m.midi - h.midi;             /* (its pitch there: a bend on top of the replay's) */
                    h.vol = m.vol / 127;
                    if (h.engine === "JUNO") {           /* (its filter follows the new key) */
                        h.jkey = m.midi;
                        const c = h.e >= 0 ? h.e : this.backEng(h);
                        if (c >= 0) h.eng.key(this.en[c].st, m.midi);
                    }
                    this.pitch(h, this.bendOf(h));
                    if (!m.key && track !== KEYS) this.latest[track] = noteKey(m.id);
                    return;
                }
                if (h) this.release(h);                  /* else a plain note, as any */
                if (!this.fx.hush) this.fireT[m.track < 0 ? KEYS : m.track] = 1;   /* (its FILTER envelope hit) */
            }
            if (v) { if (m.retrig) this.cutNote(v); else this.release(v); }   /* (E9x: its last start fades out and goes; a
                                                  * ghost at once) */
            if (v && this.byId.get(m.id) === v) {        /* (the note before under this id, ringing on or a ghost: a name of
                                                  * its own from now, so the new note is the id's alone and the track's
                                                  * latest test and commands pass the old one by, as the firmware's gen;
                                                  * the screens still see the page's id) */
                this.byId.delete(m.id);
                v.pid = v.pid || v.id;
                v.id = "~" + (++this.renamed) + ":" + v.id;
            }
            if (!m.key && track !== KEYS) this.latest[track] = noteKey(m.id);
            if (!this.alloc(m, inst.engine)) { if (m.tap) this.tapSay(m.id, "none", m); return; }
            this.hlAdd(LOAD_NOTE);                       /* (a note started: the load guard's) */
            const born = this.alBorn, c = this.alC;
            if (m.tap && born) this.tapSay(m.id, "none", m);
            const sh = this.dj && track < 32 ? this.shift[track >> 4] : 0;
            v = { id: m.id, ref: m.ref, eng, engine: inst.engine, track, key: !!m.key, lane: !!m.lane, fcell: m.fcell || null,
                  age: ++this.age, tap: !!m.tap && !born, held: true, cutting: false, vol: m.vol / 127, vel0: m.vel !== undefined ? m.vel : m.vol,
                  press: keyOf(m.id) >= 0 ? this.kpress[keyOf(m.id)] || 1 : 1,
                  gain: E.dB(inst.level || 0) * (E.GAIN[inst.engine] || 1), midi: m.midi, jkey: m.midi, accent: !!m.accent,
                  peak: 0, pk: 0, bend: sh, bendIn: 0, leg: 0, e: -1, gh: 0, tOn: this.now, tOff: -1, tEnd: Infinity,
                  back: 0, heard: false, gLast: 0, gMix: 0, done: 0, pl: null, tPl: 0, pb: 0, crel: Infinity,
                  own: this.own(track) };              /* (the track it counts as, fixed at its start: the stage's loop then) */
            this.vs.push(v);
            this.byId.set(m.id, v);
            v.par = this.parOf(m);                       /* (its commands: what a return builds it from again) */
            v.p = this.build(inst, v.par);
            v.ownP = v.p !== inst.p;
            if (born) {                                  /* (unheard until an engine is free and it is heard) */
                if (!this.ghBorn(v, m)) { this.free(v); return; }
                this.ghAdd(v);
                return;
            }
            const e = this.en[c], last = e.last && e.last.kind === inst.engine ? e.last : null;   /* (its engine's last note
                                                  * of this engine: its state goes on, as the FM-1's pl_eng_start keeps
                                                  * it; another engine's, or none: from zero) */
            e.last = null;
            e.kind = inst.engine;
            e.st = { midi: m.midi, bend: sh };
            e.voice = v;
            v.e = c;
            e.scope.fill(0);
            eng.start(e.st, v.p, m.midi, m, this, last);
            if (inst.engine === "SAMPLER" && !e.st.dead) e.st.looping = this.loopAtStart(inst, v.par);
        }
        /* a parameter command: the track keeps it, and its latest note takes it on (ghosts too) */
        param(ev) {
            parPut(this.tpar[ev.track] = this.tpar[ev.track] || [], ev.code, ev.value);
            const lat = this.latest[ev.track];
            for (const v of this.vs.slice())
                if (v.track === ev.track && !v.key && noteKey(v.id) === lat && E.params[v.engine]) this.paramOn(v, ev.code, ev.value);
        }
        /* a parameter turned on a note, as turning the knob would: on its engine while it has one, and in its voice's
         * patch, which a start from the voice takes (a ghost's return). A sampler ghost's place walked to now first, its
         * loop by the loop rule and its end again after. A return under way starts again */
        paramOn(v, code, value) {
            if (v.gh === BACKING) this.backStop(v);
            parPut(v.par, code, value);
            if (!v.ownP) { v.p = E.clone(v.p); v.ownP = true; }
            const moves = v.gh && v.engine === "SAMPLER" && v.pl && E.samp.moves(code) && !!this.walkP(v);   /* (pl_gh_samp_par's:
                                                  * its instrument there to walk by) */
            if (moves) this.walk(v);
            E.params[v.engine](v.p, code, value, this);
            if (moves) {
                if (E.samp.unloops(code, v.p, E.samp.facts(v.p, this.sampOf(v)))) v.pl.looping = false;
                this.sampEnd(v);
            }
            if (v.e < 0) return;
            const st = this.en[v.e].st;
            if (st.p !== v.p) {
                if (v.engine === "CHIP") E.params.CHIP(st.p, code, value, this);   /* (its own copy of the settings) */
                else st.p = v.p;
            }
            if (v.eng.refresh) v.eng.refresh(st, code, this);
        }
        /* note off: it releases (its let-go's frame kept); a ghost too. It follows its track while it is the latest */
        release(v) {
            if (!v.held) return;
            if (v.e < 0) { this.ghostOff(v); return; }
            v.eng.release(this.en[v.e].st, this);
            v.tOff = this.now;
            v.held = false;
        }
        apply(ev) {
            if (ev.t === "on") return this.noteOn(ev);
            if (ev.t === "fire") { if (!this.fx.hush) this.fireT[ev.track < 0 ? KEYS : ev.track] = 1; return; }
            if (ev.t === "click") return this.fx.click(ev);
            if (ev.t === "pulse") return this.fx.pulse(ev);
            if (ev.t === "iparam") return this.param(ev);
            if (ev.t === "iclear") {             /* (another instrument: its parameters gone) */
                delete this.tpar[ev.track];
                this.tinst[ev.track] = ev.ref;
                this.tfus[ev.track] = ev.fpat != null ? ev.fpat : null;
                return;
            }
            if (ev.t === "cut") { const v = this.byId.get(ev.id); if (v) this.cutNote(v); return; }
            if (ev.t === "tmix" || ev.t === "fxcmd") return this.fx.timed(ev);
            if (ev.t === "tpunch") return this.fx.msg({ t: "punch", k: ev.k, on: ev.on });
            if (ev.t === "tbar") return this.fx.msg({ t: "bar", time: ev.time, deck: ev.deck });       /* GATE's step 1 */
            if (ev.t === "fxempty") return this.fx.msg({ t: "empty" });     /* the effects' tails gone (PLAYBACK's K1) */
            if (ev.t === "brake") { this.brake[ev.deck] = ev.clear ? null : { t0: ev.time, up: !!ev.up, fast: ev.fast || 1, dur: ev.dur || .5 }; return; }
            if (ev.t === "cutp" || ev.t === "killp") {   /* a replay's notes cut (A.cutAt): each faded out over 64 frames and
                                                  * gone, a ghost at once; killp, a little later, any left */
                for (const v of this.vs.slice()) {
                    const id = v.pid || v.id;            /* (a note renamed: by the page's id it had) */
                    if (!id || id[0] !== ev.prefix) continue;
                    if (v.e < 0) { if (ev.t === "cutp") this.free(v); continue; }
                    if (ev.t === "cutp") { if (!v.cutting) { v.cutting = true; v.cutF = FADE; } }   /* (as NOTE CUT's) */
                    else if (v.cutting) { v.kill = true; v.cutting = false; }
                }
                return;
            }
            /* (a song's track's own bend, as its channel's on the FM-1: 0 at its note, then its latest note's ticks; what a
             * DJ deck's KEY or stop puts every note of the deck on) */
            if ((ev.t === "tbend" || ev.t === "ctl") && ev.track >= 0 && ev.track < 32) this.tbend[ev.track] = ev.bend;
            if (ev.t === "tbend") return;
            const v = this.byId.get(ev.id);
            if (!v) return;
            if (ev.t === "off") this.release(v);
            else if (ev.t === "ctl") {
                v.bendIn = ev.bend;
                if (ev.glide > 0) { v.volTo = ev.vol; v.volN = Math.round(ev.glide * SR) + (ev.off || 0); }   /* (a lane's
                                                         * volume alone: there over its row, a share a block) */
                else { v.vol = ev.vol; v.volTo = null; }
                this.pitch(v, this.bendOf(v));
            }
        }

        /* ---- the block ---- */
        /* each engine's note over frames [a, b) into its track's buffer, its peak on the FM-1's scale (its gain with its
         * track's level in the mix: the half, LEVEL, DUCK); a note ended keeps its engine, unrendered, till the voices
         * pass of the next block frees it (process); a cut one fades out over its 64 frames and goes */
        renderVoices(a, b) {
            const tmp = this.tmp;
            for (let c = 0; c < NE; c++) {
                const e = this.en[c], v = e.voice;
                if (!v || v.ended) continue;
                if (v.kill) { this.free(v); continue; }
                const st = e.st;
                st.bend = v.bend = this.bendOf(v);
                st.midi = v.midi;
                tmp.fill(0, a, b);
                const alive = v.eng.render(st, tmp, a, b, this);
                const g = v.gain * v.vol * (v.press || 1) *
                          (v.track < 16 ? this.songGain : v.track === KEYS && !this.fx.keysLoop ? this.keysGain : 1),
                      out = this.tracks[v.track] || this.tracks[KEYS], lv = this.mixOf(v.track).levelOf(v.track),
                      sg = this.dj && v.track < 32 ? this.stopG[v.track >> 4] : null;
                let pk = 0, gl = g;
                for (let i = a; i < b; i++) {
                    gl = v.back > 0 ? g * (1 - v.back-- / BACK) : g;   /* (back: fading in) */
                    if (sg) {                           /* (DJ: its deck's stop, ramped over each 64 frames, as the FM-1's
                                                         * voice gains are) */
                        const h = Math.floor(i / U);
                        gl *= sg[h] + (sg[h + 1] - sg[h]) * (i % U + 1) / U;
                    }
                    if (v.cutF != null) gl *= v.cutF > 0 ? v.cutF-- / FADE : 0;   /* (cut: fading out) */
                    const s = tmp[i] * gl;
                    out[i] += s;
                    e.scope[e.sw++ & 255] = s;
                    const q = s < 0 ? -s : s;
                    if (q > pk) pk = q;
                }
                if (b > a) { v.heard = true; v.gLast = gl; v.gMix = gl * lv; }
                if (pk * lv > v.pk) v.pk = pk * lv;
                if (v.cutF === 0) { this.free(v); continue; }
                if (!alive) { v.ended = true; v.endAt = this.frame; }
            }
            /* the fades: each taken note's last 64 frames from its take, its gain down to 0, into its track; then its
             * engine as its note leaves it (a ghost whose return took a release tail's engine begins there next block) */
            for (let i = 0; i < this.fds.length;) {
                const f = this.fds[i], n = Math.min(b - a, FADE - f.n), e = this.en[f.c];
                if (n > 0) {
                    tmp.fill(0, a, a + n);
                    f.eng.render(f.st, tmp, a, a + n, this);
                    const out = this.tracks[f.track] || this.tracks[KEYS];
                    for (let k = 0; k < n; k++) {
                        const s = tmp[a + k] * f.g0 * (FADE - f.n - k) / FADE;
                        out[a + k] += s;
                        if (!e.voice) e.scope[e.sw++ & 255] = s;
                    }
                    f.n += n;
                }
                if (f.n >= FADE) {
                    this.fds.splice(i, 1);
                    e.fading--;
                    continue;
                }
                i++;
            }
        }
        process(inputs, outputs) {
            const out = outputs[0], L = out[0], R = out[1] || out[0];
            const t0 = currentTime, t1 = t0 + BLK / SR;
            for (const t of this.tracks) t.fill(0);
            this.now = this.frame;
            this.nfd = this.nfdNext;                    /* (another count's fades, begun between blocks: this block's) */
            this.nfdNext = 0;
            if (this.dj) this.djShift(t0);
            for (const v of this.vs) v.pk = 0;
            for (const g of this.gl) { const b = this.bendOf(g); if (b !== g.bend) this.pitch(g, b); }   /* (DJ's KEY, BRAKE) */
            if (!this.sorted) { this.events.sort((x, y) => x.time - y.time); this.sorted = true; }
            let pos = 0;
            while (this.events.length && this.events[0].time < t1) {
                const ev = this.events.shift();
                const off = Math.max(0, Math.min(BLK, Math.round((ev.time - t0) * SR)));
                if (off > pos) { this.renderVoices(pos, off); pos = off; }
                ev.off = off;
                this.now = this.frame + off;
                this.apply(ev);
            }
            /* the FM-1's order: the block's new notes have taken their engines and its fades first; then, at its last
             * event's frame (its first with none), the ghosts' ends and their returns; then the notes that ended in an
             * earlier block leave their engines, as the FM-1's voices pass frees them; then the rest of the block */
            for (const v of this.vs)                    /* (the volumes gliding: a share of the way left, from where the
                                                         * glide began in the block on, ghosts too, as the FM-1's lanes' voices) */
                if (v.volTo != null) {
                    v.vol += (v.volTo - v.vol) * Math.min(1, BLK / v.volN);
                    if ((v.volN -= BLK) <= 0) { v.vol = v.volTo; v.volTo = null; }
                }
            this.now = this.frame + pos;
            if (this.soon <= this.frame) this.ghostEnds();
            if (this.gl.length && !this.fx.hush && !this.heavy()) this.returns();   /* (none while the half is heavy) */
            for (let c = 0; c < NE; c++) { const v = this.en[c].voice; if (v && v.ended && v.endAt < this.frame) this.free(v); }
            if (pos < BLK) this.renderVoices(pos, BLK);
            this.now = this.frame + BLK;
            for (let c = 0; c < NE; c++) {              /* (a note let go under -60 dB for 4 FM-1 blocks has ended: freed) */
                const v = this.en[c].voice;
                if (!v) continue;
                v.peak = v.pk;                          /* (its last block's peak: the takes' loudness; a note that ended
                                                         * this block, this block's, its engine kept till the next) */
                if (v.ended) continue;
                v.quiet = !v.held && v.pk < QUIET ? (v.quiet || 0) + R : 0;
                if (v.quiet >= QUIET_N) this.free(v);
            }
            this.ageSeen = this.age;
            this.heldT.fill(0);                         /* (FILTER's envelope: the notes held, ghosts too) */
            for (const v of this.vs) if (v.held && !v.cutting) this.heldT[v.key ? KEYS : v.track] = 1;
            this.fx.process(this.tracks, L, R, t0);
            /* a muted track's notes, their mute's ramp made: ghosts from the next block (DUCK's source's, a TB-303's,
             * the stage's column's keep their engines) */
            for (let c = 0; c < NE; c++) {
                const v = this.en[c].voice;
                if (v && !v.ended && !v.cutting && v.engine !== "TB303" && this.mutedV(v) && this.silentEnd(v)) this.muteGhost(v);
            }
            this.fireT.fill(0);
            this.frame += BLK;
            if (t0 - this.statsAt > .04) { this.statsAt = t0; this.post(); }
            return true;
        }
        /* each deck's bend: its KEY, and BRAKE's fall (to a stop in 0.5 s, held there), BACKSPIN's (from 3x, high,
         * to a stop in 1 s) or a spin up (0.1 s); and its stop's gain on its voices at each 64-frame half's end */
        djShift(t) {
            for (let d = 0; d < 2; d++) {
                const b = this.brake[d], g = this.stopG[d];
                let s = 0;
                /* (four octaves down at most, stood too; a spin up from a 16th of the speed: the firmware's floors) */
                if (b && !b.up) s = Math.max(-48, 12 * Math.log2(Math.max(1e-4, b.fast * Math.min(1, 1 - (t - b.t0) / b.dur))));
                else if (b) { const k = (t - b.t0) / .1; if (k >= 1) this.brake[d] = null; else s = 12 * Math.log2(Math.max(1 / 16, k)); }
                const was = this.shift[d];
                this.shift[d] = (this.dj && this.dj.key ? this.dj.key[d] : 0) + s;
                /* (a change: every note of the deck, sounding or a ghost, on its track's own bend plus the deck's, as the
                 * firmware's pl_dj_pitch: an older note's own bend and a FUSION layer's arpeggio step dropped, the latest
                 * note's back at its track's next tick) */
                if (this.shift[d] !== was)
                    for (const v of this.vs) if (!v.key && v.track < 32 && v.track >> 4 === d) v.bendIn = this.tbend[v.track];
                g[0] = g[R];
                for (let h = 0; h < R; h++) g[h + 1] = this.stopGain(b, t + h * U / SR);
            }
        }
        /* a deck's stop b's gain on its voices over the 64 frames from t (the firmware's: x the record's speed, 1 to 0
         * as the bend's; BRAKE x, BACKSPIN whole until x is under a tenth, then x 10, both counted at those frames' end;
         * a spin up x, from 1/16, counted at their start); stood, 0 */
        stopGain(b, t) {
            if (!b) return 1;
            if (b.up) return Math.min(1, Math.max(1 / 16, (t - b.t0) / .1));
            const x = Math.min(1, Math.max(0, 1 - (t + U / SR - b.t0) / b.dur));
            return b.fast > 1 ? (x > .1 ? 1 : x * 10) : x;
        }
        /* what the screens read: each engine's sounding note (a ghost has none), the steals, the held ghosts */
        post() {
            const voices = [];
            const count = { DX7: 0, JUNO: 0, TB303: 0, SAMPLER: 0, CHIP: 0 };
            for (let c = 0; c < NE; c++) {
                const e = this.en[c], v = e.voice;
                if (!v || v.ended) continue;
                count[v.engine]++;
                const s = new Float32Array(64);
                for (let i = 0; i < 64; i++) s[i] = e.scope[(e.sw - 256 + i * 4) & 255];
                const track = v.track === KEYS ? -1 : v.track === STAGE && this.fx.stageCol != null ? this.fx.stageCol : v.track;   /* (the stage: its loop's) */
                voices.push({ id: v.pid || v.id, track, engine: v.engine, midi: v.midi, rel: !v.held, scope: s, peak: v.peak,
                              key: v.p && v.p.key, pos: e.st && e.st.pos, ref: v.ref, eng: c });   /* a sample voice: which sample,
                                                                                                * where it plays, its instrument */
            }
            for (const f of this.fds) count[f.kind]++;   /* (a fade: its engine's block rendered once more) */
            const ghosts = this.gl.filter(g => g.held).length;
            this.port.postMessage({ t: "stats", voices, count, steals: this.steals, ghosts, ghostsAll: this.gl.length, max: this.max,
                                    fx: this.fx.stats(), djTaken: this.djTaken });
        }
    }
    E.Proc = Proc;
});

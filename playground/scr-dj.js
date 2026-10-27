/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground: DJ (specs/ui/screens/dj.md): two songs on two decks, mixed live with a
 * crossfader. The left deck is the main; SEQ swaps them. The decks themselves (their replays, tempo, SYNC,
 * the hands, the pads' effects) are the player's (player.js, P.dj...); here the screen: the decks' keys
 * (each deck's five black keys, mirrored, the pads' mode between them), the pads on the white keys, the
 * knobs, the song list, the layout, the LEDs, DJ's properties. Nothing here is written into a song. */
(function () {
    "use strict";
    const D = PTFM.draw, F = D.F, M = PTFM.model, U = PTFM.ui, B = U.B, E = U.E, P = PTFM.player, DJ = P.dj;
    /* the pads' modes: the blank key turns PLAY / LOOPS, LFO switches the LFO pads in and out */
    const MODES = ["PLAY", "LOOPS", "LFO"], LFO_MODE = 2;
    /* LOOPS' pads: PATTERN (the deck's current pattern over and over: REPEAT), then the loops, longest first */
    /* LOOPS' pads: the pattern repeated; the bars around repeated (4, 2, 1, on the pattern's bar grid); free loops
     * from where the deck is (the shorter ones are FX's punch-ins). Held, pads 2-8 ROLL */
    const LOOP_NAMES = ["PATTERN", "4 BARS", "2 BARS", "1 BAR", "2 BARS", "1 BAR", "1/2 BAR", "1/4"];
    const loopWord = (k, roll) => (roll ? "ROLL " : k <= 3 ? "REPEAT " : "LOOP ") + LOOP_NAMES[k];
    /* the black keys (the keys' index, F3 = 0), a job a group: the left deck (OP1-OP3: PLAY, HOLD, FILTER), the
     * LOADs (OP4, OP5), the right deck (OP6, PIT, GLO, in the left's order), SYNC (MONO; POLY beside it does
     * nothing), the pads' mode, PLAY / LOOPS (the blank key). HOLD: a hand on the record, its tempo knob moving it */
    const KEY = { LPLAY: 1, LHOLD: 3, LFILTER: 5, LLOAD: 8, RLOAD: 10, RPLAY: 13, RHOLD: 15, RFILTER: 17, SYNC: 20, MODE: 25 };
    /* PLAY's pads: where the deck plays, 1 SET CUE (the cue set, the deck playing on), 2 CUE (as on a DJ player:
     * held, plays from the cue), 3 JUMP (to the cue, playing on), 4 MOVE (held, the knobs move a cursor by bars
     * and patterns; let go, the deck goes there on the beat) | the stops, the record's 5 BRAKE, 6 BACKSPIN, the
     * tails' 7 ECHO OUT, 8 REVERB OUT. The LFO pads: lfoPad below */
    const PAD = { SET: 0, CUE: 1, JUMP: 2, MOVE: 3, BRAKE: 4, SPIN: 5, ECHO: 6, WASH: 7 };
    const DONE_MS = 300;                                 /* SET CUE lit full this long as it sets */
    const FAST = 150, SLOW = 500;
    /* a LOOPS pad let go within this was a tap (a LOOP), else a ROLL: its own, shorter than the hold delay, so a
     * short stutter rolls */
    const PAD_TAP_MS = 250;
    /* list: the highlighted song; mode: the pads' mode, back: the one LFO came from; page: HOME's mutes' tracks;
     * last: the last knob turned (SEL resets it), or a KEY pad; held: a key's press, its deck and what it started
     * (its release goes to the same deck, a SWAP meanwhile or not); flash: a deck key blinking, refused; taken:
     * the voices taken from the quieter deck, as last seen */
    const dj = { list: -1, mode: 0, back: 0, page: 0, last: null, held: {}, flash: {}, done: {}, k23: 0 };
    /* what K2 / K3 turn, ENV's cycle (FILTER at first; more can join it) */
    const K23 = ["FILTER", "VOLUME"];

    const songs = () => M.state.songs;
    const L = () => DJ.sides[0], R = () => DJ.sides[1];
    const sideName = d => d === L() ? "LEFT" : "RIGHT";
    const now = () => U.now();
    const dsong = d => d.song != null ? songs()[d.song] : null;
    const beatsOf = s => ((s.fx.knobs || {}).METRONOME || [])[1] || 4;
    const loopBeats = (s, k) => k === 7 ? 1 : [0, 4, 2, 1, 2, 1, .5][k] * beatsOf(s);   /* (pads 2-8) */
    const tracksOf = s => Math.max(0, ...s.order.map(x => (M.pattern(x) || { tracks: 0 }).tracks));
    /* OCT- / OCT+: HOME's mutes' tracks, 00-07 or 08-0F */
    const pages = () => Math.max(1, ...DJ.decks.map(d => dsong(d) ? Math.ceil(tracksOf(dsong(d)) / 8) : 1));
    /* a MOVE pad held for the deck: its cursor, patterns and bars from where the deck is */
    const moving = d => Object.values(dj.held).find(h => h && h.move && h.d === d);
    const refuseKey = n => { dj.flash[n] = now() + 600; };
    const noSong = n => { refuseKey(n); U.toast("NO SONG ON THE DECK"); };
    const fmtBpm = v => v.toFixed(1);
    const change = d => { const own = P.djOwnBpm(d); if (!own) return ""; const c = (P.djBpm(d) / own - 1) * 100; return (c < 0 ? "-" : "+") + Math.abs(c).toFixed(1) + "%"; };
    const filterWord = v => M.FX.FILTER.knobs[0].fmt(v);
    const xfWord = () => DJ.xf === 16 ? "MIDDLE" : (DJ.xf < 16 ? "L " : "R ") + Math.round(Math.abs(DJ.xf - 16) / 16 * 100) + "%";

    /* ---------------------------------------------------------------- the decks' keys --- */
    function playKey(d, n) {
        if (d.song == null) return noSong(n);
        if (d.startAt) { P.djCancelStart(d); return; }           /* a second tap: the synced start cancelled */
        if (P.djStopKeep(d)) return "kept";              /* a stop's pad held: letting go stays stopped, or plays on (the footer) */
        if (P.djCueKeep(d)) return "kept";               /* CUE held playing: letting go plays on, or goes back to the cue */
        if (d.playing) { P.djPause(d); return; }
        if (P.djPlay(d) === "end") { refuseKey(n); U.toast("THE END: CUE GOES BACK"); }
    }
    const cueToast = d => U.toast("CUE: " + cursorWord(d, d.cue));
    function cueKey(d, n) {
        const r = P.djCueDown(d);
        if (r === "empty") return noSong(n);
        if (r === "set") cueToast(d);
    }
    /* LOAD: on a playing deck at its next bar line, the tempo kept; held past the hold delay, the song's own */
    function loadKey(d, n) {
        if (dj.list < 0 || !songs()[dj.list]) { refuseKey(n); return null; }
        const r = P.djLoad(d, dj.list), name = songs()[dj.list].name;
        if (r === "cancel") { U.toast(sideName(d) + ": LOAD CANCELLED"); return null; }
        if (r === "empty") { refuseKey(n); U.toast("SONG " + M.hex(dj.list) + " IS EMPTY"); return null; }
        U.toast(sideName(d) + ": " + name + (r === "atbar" ? " AT THE BAR" : ""));
        if (r !== "atbar") return null;
        const h = { d }, i = dj.list;
        h.timer = setTimeout(() => {
            if (dj.held[n] !== h || (d.loadTo !== i && d.song !== i)) return;   /* (still waiting, or landed: not
                                                                                 * dropped by a pause) */
            P.djLoadOwnTempo(d);
            U.toast(sideName(d) + ": " + name + " AT ITS TEMPO");
        }, U.set("HOLD DELAY"));
        return h;
    }
    function holdKey(d, n, down) {                               /* HOLD: the deck stops while held */
        if (down) {
            if (P.djHoldDown(d) === "empty") { noSong(n); return null; }
            return d;
        }
        P.djHoldUp(d);
        return null;
    }
    /* FILTER: on and off from the press (so it lands on the beat), sliding while the key is held; let go, it is
     * there: a quick press switches at once, a held one slides */
    function filterKey(d, n) {
        const r = P.djFilterSwitch(d);
        if (r === "empty") { noSong(n); return null; }
        U.toast("FILTER " + sideName(d)[0] + (r === "on" ? " ON " + filterWord(d.filterSet) : " OFF"));
        return { d, filter: true };
    }

    /* the LFO pads (specs/ui/screens/dj.md#the-lfo-pads), groups of four across both decks' keys: 1-4 the
     * crossfader's LFO (on / off, shape, range, reverse); 5-7 the left deck's filter's (on / off, shape, range), 8
     * its KEY (held: K1 turns it); 9-12 the right deck's the same (K4); 13-15 the FX LFO's (the MIX of the effect
     * DJ's properties pick, REVERB at first), 16 every LFO off. An on / off pad held: ALG sets that LFO's
     * frequency; let go, a running one goes off, unless ALG turned meanwhile */
    const LFO_RANGES = { FADER: 4, FILTER: 4, FX: 2 };
    const lfoName = g => g === 0 ? "FADER" : g === 3 ? "FX" : "FILTER";
    const lfoDeck = g => g === 1 ? L() : g === 2 ? R() : null;
    const lfoLabel = (name, d) => name === "FILTER" ? "FILTER " + sideName(d)[0] : name === "FX" ? P.djLfoFx() : name;
    const hzWord = l => P.djLfoSpeed(l);                 /* (a note length of the tempo: 1/2, 4 BARS) */
    function rangeWord(name, l) {
        if (name === "FADER") return l.range === 3 ? "MIRROR" : l.range === 2 ? "SWING" : l.range ? (l.rev ? "R TO L" : "L TO R") : (l.rev ? "HERE TO R" : "L TO HERE");
        if (name === "FILTER") return ["OFF TO HERE", "OFF TO FULL", "HERE TO FULL", "SWING"][l.range];   /* (FULL: its side's end) */
        return l.range ? "0 TO 100 %" : "HERE TO 100 %";               /* (the FX LFO's MIX) */
    }
    const keyWord = d => d.key ? (d.key > 0 ? "+" : "") + d.key : "0";
    /* where the LFOs have things now, as the sound reports it: the crossfader, a deck's filter (its row), the FX
     * LFO's MIX, in the footer before the voices, short (`REV 62%`; `REV OFF` while DJ's FX has the effect off: the
     * LFO does nothing then) */
    const lfoNow = () => P.what === "dj" && PTFM.audio.stats.djLfo;
    const FX_SHORT = { REVERB: "REV", DELAY: "DLY", CHORUS: "CHO" };
    function fxWord() {
        const name = P.djLfoFx(), a = lfoNow();
        if (!M.state.dj.fx.on[name]) return FX_SHORT[name] + " OFF";
        return FX_SHORT[name] + (a && a.mix != null ? " " + Math.round(a.mix * 100) + "%" : "");
    }
    /* the on / off pad held last, ALG's; a deck's KEY pad held */
    const lfoHeld = () => Object.values(dj.held).filter(h => h && h.lfo).sort((a, b) => a.t - b.t).pop();
    const keyHeld = d => Object.values(dj.held).find(h => h && h.key && h.d === d);
    /* an LFO switched on: its shape and speed (FADER LFO: TRIANGLE 4 BARS); with its filter or effect off, that it
     * does nothing (its speed turned shows as a knob: FADER LFO 4 BARS) */
    function lfoToast(h) {
        const l = h.lfo, label = lfoLabel(h.name, h.d);
        U.toast(label + " LFO: " + (h.name === "FILTER" && !h.d.filterOn ? "ITS FILTER IS OFF"
                                    : h.name === "FX" && !M.state.dj.fx.on[label] ? label + " IS OFF"     /* (FX, DJ's: it does nothing then) */
                                    : P.LFO_SHAPES[l.shape] + " " + hzWord(l)));
    }
    function lfoPad(wi, down, h) {
        const n = U.WHITE[wi], g = wi >> 2, k = wi & 3, name = lfoName(g), d = lfoDeck(g), mode = LFO_MODE;
        if (!down) {
            if (h && h.lfo && h.wasOn && !h.turned) { P.djLfoOn(h.lfo); U.toast(lfoLabel(h.name, h.d) + " LFO OFF"); }
            return null;
        }
        if (g === 3 && k === 3) { P.djLfosOff(); dj.done[n] = now() + DONE_MS; U.toast("ALL LFOS OFF"); return { mode }; }
        if (d && d.song == null) { noSong(n); return null; }
        if (d && k === 3) { U.showKnob("KEY " + sideName(d)[0], keyWord(d)); return { d, mode, key: true }; }   /* KEY: held, its knob turns it */
        const l = P.djLfo(name, d), label = lfoLabel(name, d);
        if (k === 0) {                                           /* on at the press; a running one off at the release */
            const wasOn = l.on;
            const h = { d, mode, lfo: l, name, wasOn, turned: false, t: now() };
            if (!wasOn) { P.djLfoOn(l); lfoToast(h); }
            return h;
        }
        if (k === 1) P.djLfoShape(l);
        else if (k === 2) P.djLfoRange(l, LFO_RANGES[name]);
        else P.djLfoRev(l);                                      /* (pad 4: the crossfader's reverse) */
        U.toast(label + ": " + (k === 1 ? P.LFO_SHAPES[l.shape] : rangeWord(name, l)));
        return { d, mode };
    }

    /* the pads (specs/ui/screens/dj.md#the-pads): a deck's 8 white keys in the pads' mode; the LFO pads across both */
    function pad(wi, down, h) {
        if ((h ? h.mode : dj.mode) === LFO_MODE) return lfoPad(wi, down, h);
        const d = h ? h.d : wi < 8 ? L() : R(), k = wi % 8, s = dsong(d), mode = h ? h.mode : dj.mode;
        if (!s) return null;
        if (mode === 0) {                                        /* PLAY: CUE, SET CUE, JUMP, MOVE | the stops */
            if (k === PAD.CUE) {                                 /* CUE: as a DJ player's, held: plays from the cue */
                if (down) { cueKey(d, U.WHITE[wi]); return { d, mode, cue: true }; }
                if (h && h.cue) P.djCueUp(d);
                return null;
            }
            if (k === PAD.SET) {                                 /* SET CUE: the cue point here, the deck playing on */
                if (!down) { if (h) clearTimeout(h.timer); return null; }
                const mv = moving(d), n = U.WHITE[wi];
                /* the place the press found: held past PAD_TAP_MS, the cue goes to the start of its beat (as JUMP held
                 * goes on the beat); a tap keeps the bar line */
                const at0 = d.scratch ? d.scratch.at : d.playing ? P.djState(d).at : Object.assign({}, d.place);
                if (mv && mv.move.at) P.djCueAt(d, mv.move.at);  /* MOVE's cursor out: the cue there (MOVE still moves) */
                else if (!P.djCueSet(d)) { refuseKey(n); U.toast("THE END: CUE GOES BACK"); return null; }
                dj.done[n] = now() + DONE_MS;
                cueToast(d);
                const h2 = { d, mode, set: true };
                if (!(mv && mv.move.at)) h2.timer = setTimeout(() => {
                    if (dj.held[n] !== h2 || !P.djCueBeat(d, at0)) return;
                    h2.beat = true; dj.done[n] = now() + DONE_MS; cueToast(d);
                }, PAD_TAP_MS);
                return h2;
            }
            if (k === PAD.JUMP) {                                /* JUMP: let go, to the cue: a tap at once, a hold on the beat */
                if (down) return { d, mode, t: now(), jump: true };
                if (h && !P.djJump(d, now() - h.t >= PAD_TAP_MS)) refuseKey(U.WHITE[wi]);
                return null;
            }
            if (k === PAD.MOVE) {                                /* MOVE: held, the knobs move a cursor; let go, there on the beat */
                if (down) return { d, mode, move: { at: P.djMoveStart(d), turned: false } };   /* (shown on the bar the deck is in) */
                if (h && h.move && h.move.turned && !P.djMove(d, h.move.at)) refuseKey(U.WHITE[wi]);
                return null;
            }
            /* the stops, one at a time: held, it stops; let go, it plays on where it would have been (its PLAY key
             * meanwhile: it stays stopped) */
            if (!down) { if (h && h.stop) P.djStopUp(d); return null; }
            const ok = k === PAD.BRAKE ? P.djBrake(d) : k === PAD.SPIN ? P.djBackspin(d) : P.djEchoOut(d, k === PAD.WASH ? "REVERB" : "DELAY");
            if (!ok) { refuseKey(U.WHITE[wi]); return null; }
            return { d, mode, stop: true };
        }
        if (mode === 1 && k === 0) {                             /* LOOPS' PATTERN: the deck's current pattern over and over */
            if (!down) return null;
            if (!d.repeat && d.loop) P.djLoopOff(d);             /* (one loop at a time: PATTERN is the longest) */
            U.toast(sideName(d) + (P.djRepeat(d) ? ": REPEAT PATTERN ON" : ": REPEAT PATTERN OFF"));
            return { d, mode };
        }
        /* LOOPS: a tap a LOOP, held a ROLL */
        if (down) {
            if (d.repeat) P.djRepeat(d);                         /* (a loop instead of the pattern's repeat) */
            if (d.loop && d.loop.idx === k && !d.loop.roll) { P.djLoopOff(d); return { d, mode, off: true }; }
            if (!P.djLoop(d, loopBeats(s, k), !d.loop, k <= 3)) return null;   /* (pads 2-4: the bars around; 5-7 free) */
            d.loop.idx = k;
            return { d, mode, t: now(), fresh: d.loop.roll };
        }
        if (!h || h.off || !d.loop || d.loop.idx !== k) return null;
        if (h.fresh && now() - h.t < PAD_TAP_MS) P.djLoopKeep(d);                 /* a tap: it loops on */
        else if (h.fresh) P.djLoopOff(d);                                          /* a ROLL let go: on from the loop */
        return null;
    }
    /* MOVE held: its cursor, a bar line put where the deck is at the first turn, then moved by the knobs only (the
     * deck plays on under it): a click a bar (the tempo knob) or a pattern (the filter knob), never past the song's
     * ends */
    /* a place as POS and BAR, and BEAT (from 0, as the bar) when it is not on a bar line (SET CUE held) */
    function cursorWord(d, at) {
        const s = dsong(d), br = s.rpb * beatsOf(s), beat = Math.floor((at.row % br) / s.rpb);
        return "POS " + M.hex(at.pos) + " BAR " + M.hex(Math.floor(at.row / br), 1) + (beat ? " BEAT " + beat : "");
    }
    function moveTurn(d, h, pats, bars) {
        const m = h.move;
        if (!m.at) m.at = P.djMoveStart(d);
        for (let k = 0; k < Math.abs(pats || bars) && m.at; k++) {
            const x = P.djMoveStep(d, m.at, Math.sign(pats), Math.sign(bars));
            if (!x) break;
            m.at = x;
            m.turned = true;                                 /* (let go without a turn: nothing moves) */
        }
        if (m.at) U.showKnob("MOVE " + sideName(d)[0], cursorWord(d, m.at));
    }

    /* ------------------------------------------------------------------- the knobs --- */
    function tempo(d, s, coarse) {
        const r = P.djTempo(d, s * (coarse ? 10 : 1));
        if (r === "synced") { U.toast("SYNC: TEMPO ON THE MAIN DECK"); return; }
        if (r !== "ok") return;
        dj.last = { k: "tempo", d };
        U.showKnob("TEMPO " + sideName(d)[0], fmtBpm(P.djBpm(d)) + " BPM " + change(d));
    }
    function filter(d, s, coarse) {
        if (d.song == null) return;
        P.djFilter(d, s * (coarse ? 10 : 1));                   /* (heard while its FILTER key has it on) */
        dj.last = { k: "filter", d };
        U.showKnob("FILTER " + sideName(d)[0], filterWord(d.filterSet) + (d.filterOn ? "" : " (OFF)"));
    }
    const volWord = v => v <= P.VOL_OFF ? "OFF" : (v > 0 ? "+" : "") + v + " DB";
    function volume(d, s, coarse) {                             /* (ENV's VOLUME: a dB a click; SEL held 6) */
        if (d.song == null) return;
        P.djVol(d, d.vol + s * (coarse ? 6 : 1));
        dj.last = { k: "vol", d };
        U.showKnob("VOLUME " + sideName(d)[0], volWord(d.vol));
    }
    /* SEL: the last knob turned back to its default (LFO's job elsewhere; here LFO is the LFO pads) */
    function resetKnob() {
        const x = dj.last;
        if (!x) return;
        if (x.k === "xf") { P.djXf(16 - DJ.xf); U.showKnob("X-FADER", xfWord()); }
        else if (x.k === "tempo" && x.d.song != null) { P.djTempoReset(x.d); U.toast("TEMPO " + fmtBpm(P.djBpm(x.d)) + " BPM: THE SONG'S"); }
        else if (x.k === "filter") { P.djFilter(x.d, -x.d.filterSet); U.showKnob("FILTER " + sideName(x.d)[0], filterWord(0)); }
        else if (x.k === "vol") { P.djVol(x.d, 0); U.showKnob("VOLUME " + sideName(x.d)[0], volWord(0)); }
        else if (x.k === "key") { P.djKey(x.d, 0); U.toast("KEY 0"); }
        else if (x.k === "lfo") { P.djLfoReset(x.h.lfo); U.showKnob(lfoLabel(x.h.name, x.h.d) + " LFO", hzWord(x.h.lfo)); }
    }

    /* ---------------------------------------------------------------- properties --- */
    function props() {
        const S = M.state.dj, set = (k, v) => { U.edit(() => { S[k] = v; }); P.djMix(); };
        const step = (list, v, d) => list[U.clamp(list.indexOf(v) + Math.sign(d), 0, list.length - 1)];
        U.props("DJ PROPERTIES", [
            { name: "CROSSFADER", get: () => S.xfader, change: d => set("xfader", step(M.DJ_XFADERS, S.xfader, d)) },
            { name: "FILTER RESONANCE", get: () => S.reso + " %", change: (d, c) => set("reso", U.clamp(S.reso + d * (c ? 10 : 1), 0, 100)) },
            { name: "FILTER THE SENDS", get: () => S.filterSends, change: d => set("filterSends", d > 0 ? "ON" : "OFF") },
            { name: "FX LFO", get: () => S.lfoFx || "REVERB MIX", change: d => set("lfoFx", step(M.DJ_LFO_FX, S.lfoFx || "REVERB MIX", d)) },
            /* the stand back to a new store's, after a question: both decks empty, all as at first (the setup kept) */
            { name: "RESET DJ", get: () => "EDIT", col: () => D.YELLOW,
              edit: () => U.ask(["RESET DJ?", "BOTH DECKS EMPTIED, ALL AS AT FIRST"], ["RESET", "KEEP"], a => {
                  if (a !== "RESET") return;
                  P.djRestore(null);
                  U.toast("DJ RESET");
              }) },
        ]);
    }

    /* ------------------------------------------------------------------- drawing --- */
    const Y = { song: 19, tempo: 31, place: 48, over: 61, beats: 79, left: 77, state: 91, filt: 104, set: 117, xf: 131, rule: 144, cols: 146, list: 158 };
    const LIST_ROWS = 5;
    /* a key or pad held shows what it does, as the hands' and the stops' words do: CUE, SET CUE, JUMP; the deck's
     * PLAY key (past a tap: what holding it does), its FILTER key (the slide) */
    function heldWord(d) {
        for (const h of Object.values(dj.held)) {
            if (!h || h.d !== d) continue;
            if (h.mode === 0 && h.cue) return d.cueHeld ? "CUE PLAY" : "CUE";
            if (h.mode === 0 && h.set) return h.beat ? "SET CUE ON THE BEAT" : "SET CUE";
            if (h.mode === 0 && h.jump) return now() - h.t < PAD_TAP_MS ? "JUMP" : "JUMP ON THE BEAT";
            if (h.play && (h.fired || now() - h.t >= PAD_TAP_MS)) return h.fired ? "BACK TO THE START" : "HOLD: TO THE START";
            if (h.filter) return d.filterOn ? "FILTER ON " + filterWord(d.filterSet) : "FILTER OFF";
            if (h.key) return "KEY";
            if (h.lfo) return "LFO " + hzWord(h.lfo);                /* (its filter's on / off pad: ALG its speed) */
        }
        return null;
    }
    function stateLine(d, st) {
        if (d.loadTo != null) return d.loadOwn ? "LOADS AT ITS TEMPO" : "LOADS AT THE BAR";
        if (d.scratch) return "HELD";
        const mv = moving(d);
        if (mv && mv.move.at) return "MOVE " + cursorWord(d, mv.move.at);   /* (the cursor's place) */
        const hw = heldWord(d);
        if (hw) return hw;
        if (d.loop && d.loop.roll) return loopWord(d.loop.idx, true);   /* (a ROLL's while held; a loop set: the set line) */
        if (d.brake) return "BRAKE";
        if (d.backspin) return "BACKSPIN";
        if (d.echo) return d.echo.fx === "REVERB" ? "REVERB OUT" : "ECHO OUT";
        if (d.startAt) return "STARTS AT THE BAR";
        if (d.jump) return d.jump.cue ? "JUMP ON THE BEAT" : "MOVE ON THE BEAT";
        if (st.end) return "AT THE END";
        if (P.djAtCue(d)) return "AT THE CUE";
        return null;
    }
    function warning(d, st) { return st.s.atEnd === "STOP" && d.playing && st.barsLeft <= 8; }
    function deck(d, x0) {
        const st = P.djState(d), hint = d === L() ? "OP4" : "OP5";
        if (!st) {
            D.text(x0, Y.song, F.T, "NO SONG", D.DGREY);
            D.runs(x0, Y.place, F.T, [[hint, D.YELLOW], [": LOAD THE", D.LGREY]]);
            D.text(x0, Y.over, F.T, "HIGHLIGHTED SONG", D.LGREY);
            return;
        }
        const s = st.s;
        D.text(x0, Y.song, F.T, M.hex(d.song), D.DGREY);
        D.text(x0 + 18, Y.song, F.T, s.name.slice(0, 15), d.playing ? D.GREEN : D.WHITE);
        volBar(d, x0);
        const bpm = fmtBpm(P.djBpm(d)).padStart(5);
        D.text(x0 + 40 - D.tw(F.S, bpm), Y.tempo, F.S, bpm, D.WHITE);
        D.text(x0 + 44, Y.tempo + 3, F.T, "BPM", D.LGREY);
        if (P.djSynced(d) && L().song != null) D.text(x0 + 114 - D.tw(F.T, "SYNC"), Y.tempo + 3, F.T, "SYNC", D.GREEN);
        else { const c = change(d); D.text(x0 + 114 - D.tw(F.T, c), Y.tempo + 3, F.T, c, /^[+-]0\.0%$/.test(c) ? D.LGREY : D.WHITE); }
        /* the place: the position of the last, the bar of the position's last */
        const p = M.pattern(s.order[st.at.pos]), barsIn = p ? Math.max(1, Math.ceil(p.rows / st.br)) : 1;
        D.runs(x0, Y.place, F.T, [["POS ", D.CYAN], [M.of(st.at.pos, s.order.length, 2), D.LGREY], [" BAR ", D.CYAN],
                                  [M.of(Math.floor(st.at.row / st.br), barsIn), D.LGREY]]);
        overview(d, st, x0);
        /* the bar's beats, the one playing lit; the time left */
        const w = Math.floor(60 / st.beats), beat = Math.floor((st.at.row % st.br) / s.rpb), warn = warning(d, st);
        for (let k = 0; k < st.beats; k++)
            D.rect(x0 + k * w, Y.beats, w - 2, 8, d.playing && k === beat ? (k === 0 ? D.YELLOW : D.GREEN) : D.DEEP + D.GREEN);
        const left = "-" + U.fmtTime(st.secsLeft);
        D.text(x0 + 114 - D.tw(F.T, left), Y.left, F.T, left, warn ? D.RED : D.LGREY);
        /* what the deck does now; its filter; what is set on it */
        const what = stateLine(d, st);
        if (what) D.text(x0, Y.state, F.T, what, D.YELLOW);
        filterRow(d, x0);
        setLine(d, s, x0);
    }
    /* the deck's VOLUME (specs/ui/screens/dj.md#volume): a bar as the header's CPU bar, at the song row's end, 4 × 12:
     * 10 px at 0 dB, 12 at +6, 1 at -24, none at OFF; green, yellow above 0 dB (boosted) */
    function volBar(d, x0) {
        const v = d.vol, h = v <= P.VOL_OFF ? 0 : v <= 0 ? 1 + Math.round((v + 24) / 24 * 9) : 10 + Math.round(v / 3);
        D.rect(x0 + 110, Y.song, 4, 12, D.DEEP + D.DGREY);
        if (h) D.rect(x0 + 110, Y.song + 12 - h, 4, h, v > 0 ? D.YELLOW : D.GREEN);
    }
    /* the set line (specs/ui/screens/dj.md#the-set-line): what is set on the deck, as long as it is: the loop going
     * round (green), its mutes after it (`MUTE 2`, `SOLO 05`), KEY right-aligned (white), in 19 characters. Short of
     * room the loop's first word goes (`PATTERN`, `4 BARS`), then the mutes */
    const SET_CHARS = 19;
    function setLine(d, s, x0) {
        const key = d.key ? "KEY " + (d.key > 0 ? "+" : "") + d.key : "";
        const tracks = tracksOf(s), muted = d.mutes.filter((m, t) => m && t < tracks).length;
        const mute = d.solo >= 0 ? ["SOLO", M.hex(d.solo)] : muted ? ["MUTE", String(muted)] : null;
        const loop = d.loop && !d.loop.roll ? loopWord(d.loop.idx, false) : d.repeat ? "REPEAT PATTERN" : "";
        const short = loop.slice(loop.indexOf(" ") + 1), mt = mute ? mute.join(" ") : "";
        const fits = (lp, mu) => lp.length + (lp && mu ? 1 : 0) + mu.length + (key ? 1 + key.length : 0) <= SET_CHARS;
        let lp = loop, mu = mt;
        if (!fits(lp, mu)) lp = short;
        if (!fits(lp, mu)) { mu = ""; lp = fits(loop, "") ? loop : short; }
        let x = x0;
        if (lp) x = D.text(x0, Y.set, F.T, lp, D.GREEN) + 6;
        if (mu) D.runs(x, Y.set, F.T, [[mute[0], D.CYAN], [" " + mute[1], D.LGREY]]);
        if (key) D.text(x0 + 114 - D.tw(F.T, key), Y.set, F.T, key, D.WHITE);
    }
    /* the filter row (specs/ui/screens/dj.md#the-filter-row): the DJ FILTER's value coloured by its side (LP light
     * blue, HP brown, OFF light grey; a setting switched off dark grey), and a slider, OFF in the middle, filled from
     * there out to the value in the same colour; its LFO swinging it, where the sound has the swing now (with the
     * filter off a preview, dark grey, around the setting the key brings in), the place it comes back to marked,
     * LFO at the end (white heard, dark grey previewing) */
    const SLIDE = { x: 47, half: 23 };                       /* (x0 + 47 .. x0 + 93; the middle x0 + 70) */
    const sideCol = v => v < 0 ? D.LBLUE : v > 0 ? D.BROWN : D.LGREY;
    function filterRow(d, x0) {
        const a = lfoNow(), heard = d.filterOn || !!d.slide, swings = d.lfo.on && !!(a && a.prev);
        const v = swings ? Math.round(a.prev[d.i]) : heard ? d.filter : d.filterSet;
        const col = heard ? sideCol(v) : D.DGREY, y = Y.filt + 5, cx = x0 + SLIDE.x + SLIDE.half;
        const at = u => cx + Math.round(u < 0 ? u / 64 * SLIDE.half : u / 63 * SLIDE.half);
        D.text(x0, Y.filt, F.T, filterWord(v), v ? col : D.DGREY);  /* (OFF dark grey) */
        D.hline(x0 + SLIDE.x, y, 2 * SLIDE.half + 1, D.DGREY);
        if (v) D.rect(Math.min(cx, at(v)), y - 2, Math.abs(at(v) - cx) + 1, 5, col);
        /* where it comes back to: dark grey in the heard swing; previewing, the setting in its side's colour */
        if (swings) D.vline(at(heard ? d.filter : d.filterSet), y - 3, 7, heard ? D.DGREY : sideCol(d.filterSet));
        D.vline(cx, y - 3, 7, D.LGREY);                              /* OFF */
        if (d.lfo.on) D.text(x0 + 114 - D.tw(F.T, "LFO"), Y.filt, F.T, "LFO", swings && heard ? D.WHITE : D.DGREY);
    }
    /* the overview (specs/ui/screens/dj.md#the-overview): a column a bar as tall as its notes; the positions'
     * boundaries, the playhead, the cue point, a loop's span; played dark grey, the rest light grey, the
     * last 8 bars red while their warning runs */
    function overview(d, st, x0) {
        const ov = d.over;
        if (!ov || !ov.bars.length) return;
        const n = ov.bars.length, xb = b => x0 + Math.floor(b * 114 / n), warn = warning(d, st);
        /* the pattern playing light grey, the others dark grey */
        const colH = b => 2 + Math.round(ov.bars[b] / ov.max * 12), p = M.pattern(st.s.order[st.at.pos]);
        const cur0 = P.djBarOf(d, { pos: st.at.pos, row: 0 }), cur1 = cur0 + (p ? p.rows : 0) / st.br;
        for (let b = 0; b < n; b++) {
            const h = colH(b), w = Math.max(1, xb(b + 1) - xb(b));
            D.rect(xb(b), Y.over + 14 - h, w, h, warn && b >= n - 8 ? D.RED : b + 1 > cur0 && b < cur1 ? D.LGREY : D.DGREY);
        }
        /* what goes round, green to the row: a loop's rows as the song's flow plays them (into the next position,
         * past the end to its LOOP FROM), or REPEAT PATTERN's pattern; over the columns, and a line under them (a bar
         * with no notes shows there) */
        for (const [from, to] of P.djRoundSpans(d, st.at)) {
            const a = Math.min(from, n), z = Math.min(to, n);
            for (let b = Math.floor(a); b < Math.ceil(z); b++) {
                const x1 = Math.max(xb(b), xb(a)), x2 = Math.min(xb(b + 1), xb(z)), w = Math.max(1, x2 - x1), h = colH(b);
                D.rect(x1, Y.over + 14 - h, w, h, D.GREEN);
                D.rect(x1, Y.over + 14, w, 2, D.GREEN);
            }
        }
        for (const sb of ov.starts.slice(1)) D.vline(xb(sb), Y.over, 16, D.BLACK);    /* the positions' gaps, through the line too */
        D.rect(Math.min(x0 + 112, xb(P.djBarOf(d, d.cue))), Y.over - 2, 3, 2, D.YELLOW);
        /* the playhead, and MOVE's cursor while MOVE is held (where letting go takes the deck): each between black
         * lines, so they show on any column in any theme (specs/ui/colours.md) */
        const mark = (cx, col) => {
            if (cx > x0) D.vline(cx - 1, Y.over - 1, 17, D.BLACK);
            if (cx < x0 + 113) D.vline(cx + 1, Y.over - 1, 17, D.BLACK);
            D.vline(cx, Y.over - 1, 17, col);
        };
        mark(Math.min(x0 + 113, x0 + Math.floor(st.bar * 114 / n)), D.WHITE);
        const mv = moving(d);
        if (mv && mv.move.at) mark(Math.min(x0 + 113, x0 + Math.floor(P.djBarOf(d, mv.move.at) * 114 / n)), D.CYAN);
    }
    function list() {
        const all = songs();
        D.hline(0, Y.rule, 240, D.DGREY);
        D.text(18, Y.cols, F.T, "NR", D.CYAN);
        D.text(36, Y.cols, F.T, "NAME", D.CYAN);
        D.text(156 - D.tw(F.T, "BPM"), Y.cols, F.T, "BPM", D.CYAN);
        D.text(180 - D.tw(F.T, "LEN"), Y.cols, F.T, "LEN", D.CYAN);          /* (the positions: its length) */
        D.text(192, Y.cols, F.T, "TIME", D.CYAN);
        if (!all.length) { D.text(2, Y.list, F.T, "NO SONGS: MAKE ONE ON SONGS", D.DGREY); return; }
        const top = Math.max(0, Math.min(dj.list - 2, all.length - LIST_ROWS));   /* a song at a time, the highlight in the middle */
        for (let i = top; i < Math.min(all.length, top + LIST_ROWS); i++) {
            const s = all[i], y = Y.list + 13 * (i - top), on = i === dj.list;
            if (on) D.box(0, y - 1, 240, 13, D.WHITE);
            const mark = (d, ch, x) => { if (d.song === i) D.text(x, y, F.T, ch, d.playing ? D.GREEN : D.LGREY); };
            mark(L(), "L", 2);
            mark(R(), "R", 8);
            D.text(18, y, F.T, M.hex(i), D.DGREY);
            D.text(36, y, F.T, s.name.slice(0, 16), on ? D.WHITE : D.LGREY);
            const bpm = String(M.bpm(s)), pos = String(s.order.length);
            D.text(156 - D.tw(F.T, bpm), y, F.T, bpm, D.LGREY);
            D.text(180 - D.tw(F.T, pos), y, F.T, pos, D.LGREY);
            D.text(192, y, F.T, U.fmtTime(U.songSeconds(s)), D.LGREY);
        }
    }
    /* FADER, L, the track, R, ALG (the knob that moves it), the track centred on the decks' divider (its middle mark
     * at x 119-121); its LFO on: the mark where the swing has it (as the sound reports it), where it comes back to
     * dark grey, filled between the two as the filter row's slider is */
    const XF_X = 60;
    function crossfader() {
        D.text(14, Y.xf, F.T, "FADER", D.CYAN);
        D.text(50, Y.xf, F.T, "L", D.CYAN);
        D.hline(XF_X, Y.xf + 5, 120, D.DGREY);
        const a = lfoNow(), x = a && a.xf != null ? a.xf : DJ.xf, mx = v => XF_X + Math.round(v * 117 / 32);
        if (P.djLfo("FADER").on) {
            const h = mx(DJ.xf) + 1, s = mx(x) + 1;
            D.rect(Math.min(h, s), Y.xf + 3, Math.abs(s - h) + 1, 5, D.LGREY);
            D.rect(mx(DJ.xf), Y.xf, 3, 11, D.DGREY);
        }
        D.rect(mx(x), Y.xf, 3, 11, D.WHITE);
        D.text(184, Y.xf, F.T, "R", D.CYAN);
        D.text(196, Y.xf, F.T, "ALG", D.YELLOW);
    }

    /* --------------------------------------------------------------------- LEDs --- */
    const blink = ms => Math.floor(now() / ms) % 2 === 0;
    /* a deck's PLAY: brighter on each of its beats; the last 8 bars blinking on the beat */
    function playLed(d) {
        if (d.song == null || d.atEnd) return 0;                 /* (at the song's end PLAY has nothing to play) */
        if (d.startAt) return blink(FAST) ? 1 : 0;
        if (!d.playing) return .25;
        const st = P.djState(d), h = P.djHeard(d), onBeat = h && h.row % (h.rpb || 4) === 0 && st.rowFrac < .6;
        if (warning(d, st)) return onBeat ? 1 : 0;
        return onBeat ? 1 : .25;                                  /* a blink a beat: full on it, dim between */
    }
    function deckKeys(keys, d, k) {
        const has = d.song != null, fl = n => dj.flash[n] && now() < dj.flash[n];
        keys[k.play] = playLed(d);
        keys[k.hold] = !has ? 0 : d.scratch ? 1 : .25;
        keys[k.filter] = !has ? 0 : d.filterOn ? 1 : .25;                  /* lit while on */
        keys[k.load] = fl(k.load) || d.loadTo != null ? (blink(FAST) ? 1 : 0) : .25;   /* blinking: refused, or waiting for the bar */
        for (const n of [k.play, k.hold, k.filter]) if (fl(n)) keys[n] = blink(FAST) ? 1 : 0;
    }
    /* the pads light for finding them by touch (specs/ui/leds.md): each group of four's first dim, the rest dark;
     * what is on full, what waits blinking, over that */
    function padLeds(keys, d, base) {
        const s = dsong(d);
        for (let k = 0; k < 8; k++) {
            const n = U.WHITE[base + k], fl = dj.flash[n] && now() < dj.flash[n], nav = k % 4 === 0 ? .25 : 0;
            if (dj.mode === LFO_MODE) { keys[n] = fl ? (blink(FAST) ? 1 : 0) : lfoLed(base + k, n, nav); continue; }
            if (!s) { keys[n] = nav; continue; }
            let v = nav;
            /* CUE: full paused at the cue point, or held playing from it; blinking slowly paused elsewhere (a tap would
             * set it there) */
            if (dj.mode === 0 && k === PAD.CUE) v = d.cueHeld || P.djAtCue(d) ? 1 : !d.playing ? (blink(SLOW) ? .25 : 0) : nav;
            else if (dj.mode === 0 && k === PAD.SET) v = dj.done[n] && now() < dj.done[n] ? 1 : nav;   /* full as it sets */
            else if (dj.mode === 0 && k === PAD.JUMP) v = d.jump && d.jump.cue ? (blink(FAST) ? 1 : 0) : 0;   /* blinking: waits for the beat */
            else if (dj.mode === 0 && k === PAD.MOVE) v = moving(d) ? 1 : d.jump && !d.jump.cue ? (blink(FAST) ? 1 : 0) : 0;
            else if (dj.mode === 0) {                                        /* a stop: full until the deck stops */
                const tail = d.echo && (d.echoStop || d.echo.back) ? (d.echo.fx === "REVERB" ? PAD.WASH : PAD.ECHO) : -1;
                v = (k === PAD.BRAKE && d.brake) || (k === PAD.SPIN && d.backspin) || k === tail ? 1 : nav;
            }
            else if (k === 0) v = d.repeat ? 1 : nav;
            else v = d.loop && d.loop.idx === k ? 1 : nav;
            keys[n] = fl ? (blink(FAST) ? 1 : 0) : v;
        }
    }
    /* the LFO pads: each four's first (on / off) dim, the rest dark; full what is on: an on / off pad while its LFO
     * runs (blinking while it waits for the step), the reverse, a KEY pad held or its deck's KEY not 0; ALL OFF full as
     * it acts. A deck with no song: its group only the finding light */
    function lfoLed(wi, n, nav) {
        const g = wi >> 2, k = wi & 3, d = lfoDeck(g);
        if (g === 3 && k === 3) return dj.done[n] && now() < dj.done[n] ? 1 : 0;
        if (d && d.song == null) return nav;
        if (d && k === 3) return keyHeld(d) || d.key ? 1 : 0;
        const l = P.djLfo(lfoName(g), d);
        if (k === 0) return P.djLfoWaits(l) ? (blink(FAST) ? 1 : 0) : l.on ? 1 : nav;   /* (blinking: waiting for the step) */
        if (k === 3) return l.rev ? 1 : 0;
        return 0;
    }
    /* LFO held: its layer lists the LFO pads by key (specs/ui/dialogs.md#hold-layers), a line a group of four, what is
     * on white; let go, the pads are the LFOs' (already: they stay) */
    const LFO_KEYS = [["FADER", "SHAPE", "RANGE", "REVERSE"], ["FILTER L", "SHAPE", "RANGE", "KEY"],
                      ["FILTER R", "SHAPE", "RANGE", "KEY"], ["FX", "SHAPE", "RANGE", "ALL OFF"]];
    function lfoLayer() {
        const lines = LFO_KEYS.map((g, gi) => g.flatMap((w, i) => {
            const d = lfoDeck(gi), l = P.djLfo(lfoName(gi), d);
            const on = i === 0 ? l.on : i === 3 && gi === 0 ? l.rev : i === 3 && d ? !!d.key : false;
            return (i ? [["|", D.DGREY]] : []).concat([[(gi * 4 + i + 1) + " ", D.YELLOW], [w, on ? D.WHITE : D.LGREY]]);
        }));
        return lines.concat([[["1 5 9 13", D.YELLOW], [": ON / OFF; + ", D.LGREY], ["ALG", D.YELLOW], [": SPEED", D.LGREY]],
                             [["8 12", D.YELLOW], [" HELD + ", D.LGREY], ["K1 / K4", D.YELLOW], [": THE KEY", D.LGREY]]]);
    }
    function layerUp(b) {
        if (b !== B.LFO || dj.mode === LFO_MODE) return;
        dj.back = dj.mode;
        dj.mode = LFO_MODE;
        U.toast("PADS: " + MODES[dj.mode]);
    }

    U.register({
        name: "DJ", group: "PLAY",
        title: () => "DJ",
        /* the pads' mode */
        sub: () => MODES[dj.mode],
        does: [B.SEQ, B.LFO, B.GLO, B.SEL],
        ownArp: "none",                                      /* ARP does nothing here */
        lfoLayer, layerUp,
        enter() {                                            /* (what plays plays on: DJ apart till its first start) */
            if (dj.list < 0 || !songs()[dj.list]) dj.list = Math.min(U.song, songs().length - 1);
            dj.page = Math.min(dj.page, pages() - 1);
        },
        play() {
            const r = P.djPlayButton();
            if (r === "other") U.toast("PAUSED: PLAY AGAIN FOR DJ");     /* (what played, not started on DJ) */
            if (r === "empty") { U.refuse(B.PLAY); U.toast("NO SONG ON THE DECK"); }
            if (r === "end") { U.refuse(B.PLAY); U.toast("THE END: CUE GOES BACK"); }
        },
        canPlay: () => DJ.decks.some(d => d.song != null),
        listAt: () => dj.list,                               /* (the highlighted song, the pads' mode and page: for the checks) */
        padsAt: () => ({ mode: MODES[dj.mode], page: dj.page }),
        lit(b) {
            if (b === B.OCTM || b === B.OCTP) return 0;      /* (lit in HOME's combination only: homeOct) */
            if (b === B.SEQ) return DJ.decks.some(d => d.song != null) ? .25 : 0;
            if (b === B.ENV) return dj.k23 ? 1 : .25;        /* (full while K2 / K3 are off their first, the filters) */
            if (b === B.EDIT || b === B.ARP) return 0;
            if (b === B.REC) return 0;                       /* (nothing alone: HOME + REC is the CUT) */
            if (b === B.LFO) return dj.mode === LFO_MODE ? 1 : .25;   /* full while the pads are the LFO's */
        },
        selLines: () => ["K1, K4: THE TEMPO BY 1 BPM", K23[dj.k23] === "VOLUME" ? "K2, K3: THE VOLUMES BY 6 DB" : "K2, K3: THE FILTERS BY 10"],
        /* no keyboard functions here: no key plays an instrument, so PRESETS and SEL + PRESETS do nothing */
        noKeyboard: true,
        knob(e, s, coarse) {
            if (e === E.SELECT) { if (songs().length) dj.list = U.clamp(dj.list + Math.sign(s), 0, songs().length - 1); return; }
            if (e === E.ALGORITHM) {
                const lh = lfoHeld();                         /* an LFO's on / off pad held: its frequency */
                if (lh) {
                    P.djLfoTurn(lh.lfo, s); lh.turned = true;
                    dj.last = { k: "lfo", h: lh };          /* (SEL after: back to 1/2) */
                    U.showKnob(lfoLabel(lh.name, lh.d) + " LFO", hzWord(lh.lfo));   /* (the header follows the turn, as any knob) */
                    return;
                }
                P.djXf(s); dj.last = { k: "xf" }; U.showKnob("X-FADER", xfWord());   /* (with its LFO on: where it swings from, or comes back to) */
                return;
            }
            if (e === E.K1 || e === E.K4) {
                const d = e === E.K1 ? L() : R();
                if (d.scratch) {                              /* HOLD held: the deck by hand, a row a click (a fast turn more) */
                    let at = null;
                    for (let k = 0; k < Math.abs(s); k++) at = P.djHoldTurn(d, Math.sign(s)) || at;   /* (a loop moves with it) */
                    if (at) U.showKnob("POS " + M.hex(at.pos), "ROW " + M.hex(at.row));
                    return;
                }
                const mv = moving(d);
                if (mv) return moveTurn(d, mv, 0, s);         /* MOVE held: the cursor a bar a click */
                if (keyHeld(d)) {                             /* its KEY pad held: a semitone a click */
                    P.djKey(d, d.key + Math.sign(s));
                    dj.last = { k: "key", d };
                    U.showKnob("KEY " + sideName(d)[0], keyWord(d));
                    return;
                }
                return tempo(d, s, coarse);
            }
            if (e === E.K2 || e === E.K3) {
                const d = e === E.K2 ? L() : R();
                if (d.scratch) {                              /* HOLD held: the deck moved silently, a beat a click */
                    const at = P.djHoldMove(d, s);
                    if (at) U.showKnob("POS " + M.hex(at.pos), "ROW " + M.hex(at.row));
                    return;
                }
                const mv = moving(d);
                if (mv) return moveTurn(d, mv, s, 0);         /* MOVE held: the cursor a pattern a click */
                return K23[dj.k23] === "VOLUME" ? volume(d, s, coarse) : filter(d, s, coarse);   /* (ENV's cycle) */
            }
        },
        tap(b) {
            if (b === B.SEQ) {
                if (!P.djSwap()) { U.refuse(B.SEQ); U.toast("NO SONGS TO SWAP"); return true; }
                U.toast("DECKS SWAPPED");
                return true;
            }
            if (b === B.OCTM || b === B.OCTP) return true;   /* nothing alone (in HOME's combination: homeOct) */
            if (b === B.LFO) {                                /* the LFO pads in, or back to the mode before */
                if (dj.mode === LFO_MODE) dj.mode = dj.back;
                else { dj.back = dj.mode; dj.mode = LFO_MODE; }
                U.toast("PADS: " + MODES[dj.mode]);
                return true;
            }
            if (b === B.SEL) { resetKnob(); return true; }
            if (b === B.GLO) { props(); return true; }
            if (b === B.ENV) {                                /* what K2 / K3 turn: the next in the cycle */
                dj.k23 = (dj.k23 + 1) % K23.length;
                U.toast("K2 K3: " + K23[dj.k23]);
                return true;
            }
            return b === B.EDIT || b === B.ARP;
        },
        /* the keys: the decks' and the pads, never notes */
        key(n, down) {
            const wi = U.whiteIndex(n);
            if (wi >= 0) {
                if (down) dj.held[n] = pad(wi, true, null);
                else { const h = dj.held[n]; delete dj.held[n]; if (h) pad(wi, false, h); }
                return true;
            }
            const deckOf = { [KEY.LPLAY]: L, [KEY.LHOLD]: L, [KEY.LFILTER]: L, [KEY.LLOAD]: L,
                             [KEY.RPLAY]: R, [KEY.RHOLD]: R, [KEY.RFILTER]: R, [KEY.RLOAD]: R, [KEY.SYNC]: R };
            if (!down) {
                const h = dj.held[n];
                delete dj.held[n];
                if (h && (n === KEY.LHOLD || n === KEY.RHOLD)) holdKey(h, n, false);
                if (h && h.filter) P.djFilterSettle(h.d);
                if (h && h.timer) clearTimeout(h.timer);
                return true;
            }
            const d = deckOf[n] ? deckOf[n]() : null;
            if (n === KEY.LPLAY || n === KEY.RPLAY) {             /* play / pause; held past the hold delay: back to the start */
                if (playKey(d, n) !== "kept" && d.song != null) {
                    const h = dj.held[n] = { d, play: true, t: now() };
                    h.timer = setTimeout(() => {
                        if (dj.held[n] === h && P.djToStart(d)) { h.fired = true; U.toast(sideName(d) + ": BACK TO THE START"); }
                    }, U.set("HOLD DELAY"));
                }
            }
            else if (n === KEY.LHOLD || n === KEY.RHOLD) dj.held[n] = holdKey(d, n, true);
            else if (n === KEY.LFILTER || n === KEY.RFILTER) dj.held[n] = filterKey(d, n);
            else if (n === KEY.LLOAD || n === KEY.RLOAD) dj.held[n] = loadKey(d, n);
            else if (n === KEY.SYNC) {
                if (d.song == null) noSong(n);
                else U.toast(P.djSync() ? "SYNC ON" : "SYNC OFF");
            } else if (n === KEY.MODE) {                     /* PLAY / LOOPS; from the LFO pads, back to the mode before */
                dj.mode = dj.mode === LFO_MODE ? dj.back : 1 - dj.mode;
                U.toast("PADS: " + MODES[dj.mode]);
            }
            return true;
        },
        keyLeds(keys) {
            keys.fill(0);
            deckKeys(keys, L(), { play: KEY.LPLAY, hold: KEY.LHOLD, filter: KEY.LFILTER, load: KEY.LLOAD });
            deckKeys(keys, R(), { play: KEY.RPLAY, hold: KEY.RHOLD, filter: KEY.RFILTER, load: KEY.RLOAD });
            keys[KEY.SYNC] = R().song == null ? 0 : DJ.sync ? 1 : .25;
            if (dj.flash[KEY.SYNC] && now() < dj.flash[KEY.SYNC]) keys[KEY.SYNC] = blink(FAST) ? 1 : 0;
            keys[KEY.MODE] = .25;
            padLeds(keys, L(), 0);
            padLeds(keys, R(), 8);
            return true;
        },
        /* HOME + white key: keys 1-8 the left deck's tracks, 9-16 the right's (on the pads' second page 08-0F);
         * solo within its deck */
        muteKey(wi) {
            const d = wi < 8 ? L() : R(), t = (dj.page % 2) * 8 + wi % 8;
            d.mutes[t] = !d.mutes[t];
            P.mutesChanged();
            U.toast(sideName(d) + " TRACK " + M.hex(t) + (d.mutes[t] ? " MUTED" : " ON"));
        },
        soloKey(wi) {
            const d = wi < 8 ? L() : R(), t = (dj.page % 2) * 8 + wi % 8;
            d.solo = d.solo === t ? -1 : t;
            P.mutesChanged();
            U.toast(d.solo < 0 ? "SOLO OFF" : sideName(d) + " TRACK " + M.hex(t) + " SOLO");
        },
        /* OCT-, OCT+ in HOME's combination (with the white keys): the tracks they mute, 00-07 or 08-0F, where a deck's
         * song has tracks that way; go: move there (else only whether it can). Nowhere to go: nothing, no message */
        homeOct(b, go) {
            const to = dj.page + (b === B.OCTP ? 1 : -1);
            if (to < 0 || to >= pages()) return false;
            if (go) { dj.page = to; U.toast("MUTES: TRACKS " + (to ? "08-0F" : "00-07")); }
            return true;
        },
        muteLeds(keys) {
            U.WHITE.forEach((n, wi) => {
                const d = wi < 8 ? L() : R(), t = (dj.page % 2) * 8 + wi % 8, s = dsong(d);
                const tracks = s ? Math.max(0, ...s.order.map(x => (M.pattern(x) || { tracks: 0 }).tracks)) : 0;
                keys[n] = t >= tracks ? 0 : d.solo === t ? 1 : P.deckMuted(d, t) ? 0 : .25;
            });
        },
        /* the footer's left: no instrument here (the keys are the decks'), the pads' keys instead, by their mode; a
         * HOLD key held: its knob */
        footHints() {
            const Yl = D.YELLOW, G = D.LGREY, sc = DJ.decks.find(d => d.scratch);
            if (sc) return [[sc === L() ? "K1" : "K4", Yl], [" ROWS HEARD  ", G], [sc === L() ? "K2" : "K3", Yl], [" BEATS", G]];   /* (27 at most: */
            const held = Object.values(dj.held).filter(h => h && h.mode != null), sp = held.find(h => h.stop);   /* (the pads) */
            if (sp) {                                        /* what letting go does; the deck's PLAY key turns it */
                const st = sp.d.brake || sp.d.backspin || sp.d.echo, op = sp.d === L() ? "OP1" : "OP6";
                return st && !st.back ? [["LET GO", Yl], [" STAYS  ", G], [op, Yl], [" PLAYS ON", G]]
                                      : [["LET GO", Yl], [" PLAYS ON  ", G], [op, Yl], [" STAYS", G]];
            }
            /* PLAY's other pads held: what letting go does, or the cue they set */
            const cue = held.find(h => h.cue), set = held.find(h => h.set), jp = held.find(h => h.jump);
            if (cue && cue.d.cueHeld) {                      /* what letting go does; the deck's PLAY key turns it */
                const op = cue.d === L() ? "OP1" : "OP6";
                return cue.d.cueKeep ? [["LET GO", Yl], [" PLAYS ON  ", G], [op, Yl], [" CUE", G]]
                                     : [["LET GO", Yl], [" CUE  ", G], [op, Yl], [" PLAYS ON", G]];
            }
            if (jp) return [["LET GO", Yl], [now() - jp.t < PAD_TAP_MS ? " JUMPS TO THE CUE" : " CUE ON THE BEAT", G]];
            if ((cue || set) && dsong((cue || set).d)) return [["CUE", Yl], [" " + cursorWord((cue || set).d, (cue || set).d.cue), G]];
            const mv = DJ.decks.find(d => moving(d));
            if (mv) return [[mv === L() ? "K1" : "K4", Yl], [" BARS  ", G], [mv === L() ? "K2" : "K3", Yl], [" PATTERNS", G]];   /* the voices */
            /* the LFO pads held: an on / off pad (ALG its frequency; what letting go does), a KEY pad */
            const lh = lfoHeld(), kp = held.find(h => h.key);
            if (lh) return [["ALG", Yl], [" SPEED  ", G], ["LET GO", Yl], [lh.wasOn && !lh.turned ? " OFF" : " STAYS ON", G]];
            if (kp) return [[kp.d === L() ? "K1" : "K4", Yl], [" KEY, A SEMITONE A CLICK", G]];
            if (dj.mode === 0) return [["SET CUE JUMP MOVE", G], ["|", D.DGREY], ["STOPS", G]];   /* on the right) */
            if (dj.mode === 1) return [["1", Yl], [" PATTERN ", G], ["2-4", Yl], [" BARS ", G], ["5-8", Yl], [" FREE", G]];
            return [["1", Yl], [" FADER ", G], ["5", Yl], [" FILTER ", G], ["13", Yl], [" " + P.djLfoFx(), G]];
        },
        footer() {
            /* the FX LFO's MIX, white, while it runs (the voices are the header's: specs/ui/frame.md#the-header) */
            return P.djLfo("FX").on ? [fxWord(), D.WHITE] : null;
        },
        draw() {
            D.vline(120, U.BODY_Y, 112, D.DGREY);
            deck(L(), 2);
            deck(R(), 123);
            crossfader();
            list();
        },
    });
})();

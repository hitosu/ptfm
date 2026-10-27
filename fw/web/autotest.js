/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The debug page's autotests (specs/debug.md#autotests): the FM-1 played as a hand on its panel would, through
 * MIRROR (specs/connections.md#mirror-the-panel-from-a-page), checked through the state (40 STATE?, 41 STATE),
 * measured through FIGURES (fw/web/figures.js). No DOM, no MIDI: the page (debug.html) gives an io, and Node runs
 * the same against the firmware built for the browser (fw/tests/autotest_test.js).
 *
 *   PTFM_autotest.SCENARIOS                  [{group, name, debug, artifact, input, streams, records, secs, what, run(t)}]:
 *                                               the scenarios, in order (a run takes the input ones first: the
 *                                               browser asks for the input at its start, the hand still there);
 *                                               artifact: it makes a file (unticked at first), input: it takes the
 *                                               FM-1's USB audio input (the browser asks for it then), streams: the FM-1 sends its USB audio by design (a recording),
 *                                               records: the page records it (io.record: its sound made in RUN's
 *                                               click), secs: about how long it takes on the FM-1 (the run's
 *                                               counter); tsecs: its test alone, from its first measured
 *                                               stretch (or the bench) to its end (the scenario's counter)
 *   PTFM_autotest.run(io, list, opts)        -> a run: {lines(), results}; io below; opts.scale shortens the
 *                                               times measured (the tests' 0.05), opts.version the page's;
 *                                               opts.recordAudio: the page records the FM-1's USB audio all run
 *                                               (the report says so), opts.audioRefused: it was not allowed to
 *   PTFM_autotest.decodeState(message)       -> the state, or null (not a 41 STATE)
 *   PTFM_autotest.hello(on), btn(id, down), key(k, down), knob(id, clicks), ASK, TONE(on), SECOND(on): the messages
 *
 * The io: send(bytes) a MIDI message or SysEx to the FM-1; sleep(ms) the time passing; ask() -> Promise of the
 * state (null: no answer); measure(name) starts a measured stretch, measured() ends it -> its FIGURES windows;
 * bench() -> Promise of the bench's records (32 BENCH, its 33s, until END); prompt(text) -> Promise, CONTINUE;
 * log(text) what is under way; stopped() STOP was pressed; usbIn(on) -> Promise of {ok, why, name, check}: the FM-1's
 * USB audio input taken (so the FM-1 sends it) or let go, at every scenario's start but an input one's (optional:
 * without it a scenario needing it is skipped); record(on) -> Promise of {ok, why, check, secs}: the page records the
 * input it took into the scenario's own WAV (in the run's ZIP), or ends that (optional: without it a scenario
 * recording is skipped); now() the time in ms (optional: Date.now; the tests' own clock); sendAt(bytes, ms) sends
 * it ms from now, timed by the MIDI port (optional: a press's release; without it the page's own wait times it);
 * freeze(on) FREEZE THE SCREEN on or off with the next FIGURES' question (optional: without it a scenario needing
 * it is skipped); heard() the ms since the FM-1's last FIGURES answer (optional: a state left unanswered says it);
 * answers() -> {lost, late}: the state answers counted so far that the FM-1 sent and the page never got (by their
 * numbers), and those that came after their question's time-out (optional: a state answered after a silence says it).
 * The io's sleep and ask's time-out should keep their time with the page out of sight (debug.html: a worker's).
 * A run's R.list: its scenarios in the order it runs them (the input ones first); R.current {key, t0, t1}: the
 * scenario under way, its start and its test's (0 while it sets the FM-1 up; null between and after them); a result's
 * ms its whole time, testMs its test's. */
(function (root) {
"use strict";
const FG = typeof module !== "undefined" && module.exports ? require("./figures.js") : root.PTFM_figures;

/* ---- MIRROR's messages (the PT/FM controller's mirror mode, channel 16) ---- */
const CH = 15;
const B = { OCTM: 0, OCTP: 1, FX: 2, SEL: 3, ENV: 4, LFO: 5, EDIT: 6, GLO: 7, HOME: 8, SAVE: 9, ARP: 10, SEQ: 11, PLAY: 12, REC: 13 };
const K = { SELECT: 0, ALG: 1, K1: 2, K2: 3, K3: 4, K4: 5, PRESETS: 6 };
const WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26];     /* the pads, 1-16 */
const BLACK = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25];                       /* OP1 ... the blank key */
const SCREEN_KEY = { SONGS: 1, EDITOR: 3, INSTRUMENTS: 5, PLAYBACK: 8, DJ: 10, VISUAL: 15, STATUS: 17, SETTINGS: 20, LOOPS: 22 };
const FXKEY = { GATE: 1, FILTER: 3, DRIVE: 5, "LO-FI": 8, CHORUS: 10, DELAY: 13, REVERB: 15, LIMITER: 17, DUCK: 20, METRONOME: 22 };
const FXNUM = { GATE: 0, FILTER: 1, DRIVE: 2, "LO-FI": 3, CHORUS: 4, DELAY: 5, REVERB: 6, LIMITER: 7, DUCK: 8, METRONOME: 9 };
/* SETTINGS' lines (specs/ui/screens/settings.md: the list's order), the MIDI page's */
const SET = { VOICES: 3, SECOND_CORE: 4, KEYS_OCTAVE: 9, COUNT_IN: 14, TOOLS: 21 };
const MIDI_SET = { SYNC: 3 };
const TOOL = { CHECK_REFERENCES: 0, CHECK_STORAGE: 1, AUTO_LEVEL_ALL: 3 };

const hello = (on) => [0xF0, 0x7D, 0x50, 0x54, 0x43, 0x02, on ? 0 : 1, 0x02, CH, 0xF7];
const btn = (id, down) => [0xB0 | CH, 102 + id, down ? 127 : 0];
const key = (k, down) => (down ? [0x90 | CH, 53 + k, 127] : [0x80 | CH, 53 + k, 0]);
const knob = (id, clicks) => [0xB0 | CH, 20 + id, clicks < 0 ? 128 + clicks : clicks];
const ASK = [0xF0, 0x7D, 0x50, 0x54, 0x40, 0xF7];
const TONE = (on) => [0xF0, 0x7D, 0x50, 0x54, 0x34, on ? 1 : 0, 0xF7];
const SECOND = (on) => [0xF0, 0x7D, 0x50, 0x54, 0x38, on ? 1 : 0, 0xF7];   /* 38 SECOND CORE: 0 off, 1 as SETTINGS says */
const OVERLAY = ["NONE", "MENU", "QUESTION", "PROPERTIES", "TEXT", "PROGRESS", "HELP", "HOME", "LIST", "BOX"];

/* ---- the state's bytes (specs/connections.md#the-state) ---- */
function decodeState(m) {
    if (!m || m.length < 8 || m[0] !== 0xF0 || m[1] !== 0x7D || m[2] !== 0x50 || m[3] !== 0x54 || m[4] !== 0x41 ||
        m[m.length - 1] !== 0xF7) return null;
    const b = FG.unpack7(m.subarray ? m.subarray(5, m.length - 1) : m.slice(5, m.length - 1));
    if (b.length < 325 || b[0] !== 1) return null;
    const u16 = (o) => b[o] | b[o + 1] << 8, s8 = (o) => b[o] << 24 >> 24;
    const str = (o, n) => { let s = ""; for (let i = 0; i < n && b[o + i]; i++) s += String.fromCharCode(b[o + i]); return s; };
    const dj = b.slice(116, 301), rec = dj.slice(49);
    const deck = (d) => {
        const q = 1 + 24 * d, r = 88 + 24 * d;
        return { playing: !!(dj[q] & 1), armed: !!(dj[q] & 2), pos: dj[q + 1], row: dj[q + 2], bpm10: dj[q + 3] | dj[q + 4] << 8,
                 name: str(116 + q + 8, 16), song: rec[r], filter: rec[r + 9] << 24 >> 24, filterOn: !!(rec[r + 10] & 1),
                 lfo: { on: !!(rec[r + 19] & 1), shape: rec[r + 20], range: rec[r + 21], speed: rec[r + 22] } };
    };
    const lfo = (o) => ({ on: !!(rec[o] & 1), rev: !!(rec[o] & 2), shape: rec[o + 1], range: rec[o + 2], speed: rec[o + 3] });
    return {
        debug: !!(b[1] & 1), mirror: !!(b[1] & 2), arp: !!(b[1] & 4), latch: !!(b[1] & 8), playing: !!(b[1] & 16),
        tone: !!(b[1] & 32), mirrorCh: b[2], what: b[3], song: b[4], pos: b[5], row: b[6], bpm10: u16(7), fx: u16(9),
        voiceCount: b[11], voices: b[12], unsaved: u16(13), holdDelay: u16(15), grace: u16(17), cart: b[19], slot: b[20],
        octave: s8(21), sync: b[22], countIn: b[23], usbGain: s8(24), num: b[25], screen: str(26, 16), overlay: b[42], overlays: b[43],
        words: str(44, 24), cursor: Array.from(b.slice(68, 76)), songName: str(76, 16), version: str(92, 24),
        toast: str(301, 24),
        edits: b.length >= 331 ? { used: u16(325) * 4, top: u16(327) * 4, limit: u16(329) * 4 } : null,   /* (4-byte units; older: 325 B) */
        dj: { live: !!(dj[0] & 1), decks: [deck(0), deck(1)], rec: Array.from(rec), xfade: rec[5], sync: !!(rec[6] & 1),
              preview: !!(rec[6] & 2), xlfo: lfo(80), fxlfo: lfo(84) },
    };
}
const kb = (n) => (n / 1024).toFixed(1);
const editsText = (e) => `${kb(e.top)} of ${kb(e.limit)} KB filled, the changes ${kb(e.used)} KB`;
const fxOn = (s, name) => !!(s.fx >> FXNUM[name] & 1);                     /* at work now (playing) */
const fxSwitched = (s, name) => !!((s.cursor[2] | s.cursor[3] << 8) >> FXNUM[name] & 1);   /* on FX: switched on */

/* ---- the timing, as a hand's (specs/debug.md: A scenario) ---- */
const TAP = 60, GAP = 60, CLICK = 50;
const SLICE = 200;                      /* the longest sleep between looks at STOP */
const WATCH = 2000;                     /* a measured stretch looks at the state this often */
const SEEK_CLICK = 120;                 /* seeking's clicks: slower than the knobs' acceleration (under 90 ms a click
                                         * can count twice), so a click is one step */

class Failed extends Error {}
class Stopped extends Error {}
class Skipped extends Error {}
class Ended extends Error {}            /* the scenario is over: what still runs in its background stops */

/* the steps a scenario is made of: input, seeking, waiting, checks, measured stretches, prompts */
function steps(io, ctx) {
    const t = {};
    const wait = async (ms) => {                    /* (in slices: STOP ends any wait at once) */
        for (let left = ms; ; left -= SLICE) {
            if (io.stopped()) throw new Stopped("STOPPED");
            if (ctx.over) throw new Ended("ENDED");
            if (left <= 0) return;
            await io.sleep(Math.min(left, SLICE));
        }
    };
    t.sleep = wait;
    t.send = (m) => io.send(m);
    /* input going on while a stretch is measured; it ends with its scenario, failed or stopped (its waits throw) */
    t.background = (fn) => { const p = fn(); p.catch(() => {}); return p; };
    /* four questions; while the FM-1's FIGURES still come (it runs, its USB works: its answers lost on the way or held
     * up in the computer), up to twelve, and a state answered after such a silence is noted with what it says */
    t.ask = async () => {
        const now = () => (io.now ? io.now() : Date.now()), t0 = now(), a0 = io.answers ? io.answers() : null;
        const w0 = Date.now();                          /* (the FIGURES windows from here: io.since) */
        for (let i = 0; ; i++) {
            const s = await io.ask();
            if (s) {
                ctx.last = s;
                if (i >= 2) {
                    const a = io.answers ? io.answers() : null, w = io.since ? io.since(w0) : null;
                    t.note(`THE STATE ANSWERED ONLY AFTER ${((now() - t0) / 1000).toFixed(1)} S` + (a && a0 ?
                           `: THE FM-1 SENT ${a.lost - a0.lost} ANSWERS THE COMPUTER NEVER GOT, ${a.late - a0.late} CAME LATE` : "") +
                           (w && w.windows ? `; MEANWHILE (${w.windows} FIGURES WINDOWS) ITS MAIN LOOP'S LONGEST PASS ` +
                            `${(w.passMax / 1000).toFixed(1)} MS, SYSEX LOST ON THE WAY IN: ${w.lost[0]} BEGUN WHILE OTHERS ` +
                            `WAITED, ${w.lost[1]} CUT SHORT` : ""));
                }
                return s;
            }
            const h = io.heard ? io.heard() : -1;
            if (i >= 3 && !(h >= 0 && h < 2000 && i < 11)) {
                /* whose silence: the FM-1's FIGURES still coming (its USB and the PC fine: the answer itself missing),
                 * or nothing at all from it (the USB link or the PC stalled, or the FM-1 stuck) */
                const s1 = (h / 1000).toFixed(1), secs = ((now() - t0) / 1000).toFixed(0), w = io.since ? io.since(w0) : null;
                const more = w && w.windows ? `; ITS MAIN LOOP'S LONGEST PASS ${(w.passMax / 1000).toFixed(1)} MS, SYSEX LOST ` +
                             `${w.lost[0]} + ${w.lost[1]}` : "";
                throw new Failed("THE FM-1 DOES NOT ANSWER THE STATE" + (h < 0 ? "" : h < 2000 ?
                    ` (ITS FIGURES STILL COME, THE LAST ${s1} S AGO; ASKED FOR ${secs} S${more})` : ` (NOTHING FROM THE FM-1 FOR ${s1} S)`));
            }
            await wait(200);
        }
    };
    t.check = (ok, what) => { ctx.checks++; if (!ok) throw new Failed(what); };
    t.note = (text) => ctx.notes.push(text);
    t.skip = (why) => { throw new Skipped(why); };
    /* a button let go: the next input waits out COMBINATION GRACE (FX, SEL, ARP ... combine with what comes within
     * it: a key 60 ms after FX's tap would be FX + the key) */
    const graceGap = () => Math.max(GAP, (ctx.last ? ctx.last.grace : 100) + 60);
    /* a press of ms: its release timed by the MIDI port itself when the page can (io.sendAt), so it is exact however
     * late the page's own timers run (a browser slows them for a page out of sight: a 60 ms tap of FX became a hold) */
    const press = async (on, off, ms) => {
        io.send(on);
        if (io.sendAt) { io.sendAt(off, ms); await wait(ms); } else { await wait(ms); io.send(off); }
    };
    t.down = async (b) => { io.send(btn(b, 1)); await wait(GAP); };
    t.up = async (b) => { io.send(btn(b, 0)); await wait(graceGap()); };
    t.tap = async (b) => { await press(btn(b, 1), btn(b, 0), TAP); await wait(graceGap()); };
    t.hold = async (b, ms) => { await press(btn(b, 1), btn(b, 0), ms); await wait(graceGap()); };
    t.keyDown = async (k) => { io.send(key(k, 1)); await wait(GAP); };
    t.keyUp = async (k) => { io.send(key(k, 0)); await wait(GAP); };
    t.keyTap = async (k) => { await press(key(k, 1), key(k, 0), TAP); await wait(GAP); };
    t.keyHold = async (k, ms) => { await press(key(k, 1), key(k, 0), ms); await wait(GAP); };
    t.keysDown = (ks) => { for (const k of ks) io.send(key(k, 1)); };
    t.keysUp = (ks) => { for (const k of ks) io.send(key(k, 0)); };
    /* clicks, then the state: a pass answers it after taking its input (app.c), so the clicks are taken before
     * whatever comes next. A press sharing a pass with the last clicks would be taken first, as the button held + the
     * knob, a combination and no tap (on the FM-1 a pass runs past 50 ms while a list redraws: NEW SONG's SEL was lost) */
    t.turn = async (k, n) => {
        for (let i = 0; i < Math.abs(n); i++) { io.send(knob(k, n < 0 ? -1 : 1)); await wait(CLICK); }
        await t.ask();
    };
    /* clicks one at a time, at seeking's pace (each a step of its own, never a fast turn: the first too, after a turn),
     * the state asked after each, so the next is sent only once the FM-1 has taken it (a main loop held up, as by a
     * list built on the FM-1, would take the clicks waiting all in one pass: a fast turn); then the state */
    t.clicks = async (k, n) => {
        let s = null;
        for (let i = 0; i < Math.abs(n); i++) { await wait(SEEK_CLICK); io.send(knob(k, n < 0 ? -1 : 1)); s = await t.ask(); }
        return s || t.ask();
    };
    /* held + what fn does; let go, the next input waits out COMBINATION GRACE (a knob turned within it would still
     * count as held + knob) */
    t.combo = async (held, fn) => {
        await t.down(held);
        try { await fn(); } finally { io.send(btn(held, 0)); await wait(graceGap()); }
    };
    t.waitFor = async (pred, ms, what) => {
        const end = ms / 100;
        let said = "";                                  /* (a toast seen while waiting: gone again by the end, often) */
        for (let i = 0; i <= end; i++) {
            const s = await t.ask();
            if (pred(s)) return s;
            if (s.toast) said = s.toast;
            await wait(100);
        }
        const s = ctx.last;                             /* what the FM-1 showed at the end of the wait */
        const at = s ? ` (LAST: ${s.screen}, ${s.overlay ? `OVERLAY ${s.overlay} "${s.words}"` : "NO OVERLAY"}, ` +
                       `SONG "${s.songName}", ${s.unsaved} UNSAVED${said ? `; IT SAID "${said}"` : ""})` : "";
        throw new Failed(`WAITED ${(ms / 1000).toFixed(1)} S FOR ${what}${at}`);
    };
    /* seeking: knob k a click at a time until read(state) is target (a number), at most max clicks; or, with a
     * predicate for target, turning dir until it holds */
    t.seek = async (k, read, target, what, max = 80) => {
        let s = await t.ask(), last = null, same = 0;
        for (let i = 0; i < max; i++) {
            const v = read(s);
            if (typeof target === "function" ? target(s) : v === target) return s;
            const dir = typeof target === "function" ? 1 : v < target ? 1 : -1;
            same = v === last ? same + 1 : 0;
            if (same >= 3) break;                       /* (at an end: it no longer moves) */
            last = v;
            io.send(knob(k, dir));
            await wait(SEEK_CLICK);
            s = await t.ask();
        }
        throw new Failed(`COULD NOT REACH ${what} (AT ${read(s)})`);
    };
    t.seekCombo = async (held, k, read, target, what, max) => {   /* (already there: held is not even pressed) */
        let s = await t.ask();
        if (read(s) === target) return s;
        await t.combo(held, async () => { s = await t.seek(k, read, target, what, max); });
        return s;
    };
    /* the cartridge cart picked by SEL + PRESETS (read: where the screen keeps it), a second go when the first did not
     * reach it (a SEL press not taken leaves PRESETS turning the slots); then a built-in (F0-FF, always on the FM-1)
     * fails the scenario with the why, the user's (a demo disk's) skips it as missing */
    t.pickCart = async (read, cart) => {
        const what = `THE CARTRIDGE ${hex2(cart)}`;
        try { return await t.seekCombo(B.SEL, K.PRESETS, read, cart, what, 40); }
        catch (e) { if (!(e instanceof Failed)) throw e; }
        try { return await t.seekCombo(B.SEL, K.PRESETS, read, cart, what, 40); }
        catch (e) {
            if (!(e instanceof Failed) || cart >= 0xF0) throw e;
            return t.skip(`CARTRIDGE ${hex2(cart)} NOT ON THE FM-1 (${e.message})`);
        }
    };
    t.screen = async (name) => {
        if (name === "FX") {                            /* (FX tapped again once if the first did not take) */
            for (let i = 0; ; i++) {
                const s = await t.ask();
                if (s.screen !== "FX") await t.tap(B.FX);
                try { return await t.waitFor((x) => x.screen === "FX", 2000, "THE FX SCREEN"); }
                catch (e) { if (i || !(e instanceof Failed)) throw e; }
            }
        }
        await t.combo(B.HOME, () => t.keyTap(SCREEN_KEY[name]));
        return t.waitFor((s) => s.screen === name && !s.overlay, 2000, `THE ${name} SCREEN`);
    };
    /* a measured stretch: FIGURES' windows, and the state looked at every WATCH ms (none when the FM-1 does not
     * answer: a progress bar holds its main loop). A question no scenario asks for (MEMORY FOR EDITS IS FULL) or
     * the edit memory filling past three quarters while it runs ends it, and the scenario fails with the why. Its
     * length by the run's clock (late timers make it no longer). With until: it ends as soon as a look finds
     * until(state) true, else fails after ms (what: what it waited for). The scenario's first one starts its test:
     * the counter's time from there (ctx.begin) */
    const stretch = async (name, ms, until, what) => {
        if (ctx.begin) ctx.begin();
        io.log(`${ctx.name}: ${name}${until ? "" : `, ${Math.round(ms * ctx.scale / 1000)} s`}`);
        const s0 = await io.ask();
        io.measure(name);
        const from = now(), end = from + Math.max(200, ms * ctx.scale);
        try {
            for (let left = end - now(); left > 0; left = end - now()) {
                await wait(Math.min(left, WATCH));
                const s = await io.ask();
                if (!s) continue;
                ctx.last = s;
                if (until && until(s)) return s;
                if (s.overlay === 2) throw new Failed(`A QUESTION CAME UP: ${s.words}`);
                const e = s.edits, e0 = s0 && s0.edits;
                if (e && e0 && e.top > e0.top && e.top * 4 > e.limit * 3)
                    throw new Failed(`THE EDIT MEMORY FILLS: ${editsText(e).toUpperCase()} (${kb(e0.top)} KB AT THE START)`);
            }
            if (until) throw new Failed(`WAITED ${(ms / 1000).toFixed(0)} S FOR ${what}`);
            return null;
        } finally { ctx.stretches.push({ name, windows: io.measured(), secs: (now() - from) / 1000 }); }
    };
    const now = () => (io.now ? io.now() : Date.now());
    t.now = now;
    t.until = (at) => wait(Math.max(0, at - now()));    /* (a time by the run's clock) */
    t.begin = () => { if (ctx.begin) ctx.begin(); };    /* the test starts here (a scenario whose steps are its test) */
    t.measure = (name, ms) => stretch(name, ms);
    t.measureUntil = (name, until, ms, what) => stretch(name, ms, until, what);
    t.freeze = (on) => { if (io.freeze) io.freeze(on); };   /* FREEZE THE SCREEN, with the next FIGURES' question */
    t.canFreeze = !!io.freeze;
    /* a SEL menu's action by its name (the state's words: the highlighted item), opened and picked by opener */
    t.menuPick = async (opener, name) => {
        await t.tap(opener);
        await t.waitFor((x) => x.overlay === 1, 2000, "THE MENU");
        await t.turn(K.SELECT, -20);                    /* (the top: it stops there), then down an item at a time */
        let s = await t.ask();
        for (let i = 0; i < 20 && s.words !== name; i++) {
            io.send(knob(K.SELECT, 1));
            await wait(SEEK_CLICK);
            s = await t.ask();
        }
        t.check(s.words === name, `NO ${name} IN THE MENU (AT ${s.words})`);
        await t.tap(opener);
    };
    t.prompt = async (text) => { io.log(text); await io.prompt(text); if (io.stopped()) throw new Stopped("STOPPED"); };
    /* the FM-1's USB audio input taken by the page (as LISTEN takes it: the FM-1 then sends its audio), or let go:
     * { ok, why, name, check } (check: the page can tell whether the FM-1 sent it) */
    t.usbIn = async (on) => io.usbIn ? io.usbIn(on) : { ok: false, why: "THIS PAGE CANNOT TAKE THE FM-1'S USB AUDIO" };
    /* the page records the input it took (usbIn) into this scenario's own WAV, in the run's ZIP, or ends that:
     * { ok, why, check, secs } (check: the page can tell how much it recorded) */
    t.record = async (on) => io.record ? io.record(on) : { ok: false, why: "THIS PAGE CANNOT RECORD THE FM-1'S USB AUDIO" };
    t.windows = (prefix) => ctx.stretches.filter((x) => x.name.startsWith(prefix)).flatMap((x) => x.windows);  /* measured */
    t.allSoundOff = () => t.combo(B.HOME, () => t.tap(B.REC));
    /* SETTINGS' list (page 0) or MIDI page (1): line, then K1 until read(state) is the value */
    t.setting = async (page, line, read, value, what) => {
        await t.screen("SETTINGS");
        await t.seek(K.ALG, (s) => s.cursor[0], page, "SETTINGS' PAGE");
        await t.seek(K.SELECT, (s) => s.cursor[1], line, `THE LINE ${what}`);
        return t.seek(K.K1, read, value, what, 40);
    };
    /* REVERT by its shortcut, SAVE + OCT-. SAVE is held past HOLD DELAY first, its layer open, and seen held (a
     * state asked after it), before OCT- comes: a SAVE let go after its layer, or after a combination, is no tap,
     * so it never saves, whatever happens (a SAVE and an OCT- seen in one main-loop pass would be OCT- first,
     * then a SAVE alone). Then REVERT'S question, answered REVERT (OCT+). */
    t.revert = async () => {
        let s = await t.ask();
        if (!s.unsaved) return;
        io.send(btn(B.SAVE, 1));
        try {
            await wait(s.holdDelay + 300);
            await t.ask();
            await t.tap(B.OCTM);
            s = await t.waitFor((x) => x.overlay === 2, 2000, "REVERT'S QUESTION");
        } finally { io.send(btn(B.SAVE, 0)); await wait(graceGap()); }
        t.check(s.words === "REVERT TO SAVED?", `EXPECTED REVERT'S QUESTION, FOUND "${s.words}"`);
        await t.tap(B.OCTP);
        await t.waitFor((x) => !x.overlay && !x.unsaved, 20000, "THE REVERT");
    };
    /* a question the step asks first (its words matching re), answered yes (OCT+: CLEAR, DELETE, LEVEL) */
    t.yes = async (re, what) => {
        await t.waitFor((x) => x.overlay === 2 && re.test(x.words || ""), 2000, what);
        await t.tap(B.OCTP);
    };
    /* the known place every scenario starts from (specs/debug.md: A scenario) */
    t.prelude = async () => {
        let s = await t.ask();
        if (!s.mirror) { io.send(hello(1)); await wait(100); s = await t.ask(); t.check(s.mirror, "MIRROR DID NOT SWITCH ON"); }
        for (let i = 0; i < 6 && s.overlay; i++) { await t.tap(B.HOME); s = await t.ask(); }
        t.check(!s.overlay, `AN OVERLAY STAYS OPEN (${OVERLAY[s.overlay] || s.overlay})`);
        if (s.playing && s.what === 4) { await t.screen("DJ"); await t.tap(B.PLAY); }
        if (s.playing && s.what === 3) { await t.screen("LOOPS"); await t.tap(B.PLAY); }
        s = await t.screen("SONGS");
        if (s.playing) { await t.tap(B.PLAY); s = await t.waitFor((x) => !x.playing, 2000, "THE SONG TO STOP"); }
        if (s.arp) { await t.tap(B.ARP); s = await t.waitFor((x) => !x.arp, 2000, "ARP OFF"); }
        await t.allSoundOff();
        await t.revert();
        s = await t.ask();
        t.check(!s.playing && !s.unsaved && !s.overlay, "THE START: NOT STOPPED, OR CHANGES LEFT");
        return s;
    };
    /* SONGS: the song named name, its start position pos, played; all sound off first, so nothing of what played
     * before (released notes, the effects' tails) rings into it */
    /* a song from position pos (LAST: its last, ALGORITHM turned up a click at a time till it moves no more), from
     * silence; t.pos: where it started */
    t.playSong = async (name, pos) => {
        await t.screen("SONGS");
        const n = ctx.songNames.indexOf(name);
        if (n < 0) t.skip(`${name} IS NOT ON THE FM-1`);
        let s = await t.seek(K.SELECT, (x) => x.cursor[0], n, `THE SONG ${name}`, 300);
        if (pos === LAST) {
            for (let i = 0, was = -1; i < 64 && s.cursor[1] !== was; i++) {
                was = s.cursor[1];
                io.send(knob(K.ALG, 1));
                await wait(SEEK_CLICK);
                s = await t.ask();
            }
        } else if (pos) {
            s = await t.seek(K.ALG, (x) => x.cursor[1], pos, `POSITION ${pos}`, 40);
        }
        t.pos = s.cursor[1];
        await t.allSoundOff();
        await t.tap(B.PLAY);
        return t.waitFor((s) => s.playing && s.what === 1 && s.song === n, 3000, `${name} PLAYING`);
    };
    t.fx = async (name, on) => {                     /* on the FX screen (LIVE): the effect picked, on or off */
        let s = await t.screen("FX");
        if (s.cursor[0] !== 0) { s = await t.seek(K.ALG, (x) => x.cursor[0], 0, "FX LIVE"); }
        await t.keyTap(FXKEY[name]);
        s = await t.waitFor((x) => x.cursor[1] === FXNUM[name], 1000, `${name} PICKED`);
        if (fxSwitched(s, name) !== on) await t.tap(on ? B.OCTP : B.OCTM);
        return t.waitFor((x) => fxSwitched(x, name) === on, 1500, `${name} ${on ? "ON" : "OFF"}`);
    };
    return t;
}

/* the songs' names by number, as SONGS shows them while SELECT passes over them (read once a run) */
async function learnSongs(t, ctx) {
    await t.screen("SONGS");
    let s = await t.seek(K.SELECT, (x) => x.cursor[0], 0, "THE FIRST SONG", 300);
    for (let i = 0; i < 64; i++) {
        ctx.songNames[s.cursor[0]] = s.songName;
        const was = s.cursor[0];
        t.send(knob(K.SELECT, 1));
        await t.sleep(SEEK_CLICK);
        s = await t.ask();
        if (s.cursor[0] === was) break;
    }
}

/* ---- the scenarios ---- */
const DEMO = ["PUNCH IN", "SPICE ROAD", "MIDNIGHT RUN", "CITY LIGHTS", "ACID TEST", "ADVENTURE"];
const LOOPS = ["TRANCE LOOP", "BREAKS LOOP", "TECHNO LOOP", "ACID LOOP", "DISCO LOOP", "CHIP LOOP", "HIP-HOP LOOP",
               "DNB LOOP", "ROCK LOOP", "METAL LOOP", "ELECTRO LOOP", "WORLD LOOP"];
const HEAVY = { "PUNCH IN": 2, "SPICE ROAD": 3, "MIDNIGHT RUN": 3, "CITY LIGHTS": 3, "ACID TEST": 3, "ADVENTURE": 3 };
const LAST = "LAST";                     /* playSong's position: the song's last (a loop's heaviest, usually) */
const KEYS_N = [1, 4, 8, 12, 16];                       /* (past 10: core 2's voices) */
const SPREAD = [0, 4, 7, 11, 14, 2, 9, 16, 5, 12, 19, 21, 24, 18, 23, 26];  /* the keys held, in the order added */
/* each engine on the keys: its cartridge and slot, the keys held (the TB-303 plays one note at a time, legato: one
 * key, then 4, 8 and 12 voices as loops, tb303Voices; the sampler's instrument loops, so it sounds the whole time it
 * is held) */
const ENGINES = [["DX7", 0xF4, 0x00, KEYS_N], ["JUNO RESONANT", 0xF8, 0x25, KEYS_N], ["JUNO PLAIN", 0xF8, 0x4A, KEYS_N],
                 ["TB-303", 0xFA, 0x00, [1]], ["SAMPLER", 0x03, 0x08, KEYS_N]];

const voicesSetting = (t, n) => t.setting(0, SET.VOICES, (s) => s.voiceCount, n, `VOICES ${n}`);

/* KEYS' and IDLE's one start, whatever the scenario before left: WORLD LOOP played a moment from its start, paused
 * and all sound off, so it is what played last and the keys sound through its effects, DELAY and REVERB (at rest
 * till a sound comes); as the KEYS runs measured before */
const KEYS_SONG = "WORLD LOOP";
async function keysStart(t) {
    await t.playSong(KEYS_SONG, 0);
    await t.tap(B.PLAY);
    await t.waitFor((s) => !s.playing, 2000, `${KEYS_SONG} PAUSED`);
    await t.allSoundOff();
}

/* an engine on the keys (KEYS', KEYS AT 12's), VOICES 12 set before: from KEYS' start, its cartridge and slot picked on
 * INSTRUMENTS, then each count of keys held 5 s and measured (`DX7 X12`), its release rung out */
async function keysHeld(t, name, cart, slot, keysN) {
    await keysStart(t);
    await t.screen("INSTRUMENTS");
    let s = await t.pickCart((x) => x.cursor[0], cart);
    s = await t.seek(K.PRESETS, (x) => x.cursor[1], slot, `THE SLOT ${hex2(slot)}`, 140);
    t.check(s.cart === cart && s.slot === slot, `THE KEYS PLAY ${hex2(s.cart)}:${hex2(s.slot)}, NOT ${hex2(cart)}:${hex2(slot)}`);
    for (const n of keysN) {
        const ks = SPREAD.slice(0, n);
        t.keysDown(ks);
        await t.sleep(300);
        if (n === keysN[0]) {
            s = await t.ask();
            t.check(fxOn(s, "DELAY") && fxOn(s, "REVERB"), `THE KEYS WITHOUT ${KEYS_SONG}'S DELAY AND REVERB AT WORK`);
        }
        await t.measure(`${name} X${n}`, 5000);
        t.keysUp(ks);
        await t.sleep(1500);                            /* (their release rings out) */
    }
}

/* ONE CORE's (specs/debug.md: the scenarios): body with core 2 off by 38 SECOND CORE 0 from the scenario's start, back
 * to what SETTINGS says (38 with 1) at its end, whatever happens; every window measured must show it so in its 37 (off
 * by 38, no block handed over), so the figures compare with the same scenario on two cores */
async function oneCore(t, body) {
    t.send(SECOND(0));
    try {
        await body();
        const all = t.windows(""), w = all.filter((f) => f.c2), two = w.filter((f) => !f.c2.forced || f.c2.blocks);
        t.check(!two.length, two.length ? `CORE 2 NOT OFF IN ${two.length} OF ITS ${w.length} WINDOWS (${FG.c2Text(two[0].c2)}, ` +
                                          `${two[0].c2.blocks} BLOCKS HANDED OVER)` : "");
        t.check(!all.length || w.length, `NO 37 FIGURES 2 WITH ITS ${all.length} WINDOWS: CORE 2 OFF NOT SEEN`);
        if (w.length) t.note(`core 2 ${FG.c2Text(w[w.length - 1].c2)} in its ${w.length} windows, no block handed over`);
        else t.note("no FIGURES window measured: core 2 off not seen");
    } finally { t.send(SECOND(1)); }
}

/* DJ's properties (GLO) as a new store has them, whatever stand REVERT brought back: CROSSFADER SMOOTH, FILTER
 * RESONANCE 30 %, FILTER THE SENDS ON, FX LFO REVERB MIX. Each item from the top by SELECT, K1 until DJ's record
 * (in the state) says so; on DJ */
async function djProps(t) {
    const as = (x) => x.dj.rec[1] === 0 && x.dj.rec[2] === 30 && x.dj.rec[3] === 0 && x.dj.rec[4] === 0;
    let s = await t.ask();
    if (as(s)) return;
    await t.tap(B.GLO);
    await t.waitFor((x) => x.overlay === 3, 1000, "DJ PROPERTIES");
    await t.turn(K.SELECT, -6);                         /* CROSSFADER, on top */
    await t.seek(K.K1, (x) => x.dj.rec[1], 0, "THE CROSSFADER SMOOTH", 4);
    await t.turn(K.SELECT, 1);
    await t.seek(K.K1, (x) => x.dj.rec[2], 30, "FILTER RESONANCE 30 %", 110);
    await t.turn(K.SELECT, 1);
    await t.seek(K.K1, (x) => x.dj.rec[3], (x) => x.dj.rec[3] === 0, "FILTER THE SENDS ON", 4);   /* (clockwise: ON) */
    await t.turn(K.SELECT, 1);
    await t.seek(K.K1, (x) => x.dj.rec[4], 0, "THE FX LFO ON THE REVERB'S MIX", 4);
    await t.tap(B.GLO);
    s = await t.waitFor((x) => !x.overlay, 1000, "DJ PROPERTIES CLOSED");
    t.check(as(s), "DJ'S PROPERTIES AS AT FIRST (SMOOTH, 30 %, THE SENDS FILTERED, REVERB MIX)");
}

/* the DJ stand (specs/debug.md: The scenarios, DJ) */
async function djStand(t) {
    await t.setting(1, MIDI_SET.SYNC, (s) => s.sync, 0, "SYNC INTERNAL");
    let s = await t.screen("DJ");
    if (s.cursor[1] === 2) { await t.tap(B.LFO); s = await t.waitFor((x) => x.cursor[1] !== 2, 1000, "THE PADS OUT OF LFO"); }
    await djProps(t);
    if (s.cursor[2] !== 0) { await t.tap(B.ENV); s = await t.waitFor((x) => x.cursor[2] === 0, 1000, "K2 K3: FILTER"); }
    const load = async (name, d) => {
        const n = t.songNames.indexOf(name);
        if (n < 0) t.skip(`${name} IS NOT ON THE FM-1`);
        await t.seek(K.SELECT, (x) => x.cursor[0], n, `${name} IN THE LIST`, 300);
        await t.keyTap(BLACK[d ? 4 : 3]);              /* OP4: LOAD left, OP5: LOAD right */
        return t.waitFor((x) => x.dj.decks[d].name === name, 3000, `${name} ON THE ${d ? "RIGHT" : "LEFT"} DECK`);
    };
    await load("TECHNO LOOP", 0);
    s = await load("ELECTRO LOOP", 1);
    if (!s.dj.sync) { await t.keyTap(BLACK[8]); s = await t.waitFor((x) => x.dj.sync, 1000, "SYNC ON"); }
    s = await t.seek(K.ALG, (x) => x.dj.xfade, 6, "THE CROSSFADER AT 6 OF 32", 60);
    for (const [d, kn, v] of [[0, K.K2, -29], [1, K.K3, 25]]) {
        if (!s.dj.decks[d].filterOn) { await t.keyTap(BLACK[d ? 7 : 2]); s = await t.waitFor((x) => x.dj.decks[d].filterOn, 1000, "THE FILTER ON"); }
        s = await t.seek(kn, (x) => x.dj.decks[d].filter, v, `THE ${d ? "RIGHT" : "LEFT"} FILTER AT ${v}`, 120);
    }
    await t.tap(B.LFO);                                 /* the LFO pads */
    s = await t.waitFor((x) => x.cursor[1] === 2, 1000, "THE LFO PADS");
    const pad = (i) => t.keyTap(WHITE[i - 1]);
    const until = async (i, read, v, what) => {          /* a shape or range pad tapped until read(state) is v */
        for (let n = 0; n < 7; n++) { s = await t.ask(); if (read(s) === v) return; await pad(i); }
        throw new Failed(`COULD NOT SET ${what}`);
    };
    await until(2, (x) => x.dj.xlfo.shape, 2, "THE CROSSFADER LFO TRIANGLE");
    await until(3, (x) => x.dj.xlfo.range, 3, "THE CROSSFADER LFO MIRROR");
    await until(6, (x) => x.dj.decks[0].lfo.shape, 2, "THE LEFT FILTER LFO TRIANGLE");
    await until(7, (x) => x.dj.decks[0].lfo.range, 2, "THE LEFT FILTER LFO HERE TO FULL");
    await until(10, (x) => x.dj.decks[1].lfo.shape, 2, "THE RIGHT FILTER LFO TRIANGLE");
    await until(11, (x) => x.dj.decks[1].lfo.range, 3, "THE RIGHT FILTER LFO SWING");
    await until(14, (x) => x.dj.fxlfo.shape, 5, "THE FX LFO RANDOM");
    await until(15, (x) => x.dj.fxlfo.range, 1, "THE FX LFO 0 TO 100 %");
    const ons = [[1, (x) => x.dj.xlfo], [5, (x) => x.dj.decks[0].lfo], [9, (x) => x.dj.decks[1].lfo], [13, (x) => x.dj.fxlfo]];
    for (const [i, lfo] of ons) {                       /* each at 1/2: its on / off pad held, ALGORITHM its speed (an */
        s = await t.ask();                              /* LFO off goes on at the press and stays on once turned) */
        if (lfo(s).speed === 6) continue;
        await t.keyDown(WHITE[i - 1]);
        try { await t.seek(K.ALG, (x) => lfo(x).speed, 6, `LFO ${i}'S SPEED 1/2`, 12); } finally { await t.keyUp(WHITE[i - 1]); }
    }
    for (const [i, lfo] of ons) {
        s = await t.ask();
        if (!lfo(s).on) await pad(i);
    }
    s = await t.ask();
    t.check(ons.every(([, lfo]) => lfo(s).on && lfo(s).speed === 6), "THE FOUR LFOS ON, EACH AT 1/2");
    await t.tap(B.LFO);                                 /* back to the PLAY pads */
    await t.fx("CHORUS", false);                        /* DJ's own effects: FX from DJ edits DJ's setup */
    await t.fx("DELAY", true);
    await t.fx("REVERB", true);
    await t.tap(B.FX);
    await t.waitFor((x) => x.screen === "DJ", 2000, "BACK ON DJ");
    await t.keyTap(BLACK[0]);                           /* OP1: the left deck plays */
    await t.waitFor((x) => x.dj.decks[0].playing, 2000, "THE LEFT DECK PLAYING");
    await t.keyTap(BLACK[5]);                           /* OP6: the right, on the main's bar line */
    return t.waitFor((x) => x.dj.decks[0].playing && x.dj.decks[1].playing, 8000, "BOTH DECKS PLAYING");
}

/* DJ with SYNC INTERNAL, the PLAY pads, its properties as at first, left and right loaded */
async function djLoad(t, left, right) {
    await t.setting(1, MIDI_SET.SYNC, (s) => s.sync, 0, "SYNC INTERNAL");
    let s = await t.screen("DJ");
    if (s.cursor[1] === 2) { await t.tap(B.LFO); s = await t.waitFor((x) => x.cursor[1] !== 2, 1000, "THE PADS OUT OF LFO"); }
    await djProps(t);
    for (const [name, d] of [[left, 0], [right, 1]]) {
        const n = t.songNames.indexOf(name);
        if (n < 0) t.skip(`${name} IS NOT ON THE FM-1`);
        await t.seek(K.SELECT, (x) => x.cursor[0], n, `${name} IN THE LIST`, 300);
        await t.keyTap(BLACK[d ? 4 : 3]);
        s = await t.waitFor((x) => x.dj.decks[d].name === name, 3000, `${name} ON THE ${d ? "RIGHT" : "LEFT"} DECK`);
    }
    return s;
}

/* two songs on the decks, SYNC off, the crossfader in the middle, DJ plain (djPlain), both playing (DJ TWO TEMPOS) */
async function djTwo(t, left, right) {
    let s = await djLoad(t, left, right);
    if (s.dj.sync) { await t.keyTap(BLACK[8]); s = await t.waitFor((x) => !x.dj.sync, 1000, "SYNC OFF"); }
    s = await t.seek(K.ALG, (x) => x.dj.xfade, 16, "THE CROSSFADER IN THE MIDDLE", 60);
    await djPlain(t, s);
    await t.keyTap(BLACK[0]);                           /* OP1, OP6: both decks play, each at its own place */
    await t.keyTap(BLACK[5]);
    return t.waitFor((x) => x.dj.decks[0].playing && x.dj.decks[1].playing, 8000, "BOTH DECKS PLAYING");
}

/* two full songs mixed (DJ HEAVY): SYNC on, the crossfader in the middle, DJ plain (djPlain), each deck at its second
 * pattern (the first is an intro), both playing, the right from the main's bar line */
async function djHeavy(t, left, right) {
    let s = await djLoad(t, left, right);
    if (!s.dj.sync) { await t.keyTap(BLACK[8]); s = await t.waitFor((x) => x.dj.sync, 1000, "SYNC ON"); }
    s = await t.seek(K.ALG, (x) => x.dj.xfade, 16, "THE CROSSFADER IN THE MIDDLE", 60);
    s = await djPlain(t, s);
    if (s.cursor[1] === 1) { await t.keyTap(BLACK[10]); s = await t.waitFor((x) => x.cursor[1] === 0, 1000, "THE PLAY PADS"); }
    for (const d of [0, 1]) {                           /* MOVE held, K2 / K3 a pattern on, let go: paused, there at once */
        await t.keyDown(WHITE[d ? 11 : 3]);
        await t.turn(d ? K.K3 : K.K2, 1);
        await t.keyUp(WHITE[d ? 11 : 3]);
        s = await t.waitFor((x) => x.dj.decks[d].pos === 1 && x.dj.decks[d].row === 0, 1000,
                            `THE ${d ? "RIGHT" : "LEFT"} DECK AT ITS SECOND PATTERN`);
    }
    await t.keyTap(BLACK[0]);                           /* OP1: the left deck plays; OP6: the right, on the bar line */
    await t.waitFor((x) => x.dj.decks[0].playing, 2000, "THE LEFT DECK PLAYING");
    await t.keyTap(BLACK[5]);
    return t.waitFor((x) => x.dj.decks[0].playing && x.dj.decks[1].playing, 8000, "BOTH DECKS PLAYING");
}

/* DJ plain, whatever stand REVERT brought back: no DJ FILTER, no LFO, DJ's DELAY and REVERB on (its CHORUS off); back
 * on DJ with the PLAY pads (s: the state on DJ) */
async function djPlain(t, s) {
    for (const d of [0, 1]) {
        if (s.dj.decks[d].filterOn) { await t.keyTap(BLACK[d ? 7 : 2]); s = await t.waitFor((x) => !x.dj.decks[d].filterOn, 1000, "THE FILTER OFF"); }
    }
    const lfos = [[1, (x) => x.dj.xlfo.on], [5, (x) => x.dj.decks[0].lfo.on], [9, (x) => x.dj.decks[1].lfo.on], [13, (x) => x.dj.fxlfo.on]];
    if (lfos.some(([, on]) => on(s))) {
        await t.tap(B.LFO);                             /* the LFO pads: each one on switched off */
        s = await t.waitFor((x) => x.cursor[1] === 2, 1000, "THE LFO PADS");
        for (const [i, on] of lfos) { s = await t.ask(); if (on(s)) await t.keyTap(WHITE[i - 1]); }
        await t.tap(B.LFO);
    }
    s = await t.ask();
    t.check(!lfos.some(([, on]) => on(s)) && !s.dj.decks[0].filterOn && !s.dj.decks[1].filterOn, "NO FILTER, NO LFO");
    await t.fx("CHORUS", false);                        /* DJ's own effects: FX from DJ edits DJ's setup */
    await t.fx("DELAY", true);
    await t.fx("REVERB", true);
    await t.tap(B.FX);
    return t.waitFor((x) => x.screen === "DJ", 2000, "BACK ON DJ");
}

/* the loop station (specs/ui/screens/loops.md): n loops recorded from the keys, a note held over each one's take, the
 * first half on JUNO-106 sounds, the rest on DX7's, the next sound for each (PRESETS); the first armed by REC, started by
 * its note and closed by REC on its bar line (a bar); each next one REC on + NEW LOOP while the loops play (armed, its
 * take from their next start, one pass) */
async function loopStation(t, n) {
    const k = loopsKit(t);
    await k.clear();
    let l;
    for (let i = 0; i < n; i++) {
        const key = SPREAD[i % SPREAD.length];
        if (i === 0) await k.sound(0xF8, 0x4A);
        else if (i === n >> 1) await k.sound(0xF4, 0x00);
        else {
            const s0 = await t.ask(), s = await t.clicks(K.PRESETS, 1);   /* (the next sound) */
            t.check(s.slot === s0.slot + 1, `PRESETS: THE NEXT SOUND (${hex2(s0.cart)}:${hex2(s0.slot)} -> ${hex2(s.cart)}:${hex2(s.slot)})`);
        }
        if (!i) { l = lps(await k.first([[0, [key], 12]])); continue; }
        l = await k.take([[0, [key], k.lenRows - 4]]);
        t.check(l.added > 0, `LOOP ${hex2(i)}: NO NOTE TAKEN`);
    }
    t.check(l.n === n, `${n} LOOPS (THERE ARE ${l.n})`);
    return l;
}

/* the EDITOR's part of the state (its cursor, specs/connections.md#the-state): the pattern it is on (FE its own), the
 * cursor's row and track, its own pattern holding anything */
const OWN_PAT = 0xFE;
const ed = (s) => ({ pat: s.cursor[0], row: s.cursor[1], track: s.cursor[2], holds: !!(s.cursor[3] & 1) });
const WARNING = /^EDITOR CHANGES WILL BE/;      /* (EDITOR CHANGES WILL BE LOST: the state's 24 bytes cut it) */
/* an item of the EDITOR's SEL menu (the cell's), its warning answered YES if it comes: the state after */
async function editorOwn(t, item) {
    await t.menuPick(B.SEL, item);
    /* (SEL acts on its release, after the combination grace: on a slow pass that is after the next look) */
    let s = await t.waitFor((x) => x.overlay !== 1, 2000, `${item} TAKEN`);
    if (s.overlay === 2 && WARNING.test(s.words)) {
        await t.tap(B.OCTP);
        s = await t.waitFor((x) => x.overlay !== 2, 2000, "THE WARNING ANSWERED");
    }
    return s;
}

/* LOOPS' part of the state (its cursor, specs/connections.md#the-state): the loops, the one selected (the count: + NEW
 * LOOP), the notes it starts; a take armed, recording, the keys in CONTROL mode, the selected loop muted, soloed, a song
 * armed, recording, the pattern recorder open; the length in bars, the row, the notes all the loops start */
const lps = (s) => ({ n: s.cursor[0], sel: s.cursor[1], notes: s.cursor[2], armed: !!(s.cursor[3] & 1),
                      take: !!(s.cursor[3] & 2), control: !!(s.cursor[3] & 4), muted: !!(s.cursor[3] & 8),
                      soloed: !!(s.cursor[3] & 16), songArmed: !!(s.cursor[3] & 32), song: !!(s.cursor[3] & 64),
                      recorder: !!(s.cursor[3] & 128), bars: s.cursor[4], row: s.cursor[5],
                      all: s.cursor[6] | s.cursor[7] << 8 });
/* CONTROL mode's black keys by their print (specs/ui/screens/loops.md#control-mode-mutes-and-solos) */
const CTL = { ROW: BLACK[3], PIT: BLACK[6], GLO: BLACK[7], MONO: BLACK[8], POLY: BLACK[9] };
const KIT = [0x06, 0x05];               /* FUSION's DRUM KIT (the demo disk's): a drum a key, the kick on C-4 */
const TB303 = [0xFA, 0x00], EL_PIANO = [0xF0, 0x00];
const NOTE_NAMES = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"];
/* the pattern recorder's beat: a drum a row (the kit's KICK C-4, SNARE D-4, HAT F#4, HAT OPEN A#4, as MIDI notes) and
 * its steps, 10 notes */
const BEAT = [[60, [0, 4, 8, 12]], [62, [4, 12]], [66, [2, 6, 10]], [70, [14]]], BEAT_NOTES = 10;
/* takes: [row, keys, rows held (0: a tap)], from the loops' start, a row a sixteenth (GRID 1/16); keys from F3 (0) */
const BASS = [[0, [0], 2], [3, [0], 2], [6, [7], 2], [10, [5], 3]];              /* F3 F3 C4 A#3 */
const BASS_AGAIN = [[0, [5], 3], [4, [7], 2], [8, [0], 2], [12, [12], 2]];       /* A#3 C4 F3 F4 */
const PAD = [[0, [7, 11, 14, 18], 8]];                                         /* C4 E4 G4 B4, held half a bar */
/* LOOPS' steps as a hand's (specs/ui/screens/loops.md), each checked through LOOPS' cursor in the state. k.rowMs a row's
 * time (GRID 1/16: four rows a beat), k.lenRows the loops' length in rows and k.barRows a bar's, from k.timing once the
 * loops play. The keys' mode (CONTROL), its white keys' MUTE or SOLO and the pattern recorder's settings stay as they
 * were left, across screens and runs, until power-off: each is set where it matters */
function loopsKit(t) {
    const k = { lenRows: 16, barRows: 16, rowMs: 125 };
    const L = k.L = async () => lps(await t.ask());
    const passMs = () => k.lenRows * k.rowMs;
    k.keys = async (control) => {                       /* the keys in CONTROL mode or in PLAY mode (SEQ) */
        if ((await L()).control !== control) await t.tap(B.SEQ);
        return t.waitFor((x) => lps(x).control === control, 1000, `THE KEYS IN ${control ? "CONTROL" : "PLAY"} MODE`);
    };
    k.black = async (key, toast) => {                   /* a black key in CONTROL mode, its toast checked */
        await t.keyTap(key);
        return t.waitFor((x) => x.toast === toast, 1000, toast);
    };
    /* CONTROL mode's white keys to MUTE: GLO's black key switches MUTE and SOLO, its toast saying which it is now */
    k.muteMode = async () => {
        await t.keyTap(CTL.GLO);
        const s = await t.waitFor((x) => /^SEQ: (MUTE|SOLO)$/.test(x.toast), 1000, "SEQ: MUTE OR SOLO");
        if (s.toast === "SEQ: SOLO") await k.black(CTL.GLO, "SEQ: MUTE");
    };
    /* how the next loops record, MONO or POLY (CONTROL mode's black keys) */
    k.mode = async (m) => { await k.keys(true); await k.black(CTL[m], `NEXT LOOPS: ${m}`); await k.keys(false); };
    k.clearLoops = async () => {                        /* SEL's CLEAR LOOPS, answered CLEAR */
        await t.menuPick(B.SEL, "CLEAR LOOPS");
        await t.yes(/^CLEAR ALL \d+ LOOPS?\?$/, "CLEAR LOOPS' QUESTION");
        return t.waitFor((x) => !x.overlay && !lps(x).n, 2000, "THE LOOPS CLEARED");
    };
    /* LOOPS with no loops (the saved ones come back with the next REVERT), nothing playing; GRID 1/16 (LOOPS PROPERTIES'
     * fourth item, K1 to its first), QUANTIZE ROW, the next loops POLY, the white keys MUTE, the keys in PLAY mode */
    k.clear = async () => {
        let s = await t.screen("LOOPS");
        if (lps(s).n) s = await k.clearLoops();
        if (s.playing) { await t.tap(B.PLAY); await t.waitFor((x) => !x.playing, 2000, "NOTHING PLAYING"); }
        await t.tap(B.GLO);
        await t.waitFor((x) => x.overlay === 3, 1000, "LOOPS PROPERTIES");
        await t.clicks(K.SELECT, -7);
        await t.clicks(K.SELECT, 3);
        await t.clicks(K.K1, -2);
        await t.tap(B.GLO);
        await t.waitFor((x) => !x.overlay, 1000, "LOOPS PROPERTIES CLOSED");
        await k.keys(true);
        await k.black(CTL.ROW, "QUANTIZE: ROW");
        await k.black(CTL.POLY, "NEXT LOOPS: POLY");
        await k.muteMode();
        return k.keys(false);
    };
    /* the selection: loop i, or + NEW LOOP (-1: at the bottom, clockwise going down) */
    k.select = async (i, what) => {
        for (let n = 0; n < 24; n++) {
            const l = await L();
            const want = i < 0 ? l.n : i;
            if (l.sel === want) return l;
            t.send(knob(K.SELECT, l.sel < want ? 1 : -1));
            await t.sleep(SEEK_CLICK);
        }
        throw new Failed(`COULD NOT SELECT ${what}`);
    };
    k.sound = async (cart, slot) => {                   /* + NEW LOOP's sound: SEL + PRESETS the cartridge, PRESETS the slot */
        await t.pickCart((x) => x.cart, cart);
        const x = await t.seek(K.PRESETS, (y) => y.slot, slot, `THE SLOT ${hex2(slot)}`, 140);
        t.check(x.cart === cart && x.slot === slot, `+ NEW LOOP PLAYS ${hex2(x.cart)}:${hex2(x.slot)}, NOT ${hex2(cart)}:${hex2(slot)}`);
    };
    /* the loops playing: a row's time from the tempo (four rows a beat), the length's rows as the playhead goes round */
    k.timing = async () => {
        const s = await t.ask(), bars = Math.max(1, lps(s).bars);
        k.rowMs = 150000 / s.bpm10;
        let top = 0;
        for (let i = 0; i < 800; i++) { const r = (await L()).row; if (r < top) break; top = r; await t.sleep(15); }
        k.barRows = Math.ceil((top + 1) / bars);
        k.lenRows = k.barRows * bars;
        return s;
    };
    /* the time the loops' pass under way started (the playhead seen going back to their start), so steps land on their
     * rows */
    k.nextPass = async () => {
        let last = (await L()).row;
        for (let i = 0; i < 800; i++) {
            await t.sleep(15);
            const r = (await L()).row;
            if (r < last) return t.now() - r * k.rowMs;
            last = r;
        }
        throw new Failed("THE LOOPS DO NOT GO ROUND");
    };
    /* a take's steps from t1 (its start), in order: [row, keys, rows held (0: a tap)], keys down together, up together */
    k.press = async (steps, t1) => {
        for (const [r, keys, len] of steps) {
            await t.until(t1 + r * k.rowMs - 10);
            t.keysDown(keys);
            await t.sleep(len ? Math.max(TAP, len * k.rowMs - 30) : TAP);
            t.keysUp(keys);
        }
    };
    /* the first loop from the keys (no loops yet): REC arms it, nothing playing, so its first key starts the loops and the
     * take at once; REC before the bar line closes it there, a bar, the loops playing on */
    k.first = async (steps) => {
        k.rowMs = 150000 / (await t.ask()).bpm10;
        await t.tap(B.REC);
        await t.waitFor((x) => lps(x).armed, 1000, "THE FIRST LOOP ARMED");
        const t1 = t.now() + 10;
        await k.press(steps, t1);
        await t.until(t1 + 15.5 * k.rowMs);
        await t.tap(B.REC);
        const s = await t.waitFor((x) => !lps(x).take && x.playing && x.what === 3, 3000, "THE FIRST LOOP CLOSED, PLAYING");
        const l = lps(s);
        t.check(l.n === 1 && l.bars === 1 && l.all > 0 && s.toast === "LOOP 00: 1 BAR",
                `THE FIRST LOOP: A BAR (${l.n} LOOPS, ${l.bars} BARS, ${l.all} NOTES; "${s.toast}")`);
        await k.timing();
        return s;
    };
    /* a take armed while the loops play: REC on + NEW LOOP (at -1: a new loop), or EDIT on loop at (a re-record, its
     * notes gone at once), not in the last half bar before the loops' next start (else after it), so it is armed in
     * time; its take starts there, its steps played from then on; it is over by itself after its pass. The state after,
     * and the notes it added */
    k.take = async (steps, at = -1) => {
        const l0 = await k.select(at, at < 0 ? "+ NEW LOOP" : `LOOP ${hex2(at)}`);
        if (l0.row >= k.lenRows - 8) await k.nextPass();
        await t.tap(at < 0 ? B.REC : B.EDIT);
        const s = await t.ask();
        t.check(lps(s).armed && !lps(s).take, "THE TAKE NOT ARMED FOR THE LOOPS' NEXT START");
        if (at >= 0) t.check(!lps(s).notes && s.toast === `LOOP ${hex2(at)} RE-RECORDING`,
                             `EDIT: LOOP ${hex2(at)}'S NOTES GONE AT ONCE (${lps(s).notes} LEFT; "${s.toast}")`);
        await k.press(steps, await k.nextPass());
        const l = lps(await t.waitFor((x) => !lps(x).armed && !lps(x).take, passMs() + 2000, "THE TAKE'S END"));
        t.check(l.n === l0.n + (at < 0 ? 1 : 0) && l.sel === (at < 0 ? l.n : at), `AFTER THE TAKE: ${l.n} LOOPS (${l0.n} ` +
                `BEFORE), ${at < 0 ? "+ NEW LOOP" : `LOOP ${hex2(at)}`} SELECTED (THE SELECTION ${hex2(l.sel)})`);
        return { ...l, added: l.all - l0.all };
    };

    /* the pattern recorder (ARP). Its row selected is kept as it closes (recRow); REC writes and closes it, or with no
     * step on is refused (NO STEPS ON), the box staying open */
    let recRow = 0;
    k.recOpen = async () => { await t.tap(B.ARP); return t.waitFor((x) => lps(x).recorder, 1000, "THE PATTERN RECORDER"); };
    k.recRow = async (r) => { await t.clicks(K.SELECT, r - recRow); recRow = r; };
    k.recRec = async () => { await t.tap(B.REC); return t.ask(); };
    const noSteps = (s) => lps(s).recorder && s.toast === "NO STEPS ON";
    /* as a first loop takes it: 16 steps (K2, 4 a click), STEP SIZE 1/16 then size clicks up (K1: 1/16, 1/8, 1/4), TIMES
     * X1 (K4), row 0, the notes ringing to the next (GLO switches it, its toast saying which) */
    k.recSetup = async (size) => {
        await t.clicks(K.K2, 3);
        await t.clicks(K.K1, -3);
        await t.clicks(K.K1, size);
        await t.clicks(K.K4, -7);
        await t.clicks(K.SELECT, -3);
        recRow = 0;
        await t.tap(B.GLO);
        const s = await t.waitFor((x) => /^NOTES (A STEP LONG|RING TO THE NEXT)$/.test(x.toast), 1000, "THE NOTES' LENGTH");
        if (s.toast !== "NOTES RING TO THE NEXT") {
            await t.tap(B.GLO);
            await t.waitFor((x) => x.toast === "NOTES RING TO THE NEXT", 1000, "NOTES RING TO THE NEXT");
        }
    };
    /* the selected row's note (MIDI): SEL held + its key at the keyboard's octave; with look, OCT+ then OCT- show it
     * (`NOTE C-4`), and the keyboard's octave (not in the state) is moved by OCT until the note is right */
    k.recNote = async (note, look) => {
        await t.combo(B.SEL, () => t.keyTap(note - 53));
        if (!look) return;
        await t.tap(B.OCTP);
        let s = await t.waitFor((x) => /^NOTE /.test(x.toast), 1000, "OCT+: THE ROW'S NOTE");
        await t.tap(B.OCTM);
        s = await t.waitFor((x) => /^NOTE /.test(x.toast) && x.toast !== s.toast, 1000, "OCT-: THE ROW'S NOTE");
        for (let i = 0, m = midiOf(s.toast.slice(5)); m !== note; i++, m = midiOf(s.toast.slice(5))) {
            t.check(i < 8 && m >= 0 && (m - note) % 12 === 0, `THE ROW'S NOTE ${s.toast.slice(5)}, NOT ${noteName(note)}`);
            const was = s.toast;
            await t.tap(m > note ? B.OCTM : B.OCTP);
            s = await t.waitFor((x) => /^NOTE /.test(x.toast) && x.toast !== was, 1000, "THE KEYBOARD'S OCTAVE");
        }
    };
    /* steps left on from before, which the probe wrote as loop 00: one at a time switched and written into loop 00 again,
     * its notes telling whether it was on (one fewer) or off (one more: switched back with the next), till REC is refused
     * (NO STEPS ON); then the loops cleared again */
    k.wipe = async () => {
        let on = (await k.select(0, "LOOP 00")).notes, back = null;
        t.note(`the pattern recorder had ${on} steps on from before: switched off one by one`);
        for (let r = 0, i = 0; ;) {
            if (!(await L()).recorder) await k.recOpen();
            if (back) { await k.recRow(back[0]); await t.keyTap(WHITE[back[1]]); on--; back = null; }
            if (on) {
                t.check(r < 4, `COULD NOT SWITCH THE PATTERN RECORDER'S STEPS OFF (${on} LEFT)`);
                await k.recRow(r);
                await t.keyTap(WHITE[i]);
            }
            const s = await k.recRec();
            if (noSteps(s)) break;
            const m = lps(s).notes;
            t.check(on && !lps(s).recorder && Math.abs(m - on) === 1, `SWITCHING THE PATTERN RECORDER'S STEPS OFF: ${m} ` +
                    `NOTES WRITTEN, ${on} STEPS ON BEFORE`);
            if (m > on) back = [r, i];
            on = m;
            if (++i === 16) { i = 0; r++; }
        }
        await t.tap(B.HOME);
        await t.waitFor((x) => !lps(x).recorder, 1000, "THE PATTERN RECORDER CLOSED");
        await k.clearLoops();
    };
    /* the first loop from the pattern recorder (+ NEW LOOP, no loops): opened, its steps found all off (REC refused, NO
     * STEPS ON; else k.wipe), set up (k.recSetup); rows [note (0: as it is), steps] a row each; then before() (the box
     * open, its preview playing), and REC writes it: the state then. After, its steps are switched off again, so the next
     * use finds none (REC refused, HOME closing the box) */
    k.recorder = async (rows, size, before) => {
        for (let go = 0; ; go++) {
            await k.recOpen();
            await k.recSetup(size);
            if (noSteps(await k.recRec())) break;
            t.check(!go, "THE PATTERN RECORDER'S STEPS ARE STILL ON");
            await k.wipe();
        }
        for (const [r, [note, on]] of rows.entries()) {
            await k.recRow(r);
            if (note) await k.recNote(note, r === 0);
            for (const i of on) await t.keyTap(WHITE[i]);
        }
        if (before) await before();
        await t.tap(B.REC);
        const s = await t.waitFor((x) => !lps(x).recorder, 2000, "THE PATTERN WRITTEN");
        await k.recOpen();
        for (const [r, [, on]] of rows.entries()) { await k.recRow(r); for (const i of on) await t.keyTap(WHITE[i]); }
        t.check(noSteps(await k.recRec()), "THE PATTERN RECORDER'S STEPS NOT ALL OFF AGAIN");
        await t.tap(B.HOME);
        await t.waitFor((x) => !lps(x).recorder, 1000, "THE PATTERN RECORDER CLOSED");
        return s;
    };
    return k;
}
const noteName = (m) => NOTE_NAMES[m % 12] + (Math.floor(m / 12) - 1);
const midiOf = (name) => { const m = /^(..)(-?\d)$/.exec(name), i = m ? NOTE_NAMES.indexOf(m[1]) : -1; return i < 0 ? -1 : 12 * (+m[2] + 1) + i; };

/* the TB-303 at 4, 8 and 12 voices. The keys play it as one voice (legato, as the one 303 it is), so these are loops on
 * LOOPS, a voice a lane: the loops cleared (QUANTIZE ROW, the next loops POLY), then POLY loops of 4 lanes, one for each
 * four voices, a chord of four tapped on every row of a bar, as a 303 line in sixteenths (on a lane a 303's gate closes
 * half a row after its note, so a voice sounds only while notes come); the first take starting the loops, the others
 * armed on + NEW LOOP while they play. Each count measured 5 s */
const TB_CHORD = [0, 4, 7, 11];         /* F3, A3, C4, E4 */
const TB_LINE = Array.from({ length: 16 }, (_, r) => [r, TB_CHORD, 0]);
async function tb303Voices(t, cart, slot) {
    const k = loopsKit(t);
    await k.clear();
    await k.sound(cart, slot);
    let s = await k.first(TB_LINE);
    await t.fx("DELAY", true);                          /* LOOPS' own effects (FX edits what plays'): DELAY and REVERB, */
    await t.fx("REVERB", true);                         /* as the keys had them */
    s = await t.screen("LOOPS");
    if (!(fxOn(s, "DELAY") && fxOn(s, "REVERB"))) {   /* (once on the FM-1, not again in the rerun: what was, and
                                                     * whether they come back within a second) */
        const how = (x) => `${["NOTHING", "A SONG", "A PATTERN", "LOOPS", "DJ"][x.what] || x.what} ` +
                           `${x.playing ? "PLAYING" : "STOPPED"}, SCREEN ${x.screen}, AT WORK: ` +
                           (Object.keys(FXNUM).filter((n) => fxOn(x, n)).join(" ") || "NONE");
        const was = how(s), t0 = t.now();
        let back = null;
        try { back = await t.waitFor((x) => fxOn(x, "DELAY") && fxOn(x, "REVERB"), 1000, "LOOPS' DELAY AND REVERB AT WORK"); }
        catch (e) { if (!(e instanceof Failed)) throw e; }
        if (!back) t.check(false, `LOOPS' DELAY AND REVERB NOT AT WORK (${was}; STILL NOT AFTER 1 S: ${how(await t.ask())})`);
        t.note(`LOOPS' DELAY AND REVERB NOT AT WORK AT FIRST (${was}); AT WORK ${((t.now() - t0) / 1000).toFixed(1)} S LATER`);
        s = back;
    }
    for (let n = 4; ; n += 4) {
        const l = lps(s);
        t.note(`X${n}: ${l.n} loop${l.n === 1 ? "" : "s"} of 4 lanes, ${l.all} notes, a bar of ${k.barRows} rows`);
        t.check(l.n === n / 4, `${n / 4} TB-303 LOOPS (THERE ARE ${l.n})`);
        await t.sleep(300);
        await t.measure(`TB-303 X${n}`, 5000);
        /* the most voices at once: the debug build's figures count them every half; without figures, looks at the
         * state, spaced unevenly (each voice sounds about half a row, and looks evenly spaced can lock to the rows:
         * on the FM-1 at 24 fps three frames are one row of 120 BPM, and every look fell in the rows' silent half) */
        const w = t.windows(`TB-303 X${n}`);
        let most = w.length ? Math.max(...w.map((f) => f.voicesMost)) : 0;
        for (let i = 0; i < 12 && most < n; i++) { s = await t.ask(); most = Math.max(most, s.voices); await t.sleep(20 + (i * 37) % 70); }
        t.check(most >= n, `TB-303 X${n}: ${n} VOICES SOUNDING (AT MOST ${most})`);
        if (n === 12) break;
        await k.select(-1, "+ NEW LOOP");
        await k.sound(cart, slot);
        await k.take(TB_LINE);
        s = await t.ask();
    }
    t.note(`${lps(s).n} loops, ${lps(s).all} notes (a 303's gate closes half a row in: each voice half a row)`);
}

/* a beat on LOOPS (specs/ui/screens/loops.md), built as a hand would, every step checked through LOOPS' cursor: the
 * loops cleared; the first loop from the pattern recorder (the kit, a drum a row, 16 steps of 1/16, TIMES X1: a bar, the
 * loops starting); a TB-303 bass line taken on + NEW LOOP (MONO, armed while the loops play); a chord of four on EL PIANO
 * (POLY, its 4 lanes); 20 s measured; a re-record (EDIT on the bass's loop: its notes gone at once, a new line taken); a
 * peel (LFO on + NEW LOOP: the last loop, the chord's, goes); CONTROL mode: white key 1 tapped (loop 00 muted at once),
 * white key 2 held (loop 01's mute waiting for the bar line, then muted), GLO's black key to SOLO, white key 1 (loop 00
 * soloed), PIT (all back on); 10 s measured, the loops playing on */
async function loopBeat(t) {
    const k = loopsKit(t), { L, select } = k;
    await k.clear();
    t.begin();
    await k.sound(...KIT);
    let s = await k.recorder(BEAT, 0), l = lps(s);
    t.check(s.toast === "PATTERN: LOOP 00, 1 BAR" && l.n === 1 && l.bars === 1 && l.all === BEAT_NOTES && s.playing &&
            s.what === 3, `THE PATTERN RECORDER'S KIT: A BAR OF ${BEAT_NOTES} NOTES, PLAYING (${l.n} LOOPS, ${l.bars} BARS, ` +
            `${l.all} NOTES, ${s.playing ? "" : "NOT "}PLAYING; "${s.toast}")`);
    await k.timing();
    await k.mode("MONO");                               /* a TB-303 bass line, MONO */
    await k.sound(...TB303);
    l = await k.take(BASS);
    t.check(l.n === 2 && l.added === BASS.length, `THE BASS: LOOP 01, ${BASS.length} NOTES (${l.added})`);
    await k.mode("POLY");                               /* a chord of four on EL PIANO, POLY */
    await k.sound(...EL_PIANO);
    l = await k.take(PAD);
    t.check(l.n === 3 && l.added === 4, `THE CHORD: LOOP 02, 4 NOTES ON ITS 4 LANES (${l.added})`);
    t.note(`a beat of ${l.n} loops, ${l.all} notes, a bar of ${k.barRows} rows`);
    await t.measure("LOOPS: A BEAT", 20000);
    l = await k.take(BASS_AGAIN, 1);                    /* a re-record of the bass (EDIT) */
    t.check(l.n === 3 && l.notes === BASS_AGAIN.length, `THE BASS RE-RECORDED: ${BASS_AGAIN.length} NOTES (${l.notes})`);
    await select(-1, "+ NEW LOOP");                     /* a peel: LFO on + NEW LOOP, the last loop goes */
    await t.tap(B.LFO);
    s = await t.waitFor((x) => lps(x).n === 2, 1000, "LOOP 02 PEELED");
    t.check(s.toast === "LOOP 02 PEELED" && lps(s).all === BEAT_NOTES + BASS_AGAIN.length,
            `THE PEEL: THE CHORD'S LOOP GONE (${lps(s).all} NOTES LEFT; "${s.toast}")`);
    /* CONTROL mode: mutes at once and at the bar line, a solo, all back on */
    await k.keys(true);
    await k.muteMode();
    await select(0, "LOOP 00");
    await t.keyTap(WHITE[0]);
    s = await t.waitFor((x) => lps(x).muted, 1000, "LOOP 00 MUTED AT ONCE");
    t.check(s.toast === "LOOP 00 MUTED", `WHITE KEY 1: "${s.toast}"`);
    await select(1, "LOOP 01");
    const t0 = await k.nextPass();                      /* (a bar line just gone: the next a bar away) */
    await t.keyHold(WHITE[1], s.holdDelay + 300);
    s = await t.ask();
    t.check(/^LOOP 01 MUTED AT THE B/.test(s.toast), `WHITE KEY 2 HELD: "${s.toast}"`);   /* (cut at 23 characters) */
    if (t.now() < t0 + k.barRows * k.rowMs - 50) t.check(!lps(s).muted, "WHITE KEY 2 HELD: LOOP 01 MUTED BEFORE THE BAR LINE");
    else t.note("white key 2's hold reached the bar line: its wait not seen");
    s = await t.waitFor((x) => lps(x).muted, k.barRows * k.rowMs + 1500, "LOOP 01 MUTED AT THE BAR LINE");
    await k.black(CTL.GLO, "SEQ: SOLO");
    await select(0, "LOOP 00");
    await t.keyTap(WHITE[0]);
    s = await t.waitFor((x) => lps(x).soloed, 1000, "LOOP 00 SOLOED");
    t.check(s.toast === "LOOP 00 SOLO", `SOLO, WHITE KEY 1: "${s.toast}"`);
    await k.black(CTL.PIT, "ALL LOOPS ON");
    l = await L();
    const l1 = await select(1, "LOOP 01");
    t.check(!l.muted && !l.soloed && !l1.muted, "PIT: ALL BACK ON");
    await k.black(CTL.GLO, "SEQ: MUTE");                /* (as found) */
    await k.keys(false);
    await t.measure("LOOPS: AFTER THE EDITS", 10000);
    s = await t.ask();
    t.check(s.playing && s.what === 3, "THE LOOPS PLAY ON");
    t.note(`at the end ${lps(s).n} loops, ${lps(s).all} notes, ${s.voices} voices sounding`);
}

/* LOOPS TO A PATTERN AND BACK's and RECORD LIVE's three loops, 2 bars (so a pass leaves room for RECORD LIVE's steps):
 * the pattern recorder's drums (the beat in eighths, TIMES X1), a MONO bass (the TB-303) and a POLY pad (EL PIANO's
 * chord); playing */
async function threeLoops(t, k) {
    await k.clear();
    await k.sound(...KIT);
    const s = await k.recorder(BEAT, 1);                /* (STEP SIZE 1/8) */
    t.check(/^PATTERN: LOOP 00, 2 BAR/.test(s.toast) && lps(s).bars === 2 && s.playing, `THE DRUMS: 2 BARS, PLAYING ` +
            `(${lps(s).bars} BARS; "${s.toast}")`);
    await k.timing();
    await k.mode("MONO");
    await k.sound(...TB303);
    let l = await k.take(BASS);
    t.check(l.added === BASS.length, `THE BASS: ${BASS.length} NOTES (${l.added})`);
    await k.mode("POLY");
    await k.sound(...EL_PIANO);
    l = await k.take(PAD);
    t.check(l.n === 3 && l.added === 4, `THE PAD: 4 NOTES (${l.n} LOOPS, ${l.added} NOTES)`);
    return l;
}

/* the first free pattern number, the one SAVE TO PATTERN's NEW takes. The state's toast is cut at 23 characters, so
 * `LOOPS STORED IN PATTERN xx` comes without its number: the EDITOR's STORE IN A PATTERN, + NEW PATTERN, says it
 * (`STORED IN PATTERN xx`), and REVERT drops that copy */
async function firstFreePattern(t) {
    await t.screen("EDITOR");
    let s = await editorOwn(t, "STORE IN A PATTERN");
    t.check(s.overlay === 8, "STORE IN A PATTERN: NO PATTERN LIST");
    await t.tap(B.SEL);                                 /* (+ NEW PATTERN, highlighted as it opens) */
    s = await t.waitFor((x) => !x.overlay && /^STORED IN PATTERN [0-9A-F]{2}$/.test(x.toast), 2000, "STORED IN PATTERN xx");
    await t.revert();
    return parseInt(s.toast.slice(18), 16);
}

/* (secs: the times of `autotest_test.js WASM FLASH 1`, the browser's firmware at full length; THE CHIP's and AUTO
 * LEVEL ALL's: the FM-1's, ~5 s and ~6 s) */
const SCENARIOS = [
    { group: "SONGS", name: "IDLE", secs: 20, tsecs: 10, what: "nothing plays (WORLD LOOP played a moment, paused, all sound off), 10 s",
      run: async (t) => {
        await keysStart(t);
        await t.screen("SONGS");
        await t.sleep(2000 * t.scale);                  /* (its DELAY and REVERB at rest) */
        await t.measure("IDLE", 10000);
    } },
    /* IDLE with nothing drawn: what the screen's drawing between the halves costs the sound (the code it runs pushing
     * the sound's out of the code cache, fetched again at the next half) */
    { group: "SONGS", name: "IDLE, THE SCREEN STILL", debug: true, secs: 22, tsecs: 10,
      what: "IDLE again with the screen frozen (nothing drawn), 10 s", run: async (t) => {
        if (!t.canFreeze) t.skip("THIS PAGE CANNOT FREEZE THE SCREEN");
        await keysStart(t);
        await t.screen("SONGS");
        await t.sleep(2000 * t.scale);
        t.freeze(true);
        try {
            await t.sleep(1200);                        /* (the freeze goes with the next FIGURES' question, once a second) */
            await t.measure("IDLE, THE SCREEN STILL", 10000);
            const w = t.windows("IDLE, THE SCREEN STILL");
            t.check(w.every((f) => f.frozen), `THE SCREEN NOT FROZEN IN ${w.filter((f) => !f.frozen).length} OF ITS ` +
                    `${w.length} WINDOWS`);
        } finally { t.freeze(false); }
    } },
    { group: "SONGS", name: "IDLE AFTER A SONG", secs: 30, tsecs: 10, what: "PUNCH IN 10 s, paused, 5 s on, then 10 s: its DELAY and REVERB at rest",
      run: async (t) => {
        await t.playSong("PUNCH IN", 2);
        await t.sleep(10000 * t.scale);
        await t.tap(B.PLAY);
        await t.waitFor((s) => !s.playing, 2000, "PUNCH IN PAUSED");
        await t.sleep(5000 * t.scale);                  /* (the effects rest 1-4 s after the last sound) */
        await t.measure("IDLE AFTER A SONG", 10000);
    } },
    ...DEMO.map((name) => ({ group: "SONGS", name, secs: 24, tsecs: 20, what: `from position ${HEAVY[name]}, 20 s`, run: async (t) => {
        await t.playSong(name, HEAVY[name]);
        await t.measure(name, 20000);                   /* (a pattern change: 8 bars at 96-150 BPM take 13-20 s) */
    } })),
    ...LOOPS.map((name) => ({ group: "SONGS", name, secs: 23, tsecs: 20, what: "from its last position (a loop's heaviest, usually), 20 s",
      run: async (t) => {
        await t.playSong(name, LAST);
        t.note(`from position ${hex2(t.pos)}`);
        await t.measure(name, 20000);
    } })),
    ...ENGINES.map(([name, cart, slot, keysN]) => ({ group: "KEYS", name, secs: keysN.length > 1 ? 45 : 47, tsecs: keysN.length > 1 ? 32 : 39,
      what: name === "TB-303" ? "1 key held 5 s (the keys play it as one voice), then 4, 8 and 12 as POLY loops of 4 lanes on " +
      "LOOPS, 5 s each" :
      `${keysN.join(", ")} keys held (past 10 on core 2), 5 s each`, run: async (t) => {
        await voicesSetting(t, 16);                     /* (16: 10 on core 1, 6 on core 2) */
        await keysHeld(t, name, cart, slot, keysN);
        if (name === "TB-303") await tb303Voices(t, cart, slot);
    } })),
    { group: "EFFECTS", name: "A SONG WITH EFFECTS", secs: 37, tsecs: 30, what: "MIDNIGHT RUN, CHORUS DELAY REVERB on, knobs swept, 30 s", run: async (t) => {
        await t.playSong("MIDNIGHT RUN", 2);
        for (const e of ["CHORUS", "DELAY", "REVERB"]) await t.fx(e, true);
        const s = await t.ask();
        t.check(fxSwitched(s, "CHORUS") && fxSwitched(s, "DELAY") && fxSwitched(s, "REVERB") && fxOn(s, "REVERB"),
                "CHORUS, DELAY AND REVERB ON, THE REVERB AT WORK");
        await t.keyTap(FXKEY.REVERB);                   /* (K1-K4 the REVERB's while the measure runs) */
        const sweep = t.background(async () => {
            for (let i = 0; i < 3; i++) { await t.turn(K.K1, 8); await t.turn(K.K2, -8); await t.turn(K.K1, -8); await t.turn(K.K2, 8); }
        });
        await t.measure("CHORUS DELAY REVERB, SWEPT", 30000);
        await sweep;
    } },
    { group: "EFFECTS", name: "THE PUNCH-INS", secs: 37, tsecs: 32, what: "each white key held 2 s over MIDNIGHT RUN", run: async (t) => {
        await t.playSong("MIDNIGHT RUN", 2);
        await t.screen("FX");
        const held = t.background(async () => { for (const k of WHITE) await t.keyHold(k, 2000 * Math.max(0.1, t.scale)); });
        await t.measure("PUNCH-INS", 32000);
        await held;
    } },
    { group: "EFFECTS", name: "METRONOME AND COUNT-IN", secs: 25, tsecs: 20, what: "COUNT-IN 1 BAR, the METRONOME on, 20 s", run: async (t) => {
        await t.setting(0, SET.COUNT_IN, (s) => s.countIn, 1, "COUNT-IN 1 BAR");
        await t.playSong("CITY LIGHTS", 0);
        await t.fx("METRONOME", true);
        await t.screen("SONGS");
        await t.tap(B.PLAY);
        await t.waitFor((x) => !x.playing, 2000, "PAUSED");
        await t.tap(B.PLAY);
        await t.measure("METRONOME, COUNT-IN", 20000);
    } },
    { group: "DJ", name: "THE STAND", secs: 49, tsecs: 30, what: "TECHNO LOOP + ELECTRO LOOP, filters and LFOs, 30 s", run: async (t) => {
        await djStand(t);
        await t.measure("DJ STAND", 30000);             /* (steady: its LFOs a second a turn, the loops round twice) */
    } },
    { group: "DJ", name: "DJ BUSY", secs: 64, tsecs: 46, what: "each action measured on its own: a LOAD at the bar, SWAP, the stops, the crossfader, " +
      "MOVE, JUMP, 46 s", run: async (t) => {
        await djStand(t);
        const acid = t.songNames.indexOf("ACID LOOP");
        if (acid >= 0) await t.seek(K.SELECT, (x) => x.cursor[0], acid, "ACID LOOP IN THE LIST", 300);
        /* each action done while a stretch of its own is measured (it asks no state: the measure's own looks only),
         * so its late halves and its stages are its own */
        const sc = t.scale, step = async (name, ms, act) => { const b = t.background(act); await t.measure("DJ BUSY: " + name, ms); await b; };
        if (acid >= 0) await step("A LOAD AT THE BAR", 8000, () => t.keyTap(BLACK[3]));   /* (on the left) */
        await step("SWAP", 4000, () => t.tap(B.SEQ));
        for (const [pad, name] of [[4, "BRAKE"], [5, "BACKSPIN"], [6, "ECHO OUT"], [7, "REVERB OUT"]])   /* (the left deck) */
            await step(name, 5000, () => t.keyHold(WHITE[pad], Math.max(300, 2000 * sc)));
        await step("THE CROSSFADER", 4000, async () => { await t.turn(K.ALG, 26); await t.turn(K.ALG, -26); });
        await step("MOVE", 4000, async () => {          /* (held on the left deck, K1 two bars on, let go) */
            await t.keyDown(WHITE[3]);
            await t.turn(K.K1, 2);
            await t.keyUp(WHITE[3]);
        });
        await step("JUMP", 6000, () => t.keyTap(WHITE[2]));
    } },
    { group: "DJ", name: "DJ EFFECTS AND PUNCH-INS", secs: 50, tsecs: 32, what: "the stand with DJ's CHORUS on too, each white key's punch-in " +
      "held 2 s over it", run: async (t) => {
        await djStand(t);
        await t.fx("CHORUS", true);                     /* (FX from DJ: DJ's setup) */
        const s = await t.ask();
        t.check(fxSwitched(s, "CHORUS") && fxSwitched(s, "DELAY") && fxSwitched(s, "REVERB"), "DJ'S CHORUS, DELAY AND REVERB ON");
        const held = t.background(async () => { for (const k of WHITE) await t.keyHold(k, 2000 * Math.max(0.1, t.scale)); });
        await t.measure("DJ: EFFECTS, PUNCH-INS", 32000);
        await held;
    } },
    { group: "DJ", name: "DJ TWO TEMPOS", secs: 46, tsecs: 39, what: "TRANCE LOOP and DISCO LOOP, SYNC off, each deck's tempo turned on its own " +
      "(never in step), 30 s", run: async (t) => {
        await djTwo(t, "TRANCE LOOP", "DISCO LOOP");
        const sc = t.scale, turns = t.background(async () => {   /* K1 the left's TEMPO, K4 the right's */
            for (let i = 0; i < 3; i++) {
                await t.turn(K.K1, 7); await t.sleep(2500 * sc); await t.turn(K.K4, -5); await t.sleep(2500 * sc);
                await t.turn(K.K1, -9); await t.sleep(2500 * sc); await t.turn(K.K4, 6); await t.sleep(2500 * sc);
            }
        });
        await t.measure("DJ TWO TEMPOS", 30000);
        await turns;
        const s = await t.ask();
        t.check(s.dj.decks[0].playing && s.dj.decks[1].playing && s.dj.decks[0].bpm10 !== s.dj.decks[1].bpm10,
                "BOTH DECKS PLAYING, EACH AT ITS OWN TEMPO");
        t.note(`the tempos at the end: ${s.dj.decks[0].bpm10 / 10} and ${s.dj.decks[1].bpm10 / 10} BPM`);
    } },
    { group: "DJ", name: "DJ HEAVY", secs: 42, tsecs: 30, what: "PUNCH IN and MIDNIGHT RUN mixed from their second patterns, SYNC on, " +
      "the crossfader in the middle, no filters or LFOs, DJ's DELAY and REVERB on, VOICES 16, 30 s", run: async (t) => {
        await voicesSetting(t, 16);                     /* (the notes past 10 on core 2) */
        await djHeavy(t, "PUNCH IN", "MIDNIGHT RUN");
        await t.measure("DJ HEAVY", 30000);
        const s = await t.ask();
        t.check(s.dj.decks[0].playing && s.dj.decks[1].playing, "BOTH DECKS STILL PLAYING");
    } },
    { group: "LOOPS", name: "THE LOOP STATION", secs: 85, tsecs: 20, what: "12 loops recorded from the keys, a take each (JUNO-106 " +
      "sounds, then DX7's), all playing, 20 s", run: async (t) => {
        await voicesSetting(t, 12);
        const l = await loopStation(t, 12);
        t.note(`${l.n} loops of ${l.bars} bar${l.bars === 1 ? "" : "s"}, ${l.all} notes`);
        await t.measure("12 LOOPS", 20000);
        const s = await t.ask();
        t.check(s.playing && s.what === 3, "THE LOOPS PLAY ON");
        t.note(`${s.voices} voices sounding at the end`);
    } },
    { group: "LOOPS", name: "A BEAT", secs: 68, tsecs: 61, what: "a beat as a hand builds one: the pattern recorder's kit, a MONO " +
      "bass, a POLY chord; a re-record, a peel, mutes and a solo in CONTROL mode; 20 s and 10 s measured", run: async (t) => {
        await voicesSetting(t, 12);
        await loopBeat(t);
    } },
    /* the same with core 2 off (38 SECOND CORE), to compare with the scenarios above on two cores */
    { group: "ONE CORE", name: "IDLE, ONE CORE", debug: true, secs: 20, tsecs: 10, what: "IDLE with core 2 off, 10 s",
      run: (t) => oneCore(t, async () => {
        await keysStart(t);
        await t.screen("SONGS");
        await t.sleep(2000 * t.scale);
        await t.measure("IDLE, ONE CORE", 10000);
    }) },
    { group: "ONE CORE", name: "DJ HEAVY, ONE CORE", debug: true, secs: 42, tsecs: 30, what: "DJ HEAVY with core 2 off, 30 s",
      run: (t) => oneCore(t, async () => {
        await djHeavy(t, "PUNCH IN", "MIDNIGHT RUN");
        await t.measure("DJ HEAVY, ONE CORE", 30000);
        const s = await t.ask();
        t.check(s.dj.decks[0].playing && s.dj.decks[1].playing, "BOTH DECKS STILL PLAYING");
    }) },
    { group: "ONE CORE", name: "KEYS AT 12, ONE CORE", debug: true, secs: 46, tsecs: 36,
      what: "12 keys held on the DX7, a Juno-106 resonant and one plain, core 2 off, 5 s each", run: (t) => oneCore(t, async () => {
        await voicesSetting(t, 12);
        for (const [name, cart, slot] of ENGINES.slice(0, 3)) await keysHeld(t, name, cart, slot, [12]);
    }) },
    { group: "CHIP", name: "THE CHIP", debug: true, secs: 6, tsecs: 1, what: "the firmware's own: THE CORE and ALONE", run: async (t) => {
        await t.playSong("PUNCH IN", 0).catch(() => {}); /* (a song started ends DJ, which the bench refuses) */
        await t.tap(B.PLAY);
        await t.allSoundOff();                          /* (its tails cut: the bench starts from silence) */
        const B2 = await t.bench();
        await t.sleep(0);                               /* (STOP ends the bench: stopped, not failed) */
        t.check(B2.end && B2.end.status === 0, `THE BENCH ENDED ${B2.end ? B2.end.name : "WITHOUT AN END"}`);
        t.chip(B2);
    } },
    { group: "TOOLS", name: "AUTO LEVEL ALL", secs: 6, tsecs: 2, what: "SETTINGS' tool over every instrument", run: async (t) => {
        await t.screen("SETTINGS");
        await t.seek(K.ALG, (s) => s.cursor[0], 0, "SETTINGS' LIST");
        await t.seek(K.SELECT, (s) => s.cursor[1], SET.TOOLS, "THE LINE TOOLS");
        await t.seek(K.K1, (s) => s.cursor[2], TOOL.AUTO_LEVEL_ALL, "THE TOOL AUTO LEVEL ALL", 10);
        await t.tap(B.EDIT);
        await t.yes(/^AUTO LEVEL \d+ SOUNDS?\?$/, "AUTO LEVEL ALL'S QUESTION");    /* (then its progress bar: ~2 s on the FM-1) */
        /* measured while it runs (the main loop's stack), and ended by its end: the state answered again (the bar holds
         * the main loop: no answer meanwhile) with the bar gone; the first look comes after the bar is up */
        const s = await t.measureUntil("AUTO LEVEL ALL", (x) => !x.overlay, 600000, "AUTO LEVEL'S END");
        t.check(s.unsaved > 0 || /LEVEL/.test(s.toast), "AUTO LEVEL ALL DID NOT RUN");
        t.note(`it left ${s.unsaved} unsaved change${s.unsaved === 1 ? "" : "s"}${s.toast ? `; the FM-1 said ${s.toast}` : ""}`);
    } },
    /* the two tools that write nothing: measured till their progress bar goes; what they found checked (a missing
     * reference only noted: a user's own songs may point at things deleted) */
    ...[["CHECK REFERENCES", TOOL.CHECK_REFERENCES, "NO MISSING REFERENCES"], ["CHECK STORAGE", TOOL.CHECK_STORAGE, "STORAGE OK"]].map(
        ([name, tool, good]) => ({ group: "TOOLS", name, secs: 5, tsecs: 1, what: "SETTINGS' tool (it writes nothing)", run: async (t) => {
            await t.screen("SETTINGS");
            await t.seek(K.ALG, (s) => s.cursor[0], 0, "SETTINGS' LIST");
            await t.seek(K.SELECT, (s) => s.cursor[1], SET.TOOLS, "THE LINE TOOLS");
            await t.seek(K.K1, (s) => s.cursor[2], tool, `THE TOOL ${name}`, 10);
            await t.tap(B.EDIT);
            const s = await t.measureUntil(name, (x) => x.overlay !== 5, 60000, `${name}'S END`);
            const list = s.overlay === 8;
            if (list) await t.tap(B.HOME);
            if (list && tool === TOOL.CHECK_REFERENCES) t.note("some references are missing (the list closed)");
            else t.check(!list && s.toast === good, list ? "CHECK STORAGE FOUND PROBLEMS" : `THE FM-1 SAID ${s.toast || "NOTHING"}, NOT ${good}`);
        } })),
    { group: "TOOLS", name: "RECORD PATTERN", secs: 15, tsecs: 6, what: "in LOOPS with no loops, the pattern recorder: four steps, " +
      "its preview measured, written as the first loop", run: async (t) => {
        const k = loopsKit(t);
        await k.clear();
        const s = await k.recorder([[0, [0, 3, 6, 10]]], 0, async () => {   /* (its preview plays from the moment it opens) */
            await t.measure("RECORD PATTERN: ITS PREVIEW", 5000);
            await t.waitFor((x) => lps(x).recorder && x.voices > 0, 2000, "ITS PREVIEW SOUNDING");
        });
        const l = lps(s);
        t.check(s.toast === "PATTERN: LOOP 00, 1 BAR" && l.n === 1 && l.all === 4 && s.unsaved > 0,
                `REC: THE FIRST LOOP, A BAR OF 4 NOTES (${l.n} LOOPS, ${l.bars} BARS, ${l.all} NOTES; "${s.toast}")`);
    } },
    /* the UI (specs/debug.md: The scenarios, UI): edits only, dropped by REVERT after */
    { group: "UI", name: "THE EDITOR'S OWN PATTERN", secs: 45, tsecs: 40, what: "NEW PATTERN, notes by REC and the keys, its loop " +
      "4 s, the warning (NO, then YES), LOAD A PATTERN (PUNCH IN's first), STORE IN A PATTERN (+ NEW PATTERN)", run: async (t) => {
        const n = t.songNames.indexOf("PUNCH IN");
        if (n < 0) t.skip("PUNCH IN IS NOT ON THE FM-1");
        await t.screen("EDITOR");
        t.begin();
        let s = await editorOwn(t, "NEW PATTERN");
        s = await t.waitFor((x) => ed(x).pat === OWN_PAT && !ed(x).holds, 2000, "THE EDITOR ON ITS OWN PATTERN, EMPTY");
        await t.tap(B.REC);
        for (const i of [0, 2, 4, 7]) await t.keyTap(WHITE[i]);
        await t.tap(B.REC);
        s = await t.waitFor((x) => ed(x).holds, 2000, "NOTES IN THE EDITOR'S OWN PATTERN");
        await t.tap(B.PLAY);
        await t.waitFor((x) => x.playing && x.what === 2, 2000, "ITS LOOP PLAYING");
        await t.measure("ITS LOOP", 4000);
        await t.tap(B.PLAY);
        await t.waitFor((x) => !x.playing, 2000, "ITS LOOP PAUSED");
        /* another pattern from ENV's list: the warning, NO, then YES */
        const envPick = async () => {
            await t.tap(B.ENV);
            await t.waitFor((x) => x.overlay === 8, 2000, "ENV'S PATTERN LIST");
            await t.tap(B.ENV);
            return t.waitFor((x) => x.overlay === 2, 2000, "THE WARNING");
        };
        s = await envPick();
        t.check(WARNING.test(s.words), `EXPECTED THE WARNING, FOUND "${s.words}"`);
        await t.tap(B.OCTM);
        s = await t.waitFor((x) => !x.overlay, 2000, "THE WARNING CLOSED");
        t.check(ed(s).pat === OWN_PAT && ed(s).holds, "NO: STILL ON ITS OWN PATTERN, ITS NOTES KEPT");
        await envPick();
        await t.tap(B.OCTP);
        s = await t.waitFor((x) => !x.overlay && ed(x).pat !== OWN_PAT, 2000, "THE PATTERN PICKED");
        t.check(!ed(s).holds, "YES: ITS OWN PATTERN EMPTIED");
        /* PUNCH IN's first pattern the EDITOR's (SONGS' ENV), then LOAD A PATTERN copies it into its own */
        await t.screen("SONGS");
        await t.seek(K.SELECT, (x) => x.cursor[0], n, "THE SONG PUNCH IN", 300);
        await t.tap(B.ENV);
        await t.waitFor((x) => x.overlay === 8, 2000, "SONGS' PATTERN LIST");
        await t.tap(B.ENV);
        s = await t.waitFor((x) => !x.overlay && /^PATTERN [0-9A-F]{2} FOR THE/.test(x.toast), 2000,   /* (cut at 24) */
                            "PATTERN xx FOR THE EDITOR");
        const p = s.toast.slice(8, 10);
        s = await t.screen("EDITOR");
        t.check(hex2(ed(s).pat) === p, `THE EDITOR NOT ON PUNCH IN'S FIRST PATTERN ${p} (ON ${hex2(ed(s).pat)})`);
        s = await editorOwn(t, "LOAD A PATTERN");
        t.check(s.overlay === 8, "LOAD A PATTERN: NO PATTERN LIST");
        await t.tap(B.SEL);                             /* (the list opens on the EDITOR's pattern) */
        s = await t.waitFor((x) => !x.overlay && ed(x).pat === OWN_PAT, 2000, "THE EDITOR ON ITS OWN PATTERN");
        t.check(ed(s).holds && s.toast === `PATTERN ${p} LOADED`, `LOAD A PATTERN: EXPECTED PATTERN ${p} LOADED, FOUND "${s.toast}"`);
        /* STORE IN A PATTERN, + NEW PATTERN (highlighted when it opens) */
        s = await editorOwn(t, "STORE IN A PATTERN");
        t.check(s.overlay === 8, "STORE IN A PATTERN: NO PATTERN LIST");
        await t.tap(B.SEL);
        s = await t.waitFor((x) => !x.overlay && ed(x).pat !== OWN_PAT, 2000, "THE EDITOR ON THE PATTERN STORED");
        t.check(!ed(s).holds && s.toast === `STORED IN PATTERN ${hex2(ed(s).pat)}`,
                `STORE IN A PATTERN: EXPECTED STORED IN PATTERN ${hex2(ed(s).pat)}, ITS OWN EMPTY; FOUND "${s.toast}"`);
        t.note(`PUNCH IN's first pattern ${p} loaded, stored in pattern ${hex2(ed(s).pat)}`);
        await t.revert();
    } },
    { group: "UI", name: "NEW SONG", secs: 14, tsecs: 10, what: "SONGS' NEW, + NEW PATTERN: a new song made; REVERT drops it",
      run: async (t) => {
        let s = await t.screen("SONGS");
        t.begin();
        await t.menuPick(B.SEL, "NEW");
        await t.waitFor((x) => x.overlay === 8, 2000, "THE LIST NEW SONG STARTS WITH");
        await t.turn(K.SELECT, -80);                    /* (+ NEW PATTERN, on top) */
        await t.tap(B.SEL);
        s = await t.waitFor((x) => !x.overlay && x.songName === "NEW SONG" && x.unsaved > 0, 2000, "A NEW SONG, UNSAVED");
        await t.revert();
        s = await t.ask();
        t.check(!s.unsaved && s.songName !== "NEW SONG", `REVERT DID NOT DROP THE NEW SONG (${s.unsaved} UNSAVED, "${s.songName}")`);
    } },
    { group: "UI", name: "THE KEYS ON LOOPS", secs: 14, tsecs: 4, what: "the loops cleared, LOOPS' DELAY on, nothing playing: " +
      "a key held on LOOPS sounds through LOOPS' effects at LOOPS' tempo, on SONGS not", run: async (t) => {
        const held = async () => { await t.keyDown(WHITE[4]); await t.sleep(400); const s = await t.ask(); await t.keyUp(WHITE[4]); return s; };
        await keysStart(t);                             /* (a song what played last, not the loops a scenario before
                                                         * played: on SONGS the keys sound through its setup) */
        await t.screen("SONGS");
        await t.fx("DELAY", false);                     /* (FX from SONGS: the held setup's DELAY off, an edit: the song
                                                         * played last's) */
        await t.screen("SONGS");
        const song = await held();
        t.check(!fxOn(song, "DELAY"), "A KEY HELD ON SONGS: THE SONG'S DELAY STILL AT WORK");
        await loopsKit(t).clear();
        t.begin();
        await t.fx("DELAY", true);                      /* (FX from LOOPS: LOOPS' setup) */
        let s = await t.screen("LOOPS");
        t.check(!s.playing, "SOMETHING PLAYS ON LOOPS");
        s = await held();
        t.check(fxOn(s, "DELAY"), "A KEY HELD ON LOOPS: LOOPS' DELAY NOT AT WORK");
        const b0 = s.bpm10;
        await t.tap(B.GLO);                             /* LOOPS PROPERTIES: TEMPO first, +1 */
        await t.waitFor((x) => x.overlay === 3, 1000, "LOOPS PROPERTIES");
        await t.turn(K.K1, 1);
        await t.tap(B.GLO);
        await t.waitFor((x) => !x.overlay, 1000, "LOOPS PROPERTIES CLOSED");
        s = await held();
        t.check(s.bpm10 > b0, `THE CLOCK NOT LOOPS' TEMPO (${b0 / 10} BPM, AFTER TEMPO +1 ${s.bpm10 / 10})`);
        await t.screen("SONGS");
        s = await held();
        t.check(!fxOn(s, "DELAY"), "A KEY HELD ON SONGS: LOOPS' DELAY AT WORK");
        t.note(`LOOPS' tempo ${b0 / 10} BPM, the song's ${song.bpm10 / 10}`);
    } },
    { group: "UI", name: "LOOPS TO A PATTERN AND BACK", secs: 60, tsecs: 53, what: "three loops (the pattern recorder's drums, a MONO " +
      "bass, a POLY pad) saved to a NEW pattern, cleared, loaded from it again: the same; REVERT drops it all", run: async (t) => {
        const F = await firstFreePattern(t);            /* (the number NEW takes) */
        const k = loopsKit(t);
        t.begin();
        let l = await threeLoops(t, k);
        await t.menuPick(B.SEL, "SAVE TO PATTERN");
        await t.waitFor((x) => x.overlay === 8, 2000, "THE PATTERN LIST, SAVE THE LOOPS TO");
        await t.tap(B.SEL);                             /* (NEW, picked as it opens) */
        let s = await t.waitFor((x) => !x.overlay && /^LOOPS STORED IN PATTERN/.test(x.toast), 2000, "LOOPS STORED IN PATTERN xx");
        t.check(lps(s).n === 3 && s.playing, "SAVE TO PATTERN: THE LOOPS DO NOT GO ON AS THEY WERE");
        s = await k.clearLoops();
        t.check(s.toast === "LOOPS CLEARED", `CLEAR LOOPS: "${s.toast}"`);
        await t.menuPick(B.SEL, "LOAD FROM PATTERN");
        await t.waitFor((x) => x.overlay === 8, 2000, "THE PATTERN LIST, LOAD INTO LOOPS");
        await t.turn(K.SELECT, -80);                    /* (its top; the patterns below the first free all there: the
                                                         * one stored is the list's line F) */
        await t.clicks(K.SELECT, F);
        await t.tap(B.SEL);
        s = await t.waitFor((x) => !x.overlay && /^PATTERN [0-9A-F]{2} IN LOOPS: /.test(x.toast), 3000, "PATTERN xx IN LOOPS");
        const m = lps(s);
        t.check(s.toast === `PATTERN ${hex2(F)} IN LOOPS: 3 `, `LOAD FROM PATTERN ${hex2(F)}: "${s.toast}"`);   /* (3 LOOPS, cut) */
        t.check(m.n === 3 && m.bars === l.bars && m.all === l.all, `LOADED: ${m.n} LOOPS OF ${m.bars} BARS, ${m.all} NOTES ` +
                `(BEFORE: ${l.n} OF ${l.bars}, ${l.all})`);
        t.note(`pattern ${hex2(F)}: ${m.n} loops of ${m.bars} bars, ${m.all} notes, as stored`);
        await t.revert();
    } },
    { group: "UI", name: "RECORD LIVE", secs: 50, tsecs: 49, what: "three loops playing, RECORD LIVE's panel, REC: it starts at once; " +
      "EDIT refused; a mute, a punch-in, PLAY silencing the loops and PLAY again (the song going on), REC: a song of 1 " +
      "pattern at LOOPS' tempo, named LOOPS ##; REVERT drops it", run: async (t) => {
        const k = loopsKit(t);
        t.begin();
        await threeLoops(t, k);
        let s = await t.ask();
        const bpm10 = s.bpm10;
        await t.menuPick(B.SEL, "RECORD LIVE");
        s = await t.waitFor((x) => x.overlay, 2000, "RECORD LIVE'S PANEL");
        await t.tap(B.REC);                             /* (ARM: the panel closes) */
        s = await t.waitFor((x) => !x.overlay && lps(x).songArmed, 2000, "RECORD LIVE ARMED");
        await t.tap(B.REC);                             /* (the loops playing: it starts at once) */
        s = await t.waitFor((x) => lps(x).song, 2000, "THE SONG RECORDING");
        await t.tap(B.EDIT);                            /* no change of the loops while it records */
        s = await t.waitFor((x) => x.toast === "RECORDING A SONG", 1000, "EDIT REFUSED: RECORDING A SONG");
        t.check(!lps(s).armed && lps(s).song, "EDIT DURING THE SONG: A TAKE ARMED, OR THE SONG ENDED");
        await k.keys(true);
        await k.muteMode();
        await t.keyTap(WHITE[0]);
        await t.waitFor((x) => x.toast === "LOOP 00 MUTED", 1000, "LOOP 00 MUTED");
        await t.combo(B.FX, () => t.keyHold(WHITE[4], 400));
        await k.keys(false);
        await t.tap(B.PLAY);                            /* the loops silent, the song going on */
        await t.sleep(4 * k.rowMs + 300);
        s = await t.ask();
        t.check(lps(s).song && s.playing, "PLAY: THE SONG ENDED, OR ITS TIME STOPPED");
        await t.tap(B.PLAY);                            /* the loops on again */
        await t.sleep(4 * k.rowMs + 300);
        await t.tap(B.REC);                             /* it ends at once */
        s = await t.waitFor((x) => !lps(x).song && /^SONG [0-9A-F]{2} /.test(x.toast), 2000, "THE SONG'S END");
        t.check(/^SONG [0-9A-F]{2} RECORDED: \d+:\d\d/.test(s.toast), `THE SONG'S END: "${s.toast}"`);   /* (cut at 23 characters) */
        const n = parseInt(s.toast.slice(5, 7), 16);
        /* the new song on SONGS: its one position, LOOPS' tempo, the panel's name */
        await t.screen("SONGS");
        s = await t.seek(K.SELECT, (x) => x.cursor[0], n, `THE SONG ${hex2(n)}`, 300);
        t.check(/^LOOPS \d\d$/.test(s.songName), `SONG ${hex2(n)}: "${s.songName}"`);
        for (let i = 0, was = -1; i < 8 && s.cursor[1] !== was; i++) { was = s.cursor[1]; s = await t.clicks(K.ALG, 1); }
        t.check(s.cursor[1] === 0, `SONG ${hex2(n)}: ITS LAST POSITION ${hex2(s.cursor[1])}, NOT 00`);
        await t.allSoundOff();
        await t.tap(B.PLAY);
        s = await t.waitFor((x) => x.playing && x.what === 1 && x.song === n, 2000, `SONG ${hex2(n)} PLAYING`);
        t.check(s.bpm10 === bpm10, `SONG ${hex2(n)} AT ${s.bpm10 / 10} BPM, NOT LOOPS' ${bpm10 / 10}`);
        await t.tap(B.PLAY);
        t.note(`song ${hex2(n)}: 1 pattern at ${bpm10 / 10} BPM, LOOPS' tempo`);
        await t.revert();
    } },
    /* the FM-1's input taken by the page (the browser asks for it now: it keeps no answer for a page opened from a
     * file) and let go after: every other scenario is measured while the FM-1 sends no USB audio */
    { group: "USB", name: "AUDIO OUT", input: true, secs: 74, tsecs: 57, what: "the stand and MIDNIGHT RUN while this page takes the FM-1's USB audio " +
      "(no file); the debug build: the test tone on and off", run: async (t) => {
        const u = await t.usbIn(true);
        if (!u.ok) t.skip(u.why);
        try {
            await djStand(t);
            await t.measure("USB OUT: DJ STAND", 30000);
            await t.screen("DJ");
            await t.tap(B.PLAY);
            await t.playSong("MIDNIGHT RUN", 2);
            await t.measure("USB OUT: MIDNIGHT RUN", 20000);
            await t.tap(B.PLAY);
            if (t.debug) {
                t.send(TONE(1));
                await t.waitFor((x) => x.tone, 1000, "THE TEST TONE ON");
                try { await t.measure("USB OUT: THE TEST TONE", 5000); } finally { t.send(TONE(0)); }
                await t.waitFor((x) => !x.tone, 1000, "THE TEST TONE OFF");
            }
            const w = t.windows("USB OUT"), sent = w.filter((f) => f.usb && (f.usb.on || f.usb.pkts)).length;
            const checked = u.check && w.length > 0;        /* (the release build sends no FIGURES: not checked) */
            if (checked) t.check(sent * 2 >= w.length, `THE FM-1 SENT ITS USB AUDIO IN ${sent} OF ${w.length} WINDOWS: ` +
                                 `PICK "LINE (FM-1 PT/FM)" IN THE BROWSER'S PROMPT OR ITS MICROPHONE SETTING`);
            t.note(`input: ${u.name}; ${checked ? `USB audio in ${sent} of ${w.length} windows` : "its USB audio not checked"}`);
        } finally { await t.usbIn(false); }
    } },
    /* FILES: the scenarios whose point is a file: this page records the FM-1's USB audio itself, a WAV in the run's ZIP;
     * unticked at first (specs/debug.md): a run by default makes only its text report. They take the input as AUDIO
     * OUT does (LISTEN plays it) and need no hand */
    { group: "FILES", name: "USB RECORDING", artifact: true, input: true, streams: true, records: true, secs: 100, tsecs: 92,
      what: "the stand and MIDNIGHT RUN recorded by this page: a WAV in the run's ZIP", run: async (t) => {
        await recording(t, async () => {
            await djStand(t);
            await t.measure("USB: DJ STAND", 60000);
            await t.screen("DJ");
            await t.tap(B.PLAY);
            await t.playSong("MIDNIGHT RUN", 2);
            await t.measure("USB: MIDNIGHT RUN", 30000);
            await t.tap(B.PLAY);
        }, "");
    } },
    { group: "FILES", name: "USB TONE", artifact: true, input: true, streams: true, records: true, debug: true, secs: 34, tsecs: 30,
      what: "the USB test tone recorded by this page, 30 s: a WAV in the run's ZIP", run: async (t) => {
        await recording(t, async () => {
            t.send(TONE(1));
            await t.waitFor((x) => x.tone, 1000, "THE TEST TONE ON");
            try { await t.measure("USB TONE", 30000); } finally { t.send(TONE(0)); }
            await t.waitFor((x) => !x.tone, 1000, "THE TEST TONE OFF");
        }, "the tone: 997 Hz left, 1,499 Hz right, -12 dBFS");
    } },
];
/* a FILES scenario's body, recorded: the input taken (skipped when the browser is not allowed it), the page recording
 * it into the scenario's WAV, the body, then the recording ended and said in the report (empty: failed), with what
 * it holds (about) */
async function recording(t, body, about) {
    const u = await t.usbIn(true);
    if (!u.ok) t.skip(u.why);
    try {
        const r = await t.record(true);
        if (!r.ok) t.skip(r.why);
        await body();
        const e = await t.record(false);
        if (e.check) t.check(e.secs > 1, "THE PAGE RECORDED NOTHING FROM THE FM-1'S INPUT");
        t.note(`recorded ${e.check ? `${e.secs.toFixed(1)} s of ` : ""}${u.name}: a WAV in the run's ZIP`);
        if (about) t.note(about);
    } finally {
        await t.record(false);
        await t.usbIn(false);
    }
}
const hex2 = (n) => n.toString(16).toUpperCase().padStart(2, "0");

/* ---- a run ---- */
/* its end, stopped, failed or done (specs/debug.md: STOP): every control let go, the tone off, then as a scenario's
 * start: what plays stopped, all sound off, the changes dropped, no question open; then MIRROR off. Its waits run on
 * after STOP (they are the STOP). What it could not do goes into the report. */
async function finish(io, ctx0, R) {
    try {
        io.send(TONE(0));
        io.send(SECOND(1));                             /* (core 2 back to what SETTINGS says, as the ONE CORE ones end) */
        for (let b = 0; b < 14; b++) io.send(btn(b, 0));
        for (let k = 0; k < 27; k++) io.send(key(k, 0));
    } catch (e) { /* */ }
    io.log("THE END: STOPPING WHAT PLAYS, ALL SOUND OFF, DROPPING THE CHANGES");
    const t = steps({ ...io, stopped: () => false }, { ...ctx0, checks: 0, notes: [], stretches: [], name: "END", last: null });
    try {
        await io.sleep(300);
        await t.prelude();
    } catch (e) {
        R.endProblem = e.message || String(e);
        try {                                           /* still: all sound off, and a question closed by HOME (its BACK) */
            io.send(btn(B.HOME, 1)); await io.sleep(GAP); io.send(btn(B.REC, 1)); await io.sleep(TAP); io.send(btn(B.REC, 0));
            await io.sleep(GAP); io.send(btn(B.HOME, 0)); await io.sleep(GAP);
            for (let i = 0; i < 6; i++) {
                const s2 = await io.ask();
                if (!s2 || !s2.overlay) break;
                io.send(btn(B.HOME, 1)); await io.sleep(TAP); io.send(btn(B.HOME, 0)); await io.sleep(300);
            }
        } catch (x) { /* */ }
    }
    /* then the FM-1 at rest (specs/debug.md: the end): no voice sounding (the state) and its effects resting, from
     * its FIGURES (an effect at work shows its own stage; resting, they are all skipped and none does) */
    if (!R.endProblem && io.measure) {
        try {
            await io.sleep(3000);                       /* (the DELAY's and REVERB's tails fall asleep) */
            io.measure("THE END");
            await io.sleep(2500);
            const w = io.measured(), s = await t.ask();
            if (w.length) {
                const at = (f, n) => (f.stages.find((x) => x.name === n) || { sum: 0 }).sum;
                R.end = { cpu: FG.stats(w).cpu.mean, voices: s.voices, awake: END_AWAKE.filter((n) => w.some((f) => at(f, n) > 0)) };
            }
        } catch (e) { /* (not measured: the report says nothing of it) */ }
    }
    try { io.send(hello(0)); } catch (e) { /* */ }
}
const END_AWAKE = ["REVERB", "DELAY", "CHORUS", "DJ FILTERS", "DJ MIX", "INSERTS", "LIMITER", "PUNCH-INS"];

function run(io, list, opts) {
    opts = opts || {};
    /* the scenarios taking the FM-1's input first: the browser's prompt for it comes at once, while the hand that
     * pressed RUN is still there (a browser keeps a once-only grant just minutes in the background, unused) */
    list = list.filter((sc) => sc.input).concat(list.filter((sc) => !sc.input));
    const clock = io.now || Date.now;
    const R = { results: [], list, started: Date.now(), c0: clock(), current: null, version: "", build: "", done: false,
                recordAudio: !!opts.recordAudio, audioRefused: !!opts.audioRefused };
    const ctx0 = { songNames: [], scale: opts.scale || 1 };
    R.songNames = ctx0.songNames;
    R.promise = (async () => {
        const t0 = steps(io, { ...ctx0, checks: 0, notes: [], stretches: [], name: "START" });
        let s = null;
        try {
            io.send(hello(1));
            await io.sleep(150);
            s = await t0.ask();
        } catch (e) { R.results.push({ group: "", name: "CONNECT", status: "FAILED", why: "THE FM-1 DOES NOT ANSWER THE STATE (A FIRMWARE WITHOUT MIRROR?)" }); R.done = true; return R; }
        R.version = s.version; R.build = s.debug ? "DEBUG" : "RELEASE";
        for (const sc of list) {
            if (io.stopped()) { R.results.push({ group: sc.group, name: sc.name, status: "SKIPPED", why: "STOPPED" }); continue; }
            const ctx = { ...ctx0, checks: 0, notes: [], stretches: [], name: sc.name, last: null };
            const r = { group: sc.group, name: sc.name, what: sc.what, status: "PASSED", why: "", t0: clock(), t1: 0 };
            R.current = { key: `${sc.group}: ${sc.name}`, t0: r.t0, t1: 0 };
            /* its test starts with its first measured stretch (or the bench): what comes before sets the FM-1 up */
            ctx.begin = () => { if (!r.t1) R.current.t1 = r.t1 = clock(); };
            const t = steps(io, ctx);
            t.scale = ctx.scale;
            t.debug = !!s.debug;                        /* (a scenario's part only the debug build has) */
            t.songNames = ctx0.songNames;
            t.bench = () => { ctx.begin(); return io.bench(); };
            t.chip = (b) => { ctx.chip = b; };
            io.log(`${sc.group}: ${sc.name}`);
            try {
                if (sc.debug && !s.debug) throw new Skipped("THE DEBUG BUILD'S ONLY");
                if (!sc.input && io.usbIn) await io.usbIn(false);   /* (the FM-1's input let go: nothing measured with it) */
                await t.prelude();
                if (!ctx0.songNames.length) await learnSongs(t, ctx0);
                await sc.run(t);
                ctx.over = true;
            } catch (e) {
                ctx.over = true;                        /* (its background input stops before the controls are let go) */
                r.status = e instanceof Skipped ? "SKIPPED" : e instanceof Stopped ? "STOPPED" : "FAILED";
                r.why = e.message || String(e);
                if (!(e instanceof Skipped)) {
                    try {
                        io.send(TONE(0));
                        if (sc.group === "ONE CORE") io.send(SECOND(1));   /* (its own finally sends it too) */
                        for (let b = 0; b < 14; b++) io.send(btn(b, 0));
                        for (let k = 0; k < 27; k++) io.send(key(k, 0));
                    } catch (x) { /* */ }
                }
            }
            if (r.status !== "SKIPPED" && r.status !== "STOPPED") {     /* what the scenario left in the edit memory */
                const s2 = await io.ask().catch(() => null);
                if (s2 && s2.edits) ctx.notes.push(`edit memory: ${editsText(s2.edits)}`);
            }
            if (!sc.input && !sc.streams) {                 /* the input held by something else: said, as it costs */
                const w = ctx.stretches.flatMap((x) => x.windows || []), n = w.filter((f) => f.usb && (f.usb.on || f.usb.pkts)).length;
                if (n) ctx.notes.push(`USB AUDIO SENT IN ${n} OF ${w.length} WINDOWS (${R.recordAudio ? "RECORD AUDIO" : "AN APP ON THE COMPUTER TOOK IT"}): ` +
                                      "THE FIGURES INCLUDE IT");
            }
            for (const st of ctx.stretches) {           /* FIGURES' windows lost on the way (one a second expected) */
                const got = (st.windows || []).length, unsent = FG.unsentIn(st.windows || []);
                if (got && st.secs >= 5 && got < 0.8 * st.secs)
                    ctx.notes.push(`${st.name}: ONLY ${got} FIGURES WINDOWS IN ${Math.round(st.secs)} S (USB LOST THE REST; ` +
                                   `THE FM-1 COULD NOT SEND ${unsent} OF THEM): ITS FIGURES COVER THOSE ONLY`);
            }
            r.ms = clock() - r.t0;
            r.testMs = r.t1 ? clock() - r.t1 : 0;       /* (the test's own time, the setting up left out) */
            R.current = null;
            r.checks = ctx.checks;
            r.notes = ctx.notes;
            r.stretches = ctx.stretches;
            r.chip = ctx.chip;
            r.at = ctx.last ? `${ctx.last.screen}${ctx.last.overlay ? " + " + (OVERLAY[ctx.last.overlay] || ctx.last.overlay) : ""}` : "";
            R.results.push(r);
            if (r.status === "STOPPED") break;
        }
        await finish(io, ctx0, R);
        R.done = true;
        R.ended = Date.now();
        R.ms = clock() - R.c0;
        return R;
    })();
    R.lines = () => reportLines(R);
    return R;
}

const mmss = (ms) => { const s = Math.round(ms / 1000); return `${String(Math.floor(s / 60)).padStart(2, "0")}:${String(s % 60).padStart(2, "0")}`; };
function reportLines(R) {
    const L = [], at = new Date(R.started).toISOString().slice(0, 16).replace("T", " ");
    const n = (st) => R.results.filter((r) => r.status === st).length;
    L.push(`PT/FM AUTOTEST  firmware ${R.version || "?"} (${R.build || "?"})  ${at} UTC`);
    L.push(`  ${R.results.length} scenarios: ${n("PASSED")} passed, ${n("FAILED")} failed, ${n("SKIPPED")} skipped` +
           (n("STOPPED") ? `, stopped` : "") + (R.ended ? `; ${mmss(R.ms)}` : "; under way"));
    if (R.recordAudio) L.push("  RECORD AUDIO: a WAV a scenario; USB audio sent all run, so the figures include it (~8 % CPU)");
    if (R.audioRefused) L.push("  RECORD AUDIO: the browser was not allowed the FM-1's input: nothing recorded");
    if (R.endProblem) L.push(`  THE END COULD NOT LEAVE THE FM-1 STOPPED, SILENT AND WITHOUT CHANGES: ${R.endProblem}`);
    if (R.end && !R.end.voices && !R.end.awake.length)
        L.push(`  THE END: AT REST, NO VOICE SOUNDING, THE EFFECTS RESTING (CPU ${R.end.cpu.toFixed(1)} %)`);
    else if (R.end) {
        L.push(`  THE END: NOT AT REST (CPU ${R.end.cpu.toFixed(1)} %): ${R.end.voices} VOICE${R.end.voices === 1 ? "" : "S"} SOUNDING`);
        if (R.end.awake.length) L.push(`    EFFECTS AT WORK: ${R.end.awake.join(", ")}`);
    }
    for (const r of R.results) {
        const took = r.testMs ? `${mmss(r.ms - r.testMs)} setting up + ${mmss(r.testMs)}` : mmss(r.ms || 0);
        L.push(`== ${r.group ? r.group + ": " : ""}${r.name}  ${r.status}${r.why ? ": " + r.why : ""}  (${took}, ` +
               `${r.checks || 0} checks${r.status === "FAILED" && r.at ? ", at " + r.at : ""})`);
        if (r.what) L.push(`   ${r.what}`);
        for (const x of r.notes || []) L.push(`   ${x}`);
        for (const st of r.stretches || []) {
            const s = st.windows && st.windows.length ? FG.stats(st.windows) : null;
            if (s) L.push(...FG.sectionLines(st.name, s));
            else L.push(`-- ${st.name}: no figures (not the debug build)`);
        }
        if (r.chip) L.push(...r.chip.lines());
    }
    return wrapLines(L);
}
/* lines under 100 characters: a longer one goes on in the next, indented, at a space where it can (nothing cut) */
function wrapLines(L) {
    const out = [];
    for (let l of L) {
        while (l.length > 99) {
            let k = l.lastIndexOf(" ", 99);
            if (k < 40) k = 99;
            out.push(l.slice(0, k).trimEnd());
            l = "     " + l.slice(k).trimStart();
        }
        out.push(l);
    }
    return out;
}

const A = { SCENARIOS, run, decodeState, hello, btn, key, knob, ASK, TONE, SECOND, B, K, WHITE, BLACK, CH, wrapLines };
if (typeof module !== "undefined" && module.exports) module.exports = A;
else root.PTFM_autotest = A;
})(typeof window !== "undefined" ? window : globalThis);

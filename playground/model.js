/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground: what the screens show and change: cartridges and instruments, the pattern
 * pool, songs with their timing, mixer and FX setups, the loops, the GATE patterns, the settings.
 * The demo disk is content.js's. The "flash", the user area, starts clean and is kept in this browser
 * (IndexedDB): SAVE copies the edits there, REVERT copies them back, a reload starts from what was kept, the
 * edits not saved gone (specs/ui/saving.md); DEMO DISK writes the demo disk into it, ERASE DATA drops it. Values are kept as the files store them (specs/files/: mixer bytes, FX knobs
 * as steps).
 *
 * A cell: {note, inst, vol, cmds}: note a MIDI number (12-127), M.OFF (===) or null; inst a
 * reference, cartridge << 8 | slot, or null (the track's current instrument); vol 0-127 or null
 * (the instrument's default); cmds up to four commands, each {c: code, v: value} (cmds.js). */
PTFM.model = (function () {
    "use strict";
    const C = PTFM.CONTENT;
    const M = { OFF: -1 };
    const NOTES = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"];
    M.noteName = n => n === M.OFF ? "===" : n == null ? "---" : NOTES[n % 12] + (Math.floor(n / 12) - 1);
    M.hex = (v, w = 2) => v.toString(16).toUpperCase().padStart(w, "0");
    M.dec = (v, w = 2) => String(v).padStart(w, "0");
    /* a signed value: its sign, then hex */
    M.sgn = (v, w = 1) => (v > 0 ? "+" : v < 0 ? "-" : "") + M.hex(Math.abs(v), w);
    /* N/M, where you are of n (specs/ui/frame.md, Numbers): the index and the last index, both hex */
    M.of = (i, n, w = 1) => { const m = M.hex(Math.max(0, n - 1), w); return M.hex(i, m.length) + "/" + m; };
    /* pages, and the songs on SONGS, are counted: decimal, from 1, of the total (`1/4`; specs/ui/frame.md) */
    M.pageOf = (i, n) => (i + 1) + "/" + n;
    /* FILTER's CUT (-64 .. 63, 0 OFF) as the DSP sets it: low-pass 20 kHz down to 100 Hz, high-pass 20 Hz up to 8 kHz */
    M.filterHz = c => c < 0 ? 20000 * Math.pow(100 / 20000, -c / 64) : 20 * Math.pow(8000 / 20, c / 63);
    M.hzShort = f => f >= 1000 ? (f / 1000).toFixed(f >= 10000 ? 0 : 1) + "K" : String(Math.round(f));
    /* FILTER's envelope's times by step, 0.1 ms (specs/ui/screens/fx.md#filters-envelope): ATTACK's, then DECAY's
     * and RELEASE's, rounded as the firmware's table (fw/tools/gen_fx.py) */
    M.ENV_T10 = [Array.from({ length: 128 }, (_, i) => Math.round(20 * Math.pow(1500, i / 127))),
                 Array.from({ length: 128 }, (_, i) => Math.round(50 * Math.pow(1000, i / 127)))];
    M.envTime = t10 => t10 < 100 ? Math.floor(t10 / 10) + "." + t10 % 10 + " MS"
        : Math.floor((t10 + 5) / 10) < 1000 ? Math.floor((t10 + 5) / 10) + " MS"
        : (cs => Math.floor(cs / 100) + "." + String(cs % 100).padStart(2, "0") + " S")(Math.floor((t10 + 50) / 100));
    M.ENV_SOURCES = ["4 BEATS", "2 BEATS", "BEAT", "1/2 BEAT", "1/4 BEAT"];
    M.envSource = v => v === 0 ? "KEYS" : v <= 16 ? "TRACK " + M.hex(v - 1) : M.ENV_SOURCES[Math.min(4, v - 17)];
    M.refText = r => M.hex(r >> 8) + ":" + M.hex(r & 255);

    M.ENGINES = {
        DX7: { col: 12, short: "DX7" }, JUNO: { col: 11, short: "JUNO" }, TB303: { col: 10, short: "TB-303" },
        SAMPLER: { col: 14, short: "SAMPLE" }, FUSION: { col: 13, short: "FUSION" }, CHIP: { col: 9, short: "CHIP" },
    };
    M.SIZES = { DX7: 32, JUNO: 128, TB303: 128, SAMPLER: 64, FUSION: 64, CHIP: 128 };
    M.TYPE_NAMES = { DX7: "DX7", JUNO: "JUNO-106", TB303: "TB-303", SAMPLER: "SAMPLER", FUSION: "FUSION", CHIP: "CHIP" };

    /* the samples' audio stays out of the edited state: M.audio[id], the sample records refer to it */
    M.audio = [];
    function b64(s) {
        const bin = atob(s), a = new Int8Array(bin.length);
        for (let i = 0; i < bin.length; i++) a[i] = bin.charCodeAt(i) << 24 >> 24;
        return a;
    }

    /* ------------------------------------------------------------ instruments' defaults --- */
    const INIT = {
        DX7: () => ({ algorithm: 1, feedback: 0, osc_key_sync: true, transpose: 0,
            pitch_eg: { rates: [99, 99, 99, 99], levels: [50, 50, 50, 50] },
            lfo: { speed: 35, delay: 0, pm_depth: 0, am_depth: 0, key_sync: true, wave: "triangle", pitch_mod_sens: 3 },
            ops: [0, 1, 2, 3, 4, 5].map(i => ({ eg: { rates: [99, 99, 99, 99], levels: [99, 99, 99, 0] }, level: i ? 0 : 99,
                mode: "ratio", coarse: 1, fine: 0, detune: 0, rate_scaling: 0, amp_mod_sens: 0, key_vel_sens: 0,
                kbd: { break_point: 39, left_depth: 0, right_depth: 0, left_curve: "-lin", right_curve: "-lin" } })) }),
        JUNO: () => ({ lfo: { rate: 40, delay: 0 }, dco: { lfo: 0, pwm: 0, noise: 0, sub: 0, range: "8'", pulse: false, saw: true,
            pwm_source: "manual" }, hpf: 1, vcf: { freq: 127, res: 0, env: 0, lfo: 0, kybd: 0, env_polarity: "+" },
            vca: { level: 64, mode: "env" }, env: { a: 0, d: 0, s: 127, r: 10 }, chorus: "off" }),
        TB303: () => ({ wave: 0, cutoff: 64, reso: 64, envmod: 64, decay: 64, accent: 64 }),
        /* a new sample instrument plays its sample whole (specs/files/instruments.md, Sample instruments) */
        SAMPLER: () => ({ sample: 0, start: 0, end: 0, loop: "OFF", loopStart: 0, loopEnd: 0, base: 60, tune: 0, volume: 64,
                          env: { rates: [99, 99, 99, 80], levels: [99, 99, 99, 0] } }),
        FUSION: () => ({ pattern: null }),
        /* a new CHIP instrument is the engine's reference sound (specs/chip.md): PULSE 50 %, CLOCK EXACT, CURVE BRK,
         * FILTER NONE, VOL 15, no table, no vibrato */
        CHIP: () => ({ osc: 0, duty: 2, bz_shape: 0, bz_steps: 0, bz_gate: 0, n_mix: 0, n_short: 0, n_rate: 4, n_key: 0,
                       clock: 6, curve: 0, filter: 3, fps50: 0, sweep: 0, sw_shift: 2, sw_pace: 2, vol: 15, env: 0,
                       env_step: 0, release: 0, psg_shape: 9, psg_period: 800, wave: 0, tab: [0, 0, 0, 0, 0],
                       spd: [1, 1, 1, 1, 1], vib_delay: 0, vib_speed: 1, vib_depth: 0 }),
    };
    M.init = (type, name) => ({ name: name || "INIT", level: 0, p: INIT[type]() });

    /* -------------------------------------------------------------------------- loading --- */
    const carts = C.cartridges.map(c => {
        const cart = { id: c.id, name: c.name, type: c.type, size: c.size, slots: new Array(c.size).fill(null) };
        if (c.samples) {
            cart.samples = c.samples.map(s => {
                const id = M.audio.length, data = b64(s.audio);
                M.audio.push(data);
                return { name: s.name, kind: s.kind, rate: s.rate, length: data.length, audio: id };
            });
        }
        c.slots.forEach((s, i) => {
            const p = s.dx7 || s.juno || s.tb303 || s.sampler || s.fusion || s.chip;
            cart.slots[i] = { name: s.name, level: s.level || 0, p };
        });
        return cart;
    });

    /* a cell from content.js: [row, track, note (0 none, 1 off), instrument, volume, code, value, ...] */
    function cellOf(a) {
        const cmds = [];
        for (let i = 5; i + 1 < a.length; i += 2) cmds.push({ c: a[i], v: a[i + 1] });
        return { note: a[2] === 0 ? null : a[2] === 1 ? M.OFF : a[2], inst: a[3] < 0 ? null : a[3], vol: a[4] < 0 ? null : a[4], cmds };
    }
    function patternOf(src, n) {
        const p = M.newPattern(n, src.tracks, src.rows);
        p.step = src.step == null ? 1 : src.step;
        p.what = src.what || "";
        for (const a of src.cells) p.cells[a[0] * 16 + a[1]] = cellOf(a);
        return p;
    }
    M.newPattern = (n, tracks, rows) => ({ n, tracks, rows, step: 1, what: "", cells: new Array(rows * 16).fill(null) });
    M.newCell = (note, inst, vol) => ({ note, inst, vol, cmds: [] });
    M.cellEmpty = c => !c || (c.note == null && c.inst == null && c.vol == null && !c.cmds.length);
    M.copyCell = c => c ? { note: c.note, inst: c.inst, vol: c.vol, cmds: c.cmds.map(x => ({ c: x.c, v: x.v })) } : null;
    const patterns = new Array(256).fill(null);
    for (const p of C.patterns) patterns[p.n] = patternOf(p, p.n);
    /* the EDITOR's own pattern (specs/ui/screens/editor.md#the-editors-own-pattern): FE, the firmware's own, empty
     * (8 tracks x 64 rows, EDIT STEP 1), never listed (M.patternNums); FF stays "no pattern" */
    M.EDITOR_PATTERN = 0xFE;
    patterns[M.EDITOR_PATTERN] = M.newPattern(M.EDITOR_PATTERN, 8, 64);
    M.PAT_ROWS_MAX = 128;                /* a pattern's rows at most (specs/files/patterns.md#size) */

    /* FWS, the firmware space (specs/ui/saving.md#fws): the firmware and what it carries, never written while
     * running; everything there is edited as anywhere. The numbers F0-FF are the firmware's: the built-in
     * instruments' cartridges (F0-FA), any built-in patterns (none yet) and the EDITOR's own pattern (FE), never
     * deleted or moved; the user's numbers are 00-EF. `rom` marks a built-in as the firmware has it (brown); a
     * changed one is saved into the user area (SAVE clears its `rom`, it keeps `builtin`, its change found
     * first); BACK TO BUILT-IN deletes the change, the firmware's own back (M.builtinOriginal) */
    M.USER_LAST = 0xEF;
    M.builtinCart = id => id > M.USER_LAST;
    M.builtinPattern = n => n > M.USER_LAST;
    for (const c of carts) if (M.builtinCart(c.id)) c.rom = c.builtin = true;
    for (const p of patterns) if (p && M.builtinPattern(p.n)) p.rom = p.builtin = true;
    const BUILTIN = { cart: new Map(), pattern: new Map() };   /* the built-ins as the firmware has them */
    for (const c of carts) if (c.builtin) BUILTIN.cart.set(c.id, JSON.stringify(c));
    for (const p of patterns) if (p && p.builtin) BUILTIN.pattern.set(p.n, JSON.stringify(p));
    /* the firmware's own of a built-in thing, a fresh copy (kind "cart" by id, "pattern" by number); null none */
    M.builtinOriginal = (kind, key) => { const t = BUILTIN[kind].get(key); return t ? JSON.parse(t) : null; };
    /* a built-in thing changed from the firmware's own (its `rom` flag aside) */
    const unflagged = o => { const x = Object.assign({}, o); delete x.rom; delete x.builtin; return JSON.stringify(x); };
    M.builtinChanged = (kind, thing) => {
        const t = thing && thing.builtin && BUILTIN[kind].get(kind === "cart" ? thing.id : thing.n);
        return !!t && unflagged(thing) !== unflagged(JSON.parse(t));
    };

    /* ---------------------------------------------------------------------------- the FX --- */
    const pct = { min: 0, max: 100, unit: "%", def: 50 };
    const NV = ["1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2"];
    M.FX = {
        GATE: { col: 9, knobs: [{ n: "PATTERN", min: 0, max: 31, def: 0, fmt: v => M.hex(v) },
            { n: "RATE", list: ["1/8", "1/16", "1/32", "1/8T", "1/16T"], def: 1 },
            Object.assign({}, pct, { n: "DEPTH", def: 100 }), Object.assign({}, pct, { n: "SMOOTH", def: 20 })] },
        /* CUT in Hz wherever it shows (`LP 3.8K`, `HP 347`): the DSP's curve (dsp-fx.js FILTER) */
        FILTER: { col: 3, knobs: [{ n: "CUT", min: -64, max: 63, def: 0, fmt: v => v === 0 ? "OFF" : (v < 0 ? "LP " : "HP ") + M.hzShort(M.filterHz(v)) },
            Object.assign({}, pct, { n: "RESO", def: 30 }),
            /* its envelope (specs/ui/screens/fx.md#filters-envelope), at the codes 0xB0-0xB5 (M.fxCode) */
            Object.assign({}, pct, { n: "ENV", def: 0 }), { n: "SOURCE", min: 0, max: 21, def: 0, fmt: v => M.envSource(v) },
            { n: "ATTACK", min: 0, max: 127, def: 0, fmt: v => M.envTime(M.ENV_T10[0][v]) },
            { n: "DECAY", min: 0, max: 127, def: 75, fmt: v => M.envTime(M.ENV_T10[1][v]) },
            Object.assign({}, pct, { n: "SUSTAIN", def: 0 }),
            { n: "RELEASE", min: 0, max: 127, def: 75, fmt: v => M.envTime(M.ENV_T10[1][v]) }] },
        DRIVE: { col: 12, knobs: [Object.assign({}, pct, { n: "DRIVE", def: 30 }), Object.assign({}, pct, { n: "TONE", def: 50 })] },
        "LO-FI": { col: 14, knobs: [{ n: "BITS", min: 2, max: 16, def: 8 }, { n: "RATE", min: 1, max: 44, def: 22, unit: " KHZ" },
            Object.assign({}, pct, { n: "DUST", def: 20 })] },
        CHORUS: { col: 11, knobs: [{ n: "MODE", list: ["I", "II", "I+II"], def: 0 }, Object.assign({}, pct, { n: "RATE", def: 30 }),
            Object.assign({}, pct, { n: "DEPTH", def: 50 }), Object.assign({}, pct, { n: "MIX", def: 50 })] },
        DELAY: { col: 10, knobs: [{ n: "TIME", list: NV, def: 7 }, Object.assign({}, pct, { n: "FEEDBACK", def: 40 }),
            Object.assign({}, pct, { n: "TONE", def: 60 }), { n: "PING-PONG", list: ["OFF", "ON"], def: 0 },
            Object.assign({}, pct, { n: "MIX", def: 30 })] },
        REVERB: { col: 13, knobs: [Object.assign({}, pct, { n: "SIZE", def: 60 }), Object.assign({}, pct, { n: "DAMP", def: 40 }),
            { n: "PRE-DELAY", min: 0, max: 100, def: 10, unit: " MS" }, Object.assign({}, pct, { n: "MIX", def: 25 })] },
        LIMITER: { col: 15, knobs: [{ n: "CEILING", min: -12, max: 0, def: -1, unit: " DB" },
            { n: "RELEASE", min: 10, max: 1000, def: 100, unit: " MS", step: 10 }] },
        DUCK: { col: 6, knobs: [{ n: "SOURCE", min: 0, max: 16, def: 0, fmt: v => v >= 16 ? "KEYS" : "TRACK " + M.hex(v) },
            Object.assign({}, pct, { n: "DEPTH", def: 60 }), { n: "RELEASE", min: 20, max: 1000, def: 150, unit: " MS", step: 10 }] },
        METRONOME: { col: 2, knobs: [{ n: "TEMPO", min: 29, max: 300, def: 29, fmt: v => v === 29 ? "FOLLOW" : v + " BPM" },
            { n: "BEATS A BAR", min: 1, max: 16, def: 4 }, { n: "SOUND", list: ["CLICK", "BEEP", "WOOD", "COWBELL"], def: 1 },
            Object.assign({}, pct, { n: "LEVEL", def: 60 })] },
    };
    /* each knob's FX command (specs/files/commands.md#mixer-and-fx-0x80-0xbf): the effects' knobs one after another
     * from 0x90 (GATE .. DUCK), but FILTER's envelope's, added later, at 0xB0-0xB5 */
    M.FX_CODES = {};
    {
        let c = 0x90;
        for (const n of ["GATE", "FILTER", "DRIVE", "LO-FI", "CHORUS", "DELAY", "REVERB", "LIMITER", "DUCK"])
            M.FX[n].knobs.forEach((k, i) => { M.FX_CODES[n === "FILTER" && i >= 2 ? 0xB0 + i - 2 : c++] = [n, i]; });
    }
    M.fxCode = (name, i) => +Object.keys(M.FX_CODES).find(c => M.FX_CODES[c][0] === name && M.FX_CODES[c][1] === i);
    /* the effects' numbers (FX ON / OFF's values), and the effect an FX command turns */
    M.FX_ORDER = ["GATE", "FILTER", "DRIVE", "LO-FI", "CHORUS", "DELAY", "REVERB", "LIMITER", "DUCK", "METRONOME"];
    M.fxOfCode = (code, v) => code === 0xAE || code === 0xAF ? M.FX_ORDER[v] : (M.FX_CODES[code] || [])[0];
    /* the chain's 9 items, as a song keeps them: the 8 effects and the SENDS item */
    M.FX_CHAIN = ["GATE", "FILTER", "LO-FI", "SENDS", "CHORUS", "DELAY", "REVERB", "DRIVE", "LIMITER"];
    M.SENDS = ["CHORUS", "DELAY", "REVERB"];
    M.fxValue = (name, i, v) => {
        const k = M.FX[name].knobs[i];
        if (k.list) return k.list[v];
        if (k.fmt) return k.fmt(v);
        return v + (k.unit || "");
    };
    function fxDefaults() {
        const knobs = {}, on = {};
        for (const n in M.FX) { knobs[n] = M.FX[n].knobs.map(k => k.def); on[n] = n === "LIMITER"; }
        return { routing: "GLOBAL", chain: M.FX_CHAIN.slice(), on, knobs };
    }
    /* an FX setup as the demo disk writes it -> the knobs as the FX screen keeps them */
    function fxOf(src) {
        const fx = fxDefaults(), pick = (n, i, v) => Math.max(0, M.FX[n].knobs[i].list.indexOf(v));
        fx.routing = src.routing.toUpperCase();
        fx.chain = src.chain.slice();
        const g = src.gate, f = src.filter, d = src.drive, l = src.lofi, c = src.chorus, y = src.delay, r = src.reverb;
        const lm = src.limiter, k = src.duck, m = src.metronome;
        const cut = f.cut === "OFF" ? 0 : f.cut.startsWith("LP") ? -parseInt(f.cut.slice(3)) : parseInt(f.cut.slice(3));
        Object.assign(fx.knobs, {
            GATE: [g.pattern - 1, pick("GATE", 1, g.rate), g.depth, g.smooth],
            FILTER: [cut, f.reso].concat(M.FX.FILTER.knobs.slice(2).map((k, i) => {   /* (its envelope: a new song's
                                                                                       * where the disk has none) */
                const v = f[["env", "source", "attack", "decay", "sustain", "release"][i]];
                return v != null ? v : k.def;
            })), DRIVE: [d.drive, d.tone],
            "LO-FI": [l.bits, l.rate, l.dust], CHORUS: [pick("CHORUS", 0, c.mode), c.rate, c.depth, c.mix],
            DELAY: [pick("DELAY", 0, y.time), y.feedback, y.tone, y.ping_pong ? 1 : 0, y.mix],
            REVERB: [r.size, r.damp, Math.min(100, r.pre_delay), r.mix], LIMITER: [lm.ceiling, lm.release], DUCK: [k.source - 1, k.depth, k.release],
            METRONOME: [m.tempo === "FOLLOW" ? 29 : m.tempo, m.beats_a_bar, pick("METRONOME", 2, m.sound), m.level],
        });
        const keys = { GATE: g, FILTER: f, DRIVE: d, "LO-FI": l, CHORUS: c, DELAY: y, REVERB: r, LIMITER: lm, DUCK: k, METRONOME: m };
        for (const n in keys) fx.on[n] = !!keys[n].on;
        return fx;
    }
    /* a track's mixer, as a song keeps it (specs/files/songs.md): LEVEL 00-7F, PAN 00-FF (80 the middle),
     * DUCK and the three sends 00-7F */
    M.MIX_ROWS = [{ k: "lvl", n: "LVL", def: 127 }, { k: "pan", n: "PAN", def: 128, max: 255 }, { k: "dck", n: "DCK", def: 0 },
                  { k: "cho", n: "CHO", def: 0 }, { k: "dly", n: "DLY", def: 0 }, { k: "rev", n: "REV", def: 0 }];
    const mixRow = r => ({ lvl: r.level, pan: r.pan, dck: r.duck, cho: r.chorus, dly: r.delay, rev: r.reverb });
    const mixer = n => Array.from({ length: n }, () => ({ lvl: 127, pan: 128, dck: 0, cho: 0, dly: 0, rev: 0 }));
    M.newFx = fxDefaults;
    M.newMixer = () => mixer(16);

    const songs = C.songs.map(s => ({
        name: s.name, what: s.what, tempo: s.tempo, ticks: s.ticks, rpb: s.rpb, swing: s.swing, atEnd: s.atEnd,
        loopFrom: s.loopFrom, slides: s.slides, fastVol: s.fastVol, gvol: s.gvol, order: s.order.slice(),
        fx: fxOf(s.fx), mixer: s.mixer.map(mixRow) }));
    M.newSong = () => ({ name: "NEW SONG", what: "", tempo: 120, ticks: 6, rpb: 4, swing: 50, atEnd: "LOOP", loopFrom: 0,
                         slides: "linear", fastVol: false, gvol: 64, order: [], fx: fxDefaults(), mixer: mixer(16) });
    /* the keys' song (specs/ui/screens/fx.md#the-fx-setup): the firmware's own song FE, never listed, a new song's
     * time and setup, and the REVERB on at its own settings; with no song at all the keys play through it and FX edits
     * it. Changed, it is kept as a built-in's change is (SAVE). The user's songs end at FD */
    M.newKeys = () => {
        const k = Object.assign(M.newSong(), { name: "THE KEYS" });
        k.fx.on.REVERB = true;
        return k;
    };
    M.SONG_LAST = 0xFD;
    /* the musical tempo the screens show (specs/files/commands.md, Time) */
    M.bpm = s => Math.round(s.tempo * 24 / (s.ticks * s.rpb));
    /* TEMPO's range, a song's and the loops' (specs/files/songs.md): a tick 2.5 s / TEMPO, 0.83 s to 4.2 ms; every
     * tempo a replay plays is held within it. The BPM DJ and PLAYBACK can set a song to: 3 to 600, where its ticks
     * stay within TEMPO's range (specs/ui/screens/dj.md#tempo-and-sync) */
    M.TEMPO_MIN = 3;
    M.TEMPO_MAX = 600;
    M.tempoRange = s => {
        const d = (s.ticks || 6) * (s.rpb || 4);
        return [Math.max(M.TEMPO_MIN, Math.ceil(M.TEMPO_MIN * 240 / d) / 10), Math.min(M.TEMPO_MAX, Math.floor(M.TEMPO_MAX * 240 / d) / 10)];
    };

    /* the GATE patterns: 16 built in, 16 of your own (0 off, 1 on, 2-4 ratchets) */
    const G = s => s.split("").map(Number);
    const gates = [
        ["STRAIGHT", "1111111111111111"], ["OFFBEAT", "0101010101010101"], ["TRANCE", "1011101110111011"],
        ["HALF", "1100110011001100"], ["GALLOP", "1011011010110110"], ["SKIP", "1101101101101101"],
        ["SPARSE", "1000100010001000"], ["ROLL", "1111222211113333"], ["STUTTER", "2020202020202020"],
        ["DOTTED", "1001001001001001"], ["SYNCOPE", "1001010010010100"], ["PULSE", "1110111011101110"],
        ["RAMP", "1010110111101111"], ["BREAK", "1111111100000000"], ["TRIPS", "3030303030303030"],
        ["CHOP", "1100101011001010"],
    ].map(([name, s]) => ({ name, steps: G(s) }));
    for (let i = 0; i < 16; i++) gates.push({ name: "MY GATE " + M.hex(16 + i), steps: new Array(16).fill(i ? 0 : 1) });

    /* LOOPS (specs/files/loops.md): one length for all (rows, whole bars; 0 before the first loop), the setup, the next
     * loops' mode and the next loop's mix, and the loops, 16 at most, in the order they were made: each its instrument,
     * colour (0-11, M.LOOP_COLS), MONO or POLY, lanes (1-4), base (rows) and repeat count, muted, soloed, and its notes
     * once: cells[4 r + l], lane l's cell at row r of its base (no instrument in them: the loop's). Loop i is track
     * 16 + i: its mix is mixer[i]; the mixers past the last loop are the next loop's mix (a new loop's take and the
     * pattern recorder's preview sound through it). sel: the selection as SAVE kept it (the screen keeps its own).
     * The playground starts with none: the demo disk keeps no loops */
    M.LOOPS_MAX = 16;
    M.LOOP_COLS = [12, 14, 10, 11, 9, 13, 6, 2, 3, 4, 5, 7];   /* light red, yellow, light green ... light grey */
    M.QUANTIZES = ["2 ROWS", "3 ROWS", "4 ROWS", "ROW", "REAL TIME"];
    M.LOOP_COUNT_INS = ["OFF", "1 BAR", "2 BARS"];
    M.newLoops = () => ({ tempo: 120, ticks: 6, rpb: 4, swing: 50, quantize: "ROW", legato: "GLIDE", countIn: "OFF",
                          nextMono: false, nextMix: mixer(1)[0], sel: 0, rows: 0, list: [], mixer: mixer(16), fx: fxDefaults() });
    const loops = M.newLoops();
    /* a record kept in an older shape (the first LOOPS: one pattern of 16 tracks) is not read: LOOPS starts empty */
    const loopsOk = j => !!(j && Array.isArray(j.list) && j.nextMix && !j.pattern && j.list.every(l => Array.isArray(l.cells)));

    /* COUNT-IN's choices, as the settings record keeps them (0-3): KEY, the take waits for the first note
     * (specs/ui/transport.md); LOOPS' own are M.LOOP_COUNT_INS. The rhythms ARP repeats in, # a note, _ a rest
     * (specs/files/settings.md: kept by their place, the list grows at its end) */
    M.COUNT_INS = ["OFF", "1 BAR", "2 BARS", "KEY"];
    M.RHYTHMS = ["#", "#_", "#__", "#___", "##_", "###_", "##__", "#_##_", "#_______", "#_______________"];
    /* the arpeggio's order of its notes, and how many it keeps once their keys are let go (OFF: a note
     * stops with its key): one ARP for every screen but the EDITOR (specs/ui/controls.md) */
    M.ARP_PATTERNS = ["UP", "DOWN", "UP-DOWN", "RANDOM", "AS PLAYED"];
    M.ARP_LATCHES = ["OFF", "1 NOTE", "2 NOTES", "3 NOTES", "4 NOTES", "5 NOTES", "6 NOTES", "7 NOTES", "8 NOTES"];
    M.latchN = v => Math.max(0, M.ARP_LATCHES.indexOf(v));
    M.SETTINGS = [
        /* SYNC PULSE L / R: a click a half beat on that side, the music mono on the other (specs/connections.md#clock) */
        { k: "OUTPUT", list: ["STEREO", "MONO", "SYNC PULSE L", "SYNC PULSE R"], def: "STEREO" },
        /* the analog out's level (the speaker and the jack), with MASTER; USB audio's, without it */
        { k: "LINE GAIN", min: -24, max: 12, def: 0, fmt: v => (v > 0 ? "+" : "") + v + " DB" },
        { k: "USB GAIN", min: -24, max: 12, def: 0, fmt: v => (v > 0 ? "+" : "") + v + " DB" },
        { k: "VOICES", list: [8, 12, 16], def: 16 },  /* (16: 12 while the second core does not run: settings.md#voices) */
        /* the effects and the output stage on a second CPU, or all on one (specs/second-core.md) */
        { k: "SECOND CORE", list: ["ON", "OFF"], def: "ON" },
        { k: "THEME", list: PTFM.draw.THEMES, def: "DEFAULT" },
        { k: "KEYS VOLUME", min: 1, max: 127, def: 0x60, fmt: v => M.hex(v) },
        /* the keys as heard (the FM-1's and MIDI's), not as recorded: a gain on the voices they start */
        { k: "KEYS GAIN", min: -24, max: 12, def: 0, fmt: v => (v > 0 ? "+" : "") + v + " DB" },
        /* the keys as heard through the effects, or DRY: past every effect (specs/ui/screens/fx.md#the-keys-dry); FX's
         * GLO shows it too */
        { k: "KEYS", list: ["THROUGH FX", "DRY"], def: "THROUGH FX" },
        { k: "KEYS OCTAVE", min: -4, max: 4, def: 0, fmt: v => M.sgn(v) },
        { k: "KNOB SPEED", list: ["OFF", "LOW", "HIGH"], def: "LOW" },
        { k: "HOLD DELAY", min: 300, max: 1500, step: 50, def: 500, unit: " MS" },
        { k: "COMBO GRACE", min: 0, max: 250, step: 10, def: 100, unit: " MS" },
        { k: "PLAYBACK SCROLL", list: ["ROWS", "PAGES"], def: "ROWS" },
        /* the EDITOR's takes (LOOPS has its own, in its properties: M.LOOP_COUNT_INS): 4 beats a bar, its own click */
        { k: "COUNT-IN", list: M.COUNT_INS, def: "1 BAR" },
        /* where the EDITOR's takes put a note (LOOPS has its own, in its properties): QUANTIZE's values, ROW at first */
        { k: "QUANTIZE", list: M.QUANTIZES, def: "ROW" },
        /* ARP: the rhythm (the EDITOR's row preview's too), the pattern, the latch (ARP held + SELECT,
         * ALGORITHM, K1 change them too) */
        { k: "ARP RHYTHM", list: M.RHYTHMS, def: M.RHYTHMS[3] },
        { k: "ARP PATTERN", list: M.ARP_PATTERNS, def: "UP" },
        { k: "ARP LATCH", list: M.ARP_LATCHES, def: "4 NOTES" },
        { k: "POWER-ON SCREEN", list: ["STATUS", "LAST", "SONGS", "EDITOR", "INSTRUMENTS", "PLAYBACK", "FX", "VISUAL", "SETTINGS", "LOOPS", "DJ"], def: "STATUS" },
        { k: "TUNING", min: 430, max: 450, def: 440, unit: " HZ" },
        { k: "TOOLS", list: ["CHECK REFERENCES", "CHECK STORAGE", "CLEAN UP", "AUTO LEVEL ALL", "REVERT ALL"], def: "CHECK REFERENCES" },
        { k: "ABOUT", list: ["VERSION, LICENCES"], def: "VERSION, LICENCES" },
    ];
    /* SETTINGS' MIDI page (specs/connections.md): MIDI in and out, the clock; LEARN adds to the MIDI map */
    M.MIDI_SETTINGS = [
        { k: "MIDI IN CHANNEL", list: ["ALL"].concat([...Array(16).keys()].map(i => i + 1)), def: "ALL" },
        { k: "MIDI OUT", list: ["OFF", "KEYS", "ALL"], def: "KEYS" },
        { k: "MIDI OUT CHANNEL", min: 1, max: 16, def: 1 },
        { k: "SYNC", list: ["INTERNAL", "EXTERNAL"], def: "INTERNAL" },
        { k: "CLOCK OUT", list: ["OFF", "ON"], def: "OFF" },
        { k: "MIDI CONTROLLER", ctl: true },                 /* (no value: EDIT switches, after its questions) */
        { k: "LEARN", learn: true },
    ];
    /* DJ's setup (specs/files/dj.md): its properties and its FX setup, SENDS routing, of which DJ uses CHORUS,
     * DELAY, REVERB, the LIMITER and the METRONOME. A new store's: DELAY on at 1/8., PING-PONG, FEEDBACK 40 %,
     * MIX 30 %; REVERB on, SIZE 70 %, MIX 35 %; the LIMITER on. The decks are never stored (the player's) */
    M.DJ_XFADERS = ["SMOOTH", "SHARP"];
    M.DJ_LFO_FX = ["REVERB MIX", "DELAY MIX", "CHORUS MIX"];   /* what the FX LFO (the LFO pads 13-15) swings */
    function djFx() {
        const fx = fxDefaults();
        fx.routing = "SENDS";                               /* (the chain keeps SENDS before its three, as SENDS routing does) */
        Object.assign(fx.on, { DELAY: true, REVERB: true, LIMITER: true });
        fx.knobs.DELAY = [NV.indexOf("1/8."), 40, fx.knobs.DELAY[2], 1, 30];
        fx.knobs.REVERB = [70, fx.knobs.REVERB[1], fx.knobs.REVERB[2], 35];
        return fx;
    }
    /* stand: the decks as SAVE last kept them (the player's P.djStand; null: nothing set), loaded at power-on */
    const dj = { xfader: "SMOOTH", reso: 30, filterSends: "ON", lfoFx: "REVERB MIX", fx: djFx(), stand: null };

    const settings = {};
    for (const s of M.SETTINGS.concat(M.MIDI_SETTINGS)) if (!s.learn && !s.ctl) settings[s.k] = s.def;
    /* FX MIXER's KEYS column (specs/ui/screens/fx.md#mixer): the keys' strip as a track's mixer (MIX_ROWS' keys), its LVL
     * KEYS GAIN's; one for every setup, kept with the settings (not on SETTINGS' list) */
    M.KEYS_MIX_DEF = { pan: 128, dck: 0, cho: 127, dly: 127, rev: 127 };
    settings["KEYS MIX"] = { ...M.KEYS_MIX_DEF };
    /* the MIDI map (specs/files/midi.md): {ch 0-15, msg "CC" / "NOTE", num 0-127, ctl (the FM-1's control's
     * number), kind 0-5}, in the order learned; 64 at most */
    const midi = [];
    M.MIDI_KINDS = ["ABSOLUTE", "RELATIVE", "RELATIVE", "RELATIVE", "MOMENTARY", "TOGGLE"];
    M.MIDI_MAX = 64;

    const keys = M.newKeys();
    /* the demo disk, as content.js has it; the user area starts without it (a new FM-1's: the built-ins alone, the
     * EDITOR's own pattern, every record at its default), and DEMO DISK writes it in (M.demoDisk) */
    const DEMO = structuredClone({ carts, patterns, songs, gates, loops, settings, midi, dj, keys });
    function cleanState() {
        const s = structuredClone(DEMO);
        s.carts = s.carts.filter(c => c.builtin);
        s.patterns = s.patterns.map(p => p && (p.builtin || p.n === M.EDITOR_PATTERN) ? p : null);
        s.songs = [];
        return s;
    }
    M.state = cleanState();
    /* song i, or the keys' song where there is none (no song at all) */
    M.songOrKeys = i => M.state.songs[i] || M.state.keys;

    /* ---------------------------------------------------------------------------- lookups --- */
    M.cart = id => M.state.carts.find(c => c.id === id);
    M.cartIndex = id => M.state.carts.findIndex(c => c.id === id);
    M.inst = ref => { const c = ref == null ? null : M.cart(ref >> 8); return c ? c.slots[ref & 255] : null; };
    M.instType = ref => { const c = ref == null ? null : M.cart(ref >> 8); return c ? c.type : null; };
    M.instCol = ref => { const t = M.instType(ref); return t && M.inst(ref) ? M.ENGINES[t].col : 4; };
    M.pattern = n => M.state.patterns[n];
    M.sample = (cart, i) => cart.samples[i];
    /* the instrument a track plays at a row: the cell's, else the last one above it in the pattern */
    M.trackInst = (p, r, t) => {
        for (let k = r; k >= 0; k--) { const c = p.cells[k * 16 + t]; if (c && c.inst != null) return c.inst; }
        return null;
    };

    M.patternUses = n => {
        let k = 0;
        for (const s of M.state.songs) for (const o of s.order) if (o === n) k++;
        for (const c of M.state.carts) if (c.type === "FUSION") for (const sl of c.slots) if (sl && sl.p.pattern === n) k++;
        return k;
    };
    M.songsUsing = n => M.state.songs.filter(s => s.order.includes(n)).length;
    M.instUses = ref => {
        let k = 0;
        for (const p of M.state.patterns) if (p) for (const c of p.cells) if (c && c.inst === ref) k++;
        return k;
    };
    /* the patterns' numbers every list shows: never the EDITOR's own */
    M.patternNums = () => M.state.patterns.filter(p => p && p.n !== M.EDITOR_PATTERN).map(p => p.n);
    /* a pattern holds anything: a cell with a note, an instrument, a volume or a command */
    M.patternHolds = n => { const p = M.pattern(n); return !!p && p.cells.some((c, i) => !M.cellEmpty(c) && i % 16 < p.tracks && i < p.rows * 16); };
    /* the first free number of the user's (00-EF), or -1 */
    M.freePattern = () => { for (let n = 0; n <= M.USER_LAST; n++) if (!M.state.patterns[n]) return n; return -1; };
    /* a new cartridge's number: after the last of the user's, so it ends the user's part of the list; once EF
     * is taken, the first free one (specs/ui/screens/instruments.md) */
    M.freeCart = () => {
        const last = Math.max(-1, ...M.state.carts.filter(c => !M.builtinCart(c.id)).map(c => c.id));
        if (last < M.USER_LAST) return last + 1;
        for (let n = 0; n <= M.USER_LAST; n++) if (!M.cart(n)) return n;
        return -1;
    };
    M.cell = (p, r, t) => p.cells[r * 16 + t];

    /* ----------------------------------------------------------- edits, REVERT, the "flash" --- */
    M.EDITS_MAX = 68608;                 /* the edit memory, 67 KB, the edits' alone (specs/files/ram.md) */
    M.unsaved = 0;
    M.editsUsed = 0;                     /* what the changes take: each changed piece once (specs/ui/saving.md) */
    M.version = 0;                       /* bumps on every change: views and the sound follow it */
    const snap = () => structuredClone(M.state);
    let flash = snap();
    /* the changes are what differs from what the flash holds, not a log (specs/ui/saving.md), counted as the
     * firmware's records: a pattern, a song, a cartridge, each sample, a GATE pattern one each; the settings,
     * the LOOPS, the MIDI mappings, DJ's setup one each (the DJ stand: U.unsavedCount's). The flash's side is
     * kept as text, made again when the flash changes (SAVE, a move) */
    let flashText = null;
    function texts(st) {
        const J = JSON.stringify, out = new Map();
        for (const k in st) {
            const v = st[k];
            if (k === "carts") for (const c of v) {
                const { samples, ...body } = c;
                out.set("cart" + c.id, J(body));
                (samples || []).forEach((s, i) => out.set("sample" + c.id + ":" + i, J(s)));
            }
            else if (k === "dj") { const { stand, ...rest } = v; out.set(k, J(rest)); }
            else if (k === "loops") out.set(k, J({ ...v, list: v.list.map(l => ({ ...l, soloed: false })) }));   /* (a solo is never saved) */
            else if (Array.isArray(v)) v.forEach((x, i) => { if (x != null) out.set(k + i, J(x)); });
            else out.set(k, J(v));
        }
        return out;
    }
    /* The edit memory's bytes (specs/files/ram.md#editing-and-saving): the pieces that differ from the flash, each
     * once, a room rounded up to 4 and 8 B of list a piece: a pattern's changed tracks and its head (LOOPS' too),
     * a cartridge's changed slots, head and samples' table, a sample's delete, every other thing whole (an
     * estimate from the records' sizes, as STATUS's) */
    const J = JSON.stringify, room = b => ((b + 3) & ~3) + 8;
    const cellBytes = c => c ? 2 + (c.note != null) + (c.inst != null) * 2 + (c.vol != null) + c.cmds.length * 2 : 0;
    /* a loop's stream (specs/files/loops.md#a-loops-stream): a note with its volume 3 B, a note off 2 B, a command 2 B
     * more, a key off alone 3 B; the payload's head 5 B */
    M.streamBytes = l => l.cells.reduce((a, c) => a + (!c ? 0 : (c.note == null ? 1 : c.note === M.OFF ? 2 : 3) + c.cmds.length * 2), 5);
    function patPieces(p, q) {                  /* p as it is against q as the flash has it (null: none) */
        if (!p) return q ? 8 : 0;
        let b = !q || p.tracks !== q.tracks || p.rows !== q.rows || p.step !== q.step || p.what !== q.what ? room(3) : 0;
        for (let t = 0; t < 16; t++) {
            let same = !!q, bytes = 1;
            for (let r = 0; r < Math.max(p.rows, q ? q.rows : 0); r++) {
                const a = r < p.rows ? p.cells[r * 16 + t] : null, z = q && r < q.rows ? q.cells[r * 16 + t] : null;
                bytes += cellBytes(a);
                if (same && J(a ?? null) !== J(z ?? null)) same = false;
            }
            if (!same && (q || bytes > 1)) b += room(bytes);
        }
        return b;
    }
    function cartPieces(c, d) {
        if (!c) return d ? 8 : 0;
        const { slots, samples, ...h } = c, { slots: s2 = [], samples: m2, ...h2 } = d || {};
        let b = !d || J(h) !== J(h2) ? room(26) : 0;
        for (let k = 0; k < Math.max(slots.length, s2.length); k++)
            if (J(slots[k] ?? null) !== J(s2[k] ?? null) && (d || slots[k])) b += room(SLOT_BYTES[c.type]);
        return b;
    }
    function piecesOf(k, st, fl) {
        if (k.startsWith("cart")) { const id = +k.slice(4); return cartPieces(st.carts.find(c => c.id === id), fl.carts.find(c => c.id === id)); }
        if (k.startsWith("sample")) return room(16);
        if (k.startsWith("patterns")) { const n = +k.slice(8); return patPieces(st.patterns[n], fl.patterns[n]); }
        if (k.startsWith("songs")) { const s = st.songs[+k.slice(5)]; return s ? room(songBytes(s)) : 8; }
        if (k.startsWith("gates")) return room(6);
        if (k === "keys") return room(songBytes(st.keys));
        if (k === "loops") {                     /* its head (the setup and the loop table), each loop's stream */
            const a = st.loops, z = fl.loops || {}, zl = z.list || [];
            const head = o => J(Object.assign({}, o, { list: (o.list || []).map(({ cells, ...h }) => h) }));
            let b = head(a) !== head(z) ? room(80 + 12 * a.list.length) : 0;
            a.list.forEach((l, i) => { if (!zl[i] || J(l.cells) !== J(zl[i].cells)) b += room(M.streamBytes(l)); });
            return b + 8 * Math.max(0, zl.length - a.list.length);
        }
        if (k === "settings") return room(130);
        if (k.startsWith("midi")) return room(4);       /* (a mapping of the MIDI map) */
        return room(J(st[k] ?? null).length >> 2);
    }
    function recount() {
        if (!flashText) flashText = texts(flash);
        const now = texts(M.state);
        let n = 0, b = 0;
        for (const [k, v] of now) if (flashText.get(k) !== v) { n++; b += piecesOf(k, M.state, flash); }
        for (const k of flashText.keys()) if (!now.has(k)) { n++; b += 8; }
        M.unsaved = n;
        M.editsUsed = b;
    }
    M.onFull = () => {};
    /* an edit (fn changes M.state; false: nothing done). Heard at once: the sound and the views follow M.version.
     * One that would take the edit memory past its size is not made (MEMORY FOR EDITS IS FULL); one that brings a
     * thing back as the flash holds it leaves nothing unsaved by it */
    M.edit = function (fn) {
        const before = snap();
        if (fn() === false) return false;
        recount();
        if (M.editsUsed > M.EDITS_MAX) {
            restore(before, "edit");
            recount();
            M.onFull();
            return false;
        }
        M.version++;
        return true;
    };
    /* a take: its notes are edits as they come (each heard at once); its end only counts them again */
    M.begin = () => () => { recount(); M.version++; };
    /* after a dropped edit session: what is unsaved counted again */
    M.settle = function () { recount(); };
    /* (onRestore: what follows the state, the settings' effects, applied again: the UI's; given the kind of what came
     * back, none: everything, as at a power-on or REVERT ALL) */
    function restore(s, kind) {
        for (const k in s) M.state[k] = s[k];
        /* (a user area kept before a setting existed reads its default: SECOND CORE ON) */
        if (M.state.settings && M.state.settings["SECOND CORE"] === undefined) M.state.settings["SECOND CORE"] = "ON";
        if (M.state.settings && !M.state.settings["KEYS MIX"]) M.state.settings["KEYS MIX"] = { ...M.KEYS_MIX_DEF };
        if (M.state.settings && !M.QUANTIZES.includes(M.state.settings.QUANTIZE)) M.state.settings.QUANTIZE = "ROW";   /* (one from before QUANTIZE: ROW) */
        if (M.onRestore) M.onRestore(kind);
    }
    /* SAVE writes a changed thing into the user area: a built-in changed is no longer the firmware's own (brown),
     * one as the firmware has it again (BACK TO BUILT-IN) is */
    function fwsSaved() {
        for (const p of M.state.patterns) if (p && p.builtin) { if (M.builtinChanged("pattern", p)) delete p.rom; else p.rom = true; }
        for (const c of M.state.carts) if (c.builtin) { if (M.builtinChanged("cart", c)) delete c.rom; else c.rom = true; }
    }
    /* LOOPS' solos are RAM's alone (specs/files/loops.md): never saved, none after a power-on, kept through a REVERT */
    const unsoloed = st => { for (const l of st.loops.list) l.soloed = false; return st; };
    const solosKept = (to, from) => { to.list.forEach((l, i) => { l.soloed = !!(from.list[i] && from.list[i].soloed); }); return to; };
    M.save = function () {
        fwsSaved();
        flash = unsoloed(snap());
        flashText = null;
        M.unsaved = 0;
        M.editsUsed = 0;
        keep();
    };

    /* ------------------------------------------------- the user area kept in the browser --- */
    /* What SAVE writes is kept in this browser (IndexedDB, as the emulator keeps its flash): a power cycle (a reload)
     * starts from it; the changes not saved are this tab's alone, as the FM-1's RAM (specs/panel.md#the-page). A
     * store: { get, put, del }, each a promise; none where the browser keeps nothing (then a new user area each
     * time) */
    function idbStore() {
        if (typeof indexedDB === "undefined") return null;
        const db = () => new Promise((resolve, reject) => {
            const r = indexedDB.open("ptfm-playground", 1);
            r.onupgradeneeded = () => r.result.createObjectStore("flash");
            r.onsuccess = () => resolve(r.result);
            r.onerror = () => reject(r.error);
        });
        const tx = (mode, fn) => db().then(d => new Promise(resolve => {
            const t = d.transaction("flash", mode), q = fn(t.objectStore("flash"));
            t.oncomplete = () => resolve(q && q.result);
            t.onerror = t.onabort = () => resolve(null);
        })).catch(() => null);
        return { get: () => tx("readonly", s => s.get("user")), put: v => tx("readwrite", s => s.put(v, "user")),
                 del: () => tx("readwrite", s => s.delete("user")) };
    }
    let store = idbStore(), keeping = Promise.resolve();
    M.useStore = s => { store = s; };
    function keep() {                                   /* (in turn: the last SAVE's kept last) */
        const v = structuredClone(flash);
        keeping = keeping.then(() => store && store.put(v)).catch(e => console.warn("PT/FM playground: not kept:", e));
    }
    M.kept = () => keeping;                             /* (a promise: what SAVE wrote is kept) */
    function load(s) {
        if (!loopsOk(s.loops)) s = Object.assign({}, s, { loops: M.newLoops() });
        s = unsoloed(structuredClone(s));
        restore(structuredClone(s));
        flash = structuredClone(s);
        flashText = null;
        recount();
        M.version++;
    }
    /* power-on: the user area as kept, else a new one; resolves whether one was kept */
    M.startUp = async function () {
        let s = null;
        try { s = store ? await store.get() : null; } catch (_) { s = null; }
        load(s && s.carts && s.songs ? s : cleanState());
        return !!(s && s.carts && s.songs);
    };
    /* DEMO DISK: the demo disk written into the user area, in place of what was there (refused with changes not
     * saved: SAVE or REVERT first, as on the FM-1) */
    M.demoDisk = function () {
        if (M.unsaved) return false;
        load(DEMO);
        keep();
        return true;
    };
    /* ERASE DATA: nothing kept any more */
    M.eraseKept = () => { keeping = keeping.then(() => store && store.del()); return keeping; };
    /* REVERT ALL: every change dropped, all as the flash holds it */
    M.revert = function () {
        const s = structuredClone(flash);
        solosKept(s.loops, M.state.loops);
        restore(s);
        M.unsaved = 0;
        M.editsUsed = 0;
        M.version++;
    };
    /* a sampler's CLONE or MOVE (specs/ui/screens/instruments.md) writes the flash at once, so it is made in what
     * is held and in what the flash holds alike. fn(state) gets either root */
    M.commit = function (fn) {
        fn(M.state);
        fn(flash);
        flashText = null;
        recount();
        M.version++;
        keep();
    };
    /* One thing, in the state held or as the flash holds it (st): kind "pattern" (n), "song" (its index), "cart"
     * (id), "slot" ([cart id, slot]), "sample" ([cart id, index]), "loops", "settings" (with the GATE patterns:
     * one record), "midi" (the MIDI map), "fx" (an FX setup: a song's index, "loops", "dj" or "keys") */
    const fxOwner = (st, w) => w === "loops" ? st.loops : w === "dj" ? st.dj : w === "keys" ? st.keys : st.songs[w];
    const PICK = {
        fx: (st, w) => (fxOwner(st, w) || {}).fx,
        pattern: (st, n) => st.patterns[n],
        song: (st, i) => st.songs[i],
        cart: (st, id) => st.carts.find(c => c.id === id),
        slot: (st, k) => (st.carts.find(c => c.id === k[0]) || { slots: [] }).slots[k[1]],
        sample: (st, k) => (st.carts.find(c => c.id === k[0]) || { samples: [] }).samples[k[1]],
        loops: st => st.loops,
        settings: st => [st.settings, st.gates],
        midi: st => st.midi,
    };
    /* whether a thing has unsaved changes: what is held against what the flash holds */
    /* (an FX setup with none saved: a new song's, the loops' or DJ's first, as REVERT brings it) */
    const savedOf = (kind, key) => { const v = PICK[kind](flash, key); return v == null && kind === "fx" ? (key === "dj" ? djFx() : fxDefaults()) : v; };
    M.unsavedThing = (kind, key) => J(PICK[kind](M.state, key) ?? null) !== J(savedOf(kind, key) ?? null);
    /* DJ's setup changed, its stand aside (the UI counts the stand: U.unsavedCount) */
    M.djSetupUnsaved = () => { const { stand: a, ...x } = M.state.dj || {}, { stand: b, ...y } = flash.dj || {}; return J(x) !== J(y); };
    /* a built-in thing as the firmware has it, shown brown (specs/ui/saving.md#fws): saved so and not changed since
     * (the firmware's: no edit of it and no copy in the user area), kind "cart" or "pattern"; worked out once a
     * version, the lists drawing it every frame */
    const asBuilt = { v: -1, f: null, m: new Map() };
    M.asBuilt = (kind, thing) => {
        if (!thing || !thing.rom) return false;
        if (asBuilt.v !== M.version || asBuilt.f !== flash) { asBuilt.v = M.version; asBuilt.f = flash; asBuilt.m.clear(); }
        const key = kind === "cart" ? thing.id : thing.n, k = kind + key;
        if (!asBuilt.m.has(k)) asBuilt.m.set(k, !M.unsavedThing(kind, key));
        return asBuilt.m.get(k);
    };
    M.savedThing = (kind, key) => { const v = savedOf(kind, key); return v == null ? null : structuredClone(v); };
    /* REVERT (specs/ui/saving.md#revert): one thing as the flash holds it, nothing else's changes; what plays hears
     * it at once (the state's objects are replaced: whoever keeps one looks it up again) */
    M.revertThing = function (kind, key) {
        const S = M.state, was = M.savedThing(kind, key);
        if (kind === "pattern") S.patterns[key] = was;
        else if (kind === "song") { if (was) S.songs[key] = was; else S.songs.splice(key, 1); }
        else if (kind === "cart") {
            const i = S.carts.findIndex(c => c.id === key);
            if (was && i >= 0) S.carts[i] = was;
            else if (was) { S.carts.push(was); S.carts.sort((a, b) => a.id - b.id); }
            else if (i >= 0) S.carts.splice(i, 1);
        } else if (kind === "slot") { const c = M.cart(key[0]); if (c) c.slots[key[1]] = was; }
        else if (kind === "loops") S.loops = solosKept(was, S.loops);
        else if (kind === "settings") { S.settings = was[0]; S.gates = was[1]; }
        else if (kind === "midi") S.midi = was;
        else if (kind === "fx") { const o = fxOwner(S, key); if (o) o.fx = was; }     /* (savedOf: never none) */
        recount();
        if (M.onRestore) M.onRestore(kind);
        M.version++;
    };

    /* storage, as STATUS shows it (specs/files/store.md, Where things go): the user area, the fast regions
     * (412 KB), with compaction's 2 blocks and the room kept for the EDITOR's own pattern (8 blocks: 40 KB,
     * specs/files/store.md#the-room-for-the-editors-pattern) counted as used. Nothing of the user's is in FWS.
     * What a thing takes is estimated from its records (3.4 B a cell, the slots' sizes, PTR at 41 % for 8-bit
     * audio); the built-ins as the firmware has them are the firmware's, not counted */
    M.FLASH = { user: 412 * 1024, reserve: 40 * 1024 };
    const SLOT_BYTES = { DX7: 112, JUNO: 28, TB303: 24, SAMPLER: 40, FUSION: 16, CHIP: 28 };
    const patBytes = p => p.cells.reduce((a, c) => a + (c ? 2 + (c.note != null) + (c.inst != null) * 2 + (c.vol != null) + c.cmds.length * 2 : 0), 21);
    const smpBytes = s => s.deleted ? 0 : 16 + Math.round(s.length * .41);
    const songBytes = s => 16 + 21 + s.order.length + 1 + 16 * 6 + 44 + 8;
    const cartBytes = c => 26 + c.slots.filter(Boolean).length * SLOT_BYTES[c.type] + (c.samples ? c.samples.length * 16 : 0);
    M.bytes = { pattern: patBytes, sample: smpBytes, song: songBytes, cart: cartBytes };
    M.kb = b => b < 1024 ? Math.max(0, Math.round(b)) + " B" : (b / 1024).toFixed(b < 10240 ? 1 : 0) + "K";   /* `1.3K` */
    /* a song's own patterns (SONGS' DELETE): in its order, the user's (under F0), no other song's or FUSION slot's;
     * shared: how many of its patterns others use too */
    M.songOwn = function (s) {
        const items = [], seen = new Set();
        let shared = 0;
        for (const n of s.order) {
            if (n >= 0xF0 || seen.has(n)) continue;          /* (the built-in ones never) */
            seen.add(n);
            if (M.patternUses(n) > s.order.filter(o => o === n).length) { shared++; continue; }
            const p = M.state.patterns[n];
            if (p) items.push(p);                            /* (not there: nothing to delete) */
        }
        return { items, bytes: items.reduce((a, p) => a + patBytes(p), 0), shared };
    };
    M.storage = function () {
        let patt = 0, inst = 0, smp = 0, song = 0;
        /* (a built-in as the firmware has it is the firmware's; changed and saved, its copy is in the user area) */
        for (const p of M.state.patterns) if (p && !(p.builtin && p.rom)) patt += patBytes(p);
        for (const c of M.state.carts) {
            if (c.builtin && c.rom) continue;
            inst += cartBytes(c);
            if (c.samples) for (const s of c.samples) smp += smpBytes(s);
        }
        for (const s of M.state.songs) song += songBytes(s);
        /* (the keys' song as shipped is the firmware's; changed, a copy in the user area) */
        if (J(M.state.keys) !== J(M.newKeys())) song += songBytes(M.state.keys);
        /* LOOPS' record once it holds loops; the rest (the settings, DJ's stand, the MIDI map) 2 KB */
        const lp = M.state.loops, loops = lp && lp.list.length ? 80 + lp.list.reduce((a, l) => a + 12 + M.streamBytes(l), 0) : 0, other = 2048;
        const F = M.FLASH, userUsed = patt + inst + smp + song + loops + other + F.reserve;
        return { patt, inst, smp, song, loops, other, userUsed, userFree: F.user - userUsed, used: userUsed, free: F.user - userUsed };
    };
    /* what CLEAN UP can delete, each with the bytes it frees: patterns no song position or FUSION slot
     * plays; samples no instrument of their cartridge uses; instruments no cell plays, nor a loop */
    M.cleanable = function () {
        /* (never the firmware's: the built-in instruments and patterns stay) */
        const patterns = M.state.patterns.filter(p => p && !p.builtin && !M.patternUses(p.n));
        const samples = [], instruments = [], played = new Set();
        for (const c of M.state.carts) if (c.samples) c.samples.forEach((s, k) => {
            if (!s.deleted && !c.slots.some(sl => sl && sl.p.sample === k)) samples.push({ c, k });
        });
        const cellsOf = p => p.cells.forEach(cl => { if (cl && cl.inst != null) played.add(cl.inst); });
        M.state.patterns.forEach(p => p && cellsOf(p));
        M.state.loops.list.forEach(l => played.add(l.inst));
        for (const c of M.state.carts) if (!c.builtin) c.slots.forEach((sl, k) => { if (sl && !played.has(c.id << 8 | k)) instruments.push({ c, k }); });
        return {
            patterns: { items: patterns, bytes: patterns.reduce((a, p) => a + patBytes(p), 0) },
            samples: { items: samples, bytes: samples.reduce((a, x) => a + smpBytes(x.c.samples[x.k]), 0) },
            instruments: { items: instruments, bytes: instruments.reduce((a, x) => a + SLOT_BYTES[x.c.type], 0) },
        };
    };
    return M;
})();

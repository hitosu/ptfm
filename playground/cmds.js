/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground: the commands a cell holds (specs/files/commands.md), as the screens show
 * them: in 3 characters in the grid, in words in the note editor, and as the list K2 turns through in
 * the EDITOR. A command is {c, v}: its code and its value, a byte each.
 *
 * The list K2 turns through ("effects") has an entry for each tracker effect, each E and X
 * sub-command, each of XM's volume-column effects and PT/FM's own; the instrument's parameters, the
 * mixer and the FX are one entry each, their code picked with K3. An entry's value is set as its
 * digits: K3 the first, K4 the second (one digit: K4 alone); in the three groups K3 picks the
 * parameter and K4 sets its value. */
PTFM.cmds = (function () {
    "use strict";
    const M = PTFM.model;
    const H = M.hex;
    /* values in hex, counts in decimal (specs/ui/frame.md, Numbers); a signed value keeps its sign */
    const sgn = M.sgn;
    const H1 = v => H(v, 1);

    /* the instrument's parameters, a table an engine (commands.md): code -> [name, max, words(v)] */
    const PCT = 127;
    const list = (...names) => v => names[Math.min(v, names.length - 1)];
    const PARAMS = { TB303: {}, JUNO: {}, DX7: {}, SAMPLER: {} };
    ["WAVE", "CUTOFF", "RESONANCE", "ENV MOD", "DECAY", "ACCENT"].forEach((n, i) => { PARAMS.TB303[0x40 + i] = [n, PCT]; });
    ["LFO RATE", "LFO DELAY", "DCO LFO", "DCO PWM", "DCO NOISE", "VCF FREQ", "VCF RES", "VCF ENV", "VCF LFO", "VCF KYBD",
     "VCA LEVEL", "ENV A", "ENV D", "ENV S", "ENV R", "DCO SUB"].forEach((n, i) => { PARAMS.JUNO[0x40 + i] = [n, PCT]; });
    Object.assign(PARAMS.JUNO, {
        0x50: ["RANGE", 2, list("16'", "8'", "4'")], 0x51: ["PULSE", 1, list("OFF", "ON")], 0x52: ["SAW", 1, list("OFF", "ON")],
        0x53: ["CHORUS", 2, list("OFF", "I", "II")], 0x54: ["PWM SOURCE", 1, list("LFO", "MAN")],
        0x55: ["ENV POLARITY", 1, list("+", "-")], 0x56: ["VCA MODE", 1, list("ENV", "GATE")], 0x57: ["HPF", 3] });
    for (let op = 1; op <= 6; op++) {
        const b = 0x40 + 8 * (op - 1), o = "OP" + op + " ";
        PARAMS.DX7[b] = [o + "LEVEL", 99];
        PARAMS.DX7[b + 1] = [o + "COARSE", 31];
        PARAMS.DX7[b + 2] = [o + "FINE", 99];
        PARAMS.DX7[b + 3] = [o + "DETUNE", 14, v => sgn(v - 7)];
        for (let r = 0; r < 4; r++) PARAMS.DX7[b + 4 + r] = [o + "R" + (r + 1), 99];
    }
    Object.assign(PARAMS.DX7, {
        0x70: ["ALGORITHM", 31], 0x71: ["FEEDBACK", 7], 0x72: ["OSC KEY SYNC", 1, list("OFF", "ON")],
        0x73: ["TRANSPOSE", 48, v => sgn(v - 24, 2)], 0x74: ["LFO SPEED", 99], 0x75: ["LFO DELAY", 99], 0x76: ["LFO PITCH", 99],
        0x77: ["LFO AMP", 99], 0x78: ["LFO WAVE", 5, list("TRI", "SAW DOWN", "SAW UP", "SQUARE", "SINE", "S/H")],
        0x79: ["LFO KEY SYNC", 1, list("OFF", "ON")], 0x7A: ["PITCH MOD SENS", 7] });
    Object.assign(PARAMS.SAMPLER, {
        0x40: ["START", 255], 0x41: ["END", 255], 0x42: ["LOOP", 2, list("OFF", "FORWARD", "PING-PONG")],
        0x43: ["LOOP START", 255], 0x44: ["LOOP END", 255], 0x45: ["TUNE", 255, v => sgn(v - 128, 2)] });
    for (let r = 0; r < 4; r++) { PARAMS.SAMPLER[0x48 + r] = ["ENV R" + (r + 1), 99]; PARAMS.SAMPLER[0x4C + r] = ["ENV L" + (r + 1), 99]; }
    /* CHIP: the values as the slot stores them; tables by number and name, SPEEDs X1-X4, the vibrato's frames and cents */
    const XN = v => "X" + (v + 1), OFFON = list("OFF", "ON"), CT = () => PTFM.chipTables;   /* (dsp-chip-tables.js: loaded after) */
    PARAMS.CHIP = {
        0x40: ["OSC", 4, list("PULSE", "TRIANGLE", "WAVE", "BUZZ", "NOISE")], 0x41: ["DUTY", 3, list("12.5%", "25%", "50%", "75%")],
        0x42: ["BUZZ SHAPE", 2, list("SAW DN", "SAW UP", "TRI")], 0x43: ["BUZZ STEPS", 1, list("16", "32")], 0x44: ["BUZZ GATE", 1, OFFON],
        0x48: ["NOISE", 1, OFFON], 0x49: ["NOISE MODE", 1, list("LONG", "SHORT")], 0x4A: ["NOISE RATE", 15], 0x4B: ["NOISE KEY", 1, OFFON],
        0x4C: ["CLOCK", 6, list("BRK", "8BIT", "PSG 1.77", "PSG 1.00", "PSG 1.79", "16BIT", "EXACT")],
        0x4D: ["CURVE", 3, list("BRK", "8BIT", "PSG", "16BIT")], 0x4E: ["FILTER", 3, list("BRK", "8BIT", "PSG", "NONE")],
        0x4F: ["FRAMES", 1, list("60 HZ", "50 HZ")], 0x50: ["SWEEP", 2, list("OFF", "DOWN", "UP")], 0x51: ["SWEEP SHIFT", 7],
        0x52: ["SWEEP PACE", 7], 0x54: ["VOL", 15], 0x55: ["ENV", 3, list("NONE", "DOWN", "UP", "PSG")], 0x56: ["ENV STEP", 7],
        0x57: ["RELEASE", 15], 0x58: ["PSG SHAPE", 15], 0x59: ["PSG PERIOD HI", 255], 0x5A: ["PSG PERIOD LO", 255],
        0x5B: ["WAVE", 15, v => H(v) + " " + CT().WAVE_NAMES[v]],
        0x6D: ["VIB DELAY", 31, v => 2 * v + " FRAMES"], 0x6E: ["VIB SPEED", 3, XN], 0x6F: ["VIB DEPTH", 31, v => 2 * v + " CENTS"] };
    ["vol", "arp", "pitch", "duty", "noise"].forEach((k, i) => {
        const n = () => CT().TAB_NAMES[k];
        PARAMS.CHIP[0x60 + i] = [k.toUpperCase() + " TABLE", 63, v => !v ? "--" : v <= n().length ? H(v) + " " + n()[v - 1] : H(v)];
        PARAMS.CHIP[0x68 + i] = [k.toUpperCase() + " SPEED", 3, XN];
    });

    const MIXER = { 0x80: "LEVEL", 0x81: "CHORUS SEND", 0x82: "DELAY SEND", 0x83: "REVERB SEND", 0x84: "DUCK AMOUNT" };
    /* the FX's codes (commands.md): each knob of each effect, then FX ON / OFF */
    const FX_NAMES = ["GATE", "FILTER", "DRIVE", "LO-FI", "CHORUS", "DELAY", "REVERB", "LIMITER", "DUCK", "METRONOME"];
    const FX = {}, FX_LIST = [];                  /* (the list in the effects' order: FILTER's envelope after RESO) */
    for (const n of FX_NAMES.slice(0, 9)) M.FX[n].knobs.forEach((k, i) => { const c = M.fxCode(n, i); FX[c] = [n, i]; FX_LIST.push(c); });
    FX[0xAE] = ["ON"];
    FX[0xAF] = ["OFF"];
    FX_LIST.push(0xAE, 0xAF);
    /* an FX command's value is the knob's step from the bottom of its range */
    const fxKnob = (n, i, v) => { const k = M.FX[n].knobs[i]; return k.list ? Math.min(v, k.list.length - 1) : Math.min(k.max, k.min + v * (k.step || 1)); };
    const fxSteps = (n, i) => { const k = M.FX[n].knobs[i]; return k.list ? k.list.length - 1 : (k.max - k.min) / (k.step || 1); };

    /* the entries K2 turns through, in the order the grid's letters run */
    const E = [];
    const add = (key, code, sub, name, digits, words) => { const e = { key, code, sub, name, digits, words }; E.push(e); return e; };
    const xy = v => [H1(v >> 4), H1(v & 15)];
    add(null, null, null, "NO COMMAND", 0, () => "NO COMMAND");
    add("0", 0x00, null, "ARPEGGIO", 2, v => "ARPEGGIO 0 " + xy(v).join(" "));
    add("1", 0x01, null, "SLIDE UP", 2, v => "SLIDE UP " + H(v));
    add("2", 0x02, null, "SLIDE DOWN", 2, v => "SLIDE DOWN " + H(v));
    add("3", 0x03, null, "SLIDE TO NOTE", 2, v => "SLIDE TO NOTE " + H(v));
    add("4", 0x04, null, "VIBRATO", 2, v => "VIBRATO " + xy(v).join(" "));
    add("5", 0x05, null, "SLIDE TO NOTE + VOL", 2, v => "TO NOTE, VOL " + volSlide(v));
    add("6", 0x06, null, "VIBRATO + VOL", 2, v => "VIBRATO, VOL " + volSlide(v));
    add("7", 0x07, null, "TREMOLO", 2, v => "TREMOLO " + xy(v).join(" "));
    add("8", 0x08, null, "PAN", 2, v => "PAN " + panWords(v));
    add("9", 0x09, null, "SAMPLE OFFSET", 2, v => "START AT " + H(v) + "00");
    add("A", 0x0A, null, "VOLUME SLIDE", 2, v => "VOLUME " + volSlide(v));
    add("B", 0x0B, null, "JUMP", 2, v => "JUMP TO POS " + M.hex(v));
    add("C", 0x0C, null, "VOLUME", 2, v => "VOLUME " + H(Math.min(v, 64)));
    add("D", 0x0D, null, "BREAK", 2, v => "BREAK TO ROW " + H(v));
    const ESUB = { 1: "FINE SLIDE UP", 2: "FINE SLIDE DOWN", 3: "GLISSANDO", 4: "VIBRATO WAVE", 5: "FINETUNE", 6: "LOOP",
                   7: "TREMOLO WAVE", 9: "RETRIGGER", 0xA: "FINE VOLUME UP", 0xB: "FINE VOLUME DOWN", 0xC: "NOTE CUT",
                   0xD: "NOTE DELAY", 0xE: "PATTERN DELAY" };
    const WAVES = ["SINE", "SAW", "SQUARE", "RANDOM"];
    for (const s in ESUB) {
        const n = ESUB[s], sub = +s;
        add("E" + H(sub, 1), 0x0E, sub, n, 1, v => {
            const y = v & 15;
            if (sub === 3) return "GLISSANDO " + (y ? "ON" : "OFF");
            if (sub === 4 || sub === 7) return n + " " + (WAVES[y & 3] || H1(y));
            if (sub === 5) return "FINETUNE " + sgn(y - 8);
            if (sub === 6) return y ? "LOOP " + y + " TIMES" : "LOOP FROM HERE";
            if (sub === 9) return "RETRIGGER EVERY " + y;
            if (sub === 0xC) return "NOTE CUT AT TICK " + H1(y);
            if (sub === 0xD) return "NOTE DELAY " + y + " TICKS";
            if (sub === 0xE) return "ROW DELAY " + y + " ROWS";
            return n + " " + H1(y);
        });
    }
    add("F", 0x0F, null, "TICKS / TEMPO", 2, v => !v ? "THE SONG TEMPO" : v < 0x20 ? "TICKS A ROW " + v : "TEMPO " + H(v));
    add("G", 0x10, null, "GLOBAL VOLUME", 2, v => "GLOBAL VOLUME " + H(Math.min(v, 64)));
    add("H", 0x11, null, "GLOBAL VOL SLIDE", 2, v => "GLOBAL VOL " + volSlide(v));
    add("K", 0x14, null, "KEY OFF", 2, v => "NOTE OFF AT TICK " + H(v));
    add("P", 0x19, null, "PAN SLIDE", 2, v => "PAN " + (v >> 4 ? "RIGHT " + H1(v >> 4) : "LEFT " + H1(v & 15)));
    add("R", 0x1B, null, "MULTI RETRIGGER", 2, v => "RETRIGGER " + (v & 15) + ", VOL " + H1(v >> 4));
    add("T", 0x1D, null, "TREMOR", 2, v => "TREMOR ON " + (v >> 4) + " OFF " + (v & 15));
    add("U", 0x1E, null, "FINE VIBRATO", 2, v => "FINE VIBRATO " + xy(v).join(" "));
    add("X1", 0x21, 1, "EXTRA FINE UP", 1, v => "EXTRA FINE UP " + H1(v & 15));
    add("X2", 0x21, 2, "EXTRA FINE DOWN", 1, v => "EXTRA FINE DOWN " + H1(v & 15));
    add("Y", 0x22, null, "PANBRELLO", 2, v => "PANBRELLO " + xy(v).join(" "));
    const VCOL = [["-", "VOL SLIDE DOWN"], ["+", "VOL SLIDE UP"], ["D", "FINE VOL DOWN"], ["U", "FINE VOL UP"],
                  ["S", "VIBRATO SPEED"], ["V", "VIBRATO DEPTH"], ["P", "PAN"], ["<", "PAN SLIDE LEFT"],
                  [">", "PAN SLIDE RIGHT"], ["M", "SLIDE TO NOTE"]];
    VCOL.forEach(([s, n], i) => add("~" + s, 0x24 + i, null, n, 1, v => n + " " + (s === "P" ? H(v * 17) : H1(v))));
    add("ACC", 0xC0, null, "ACCENT", 0, () => "ACCENT");
    add("SLD", 0xC1, null, "SLIDE", 0, () => "SLIDE");
    add("A+S", 0xC2, null, "ACCENT + SLIDE", 0, () => "ACCENT + SLIDE");
    add("LEG", 0xD3, null, "LEGATO", 0, () => "LEGATO");   /* (commands.md#legato) */
    /* the punch-ins (commands.md, Punch-ins), in the keys' order: the value is the rows its key is
     * held, 01-FF (00 is never written) */
    const PUNCHES = ["LOOP 1/4", "LOOP 1/8", "LOOP 1/16", "LOOP 1/32", "STUTTER 1/16T", "BIT CRUSH", "GATE 1/16",
                     "GATE 1/32", "LOW-PASS SWEEP", "HIGH-PASS SWEEP", "ECHO THROW", "REVERB SWELL", "REVERSE",
                     "HALF SPEED", "TAPE STOP", "VINYL STOP"];
    PUNCHES.forEach((n, k) => { add("!", 0xC3 + k, null, n, 2, v => n + " " + v + (v === 1 ? " ROW" : " ROWS")).min = 1; });
    add("*", "PARAM", null, "PARAMETER", -1, null);
    add("=", "MIXER", null, "MIXER", -1, null);
    add("#", "FX", null, "FX", -1, null);

    function volSlide(v) { const x = H1(v >> 4), y = H1(v & 15); return x === "F" && y !== "0" ? "FINE -" + y : y === "F" && x !== "0" ? "FINE +" + x : x !== "0" ? "+" + x : "-" + y; }
    function panWords(v) { return v === 128 ? "MIDDLE" : v < 128 ? "LEFT " + H(128 - v) : "RIGHT " + H(v - 128); }

    const C = {};
    C.PARAMS = PARAMS;
    C.MIXER = MIXER;
    C.FX = FX;
    C.FX_NAMES = FX_NAMES;
    C.PUNCHES = PUNCHES;
    C.ENTRIES = E;
    C.fxKnob = fxKnob;
    C.fxSteps = fxSteps;
    C.group = c => c >= 0x40 && c <= 0x7F ? "PARAM" : c >= 0x80 && c <= 0x8F ? "MIXER" : c >= 0x90 && c <= 0xBF ? "FX" : null;
    /* the entry a command is */
    C.entry = function (x) {
        if (!x) return 0;
        const g = C.group(x.c);
        return E.findIndex(e => g ? e.code === g : e.code === x.c && (e.sub == null || e.sub === x.v >> 4));
    };
    /* global commands (commands.md, Commands together): they act from any track, and are ignored in
     * FUSION rows */
    C.global = x => [0x0B, 0x0D, 0x0F, 0x10, 0x11].includes(x.c) || (x.c === 0x0E && [6, 0xE].includes(x.v >> 4)) || C.group(x.c) === "FX" ||
                    C.punch(x) >= 0;
    /* the punch-in a command runs, 0-15 (its key), or -1 */
    C.punch = x => x.c >= 0xC3 && x.c <= 0xD2 ? x.c - 0xC3 : -1;
    /* the grid's 3 characters */
    C.text = function (x) {
        if (!x) return "...";
        const g = C.group(x.c);
        if (g) return (g === "PARAM" ? "*" : g === "MIXER" ? "=" : "#") + H(x.v);
        if (x.c >= 0x24 && x.c <= 0x2D) return "~" + VCOL[x.c - 0x24][0] + H(x.v & 15, 1);
        if (x.c === 0xC0) return "ACC";
        if (x.c === 0xC1) return "SLD";
        if (x.c === 0xC2) return "A+S";
        if (x.c === 0xD3) return "LEG";
        if (C.punch(x) >= 0) return "!" + H(x.v);
        return x.c.toString(36).toUpperCase() + H(x.v);
    };
    /* the parameter a code names, for the instrument's engine: [name, max, words] or null */
    C.param = (engine, c) => (PARAMS[engine] || {})[c] || null;
    /* in words, as the note editor says it; engine: the engine of the track's instrument */
    C.words = function (x, engine) {
        if (!x) return "NO COMMAND";
        const g = C.group(x.c);
        if (g === "PARAM") {
            const p = C.param(engine, x.c);
            if (!p) return "PARAMETER " + H(x.c) + " " + H(x.v);
            const v = Math.min(x.v, p[1]);
            return p[0] + " " + (p[2] ? p[2](v) : H(v));
        }
        if (g === "MIXER") return (MIXER[x.c] || "MIXER " + H(x.c)) + " " + H(x.v);
        if (g === "FX") {
            const f = FX[x.c];
            if (!f) return "FX " + H(x.c) + " " + H(x.v);
            if (f.length === 1) return FX_NAMES[x.v] ? FX_NAMES[x.v] + " " + f[0] : "FX " + f[0];
            return f[0] + " " + M.FX[f[0]].knobs[f[1]].n + " " + M.fxValue(f[0], f[1], fxKnob(f[0], f[1], x.v));
        }
        const e = E[C.entry(x)];
        return e && e.words ? e.words(x.v) : C.text(x);
    };
    /* a command's name in at most 9 characters, for the 1-track view: its value is in its 3 characters */
    const SHORT = {
        "SLIDE DOWN": "SLIDE DN", "SLIDE TO NOTE": "TO NOTE", "SLIDE TO NOTE + VOL": "NOTE+VOL", "VIBRATO + VOL": "VIB+VOL",
        "SAMPLE OFFSET": "OFFSET", "VOLUME SLIDE": "VOL SLIDE", "FINE SLIDE UP": "FINE UP", "FINE SLIDE DOWN": "FINE DN",
        "VIBRATO WAVE": "VIB WAVE", "TREMOLO WAVE": "TREM WAVE", "FINE VOLUME UP": "FINE VOL+", "FINE VOLUME DOWN": "FINE VOL-",
        "NOTE DELAY": "NOTE DLY", "PATTERN DELAY": "ROW DELAY", "GLOBAL VOLUME": "GLOB VOL", "GLOBAL VOL SLIDE": "GLOB SLD",
        "MULTI RETRIGGER": "RETRIG+V", "FINE VIBRATO": "FINE VIB", "EXTRA FINE UP": "XFINE UP", "EXTRA FINE DOWN": "XFINE DN",
        "VOL SLIDE DOWN": "VOL DOWN", "VOL SLIDE UP": "VOL UP", "FINE VOL DOWN": "FINE VOL-", "FINE VOL UP": "FINE VOL+",
        "VIBRATO SPEED": "VIB SPEED", "VIBRATO DEPTH": "VIB DEPTH", "PAN SLIDE LEFT": "PAN LEFT", "PAN SLIDE RIGHT": "PAN RIGHT",
        "ACCENT + SLIDE": "ACC+SLIDE", "STUTTER 1/16T": "STUTTER", "HALF SPEED": "HALF SPD", "LOW-PASS SWEEP": "LP SWEEP",
        "HIGH-PASS SWEEP": "HP SWEEP", "ECHO THROW": "ECHO THRW", "REVERB SWELL": "REV SWELL", "VINYL STOP": "VINYL STP",
        /* the instruments' parameters */
        "OSC KEY SYNC": "OSC SYNC", "LFO KEY SYNC": "LFO SYNC", "PITCH MOD SENS": "PITCH MOD", "PWM SOURCE": "PWM SRC",
        "ENV POLARITY": "ENV POL", "LOOP START": "LOOP STRT",
        "BUZZ SHAPE": "BUZ SHAPE", "BUZZ STEPS": "BUZ STEPS", "NOISE MODE": "NSE MODE", "NOISE RATE": "NSE RATE",
        "SWEEP SHIFT": "SWP SHIFT", "SWEEP PACE": "SWP PACE", "PSG PERIOD HI": "PERIOD HI", "PSG PERIOD LO": "PERIOD LO",
        "PITCH TABLE": "PIT TABLE", "DUTY TABLE": "DTY TABLE", "NOISE TABLE": "NSE TABLE", "PITCH SPEED": "PIT SPEED",
        "DUTY SPEED": "DTY SPEED", "NOISE SPEED": "NSE SPEED",
    };
    const MIXER_SHORT = { 0x80: "LEVEL", 0x81: "CHO SEND", 0x82: "DLY SEND", 0x83: "REV SEND", 0x84: "DUCK" };
    const FX_SHORT = { FILTER: "FILT", DRIVE: "DRV", "LO-FI": "LOFI", CHORUS: "CHO", DELAY: "DLY", REVERB: "REV", LIMITER: "LIM",
                       METRONOME: "METRO" };
    const KNOB_SHORT = { DEPTH: "DPTH", SMOOTH: "SMTH", PATTERN: "PATT", FEEDBACK: "FDBK", "PING-PONG": "PING", "PRE-DELAY": "PRE",
                         CEILING: "CEIL", RELEASE: "REL", SOURCE: "SRC", ATTACK: "ATK", DECAY: "DEC", SUSTAIN: "SUS" };
    C.short = function (x, engine) {
        const g = C.group(x.c), fx = n => FX_SHORT[n] || n;
        let s;
        if (g === "PARAM") {
            const p = C.param(engine, x.c);
            s = !p ? "PARAM " + H(x.c) : SHORT[p[0]] || p[0].replace("COARSE", "CRS").replace("DETUNE", "DTUNE");
        } else if (g === "MIXER") s = MIXER_SHORT[x.c] || "MIXER " + H(x.c);
        else if (g === "FX") {
            const f = FX[x.c];
            s = !f ? "FX " + H(x.c) : f.length === 1 ? fx(FX_NAMES[x.v] || "FX") + " " + f[0]
              : fx(f[0]) + " " + (KNOB_SHORT[M.FX[f[0]].knobs[f[1]].n] || M.FX[f[0]].knobs[f[1]].n);
        } else if (x.c === 0x0F) s = !x.v ? "SONG BPM" : x.v < 0x20 ? "TICKS" : "TEMPO";
        else { const e = E[C.entry(x)]; s = e && e.code != null ? SHORT[e.name] || e.name : C.text(x); }
        return s.slice(0, 9);
    };
    /* the codes K3 turns through in a group, for the track's instrument's engine */
    C.codes = function (group, engine) {
        if (group === "PARAM") return Object.keys(PARAMS[engine] || {}).map(Number);
        if (group === "MIXER") return Object.keys(MIXER).map(Number);
        return FX_LIST.slice();
    };
    C.codeName = function (group, engine, c) {
        if (group === "PARAM") { const p = C.param(engine, c); return p ? p[0] : H(c); }
        if (group === "MIXER") return MIXER[c];
        const f = FX[c];
        return f.length === 1 ? "FX " + f[0] : f[0] + " " + M.FX[f[0]].knobs[f[1]].n;
    };
    /* the most a group's value goes to, for K4 */
    C.max = function (x, engine) {
        const g = C.group(x.c);
        if (g === "PARAM") { const p = C.param(engine, x.c); return p ? p[1] : 255; }
        if (g === "MIXER") return 127;
        const f = FX[x.c];
        return f.length === 1 ? FX_NAMES.length - 1 : fxSteps(f[0], f[1]);
    };
    return C;
})();

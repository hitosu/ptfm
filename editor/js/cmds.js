/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The commands a cell holds (specs/files/commands.md), as the WRITER shows and picks them: in 3 characters in the
 * grid (`A04`, `~-4`, `ACC`, `!08`, `*40`, `=70`, `#3C`), in words beside a cell's fields (`ARPEGGIO 0 4 7`,
 * `TAPE STOP 8 ROWS`), and as a list to pick from, grouped as commands.md groups them. A command is [code, value].
 *
 *   ED.cmds.entries(engine)  the list to pick from: { id, group, name, code, sub, digits (2: 00-FF, 1: 0-F, 0 none),
 *                            max } ; engine: "DX7", "JUNO", "TB303", "SAMPLER" or null (the track's instrument's)
 *   entryOf(cmd, engine)     the entry a command is; text(cmd) its 3 characters; words(cmd, engine) in words
 *   make(entry, value)       the command an entry makes with a value (its digits) */
(function (ED) {
"use strict";
const D = ED.defs, H = D.H, H1 = v => (v & 15).toString(16).toUpperCase();
const sgn = D.sgn, list = D.list;

/* the instruments' parameters, an engine's table: code -> [name, max, words] */
const PARAMS = { TB303: {}, JUNO: {}, DX7: {}, SAMPLER: {} };
["WAVE", "CUTOFF", "RESONANCE", "ENV MOD", "DECAY", "ACCENT"].forEach((n, i) => { PARAMS.TB303[0x40 + i] = [n, 127]; });
["LFO RATE", "LFO DELAY", "DCO LFO", "DCO PWM", "DCO NOISE", "VCF FREQ", "VCF RES", "VCF ENV", "VCF LFO", "VCF KYBD",
    "VCA LEVEL", "ENV A", "ENV D", "ENV S", "ENV R", "DCO SUB"].forEach((n, i) => { PARAMS.JUNO[0x40 + i] = [n, 127]; });
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
    0x70: ["ALGORITHM", 31, v => String(v + 1)], 0x71: ["FEEDBACK", 7], 0x72: ["OSC KEY SYNC", 1, list("OFF", "ON")],
    0x73: ["TRANSPOSE", 48, v => sgn(v - 24)], 0x74: ["LFO SPEED", 99], 0x75: ["LFO DELAY", 99], 0x76: ["LFO PITCH DEPTH", 99],
    0x77: ["LFO AMP DEPTH", 99], 0x78: ["LFO WAVE", 5, list("TRI", "SAW DOWN", "SAW UP", "SQUARE", "SINE", "S/H")],
    0x79: ["LFO KEY SYNC", 1, list("OFF", "ON")], 0x7A: ["PITCH MOD SENS", 7] });
Object.assign(PARAMS.SAMPLER, {
    0x40: ["START", 255, v => H(v) + "/100"], 0x41: ["END", 255, v => H(v) + "/100"], 0x42: ["LOOP", 2, list("OFF", "FORWARD", "PING-PONG")],
    0x43: ["LOOP START", 255, v => H(v) + "/100"], 0x44: ["LOOP END", 255, v => H(v) + "/100"],
    0x45: ["TUNE", 255, v => v === 0x80 ? "NONE" : sgn(v - 128) + "/128"] });
for (let r = 0; r < 4; r++) { PARAMS.SAMPLER[0x48 + r] = ["ENV R" + (r + 1), 99]; PARAMS.SAMPLER[0x4C + r] = ["ENV L" + (r + 1), 99]; }
const MIXER = { 0x80: "LEVEL", 0x81: "CHORUS SEND", 0x82: "DELAY SEND", 0x83: "REVERB SEND", 0x84: "DUCK AMOUNT" };
const engineName = kind => ({ 0x01: "DX7", 0x02: "JUNO", 0x03: "TB303", 0x04: "SAMPLER" })[kind] || null;

/* ---- the fixed entries ---- */
const E = [];
const xy = v => [H1(v >> 4), H1(v & 15)];
function volSlide(v) { const x = H1(v >> 4), y = H1(v & 15); return x === "F" && y !== "0" ? "FINE -" + y : y === "F" && x !== "0" ? "FINE +" + x : x !== "0" ? "+" + x : "-" + y; }
function panWords(v) { return v === 128 ? "MIDDLE" : v < 128 ? "LEFT " + H(128 - v) : "RIGHT " + H(v - 128); }
const add = (group, code, sub, name, digits, words, extra) => { const e = Object.assign({ group, code, sub, name, digits, words }, extra || {}); e.id = code + (sub == null ? "" : "." + sub); E.push(e); return e; };
const T = "TRACKER EFFECTS", V = "XM'S VOLUME COLUMN", O = "PT/FM'S OWN", P = "PUNCH-INS";
add(T, 0x00, null, "ARPEGGIO", 2, v => "ARPEGGIO 0 " + xy(v).join(" "));
add(T, 0x01, null, "SLIDE UP", 2, v => "SLIDE UP " + H(v));
add(T, 0x02, null, "SLIDE DOWN", 2, v => "SLIDE DOWN " + H(v));
add(T, 0x03, null, "SLIDE TO NOTE", 2, v => "SLIDE TO NOTE " + H(v));
add(T, 0x04, null, "VIBRATO", 2, v => "VIBRATO " + xy(v).join(" "));
add(T, 0x05, null, "SLIDE TO NOTE + VOL", 2, v => "TO NOTE, VOL " + volSlide(v));
add(T, 0x06, null, "VIBRATO + VOL", 2, v => "VIBRATO, VOL " + volSlide(v));
add(T, 0x07, null, "TREMOLO", 2, v => "TREMOLO " + xy(v).join(" "));
add(T, 0x08, null, "PAN", 2, v => "PAN " + panWords(v));
add(T, 0x09, null, "SAMPLE OFFSET", 2, v => "START AT " + H(v) + "00");
add(T, 0x0A, null, "VOLUME SLIDE", 2, v => "VOLUME " + volSlide(v));
add(T, 0x0B, null, "JUMP", 2, v => "JUMP TO POSITION " + H(v));
add(T, 0x0C, null, "VOLUME", 2, v => "VOLUME " + H(Math.min(v, 64)));
add(T, 0x0D, null, "BREAK", 2, v => "BREAK TO ROW " + H(v));
const ESUB = { 1: "FINE SLIDE UP", 2: "FINE SLIDE DOWN", 3: "GLISSANDO", 4: "VIBRATO WAVE", 5: "FINETUNE", 6: "LOOP",
    7: "TREMOLO WAVE", 9: "RETRIGGER", 0xA: "FINE VOLUME UP", 0xB: "FINE VOLUME DOWN", 0xC: "NOTE CUT", 0xD: "NOTE DELAY",
    0xE: "PATTERN DELAY" };
const WAVES = ["SINE", "SAW", "SQUARE", "RANDOM"];
for (const s in ESUB) {
    const n = ESUB[s], sub = +s;
    add(T, 0x0E, sub, n, 1, v => {
        const y = v & 15;
        if (sub === 3) return "GLISSANDO " + (y ? "ON" : "OFF");
        if (sub === 4 || sub === 7) return n + " " + (WAVES[y & 3] || H1(y));
        if (sub === 5) return "FINETUNE " + sgn(y - 8);
        if (sub === 6) return y ? "LOOP " + y + " TIMES" : "LOOP FROM HERE";
        if (sub === 9) return "RETRIGGER EVERY " + y + " TICKS";
        if (sub === 0xC) return "NOTE CUT AT TICK " + y;
        if (sub === 0xD) return "NOTE DELAY " + y + " TICKS";
        if (sub === 0xE) return "ROW DELAY " + y + " ROWS";
        return n + " " + H1(y);
    });
}
add(T, 0x0F, null, "TICKS / TEMPO", 2, v => !v ? "THE SONG'S TEMPO" : v < 0x20 ? "TICKS A ROW " + v : "TEMPO " + v + " BPM");
add(T, 0x10, null, "GLOBAL VOLUME", 2, v => "GLOBAL VOLUME " + H(Math.min(v, 64)));
add(T, 0x11, null, "GLOBAL VOL SLIDE", 2, v => "GLOBAL VOL " + volSlide(v));
add(T, 0x14, null, "KEY OFF", 2, v => "NOTE OFF AT TICK " + v);
add(T, 0x19, null, "PAN SLIDE", 2, v => "PAN " + (v >> 4 ? "RIGHT " + H1(v >> 4) : "LEFT " + H1(v & 15)));
add(T, 0x1B, null, "MULTI RETRIGGER", 2, v => "RETRIGGER " + (v & 15) + ", VOL " + H1(v >> 4));
add(T, 0x1D, null, "TREMOR", 2, v => "TREMOR ON " + (v >> 4) + " OFF " + (v & 15));
add(T, 0x1E, null, "FINE VIBRATO", 2, v => "FINE VIBRATO " + xy(v).join(" "));
add(T, 0x21, 1, "EXTRA FINE UP", 1, v => "EXTRA FINE UP " + H1(v & 15));
add(T, 0x21, 2, "EXTRA FINE DOWN", 1, v => "EXTRA FINE DOWN " + H1(v & 15));
add(T, 0x22, null, "PANBRELLO", 2, v => "PANBRELLO " + xy(v).join(" "));
const VCOL = [["-", "VOL SLIDE DOWN"], ["+", "VOL SLIDE UP"], ["D", "FINE VOL DOWN"], ["U", "FINE VOL UP"], ["S", "VIBRATO SPEED"],
    ["V", "VIBRATO DEPTH"], ["P", "PAN"], ["<", "PAN SLIDE LEFT"], [">", "PAN SLIDE RIGHT"], ["M", "SLIDE TO NOTE"]];
VCOL.forEach(([s, n], i) => add(V, 0x24 + i, null, n, 1, v => n + " " + (s === "P" ? H((v & 15) * 17) : H1(v))));
add(O, 0xC0, null, "ACCENT", 0, () => "ACCENT");
add(O, 0xC1, null, "SLIDE", 0, () => "SLIDE INTO THE NEXT NOTE");
add(O, 0xC2, null, "ACCENT + SLIDE", 0, () => "ACCENT + SLIDE");
add(O, 0xD3, null, "LEGATO", 0, () => "LEGATO: THE HELD NOTE MOVES HERE, NO NEW ATTACK");
const PUNCHES = ["LOOP 1/4", "LOOP 1/8", "LOOP 1/16", "LOOP 1/32", "STUTTER 1/16T", "BIT CRUSH", "GATE 1/16", "GATE 1/32",
    "LOW-PASS SWEEP", "HIGH-PASS SWEEP", "ECHO THROW", "REVERB SWELL", "REVERSE", "HALF SPEED", "TAPE STOP", "VINYL STOP"];
PUNCHES.forEach((n, k) => add(P, 0xC3 + k, null, n, 2, v => n + " " + v + (v === 1 ? " ROW" : " ROWS"), { min: 1 }));

const group = c => c >= 0x40 && c <= 0x7F ? "PARAM" : c >= 0x80 && c <= 0x8F ? "MIXER" : c >= 0x90 && c <= 0xBF ? "FX" : null;
/* the list to pick from, for a track whose instrument has engine (the parameters are the engine's own) */
function entries(engine) {
    const out = E.slice();
    for (const c in PARAMS[engine] || {}) {
        const p = PARAMS[engine][c];
        out.push({ id: String(c), group: "THE INSTRUMENT'S PARAMETERS", name: p[0], code: +c, sub: null, digits: 2, max: p[1], words: v => p[0] + " " + (p[2] ? p[2](Math.min(v, p[1])) : Math.min(v, p[1])) });
    }
    for (const c in MIXER) out.push({ id: String(c), group: "THE TRACK'S MIXER", name: MIXER[c], code: +c, sub: null, digits: 2, max: 127, words: v => MIXER[c] + " " + H(v) });
    for (const fx of D.FX) fx.knobs.forEach(k => {      /* (in the effects' order: FILTER's envelope after RESO) */
        const c = k.code;
        out.push({ id: String(c), group: "THE FX", name: fx.name + " " + k.n, code: +c, sub: null, digits: 2, max: D.knobSteps(k), words: v => fx.name + " " + k.n + " " + D.knobText(k, v) });
    });
    out.push({ id: String(0xAE), group: "THE FX", name: "AN EFFECT ON", code: 0xAE, sub: null, digits: 2, max: 9, words: v => (D.FX_NAMES[v] || "EFFECT " + v) + " ON" });
    out.push({ id: String(0xAF), group: "THE FX", name: "AN EFFECT OFF", code: 0xAF, sub: null, digits: 2, max: 9, words: v => (D.FX_NAMES[v] || "EFFECT " + v) + " OFF" });
    return out;
}
function entryOf(c, engine) {
    if (!c) return null;
    const [code, v] = c, all = entries(engine);
    if (code === 0x0E || code === 0x21) return all.find(e => e.code === code && e.sub === v >> 4) || null;
    return all.find(e => e.code === code) || null;
}
function make(e, value) {
    if (!e) return null;
    if (e.sub != null) return [e.code, e.sub << 4 | (value & 15)];
    if (e.digits === 0) return [e.code, 0];
    return [e.code, Math.max(e.min || 0, Math.min(e.max !== undefined ? e.max : 255, value))];
}
/* the value as the entry's digits take it: 0-F for one digit, 00-FF for two */
const valueOf = (e, c) => e && e.sub != null ? c[1] & 15 : c[1];
function text(c) {
    if (!c) return "...";
    const [code, v] = c, g = group(code);
    if (g) return (g === "PARAM" ? "*" : g === "MIXER" ? "=" : "#") + H(v);
    if (code >= 0x24 && code <= 0x2D) return "~" + VCOL[code - 0x24][0] + H1(v);
    if (code === 0xC0) return "ACC";
    if (code === 0xC1) return "SLD";
    if (code === 0xC2) return "A+S";
    if (code === 0xD3) return "LEG";
    if (code >= 0xC3 && code <= 0xD2) return "!" + H(v);
    return code.toString(36).toUpperCase() + H(v);
}
function words(c, engine) {
    if (!c) return "";
    const e = entryOf(c, engine);
    if (!e) {
        const g = group(c[0]);
        if (g === "PARAM") return "PARAMETER " + H(c[0]) + " " + H(c[1]) + " (ANOTHER ENGINE'S)";
        return "COMMAND " + H(c[0]) + " " + H(c[1]);
    }
    return e.words(c[1]);
}
/* commands that act from any track (commands.md, Commands together) */
const global = c => [0x0B, 0x0D, 0x0F, 0x10, 0x11].includes(c[0]) || (c[0] === 0x0E && [6, 0xE].includes(c[1] >> 4)) || group(c[0]) === "FX" ||
    (c[0] >= 0xC3 && c[0] <= 0xD2);

ED.cmds = { PARAMS, MIXER, PUNCHES, entries, entryOf, make, valueOf, text, words, group, global, engineName };
})(globalThis.ED = globalThis.ED || {});

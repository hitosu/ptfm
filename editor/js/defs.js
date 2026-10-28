/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* What the editor shows and edits, as tables (specs/files/ and specs/ui/screens/): the FX knobs, the settings, the
 * engines' parameters, the MIDI map's controls, the kinds' names. Values are stored as the records store them;
 * each field says how it is shown. */
(function (ED) {
"use strict";

const H = (v, n) => (v >>> 0).toString(16).toUpperCase().padStart(n || 2, "0");
const sgn = (v, n) => (v > 0 ? "+" : v < 0 ? "-" : "") + Math.abs(v).toString(n === 16 ? 16 : 10).toUpperCase();
const list = (...names) => v => names[Math.min(v, names.length - 1)] || String(v);

/* ---- the FX (specs/ui/screens/fx.md): the knobs in their codes' order 0x90-0xAC, each as the FX screen shows it,
 * FILTER's envelope's (added later) at 0xB0-0xB5; a knob's stored value is its step from the bottom of its range ---- */
const NV = ["1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2"];
const pct = (n, def) => ({ n, min: 0, max: 100, unit: " %", def });
/* FILTER's envelope's times by step, 0.1 ms: ATTACK's, then DECAY's and RELEASE's (as the firmware's table) */
const ENV_T10 = [Array.from({ length: 128 }, (_, i) => Math.round(20 * Math.pow(1500, i / 127))),
                 Array.from({ length: 128 }, (_, i) => Math.round(50 * Math.pow(1000, i / 127)))];
const envTime = t10 => t10 < 100 ? Math.floor(t10 / 10) + "." + t10 % 10 + " MS"
    : Math.floor((t10 + 5) / 10) < 1000 ? Math.floor((t10 + 5) / 10) + " MS"
    : (cs => Math.floor(cs / 100) + "." + String(cs % 100).padStart(2, "0") + " S")(Math.floor((t10 + 50) / 100));
const envSource = v => v === 0 ? "KEYS" : v <= 16 ? "TRACK " + H(v - 1)
    : ["4 BEATS", "2 BEATS", "BEAT", "1/2 BEAT", "1/4 BEAT"][Math.min(4, v - 17)];
const FX = [
    { name: "GATE", num: 0, knobs: [{ n: "PATTERN", min: 0, max: 31, def: 0, fmt: v => "PATTERN " + H(v) },
        { n: "RATE", list: ["1/8", "1/16", "1/32", "1/8T", "1/16T"], def: 1 }, pct("DEPTH", 100), pct("SMOOTH", 20)] },
    { name: "FILTER", num: 1, knobs: [{ n: "CUT", min: -64, max: 63, def: 0, fmt: v => v === 0 ? "OFF" : (v < 0 ? "LP " + (-v) : "HP " + v) },
        pct("RESO", 30), pct("ENV", 0), { n: "SOURCE", min: 0, max: 21, def: 0, fmt: envSource },
        { n: "ATTACK", min: 0, max: 127, def: 0, fmt: v => envTime(ENV_T10[0][v]) },
        { n: "DECAY", min: 0, max: 127, def: 75, fmt: v => envTime(ENV_T10[1][v]) }, pct("SUSTAIN", 0),
        { n: "RELEASE", min: 0, max: 127, def: 75, fmt: v => envTime(ENV_T10[1][v]) }] },
    { name: "DRIVE", num: 2, knobs: [pct("DRIVE", 30), pct("TONE", 50)] },
    { name: "LO-FI", num: 3, knobs: [{ n: "BITS", min: 2, max: 16, def: 8 }, { n: "RATE", min: 1, max: 44, def: 22, unit: " KHZ" },
        pct("DUST", 20)] },
    { name: "CHORUS", num: 4, knobs: [{ n: "MODE", list: ["I", "II", "I+II"], def: 0 }, pct("RATE", 30), pct("DEPTH", 50), pct("MIX", 50)] },
    { name: "DELAY", num: 5, knobs: [{ n: "TIME", list: NV, def: 7 }, pct("FEEDBACK", 40), pct("TONE", 60),
        { n: "PING-PONG", list: ["OFF", "ON"], def: 0 }, pct("MIX", 30)] },
    { name: "REVERB", num: 6, knobs: [pct("SIZE", 60), pct("DAMP", 40), { n: "PRE-DELAY", min: 0, max: 100, def: 10, unit: " MS" }, pct("MIX", 25)] },
    { name: "LIMITER", num: 7, knobs: [{ n: "CEILING", min: -12, max: 0, def: -1, unit: " DB" },
        { n: "RELEASE", min: 10, max: 1000, def: 100, unit: " MS", step: 10 }] },
    { name: "DUCK", num: 8, knobs: [{ n: "SOURCE", min: 0, max: 15, def: 0, fmt: v => "TRACK " + H(v) }, pct("DEPTH", 60),
        { n: "RELEASE", min: 20, max: 1000, def: 150, unit: " MS", step: 10 }] },
];
const FX_NAMES = ["GATE", "FILTER", "DRIVE", "LO-FI", "CHORUS", "DELAY", "REVERB", "LIMITER", "DUCK", "METRONOME", "◆ SENDS"];
const FX_CODES = {};                                    /* code -> [effect, knob index] */
{
    let c = 0x90;
    for (const e of FX) e.knobs.forEach((k, i) => { k.code = e.name === "FILTER" && i >= 2 ? 0xB0 + i - 2 : c++; FX_CODES[k.code] = [e, i]; });
}
const knobSteps = k => k.list ? k.list.length - 1 : Math.round((k.max - k.min) / (k.step || 1));
const knobValue = (k, step) => k.list ? Math.min(step, k.list.length - 1) : Math.min(k.max, k.min + step * (k.step || 1));
const knobText = (k, step) => {
    const v = knobValue(k, step);
    if (k.list) return k.list[v];
    if (k.fmt) return k.fmt(v);
    return (k.min < 0 && v > 0 ? "+" : "") + v + (k.unit || "");
};
const knobStep = (k, value) => k.list ? value : Math.round((value - k.min) / (k.step || 1));
function fxDefaults() {
    const d = {};
    for (const e of FX) for (const k of e.knobs) d[k.code] = knobStep(k, k.def);
    return d;
}
const METRONOME_SOUNDS = ["CLICK", "BEEP", "WOOD", "COWBELL"];

/* ---- the settings record (specs/files/settings.md), as SETTINGS shows it ---- */
const THEMES = ["DEFAULT", "EGA", "PASTEL", "NEON", "SUNSET", "ICE", "CLEAR", "GREEN", "AMBER", "WHITE",
                "DAYLIGHT", "PAPER", "MANUAL", "SILVER", "LCD", "COMMANDER"];
const SCREENS = ["STATUS", "LAST", "SONGS", "EDITOR", "INSTRUMENTS", "PLAYBACK", "FX", "VISUAL", "SETTINGS", "LOOPS", "DJ"];
const RHYTHMS = ["#", "#_", "#__", "#___", "##_", "###_", "##__", "#_##_", "#_______", "#_______________"];
/* each: name, byte(s), how it reads (get) and writes (set) the record's bytes, its values (list or min..max), default */
const u8 = (at) => ({ get: p => p[at], set: (p, v) => { p[at] = v & 255; } });
const SETTINGS = [
    /* OUTPUT: SYNC PULSE L / R a click on that side at every half beat; byte 120 keeps its side too, for readers from
       before (a record of then: byte 0 STEREO / MONO, byte 120 the side) */
    { n: "OUTPUT", at: 0, list: ["STEREO", "MONO", "SYNC PULSE L", "SYNC PULSE R"], def: 0,
        get: p => { const v = p[0] <= 3 ? p[0] : 1; return v < 2 && (p[120] === 1 || p[120] === 2) ? 1 + p[120] : v; },
        set: (p, v) => { p[0] = v; p[120] = v >= 2 ? v - 1 : 0; } },
    { n: "LINE GAIN", at: 111, min: -24, max: 12, def: 0, unit: " DB", get: p => p[111] - 24, set: (p, v) => { p[111] = v + 24; } },
    { n: "USB GAIN", at: 112, min: -24, max: 12, def: 0, unit: " DB", get: p => p[112] - 24, set: (p, v) => { p[112] = v + 24; } },
    { n: "VOICES", at: 1, list: ["8", "12", "16"], def: 2, get: p => p[1] === 8 ? 0 : p[1] === 12 ? 1 : 2,
      set: (p, v) => { p[1] = [8, 12, 16][v]; } },   /* (anything else reads as 16, the default) */
    /* byte 123 bit 1 set: OFF (bit 0 is the current instrument's) */
    { n: "SECOND CORE", at: 123, list: ["ON", "OFF"], def: 0, get: p => p[123] >> 1 & 1, set: (p, v) => { p[123] = (p[123] & ~2) | (v ? 2 : 0); } },
    { n: "THEME", at: 2, list: THEMES, def: 0, ...u8(2) },
    { n: "KEYS VOLUME", at: 3, min: 1, max: 127, def: 0x60, hex: true, ...u8(3) },
    { n: "KEYS GAIN", at: 124, min: -24, max: 12, def: 0, unit: " DB", get: p => (p[124] << 24) >> 24, set: (p, v) => { p[124] = v & 255; } },
    { n: "KEYS", at: 125, list: ["THROUGH FX", "DRY"], def: 0, get: p => p[125] === 1 ? 1 : 0, set: (p, v) => { p[125] = v ? 1 : 0; } },
    { n: "KEYS OCTAVE", at: 4, min: -4, max: 4, def: 0, get: p => (p[4] << 24) >> 24, set: (p, v) => { p[4] = v & 255; } },
    { n: "KNOB SPEED", at: 5, list: ["OFF", "LOW", "HIGH"], def: 1, ...u8(5) },
    { n: "HOLD DELAY", at: 6, min: 300, max: 1500, step: 50, def: 500, unit: " MS", get: p => p[6] | p[7] << 8, set: (p, v) => { p[6] = v & 255; p[7] = v >> 8; } },
    { n: "COMBO GRACE", at: 8, min: 0, max: 250, step: 10, def: 100, unit: " MS", ...u8(8) },
    { n: "PLAYBACK SCROLL", at: 9, list: ["ROWS", "PAGES"], def: 0, ...u8(9) },
    /* byte 123 bit 2 set: OFF */
    { n: "PRESETS LIST", at: 123, list: ["ON", "OFF"], def: 0, get: p => p[123] >> 2 & 1, set: (p, v) => { p[123] = (p[123] & ~4) | (v ? 4 : 0); } },
    { n: "COUNT-IN", at: 109, list: ["OFF", "1 BAR", "2 BARS", "KEY"], def: 1, ...u8(109) },
    { n: "ARP RHYTHM", at: 110, list: RHYTHMS, def: 3, ...u8(110) },
    { n: "ARP PATTERN", at: 113, list: ["UP", "DOWN", "UP-DOWN", "RANDOM", "AS PLAYED"], def: 0, ...u8(113) },
    { n: "ARP LATCH", at: 114, list: ["OFF", "1 NOTE", "2 NOTES", "3 NOTES", "4 NOTES", "5 NOTES", "6 NOTES", "7 NOTES", "8 NOTES"], def: 4, ...u8(114) },
    { n: "POWER-ON SCREEN", at: 10, list: SCREENS, def: 0, ...u8(10) },
    { n: "TUNING", at: 12, min: 4300, max: 4500, step: 1, def: 4400, tenths: true, unit: " HZ",
        get: p => 4300 + Math.min(200, p[12]), set: (p, v) => { p[12] = Math.max(0, Math.min(200, v - 4300)); } },
];
const MIDI_SETTINGS = [
    { n: "MIDI IN CHANNEL", at: 115, list: ["ALL", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"], def: 0, ...u8(115) },
    { n: "MIDI OUT", at: 116, list: ["OFF", "KEYS", "ALL"], def: 1,
        get: p => p[116] === 1 ? 0 : p[116] === 2 ? 2 : 1, set: (p, v) => { p[116] = v === 0 ? 1 : v === 2 ? 2 : 0; } },
    { n: "MIDI OUT CHANNEL", at: 117, min: 1, max: 16, def: 1, get: p => 1 + (p[117] & 15), set: (p, v) => { p[117] = v - 1; } },
    { n: "SYNC", at: 118, list: ["INTERNAL", "EXTERNAL"], def: 0, ...u8(118) },
    { n: "CLOCK OUT", at: 119, list: ["OFF", "ON"], def: 0, ...u8(119) },
];
/* the current instrument at the last SAVE (bytes 121-122, set when byte 123's bit 0 is): F0:00 when not set */
const curInst = p => p[123] & 1 ? p[121] << 8 | p[122] : 0xF000;
function setCurInst(p, ins) { p[121] = ins >> 8 & 255; p[122] = ins & 255; p[123] = (p[123] & ~1) | 1; }
/* a new settings record (128 B): every setting at its default, the user's GATE patterns all on, the EDITOR on its own
 * pattern (byte 126 clear) */
function newSettings() {
    const p = new Uint8Array(128);
    for (const s of SETTINGS.concat(MIDI_SETTINGS)) s.set(p, s.def);
    for (let g = 0; g < 16; g++) p.set([0x49, 0x92, 0x24, 0x49, 0x92, 0x24], 13 + 6 * g);
    return p;
}
const GATE_STEPS = ["OFF", "ON", "×2", "×3", "×4"];
function gateGet(p, g) {                    /* 16 steps of 3 bits, 6 B a pattern, from bit 0 */
    const out = [];
    for (let k = 0; k < 16; k++) {
        const bit = 3 * k, at = 13 + 6 * g + (bit >> 3);
        const v = ((p[at] | (p[at + 1] << 8)) >> (bit & 7)) & 7;
        out.push(v > 4 ? 0 : v);
    }
    return out;
}
function gateSet(p, g, steps) {
    p.fill(0, 13 + 6 * g, 19 + 6 * g);
    for (let k = 0; k < 16; k++) {
        const bit = 3 * k, at = 13 + 6 * g + (bit >> 3), v = steps[k] > 4 ? 0 : steps[k];
        p[at] |= (v << (bit & 7)) & 255;
        if ((bit & 7) > 5) p[at + 1] |= v >> (8 - (bit & 7));
    }
}

/* ---- the engines' parameters (specs/files/instruments.md, commands.md 0x40-0x7F), as the CARTRIDGE editor shows them ---- */
const CURVES = ["-LIN", "-EXP", "+EXP", "+LIN"];
const LFO_WAVES = ["TRIANGLE", "SAW DOWN", "SAW UP", "SQUARE", "SINE", "S/H"];
const NOTE_NAMES = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"];
const noteName = n => NOTE_NAMES[n % 12] + Math.floor(n / 12 - 1);
const DX7_OP = [
    { f: "output_level", n: "LEVEL", max: 99 }, { f: "mode", n: "MODE", max: 1, fmt: list("RATIO", "FIXED") },
    { f: "coarse", n: "COARSE", max: 31 }, { f: "fine", n: "FINE", max: 99 }, { f: "detune", n: "DETUNE", max: 14, fmt: v => sgn(v - 7) },
    { f: "r1", n: "R1", max: 99 }, { f: "r2", n: "R2", max: 99 }, { f: "r3", n: "R3", max: 99 }, { f: "r4", n: "R4", max: 99 },
    { f: "l1", n: "L1", max: 99 }, { f: "l2", n: "L2", max: 99 }, { f: "l3", n: "L3", max: 99 }, { f: "l4", n: "L4", max: 99 },
    { f: "break_point", n: "BREAK POINT", max: 99, fmt: v => noteName(v + 21) }, { f: "left_depth", n: "LEFT DEPTH", max: 99 },
    { f: "right_depth", n: "RIGHT DEPTH", max: 99 }, { f: "left_curve", n: "LEFT CURVE", max: 3, fmt: v => CURVES[v] },
    { f: "right_curve", n: "RIGHT CURVE", max: 3, fmt: v => CURVES[v] }, { f: "rate_scaling", n: "RATE SCALING", max: 7 },
    { f: "amp_mod_sens", n: "AMP MOD SENS", max: 3 }, { f: "key_vel_sens", n: "KEY VEL SENS", max: 7 }];
const DX7_VOICE = [
    { f: "algorithm", n: "ALGORITHM", max: 31, fmt: v => String(v + 1) }, { f: "feedback", n: "FEEDBACK", max: 7 },
    { f: "osc_key_sync", n: "OSC KEY SYNC", max: 1, fmt: list("OFF", "ON") },
    { f: "transpose", n: "TRANSPOSE", max: 48, fmt: v => sgn(v - 24) },
    { f: "lfo_speed", n: "LFO SPEED", max: 99 }, { f: "lfo_delay", n: "LFO DELAY", max: 99 },
    { f: "lfo_pm_depth", n: "LFO PITCH DEPTH", max: 99 }, { f: "lfo_am_depth", n: "LFO AMP DEPTH", max: 99 },
    { f: "lfo_key_sync", n: "LFO KEY SYNC", max: 1, fmt: list("OFF", "ON") }, { f: "lfo_wave", n: "LFO WAVE", max: 5, fmt: v => LFO_WAVES[v] },
    { f: "pitch_mod_sens", n: "PITCH MOD SENS", max: 7 },
    { f: "pr1", n: "PITCH R1", max: 99 }, { f: "pr2", n: "PITCH R2", max: 99 }, { f: "pr3", n: "PITCH R3", max: 99 }, { f: "pr4", n: "PITCH R4", max: 99 },
    { f: "pl1", n: "PITCH L1", max: 99 }, { f: "pl2", n: "PITCH L2", max: 99 }, { f: "pl3", n: "PITCH L3", max: 99 }, { f: "pl4", n: "PITCH L4", max: 99 }];
const JUNO = [
    { f: "lfo_rate", n: "LFO RATE", max: 127, grp: "LFO" }, { f: "lfo_delay", n: "LFO DELAY", max: 127, grp: "LFO" },
    { f: "dco_lfo", n: "DCO LFO", max: 127, grp: "DCO" }, { f: "dco_pwm", n: "DCO PWM", max: 127, grp: "DCO" },
    { f: "dco_noise", n: "DCO NOISE", max: 127, grp: "DCO" }, { f: "dco_sub", n: "DCO SUB", max: 127, grp: "DCO" },
    { f: "range", n: "RANGE", max: 2, fmt: list("16'", "8'", "4'"), grp: "DCO" }, { f: "pulse", n: "PULSE", max: 1, fmt: list("OFF", "ON"), grp: "DCO" },
    { f: "saw", n: "SAW", max: 1, fmt: list("OFF", "ON"), grp: "DCO" }, { f: "pwm_source", n: "PWM SOURCE", max: 1, fmt: list("LFO", "MAN"), grp: "DCO" },
    { f: "hpf", n: "HPF", max: 3, grp: "HPF" },
    { f: "vcf_freq", n: "VCF FREQ", max: 127, grp: "VCF" }, { f: "vcf_res", n: "VCF RES", max: 127, grp: "VCF" },
    { f: "vcf_env", n: "VCF ENV", max: 127, grp: "VCF" }, { f: "env_polarity", n: "ENV POLARITY", max: 1, fmt: list("+", "-"), grp: "VCF" },
    { f: "vcf_lfo", n: "VCF LFO", max: 127, grp: "VCF" }, { f: "vcf_kybd", n: "VCF KYBD", max: 127, grp: "VCF" },
    { f: "vca_mode", n: "VCA MODE", max: 1, fmt: list("ENV", "GATE"), grp: "VCA" }, { f: "vca_level", n: "VCA LEVEL", max: 127, grp: "VCA" },
    { f: "env_a", n: "ENV A", max: 127, grp: "ENV" }, { f: "env_d", n: "ENV D", max: 127, grp: "ENV" },
    { f: "env_s", n: "ENV S", max: 127, grp: "ENV" }, { f: "env_r", n: "ENV R", max: 127, grp: "ENV" },
    { f: "chorus", n: "CHORUS", max: 2, fmt: list("OFF", "I", "II"), grp: "CHORUS" }];
const TB303 = [{ f: "wave", n: "WAVE", max: 127 }, { f: "cutoff", n: "CUTOFF", max: 127 }, { f: "resonance", n: "RESONANCE", max: 127 },
    { f: "env_mod", n: "ENV MOD", max: 127 }, { f: "decay", n: "DECAY", max: 127 }, { f: "accent", n: "ACCENT", max: 127 }];
const SAMPLER_INST = [
    { f: "base_note", n: "BASE NOTE", max: 127, fmt: v => noteName(v) },
    { f: "tune", n: "TUNE", max: 255, fmt: v => v === 0x80 ? "NONE" : sgn(v - 128) + "/128" },
    { f: "volume", n: "VOLUME", max: 64 }, { f: "loop", n: "LOOP", max: 2, fmt: list("OFF", "FORWARD", "PING-PONG") }];
const ENV = [{ f: "r1", n: "R1", max: 99 }, { f: "r2", n: "R2", max: 99 }, { f: "r3", n: "R3", max: 99 }, { f: "r4", n: "R4", max: 99 },
    { f: "l1", n: "L1", max: 99 }, { f: "l2", n: "L2", max: 99 }, { f: "l3", n: "L3", max: 99 }, { f: "l4", n: "L4", max: 99 }];
const ENGINE = { 0x01: "DX7", 0x02: "JUNO-106", 0x03: "TB-303", 0x04: "SAMPLER", 0x05: "FUSION", 0x06: "CHIP" };
/* a CHIP instrument's settings as the device's edit pages name them (specs/proposals/202610251559-chip-engine.md) */
const XN = list("X1", "X2", "X3", "X4");
const CHIP = [
    { f: "osc", n: "OSC", max: 4, fmt: list("PULSE", "TRIANGLE", "WAVE", "BUZZ", "NOISE"), grp: "OSC" },
    { f: "duty", n: "DUTY", max: 3, fmt: list("12.5%", "25%", "50%", "75%"), grp: "OSC" },
    { f: "bz_shape", n: "SHAPE", max: 2, fmt: list("SAW DN", "SAW UP", "TRI"), grp: "OSC" },
    { f: "bz_steps", n: "STEPS", max: 1, fmt: list("16", "32"), grp: "OSC" },
    { f: "bz_gate", n: "GATE", max: 1, fmt: list("OFF", "ON"), grp: "OSC" },
    { f: "n_mix", n: "NOISE", max: 1, fmt: list("OFF", "ON"), grp: "NOISE" },
    { f: "n_short", n: "MODE", max: 1, fmt: list("LONG", "SHORT"), grp: "NOISE" },
    { f: "n_rate", n: "RATE", max: 15, grp: "NOISE" }, { f: "n_key", n: "KEY", max: 1, fmt: list("KEY OFF", "KEY ON"), grp: "NOISE" },
    { f: "clock", n: "CLOCK", max: 6, fmt: list("BRK", "8BIT", "PSG 1.77", "PSG 1.00", "PSG 1.79", "16BIT", "EXACT"), grp: "CLOCK" },
    { f: "curve", n: "CURVE", max: 3, fmt: list("BRK", "8BIT", "PSG", "16BIT"), grp: "CLOCK" },
    { f: "filter", n: "FILTER", max: 3, fmt: list("BRK", "8BIT", "PSG", "NONE"), grp: "CLOCK" },
    { f: "fps50", n: "FRAMES", max: 1, fmt: list("60 HZ", "50 HZ"), grp: "CLOCK" },
    { f: "sweep", n: "SWEEP", max: 2, fmt: list("OFF", "DOWN", "UP"), grp: "SWEEP" },
    { f: "sw_shift", n: "SHIFT", max: 7, grp: "SWEEP" }, { f: "sw_pace", n: "PACE", max: 7, grp: "SWEEP" },
    { f: "vol", n: "VOL", max: 15, grp: "VOLUME" }, { f: "env", n: "ENV", max: 3, fmt: list("NONE", "DOWN", "UP", "PSG"), grp: "VOLUME" },
    { f: "env_step", n: "STEP", max: 7, grp: "VOLUME" }, { f: "release", n: "RELEASE", max: 15, grp: "VOLUME" },
    { f: "psg_shape", n: "SHAPE", max: 15, grp: "PSG ENV" }, { f: "psg_period", n: "PERIOD", max: 65535, grp: "PSG ENV" },
    { f: "wave", n: "WAVE", max: 15, grp: "TABLE" },
    ...["vol", "arp", "pitch", "duty", "noise"].flatMap(k => [
        { f: "tab_" + k, n: "TABLE", max: 63, grp: k.toUpperCase() }, { f: "spd_" + k, n: "SPEED", max: 3, fmt: XN, grp: k.toUpperCase() }]),
    { f: "vib_delay", n: "DELAY", max: 31, fmt: v => 2 * v + " FRAMES", grp: "VIBRATO" },
    { f: "vib_speed", n: "SPEED", max: 3, fmt: XN, grp: "VIBRATO" },
    { f: "vib_depth", n: "DEPTH", max: 31, fmt: v => 2 * v + " CENTS", grp: "VIBRATO" }];
/* a new slot of each kind: a plain sound of its engine */
function newSlot(kind, name) {
    const R = ED.rec;
    if (kind === R.KIND.DX7) {
        const op = i => ({ r1: 99, r2: 99, r3: 99, r4: 99, l1: 99, l2: 99, l3: 99, l4: 0, break_point: 39, left_depth: 0, right_depth: 0,
            left_curve: 0, right_curve: 0, rate_scaling: 0, amp_mod_sens: 0, key_vel_sens: 0, output_level: i === 5 ? 99 : 0, mode: 0,
            coarse: 1, fine: 0, detune: 7 });
        return { name: name || "NEW VOICE", level: 0, ops: [0, 1, 2, 3, 4, 5].map(op), voice: { pr1: 99, pr2: 99, pr3: 99, pr4: 99,
            pl1: 50, pl2: 50, pl3: 50, pl4: 50, algorithm: 31, feedback: 0, osc_key_sync: 1, lfo_speed: 35, lfo_delay: 0,
            lfo_pm_depth: 0, lfo_am_depth: 0, lfo_key_sync: 1, lfo_wave: 0, pitch_mod_sens: 3, transpose: 24 } };
    }
    if (kind === R.KIND.JUNO) return { name: name || "NEW PATCH", level: 0, lfo_rate: 0, lfo_delay: 0, dco_lfo: 0, dco_pwm: 0, dco_noise: 0,
        vcf_freq: 127, vcf_res: 0, vcf_env: 0, vcf_lfo: 0, vcf_kybd: 0, vca_level: 127, env_a: 0, env_d: 0, env_s: 127, env_r: 0, dco_sub: 0,
        range: 1, pulse: 0, saw: 1, chorus: 0, pwm_source: 0, env_polarity: 0, vca_mode: 0, hpf: 1 };
    if (kind === R.KIND.TB303) return { name: name || "NEW 303", level: 0, wave: 0, cutoff: 64, resonance: 64, env_mod: 64, decay: 64, accent: 64 };
    if (kind === R.KIND.FUSION) return { name: name || "NEW FUSION", level: 0, pattern: 0 };
    if (kind === R.KIND.CHIP)            /* (the engine's reference sound: PULSE 50 %, CLOCK EXACT, FILTER NONE, VOL 15) */
        return { name: name || "INIT", level: 0, osc: 0, duty: 2, bz_shape: 0, bz_steps: 0, bz_gate: 0, n_mix: 0, n_short: 0,
            n_rate: 4, n_key: 0, clock: 6, curve: 0, filter: 3, fps50: 0, sweep: 0, sw_shift: 2, sw_pace: 2, vol: 15, env: 0,
            env_step: 0, release: 0, psg_shape: 9, psg_period: 800, wave: 0, tab_vol: 0, spd_vol: 0, tab_arp: 0, spd_arp: 0,
            tab_pitch: 0, spd_pitch: 0, tab_duty: 0, spd_duty: 0, tab_noise: 0, spd_noise: 0, vib_delay: 0, vib_speed: 0, vib_depth: 0 };
    return { name: name || "NEW SAMPLE", level: 0, sample: 0, start: 0, end: 0, loop_start: 0, loop_end: 0, loop: 0, base_note: 60,
        tune: 0x80, volume: 64, env: { r1: 99, r2: 99, r3: 99, r4: 99, l1: 99, l2: 99, l3: 99, l4: 0 } };
}

/* ---- the MIDI map's FM-1 controls (specs/files/midi.md) ---- */
const KNOBS = ["SELECT", "ALGORITHM", "K1", "K2", "K3", "K4", "PRESETS"];
const BUTTONS = ["OCT-", "OCT+", "FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"];
const CONTROLS = KNOBS.concat(["MASTER"], BUTTONS, Array.from({ length: 27 }, (_, k) => "KEY " + noteName(53 + k)));
const MAP_KINDS = ["ABSOLUTE KNOB", "RELATIVE: TWO'S COMPLEMENT", "RELATIVE: OFFSET 64", "RELATIVE: SIGN AND MAGNITUDE",
    "MOMENTARY PAD", "TOGGLE PAD"];

/* ---- kinds ---- */
const KIND_NAME = { 0x01: "DX7", 0x02: "JUNO-106", 0x03: "TB-303", 0x04: "SAMPLER", 0x05: "FUSION", 0x06: "CHIP", 0x10: "SAMPLE", 0x20: "PATTERN",
    0x21: "SONG", 0x22: "LOOPS", 0x23: "DJ", 0x30: "SETTINGS", 0x31: "MIDI MAP", 0xE0: "NOTES" };

ED.defs = { H, sgn, list, FX, FX_NAMES, FX_CODES, NV, knobSteps, knobValue, knobText, knobStep, fxDefaults, METRONOME_SOUNDS,
    SETTINGS, MIDI_SETTINGS, newSettings, curInst, setCurInst, GATE_STEPS, gateGet, gateSet, THEMES, DX7_OP, DX7_VOICE, JUNO, TB303, CHIP, SAMPLER_INST, ENV,
    ENGINE, newSlot, CONTROLS, MAP_KINDS, KNOBS, BUTTONS, KIND_NAME, noteName };
})(globalThis.ED = globalThis.ED || {});

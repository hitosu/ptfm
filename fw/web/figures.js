/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The debug build's figures on the page side (specs/debug.md): FIGURES' bytes decoded, every answer of a
 * session gathered into sections, the statistics, and the report to copy. No DOM, no MIDI: debug.html does
 * those; Node runs this as it is (fw/tests/figures_test.js).
 *
 *   PTFM_figures.ask(flags)          -> Uint8Array: 30 FIGURES? (flags: RESET 1, FREEZE 2, UNFREEZE 4)
 *   PTFM_figures.decode(message)     -> a window's figures, or null (not a FIGURES message); its dj: the decks as
 *                                       they play and DJ's record (a section DJ played in lists its settings); win:
 *                                       the window's number
 *   PTFM_figures.decode2(message)    -> core 2's window (37 FIGURES 2), or null; join(fig, c2) puts it in its window
 *                                       (fig.c2) when their numbers match (true), as the 37 comes right after its 31
 *   PTFM_figures.c2Text(c2)          -> core 2's state as a word (STARTING, ON, OFF, OFF (38), STOPPED: FAULT ...)
 *   PTFM_figures.secondAsk(on)       -> Uint8Array: 38 SECOND CORE (0: core 2 off, 1: as SETTINGS says)
 *   PTFM_figures.session()           -> the RUNS: { add(fig, now): kept while recording, last: the latest always;
 *                                         record(name, now), stop(now), recording, reset(), sections (the runs),
 *                                         all, of(i), remove(i), stats(list), runReport(i, now), report(now) }
 *   A run's figures count only the windows the FM-1 played in (the stopped ones left out, and said so),
 *   unless it never played.
 *   PTFM_figures.sectionLines(name, stats) -> a run's (or an autotest's measured stretch's) report lines
 *   PTFM_figures.unsentIn(windows)   -> the FIGURES answers the FM-1 could not send between the first and the last
 *   PTFM_figures.benchAsk(run)       -> Uint8Array: 32 BENCH (run 1: start it, 0: stop it)
 *   PTFM_figures.decodeBench(msg)    -> a 33 BENCH RECORD: START, CORE, ALONE or END, or null
 *   PTFM_figures.bench()             -> { add(record), rows, start, end, running(), lines(): the report's }
 *   PTFM_figures.proveAsk(what)      -> Uint8Array: 35 PROVE (0 the records, 1 the watchdog test, 2 the main loop's
 *                                       stack's overflow, 3 the interrupts'; 4 core 2 faults, 5 a misread, 6 core 2
 *                                       stuck: no restart)
 *   PTFM_figures.decodeRecords(msg)  -> a 36 RECORDS, or null; PTFM_figures.recordsLines(r) -> its lines
 */
(function (root) {
"use strict";
const PREFIX = [0xF0, 0x7D, 0x50, 0x54], ASK = 0x30, FIGS = 0x31, VERSION = 11;
const FIGS2 = 0x37, VERSION2 = 2, LEN2 = 56, SECOND = 0x38;   /* 37 FIGURES 2: core 2's window; 38 SECOND CORE */
const C2_STATE = ["STARTING", "ON", "OFF", "STOPPED"];
const C2_WHY = ["", "NO ANSWER", "MISREAD", "STUCK", "FAULT", "AFTER A CRASH"];
const STAGES = ["START", "CONTROL", "DJ DECKS", "DJ LFOS", "REPLAY", "CELLS", "NOTES", "VOICES", "DX7", "JUNO", "TB-303",
                "SAMPLE", "MIX", "J-CHORUS", "EFFECTS", "REVERB", "DELAY", "CHORUS", "DJ FILTERS", "DJ MIX", "INSERTS",
                "LIMITER", "PUNCH-INS", "OUTPUT", "USB AUDIO", "CLOCK", "BLOCKS", "MIDI", "END"];
const WORST_LEN = 8 + 2 * STAGES.length;                 /* a worst half: its sum, voices, notes, blocks, stages */
const DJ_LEN = 1 + 2 * 24 + 136;                         /* DJ: flags, each deck as it plays, DJ's record */
const USB_LEN = 16;                                      /* USB audio: its state, its packets */
const LEN = 18 + 6 * STAGES.length + 88 + WORST_LEN + DJ_LEN + USB_LEN;
const INSIDE = { "DJ DECKS": "CONTROL", "DJ LFOS": "CONTROL", CELLS: "REPLAY", NOTES: "REPLAY", DX7: "VOICES", JUNO: "VOICES", "TB-303": "VOICES", SAMPLE: "VOICES", MIX: "VOICES",
                 "J-CHORUS": "VOICES", REVERB: "EFFECTS", DELAY: "EFFECTS", CHORUS: "EFFECTS",
                 "DJ FILTERS": "EFFECTS", "DJ MIX": "EFFECTS", INSERTS: "EFFECTS", LIMITER: "EFFECTS", "PUNCH-INS": "EFFECTS",
                 "USB AUDIO": "OUTPUT" };
                                                         /* (parts of another stage: not added twice) */
const ENGINE_AT = STAGES.indexOf("DX7");
const ENGINES = ["DX7", "JUNO", "TB-303", "SAMPLE"];
const WHAT = ["NOTHING", "SONG", "PATTERN", "LOOPS", "DJ"];
const FX = ["GATE", "FILTER", "DRIVE", "LO-FI", "CHORUS", "DELAY", "REVERB", "LIMITER", "DUCK", "METRONOME"];   /* by number */
const fxNames = (m) => FX.filter((n, i) => m >> i & 1).join(" ") || "none";
const BLOCK_US = 64 / 44100 * 1e6;                       /* a voice block: 64 frames of sound, 1,451 us */
const MOMENTS = 12;                                      /* a section's moments at most (its size stays the same) */

function ask(flags) { return Uint8Array.from(PREFIX.concat([ASK, (flags || 0) & 0x7F, 0xF7])); }

/* a worst half's bytes (FIGURES' and a STEP record's): the half whose stages summed most */
function worstAt(b, o, n) {
    const u16 = (x) => b[x] | b[x + 1] << 8;
    return { us: u16(o), voices: b[o + 2], notes: b[o + 3], blocks: [0, 1, 2, 3].map((e) => b[o + 4 + e]),
             stages: STAGES.slice(0, n).map((name, s) => ({ name, us: u16(o + 8 + 2 * s) })) };
}

function unpack7(p) {
    const out = [];
    for (let i = 0; i < p.length; i += 8) {
        const top = p[i], n = Math.min(7, p.length - i - 1);
        for (let j = 0; j < n; j++) out.push(p[i + 1 + j] | (top >> j & 1) << 7);
    }
    return Uint8Array.from(out);
}

function decode(m) {
    if (!m || m.length < 7 || m[m.length - 1] !== 0xF7 || m[4] !== FIGS) return null;
    for (let i = 0; i < 4; i++) if (m[i] !== PREFIX[i]) return null;
    const b = unpack7(m.subarray ? m.subarray(5, m.length - 1) : m.slice(5, m.length - 1));
    if (b.length < LEN || b[0] !== VERSION || b[16] !== STAGES.length) return null;
    const u16 = (o) => b[o] | b[o + 1] << 8, u32 = (o) => (u16(o) + u16(o + 2) * 65536);
    const str = (o, n) => { let s = ""; for (let i = 0; i < n && b[o + i]; i++) s += String.fromCharCode(b[o + i]); return s; };
    const p = 18 + 6 * STAGES.length;                   /* (the rest, after the stages: specs/debug.md) */
    const f = {
        frozen: !!(b[1] & 1), frozenNow: !!(b[1] & 2), ms: u16(2), halves: u16(4), late: u16(6), isrSum: u32(8), isrMax: u16(12), halfUs: u16(14) || 5804,
        win: b[17], c2: null,                           /* (its 37's figures: join) */
        stages: STAGES.map((name, s) => ({ name, sum: u32(18 + 6 * s), max: u16(22 + 6 * s) })),
        blocks: ENGINES.map((e, i) => u16(p + 2 * i)),
        voices: b[p + 8], voicesMost: b[p + 9], steals: u16(p + 10), flashReads: u16(p + 12), flashUs: u16(p + 14),
        passes: u16(p + 16), frames: u16(p + 18), passMax: u32(p + 20), frameSum: u32(p + 24), frameMax: u32(p + 28),
        stackUsed: [u16(p + 32), u16(p + 34)], stackSize: [u16(p + 36), u16(p + 38)], uptime: u32(p + 40),
        what: b[p + 44], playing: !!b[p + 45], song: b[p + 46], decks: [b[p + 47], b[p + 48]], voiceCount: b[p + 49],
        bpm10: u16(p + 50), fx: u16(p + 52), screen: str(p + 54, 10), version: str(p + 64, 24),
        worst: worstAt(b, p + 88, STAGES.length),
        dj: djAt(b, p + 88 + WORST_LEN),
        usb: usbAt(b, p + 88 + WORST_LEN + DJ_LEN),
    };
    return f;
}

/* ---- core 2 (37 FIGURES 2: specs/debug.md, Core 2's bytes), sent right after each 31 for the same window ---- */
function decode2(m) {
    if (!m || m.length < 7 || m[m.length - 1] !== 0xF7 || m[4] !== FIGS2) return null;
    for (let i = 0; i < 4; i++) if (m[i] !== PREFIX[i]) return null;
    const b = unpack7(m.subarray ? m.subarray(5, m.length - 1) : m.slice(5, m.length - 1));
    if (b.length < LEN2 || b[0] !== VERSION2) return null;
    const u16 = (o) => b[o] | b[o + 1] << 8, u32 = (o) => (u16(o) + u16(o + 2) * 65536);
    return { win: b[1], state: b[2], why: b[3], setting: !!(b[4] & 1), forced: !!(b[4] & 2), clk: !!(b[4] & 4),
             whole: !!(b[4] & 8), start10ns: u16(6), sum: u32(8), max: u16(12), blocks: u16(14), misreads: u16(16),
             waitSum: u32(18), waitMax: u16(22), late1: u16(24), late2: u16(26), stackUsed: u16(28), stackSize: u16(30),
             faultVec: u32(32), faultPc: u32(36),
             /* its voice jobs (the voices in its places): their time, how many, the most of its places at once, core 1's
              * waits for them */
             vUs: u32(40), vJobs: u16(44), vMost: b[46], vWaitSum: u32(48), vWaitMax: u16(52),
             /* the window's SysEx lost on the way in: begun while others waited, cut short by another message */
             sxLost: [b[54], b[55]] };
}
/* a 37 into its window (the 31 just before it, the same number): false when they do not match (that window's 31 lost) */
function join(f, c) {
    if (!f || !c || f.win !== c.win) return false;
    f.c2 = c;
    return true;
}
const c2Text = (c) => (C2_STATE[c.state] || `STATE ${c.state}`) + (c.state === 3 && c.why ? `: ${C2_WHY[c.why] || c.why}` :
                                                                   c.state === 2 && c.forced ? " (38)" : "");
function secondAsk(on) { return Uint8Array.from(PREFIX.concat([SECOND, on ? 1 : 0, 0xF7])); }

/* ---- USB (FIGURES' USB: specs/debug.md): audio, and the FIGURES answers the FM-1 could not send ---- */
function usbAt(b, o) {
    const u16 = (x) => b[x] | b[x + 1] << 8;
    return { on: !!(b[o] & 1), r48: !!(b[o] & 2), unsent: b[o + 1], pkts: u16(o + 2), dry: u16(o + 4), missed: u16(o + 6),
             stalls: u16(o + 8), starts: u16(o + 10), fill: u16(o + 12), target: u16(o + 14) };
}
/* the FIGURES answers the FM-1 could not send between a list's first window and its last (its count is since
 * power-on, mod 256): a window missing with none of these was lost on the computer's side */
function unsentIn(w) {
    return w.length > 1 ? (w[w.length - 1].usb.unsent - w[0].usb.unsent) & 255 : 0;
}
/* a list of windows' USB audio: null if it never streamed */
function usbReport(w) {
    const on = w.filter((f) => f.usb.on || f.usb.pkts);
    if (!on.length) return null;
    const fills = on.filter((f) => f.usb.on).map((f) => f.usb.fill);
    return { windows: on.length, of: w.length, r48: on[on.length - 1].usb.r48, pkts: sum(on.map((f) => f.usb.pkts)),
             dry: sum(w.map((f) => f.usb.dry)), missed: sum(w.map((f) => f.usb.missed)), stalls: sum(w.map((f) => f.usb.stalls)),
             starts: sum(w.map((f) => f.usb.starts)), target: on[on.length - 1].usb.target,
             fillLo: fills.length ? Math.min(...fills) : 0, fillHi: fills.length ? Math.max(...fills) : 0 };
}

/* ---- DJ (FIGURES' DJ: specs/debug.md; the record: specs/files/dj.md) ---- */
function djAt(b, o) {
    const u16 = (x) => b[x] | b[x + 1] << 8;
    const str = (x, n) => { let s = ""; for (let i = 0; i < n && b[x + i]; i++) s += String.fromCharCode(b[x + i]); return s; };
    return {
        live: !!(b[o] & 1),
        decks: [0, 1].map((d) => {
            const q = o + 1 + 24 * d;
            return { playing: !!(b[q] & 1), armed: !!(b[q] & 2), ended: !!(b[q] & 4), stopped: !!(b[q] & 8), roll: !!(b[q] & 16),
                     pos: b[q + 1], row: b[q + 2], bpm10: u16(q + 3), stop: b[q + 5], loop: b[q + 6], loopRows: b[q + 7],
                     name: str(q + 8, 16) };
        }),
        rec: Array.from(b.slice(o + 49, o + 49 + 136)),
    };
}

const s8 = (x) => (x << 24 >> 24);
const signed = (x) => (x > 0 ? "+" : "") + x;
const at = (pos, row) => `${hex2(pos)}:${hex2(row)}`;
const LFO_SHAPE = ["SINE", "SQUARE", "TRIANGLE", "SAW UP", "SAW DOWN", "RANDOM"];
const LFO_SPEED = ["32 BARS", "16 BARS", "8 BARS", "4 BARS", "2 BARS", "1 BAR", "1/2", "1/4", "1/8"];
function lfoText(r, o, ranges, what) {                   /* an LFO's setup (files/dj.md, An LFO's setup) */
    if (!(r[o] & 1)) return "OFF";
    return `ON${what ? " " + what : ""} (${LFO_SHAPE[r[o + 1]] || r[o + 1]}, ${ranges[r[o + 2]] || r[o + 2]}, ${LFO_SPEED[r[o + 3]] || r[o + 3]}` +
           (r[o] & 2 ? ", REVERSED)" : ")");
}
/* DJ's own effects, from its FX setup (files/songs.md, The FX setup: on / off bits, knobs at 16 + code - 0x90) */
const DJ_FX = [["CHORUS", 4, 0x9B, ["MODE", "RATE", "DEPTH", "MIX"]], ["DELAY", 5, 0x9F, ["TIME", "FEEDBACK", "TONE", "PING-PONG", "MIX"]],
               ["REVERB", 6, 0xA4, ["SIZE", "DAMP", "PRE-DELAY", "MIX"]], ["LIMITER", 7, 0xA8, ["CEILING", "RELEASE"]]];

/* DJ's settings as [name, value] pairs, in the order the report lists them (a deck's place and what it is doing
 * now are not settings: djNow) */
function djSettings(dj) {
    const r = dj.rec, S = [], fx = r.slice(8, 72), on = fx[7] | fx[8] << 8;
    S.push(["CROSSFADER", `${["SMOOTH", "SHARP"][r[1]] || r[1]} at ${r[5]} of 32`]);
    S.push(["FILTER RESONANCE", `${r[2]} %`]);
    S.push(["FILTER THE SENDS", r[3] ? "OFF" : "ON"]);
    S.push(["SYNC", r[6] & 1 ? `ON (${["THE MAIN'S BPM", "ITS HALF", "ITS DOUBLE"][r[6] >> 2 & 3] || "?"})` : "OFF"]);
    S.push(["CROSSFADER LFO", lfoText(r, 80, ["L TO HERE", "L TO R", "SWING", "MIRROR"])]);
    S.push(["FX LFO", lfoText(r, 84, ["HERE TO 100 %", "0 TO 100 %"], ["REVERB MIX", "DELAY MIX", "CHORUS MIX"][r[4]] || String(r[4]))]);
    for (const [name, n, code, knobs] of DJ_FX)
        S.push([name, on >> n & 1 ? "ON (" + knobs.map((k, i) => `${k} ${fx[16 + code - 0x90 + i]}`).join(", ") + ")" : "OFF"]);
    S.push(["METRONOME", on >> 9 & 1 ? "ON" : "OFF"]);
    ["LEFT", "RIGHT"].forEach((side, d) => {
        const k = r.slice(88 + 24 * d, 112 + 24 * d), name = dj.decks[d].name;
        if (k[0] === 255) { S.push([side, "EMPTY"]); return; }
        const filt = s8(k[9]), mutes = [], m = k[16] | k[17] << 8;
        for (let t = 0; t < 16; t++) if (m >> t & 1) mutes.push(hex2(t));
        S.push([side, `${hex2(k[0])} ${name}`]);
        S.push([`${side} TEMPO`, `${((k[5] | k[6] << 8) / 10).toFixed(1)} BPM`]);
        S.push([`${side} KEY`, signed(s8(k[7]))]);
        S.push([`${side} VOLUME`, s8(k[8]) <= -25 ? "OFF" : `${signed(s8(k[8]))} dB`]);
        S.push([`${side} FILTER`, filt === 0 ? "OFF" : `${filt < 0 ? "LP" : "HP"} ${Math.abs(filt)}${k[10] & 1 ? "" : " (ITS KEY OFF)"}`]);
        S.push([`${side} REPEAT PATTERN`, k[10] & 2 ? "ON" : "OFF"]);
        S.push([`${side} LOOP`, k[11] ? `PAD ${k[11]} (${k[14] / 2} BEATS FROM ${at(k[12], k[13])}, ${k[15] & 1 ? "A REPEAT OF BARS" : "FREE"})` : "NONE"]);
        S.push([`${side} MUTES`, mutes.join(" ") || "NONE"]);
        S.push([`${side} SOLO`, k[18] === 255 ? "NONE" : hex2(k[18])]);
        S.push([`${side} FILTER LFO`, lfoText(k, 19, ["OFF TO HERE", "OFF TO FULL", "HERE TO FULL", "SWING"])]);
        S.push([`${side} CUE`, at(k[3], k[4])]);
    });
    return S;
}

/* what each deck is doing now: playing or not, where, its tempo, a loop or a stop running */
const STOPS = ["", "BRAKE", "BACKSPIN", "ECHO OUT", "REVERB OUT"];
function djNow(dj) {
    return dj.decks.map((k, d) => {
        const side = d ? "RIGHT" : "LEFT ";
        if (dj.rec[88 + 24 * d] === 255) return `${side} now: empty`;
        const what = k.playing ? "PLAYING" : k.armed ? "ARMED" : k.ended ? "AT ITS END" : "PAUSED";
        return `${side} now: ${what} at ${at(k.pos, k.row)}, ${(k.bpm10 / 10).toFixed(1)} BPM` +
               (k.loop ? `, looping ${k.loop === 1 ? "the pattern" : k.loopRows + " rows"}${k.roll ? " (ROLL)" : ""}` : "") +
               (k.stop ? `, ${STOPS[k.stop] || "a stop"} ${k.stopped ? "standing" : "running"}` : "");
    });
}

/* the DJ part of a section: the settings as at its end, what the decks did, and every setting that changed during it
 * (its values in order); null when DJ never played in it */
function djReport(w) {
    const dw = w.filter((f) => f.what === 4 && f.dj);
    if (!dw.length) return null;
    const seen = new Map();
    for (const f of dw)
        for (const [k, v] of djSettings(f.dj)) {
            const a = seen.get(k) || [];
            if (a[a.length - 1] !== v) a.push(v);
            seen.set(k, a);
        }
    const last = dw[dw.length - 1].dj;
    return { settings: djSettings(last), now: djNow(last),
             changed: [...seen.entries()].filter(([, a]) => a.length > 1).map(([k, a]) =>
                 `${k} ${a.length > 6 ? a.slice(0, 2).concat(["...", a[a.length - 1]]).join(" -> ") : a.join(" -> ")}`) };
}

/* parts joined by ", " into lines under 100 characters */
function wrap(parts, indent) {
    const L = [];
    let line = indent;
    for (const x of parts) {
        if (line.length > indent.length && line.length + x.length + 2 > 98) { L.push(line.replace(/,\s*$/, "")); line = indent + "  "; }
        line += x + ", ";
    }
    if (line.trim()) L.push(line.replace(/,\s*$/, ""));
    return L;
}

function djLines(d) {
    const L = ["DJ  (its settings as at the section's end; places position:row in hex, knobs as their steps)"];
    const pick = (f) => d.settings.filter(([k]) => f(k)).map(([k, v]) => `${k} ${v}`);
    L.push(...wrap(pick((k) => !/^(LEFT|RIGHT)/.test(k) && !DJ_FX.some((x) => x[0] === k) && k !== "METRONOME"), "   "));
    L.push(...wrap(pick((k) => DJ_FX.some((x) => x[0] === k) || k === "METRONOME"), "   "));
    for (const side of ["LEFT", "RIGHT"]) {
        const deck = d.settings.filter(([k]) => k.startsWith(side));
        const rest = deck.slice(1).map(([k, v]) => `${k.slice(side.length + 1)} ${v}`);
        L.push(...wrap([`${side} ${deck[0][1]}` + (rest.length ? `: ${rest[0]}` : "")].concat(rest.slice(1)), "   "));
    }
    for (const n of d.now) L.push("   " + n);
    if (d.changed.length) L.push(...wrap([`changed during the section: ${d.changed[0]}`].concat(d.changed.slice(1)), "   "));
    else L.push("   no setting changed during the section");
    return L;
}

/* ---- statistics ---- */
const sum = (a) => a.reduce((s, x) => s + x, 0);
function quantile(a, q) {
    if (!a.length) return 0;
    const s = a.slice().sort((x, y) => x - y), i = (s.length - 1) * q, lo = Math.floor(i);
    return s[lo] + (s[Math.min(lo + 1, s.length - 1)] - s[lo]) * (i - lo);
}
const hex2 = (n) => n.toString(16).toUpperCase().padStart(2, "0");
const mmss = (s) => String(Math.floor(s / 60)).padStart(2, "0") + ":" + String(Math.floor(s % 60)).padStart(2, "0");
const pct = (x) => x.toFixed(1);

/* a list of windows -> its figures, from the windows the FM-1 played in (all of them if it never played);
 * empty (no halves): null */
function stats(list) {
    const all = list.filter((f) => f.halves > 0), played = all.filter((f) => f.playing);
    const w = played.length ? played : all;
    if (!w.length) return null;
    const halves = sum(w.map((f) => f.halves)), budget = sum(w.map((f) => f.halves * f.halfUs));
    const cpu = w.map((f) => f.isrSum / (f.halves * f.halfUs) * 100);
    const r = {
        windows: w.length, seconds: sum(w.map((f) => f.ms)) / 1000, halves,
        cpu: { mean: sum(w.map((f) => f.isrSum)) / budget * 100, median: quantile(cpu, 0.5), p95: quantile(cpu, 0.95),
               worst: Math.max(...w.map((f) => f.isrMax / f.halfUs * 100)), worstUs: Math.max(...w.map((f) => f.isrMax)) },
        late: sum(w.map((f) => f.late)),
        stages: STAGES.map((name, s) => {
            const per = w.map((f) => f.stages[s].sum / f.halves);
            return { name, inside: INSIDE[name] || "", mean: sum(w.map((f) => f.stages[s].sum)) / halves,
                     share: sum(w.map((f) => f.stages[s].sum)) / budget * 100, p95: quantile(per, 0.95),
                     worst: Math.max(...w.map((f) => f.stages[s].max)) };
        }),
        engines: ENGINES.map((name, i) => {
            const us = sum(w.map((f) => f.stages[ENGINE_AT + i].sum)), blocks = sum(w.map((f) => f.blocks[i]));
            return { name, blocks, usBlock: blocks ? us / blocks : 0, voicePct: blocks ? us / blocks / BLOCK_US * 100 : 0,
                     voices: blocks * BLOCK_US / (sum(w.map((f) => f.ms)) * 1000) };
        }),
        /* (the mean from the voice blocks the windows made, every block of every voice counted: the voices now in
         * one look a window can miss voices that sound half a row, as a TB-303's) */
        voices: { mean: sum(w.map((f) => sum(f.blocks))) * BLOCK_US / (sum(w.map((f) => f.ms)) * 1000 || 1),
                  most: Math.max(...w.map((f) => f.voicesMost)), steals: sum(w.map((f) => f.steals)) },
        unsent: unsentIn(all),
        frames: sum(w.map((f) => f.frames)), frameSum: sum(w.map((f) => f.frameSum)), frameMax: Math.max(...w.map((f) => f.frameMax)),
        passMax: Math.max(...w.map((f) => f.passMax)),
        flash: { reads: sum(w.map((f) => f.flashReads)), us: sum(w.map((f) => f.flashUs)) },
        stacks: [0, 1].map((k) => ({ used: Math.max(...w.map((f) => f.stackUsed[k])), size: w[w.length - 1].stackSize[k] })),
        frozen: w.filter((f) => f.frozen).length,
        stopped: all.length - w.length,             /* windows left out: the FM-1 stopped */
        worst: w.reduce((a, f) => (f.worst.us > a.us ? f.worst : a), w[0].worst),   /* the worst half's own stages */
    };
    /* CPU 2: core 2's blocks' time (each window's 37), over the windows that have one; null when none has */
    const w2 = w.filter((f) => f.c2);
    r.cpu2 = null;
    r.core2 = null;
    if (w2.length) {
        const b2 = sum(w2.map((f) => f.halves * f.halfUs)), cpu2 = w2.map((f) => f.c2.sum / (f.halves * f.halfUs) * 100);
        const c = w2.map((f) => f.c2), last = c[c.length - 1], states = new Map();
        const fault = c.filter((x) => x.faultVec || x.faultPc).pop();   /* (the last window holding its fault record) */
        for (const x of c) states.set(c2Text(x), (states.get(c2Text(x)) || 0) + 1);
        r.cpu2 = { mean: sum(c.map((x) => x.sum)) / b2 * 100, median: quantile(cpu2, 0.5), p95: quantile(cpu2, 0.95),
                   worst: Math.max(...w2.map((f) => f.c2.max / f.halfUs * 100)), worstUs: Math.max(...c.map((x) => x.max)) };
        r.core2 = { windows: w2.length, of: w.length, states: [...states.entries()], whole: c.filter((x) => x.whole).length,
                    blocks: sum(c.map((x) => x.blocks)), waitMean: sum(c.map((x) => x.waitSum)) / sum(w2.map((f) => f.halves)),
                    waitMax: Math.max(...c.map((x) => x.waitMax)), late1: sum(c.map((x) => x.late1)),
                    late2: sum(c.map((x) => x.late2)), misreads: last.misreads, misreadsIn: last.misreads - c[0].misreads,
                    stackUsed: Math.max(...c.map((x) => x.stackUsed)), stackSize: last.stackSize,
                    start10ns: last.start10ns, clk: last.clk, fault: fault ? { vec: fault.faultVec, pc: fault.faultPc } : null,
                    v: { jobs: sum(c.map((x) => x.vJobs)), most: Math.max(...c.map((x) => x.vMost)),
                         share: sum(c.map((x) => x.vUs)) / b2 * 100, waitSum: sum(c.map((x) => x.vWaitSum)),
                         waitMax: Math.max(...c.map((x) => x.vWaitMax)) } };
    }
    /* the stages hold both cores' work (CPU time): what they leave of both cores' time is the interrupt's own (and
     * core 2's between its stages) */
    const top = r.stages.filter((s) => !s.inside), both = sum(w.map((f) => f.isrSum + (f.c2 ? f.c2.sum : 0)));
    r.cpuBoth = both / budget * 100;
    r.other = { mean: both / halves - sum(top.map((s) => s.mean)), share: r.cpuBoth - sum(top.map((s) => s.share)) };
    r.fps = r.seconds ? r.frames / r.seconds : 0;
    /* the moments: each window with late halves or a START of 300 us or more, at its time into the list (which action
     * a spike or a dropout came with); at most MOMENTS, the ones with the most late halves, then the longest START */
    let at = 0;
    const mo = [];
    for (const f of all) {
        if (f.late || f.stages[0].max >= 300) mo.push({ t: at / 1000, late: f.late, start: f.stages[0].max, worst: f.isrMax / f.halfUs * 100 });
        at += f.ms;
    }
    r.momentsAll = mo.length;
    r.moments = mo.slice().sort((a, b) => b.late - a.late || b.start - a.start).slice(0, MOMENTS).sort((a, b) => a.t - b.t);
    r.context = context(w);
    r.dj = djReport(w);
    r.usb = usbReport(w);
    return r;
}

/* what played most of a list's time, as a line */
function context(w) {
    const count = new Map();
    for (const f of w) {
        const what = f.what === 4 ? `DJ ${f.decks[0] === 255 ? "--" : hex2(f.decks[0])} + ${f.decks[1] === 255 ? "--" : hex2(f.decks[1])}`
            : `${WHAT[f.what] || "WHAT " + f.what} ${hex2(f.song)}`;
        const k = `${f.playing ? what : "STOPPED (" + what + ")"}, ${(f.bpm10 / 10).toFixed(1)} BPM, VOICES ${f.voiceCount}, ` +
                  `screen ${f.screen || "?"}, effects: ${fxNames(f.fx)}`;
        count.set(k, (count.get(k) || 0) + f.ms);
    }
    const all = sum([...count.values()]) || 1;
    return [...count.entries()].sort((a, b) => b[1] - a[1]).slice(0, 2)
        .map(([k, ms]) => `${k} (${Math.round(ms / all * 100)}%)`);
}

/* a worst half (worstAt) as lines: its time, voices, notes started; its stages (parts in small letters) and its
 * voice blocks, wrapped under 100 characters */
function worstLines(w, indent) {
    const L = [], parts = [];
    if (!w || !w.us) return L;
    L.push(`${indent}worst half ${w.us} us (${pct(w.us / HALF_US * 100)}%): voices ${w.voices}, notes started ${w.notes}`);
    for (const s of w.stages)
        if (s.us) parts.push(`${INSIDE[s.name] ? s.name.toLowerCase() : s.name} ${s.us}`);
    const bl = ENGINES.map((e, i) => w.blocks[i] ? `${e} ${w.blocks[i]} x ${(w.stages[ENGINE_AT + i].us / w.blocks[i]).toFixed(1)} us` : "")
        .filter(Boolean);
    if (bl.length) parts.push("voice blocks: " + bl.join(", "));
    let line = indent + "  ";
    for (const x of parts) {
        if (line.length + x.length + 2 > 98) { L.push(line.replace(/,\s*$/, "")); line = indent + "  "; }
        line += x + ", ";
    }
    if (line.trim()) L.push(line.replace(/,\s*$/, ""));
    return L;
}

/* core 2 in a section: its state through the windows, its blocks, core 1's waits for it, the late halves each core
 * caused, its misreads, its stack, its start and its fault record */
function core2Lines(c) {
    const hex = (v) => (v >>> 0).toString(16).toUpperCase().padStart(8, "0");
    const parts = c.states.map(([k, n], i) => `${i ? "" : `core 2 in ${c.of} windows: `}${k} ${n}`);
    if (c.windows < c.of) parts.push(`${c.of - c.windows} without its figures`);
    parts.push(`blocks handed over ${c.blocks}, every half's in ${c.whole} windows`,
               `core 1 waited for it ${c.waitMean.toFixed(1)} us a half, the longest ${c.waitMax} us`,
               `late halves: core 1's ${c.late1}, core 2's ${c.late2}`,
               `misreads ${c.misreadsIn}${c.misreads !== c.misreadsIn ? ` (${c.misreads} since its start)` : ""}`);
    if (c.v.jobs) parts.push(`voices on core 2: ${c.v.jobs} voice jobs, ${c.v.most} places at most, ${pct(c.v.share)} % of a half`,
                             `core 1 waited for them ${(c.v.waitSum / c.v.jobs).toFixed(1)} us a job, the longest ` +
                             `${c.v.waitMax} us`);
    if (c.stackSize) parts.push(`its stack ${c.stackUsed} of ${c.stackSize} B`);   /* (none where it has no stack: the
                                                                                    * emulator) */
    if (c.start10ns) parts.push(`its start answered in ${(c.start10ns / 100).toFixed(2)} us` +
                                (c.clk ? " WITH THE CLOCK-DIVIDER BIT" : ""));
    if (c.fault) parts.push(`ITS FAULT: vector ${c.fault.vec}, pc ${hex(c.fault.pc)}`);
    return wrap(parts, "");
}

function sectionLines(title, s) {
    const L = [];
    if (!s) { L.push(`== ${title}: no figures`); return L; }
    L.push(`== ${title}  (${mmss(s.seconds)}, ${s.windows} windows${s.frozen ? `, screen frozen ${s.frozen} of them` : ""}` +
           `${s.stopped ? `; ${s.stopped} stopped left out` : ""})`);
    for (const c of s.context) L.push(`   ${c}`);
    if (s.dj) L.push(...djLines(s.dj));
    L.push(`CPU 1 % a half: mean ${pct(s.cpu.mean)}  median ${pct(s.cpu.median)}  p95 ${pct(s.cpu.p95)}  ` +
           `worst ${pct(s.cpu.worst)} (${s.cpu.worstUs} us)  late ${s.late}` +
           (s.late && s.seconds ? ` (${Math.round(s.late * 60 / s.seconds)}/min)` : ""));   /* (runs of other lengths compared) */
    if (s.cpu2) {
        L.push(`CPU 2 % a half: mean ${pct(s.cpu2.mean)}  median ${pct(s.cpu2.median)}  p95 ${pct(s.cpu2.p95)}  ` +
               `worst ${pct(s.cpu2.worst)} (${s.cpu2.worstUs} us)`);
        L.push(...core2Lines(s.core2));
    }
    L.push(`voices: mean ${s.voices.mean.toFixed(1)}  most ${s.voices.most}  notes taken ${s.voices.steals}`);
    L.push("stage        us/half  share   p95 us  worst us");
    const row = (n, mean, share, p95, worst) => `${n.padEnd(12)} ${mean.toFixed(0).padStart(7)} ${(pct(share) + "%").padStart(6)} ` +
        `${p95 === "" ? "".padStart(8) : p95.toFixed(0).padStart(8)} ${worst === "" ? "" : String(worst).padStart(9)}`;
    for (const st of s.stages) {
        if (st.inside && !st.mean) continue;
        L.push(row((st.inside ? "  " : "") + st.name, st.mean, st.share, st.p95, st.worst));
    }
    L.push(row("(interrupt)", s.other.mean, s.other.share, "", ""));
    L.push(...worstLines(s.worst, ""));
    if (s.moments.length) {                             /* (wrapped under 100 characters, as the worst half) */
        let line = `moments${s.momentsAll > s.moments.length ? ` (${s.moments.length} of ${s.momentsAll})` : ""}: `;
        for (const m of s.moments) {
            const x = `${m.t.toFixed(0)} s ${m.late ? `late ${m.late} ` : ""}START ${m.start} worst ${pct(m.worst)}%`;
            if (line.length + x.length + 2 > 98) { L.push(line.replace(/;\s*$/, "")); line = "  "; }
            line += x + "; ";
        }
        L.push(line.replace(/;\s*$/, ""));
    }
    const eng = s.engines.filter((e) => e.blocks);
    if (eng.length) L.push("a voice: " + eng.map((e) => `${e.name} ${e.usBlock.toFixed(0)} us/block = ${pct(e.voicePct)}% (mean ${e.voices.toFixed(1)} voices)`).join("; "));
    L.push(`screen: ${s.fps.toFixed(1)} fps, frame mean ${s.frames ? (s.frameSum / s.frames / 1000).toFixed(1) : "-"} ms, ` +
           `worst ${(s.frameMax / 1000).toFixed(1)} ms; main loop worst pass ${(s.passMax / 1000).toFixed(1)} ms`);
    L.push(`stacks: main loop ${s.stacks[0].used} of ${s.stacks[0].size} B, interrupts ${s.stacks[1].used} of ${s.stacks[1].size} B; ` +
           `flash reads in the sound: ${s.flash.reads} (${s.flash.us} us)`);
    const u = s.usb;
    if (!u) L.push("USB audio: nothing recording");
    else {
        L.push(`USB audio: recording at ${u.r48 ? "48" : "44.1"} kHz in ${u.windows} of ${u.of} windows; its buffer ` +
               `${u.fillLo}..${u.fillHi} frames`);
        L.push(`  at a half's start (aimed at ${u.target}); packets ${u.pkts}: short ${u.dry}, missed ${u.missed}, ` +
               `stalls ${u.stalls}, restarts ${u.starts}`);
    }
    if (s.unsent) L.push(`FIGURES: ${s.unsent} window${s.unsent === 1 ? "" : "s"} the FM-1 could not send whole (USB: cut ` +
                         "short or not sent)");
    return L;
}

/* a session of runs: RECORD starts a named run and keeps every window until STOP; between them nothing is kept
 * (the latest window is still there to show). A run can be dropped (remove) and reported alone (runReport) or with
 * the others (report: one after another, no summary of them all) */
function session() {
    const S = { all: [], sections: [], last: null, recording: false };
    let ids = 0;
    S.add = (f, now) => {
        S.last = f;
        if (!S.recording) return;
        f.at = now;
        f.section = S.sections[S.sections.length - 1].id;
        S.all.push(f);
    };
    S.record = (name, now) => {
        S.sections.push({ id: ++ids, name: name || `RUN ${ids}`, at: now, end: 0 });
        S.recording = true;
    };
    S.stop = (now) => {
        if (S.recording && S.sections.length) S.sections[S.sections.length - 1].end = now || Date.now();
        S.recording = false;
    };
    S.reset = () => { S.all = []; S.sections = []; S.last = null; S.recording = false; };
    S.of = (i) => { const sec = S.sections[i]; return sec ? S.all.filter((f) => f.section === sec.id) : []; };
    S.remove = (i) => {                                  /* (not the run under way) */
        const sec = S.sections[i];
        if (!sec || (S.recording && i === S.sections.length - 1)) return false;
        S.all = S.all.filter((f) => f.section !== sec.id);
        S.sections.splice(i, 1);
        return true;
    };
    S.stats = stats;
    const head = (now, n) => {
        const v = S.last ? S.last.version : "?", t = now ? new Date(now) : new Date();
        return `PT/FM FIGURES  firmware ${v}  ${t.toISOString().slice(0, 16).replace("T", " ")} UTC  ${n} windows`;
    };
    S.runReport = (i, now) => {
        const list = S.of(i), sec = S.sections[i];
        if (!sec) return "";
        return [head(now, list.length), ...(list.length ? sectionLines(sec.name, stats(list)) : [`== ${sec.name}: no figures`])]
            .join("\n") + "\n";
    };
    S.report = (now) => {
        const L = [head(now, S.all.length)];
        S.sections.forEach((sec, i) => { const list = S.of(i); if (list.length) L.push(...sectionLines(sec.name, stats(list))); });
        return L.join("\n") + "\n";
    };
    return S;
}

/* ---- the bench (specs/debug.md, The bench): 32 BENCH asks, 33 BENCH RECORD brings each result ---- */
const BENCH = 0x32, BREC = 0x33, BENCH_VERSION = 5, TICKS_US = 24, MHZ = 360;
const BKIND = ["START", "CORE", "ALONE", "STEP", "END", "NEXT"];
const UNIT = ["cycles", "bytes", "call", "block"];
function benchAsk(run) { return Uint8Array.from(PREFIX.concat([BENCH, run ? 1 : 0, 0xF7])); }

function decodeBench(m) {
    if (!m || m.length < 7 || m[m.length - 1] !== 0xF7 || m[4] !== BREC) return null;
    for (let i = 0; i < 4; i++) if (m[i] !== PREFIX[i]) return null;
    const b = unpack7(m.subarray ? m.subarray(5, m.length - 1) : m.slice(5, m.length - 1));
    if (b.length < 26 || b[0] !== BENCH_VERSION || b[1] > 5) return null;
    const u16 = (o) => b[o] | b[o + 1] << 8, u32 = (o) => (u16(o) + u16(o + 2) * 65536);
    const str = (o, n) => { let s = ""; for (let i = 0; i < n && b[o + i]; i++) s += String.fromCharCode(b[o + i]); return s; };
    const r = { kind: BKIND[b[1]], n: u16(2), total: u16(4), name: str(6, 20) };
    if (r.kind === "START") Object.assign(r, { version: str(26, 24), voices: b[50], uptime: u32(52) });
    else if (r.kind === "END") Object.assign(r, { status: b[26], ms: u32(28) });
    else if (r.kind === "CORE" || r.kind === "ALONE")
        Object.assign(r, { unit: UNIT[b[26]] || "cycles", runs: b[27], ops: u32(28), first: u32(32), min: u32(36),
                           max: u32(40), sum: u32(44) });
    else return null;                                   /* (STEP and NEXT: the steps', gone from the firmware) */
    return r;
}

/* a run's records -> its report's lines (and what the page shows) */
function bench() {
    const B = { rows: [], start: null, end: null };
    B.add = (r) => {
        if (r.kind === "START") { B.rows = []; B.start = r; B.end = null; return; }
        if (r.kind === "END") { B.end = r; return; }
        B.rows.push(r);
    };
    B.running = () => !!B.start && !B.end;
    B.lines = () => benchLines(B);
    return B;
}

const BLOCK_FRAMES = 64;
function measureText(r) {                               /* a CORE or ALONE row's figures */
    const mean = r.runs ? r.sum / r.runs : r.min;
    if (r.unit === "cycles") {
        const c = (t) => (t * MHZ / TICKS_US / r.ops).toFixed(1);
        return `${c(r.min).padStart(7)} cycles  (${(r.min / TICKS_US * 1000 / r.ops).toFixed(1)} ns)  mean ${c(mean)}  first ${c(r.first)}`;
    }
    if (r.unit === "bytes") return `${(r.ops * TICKS_US / r.min).toFixed(0).padStart(7)} MB/s  first ${(r.ops * TICKS_US / r.first).toFixed(0)}`;
    const us = (t) => (t / TICKS_US).toFixed(1);
    if (r.unit === "call") return `${us(r.min).padStart(7)} us  first ${us(r.first)}`;
    if (!r.runs) return "    (no sound: its audio is not there)";
    const voice = BLOCK_FRAMES / 44100 * 1e6;
    return `warm ${us(r.min).padStart(6)} / ${us(mean).padStart(6)} / ${us(r.max).padStart(6)} us   cold ${us(r.first).padStart(6)}` +
           `   ${(r.min / TICKS_US / voice * 100).toFixed(1).padStart(4)}% of its time`;
}
const HALF_US = 5804;
function benchLines(B) {
    const L = [];
    if (!B.start) return L;
    const end = B.end ? `${B.end.name.toLowerCase()} after ${mmss(B.end.ms / 1000)}` : "unfinished";
    L.push(`== BENCH  (${B.rows.length} of ${B.start.total} pieces, ${end}; firmware ${B.start.version}, VOICES ${B.start.voices})`);
    const core = B.rows.filter((r) => r.kind === "CORE"), alone = B.rows.filter((r) => r.kind === "ALONE");
    if (core.length) {
        L.push(`THE CORE  (interrupts off; the best of ${core[0].runs} runs after the first; cycles at ${MHZ} MHz a turn or op)`);
        for (const r of core) L.push(`  ${r.name.padEnd(20)} ${measureText(r)}`);
    }
    if (alone.length) {
        L.push("ALONE  (one voice or effect, a 64-frame block, interrupts off: warm best / mean / worst;");
        L.push("        cold: the code cache emptied first; the share: of the 1,451 us the block plays)");
        for (const r of alone) L.push(`  ${r.name.padEnd(20)} ${measureText(r)}`);
    }
    return L;
}

/* ---- PROVE (specs/debug.md, Proving the layout): 35 PROVE asks, 36 RECORDS answers ---- */
const PROVE = 0x35, RECS = 0x36, RECS_VERSION = 3, RECS_LEN = 120;
const PSTEP = ["none", "the watchdog test, its first restart", "the watchdog test, its second restart",
               "the main loop's stack's overflow", "the interrupts' stack's overflow"];
const CRASH_MAGIC = 0x43525348, PROVE_VEC = 0xFFFFFFFE;
const CRASH = ["magic", "count", "vec", "pc", "rets", "emu", "dbg", "sp", "psr", "icfg", "uptime", "etm0", "etm1", "etm2",
               "etm3", "early", "stack", "c2"];
function proveAsk(what) { return Uint8Array.from(PREFIX.concat([PROVE, (what || 0) & 0x7F, 0xF7])); }
function decodeRecords(m) {
    if (!m || m.length < 7 || m[m.length - 1] !== 0xF7 || m[4] !== RECS) return null;
    for (let i = 0; i < 4; i++) if (m[i] !== PREFIX[i]) return null;
    const b = unpack7(m.subarray ? m.subarray(5, m.length - 1) : m.slice(5, m.length - 1));
    if (b.length < RECS_LEN || b[0] !== RECS_VERSION) return null;
    const u16 = (o) => b[o] | b[o + 1] << 8, u32 = (o) => (u16(o) + u16(o + 2) * 65536);
    const crash = {};
    CRASH.forEach((k, i) => { crash[k] = u32(20 + 4 * i); });
    return {
        windows: b[1], step: b[2], bandChecked: !!b[3], bandChanged: u16(4), p3: b[6], failed: b[7], pending: b[8],
        first: b[9] ? { failed: b[10], pending: b[11], watchdog: !!b[12], crashKept: !!b[13],
                        band: u16(14) === 0xFFFF ? null : u16(14) } : null,
        rstSrc: u32(16), crash: crash.magic === CRASH_MAGIC ? crash : null,
        stacks: { mainLo: u32(92), mainTop: u32(96), irqLo: u32(100), irqTop: u32(104) },
        /* the pointers the start's check read (usp, ssp, and sp: the one in use then), which chose the windows */
        read: { usp: u32(108), ssp: u32(112), sp: u32(116) },
    };
}
function recordsLines(r) {
    const hex = (v) => (v >>> 0).toString(16).toUpperCase().padStart(8, "0");
    const L = ["== RECORDS (kept across restarts)"];
    L.push(`stack windows: ${r.windows === 2 ? "one for each stack" : r.windows === 1 ? "ONE OVER BOTH (the pointers were not where the start put them)" : "none"}`);
    L.push(`stacks: the main loop's ${hex(r.stacks.mainLo)}..${hex(r.stacks.mainTop)}, the interrupts' ${hex(r.stacks.irqLo)}..${hex(r.stacks.irqTop)}`);
    L.push(`the start's check read: usp ${hex(r.read.usp)}, ssp ${hex(r.read.ssp)}, sp ${hex(r.read.sp)} (sp ${r.read.sp === r.read.usp ?
           "is usp" : r.read.sp === r.read.ssp ? "is ssp" : "neither"})`);
    L.push(`this start: reset reasons ${r.p3.toString(2).padStart(8, "0")} (bit 0 power-on, 2 watchdog, 6 software), ` +
           `boot guard ${r.failed} failed, pending ${r.pending}`);
    L.push(`the band (256 B at 01C07F00) at this start: ${r.bandChecked ? (r.bandChanged ? `${r.bandChanged} BYTES CHANGED by the restart` : "kept: the restart left it alone")
           : "not checked (not patterned before it: a power-on, or a restart this build did not make)"}`);
    L.push(`PROVE: ${PSTEP[r.step] || "?"}`);
    if (r.first) L.push(`the watchdog test's first restart found: the crash record ${r.first.crashKept ? "kept" : "LOST"}, ` +
                        `the boot guard ${r.first.failed} failed (1 expected), the reset ${r.first.watchdog ? "the watchdog's" : "NOT THE WATCHDOG'S"}, ` +
                        `the band ${r.first.band === null ? "not checked" : r.first.band ? `${r.first.band} BYTES CHANGED` : "kept"}`);
    if (r.crash) {
        const c = r.crash, what = c.vec === PROVE_VEC ? "the watchdog test's own record" : `vector ${c.vec}`;
        L.push(`the crash record: ${c.count} so far, the last: ${what}, pc ${hex(c.pc)}, rets ${hex(c.rets)}, ` +
               `EMU ${hex(c.emu)}, DBG ${hex(c.dbg)}, sp ${hex(c.sp)}, ${c.uptime} ms up` +
               (c.stack ? `, STACK: ${c.stack === 2 ? "THE INTERRUPTS'" : "THE MAIN LOOP'S"}` : ""));
        L.push(`  core 2 at the crash: ${c.c2 === 1 ? "RUNNING (it stays off till the power goes off)" : "not running"}`);
    } else L.push("the crash record: none");
    return L;
}

const F = { ask, decode, decode2, join, c2Text, secondAsk, unpack7, stats, session, quantile, sectionLines, unsentIn, STAGES,
            INSIDE, ENGINES, LEN, LEN2, C2_STATE, C2_WHY, RESET: 1, FREEZE: 2, UNFREEZE: 4, benchAsk, decodeBench, bench,
            proveAsk, decodeRecords, recordsLines };
if (typeof module !== "undefined" && module.exports) module.exports = F;
else root.PTFM_figures = F;
})(typeof window !== "undefined" ? window : globalThis);

/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The playground's replay (playground/player.js, the reference that follows the specs) run without a browser:
 * every note it starts in a song's first seconds, as lines "E frame track midi CC:SS", the frame counted
 * from the song's first note, to compare with the firmware's player (render --events).
 *   node fw/tests/sound/pg_events.js SONG-NAME SECONDS */
"use strict";
const fs = require("fs"), path = require("path"), vm = require("vm");
const ROOT = path.resolve(__dirname, "../../.."), PG = path.join(ROOT, "playground"), SR = 44100;
const name = process.argv[2], seconds = +(process.argv[3] || 10);

let tickFn = null, now = 0;
const page = { console, performance, structuredClone, atob, setInterval: (fn, ms) => { if (ms === 20) tickFn = fn; return 0; } };
page.window = page;
vm.createContext(page);
for (const f of ["fonts.js", "content.js", "draw.js", "model.js"]) vm.runInContext(fs.readFileSync(path.join(PG, f), "utf8"), page, { filename: f });
const M = page.PTFM.model;
M.demoDisk();                                           /* (the playground starts clean: DEMO DISK pressed first) */
const ev = [];
const nop = () => {};
const A = new Proxy({
    time: () => now,
    noteOn(id, time, midi, ref, vol, track) { ev.push([time, track, midi, ref]); },
}, { get: (t, k) => k in t ? t[k] : nop });
page.PTFM.audio = A;
page.PTFM.ui = { song: 0, pattern: 0, inst: 0x0100, screen: { name: "SONGS" }, muted: () => false, editing: () => false,
                 set: k => M.state.settings[k], toast() {}, pausedHook() {}, clamp: (v, a, b) => v < a ? a : v > b ? b : v };
vm.runInContext(fs.readFileSync(path.join(PG, "player.js"), "utf8"), page, { filename: "player.js" });
const P = page.PTFM.player;
const i = M.state.songs.findIndex(s => s && s.name === name);
if (i < 0) { console.error("no song " + name); process.exit(2); }
M.state.settings.VOICES = 16;
page.PTFM.ui.song = i;
P.playSong(i);
for (; now < seconds + 0.5; now += 0.02) tickFn();
const seen = new Set();
const hex = v => v.toString(16).toUpperCase().padStart(2, "0");
let t0 = null;
for (const [time, track, midi, ref] of ev.sort((a, b) => a[0] - b[0] || a[1] - b[1])) {
    if (typeof track !== "number" || track > 15) continue;          /* (the songs' tracks only) */
    const key = time.toFixed(5) + "/" + track;
    if (seen.has(key)) continue;                                     /* (a FUSION note's layers: one trigger) */
    seen.add(key);
    if (t0 === null) t0 = time;
    const f = Math.round((time - t0) * SR);
    if (f > seconds * SR) break;
    console.log(`E ${f} ${track} ${midi} ${hex(ref >> 8)}:${hex(ref & 255)}`);
}

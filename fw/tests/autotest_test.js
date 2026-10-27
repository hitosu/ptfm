// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Vasily Kazantsev
// The debug page's autotests (fw/web/autotest.js, specs/debug.md#autotests) run against the firmware built for the
// browser with the debug build's figures (emulator/build.py --debug), the demo disk seeded: every scenario through
// MIRROR and the state, as on the FM-1, in the device's own time (the audio rendered between the steps), its times
// measured shortened (scale). Every scenario must pass; the report is printed.
//   node autotest_test.js WASM FLASH [SCALE] [SCENARIO...]
"use strict";
const fs = require("fs"), path = require("path");
const FG = require(path.join(__dirname, "..", "web", "figures.js"));
const AT = require(path.join(__dirname, "..", "web", "autotest.js"));
const [wasmPath, flashPath, scaleArg, ...only] = process.argv.slice(2);
const SCALE = +(scaleArg || 0.05);

let fake = 0;
const ex = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(wasmPath)),
    { env: { web_clock_us: () => (fake += 1) } }).exports;
const mem = () => ex.memory.buffer;
ex.web_flash_reset();
new Uint8Array(mem(), ex.web_flash(), 1 << 20).set(fs.readFileSync(flashPath));
ex.web_init();
if (process.env.AT_PASS_MS) ex.web_pass_min(+process.env.AT_PASS_MS * 1000);   /* (passes as slow as the FM-1's) */

/* the device's time, rendered a quantum at a time; what sleeps wakes when its time comes */
let now = 0, frames = 0;
const sleepers = [], asks = [], figuresWin = [];
let measuring = null, benchWait = null, figAt = 1000, log = [], figFlags = 0;
let lastFig = null, figures = 0, joined = 0;            /* (the last 31; the 31s, the 37s joined to them) */
let figHeard = 0, stNum = 0, stLost = 0, stLate = 0, stSeen = 0;   /* (as the page counts them: io.heard, io.answers) */
/* AT_LOSE_STATES=K:N: the state's answers K to K+N-1 lost on the way (never reach the page), as a computer drops them */
const [LOSE_K, LOSE_N] = (process.env.AT_LOSE_STATES || "0:0").split(":").map(Number);
function render() {
    ex.web_render(128);
    frames += 128;
    now = frames * 1000 / 44100;
    const n = ex.web_sysex_out_len();
    if (n) {
        const out = new Uint8Array(mem(), ex.web_sysex_out(), n).slice();
        ex.web_sysex_out_clear();
        for (let i = 0, s = 0; i < out.length; i++) if (out[i] === 0xF7) { take(out.subarray(s, i + 1)); s = i + 1; }
    }
    ex.web_midi_out_clear();
    while (asks.length && asks[0].until <= now) asks.shift().r(null);
    if (now >= figAt) {                                 /* FIGURES once a second, as the page asks */
        figAt += 1000;
        sendBytes(FG.ask(figFlags));                    /* (FREEZE / UNFREEZE with it, as the page sends them) */
        figFlags = 0;
    }
}
function take(m) {
    const st = AT.decodeState(m);
    if (st) {
        if (process.env.AT_TRACE) log.push(`  ${(now / 1000).toFixed(2)} ${st.screen} ov${st.overlay} cur ${st.cursor.slice(0, 3)} ` +
            `play ${+st.playing} what ${st.what} song ${st.song} keys ${st.cart.toString(16)}:${st.cart && st.slot.toString(16)} ` +
            `fx ${st.fx.toString(16)} dj ${st.dj.decks.map((d) => `${d.name}:${+d.playing}${+d.armed}`).join(" ")} unsaved ${st.unsaved} ` +
            `hold ${st.holdDelay} grace ${st.grace}` +
            (st.toast ? ` "${st.toast}"` : ""));
        if (++stSeen >= LOSE_K && stSeen < LOSE_K + LOSE_N) return;   /* (lost on the way) */
        if (st.num && stNum) stLost += ((st.num - stNum + 255) % 255 || 1) - 1;
        stNum = st.num;
        const a = asks.shift(); if (a) a.r(st); else stLate++;
        return;
    }
    const f = FG.decode(m);
    if (f) { lastFig = f; figures++; figHeard = now; if (measuring) measuring.push(f); return; }
    const c = FG.decode2(m);                            /* 37: core 2's window, into its 31's (right before it) */
    if (c) { if (FG.join(lastFig, c)) joined++; return; }
    const b = FG.decodeBench(m);
    if (b && benchWait) { benchWait.B.add(b); if (b.kind === "END") { const w = benchWait; benchWait = null; w.r(w.B); } }
}
function sendBytes(bytes) {
    if (process.env.AT_TRACE && bytes[0] !== 0xF0) log.push(`  ${(now / 1000).toFixed(3)} > ${Array.from(bytes, (x) => x.toString(16)).join(" ")}`);
    if (bytes[0] === 0xF0) {
        new Uint8Array(mem(), ex.web_sysex_stage(), bytes.length).set(bytes);
        if (ex.web_sysex_push(bytes.length) < 0) throw new Error("the device's SysEx queue is full");
    } else ex.web_midi_in(bytes[0], bytes[1] || 0, bytes[2] || 0, 0);
}

const io = {
    send: sendBytes,
    sleep: (ms) => new Promise((r) => sleepers.push({ at: now + ms, r })),
    ask: () => new Promise((r) => { asks.push({ until: now + 600, r }); sendBytes(AT.ASK); }),
    measure: () => { measuring = []; },
    measured: () => { const w = measuring || []; measuring = null; return w; },
    freeze: (on) => { figFlags |= on ? FG.FREEZE : FG.UNFREEZE; },
    bench: () => new Promise((r) => { benchWait = { B: FG.bench(), r }; sendBytes(FG.benchAsk(1)); }),
    prompt: async (text) => { log.push(`PROMPT: ${text}`); },
    log: (text) => { log.push(text); },
    stopped: () => STOP_AT > 0 && now >= STOP_AT,     /* AT_STOP_AT=s: STOP pressed then (the device's time) */
    usbIn: async () => ({ ok: true, name: "none here (no USB audio)", check: false }),
    record: async () => ({ ok: true, check: false, secs: 0 }),   /* (no recorder here: FILES' run, unrecorded) */
    now: () => now,                                     /* (the times in the report: the device's) */
    heard: () => now - figHeard,                        /* (the ms since the last FIGURES answer, as the page) */
    answers: () => ({ lost: stLost, late: stLate }),
};
const STOP_AT = +(process.env.AT_STOP_AT || 0) * 1000;

const noTime = AT.SCENARIOS.filter((s) => !(s.secs > 0)).map((s) => s.name);
if (noTime.length) { console.log(`FAIL scenarios without secs (the page's counter): ${noTime.join(", ")}`); process.exit(1); }
const list = only.length ? AT.SCENARIOS.filter((s) => only.includes(s.name)) : AT.SCENARIOS;
const R = AT.run(io, list, { scale: SCALE });
(async () => {
    const t0 = Date.now();
    while (!R.done) {
        await new Promise(setImmediate);                /* (what was waiting takes its turn) */
        sleepers.sort((a, b) => a.at - b.at);
        if (sleepers.length && sleepers[0].at <= now) { sleepers.shift().r(); continue; }
        render();
        if (now > 3600e3) throw new Error("an hour of the device's time: stuck");
    }
    const lines = R.lines();
    if (process.env.AT_LOG) console.log(log.join("\n") + "\nsongs: " + JSON.stringify(R.songNames));
    console.log(lines.join("\n"));
    /* the end, stopped or not: the FM-1 left stopped, silent, without changes or a question (specs/debug.md) */
    for (const until = now + 500; now < until;) render();
    const end = await new Promise((r) => {
        asks.push({ until: now + 600, r }); sendBytes(AT.ASK);
        (function spin() { if (asks.length) { render(); setImmediate(spin); } })();
    });
    /* (and at rest after it: the report's THE END line measured, no voice sounding, every effect resting; here the
     * emulator's clock stands still inside a half, so its FIGURES' stages read 0: only the voices are seen) */
    const endOk = end && !end.playing && !end.voices && !end.unsaved && !end.overlay && !R.endProblem &&
                  R.end && !R.end.voices && !R.end.awake.length;
    console.log(`the end: ${end ? `${end.screen}, playing ${+end.playing}, voices ${end.voices}, unsaved ${end.unsaved}, ` +
                `overlay ${end.overlay}, MIRROR ${+end.mirror}` : "NO STATE"}${R.endProblem ? `; ${R.endProblem}` : ""}` +
                `  ${endOk ? "ok" : "FAIL"}`);
    if (!endOk) process.exit(1);
    /* both cores in the figures: every 31 followed by its 37 (the window's number joining them); DJ HEAVY on two
     * cores hands blocks over (the emulator runs them where they are handed over), its ONE CORE twin none */
    const c2Blocks = (name) => ((R.results.find((r) => r.name === name) || {}).stretches || [])
        .flatMap((x) => x.windows || []).reduce((a, f) => a + (f.c2 ? f.c2.blocks : 0), 0);
    const ran = (name) => R.results.some((r) => r.name === name && r.status === "PASSED");
    console.log(`both cores: ${figures} FIGURES, ${joined} with their 37` +
                (ran("DJ HEAVY") ? `; DJ HEAVY ${c2Blocks("DJ HEAVY")} blocks on core 2` : "") +
                (ran("DJ HEAVY, ONE CORE") ? `, ONE CORE ${c2Blocks("DJ HEAVY, ONE CORE")}` : ""));
    if (joined !== figures || (ran("DJ HEAVY") && !(c2Blocks("DJ HEAVY") > 0)) ||
        (ran("DJ HEAVY, ONE CORE") && c2Blocks("DJ HEAVY, ONE CORE") !== 0)) {
        console.log("FAIL both cores: a FIGURES without its 37, or core 2's blocks not as SECOND CORE says"); process.exit(1);
    }
    const failed = R.results.filter((r) => r.status !== "PASSED" && !(STOP_AT && /STOPPED/.test(r.status + r.why)));
    console.log(`\nautotest: ${R.results.length} scenarios in ${(now / 1000).toFixed(0)} s of the device's time ` +
                `(${((Date.now() - t0) / 1000).toFixed(0)} s here); ${failed.length ? failed.length + " NOT PASSED: " +
                failed.map((r) => `${r.name} (${r.status})`).join(", ") : "all passed"}`);
    if (lines.some((l) => l.length >= 100 || !/^[\x20-\x7E]*$/.test(l))) { console.log("FAIL the report: plain ASCII, lines under 100"); process.exit(1); }
    /* the scenarios taking the FM-1's input ran first (the browser's prompt for it at the run's start) */
    const takes = R.results.map((r) => list.some((s) => s.name === r.name && s.group === r.group && s.input));
    if (takes.includes(true) && takes.lastIndexOf(true) > takes.indexOf(false) && takes.indexOf(false) >= 0) {
        console.log("FAIL the order: a scenario taking the input ran after one that does not"); process.exit(1);
    }
    /* a line too long goes on in the next, nothing of it cut (a failure's why and its "at" came cut off on the FM-1) */
    const long = "== UI: NEW SONG  FAILED: WAITED 2.0 S FOR A NEW SONG, UNSAVED (LAST: SONGS, NO OVERLAY, SONG \"PUNCH IN\", " +
                 "0 UNSAVED)  (00:01 setting up + 00:13, 3 checks, at SONGS)";
    const flat = (s) => s.replace(/\s+/g, " ").trim(), wrapped = AT.wrapLines([long]);
    if (wrapped.length < 2 || wrapped.some((l) => l.length >= 100) || flat(wrapped.join(" ")) !== flat(long)) {
        console.log("FAIL the report: a long line cut, not carried on in the next"); process.exit(1);
    }
    process.exit(failed.length ? 1 : 0);
})().catch((e) => { console.error(e); console.log(log.slice(-20).join("\n")); process.exit(2); });

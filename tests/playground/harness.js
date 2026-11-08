/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The playground without a browser: its scripts in a Node vm with a stand-in device (no sound: the
 * transport runs on the page's clock), driven by a scenario, the screen saved as PNGs (2x).
 *
 *   node tests/playground/harness.js <scenario.js> [out dir, default build/playground]
 *
 * A scenario is a module: async function (t) with t.down / up / tap(button), t.combo(a, b),
 * t.turn(knob, clicks), t.key / keyDown / keyUp(note 0-26), t.homeKey(note), t.shot(name), t.sleep,
 * t.ok(condition, what), and t.U, t.M, t.P (the UI, the model, the player). Its exit code is the
 * number of failed checks.
 *
 * A scenario whose function has .virtual set runs on a clock of its own: no waiting, the page's timers and the audio's
 * blocks each at its time, and t.sound() starts the sound as a page with no worklet does (audio.js's onPageThread, on a
 * stand-in AudioContext), so the worklet's pool of voices is there, its words back in the same order on every run. */
"use strict";
const fs = require("fs"), path = require("path"), vm = require("vm"), zlib = require("zlib");
const ROOT = path.resolve(__dirname, "../..");
const [scen, outArg] = process.argv.slice(2);
const out = outArg || path.join(ROOT, "build", "playground");
fs.mkdirSync(out, { recursive: true });
const scenario = require(path.resolve(scen));

/* the virtual clock: timers in ms, each run at its time in order of time, then of setting */
function virtualClock() {
    let now = 0, seq = 0, timers = [];
    const add = (fn, ms, every) => { timers.push({ fn, at: now + Math.max(ms || 0, 0), every, id: ++seq }); return seq; };
    const clear = id => { timers = timers.filter(x => x.id !== id); };
    function advance(ms) {
        const end = now + ms;
        for (;;) {
            let n = null;
            for (const x of timers) if (x.at <= end && (!n || x.at < n.at || (x.at === n.at && x.id < n.id))) n = x;
            if (!n) break;
            now = Math.max(now, n.at);
            if (n.every != null) n.at += n.every;
            else clear(n.id);
            n.fn();
        }
        now = end;
    }
    return { now: () => now, advance, setTimeout: (fn, ms) => add(fn, ms), setInterval: (fn, ms) => add(fn, ms, Math.max(ms, .1)), clear };
}
const vc = scenario.virtual ? virtualClock() : null;
/* the stand-in AudioContext: no worklet, so audio.js runs the worklet's code on the page's thread, a block at a time */
class VirtualAudio {
    constructor() { this.sampleRate = 44100; this.state = "running"; this.destination = {}; }
    get currentTime() { return vc.now() / 1000; }
    createChannelSplitter() { return { connect() {} }; }
    createAnalyser() { return { connect() {} }; }
    createScriptProcessor() {
        const sp = { connect() {}, onaudioprocess: null }, buf = [new Float32Array(128), new Float32Array(128)];
        const ev = { outputBuffer: { getChannelData: c => buf[c] }, playbackTime: 0 };
        vc.setInterval(() => { ev.playbackTime = vc.now() / 1000; if (sp.onaudioprocess) sp.onaudioprocess(ev); }, 128 / 44.1);
        return sp;
    }
}

const ctx = {
    console, atob, structuredClone,
    performance: vc ? { now: vc.now } : performance,
    setTimeout: vc ? vc.setTimeout : setTimeout, clearTimeout: vc ? vc.clear : clearTimeout,
    setInterval: vc ? vc.setInterval : setInterval, clearInterval: vc ? vc.clear : clearInterval,
    requestAnimationFrame: () => 0,
    ImageData: class { constructor(w, h) { this.data = new Uint8ClampedArray(w * h * 4); } },
    localStorage: { getItem() { return null; }, setItem() {} },
};
if (vc) ctx.AudioContext = VirtualAudio;
ctx.window = ctx;
vm.createContext(ctx);
const FILES = ["panel/layout.js", "playground/fonts.js", "playground/lcd.js", "playground/content.js", "playground/draw.js",
               "playground/model.js", "playground/cmds.js", "playground/help.js", "playground/dsp-core.js", "playground/dsp-dx7.js", "playground/dsp-synth.js",
               "playground/dsp-chip-tables.js", "playground/dsp-chip.js", "playground/dsp-fx.js", "playground/audio.js", "playground/player.js", "playground/ui.js", "playground/ctl.js", "playground/scr-grid.js",
               "playground/scr-songs.js", "playground/scr-editor.js", "playground/scr-instr.js", "playground/fx-pics.js", "playground/scr-play.js", "playground/scr-loops.js", "playground/scr-dj.js",
               "playground/scr-hear.js"];
for (const f of FILES) vm.runInContext(fs.readFileSync(path.join(ROOT, f), "utf8"), ctx, { filename: f });

const listeners = {}, leds = {};
const dev = {
    on(t, fn) { (listeners[t] = listeners[t] || []).push(fn); },
    led(id, v) { leds[id] = v; }, glass: { putImageData() {} }, present() {}, NOTE0: 14, LED_PLAY_GREEN: 41,
};
const emit = (t, ev) => { ev.type = t; for (const fn of (listeners[t] || []).concat(listeners["*"] || [])) fn(ev); };
const PT = ctx.PTFM, U = PT.ui;
const lcd = PT.createLcd(dev);
PT.model.demoDisk();                                    /* (the scenarios play the demo disk: DEMO DISK pressed first) */
U.attach(dev);
U.applySettings();
U.start();
if (vc) vc.setInterval(() => U.render(lcd, dev), 31);   /* (the page's frames: main.js's) */

const BTN = { "OCT-": 0, "OCT+": 1, FX: 2, SEL: 3, ENV: 4, LFO: 5, EDIT: 6, GLO: 7, HOME: 8, SAVE: 9, ARP: 10, SEQ: 11, PLAY: 12, REC: 13 };
const ENC = { SELECT: 0, ALGORITHM: 1, K1: 2, K2: 3, K3: 4, K4: 5, PRESETS: 6 };
const sleep = vc ? async ms => vc.advance(ms) : ms => new Promise(r => setTimeout(r, ms));

function png(file) {
    const S = 2, W = 240 * S, H = 240 * S, raw = Buffer.alloc((W * 3 + 1) * H);
    for (let y = 0; y < H; y++) {
        raw[y * (W * 3 + 1)] = 0;
        for (let x = 0; x < W; x++) {
            const c = lcd.gram[(y / S | 0) * 240 + (x / S | 0)], o = y * (W * 3 + 1) + 1 + x * 3;
            raw[o] = (c >> 11 & 31) * 255 / 31; raw[o + 1] = (c >> 5 & 63) * 255 / 63; raw[o + 2] = (c & 31) * 255 / 31;
        }
    }
    const T = [];
    for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; T[n] = c >>> 0; }
    const crc = b => { let c = ~0; for (const x of b) c = T[(c ^ x) & 255] ^ (c >>> 8); return ~c >>> 0; };
    const chunk = (t, d) => {
        const l = Buffer.alloc(4), c = Buffer.alloc(4), td = Buffer.concat([Buffer.from(t), d]);
        l.writeUInt32BE(d.length); c.writeUInt32BE(crc(td));
        return Buffer.concat([l, td, c]);
    };
    const ih = Buffer.alloc(13);
    ih.writeUInt32BE(W, 0); ih.writeUInt32BE(H, 4); ih[8] = 8; ih[9] = 2;
    fs.writeFileSync(file, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk("IHDR", ih),
                                          chunk("IDAT", zlib.deflateSync(raw)), chunk("IEND", Buffer.alloc(0))]));
}

let failed = 0;
const t = {
    U, M: PT.model, P: PT.player, PTFM: PT, leds, sleep,
    sound: () => PT.audio.start(),                      /* (a virtual scenario's: the worklet's code starts) */
    down: b => emit("button", { id: BTN[b], name: b, down: true }),
    up: b => emit("button", { id: BTN[b], name: b, down: false }),
    async tap(b) { t.down(b); await sleep(20); t.up(b); await sleep(160); },
    async combo(a, b) { t.down(a); await sleep(30); t.down(b); await sleep(30); t.up(b); await sleep(20); t.up(a); await sleep(160); },
    async turn(e, n) { for (let i = 0; i < Math.abs(n); i++) { emit("encoder", { id: ENC[e], name: e, step: Math.sign(n) }); await sleep(120); } },
    keyDown: n => emit("note", { note: n, down: true }),
    keyUp: n => emit("note", { note: n, down: false }),
    async key(n, ms = 60) { t.keyDown(n); await sleep(ms); t.keyUp(n); await sleep(40); },
    async homeKey(n) { t.down("HOME"); await sleep(30); t.keyDown(n); await sleep(30); t.keyUp(n); await sleep(20); t.up("HOME"); await sleep(160); },
    frame() { U.render(lcd, dev); },
    px: (x, y) => lcd.gram[y * 240 + x],                /* (after a frame or a shot: RGB565) */
    shot(name) { U.render(lcd, dev); png(path.join(out, name + ".png")); },
    top: () => U.overlays.length ? U.overlays[U.overlays.length - 1].kind : null,
    ok(c, what) { console.log((c ? "PASS " : "FAIL ") + what); if (!c) failed++; },
};
(async () => {
    try { await scenario(t); }
    catch (e) { console.error("SCENARIO FAILED:", e); failed++; }
    console.log(failed ? failed + " FAILED" : "ALL PASSED");
    process.exit(Math.min(failed, 100));
})();

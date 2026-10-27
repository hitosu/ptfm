/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The emulator's WebAssembly run through a session (web_session.txt) as web_native.c runs the same C
 * built natively; both print the same lines when the two builds do the same (specs/emulator.md,
 * Testing). Called as the worklet calls it: web_render in quanta of 128 frames, the panel's input
 * between them. A fake clock: each read 1 us later.
 *
 *   node web_test.js WASM SESSION FLASH|-          */
"use strict";
const fs = require("fs");
const [wasmPath, sessionPath, flashPath] = process.argv.slice(2);

const fnv = (h, bytes) => {
    for (let i = 0; i < bytes.length; i++) h = Math.imul(h ^ bytes[i], 16777619) >>> 0;
    return h;
};
const hex8 = v => v.toString(16).padStart(8, "0");

let fake = 0;
const { instance } = { instance: new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(wasmPath)),
    { env: { web_clock_us: () => (fake += 1) } }) };
const ex = instance.exports;
const mem = () => ex.memory.buffer;

ex.web_flash_reset();
if (flashPath !== "-") {
    const img = fs.readFileSync(flashPath);
    if (img.length !== 1 << 20) throw new Error(flashPath + ": not a 1 MiB flash image");
    new Uint8Array(mem(), ex.web_flash(), 1 << 20).set(img);
}
ex.web_init();
console.log("power on");
let audio = 2166136261, frames = 0, shot = 0, peak = 0;
for (const line of fs.readFileSync(sessionPath, "utf8").split("\n")) {
    if (line.startsWith("#")) continue;
    const w = line.trim().split(/\s+/);
    if (w.length < 2) continue;
    const at = +w[0], cmd = w[1], a = +(w[2] || 0), b = +(w[3] || 0);
    while (frames * 1000 < at * 44100) {
        ex.web_render(128);
        audio = fnv(audio, new Uint8Array(mem(), ex.web_out(), 128 * 8));
        for (const v of new Int32Array(mem(), ex.web_out(), 256)) if (Math.abs(v) > peak) peak = Math.abs(v);
        frames += 128;
    }
    if (cmd === "btn") ex.web_button(a, b);
    else if (cmd === "note") ex.web_note(a, b);
    else if (cmd === "knob") ex.web_knob(a, b);
    else if (cmd === "master") ex.web_master(a);
    else if (cmd === "shot") {
        const leds = new Uint8Array(mem(), ex.web_leds(), 42);
        const screen = fnv(2166136261, new Uint8Array(mem(), ex.web_fb(), 240 * 240 * 2));
        console.log(`shot ${shot++} at ${at} ms (device ${ex.web_now_ms()}): audio ${hex8(audio)} peak ${peak} screen ${hex8(screen)} leds ` +
            Array.from(leds, v => v.toString(16).padStart(2, "0")).join(""));
        peak = 0;
    }
}

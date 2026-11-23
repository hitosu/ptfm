/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The keys' velocity's twins (specs/hardware.md#the-keys-velocity, specs/panel.md): the playground's map against the
 * firmware's (the same checksum as fw/tests/keyvel_test.c's over every landing and curve), the panel's press point as a
 * landing (panel/device.js's keyReading), a key's velocity heard and written as the FM-1's platform gives the
 * firmware (a landing on the curve, a velocity given, none: KEYS VOLUME; the top row at the mean of the last four
 * read; CONSTANT), the arpeggiator taking it, and MIDI CONTROLLER's twin: its notes' velocity and general mode's
 * aftertouch (ctl.js against fw/core/ctl.h: the same steps' sum as fw/tests/ctl_test.c's, CHANNEL and POLY sent, none
 * at CONSTANT or in mirror mode).
 *   node tests/playground/harness.js tests/playground/velocity.js */
"use strict";
const fs = require("fs"), path = require("path");

module.exports = async function (t) {
    const { U, M, P, PTFM } = t;
    /* the map: the firmware's own integers */
    let sum = 0;
    for (let c = 1; c <= 3; c++) for (let d = 0; d <= 1023; d++) sum += M.keyVelocity(d, c) * (d + 1) * c;
    t.ok(sum === 211832164, "the map: the firmware's checksum over every landing and curve (" + sum + ")");
    /* the panel's press point: panel/device.js's own formula */
    const src = fs.readFileSync(path.join(__dirname, "../../panel/device.js"), "utf8");
    const m = /const keyReading = (h => [^;]+);/.exec(src);
    const keyReading = m ? require("vm").runInNewContext("(" + m[1] + ")") : null;   /* (the repository's own line) */
    t.ok(keyReading && keyReading(0) === 896 && keyReading(1) === 530 && M.keyVelocity(keyReading(0), 2) === 1 &&
         M.keyVelocity(keyReading(1), 2) === 127 && Math.abs(M.keyVelocity(keyReading(0.5), 2) - (1 + 126 * Math.pow(0.5, 0.5))) <= 2,
         "the panel: the top edge 896 (velocity 1), the bottom 530 (127), the middle the curve's middle");
    /* a key's velocity heard: the voices the keys start */
    const vols = {}, A = PTFM.audio, on0 = A.noteOn;   /* (every note the sound gets: the keys', the arpeggiator's) */
    A.noteOn = (id, time, midi, ref, vol, ...a) => { vols[id] = vol; return on0(id, time, midi, ref, vol, ...a); };
    const kv = U.set("KEYS VOLUME");
    t.ok(U.set("KEYS VELOCITY") === "NORMAL", "KEYS VELOCITY: NORMAL at first");
    t.keyDown(2, { reading: 455 }); await t.sleep(40);
    t.ok(vols.k2 === 127, "a key pressed at its bottom (455): heard at 127 (" + vols.k2 + ")");
    t.keyUp(2); await t.sleep(40);
    t.keyDown(3, { reading: 700 }); await t.sleep(40);
    const v700 = M.keyVelocity(700, 2);
    t.ok(vols.k3 === v700, "a key pressed higher (700): its velocity on NORMAL, " + v700 + " (" + vols.k3 + ")");
    t.keyUp(3); await t.sleep(40);
    t.keyDown(4); await t.sleep(40);
    t.ok(vols.k4 === kv, "a key pressed with no position (the computer's keyboard, a test): KEYS VOLUME");
    t.keyUp(4); await t.sleep(40);
    t.keyDown(5, { vel: 42 }); await t.sleep(40);
    t.ok(vols.k5 === 42, "a key given a velocity (an FM-1 as the controller): that one");
    t.keyUp(5); await t.sleep(40);
    t.keyDown(22, { reading: 600 }); await t.sleep(40);
    const mean = Math.floor((127 + v700 + 1) / 2);      /* (the two read: 455's 127 and 700's; the one given not read) */
    t.ok(vols.k22 === mean, "the top row: the mean of the keys read (" + vols.k22 + ", " + mean + ")");
    t.keyUp(22); await t.sleep(40);
    M.state.settings["KEYS VELOCITY"] = "SOFT";
    t.keyDown(3, { reading: 700 }); await t.sleep(40);
    t.ok(vols.k3 === M.keyVelocity(700, 1) && vols.k3 > v700, "SOFT: the same press louder (" + vols.k3 + ")");
    t.keyUp(3); await t.sleep(40);
    M.state.settings["KEYS VELOCITY"] = "CONSTANT";
    t.keyDown(3, { reading: 455 }); await t.sleep(40);
    t.ok(vols.k3 === kv, "CONSTANT: at KEYS VOLUME whatever the press");
    t.keyUp(3); await t.sleep(40);
    M.state.settings["KEYS VELOCITY"] = "NORMAL";
    /* the arpeggiator: its notes at the last key's velocity */
    t.tap("ARP"); await t.sleep(400);
    t.ok(U.arp, "ARP on");
    t.keyDown(7, { reading: 800 }); await t.sleep(700);
    const va = M.keyVelocity(800, 2);
    t.ok(vols.pr === va, "the arpeggiator: its notes at the key's velocity (" + vols.pr + ", " + va + ")");
    t.keyUp(7); await t.sleep(40);
    t.tap("ARP"); await t.sleep(400);
    A.noteOn = on0;

    /* the tracker's pressure (ui.c's keys_press, press.h's steps as the controller's): a held key pressed harder, its own
     * notes louder, from its velocity's volume toward 127 at the most; eased, as it was; none at CONSTANT, nor on the top
     * row (the frames' pressTick stepped by hand: the harness has no frames) */
    {
        const calls = [], kp0 = A.keyPress;
        A.keyPress = (k, g) => calls.push([k, g]);
        const steps = async (ms) => { for (let e = 0; e < ms; e += 5) { await t.sleep(5); U.pressTick(); } };
        const of = (k) => calls.filter(c => c[0] === k);
        M.state.settings["KEYS VELOCITY"] = "NORMAL";
        t.keyDown(9, { reading: 600 }); await steps(400);
        const v9 = U.keyVol(9), rest = of(9).length;
        t.pressure(9, 470); await steps(200);
        const up = of(9).slice(rest), top = up.length ? up[up.length - 1][1] : 0;
        t.ok(v9 < 127 && up.length && top > 1 && top <= 127 / v9 + 0.01 && up.every((c, i) => !i || c[1] >= up[i - 1][1]),
             "the tracker's: a held key pressed harder, its notes louder (x" + top.toFixed(2) + ", rising, at most 127 / " + v9 + ")");
        t.pressure(9, 600); await steps(300);
        t.ok(of(9).pop()[1] === 1, "... eased: as it was (x1)");
        t.keyUp(9); await steps(20);
        M.state.settings["KEYS VELOCITY"] = "CONSTANT"; calls.length = 0;
        t.keyDown(9, { reading: 600 }); await steps(300); t.pressure(9, 470); await steps(200); t.keyUp(9); await steps(20);
        t.ok(!of(9).some(c => c[1] !== 1), "... KEYS VELOCITY CONSTANT: none");
        M.state.settings["KEYS VELOCITY"] = "NORMAL"; calls.length = 0;
        t.keyDown(20, { reading: 600 }); await steps(300); t.pressure(20, 470); await steps(200); t.keyUp(20); await steps(20);
        t.ok(!of(20).some(c => c[1] !== 1), "... the top row (C#5): none");
        /* what a recorder takes (a screen's pressure: the EDITOR's take, LOOPS' stage): nothing while steady; pressed
         * harder, each change of 4 or more; eased, its velocity again */
        const got = [], sc = U.screen, had = sc.pressure;
        sc.pressure = (n, v) => got.push([n, v]);
        M.state.settings["KEYS VELOCITY"] = "NORMAL";
        t.keyDown(9, { reading: 600 }); await steps(400);
        const steady = got.length, v0 = U.keyVol(9);
        t.pressure(9, 470); await steps(250);
        const up2 = got.slice(steady).map(x => x[1]);
        t.pressure(9, 600); await steps(400);
        const last2 = got.length ? got[got.length - 1][1] : 0;
        t.keyUp(9); await steps(20);
        sc.pressure = had;
        t.ok(steady === 0 && up2.length && up2.every((v, i) => (i ? Math.abs(v - up2[i - 1]) : Math.abs(v - v0)) >= 4) &&
             up2[up2.length - 1] > v0 && last2 === v0,
             "... a recorder takes nothing while steady, each change of 4 or more (" + up2.join(" ") + "), back at " + v0 + " when eased");
        A.keyPress = kp0;
    }
    /* a lane's volume alone glides over its row (the FM-1's pl_lp_ramps): the worklet's own code, a voice gliding from 0.5
     * to 1 over 0.125 s: each block a share of the way, none bigger, there at the end */
    {
        let now = 0;
        const G = PTFM.dsp[0].constructor("return this")();   /* (the pages' realm: the worklet's globals there) */
        const keep = { sampleRate: G.sampleRate, AudioWorkletProcessor: G.AudioWorkletProcessor, registerProcessor: G.registerProcessor };
        G.sampleRate = 44100;
        Object.defineProperty(G, "currentTime", { get: () => now, configurable: true });
        G.AudioWorkletProcessor = class { constructor() { this.port = { postMessage: () => {}, onmessage: null }; } };
        G.registerProcessor = () => {};
        const E = {};
        for (const f of PTFM.dsp) f(E);
        const proc = new E.Proc(), v = { id: "g", vol: 0.5 };
        proc.vs.push(v);
        proc.port.onmessage({ data: { t: "ctl", id: "g", time: 0, bend: 0, vol: 1, glide: 0.125 } });
        proc.byId.set("g", v);
        const L = new Float32Array(128), R = new Float32Array(128), vs = [];
        for (let k = 0; k < 60; k++) { now = k * 128 / 44100; proc.process([], [[L, R]]); vs.push(v.vol); }
        const share = 0.5 * 128 / (0.125 * 44100), steps = vs.map((x, k) => x - (k ? vs[k - 1] : 0.5));
        const n = vs.findIndex(x => x === 1);
        t.ok(steps.every(d => d >= -1e-9 && d <= share + 1e-9) && n >= 40 && n <= 44 && vs[vs.length - 1] === 1,
             "a lane's volume glides: a share a block (" + share.toFixed(4) + ", the most " + Math.max(...steps).toFixed(4) +
             "), there after " + n + " blocks of 128 (0.125 s)");
        Object.assign(G, keep);
    }
    /* MIDI CONTROLLER's twin */
    const C = PTFM.ctl;
    let w = 0, s2 = 0, p;
    for (let tt = 0; tt < 2000; tt += 5) {
        const d = tt < 1500 ? 600 - (tt > 400 ? Math.floor((tt - 400) / 8) : 0) + (tt > 1000 ? Math.floor((tt - 1000) / 3) : 0) : 0;
        [w, p] = C.press(w, d, 5);
        s2 = (Math.imul(s2, 31) + p) >>> 0;
    }
    t.ok(s2 === 0xE1148EDF, "the pressure's steps: the sum fw/core/press.h's give (0x" + s2.toString(16) + ")");
    const session = (mode, at, velocity) => {
        M.state.settings["CONTROLLER AFTERTOUCH"] = at;
        M.state.settings["KEYS VELOCITY"] = velocity;
        C.start();
        C.button(mode === "GENERAL" ? 8 : 13, true); C.button(mode === "GENERAL" ? 8 : 13, false);
        let now = 1000;
        C.note(5, true, 88, 600);
        for (; now < 1300; now += 1) C.poll(now);
        C.pressure(5, 470);
        for (; now < 1500; now += 1) C.poll(now);
        const st = C.state(), log = st.log.slice();
        C.note(5, false);
        return { log, after: C.state().log.slice(), st };
    };
    let r = session("GENERAL", "CHANNEL", "NORMAL");
    const on = r.log.find(e => e.kind === "on"), pr = r.log.filter(e => e.kind === "pressure");
    t.ok(on && on.bytes[2] === 88, "MIDI CONTROLLER's twin: a key's Note On at its velocity (88)");
    t.ok(pr.length && pr[pr.length - 1].val > 40 && pr[pr.length - 1].bytes.length === 2 && (pr[0].bytes[0] & 0xF0) === 0xD0,
         "... GENERAL, CHANNEL: a held key pressed harder sends Channel Pressure (" + (pr.length ? pr[pr.length - 1].val : "-") + ")");
    r = session("GENERAL", "POLY", "NORMAL");
    const po = r.log.filter(e => e.kind === "poly"), last = r.after.slice(-2);
    t.ok(po.length && (po[0].bytes[0] & 0xF0) === 0xA0 && po[0].bytes[1] === 53 + 5 && last[0].kind === "poly" && last[0].val === 0 &&
         last[1].kind === "off", "... POLY: Polyphonic Key Pressure on its note, 0 before its Note Off");
    r = session("GENERAL", "CHANNEL", "CONSTANT");
    t.ok(!r.log.some(e => e.kind === "pressure"), "... KEYS VELOCITY CONSTANT: none");
    r = session("MIRROR", "CHANNEL", "NORMAL");
    t.ok(!r.log.some(e => e.kind === "pressure" || e.kind === "poly"), "... mirror mode: none");
    M.state.settings["CONTROLLER AFTERTOUCH"] = "CHANNEL";
    M.state.settings["KEYS VELOCITY"] = "NORMAL";
};

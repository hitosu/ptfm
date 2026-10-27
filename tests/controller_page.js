/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* panel/midi.js, the page's side of MIDI CONTROLLER (specs/controller.md), in Node: a stand-in
 * device (press / release by source, LEDs, knobs, MASTER, as panel/device.js), a fake Web MIDI with a
 * fake FM-1 on it, and the protocol's table that the firmware's test reads too
 * (fw/tests/vectors.json).
 *
 *   node tests/controller_page.js        exit code: the number of failed checks */
"use strict";
const fs = require("fs"), path = require("path"), vm = require("vm");
const ROOT = path.resolve(__dirname, "..");
const V = JSON.parse(fs.readFileSync(path.join(ROOT, "fw/tests/vectors.json"), "utf8"));

let failed = 0, passed = 0;
function ok(cond, what) {
    if (cond) passed++;
    else { failed++; console.log("FAIL", what); }
}
const same = (a, b) => JSON.stringify([...a]) === JSON.stringify([...b]);
const tick = () => new Promise(r => setImmediate(r));
const STAMP = 4321.5;                                   /* every message's time stamp: knob clicks carry it on */
const sleep = ms => new Promise(r => setTimeout(r, ms));

/* midi.js in its own context, as a page loads it */
const ctx = { console, queueMicrotask, setTimeout, clearTimeout, PTFM: {} };
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(ROOT, "panel/midi.js"), "utf8"), ctx, { filename: "panel/midi.js" });
const attachMidi = ctx.PTFM.attachMidi;

/* the stand-in device: what midi.js uses of panel/device.js */
function device() {
    const listeners = {}, src = [], level = [], log = [];
    const d = {
        NOTE0: 14, LED_PLAY_GREEN: 41, log,
        on(t, fn) { (listeners[t] = listeners[t] || []).push(fn); },
        led(id, v) {
            if ((level[id] || 0) === v) return;
            level[id] = v;
            for (const fn of listeners.led || []) fn({ id, level: v, type: "led" });
        },
        press(id, s) { (src[id] = src[id] || new Set()).add(s); log.push({ press: id, src: s }); },
        release(id, s) { if (src[id] && src[id].delete(s)) log.push({ release: id, src: s }); },
        turn(id, step, t) { log.push({ turn: id, step, t }); },
        setMaster(v) { log.push({ master: v }); },
        down: id => !!(src[id] && src[id].size),
        heldBy: (id, s) => !!(src[id] && src[id].has(s)),
    };
    return d;
}

/* a fake Web MIDI: ports with names, and an FM-1 behind one input / output pair that answers the
 * question with its mode; messages arrive later, as in a browser */
function webMidi() {
    const inputs = new Map(), outputs = new Map();
    const access = { inputs, outputs, onstatechange: null };
    const change = port => { if (access.onstatechange) access.onstatechange({ port }); };
    function input(id, name) {
        const p = { id, name, type: "input", state: "connected", onmidimessage: null,
                    deliver(data) { setImmediate(() => { if (p.onmidimessage) p.onmidimessage({ data: Uint8Array.from(data), timeStamp: STAMP }); }); } };
        return p;
    }
    function output(id, name, onSend) {
        return { id, name, type: "output", state: "connected", sent: [], send(data) { this.sent.push([...data]); if (onSend) onSend([...data]); } };
    }
    const net = {
        access, inputs, outputs,
        add(port) { (port.type === "input" ? inputs : outputs).set(port.id, port); change(port); },
        remove(port) { port.state = "disconnected"; (port.type === "input" ? inputs : outputs).delete(port.id); change(port); },
        /* an FM-1 named `name` in `mode`; returns its ports */
        fm1(name, mode, id = "fm1") {
            const i = input(id + "-in", name);
            const o = output(id + "-out", name, data => { if (same(data, V.handshake.ask)) i.deliver(net.answer(mode)); });
            return { i, o, mode };
        },
        answer: mode => V.handshake.answer[typeof mode === "string" ? mode : mode === 0 ? "mirror" : "general"],
        other(name, id) { return { i: input(id + "-in", name), o: output(id + "-out", name) }; },
        input, output,
    };
    return net;
}
function store(init) {
    const m = new Map(init ? [["ptfm.midi", init]] : []);
    return { getItem: k => m.has(k) ? m.get(k) : null, setItem: (k, v) => m.set(k, String(v)), m };
}

async function setup(mode = 0, opts = {}) {
    const net = webMidi(), dev = device(), states = [];
    const fm = net.fm1("FM-1 PT/FM",mode);
    const synth = net.other("Some Synth", "synth");
    for (const p of [fm.i, fm.o, synth.i, synth.o]) (p.type === "input" ? net.inputs : net.outputs).set(p.id, p);
    let requests = 0;
    const midi = attachMidi(dev, {
        requestMIDIAccess: o => { requests++; ok(o && o.sysex === true, "asks for SysEx"); return Promise.resolve(net.access); },
        storage: opts.storage || store(),
        onState: (s, label) => states.push([s, label]),
    });
    if (opts.enable !== false) { await midi.enable(); await tick(); await tick(); }
    return { net, dev, fm, synth, midi, states, requests: () => requests };
}
const ledMsgs = sent => sent.filter(m => m[0] !== 0xF0);

(async () => {
    /* ---- the handshake: asked on every output, the answer pairs, LEDs once */
    {
        const { net, dev, fm, synth, midi, states } = await setup(0);
        ok(same(fm.o.sent[0], V.handshake.ask), "the question, as the table has it");
        ok(same(synth.o.sent[0], V.handshake.ask), "asked on every output");
        ok(midi.state === "mirror" && midi.label === "FM-1 MIRROR", "paired in mirror mode: " + midi.label);
        ok(states[0][1] === "LOOKING" && states.at(-1)[1] === "FM-1 MIRROR", "states LOOKING, then FM-1 MIRROR");
        const leds = ledMsgs(fm.o.sent);
        ok(leds.length === 42, `every LED once at pairing (${leds.length} of 42)`);
        ok(leds.every(m => m[2] === 0), "all off at first");
        ok(ledMsgs(synth.o.sent).length === 0, "no LEDs to other gear");
        void net; void dev;
    }

    /* ---- mirror_out: every message from the FM-1 lands on its control */
    {
        const { dev, fm } = await setup(0);
        for (const v of V.mirror_out) {
            dev.log.length = 0;
            fm.i.deliver(v.midi);
            await tick();
            const p = v.panel, log = dev.log;
            if (p.press !== undefined) ok(log.length === 1 && log[0].press === p.press && log[0].src === "midi", `press ${p.press} from ${v.midi}`);
            else if (p.release !== undefined) ok(log.length === 1 && log[0].release === p.release && log[0].src === "midi", `release ${p.release} from ${v.midi}`);
            else if (p.turn !== undefined) ok(log.length === Math.abs(p.clicks) && log.every(e => e.turn === p.turn && e.step === Math.sign(p.clicks) && e.t === STAMP),
                                              `knob ${p.turn}: ${p.clicks} clicks from ${v.midi}, at the message's time stamp (got ${log.length})`);
            else if (p.master !== undefined) ok(log.length === 1 && Math.abs(log[0].master - p.master / 127) < 1e-9, `MASTER ${p.master} from ${v.midi}`);
        }
    }

    /* ---- mirror_leds: the panel's LEDs go out as the table has them; only changes */
    {
        const { dev, fm } = await setup(0);
        for (const v of V.mirror_leds) {
            const id = v.panel.led;
            dev.led(id, v.panel.level === 0 ? 1 : 0);       /* first somewhere else, so the level is a change */
            await tick();
            fm.o.sent.length = 0;
            dev.led(id, v.panel.level);
            await tick();
            ok(fm.o.sent.length === 1 && same(fm.o.sent[0], v.midi), `LED ${id} at ${v.panel.level} -> ${v.midi} (got ${JSON.stringify(fm.o.sent)})`);
        }
        fm.o.sent.length = 0;
        dev.led(20, 1); dev.led(20, .55); await tick();
        ok(fm.o.sent.length === 1, "a level that sends the same value again sends nothing more");
        fm.o.sent.length = 0;
        dev.led(5, 1); dev.led(5, 0); dev.led(5, 1); await tick();
        ok(fm.o.sent.length === 1 && same(fm.o.sent[0], [191, 107, 127]), "set three times in a frame: one message, where it ends");
        fm.o.sent.length = 0;
        dev.led(5, 1); await tick();
        ok(fm.o.sent.length === 0, "unchanged: nothing sent");
    }

    /* ---- another channel (the answer's, version 2): mirror_channel_5 both ways; 16 is then not ours */
    {
        const { dev, fm, midi } = await setup("mirror_channel_5");
        ok(midi.state === "mirror", "paired on channel 5");
        const leds = ledMsgs(fm.o.sent);
        ok(leds.length === 42 && leds.every(m => (m[0] & 15) === 4), "every LED at pairing, on channel 5");
        for (const v of V.mirror_channel_5) {
            if (v.event) {
                dev.log.length = 0;
                fm.i.deliver(v.midi);
                await tick();
                const p = v.panel, log = dev.log;
                ok(log.length >= 1 && (p.press === undefined || log[0].press === p.press) &&
                   (p.turn === undefined || log[0].turn === p.turn) &&
                   (p.master === undefined || Math.abs(log[0].master - p.master / 127) < 1e-9), `channel 5: ${v.midi} lands`);
            } else {
                dev.led(v.panel.led, 0);
                await tick();
                fm.o.sent.length = 0;
                dev.led(v.panel.led, v.panel.level);
                await tick();
                ok(fm.o.sent.length === 1 && same(fm.o.sent[0], v.midi), `channel 5: LED ${v.panel.led} -> ${v.midi}`);
            }
        }
        dev.log.length = 0;
        fm.i.deliver([159, 60, 127]);
        await tick();
        ok(dev.log.length === 0, "channel 5: a message on 16 does nothing");
    }

    /* ---- a version 1 answer (9 bytes, no channel): channel 16 */
    {
        const { dev, fm, midi } = await setup(0);
        fm.i.deliver([240, 125, 80, 84, 67, 2, 0, 1, 247]);
        await tick();
        dev.log.length = 0;
        fm.i.deliver([159, 60, 127]);
        await tick();
        ok(midi.state === "mirror" && dev.log.length === 1 && dev.log[0].press === 21, "version 1's answer: channel 16");
    }

    /* ---- only the paired input reaches the panel */
    {
        const { dev, synth } = await setup(0);
        dev.log.length = 0;
        synth.i.deliver([159, 60, 127]);
        synth.i.deliver([191, 22, 1]);
        await tick();
        ok(dev.log.length === 0, "another port's notes and CCs do nothing");
    }

    /* ---- a hello pairs again: what MIDI held is let go, the LEDs go again; a mouse's hold stays */
    {
        const { net, dev, fm } = await setup(0);
        fm.i.deliver([159, 60, 127]);
        fm.i.deliver([191, 114, 127]);
        await tick();
        dev.press(21, "p1");                                /* the mouse on the same key */
        ok(dev.heldBy(21, "midi") && dev.down(12), "held from MIDI");
        dev.led(30, 1);
        await tick();
        fm.o.sent.length = 0;
        fm.i.deliver(net.answer(0));                        /* unasked: a hello */
        await tick();
        ok(!dev.heldBy(21, "midi") && !dev.down(12), "a hello lets go of what MIDI held");
        ok(dev.heldBy(21, "p1"), "the mouse still holds its key");
        const leds = ledMsgs(fm.o.sent);
        ok(leds.length === 42 && leds.some(m => same(m, [159, 69, 127])), "every LED again, as they are");
    }

    /* ---- the FM-1 goes away: let go, LOOKING; it comes back: paired again */
    {
        const { net, dev, fm, midi } = await setup(0);
        fm.i.deliver([159, 53, 127]);
        await tick();
        net.remove(fm.i);
        ok(!dev.heldBy(14, "midi"), "a disconnect lets go");
        ok(midi.state === "looking" && midi.label === "LOOKING", "LOOKING after a disconnect");
        net.remove(fm.o);
        const back = net.fm1("FM-1 PT/FM",0, "fm1b");
        back.i.onmidimessage = null;
        net.add(back.o);                                    /* the output first */
        await tick();
        net.add(back.i);                                    /* then the input: asked once both are there */
        ok(back.o.sent.length === 0, "not asked while the ports are still coming");
        await sleep(150);
        ok(midi.state === "mirror", "paired again when it comes back");
        ok(ledMsgs(back.o.sent).length === 42, `its LEDs sent (${ledMsgs(back.o.sent).length} of 42)`);
        dev.log.length = 0;
        back.i.deliver([159, 53, 127]);
        await tick();
        ok(dev.heldBy(14, "midi"), "the new port works the panel");
    }

    /* ---- general mode: shown, ignored */
    {
        const { dev, fm, midi } = await setup(1);
        ok(midi.state === "general" && midi.label === "FM-1 GENERAL (ignored)", "general mode shown: " + midi.label);
        ok(ledMsgs(fm.o.sent).length === 0, "no LEDs to a general-mode FM-1");
        dev.log.length = 0;
        fm.i.deliver([159, 60, 127]);
        fm.i.deliver([191, 114, 127]);
        fm.i.deliver([191, 22, 1]);
        await tick();
        ok(dev.log.length === 0, "its messages do nothing");
        dev.led(3, 1);
        await tick();
        ok(ledMsgs(fm.o.sent).length === 0, "LED changes not sent to it");
    }

    /* ---- remembered: on at the next load, off when turned off */
    {
        const s = store();
        const a = await setup(0, { storage: s });
        ok(s.m.get("ptfm.midi") === "on", "turning it on is remembered");
        const b = await setup(0, { storage: s, enable: false });
        await tick(); await tick();
        ok(b.requests() === 1 && b.midi.state === "mirror", "on by itself at the next load");
        b.midi.disable();
        ok(s.m.get("ptfm.midi") === "off" && b.midi.state === "off" && b.midi.label === "MIDI: OFF", "off: remembered, MIDI: OFF");
        const c = await setup(0, { storage: s, enable: false });
        ok(c.requests() === 0 && c.midi.state === "off", "stays off at the next load");
        void a;
    }

    /* ---- turned off: the panel lets go, nothing more comes through */
    {
        const { dev, fm, midi } = await setup(0);
        fm.i.deliver([159, 79, 127]);
        await tick();
        midi.disable();
        ok(!dev.heldBy(40, "midi"), "turning off lets go");
        dev.log.length = 0;
        fm.i.deliver([159, 79, 127]);
        await tick();
        ok(dev.log.length === 0 && fm.i.onmidimessage === null, "nothing after turning off");
    }

    /* ---- no Web MIDI, or not allowed: off, with the reason */
    {
        const states = [];
        const m = attachMidi(device(), { storage: store(), requestMIDIAccess: () => Promise.reject(new Error("denied")),
                                         onState: (s, l, why) => states.push([s, why]) });
        await m.enable();
        ok(m.state === "off" && /denied/.test(states.at(-1)[1]), "refused: off, with the reason");
    }

    console.log(`controller page: ${passed} checks passed, ${failed} failed`);
    process.exit(failed);
})();

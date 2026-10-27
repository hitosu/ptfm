/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM panel: the real FM-1 as the virtual one's controller, over Web MIDI, with the controller
 * firmware in its mirror mode (specs/controller.md: the protocol, the pairing).
 *
 * const midi = PTFM.attachMidi(dev, {onState}) watches dev's LEDs from now on, and returns:
 *   midi.enable(), midi.disable(), midi.toggle()
 *   midi.state          "off", "looking", "mirror" or "general"; midi.label its words for the page
 *   onState(state, label, why)   at every change (why: the reason MIDI could not start, if so)
 * It turns itself on when the page had it on last time (the browser's storage).
 *
 * Pairing: the page asks every output (F0 7D 50 54 43 01 F7) when turned on and whenever a port
 * appears (once a device's ports have all come); the input that answers (F0 7D 50 54 43 02 mode
 * version channel F7; version 1 had no channel: 16) is paired with the output of the same name, and
 * mirror mode's messages both ways use that channel. The FM-1 also answers unasked when its USB comes up (a hello): handled alike. At
 * every answer the controls held from MIDI are let go (the FM-1 then sends what is held), and in
 * mirror mode every LED is sent once. Only the paired input reaches the panel, and only in mirror
 * mode: its keys and buttons press and release with the source "midi" (a control also held by the
 * mouse stays down until both let go), its knobs click, CC 7 is MASTER. LED changes go back,
 * gathered so a level set twice in one frame sends only where it ends.
 *
 * Tests inject their own Web MIDI and storage: opts.requestMIDIAccess, opts.storage. */
(function () {
    "use strict";
    const NOTE_ON = 0x90, NOTE_OFF = 0x80, CC = 0xB0;       /* | the channel the answer gives (0-15) */
    const NOTE_F3 = 53, KEYS = 27;                           /* the keys F3-G5: notes 53-79 */
    const CC_BUTTON = 102, CC_KNOB = 20, CC_MASTER = 7, CC_PLAY_GREEN = 116;
    const BUTTONS = 14, KNOBS = 7;
    const ASK = [0xF0, 0x7D, 0x50, 0x54, 0x43, 0x01, 0xF7];
    const ANSWER = [0xF0, 0x7D, 0x50, 0x54, 0x43, 0x02];    /* then mode, version, (version 2) channel, F7 */
    const LABEL = { off: "MIDI: OFF", looking: "LOOKING", mirror: "FM-1 MIRROR", general: "FM-1 GENERAL (ignored)" };
    const STORE = "ptfm.midi";
    const ASK_AFTER_MS = 100;                                /* a port appeared: ask when its device's others have too */

    /* an LED's level (0-1) as the FM-1 takes it: 0 off, 1-63 dim, 64-127 lit */
    const ledValue = level => level >= 0.5 ? 127 : level > 0 ? 32 : 0;

    PTFM.attachMidi = function (dev, opts = {}) {
        const onState = opts.onState || (() => {});
        const request = opts.requestMIDIAccess ||
            (typeof navigator !== "undefined" && navigator.requestMIDIAccess ? o => navigator.requestMIDIAccess(o) : null);
        const storage = opts.storage || (() => { try { return localStorage; } catch (_) { return null; } })();
        const LEDS = dev.LED_PLAY_GREEN + 1;                 /* buttons, keys, PLAY's green */
        let access = null, input = null, output = null, mode = -1, ch = 15, state = "off", why = "", askTimer = 0;
        const known = new Set();                             /* ids of the ports seen connected */
        const held = new Set();                              /* control ids held from MIDI */
        const level = [], sent = [];                         /* by LED id: the panel's level, the value sent */
        let flushing = false;

        const api = { enable, disable, toggle, get state() { return state; }, get label() { return LABEL[state]; } };

        function setState(s, reason = "") {
            if (s === state && reason === why) return;
            state = s;
            why = reason;
            onState(state, LABEL[state], why);
        }
        function remember(on) {
            try { if (storage) storage.setItem(STORE, on ? "on" : "off"); } catch (_) { /* storage off: fine */ }
        }

        /* ---- the panel's side */
        function press(id) { held.add(id); dev.press(id, "midi"); }
        function release(id) { held.delete(id); dev.release(id, "midi"); }
        function releaseAll() { for (const id of [...held]) release(id); }

        function ledMessage(id, v) {
            if (id === dev.LED_PLAY_GREEN) return [CC | ch, CC_PLAY_GREEN, v];
            if (id < BUTTONS) return [CC | ch, CC_BUTTON + id, v];
            return [NOTE_ON | ch, NOTE_F3 + id - dev.NOTE0, v];
        }
        function sendLed(id, always) {
            const v = ledValue(level[id] || 0);
            if (!always && sent[id] === v) return;
            sent[id] = v;
            try { output.send(ledMessage(id, v)); } catch (_) { /* the port just went: its statechange follows */ }
        }
        function sendAllLeds() {
            if (!output) return;
            for (let id = 0; id < LEDS; id++) if (id < BUTTONS || id >= dev.NOTE0) sendLed(id, true);
        }
        dev.on("led", ev => {
            level[ev.id] = ev.level;
            if (state !== "mirror" || !output || flushing) return;
            flushing = true;
            queueMicrotask(() => {                           /* after the frame: only where the levels end */
                flushing = false;
                if (state !== "mirror" || !output) return;
                for (let id = 0; id < LEDS; id++) if (id < BUTTONS || id >= dev.NOTE0) sendLed(id, false);
            });
        });

        /* ---- the FM-1's side */
        function isAnswer(d) {                              /* version 1: 9 bytes (channel 16); 2: 10 */
            return (d.length === 9 || d.length === 10) && ANSWER.every((b, i) => d[i] === b) && d[d.length - 1] === 0xF7;
        }
        function paired(port, m, c) {
            releaseAll();
            input = port;
            output = null;
            if (access) for (const o of access.outputs.values()) if (o.name === port.name && o.state !== "disconnected") output = o;
            mode = m;
            ch = c & 15;
            sent.length = 0;                                 /* (the LEDs go again, on this channel) */
            if (mode === 0) {
                setState("mirror");
                sendAllLeds();
            } else setState("general");
        }
        function message(port, d, t) {                      /* t: the message's time stamp */
            if (d[0] === 0xF0) {
                if (isAnswer(d)) paired(port, d[6], d.length === 10 ? d[8] : 15);
                return;
            }
            if (!input || port.id !== input.id || mode !== 0 || d.length < 3 || (d[0] & 15) !== ch) return;
            const [st, a, v] = d, s = st & 0xF0;
            if (s === NOTE_ON || s === NOTE_OFF) {
                const k = a - NOTE_F3;
                if (k < 0 || k >= KEYS) return;
                if (s === NOTE_ON && v > 0) press(dev.NOTE0 + k);
                else release(dev.NOTE0 + k);
            } else if (s === CC) {
                if (a >= CC_BUTTON && a < CC_BUTTON + BUTTONS) {
                    if (v >= 64) press(a - CC_BUTTON);
                    else release(a - CC_BUTTON);
                } else if (a >= CC_KNOB && a < CC_KNOB + KNOBS) {
                    if (v === 0 || v === 64) return;
                    const clicks = v < 64 ? v : v - 128;     /* relative: 1-63 clockwise, 127-65 back */
                    for (let i = 0; i < Math.abs(clicks); i++) dev.turn(a - CC_KNOB, Math.sign(clicks), t);
                } else if (a === CC_MASTER) dev.setMaster(v / 127);
            }
        }

        function listen(port) {
            port.onmidimessage = ev => message(port, ev.data, ev.timeStamp);
        }
        function ask(port) {
            try { port.send(ASK); } catch (_) { /* a port that will not take SysEx: not ours */ }
        }
        function lost() {
            releaseAll();
            input = output = null;
            mode = -1;
            setState("looking");
        }
        function portChange(ev) {
            const p = ev.port;
            if (p.state === "disconnected") {
                known.delete(p.id);
                if ((input && p.id === input.id) || (output && p.id === output.id)) lost();
                return;
            }
            if (p.state !== "connected" || known.has(p.id)) return;
            known.add(p.id);
            if (p.type === "input") listen(p);
            /* a device's ports come one after another: ask once they are all there (an input that
             * came after its output would miss the answer) */
            clearTimeout(askTimer);
            askTimer = setTimeout(() => { if (access) for (const o of access.outputs.values()) ask(o); }, ASK_AFTER_MS);
        }

        async function enable() {
            if (state !== "off") return api;
            remember(true);
            if (!request) {
                setState("off", "this browser has no Web MIDI");
                return api;
            }
            setState("looking");
            let a;
            try {
                a = await request({ sysex: true });
            } catch (e) {
                setState("off", "MIDI not allowed: " + (e && e.message || e));
                return api;
            }
            if (state !== "looking") return api;             /* turned off while waiting */
            access = a;
            access.onstatechange = portChange;
            for (const i of access.inputs.values()) { known.add(i.id); listen(i); }
            for (const o of access.outputs.values()) { known.add(o.id); ask(o); }
            return api;
        }
        function disable() {
            remember(false);
            clearTimeout(askTimer);
            if (access) {
                access.onstatechange = null;
                for (const i of access.inputs.values()) i.onmidimessage = null;
            }
            releaseAll();
            access = input = output = null;
            mode = -1;
            known.clear();
            setState("off");
            return api;
        }
        function toggle() {
            return state === "off" ? enable() : disable();
        }

        let on = false;
        try { on = !!storage && storage.getItem(STORE) === "on"; } catch (_) { /* storage off */ }
        if (on) enable();
        return api;
    };
})();

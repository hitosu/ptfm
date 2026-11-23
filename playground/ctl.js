/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev
 * The keys' pressure (the rest it leans from, its log scale) after Charles Vestal's measurements, fm1-velocity */
/* PT/FM UI playground: SETTINGS' MIDI CONTROLLER (specs/ui/screens/settings.md#midi-controller,
 * specs/controller.md). Once started, PT/FM has ended (only reloading the page brings it back, as only
 * switching the FM-1 off and on does): the panel works this alone. Its start screen (REC mirror, HOME general,
 * SELECT the channel), then a MIDI monitor of what the FM-1 sends, as the controller's protocol makes it
 * (fw/core/ctl.h: keys, buttons, knobs, MASTER, general mode's octave, Program Change and MMC). The
 * page sends no MIDI itself: the monitor shows it.
 *
 *   PTFM.ctl.start()                 the tracker ends; the start screen
 *   PTFM.ctl.on                      started
 *   PTFM.ctl.button(id, down), note(n, down, vel, reading), encoder(id, step, ms), master(v 0-1)   (vel: the key's,
 *                                    KEYS VELOCITY's, as the tracker left it, 0 none: the mode's own; reading: its
 *                                    landing, for the aftertouch)
 *   PTFM.ctl.pressure(n, reading)    a held key pressed as hard as the panel's drag says (general mode's aftertouch)
 *   PTFM.ctl.poll(ms)                the aftertouch's steps up to ms (every 5 ms, as the firmware's main loop): render's
 *   PTFM.ctl.render(lcd, dev)        the screen and the LEDs */
(function () {
    "use strict";
    const D = PTFM.draw, F = D.F;
    const MIRROR = 0, GENERAL = 1, REC = 13, HOME = 8, OCTM = 0, OCTP = 1, PLAY = 12, PRESETS = 6;
    const NOTE0 = 53, CC_BTN = 102, CC_KNOB = 20, CC_MASTER = 7, LOG = 15;
    const GEN_CC = [1, 21, 74, 71, 73, 72, 0], GEN_VAL0 = [0, 0, 64, 64, 64, 64, 0];
    const BTN = ["OCT-", "OCT+", "FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"];
    const ENC = ["SELECT", "ALGORITHM", "K1", "K2", "K3", "K4", "PRESETS"];
    const NN = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"];
    const hex = v => v.toString(16).toUpperCase().padStart(2, "0");
    let s = null;                                        /* the state, once started */
    const C = { on: false };

    const chOf = mode => s.chan ? s.chan - 1 : mode === MIRROR ? 15 : 0;
    function log(e) {                                    /* a line; a knob or MASTER turned again: its line */
        const last = s.log[s.log.length - 1];
        if (last && (e.kind === "knob" || e.kind === "master" || e.kind === "pressure" || e.kind === "poly") && last.kind === e.kind &&
            last.id === e.id) {
            e.sum = e.kind === "knob" && s.mode === MIRROR ? last.sum + e.val : e.val;
            s.log[s.log.length - 1] = e;
        } else {
            e.sum = e.val;
            s.log.push(e);
            if (s.log.length > LOG) s.log.shift();
        }
        if (e.bytes) s.sent++;
    }
    const msg = (kind, id, val, bytes) => log({ kind, id, val, bytes });

    /* general mode's aftertouch, fw/core/ctl.h's in the same integers (SETTINGS' CONTROLLER AFTERTOUCH, specs/controller.md
     * #3-general-mode): F3-C5's pressure, pressing harder than the key rests, from its reading (the panel's: its landing,
     * then as a drag moves it), worked out every 5 ms; only with KEYS VELOCITY not CONSTANT */
    const AT_KEYS = 20, AT_MS = 5, AT_SETTLE = 250, AT_DEAD = 164, AT_SPAN = 1884;
    C.lg = x => {                                        /* log2 in Q12 (press.h's press_lg) */
        let e = 0;
        while (x >>> (e + 1)) e++;
        let f = (e >= 16 ? x >>> (e - 16) : x << (16 - e)) & 0xFFFF;
        f += Math.floor(Math.floor(f * (65536 - f) / 65536) * 22479 / 65536);
        return (e << 12) + (f >>> 4);
    };
    /* (press.h's press_step: a key's word w, its reading d, dt ms since its last step: [its word, its pressure]; the
     * tracker's keys take it too, ui.js) */
    C.press = (w, d, dt) => {
        if (!d) return [0, 0];
        const r16 = (d > 186 ? d - 185 : 1) << 4;
        let rest = w & 0x3FFF, s = w >>> 14 & 0x7FF, c = w >>> 25;
        if (!rest) { rest = r16; s = 0; c = Math.floor(AT_SETTLE * 2 / 5); }
        else { const u = Math.floor((Math.min(dt, 255) * 2 + 2) / 5); c = c > u ? c - u : 0; }
        if (r16 > rest) rest = r16;
        else if (c) rest -= Math.floor((rest - r16) / 4);
        else if (rest > r16) { const step = (rest - r16) >> 10; rest -= step || 1; }
        const lean = C.lg(rest) - C.lg(r16);
        let x = lean > AT_DEAD ? Math.trunc(((lean - AT_DEAD) * 127 + (AT_SPAN >> 1)) / AT_SPAN) : 0;
        if (x > 127) x = 127;
        s = s + Math.trunc(((x << 4) - s) * 11 / 64);
        return [(rest | s << 14 | c << 25) >>> 0, (s + 8) >> 4];
    };
    const atMode = () => ["OFF", "CHANNEL", "POLY"].indexOf(PTFM.ui && PTFM.ui.set ? PTFM.ui.set("CONTROLLER AFTERTOUCH") : "CHANNEL");
    const sensing = () => !(PTFM.ui && PTFM.ui.set && PTFM.ui.set("KEYS VELOCITY") === "CONSTANT");
    function atSend() {                                  /* (ctl.h's ctl_at_send) */
        if (s.mode !== GENERAL || !s.atMode) return;
        const ch = chOf(s.mode);
        if (s.atMode === 1) {
            let m = 0;
            for (let k = 0; k < AT_KEYS; k++) if (s.keys.has(k) && s.press[k] > m) m = s.press[k];
            if (m !== s.chSent) { s.chSent = m; msg("pressure", 0, m, [0xD0 | ch, m]); }
            return;
        }
        for (let k = 0; k < AT_KEYS; k++)
            if (s.keys.has(k) && s.press[k] !== s.atSent[k]) {
                s.atSent[k] = s.press[k];
                msg("poly", k, s.press[k], [0xA0 | ch, s.note[k], s.press[k]]);
            }
    }
    C.pressure = function (k, reading) { if (s && k < AT_KEYS && s.keys.has(k)) s.reading[k] = reading; };
    C.poll = function (now) {
        if (!s || s.mode !== GENERAL || !s.atMode) return;
        if (s.atT == null) s.atT = now;
        while (now - s.atT >= AT_MS) {
            s.atT += AT_MS;
            for (let k = 0; k < AT_KEYS; k++)
                [s.at[k], s.press[k]] = C.press(s.at[k], s.keys.has(k) && sensing() ? s.reading[k] || 0 : 0, AT_MS);
            atSend();
        }
    };

    C.start = function () {
        const held = b => PTFM.ui && PTFM.ui.held(b);
        s = { mode: null, chan: 0, armed: new Set([REC, HOME].filter(b => !held(b))), held: new Set(), keys: new Set(),
              log: [], sent: 0, oct: 0, val: GEN_VAL0.slice(), click: [0, 0, 0, 0, 0, 0, 0], playing: false, note: [],
              master: null, ign: new Set(), atMode: atMode(), at: new Array(AT_KEYS).fill(0),
              press: new Array(AT_KEYS).fill(0), atSent: new Array(AT_KEYS).fill(0), chSent: 0, reading: [], atT: null };
              /* (armed: REC and HOME not held as it starts; one held must come up first) */
        C.on = true;
        D.theme(D.THEMES[0]);                            /* (the controller's own colours, whatever THEME was) */
    };
    function pick(mode) {
        s.mode = mode;
        for (const b of s.held) s.ign.add("b" + b);      /* (held now: nothing till let go) */
        msg("mode", 0, mode, null);
        msg("hello", 0, mode, [0xF0, 0x7D, 0x50, 0x54, 0x43, 0x02, mode, 0x02, chOf(mode), 0xF7]);
        if (mode === GENERAL) msg("alloff", 0, 0, [0xB0 | chOf(mode), 123, 0]);
    }
    C.button = function (id, down) {
        if (down) s.held.add(id); else s.held.delete(id);
        if (s.mode === null) {                           /* the start: a press of REC or HOME, seen up since it began */
            if (!down && (id === REC || id === HOME)) s.armed.add(id);
            if (down && s.armed.has(id)) pick(id === REC ? MIRROR : GENERAL);
            return;
        }
        if (s.ign.has("b" + id)) { if (!down) s.ign.delete("b" + id); return; }
        const ch = chOf(s.mode);
        if (s.mode === GENERAL && (id === OCTM || id === OCTP)) {
            if (!down) return;
            const other = id === OCTM ? OCTP : OCTM;
            if (s.held.has(other)) s.oct = 0;
            else s.oct = Math.max(-3, Math.min(3, s.oct + (id === OCTM ? -1 : 1)));
            return;                                      /* (nothing sent) */
        }
        msg("button", id, down ? 127 : 0, [0xB0 | ch, CC_BTN + id, down ? 127 : 0]);
        if (s.mode === GENERAL && id === PLAY && down) {
            s.playing = !s.playing;
            msg("mmc", id, s.playing ? 1 : 0, [0xF0, 0x7F, 0x7F, 0x06, s.playing ? 2 : 1, 0xF7]);
        }
    };
    C.note = function (k, down, vel, reading) {
        if (s.mode === null) return;
        const ch = chOf(s.mode);
        if (down) s.keys.add(k); else s.keys.delete(k);
        s.reading[k] = down ? reading || 0 : 0;
        if (down) {
            const n = NOTE0 + k + (s.mode === GENERAL ? 12 * s.oct : 0);
            s.note[k] = n;
            msg("on", k, n, [0x90 | ch, n, vel >= 1 && vel <= 127 ? vel : s.mode === MIRROR ? 127 : 100]);
        } else if (s.note[k] != null) {
            if (k < AT_KEYS && s.atSent[k]) { s.atSent[k] = 0; msg("poly", k, 0, [0xA0 | ch, s.note[k], 0]); }
            msg("off", k, s.note[k], [0x80 | ch, s.note[k], 0]);
            atSend();                                    /* (CHANNEL: the highest of those still held) */
        }
    };
    C.encoder = function (e, step, ms) {
        if (s.mode === null) {                           /* the start: SELECT the channel, DEFAULT then 1-16 */
            if (e === 0) s.chan = Math.max(0, Math.min(16, s.chan + Math.sign(step)));
            return;
        }
        const ch = chOf(s.mode);
        if (s.mode === MIRROR) { msg("knob", e, step, [0xB0 | ch, CC_KNOB + e, step & 0x7F]); return; }
        if (e === PRESETS) {
            const p = Math.max(0, Math.min(127, s.val[e] + step));
            if (p !== s.val[e]) { s.val[e] = p; msg("program", e, p + 1, [0xC0 | ch, p]); }
            return;
        }
        const now = ms || performance.now(), dt = now - s.click[e], m = dt < 20 ? 4 : dt < 40 ? 2 : 1;
        s.click[e] = now;
        const v = Math.max(0, Math.min(127, s.val[e] + step * m));
        if (v !== s.val[e]) { s.val[e] = v; msg("knob", e, v, [0xB0 | ch, GEN_CC[e], v]); }
    };
    C.master = function (v) {                            /* (0-1) */
        if (s.mode === null) return;
        const m = Math.round(v * 127);
        if (s.master === null) { s.master = m; return; }   /* (the first reading: where it is, nothing sent) */
        if (m !== s.master) { s.master = m; msg("master", 0, m, [0xB0 | chOf(s.mode), CC_MASTER, m]); }
    };

    function row(y, e) {
        let col = D.LBLUE;
        const at = (x, t, c) => D.text(x, y, F.T, t, c);
        if (e.kind === "on" || e.kind === "off") {
            col = D.WHITE;
            at(2, NN[e.val % 12] + (Math.floor(e.val / 12) - 1), col);
            at(62, e.kind === "on" ? "NOTE ON" : "NOTE OFF", col);
            if (e.kind === "on") at(122, String(e.bytes[2]), col);
        } else if (e.kind === "button") {
            at(2, BTN[e.id], col); at(62, "CC", col); at(80, String(e.bytes[1]), col); at(122, String(e.val), col);
        } else if (e.kind === "knob" || e.kind === "master") {
            at(2, e.kind === "master" ? "MASTER" : ENC[e.id], col); at(62, "CC", col); at(80, String(e.bytes[1]), col);
            if (e.kind === "knob" && s.mode === MIRROR && e.sum > 0) at(116, "+", col);
            if (e.sum < 0) at(116, "-", col);
            at(122, String(Math.abs(e.sum)), col);
        } else if (e.kind === "pressure" || e.kind === "poly") {   /* (general mode's aftertouch: its line follows it) */
            col = D.LGREEN;
            at(2, e.kind === "poly" ? NN[e.bytes[1] % 12] + (Math.floor(e.bytes[1] / 12) - 1) : "KEYS", col);
            at(62, e.kind === "poly" ? "POLY PRESS" : "CHAN PRESS", col);
            at(122, String(e.val), col);
        } else if (e.kind === "program") {
            col = D.YELLOW; at(2, "PRESETS", col); at(62, "PROGRAM", col); at(110, String(e.val), col);
        } else if (e.kind === "mmc") {
            col = D.GREEN; at(2, "PLAY", col); at(62, e.val ? "MMC PLAY" : "MMC STOP", col);
        } else if (e.kind === "alloff") {
            at(2, "FM-1", col); at(62, "ALL NOTES OFF", col);
        } else if (e.kind === "hello") {
            col = D.LMAGENTA; at(2, "FM-1", col); at(62, "HELLO", col); at(110, e.val === MIRROR ? "MIRROR" : "GENERAL", col);
        } else if (e.kind === "mode") {
            at(2, e.val === MIRROR ? "REC" : "HOME", D.WHITE); at(62, e.val === MIRROR ? "MODE MIRROR" : "MODE GENERAL", D.WHITE);
            return;
        }
        if (e.bytes && e.bytes.length <= 3) e.bytes.forEach((b, k) => at(170 + 18 * k, hex(b), D.DGREY));
        else if (e.bytes) at(170, "F0 .. F7", D.DGREY);
    }
    C.render = function (lcd, dev) {
        C.poll(PTFM.ui && PTFM.ui.now ? PTFM.ui.now() : performance.now());
        D.noclip();
        D.clear(D.BLACK);
        const centre = (f, t, y, c) => D.text(D.centre(f, t, 0, 240), y, f, t, c);
        if (s.mode === null) {                           /* the start, as the firmware's */
            centre(F.L, "PT/FM", 24, D.WHITE);
            centre(F.L, "CONTROLLER", 58, D.WHITE);
            centre(F.T, "PRESS A BUTTON TO START", 104, D.LGREY);
            centre(F.S, "REC   MIRROR  CH " + String(chOf(MIRROR) + 1).padEnd(2), 130, D.LCYAN);
            centre(F.S, "HOME  GENERAL CH " + String(chOf(GENERAL) + 1).padEnd(2), 154, D.YELLOW);
            centre(F.T, s.chan ? "SELECT: CHANNEL " + s.chan : "SELECT: CHANNEL DEFAULT", 188, s.chan ? D.WHITE : D.LGREY);
        } else {
            D.text(2, 2, F.T, "PT/FM CONTROLLER", D.WHITE);
            const h = (s.mode === MIRROR ? "MIRROR" : "GENERAL") + " CH " + (chOf(s.mode) + 1);
            D.text(238 - D.tw(F.T, h), 2, F.T, h, s.mode === MIRROR ? D.LCYAN : D.YELLOW);
            D.hline(0, 15, 240, D.DGREY);
            D.text(2, 19, F.T, "SENT " + s.sent + "  LOST 0", D.LGREY);
            if (s.mode === MIRROR) { const l = "LEDS IN 0"; D.text(238 - D.tw(F.T, l), 19, F.T, l, D.DGREY); }
            D.hline(0, 32, 240, D.DGREY);
            s.log.forEach((e, i) => row(35 + 12 * (LOG - s.log.length + i), e));
        }
        D.hline(0, 216, 240, D.DGREY);
        D.text(2, 222, F.T, "SWITCH OFF AND ON FOR THE TRACKER", D.DGREY);
        D.blitTo(lcd);
        /* the LEDs: the start's REC and HOME; then general mode's own (the keys and buttons held, the octave,
         * PLAY's green); mirror mode's come from the page that pairs with it (none here) */
        for (let b = 0; b < 14; b++) {
            let v = s.mode === null ? (b === REC || b === HOME ? 1 : 0) : s.mode === GENERAL && s.held.has(b) ? 1 : 0;
            if (s.mode === GENERAL && (b === OCTM && s.oct < 0 || b === OCTP && s.oct > 0)) v = Math.abs(s.oct) === 1 ? .25 : 1;
            dev.led(b, v);
        }
        for (let n = 0; n < 27; n++) dev.led(dev.NOTE0 + n, s.mode === GENERAL && s.keys.has(n) ? 1 : 0);
        dev.led(dev.LED_PLAY_GREEN, s.mode === GENERAL && s.playing ? 1 : 0);
    };
    C.state = () => s;                                   /* (the tests') */
    PTFM.ctl = C;
})();

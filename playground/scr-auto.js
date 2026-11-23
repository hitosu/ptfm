/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground: AUTOMATIONS (specs/ui/screens/automations.md): 16 slots, each the FM-1's own input recorded at
 * its times and played back in its place, on the screen it was recorded on. ui.js's pass hands each pass's input to
 * U.auto.input before the controls act on it: recording takes its changes, playing stands in for it (MASTER the
 * player's, always), and passes of its own come at its times (U.pass). The file (specs/files/automations.md): kind 0x32,
 * number 00, a record a slot's part (M.state.autos[sub], sub: the slot x 16 + the part, its payload's bytes): part 0's
 * byte 0 the screen recorded on; each part then its events' count (2 B) and the events bit-packed, whole in a part, a
 * time counting on across parts. The firmware's coding, byte for byte (fw/core/ui/scr_auto.c). COMPACT writes a slot's
 * parts again with its pauses shortened; HOME, ARP, then a white key (ui.js) plays a slot from any screen, HOME, ARP,
 * REC, then a white key records into one; HOME, ARP, REC ends a recording where it is. */
(function () {
    "use strict";
    const D = PTFM.draw, F = D.F, M = PTFM.model, U = PTFM.ui, B = U.B, E = U.E;
    const PART_MAX = 4080;                  /* a part's bytes at most */
    const PEND = 192;                       /* recorded bits waiting for the edit memory, in bytes */
    const OP6 = 5;                          /* HOME + this black key: AUTOMATIONS */
    const IDLE = 0, REC = 1, PLAY = 2;
    const PRESS = 0, RELEASE = 1, KNOB = 2;
    const TB = [5, 8, 12, 16, 24];          /* a time class's bits after its prefix (0, 10, 110, 1110, 1111) */
    const OFF = [0, 32, 288, 4384, 69920];  /* each class's first time (ms): the bits of the classes before it */
    const ua = {
        mode: IDLE, slot: 0, sel: 0, part: 0, scr: 0, home: 0, end: 0, fresh: 0, wrote: 0,
        pend: new Uint8Array(PEND + 8),     /* recording: the part's bytes from byte fbyte of its events on */
        nev: 0,                             /* the part's events: written (recording), left (playing) */
        bits: 0, fbyte: 0, fbits: 0,        /* recording: the part's event bits; its event bytes in the edit memory whole; its
                                             * bits at the last flush */
        t0: 0, last: 0, homeT: 0, flushed: 0,   /* ms: time 0, the last event's, HOME's held-back press, the last flush
                                             * (recording) or the last pass (playing) */
        ob: 0, on: 0,                       /* the automation's buttons and keys down (recorded, or given while playing) */
        rb: 0, rn: 0, mb: 0, mn: 0,         /* playing: the controls' own last; masked till let go (the player's) */
        bit: 0, have: 0,                    /* playing: the reading's place in the part (bits); an event to come */
        nx: { t: 0, k: 0, id: 0, dt: false, c: 0 },   /* playing: the next event (dt: false in the same pass) */
        /* recording: the stream as it was before its last HOME press written, while only HOME's, OP6's, ARP's and
         * REC's events followed (the end's, HOME tapped then OP6, or then ARP and REC: taken out again at the end,
         * rewindEnd) */
        mk: { ok: 0, part: 0, nev: 0, bits: 0, last: 0, ob: 0, on: 0 },
        from: null,                         /* where AUTOMATIONS was opened from */
        gen: -1, dur: new Array(16).fill(0),    /* the slots' lengths (ms), counted while the model is at gen (-1: again) */
        scrs: new Array(16).fill(-1),       /* ... their screens (-1 empty; 0-255 the byte) */
    };
    const now = () => Math.floor(U.now());  /* (the firmware's clock: whole ms) */
    const sub = part => ua.slot * 16 + part;
    /* a slot's part as the edit memory and the flash hold it (null: none, or too short for its head) */
    const partAt = (slot, part) => { const p = M.state.autos[slot * 16 + part]; return p && p.length >= (part ? 2 : 3) ? p : null; };
    const mss = ms => Math.floor(ms / 60000) + ":" + String(Math.floor(ms / 1000) % 60).padStart(2, "0");   /* `0:42` */
    const told = what => U.toast("AUTOMATION " + M.hex(ua.slot) + " " + what + ": " + mss(ua.last - ua.t0));   /* (a recording's end) */
    /* the round's screens (ui.js's ROUND): an automation's screen is its place there, AUTOMATIONS' own not one (numbers
     * fixed for good: a round ordered otherwise needs a table of its own for them) */
    const roundAt = name => { const i = U.ROUND.indexOf(name); return i < 0 ? 0xFF : i; };
    const roundScreen = i => i >= 0 && i < U.ROUND.length - 1 && U.screens[U.ROUND[i]] ? U.ROUND[i] : null;

    /* ---- a stream's coding ---- */
    function putBits(p, bit, n, v) {
        for (let i = 0; i < n; i++, bit++) {
            const m = 1 << (bit & 7);
            if ((v >>> i) & 1) p[bit >> 3] |= m; else p[bit >> 3] &= ~m;
        }
    }
    function getBits(p, bit, n) {
        let v = 0;
        for (let i = 0; i < n; i++, bit++) v |= (((p[bit >> 3] | 0) >> (bit & 7)) & 1) << i;
        return v >>> 0;
    }
    /* an event coded at bit at of b (null: counted only): its time since the event before (d, ms), then its action; the
     * bit after it */
    function code(b, at, d, k, id, c) {
        const put = (n, v) => { if (b) putBits(b, at, n, v); at += n; };
        let c4 = 0;
        while (c4 < 4 && d >= OFF[c4 + 1]) c4++;
        put(c4 < 4 ? c4 + 1 : 4, (1 << c4) - 1);
        put(TB[c4], Math.min(d - OFF[c4], 0xFFFFFF));
        if (k === KNOB) { put(2, 3); put(3, id); put(4, c & 15); }
        else { put(k + 1, k ? 1 : 0); put(6, id); }
        return at;
    }
    /* the part as recorded so far into the edit memory: 0 done; 1 refused (the edit memory full: its own question says
     * so; or the part written before gone). (The firmware's 2, the store busy, never happens here) */
    function flush() {
        const hb = ua.part ? 2 : 3, nb = (ua.bits + 7) >> 3, k = (ua.bits >> 3) - ua.fbyte;
        const p = partAt(ua.slot, ua.part);
        if (ua.fbyte && (!p || p.length < hb + ua.fbyte)) return 1;
        const b = new Array(hb + nb).fill(0);
        if (p && ua.fbyte) for (let i = 0; i < ua.fbyte; i++) b[hb + i] = p[hb + i] | 0;
        b[0] = ua.scr;
        b[hb - 2] = ua.nev & 255;
        b[hb - 1] = ua.nev >> 8 & 255;
        for (let i = 0; i < nb - ua.fbyte; i++) b[hb + ua.fbyte + i] = ua.pend[i];
        const s = sub(ua.part);
        if (!U.edit(() => { M.state.autos[s] = b; })) return 1;
        if (!ua.wrote)                      /* (its first write: the slot's older parts past this one gone now, so a SAVE
                                             * meanwhile keeps no part of the automation it replaces beside the new one's) */
            for (let x = ua.part + 1; x < 16; x++) if (partAt(ua.slot, x)) edDel(sub(x));
        ua.wrote = 1;
        ua.pend[0] = ua.pend[k];
        ua.pend.fill(0, 1);
        ua.fbyte += k;
        ua.fbits = ua.bits;
        ua.flushed = now();
        return 0;
    }
    const edDel = s => U.edit(() => { M.state.autos[s] = null; });
    /* an event at t (ms): HOME's held-back press first; a release of what is not down left out (a key's bit its number, a
     * button's its own) */
    function ev(t, k, id, c) {
        const key = id < 27, bit = 1 << (key ? id : id - 27);
        if (ua.mode !== REC || ua.end) return;
        if (ua.home) { ua.home = 0; ev(ua.homeT, PRESS, 27 + B.HOME, 0); }
        if (k === RELEASE && !((key ? ua.on : ua.ob) & bit)) return;
        if (k === PRESS && id === 27 + B.HOME)             /* (HOME's press: the end's may begin here) */
            ua.mk = { ok: 1, part: ua.part, nev: ua.nev, bits: ua.bits, last: ua.last, ob: ua.ob, on: ua.on };
        else if (k === KNOB || (id !== 27 + B.HOME && id !== U.BLACK[OP6] && id !== 27 + B.ARP && id !== 27 + B.REC)) ua.mk.ok = 0;
        if (k !== KNOB) {
            if (key) ua.on = k === PRESS ? ua.on | bit : ua.on & ~bit;
            else ua.ob = k === PRESS ? ua.ob | bit : ua.ob & ~bit;
        }
        if (!ua.nev && !ua.part) ua.t0 = ua.last = t;
        if ((ua.part ? 2 : 3) + ((ua.bits + 44) >> 3) > PART_MAX) {   /* (the part full: the next) */
            if (flush()) { recStop(); return; }
            if (ua.part === 15) { recStop(); told("IS FULL"); return; }   /* (its 16 parts: it ends there) */
            ua.part++;
            ua.nev = 0;
            ua.bits = ua.fbyte = ua.fbits = 0;
            ua.pend.fill(0);
        } else if (ua.bits + 37 > 8 * (ua.fbyte + PEND) && flush()) { recStop(); return; }
        ua.bits = code(ua.pend, ua.bits - 8 * ua.fbyte, (t - ua.last) >>> 0, k, id, c) + 8 * ua.fbyte;
        ua.last = t;
        ua.nev = (ua.nev + 1) & 0xFFFF;
    }
    function knobEv(t, e, c) {
        while (c) { const s = c > 7 ? 7 : c < -8 ? -8 : c; ev(t, KNOB, e, s); c -= s; }
    }
    /* playing (r: ua), or counting a slot's length (r: a reader of its own): the next event read (its time from time 0),
     * across parts; none left: have 0. A part cut short (an event running past its end) ends it there: nothing read past
     * a part's last byte */
    function next(r) {
        let p = partAt(r.slot, r.part);
        while (p && !r.nev && r.part < 15) {              /* (this part's events read: the next part's) */
            p = partAt(r.slot, ++r.part);
            if (p) { r.nev = p[0] | p[1] << 8; r.bit = 16; }
        }
        r.have = p && r.nev && ((r.bit + 7) >> 3) <= p.length ? 1 : 0;
        if (!r.have) return;
        const end = 8 * p.length;
        const get = n => {
            if (r.bit + n > end) { r.bit = end + 1; return 0; }
            const v = getBits(p, r.bit, n);
            r.bit += n;
            return v;
        };
        let k = 0;
        while (k < 4 && get(1)) k++;
        const d = OFF[k] + get(TB[k]);
        r.last = (r.last + d) >>> 0;
        r.nx.t = r.last;
        r.nx.dt = d !== 0;
        if (!get(1)) { r.nx.k = PRESS; r.nx.id = get(6); }
        else if (!get(1)) { r.nx.k = RELEASE; r.nx.id = get(6); }
        else { r.nx.k = KNOB; r.nx.id = get(3); r.nx.c = get(4) << 28 >> 28; }
        if (r.bit > end) { r.have = 0; return; }
        r.nev--;
    }
    /* r reading slot s from its start (p: its part 0), its first event next; r */
    function rdStart(r, s, p) { r.slot = s; r.part = 0; r.nev = p[1] | p[2] << 8; r.bit = 24; r.last = 0; next(r); return r; }
    /* the event r read, applied to what the automation holds (r.on, r.ob): true a press. A knob, a number past 40 (kept
     * for later) hold nothing */
    function track(r) {
        const x = r.nx, key = x.id < 27, bit = 1 << (key ? x.id : (x.id - 27) & 31);
        if (x.k === KNOB || x.id > 40) return false;
        if (x.k === RELEASE) { if (key) r.on &= ~bit; else r.ob &= ~bit; return false; }
        if (key) r.on |= bit; else r.ob |= bit;
        return true;
    }

    /* ---- recording, playing ---- */
    /* AUTOMATIONS opened while recording, HOME tapped then OP6 (or HOME, ARP, REC): the end's controls, the last written,
     * out again (the stream back to before HOME's press; a part filled meanwhile keeps them) */
    function rewindEnd() {
        const nb = ua.mk.bits >> 3;
        if (!ua.mk.ok || ua.mk.part !== ua.part) return;
        if (nb < ua.fbyte) {                /* (its byte already in the edit memory: back into the buffer) */
            const p = partAt(ua.slot, ua.part), at = (ua.part ? 2 : 3) + nb;
            ua.pend.fill(0);
            ua.pend[0] = p && at < p.length ? p[at] : 0;
            ua.fbyte = nb;
        } else ua.pend.fill(0, nb - ua.fbyte + 1);
        ua.pend[nb - ua.fbyte] &= (1 << (ua.mk.bits & 7)) - 1;
        ua.bits = ua.mk.bits; ua.nev = ua.mk.nev; ua.last = ua.mk.last; ua.ob = ua.mk.ob; ua.on = ua.mk.on;
        ua.mk.ok = 0;
    }
    function recStop() {                    /* stopped where it is (the edit memory full): its parts past this one gone */
        for (let k = ua.part + 1; k < 16; k++) if (partAt(ua.slot, k)) edDel(sub(k));
        ua.mode = IDLE;
    }
    function recEnd() {                     /* what the automation holds let go at the end (HOME held back not: unrecorded) */
        const t = now();
        if (ua.mode !== REC) return;
        ua.home = ua.end = 0;
        if (!ua.nev && !ua.part) {          /* (nothing written: no flush while the end's HOME may still be taken out) */
            ua.mode = IDLE;
            U.toast("NOTHING RECORDED");
            return;
        }
        for (let id = 40; id >= 0; id--) ev(t, RELEASE, id, 0);
        if (ua.mode !== REC) return;
        ua.end = 1;
        flush();
        recStop();
        told("RECORDED");
    }
    /* a recording ends: HOME + OP6 (AUTOMATIONS' enter) or, where it is, HOME, ARP, REC (ui.js); the end's controls out
     * again */
    function end() { if (ua.mode === REC) { rewindEnd(); recEnd(); schedule(); } }
    function playEnd(stopped) {             /* stopped: the player's presses since its start masked till let go (what was
                                             * held as it started stays held, as it was all along) */
        ua.mode = IDLE;
        ua.mb = stopped ? ua.rb & ~ua.mb : 0;
        ua.mn = stopped ? ua.rn & ~ua.mn : 0;
        U.toast(stopped ? "AUTOMATION STOPPED" : "AUTOMATION " + M.hex(ua.slot) + " PLAYED");
    }
    /* the next pass of its own: recording, the flush a second after the last; playing, the next event's (a pass a ms at
     * the most, as the FM-1's main loop), and one after its end (what it held let go there) */
    let timer = null, after = false;
    function schedule() {
        clearTimeout(timer);
        timer = null;
        let due = null;
        if (ua.mode === REC && !ua.mk.ok && ua.bits > ua.fbits) due = ua.flushed + 1000;
        else if (ua.mode === PLAY) due = ua.have && !U.blocking() ? ua.t0 + ua.nx.t : U.now();
        else if (after) due = U.now();
        after = false;
        if (due != null) timer = setTimeout(() => { timer = null; U.pass(); }, Math.max(1, due - U.now()));
    }
    /* a pass's input (inp: {buttons, notes, knob[7], knobPre[7]}: the knobs' clicks, those before the pass's first press
     * apart), against what the controls were given last (last: {buttons, notes}); recording, taken as it is; playing, or
     * a control of the player's masked, changed in place: true then. (The firmware's end at a seeding: none here, the
     * playground seeds nothing) */
    function pass(inp, last) {
        let masked = false;
        if (ua.mode !== PLAY) {             /* (masked: the player's presses that stopped a playback, till let go) */
            ua.mb &= inp.buttons;
            ua.mn &= inp.notes;
            inp.buttons &= ~ua.mb;
            inp.notes &= ~ua.mn;
            masked = !!(ua.mb | ua.mn);
            if (masked) { inp.knob.fill(0); inp.knobPre.fill(0); }
        }
        if (ua.mode === REC) {
            const t = inp.at, bd = inp.buttons & ~last.buttons, bu = last.buttons & ~inp.buttons;
            const nd = inp.notes & ~last.notes, nu = last.notes & ~inp.notes;
            if (U.blocking()) return masked;    /* (the FM-1 takes no input meanwhile: a save's progress) */
            if (ua.home && (nd >> U.BLACK[OP6] & 1) && (inp.buttons >> B.HOME & 1)) return masked;   /* (HOME held + OP6:
                                                 * AUTOMATIONS opens and ends it, neither recorded; refused, HOME's press
                                                 * stays held back) */
            for (let e = 0; e < 7; e++) knobEv(t, e, inp.knobPre[e]);
            for (let b = 0; b < 14; b++)
                if (bd >> b & 1) {
                    if (b === B.HOME) { ua.home = 1; ua.homeT = t; ua.mk.ok = 0; }   /* (a new HOME: no earlier one is the end's) */
                    else ev(t, PRESS, 27 + b, 0);
                }
            for (let n = 0; n < 27; n++) if (nd >> n & 1) ev(t, PRESS, n, 0);
            for (let e = 0; e < 7; e++) knobEv(t, e, inp.knob[e] - inp.knobPre[e]);
            for (let n = 0; n < 27; n++) if (nu >> n & 1) ev(t, RELEASE, n, 0);
            for (let b = 0; b < 14; b++) if (bu >> b & 1) ev(t, RELEASE, 27 + b, 0);
            if (ua.mode === REC && !ua.mk.ok && ua.bits > ua.fbits && t - ua.flushed >= 1000 && flush() === 1)
                recStop();                      /* (about a second's events written: not while the end's HOME may be taken out) */
            return masked;
        }
        if (ua.mode === PLAY) {
            let nb = inp.buttons & ~ua.rb, nn = inp.notes & ~ua.rn, turned = inp.knob.some(Boolean);
            ua.rb = inp.buttons;
            ua.rn = inp.notes;
            if (ua.fresh) { ua.fresh = 0; ua.mb = ua.rb; ua.mn = ua.rn; nb = nn = 0; turned = false; }
            if (nb || nn || turned) {           /* (a control of the player's: it stops, that control doing nothing else) */
                playEnd(true);
                inp.buttons &= ~ua.mb;
                inp.notes &= ~ua.mn;
                inp.knob.fill(0);
                inp.knobPre.fill(0);
                return true;
            }
            const tn = now(), pr = U.overlays.find(o => o.kind === "progress"), still = !!pr;
            if (still) ua.t0 += tn - Math.max(ua.flushed, Math.floor(pr.at));   /* (the FM-1 takes no input meanwhile, a save's
                                                 * progress: the automation's time stands still, nothing given; it goes on
                                                 * from there after. From the progress's start: the firmware's passes come
                                                 * every ms, here a pass a ms while it lasts) */
            ua.flushed = tn;
            const at = tn - ua.t0;
            let first = true, pressed = false;
            if (!still && ua.have && ua.nx.t <= at) inp.at = ua.t0 + ua.nx.t;   /* (the pass played at its recorded time:
                                                 * the knobs' acceleration counts the gaps as they were) */
            inp.knob.fill(0);
            inp.knobPre.fill(0);
            while (!still && ua.have && ua.nx.t <= at && (first || !ua.nx.dt)) {   /* (a recorded pass a pass) */
                const x = ua.nx;
                if (x.k === KNOB && x.id < 7) {     /* (knob 7, a number past 40: kept for later, skipped) */
                    inp.knob[x.id] += x.c;
                    if (!pressed) inp.knobPre[x.id] += x.c;
                }
                if (track(ua)) pressed = true;
                first = false;
                next(ua);
            }
            inp.buttons = ua.ob | (ua.rb & ua.mb);
            inp.notes = ua.on | (ua.rn & ua.mn);
            if (!ua.have) { playEnd(false); after = true; }
            return true;
        }
        return masked;
    }
    function input(inp, last) { const r = pass(inp, last); schedule(); return r; }
    /* REVERT ALL (ui.js's revert, as the firmware's ui_revert_all): a recording or a playback ends (its changes dropped
     * with the others); a pass after it lets go of what a playback held */
    function reverted() {
        if (ua.mode === PLAY) playEnd(true);
        else if (ua.mode === REC) { ua.mode = IDLE; U.toast("AUTOMATION STOPPED"); }
        ua.gen = -1;
        after = true;
        schedule();
    }
    /* ui.js's: the pass's hand-over, REVERT ALL's, what the header shows (AUTO 03: 1 recording, red; 2 playing, green), and
     * HOME's combination's (its white keys' LEDs, HOME, ARP, (REC,) a white key; HOME, ARP, REC's end) */
    U.auto = { input, reverted, state: () => ua.mode, slot: () => ua.slot, holds: s => !!partAt(s, 0), shortcut, end };

    /* ---- the screen ---- */
    function counts() {                     /* each slot's screen and length, read again when the model changed */
        if (ua.gen === M.version) return;
        for (let i = 0; i < 16; i++) {
            const q = partAt(i, 0);
            ua.scrs[i] = q ? q[0] : -1;
            ua.dur[i] = 0;
            if (!q || ua.mode === PLAY) continue;
            const r = rdStart({ nx: {} }, i, q);
            while (r.have) next(r);
            ua.dur[i] = r.last;
        }
        ua.gen = ua.mode === PLAY ? -1 : M.version;     /* (playing: the lengths not counted, so counted again after) */
    }
    function empty(s) {                     /* slot s holds none: AUTOMATION 03 IS EMPTY */
        if (partAt(s, 0)) return false;
        U.toast("AUTOMATION " + M.hex(s) + " IS EMPTY");
        return true;
    }
    function play() {
        if (empty(ua.sel)) { U.refuse(B.PLAY); return; }
        const p = partAt(ua.sel, 0), s = roundScreen(p[0]);
        rdStart(ua, ua.sel, p);
        ua.t0 = ua.flushed = now(); ua.ob = ua.on = 0; ua.fresh = 1;
        ua.mode = PLAY;
        U.goBack(s || ua.from);
        schedule();
    }
    /* HOME, ARP, then a white key (ui.js): slot s played as PLAY plays it (r: recorded into, as REC does, on the screen it
     * is on, a slot holding one asking first), highlighted here (a screen number no screen has: on the one it is on now);
     * false refused, the combination staying open */
    function shortcut(s, r) {
        if (ua.mode) { U.toast("ONE AUTOMATION AT A TIME"); return false; }
        if (!r && empty(s)) return false;
        if (U.screen.name !== "AUTOMATIONS") ua.from = U.screen.name;
        ua.sel = s;
        if (r) rec(); else play();
        return true;
    }
    /* COMPACT (specs/ui/screens/automations.md#compacting): the slot's events again, each pause while nothing of the
     * automation's is held (from an event after which none of its keys and buttons is down, to the next) longer than lim
     * (ms) made lim; the time before the first event and the events of one pass (0 ms apart) as they were. Laid out in
     * parts by the file's rule, as ev() lays them: { parts (each its payload's bytes), t its new length, cut whether a
     * pause was shortened }. The firmware's squash, byte for byte (it puts each part as it goes; here they are put
     * together, one edit) */
    function squash(lim) {
        const p = partAt(ua.sel, 0), r = rdStart({ nx: {}, on: 0, ob: 0 }, ua.sel, p), parts = [];
        let b = new Uint8Array(PART_MAX), hb = 3, at = 0, was = 0, t = 0, n = 0, first = true, cut = false;
        for (;;) {
            if (!r.have || hb + ((at + 44) >> 3) > PART_MAX) {   /* (the part closes: none left, or the next might not fit) */
                b[0] = p[0];
                b[hb - 2] = n & 255;
                b[hb - 1] = n >> 8 & 255;
                parts.push(Array.from(b.subarray(0, hb + ((at + 7) >> 3))));
                b = new Uint8Array(PART_MAX);
                hb = 2; at = 0; n = 0;
                if (!r.have) break;
            }
            let d = (r.nx.t - was) >>> 0;
            was = r.nx.t;
            if (!first && !(r.on | r.ob) && d > lim) { d = lim; cut = true; }
            first = false;
            at = code(b.subarray(hb), at, d, r.nx.k, r.nx.id, r.nx.c);
            t += d;
            n++;
            track(r);
            next(r);
        }
        return { parts, t, cut };
    }
    function compact(lim) {                 /* its list's 1 S, 0.5 S, 0.3 S */
        const q = squash(lim), s0 = ua.sel * 16;
        if (!q.cut) { U.toast("NOTHING TO COMPACT"); return; }
        if (!U.edit(() => { for (let k = 0; k < 16; k++) M.state.autos[s0 + k] = q.parts[k] || null; })) return;   /* (MEMORY
                                                 * FOR EDITS IS FULL asked: the slot as it was) */
        ua.slot = ua.sel; ua.t0 = 0; ua.last = q.t;
        told("COMPACTED");
    }
    function recGo() {
        ua.slot = ua.sel; ua.part = 0; ua.nev = 0; ua.bits = ua.fbyte = ua.fbits = 0; ua.home = ua.end = ua.wrote = 0;
        ua.ob = ua.on = 0;
        ua.mk.ok = 0;
        ua.pend.fill(0);
        ua.scr = roundAt(ua.from);
        ua.flushed = now();
        ua.mode = REC;
        U.goBack(ua.from);
        schedule();
    }
    function rec() {
        counts();
        if (!partAt(ua.sel, 0)) { recGo(); return; }
        U.ask(["AUTOMATION " + M.hex(ua.sel) + " HOLDS " + mss(ua.dur[ua.sel]), "REPLACE IT?"], ["REPLACE", "KEEP"],
              a => { if (a === "REPLACE") recGo(); });
    }
    function clear() {
        U.ask(["CLEAR AUTOMATION " + M.hex(ua.sel) + "?"], ["CLEAR", "KEEP"], a => {
            if (a !== "CLEAR") return;
            for (let k = 15; k >= 0; k--) if (partAt(ua.sel, k)) edDel(ua.sel * 16 + k);
            U.toast("AUTOMATION " + M.hex(ua.sel) + " CLEARED");
        });
    }
    function menuPick(it) {
        if (it === "RECORD") rec();
        else if (it === "PLAY") play();
        else if (it === "REVERT") U.askRevert("REVERT AUTOMATIONS TO SAVED?", null, () => M.revertThing("autos"));
        else if (it === "CLEAR" && partAt(ua.sel, 0)) clear();
        else if (it === "COMPACT" && !empty(ua.sel))
            U.menu("auto-c", "COMPACT AUTOMATION " + M.hex(ua.sel), ["1 S", "0.5 S", "0.3 S"], q => compact({ "1 S": 1000, "0.5 S": 500, "0.3 S": 300 }[q]));
    }
    const H = [["REC", D.YELLOW], [" RECORD  ", D.LGREY], ["PLAY", D.YELLOW], [" PLAY  ", D.LGREY], ["SEL", D.YELLOW], [" MENU", D.LGREY]];
    const scr = {
        name: "AUTOMATIONS", group: "PLAY", does: [B.SEL, B.REC],
        enter(from) {
            end();                          /* (HOME + OP6 while recording: it ends) */
            if (from && from !== "AUTOMATIONS") ua.from = from;   /* (itself: none) */
            if (!ua.from) ua.from = roundScreen(0);
            ua.gen = -1;
        },
        knob(e, s) { if (e === E.SELECT || e === E.ALGORITHM) ua.sel = U.clamp(ua.sel + s, 0, 15); },
        tap(b) {
            if (b !== B.SEL) return false;
            U.menu("auto", "AUTOMATIONS", ["RECORD", "PLAY", "COMPACT", "CLEAR", M.unsavedThing("autos") ? "REVERT" : null], menuPick, B.SEL);
            return true;
        },
        key(n, down) {                      /* a white key picks its slot; the keys play nothing here */
            const w = U.whiteIndex(n);
            if (down && w >= 0) ua.sel = w;
            return true;
        },
        rec,
        playDown: () => true,               /* (its own: PLAY plays at its release, nothing held then) */
        playUp(used) { if (!used) play(); },
        lit: b => b === B.REC || b === B.PLAY || b === B.SEL ? .25 : undefined,
        draw() {
            counts();
            for (let i = 0; i < 16; i++) {
                const x = (i & 3) * 60, y = U.BODY_Y + 6 + (i >> 2) * 46, on = i === ua.sel, s = ua.scrs[i];
                const sc = s >= 0 ? roundScreen(s) : null;
                if (on) D.box(x, y - 2, 60, 44, D.WHITE);
                D.text(x + 4, y, F.S, M.hex(i), on ? D.WHITE : D.CYAN);
                D.text(x + 4, y + 16, F.T, (s < 0 ? "EMPTY" : sc || "?").slice(0, 9), s < 0 ? D.DGREY : D.LGREY);
                if (s < 0) continue;
                D.text(x + 4, y + 28, F.T, mss(ua.dur[i]), D.LGREY);
            }
            D.runs(2, U.BODY_Y + U.BODY_H - 14, F.T, H);
        },
        /* (for the checks: the slot highlighted, the state) */
        sel: () => ua.sel,
    };
    U.register(scr);
})();

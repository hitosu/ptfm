/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM UI playground: the UI's core, after the UI's specs (specs/ui/): the controls' grammar
 * (controls.md: taps on release, holds as modifiers, layers, the combination grace period), the frame
 * (frame.md: header, footer, toasts), overlays (dialogs.md: menus, lists, properties, questions, text
 * entry; help.md), screens and their order (navigation.md), the LEDs (leds.md), the keys playing the
 * current instrument. The screens are in scr-*.js, the transport in player.js, the sound in audio.js.
 *
 * A screen: {name, group, enter(from), leave(), page() -> "N/M" or null, draw(), footer() -> [text,
 * colour] or null, knob(enc, steps, coarse), mark(enc, steps), tap(btn) -> handled, key(note, down)
 * -> handled, back() -> closed something, lit(btn) -> LED level or undefined, does: [buttons with a
 * function here], combines: [buttons that wait the grace period], keyLeds(set)}. */
PTFM.ui = (function () {
    "use strict";
    const D = PTFM.draw, F = D.F, M = PTFM.model;
    const B = { OCTM: 0, OCTP: 1, FX: 2, SEL: 3, ENV: 4, LFO: 5, EDIT: 6, GLO: 7, HOME: 8, SAVE: 9, ARP: 10, SEQ: 11, PLAY: 12, REC: 13 };
    const BNAME = ["OCT-", "OCT+", "FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"];
    const E = { SELECT: 0, ALGORITHM: 1, K1: 2, K2: 3, K3: 4, K4: 5, PRESETS: 6 };
    const WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26];
    const BLACK = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25];
    /* (FX has its own button and no black key; OP6 and the blank key no screen yet) */
    const SCREEN_KEY = { 1: "SONGS", 3: "EDITOR", 5: "INSTRUMENTS", 8: "PLAYBACK", 10: "DJ", 15: "VISUAL", 17: "STATUS",
                         20: "SETTINGS", 22: "LOOPS" };
    /* the round HOME's SELECT turns (specs/ui/navigation.md): the start screen first, SETTINGS last */
    const ROUND = ["STATUS", "SONGS", "EDITOR", "INSTRUMENTS", "PLAYBACK", "VISUAL", "FX", "LOOPS", "DJ", "SETTINGS"];
    const GROUP_COL = { MAKE: D.LBLUE, PLAY: D.GREEN, HEAR: D.BROWN, SYSTEM: D.MAGENTA };
    const BODY_Y = 17, BODY_H = 210;

    const U = {
        B, E, BNAME, WHITE, BLACK, BODY_Y, BODY_H, ROUND, GROUP_COL,
        screens: {}, screen: null, from: null, deep: [], overlays: [],
        song: 0, inst: 0xF000, lastInst: 0xF000, octave: 0,  /* (F0:00, the first built-in instrument: SAVE keeps the current one) */
        pattern: M.EDITOR_PATTERN,                           /* (the EDITOR's: its own at first) */
        mutes: new Array(16).fill(false), solo: -1,
        version: "0.26", dev: true,                          /* (fw/VERSION's number; the playground a dev build) */
    };
    U.now = () => performance.now();
    U.set = k => M.state.settings[k];
    U.whiteIndex = n => WHITE.indexOf(n);        /* 0-15, or -1 */
    U.blackIndex = n => BLACK.indexOf(n);
    U.muted = t => U.solo >= 0 ? t !== U.solo : U.mutes[t];
    /* a song's commands change only what is not being edited right now (specs/ui/screens/editor.md):
     * the screen says what it edits: "fx" (FX's EFFECTS), "mix" and a track (16 up: the loops'), "inst"
     * and an ID */
    U.editing = (what, key) => !!(U.screen && U.screen.editing && U.screen.editing(what, key));

    /* ------------------------------------------------------------------- messages --- */
    /* a toast, or a knob turned (its name and value, 1.2 s after the last click), in the header's place: the newest */
    let toast = null, knobShow = null;
    U.toast = (text, ms = 2000) => { toast = { text, until: U.now() + ms }; knobShow = null; };
    /* a toast in colours: runs [[text, colour], ...] (a key's name in the keys' yellow, as the help draws one) */
    U.toastRuns = (runs, ms = 2000) => { toast = { text: runs.map(r => r[0]).join(""), runs, until: U.now() + ms }; knobShow = null; };
    U.toastNow = () => toast && U.now() < toast.until ? toast.text : null;     /* (what shows: for the checks) */
    U.showKnob = (name, value) => { knobShow = { text: name + " " + value, until: U.now() + 1200 }; toast = null; };
    U.knobNow = () => knobShow && U.now() < knobShow.until ? knobShow.text : null;   /* (for the checks) */
    U.flash = {};                                /* button -> until: a refusal blinks it */
    U.refuse = btn => { U.flash[btn] = U.now() + 600; };

    /* ----------------------------------------------------------- the current things --- */
    U.instName = ref => { const i = M.inst(ref); return i ? i.name : "----------"; };
    /* lastInst: the last instrument picked that sounds by itself (not a FUSION slot), the one a new
     * FUSION pattern is offered to be filled with */
    U.setInst = ref => {
        U.inst = ref;
        if (M.inst(ref) && M.instType(ref) !== "FUSION") U.lastInst = ref;
        PTFM.audio.instrument(ref);
        PTFM.player.repeatRef(ref);                  /* a note repeat running (ARP) plays it from its next note, however it was picked */
    };
    /* PRESETS: through the cartridge's used slots, wrapping; from an empty one (INSTRUMENTS' cursor can stand
     * there), the next used one its way */
    U.stepInst = function (d) {
        const c = M.cart(U.inst >> 8);
        if (!c) return;
        const n = c.slots.length, dir = Math.sign(d) || 1;
        let at = U.inst & 255;
        for (let k = 0; k < Math.abs(d); k++) {
            let next = -1;
            for (let j = 1; j <= n && next < 0; j++) { const i = ((at + dir * j) % n + n) % n; if (c.slots[i]) next = i; }
            if (next < 0) return;                    /* an empty cartridge */
            at = next;
        }
        U.setInst(c.id << 8 | at);
    };
    U.stepCart = function (d) {                  /* SEL + PRESETS: the cartridges, wrapping */
        const cs = M.state.carts, i = Math.max(0, M.cartIndex(U.inst >> 8));
        const c = cs[((i + d) % cs.length + cs.length) % cs.length];
        const first = c.slots.findIndex(Boolean);
        U.setInst(c.id << 8 | Math.max(0, first));
    };
    U.midi = n => 53 + n + 12 * U.octave;       /* key 0 is F-3 at OCT 0 */
    /* ARP held + SELECT on the EDITOR, INSTRUMENTS and FX: the setting ARP RHYTHM (specs/ui/screens/
     * settings.md), one for all of them, an edit like any setting's */
    U.stepRhythm = function (s) {
        const L = M.RHYTHMS, v = L[clamp(L.indexOf(U.set("ARP RHYTHM")) + Math.sign(s), 0, L.length - 1)];
        U.edit(() => { M.state.settings["ARP RHYTHM"] = v; });
        PTFM.player.rhythm(v);
        U.showKnob("RHYTHM", v);
    };
    /* ARP held + ALGORITHM, + K1: the settings ARP PATTERN (round) and ARP LATCH, a running arpeggio
     * taking them at once */
    U.stepArpPattern = function (s) {
        const L = M.ARP_PATTERNS, v = L[U.wrap(L.indexOf(U.set("ARP PATTERN")) + Math.sign(s), L.length)];
        U.edit(() => { M.state.settings["ARP PATTERN"] = v; });
        arpSettings();
        U.toast("ARP " + v);
    };
    U.stepArpLatch = function (s) {
        const L = M.ARP_LATCHES, v = L[clamp(L.indexOf(U.set("ARP LATCH")) + Math.sign(s), 0, L.length - 1)];
        U.edit(() => { M.state.settings["ARP LATCH"] = v; });
        arpSettings();
        U.toast(v === "OFF" ? "LATCH OFF" : "LATCH " + v);
    };
    const arpHeld = () => Object.keys(keyUse).filter(n => keyUse[n].kind === "arp").map(n => keyUse[n].midi);
    function arpSettings() {
        PTFM.player.repeatSet(U.set("ARP PATTERN"), M.latchN(U.set("ARP LATCH")), arpHeld());
    }
    U.arpSettings = arpSettings;                 /* (SETTINGS' list changing them) */

    /* ARP (specs/ui/controls.md): the arpeggiator, one for every screen but those with an ARP of their own
     * (`ownArp`: the EDITOR's row preview, "preview"; LOOPS' pattern recorder, "pattern").
     * A tap: on / off; on with latched notes and no key held: they go first, it stays on. On, the keys feed
     * it instead of sounding (the screen's own uses of a key come first: FX LIVE's punch-ins). Switched on
     * with LATCH not OFF and nothing to play, it starts with the last note played, hands free. A change of
     * screen switches it off. */
    U.arp = false;
    const genericArp = () => !U.screen.ownArp;
    function arpTap() {
        const P = PTFM.player, latch = M.latchN(U.set("ARP LATCH"));
        if (U.arp && P.repeatNotes().length && latch > 0 && !U.keysHeld().length) { P.repeatClear(); U.toast("LATCHED NOTES LET GO"); return; }
        U.arp = !U.arp;
        if (!U.arp) { P.repeat(false); U.toast("ARP OFF"); return; }
        /* keys held as it comes on become its notes (their own notes end); else it waits for a key. Only where
         * the keys play no notes (FX LIVE: punch-ins) does it start on the last note, hands free */
        const playing = Object.keys(keyUse).filter(n => keyUse[n].kind === "play").map(Number);
        const handsFree = !!(U.screen.arpHandsFree && U.screen.arpHandsFree());
        const m = !playing.length && latch > 0 && handsFree ? (U.lastKey != null ? U.midi(U.lastKey) : 60) : null;
        P.repeat(true, m, U.inst, U.set("ARP RHYTHM"), U.set("ARP PATTERN"), latch);
        for (const n of playing) { U.keyOff(n); keyUse[n] = { kind: "arp", midi: U.midi(n) }; P.repeatKey(U.midi(n), U.inst, false); }
        U.toast("ARP " + U.set("ARP PATTERN") + (m != null ? " " + M.noteName(m) : ""));
    }
    function arpOff() { if (U.arp) { U.arp = false; PTFM.player.repeat(false); } }

    /* ---------------------------------------------------------------- the keys sound --- */
    const sounding = {};
    const keyMax = new Array(27).fill(0);        /* the loudest a key's note has been: its light follows it */
    U.keyOn = function (n, ref = U.inst, track = -1) {
        const m = U.midi(n);
        if (m < 12 || m > 127) return;
        keyMax[n] = 0;
        keyAt[n] = U.now();
        sounding[n] = PTFM.player.noteOn("k" + n, m, ref, U.set("KEYS VOLUME"), track);
    };
    const keyAt = new Array(27).fill(-1e9);     /* lit at once, before the sound's level comes back */
    /* a played key's light: full while its note is loud, dim as it fades
     * (under half its loudest), off under an eighth; with no sound running, lit while held */
    function keyGlow(keys) {
        const st = PTFM.audio.stats;
        if (!PTFM.audio.running) return;
        const lvl = new Array(27).fill(0);
        for (const v of st.voices || []) {
            const m = v.id && /^k(\d+)|k(\d+)$/.exec(v.id);      /* the keys' notes, and those of LOOPS' takes (a lane's, ...k5) */
            if (!m) continue;
            const n = +(m[1] || m[2]);
            if (!(n >= 0 && n < 27)) continue;
            keyMax[n] = Math.max(keyMax[n], v.peak);
            lvl[n] = Math.max(lvl[n], keyMax[n] > 1e-4 ? v.peak / keyMax[n] : 0);
        }
        for (let n = 0; n < 27; n++) {
            keys[n] = Math.max(keys[n], lvl[n] >= .5 || U.now() - keyAt[n] < 100 ? 1 : lvl[n] >= .125 ? DIM : 0);
            if (!lvl[n]) keyMax[n] = 0;                          /* silent: the next note starts afresh */
        }
    }
    U.keyOff = n => { PTFM.player.noteOff("k" + n); delete sounding[n]; };
    U.lastKey = null;

    /* ------------------------------------------------------------------- overlays --- */
    U.open = o => { U.overlays.push(o); return o; };
    U.close = o => { const i = U.overlays.indexOf(o || U.top()); if (i >= 0) U.overlays.splice(i, 1); };
    U.top = () => U.overlays[U.overlays.length - 1];
    U.blocking = () => U.overlays.some(o => o.kind === "progress");
    let selArmed = false;                        /* SEL tapped on the screen, nothing touched since: PRESETS opens its list */

    /* a box over the body: x, y, w, h; a title */
    function panel(x, y, w, h, title, col = D.CYAN) {
        D.rect(x, y, w, h, D.BLACK);
        D.box(x, y, w, h, D.LGREY);
        if (title) D.text(x + 4, y + 3, F.T, title, col);
    }
    U.panel = panel;
    const lastMenu = {};

    /* SEL menus (specs/ui/dialogs.md#menus): the last action used is preselected. An item is an action, or
     * a group {group: name, items: [...]} (one level, "Levels"): marked >, SEL shows its items in the same
     * panel after BACK, titled TITLE > GROUP; BACK goes up, the group highlighted; HOME closes all. A group
     * with nothing that can act is left out, one with one action shows it in its place. The menu reopens
     * at the last action's level when it can act there */
    U.menu = function (id, title, items, pick, opener = B.SEL) {
        items = items.filter(Boolean).map(x => {
            if (typeof x === "string") return x;
            const its = x.items.filter(Boolean);
            return its.length > 1 ? { group: x.group, items: its } : its[0] || null;
        }).filter(Boolean);
        const last = lastMenu[id] || {}, grp = last.group && items.find(x => x.group === last.group && x.items.includes(last.item));
        const o = U.open({
            kind: "menu", title, top: items, level: grp || null, sel: 0, opener,
            list() { return this.level ? ["BACK"].concat(this.level.items) : this.top; },
            /* (what shows, as words; and an action highlighted wherever it is, its group opened: for the checks) */
            get items() { return this.list().map(x => x.group || x); },
            goto(name) {
                const g = this.top.find(x => x.group && x.items.includes(name));
                this.level = g || null;
                this.sel = g ? g.items.indexOf(name) + 1 : this.top.indexOf(name);
            },
            knob(e, s) { if (e === E.SELECT) this.sel = clamp(this.sel + s, 0, this.list().length - 1); },
            tap(b) {
                if (b !== this.opener) return false;
                const it = this.list()[this.sel];
                if (it == null) return true;                     /* (nothing highlighted: nothing to do) */
                /* each group's last-used item (used), kept apart from where the menu reopens */
                const used = (lastMenu[id] && lastMenu[id].used) || {};
                /* BACK: the level above, the group highlighted; the menu reopens there, not inside it */
                if (it === "BACK" && this.level) {
                    lastMenu[id] = { group: null, item: this.level.group, used };
                    this.sel = this.top.indexOf(this.level); this.level = null;
                    return true;
                }
                if (it.group) {
                    const remembered = it.items.indexOf(used[it.group]);
                    this.level = it; this.sel = remembered >= 0 ? remembered + 1 : 1;
                    return true;
                }
                U.close(this);
                if (this.level) used[this.level.group] = it;
                lastMenu[id] = { group: this.level ? this.level.group : null, item: it, used };
                pick(it);
                return true;
            },
            draw() {
                const list = this.list(), n = list.length, h = Math.min(n, 15) * 12 + 22, w = 150, x = 240 - w - 6, y = BODY_Y + 4;
                const t = this.level ? (this.title + " > " + this.level.group).length > 23 ? this.level.group : this.title + " > " + this.level.group : this.title;
                panel(x, y, w, h, t);
                const top = Math.max(0, Math.min(this.sel - 7, n - 15));
                for (let i = top; i < Math.min(n, top + 15); i++) {
                    const yy = y + 18 + (i - top) * 12, on = i === this.sel, it = list[i];
                    if (on) { D.rect(x + 2, yy - 1, w - 4, 12, D.DEEP + D.BLUE); D.box(x + 2, yy - 1, w - 4, 12, D.WHITE); }
                    /* in a group, BACK first: `<` and the word in cyan, a dark grey rule under it (a way out,
                     * not an action) */
                    if (this.level && i === 0) {
                        if (!on) D.hline(x + 4, yy + 10, w - 8, D.DGREY);
                        D.text(x + 6, yy, F.T, "<", on ? D.WHITE : D.CYAN);
                        D.text(x + 18, yy, F.T, it, on ? D.WHITE : D.CYAN);
                        continue;
                    }
                    D.text(x + 6, yy, F.T, it.group || it, on ? D.WHITE : D.LGREY);
                    if (it.group) D.text(x + w - 12, yy, F.T, ">", on ? D.WHITE : D.LGREY);
                }
            },
        });
        /* the last action highlighted: in its group (opened), else at the top (a group left by BACK: on it) */
        o.sel = grp ? grp.items.indexOf(last.item) + 1 : Math.max(0, items.findIndex(x => x === last.item || x.group === last.item));
        return o;
    };
    /* a question (specs/ui/dialogs.md): every one is a yes or no. answers: [yes, no] in its own words;
     * OCT+ answers yes, OCT- no, HOME cancels (done(null)); nothing else acts while it shows */
    U.ask = function (lines, answers, done) {
        return U.open({
            kind: "question", lines, answers,
            knob() {},
            tap(b) {
                if (b === B.OCTP || b === B.OCTM) { U.close(this); done(this.answers[b === B.OCTP ? 0 : 1]); }
                return true;
            },
            cancel() { U.close(this); done(null); },
            draw() {
                const h = this.lines.length * 12 + 62, y = BODY_Y + Math.floor((BODY_H - h) / 2);
                panel(8, y, 224, h, null);
                this.lines.forEach((l, i) => D.text(D.centre(F.T, l, 8, 224), y + 8 + i * 12, F.T, l, i ? D.LGREY : D.WHITE));
                askFoot(y + 12 + this.lines.length * 12, y + h, this.answers, [["HOME", D.YELLOW], [": CANCEL", D.LGREY]]);
            },
        });
    };
    /* a question's foot: no on the left, under OCT-; yes on the right, under OCT+, as the two buttons
     * sit; then its keys' line, centred, 16 px above the panel's bottom */
    function askFoot(ay, bottom, answers, keys, btns = [B.OCTM, B.OCTP]) {
        [[btns[0], answers[1], 8], [btns[1], answers[0], 120]].forEach(([btn, a, x]) => {
            D.text(D.centre(F.T, BNAME[btn], x, 112), ay, F.T, BNAME[btn], D.YELLOW);
            D.text(D.centre(F.S, a, x, 112), ay + 13, F.S, a, D.WHITE, D.CROP_S.map((v, j) => j ? 16 : 0));
        });
        const kw = keys.reduce((a, r) => a + D.tw(F.T, r[0]), 0);
        D.runs(8 + Math.floor((224 - kw) / 2), bottom - 16, F.T, keys);
    }
    U.askFoot = askFoot;                         /* (LOOPS' RECORD LIVE panel: HOME and REC its buttons) */
    /* a question with a checklist: SELECT picks a line, K1 ticks it (clockwise) or clears it; a line with
     * nothing in it stays clear and dim. OCT+ answers yes with the ticked lines, OCT- no, HOME cancels.
     * title: text, or a function of the rows; rows: {name, count, note, on}; done(answer, rows); more: sub (a second
     * title line), foot (a line under the rows), bare (yes stands with none ticked: SONGS' DELETE) */
    U.checklist = function (title, rows, answers, done, fits, more = {}) {
        const SHOWN = 8;                                     /* more lines scroll with the pick */
        return U.open({
            kind: "question", title, rows, answers, sel: Math.max(0, rows.findIndex(r => r.count)), top: 0, more,
            knob(e, s) {
                if (e === E.SELECT) {
                    this.sel = clamp(this.sel + s, 0, this.rows.length - 1);
                    this.top = clamp(this.top, this.sel - SHOWN + 1, this.sel);
                }
                if (e === E.K1 && this.rows[this.sel].count) this.rows[this.sel].on = s > 0;
            },
            tap(b) {
                /* yes is refused with nothing ticked, or what is ticked does not fit (fits: the caller's) */
                if (b === B.OCTP && ((!this.rows.some(r => r.on) && !more.bare) || (fits && !fits(this.rows)))) { U.refuse(B.OCTP); return true; }
                if (b === B.OCTP || b === B.OCTM) { U.close(this); done(this.answers[b === B.OCTP ? 0 : 1], this.rows); }
                return true;
            },
            cancel() { U.close(this); done(null, this.rows); },
            draw() {
                const n = Math.min(SHOWN, this.rows.length), sy = more.sub ? 12 : 0, ex = sy + (more.foot ? 13 : 0);
                const h = n * 13 + 94 + ex, y = BODY_Y + Math.floor((BODY_H - h) / 2);
                panel(8, y, 224, h, null);
                const title = typeof this.title === "function" ? this.title(this.rows) : this.title;   /* it may follow the ticks */
                D.text(D.centre(F.T, title, 8, 224), y + 8, F.T, title, fits && !fits(this.rows) ? D.RED : D.WHITE);
                if (more.sub) D.text(D.centre(F.T, more.sub, 8, 224), y + 20, F.T, more.sub, D.LGREY);
                if (more.foot) D.text(D.centre(F.T, more.foot, 8, 224), y + 24 + sy + n * 13, F.T, more.foot, D.DGREY);
                this.rows.slice(this.top, this.top + n).forEach((r, k) => {
                    const i = this.top + k, ry = y + 24 + sy + k * 13, on = i === this.sel, col = r.count ? D.LGREY : D.DGREY;
                    if (on) D.box(12, ry - 1, 216, 13, D.WHITE);
                    D.box(16, ry + 1, 9, 9, col);                         /* the box, ticked: filled */
                    if (r.on) D.rect(18, ry + 3, 5, 5, D.WHITE);
                    D.text(32, ry, F.T, r.name, r.count ? (on ? D.WHITE : D.LGREY) : D.DGREY);
                    D.text(220 - D.tw(F.T, r.note), ry, F.T, r.note, col);
                });
                const keys = [["SELECT", D.YELLOW], [": PICK  ", D.LGREY], ["K1", D.YELLOW], [": TICK  ", D.LGREY], ["HOME", D.YELLOW], [": CANCEL", D.LGREY]];
                askFoot(y + 30 + n * 13 + ex, y + h, this.answers, this.rows.length === 1 ? keys.slice(2) : keys);   /* (one line: nothing to pick) */
            },
        });
    };
    /* GLO: properties. SELECT picks, K1 changes at once; GLO or HOME closes (SEL's REVERT takes changes back) */
    /* (a built-in thing too: edited as any, specs/ui/saving.md#fws) */
    /* sub: a second title line, whose they are, under a title ending in FOR (FX's GLO) */
    U.props = function (title, items, opener = B.GLO, sub = "") {
        return U.open({
            kind: "props", title, items, sel: 0, opener, sub,
            knob(e, s, coarse) {
                if (e === E.SELECT) this.sel = clamp(this.sel + s, 0, this.items.length - 1);
                else if (e === E.K1) {
                    const it = this.items[this.sel];
                    if (it.change) it.change(s, coarse);
                }
            },
            tap(b) {
                if (b === this.opener) { U.close(this); return true; }
                const it = this.items[this.sel];
                if (b === B.EDIT && it.edit) { it.edit(); return true; }
                if (b === B.LFO && it.reset) { it.reset(); return true; }
                return false;
            },
            draw() {
                const n = this.items.length, sub = this.sub ? 12 : 0, h = n * 14 + 24 + sub, y = BODY_Y + 4;
                panel(6, y, 228, h, this.title);
                if (sub) D.text(10, y + 15, F.T, this.sub, D.WHITE);
                this.items.forEach((it, i) => {
                    const yy = y + 19 + sub + i * 14, on = i === this.sel;
                    if (on) D.box(8, yy - 2, 224, 14, D.WHITE);
                    D.text(12, yy, F.T, it.name, on ? D.WHITE : D.CYAN);
                    const v = it.get(), col = it.col ? it.col(on) : (on ? D.WHITE : D.LGREY);   /* (an item's own colour, highlighted or not) */
                    D.text(228 - D.tw(F.T, v), yy, F.T, v, it.fixed ? D.DGREY : col);
                });
            },
        });
    };
    /* RECORD PATTERN (specs/ui/dialogs.md): the EDITOR's SEL menu (LOOPS has a recorder of its own). Up to 16 steps
     * on the white keys, one note (K1), a step's length (K2), the size (K3, 1-16 steps), written round the
     * loop or once (K4), OCT the octave, SEQ the keys picking the note instead; REC writes it (the screen's
     * own), HOME cancels. What is set stays for the next time, on either screen.
     * o: {opener, title, rpb, note (the first time's), round (K4's word: ROUND THE LOOP, TO THE END),
     *     where() runs, sound() ref, playing() the step playing or -1, hear(note), write() true to close} */
    const rp = { steps: new Array(16).fill(false), note: null, step: "1/16", size: 16, round: true, stepLong: false };
    const STEP_LENS = [["1/32", 8], ["1/16T", 6], ["1/16", 4], ["1/8T", 3], ["1/8", 2], ["1/4", 1]];     /* a beat's part */
    U.recPat = rp;
    /* the lengths that are whole rows at rpb rows a beat (none of one row: A ROW first) */
    U.stepLens = rpb => {
        const L = STEP_LENS.filter(([, d]) => rpb % d === 0);
        return L.some(([, d]) => rpb / d === 1) ? L : [["A ROW", rpb]].concat(L);
    };
    U.stepRows = rpb => { const s = STEP_LENS.find(x => x[0] === rp.step); return s ? rpb / s[1] : 1; };
    /* the step written q rows after the start (sr rows a step, limit the rows it may cover; once: the
     * steps once, whatever K4 says), or -1 */
    U.patternStep = (q, sr, limit, once) => q < (rp.round && !once ? limit : Math.min(rp.size * sr, limit)) && q % sr === 0 ? (q / sr) % rp.size : -1;
    /* what REC writes, n rows from start (wrap: round the end to the beginning, as a loop does): the
     * rows of its notes; with A STEP LONG the rows a step after them for a note off, where the next step
     * is not on (the screen writes those only on empty cells: nothing is removed). RING TO THE NEXT (the
     * first): no note offs, each note rings until its track's next, so a pad holds through the rests */
    U.patternPlan = function (n, start, wrap, sr, once) {
        const limit = wrap ? n : n - start, on = new Set(), off = [];
        for (let q = 0; q < limit; q += sr) { const k = U.patternStep(q, sr, limit, once); if (k >= 0 && rp.steps[k]) on.add((start + q) % n); }
        if (rp.stepLong) for (let q = 0; q < limit; q += sr) {
            if (!on.has((start + q) % n) || (!wrap && q + sr >= limit)) continue;
            const e = (start + q + sr) % n;
            if (!on.has(e)) off.push(e);
        }
        return { on: [...on], off };
    };
    U.patternDialog = function (o) {
        if (rp.note == null) rp.note = o.note;
        const lens = U.stepLens(o.rpb);
        if (!lens.some(s => s[0] === rp.step)) rp.step = (lens.find(([, d]) => o.rpb / d === 1) || lens[0])[0];
        /* PLAY: the steps heard on their own, from the first, round the size, at the screen's tempo (o.rowSec)
         * and as K1-K4 and SELECT set them now; whatever played pauses and keeps its place, as the pattern
         * list's preview. PLAY again, REC or HOME end it. pv: {t: the next step's time, k: its count, at: the
         * steps scheduled [time, step], timer} */
        const A = PTFM.audio;
        const pvStop = d => { if (d.pv) { clearInterval(d.pv.timer); d.pv = null; } };
        function pvRun(d) {
            const pv = d.pv;
            if (!pv) return;
            if (!U.overlays.includes(d)) { pvStop(d); return; }
            const now = A.time();
            while (pv.t < now + .12) {
                const dur = o.rowSec() * U.stepRows(o.rpb), k = pv.k % rp.size;
                if (rp.steps[k]) {                                /* A STEP LONG, else ringing to the next step on */
                    let n = 1;
                    if (!rp.stepLong) while (n < rp.size && !rp.steps[(k + n) % rp.size]) n++;
                    o.hear(rp.note, pv.t, dur * n * .95);
                }
                pv.at.push([pv.t, k]);
                pv.t += dur; pv.k++;
            }
            while (pv.at.length > 1 && pv.at[1][0] <= now) pv.at.shift();
        }
        const pvStep = d => d.pv && d.pv.at.length && d.pv.at[0][0] <= A.time() ? d.pv.at[0][1] : -1;
        return U.open({
            kind: "pattern", opener: o.opener,
            noteKeys: false,                     /* SEQ: the keys pick the note instead of the steps (steps at first) */
            pv: null,
            play() {
                if (this.pv) { pvStop(this); return; }
                if (PTFM.player.playing) PTFM.player.pause();
                this.pv = { t: A.time() + .05, k: 0, at: [], timer: setInterval(() => pvRun(this), 25) };
                pvRun(this);
            },
            /* the step showing as playing: the preview's, else the screen's (the steps along what plays) */
            stepNow() { return this.pv ? pvStep(this) : o.playing(); },
            knob(e, s, coarse) {
                if (e === E.K1) { rp.note = clamp(rp.note + s * (coarse ? 12 : 1), 12, 127); o.hear(rp.note); }
                if (e === E.K2) rp.step = lens[clamp(lens.findIndex(x => x[0] === rp.step) + Math.sign(s), 0, lens.length - 1)][0];
                if (e === E.K3) rp.size = clamp(rp.size + s, 1, 16);
                if (e === E.K4) rp.round = !rp.round;
                if (e === E.SELECT) rp.stepLong = s > 0;     /* the notes' length: clockwise a step, back ring on */
            },
            /* a white key in the size: its step on / off, heard as it comes on (unless the steps play along);
             * after SEQ any key is the note, heard, at the keyboard's octave */
            key(n, down) {
                if (this.noteKeys) { if (down) { rp.note = clamp(U.midi(n), 12, 127); o.hear(rp.note); } return true; }
                const k = WHITE.indexOf(n);
                if (down && k >= 0 && k < rp.size) { rp.steps[k] = !rp.steps[k]; if (rp.steps[k] && this.stepNow() < 0 && !this.pv) o.hear(rp.note); }
                return true;
            },
            /* OCT: the keyboard's octave as everywhere, the note an octave with it, heard */
            tap(b) {
                if (b === B.SEQ) { this.noteKeys = !this.noteKeys; U.toast(this.noteKeys ? "KEYS: THE NOTE" : "KEYS: THE STEPS"); }
                if (b === B.OCTM || b === B.OCTP) {
                    const d = b === B.OCTP ? 1 : -1;
                    if (U.octave + d < -4 || U.octave + d > 4 || rp.note + 12 * d < 12 || rp.note + 12 * d > 127) { U.refuse(b); return true; }
                    U.octave += d;
                    rp.note += 12 * d;
                    o.hear(rp.note);
                    U.toast("NOTE " + M.noteName(rp.note));
                }
                return true;
            },
            rec() { pvStop(this); if (o.write()) U.close(this); return true; },     /* REC writes it */
            cancel() { pvStop(this); U.close(this); },                              /* HOME */
            /* a step on full; off, dark, but each group of four's first dim, where a block starts (as LIVE's
             * punch-ins); past the size dark; the one playing turned over. The keys picking the note: its key
             * lit, where the keyboard's octave has it */
            keyLeds(keys) {
                if (this.noteKeys) { keys.fill(0); const n = rp.note - U.midi(0); if (n >= 0 && n < 27) keys[n] = 1; return true; }
                const pl = this.stepNow();
                WHITE.forEach((n, k) => { keys[n] = k >= rp.size ? 0 : k === pl ? (rp.steps[k] ? 0 : 1) : rp.steps[k] ? 1 : k % 4 === 0 ? DIM : 0; });
                return true;
            },
            draw() {
                const h = 180, y = BODY_Y + Math.floor((BODY_H - h) / 2), snd = o.sound(), col = M.instCol(snd), pl = this.stepNow();
                panel(8, y, 224, h, null);
                D.text(D.centre(F.T, o.title, 8, 224), y + 6, F.T, o.title, D.WHITE);
                for (let k = 0; k < 16; k++) {                       /* the steps, in fours; the one playing boxed */
                    const x = 13 + k * 13 + Math.floor(k / 4) * 2;
                    if (k >= rp.size) { D.box(x, y + 22, 11, 14, D.DEEP + D.DGREY); continue; }
                    D.rect(x, y + 22, 11, 14, rp.steps[k] ? col : D.DEEP + D.DGREY);
                    if (k === pl) D.box(x - 1, y + 21, 13, 16, D.WHITE);
                }
                const line = (i, k, name, v, more) => D.runs(14, y + 44 + 12 * i, F.T, [[k, D.YELLOW], [" " + name.padEnd(6), D.LGREY], [v, D.WHITE]].concat(more || []));
                line(0, "K1", "NOTE", M.noteName(rp.note), [["  " + U.instName(snd), col]]);
                line(1, "K2", "STEP", rp.step);
                line(2, "K3", "SIZE", rp.size + (rp.size > 1 ? " STEPS" : " STEP"));
                if (o.round) line(3, "K4", "WRITE", rp.round ? o.round : "ONCE");
                else D.runs(14, y + 80, F.T, [["K4", D.DGREY], [" WRITE ", D.LGREY], ["ONCE, SILENCE TO THE BAR", D.WHITE]]);   /* a new first loop */
                D.runs(14, y + 92, F.T, [["SELECT", D.YELLOW], [" NOTES ", D.LGREY], [rp.stepLong ? "A STEP LONG" : "RING TO THE NEXT", D.WHITE]]);
                D.runs(14, y + 104, F.T, o.where());
                D.runs(14, y + 116, F.T, [["PLAY", D.YELLOW], [this.pv ? ": STOP HEARING THEM" : ": HEAR THE STEPS ON THEIR OWN", D.LGREY]]);
                askFoot(y + 132, y + h, ["RECORD", "CANCEL"], [["KEYS", D.YELLOW], [this.noteKeys ? ": NOTE  " : ": STEPS  ", D.LGREY],
                                                               ["SEQ", D.YELLOW], [this.noteKeys ? ": STEPS  " : ": NOTE  ", D.LGREY],
                                                               ["OCT", D.YELLOW], [": OCTAVE", D.LGREY]], [B.HOME, B.REC]);
            },
        });
    };
    /* a held button's layer: what the keys and knobs do with it (specs/ui/controls.md). Not an overlay:
     * it takes no input and outlives a change of screen; it shows, over everything, until its button is let go,
     * combinations made meanwhile included */
    U.layers = [];
    U.unlayer = l => { const i = U.layers.indexOf(l); if (i >= 0) U.layers.splice(i, 1); };
    /* a combination's lines at the body's bottom (specs/ui/dialogs.md): the heading, then a control and what
     * it does on each line */
    function comboPanel(heading, lines) {
        const h = lines.length * 12 + 26, y = BODY_Y + BODY_H - h - 2;
        panel(4, y, 232, h, null);
        D.runs(8, y + 5, F.T, heading);
        lines.forEach((l, i) => {
            if (Array.isArray(l)) return D.runs(8, y + 19 + i * 12, F.T, l);      /* (a line coloured already) */
            const k = l.indexOf(":");
            D.runs(8, y + 19 + i * 12, F.T, [[l.slice(0, k), D.YELLOW], [l.slice(k), D.LGREY]]);
        });
    }
    /* lines: a list, or a function giving it (HOME's change as its combination is used) */
    U.layer = function (btn, lines) {
        const l = {
            btn, lines: typeof lines === "function" ? lines : () => lines,
            draw() {
                if (this.btn === B.HOME) return homePanel(home() || held.get(B.HOME) || {}, true);
                const tail = this.btn === B.FX ? [[" + ", D.CYAN], ["KEYS", D.YELLOW]] : [[" +", D.CYAN]];   /* (FX: HOLD FX + KEYS) */
                comboPanel([["HOLD ", D.CYAN], [BNAME[this.btn], D.YELLOW]].concat(tail), this.lines());
            },
        };
        U.layers.push(l);
        return l;
    };
    /* the help (specs/ui/help.md): HOME + LFO, on any screen and at any depth */
    U.openHelp = function () {
        const name = U.screen.name;
        const pages = PTFM.help.pages(name);
        U.open({
            kind: "help", pages, page: 0, header: "LOOK FOR ONSWERS",
            knob(e, s) { if (e === E.SELECT) this.page = clamp(this.page + s, 0, this.pages.length - 1); },
            tap() { return true; },
            draw() {
                D.rect(0, BODY_Y, 240, BODY_H, D.BLACK);
                this.pages[this.page].forEach((runs, i) => D.runs(0, BODY_Y + 2 + i * 12, F.T, runs));
            },
            pageText() { return M.pageOf(this.page, this.pages.length); },
        });
    };
    /* text entry (specs/ui/dialogs.md), the arcade way */
    const CHARSETS = ["ABCDEFGHIJKLMNOPQRSTUVWXYZ", "0123456789", " :.-?|/\\*#$@%"];
    const CHARS = CHARSETS.join("");
    U.textEntry = function (title, name, max, opener, done) {
        const chars = name.padEnd(max, " ").split("");
        return U.open({
            kind: "text", title, chars, pos: 0, opener,
            knob(e, s, coarse) {
                if (e === E.ALGORITHM) this.pos = clamp(this.pos + s, 0, max - 1);
                else if (e === E.SELECT) {
                    const c = this.chars[this.pos];
                    if (coarse) {
                        const set = CHARSETS.findIndex(x => x.includes(c));
                        this.chars[this.pos] = CHARSETS[((set + Math.sign(s)) % 3 + 3) % 3][0];
                    } else {
                        const i = CHARS.indexOf(c);
                        this.chars[this.pos] = CHARS[((i + s) % CHARS.length + CHARS.length) % CHARS.length];
                    }
                }
            },
            mark(e, s) { this.knob(e, s, true); },
            tap(b) {
                if (b === this.opener) { U.close(this); done(this.chars.join("").trimEnd()); return true; }
                if (b === B.LFO) { this.chars.splice(this.pos, 1); this.chars.push(" "); return true; }
                if (b === B.OCTP) { this.chars.splice(this.pos, 0, " "); this.chars.length = max; return true; }
                return true;
            },
            cancel() { U.close(this); done(null); },
            draw() {
                const y = BODY_Y + 44;
                panel(10, y, 220, 104, this.title);
                const x0 = D.centre(F.B, "W".repeat(max), 10, 220);
                this.chars.forEach((c, i) => {
                    const x = x0 + i * 12;
                    if (i === this.pos) D.rect(x, y + 22, 12, 26, D.DEEP + D.BLUE);
                    D.text(x, y + 23, F.B, c === " " ? "_" : c, i === this.pos ? D.WHITE : c === " " ? D.DGREY : D.LGREY);
                });
                D.runs(16, y + 56, F.T, [["ALGORITHM", D.YELLOW], [": MOVE  ", D.LGREY], ["SELECT", D.YELLOW], [": LETTER", D.LGREY]]);
                D.runs(16, y + 70, F.T, [[BNAME[this.opener], D.YELLOW], [": DONE  ", D.LGREY], ["HOME", D.YELLOW], [": CANCEL", D.LGREY]]);
                D.runs(16, y + 84, F.T, [["LFO", D.YELLOW], [": DELETE  ", D.LGREY], ["OCT+", D.YELLOW], [": SPACE", D.LGREY]]);
            },
        });
    };
    /* a progress bar: input waits */
    U.progress = function (text, ms, then) {
        const t0 = U.now();
        const o = U.open({
            kind: "progress", text,
            knob() {}, tap() { return true; },
            draw() {
                const k = clamp((U.now() - t0) / ms, 0, 1), y = BODY_Y + 80;
                panel(20, y, 200, 44, null);
                D.text(D.centre(F.T, this.text, 20, 200), y + 6, F.T, this.text, D.WHITE);
                D.box(30, y + 24, 180, 10, D.LGREY);
                D.rect(32, y + 26, Math.round(176 * k), 6, D.GREEN);
            },
        });
        setTimeout(() => { U.close(o); if (then) then(); }, ms);
        return o;
    };

    /* ---------------------------------------------------------------- navigation --- */
    U.register = s => { U.screens[s.name] = s; };
    U.go = function (name, opts = {}) {
        const s = U.screens[name];
        if (!s || s === U.screen) return;
        if (U.locked() && !opts.force) { U.refuse(B.EDIT); return; }
        const prev = U.screen;
        /* What plays plays on across screens, the loops too, until something else starts
         * (specs/ui/navigation.md). FX from LOOPS does not leave LOOPS: a take goes on, FX works on
         * LOOPS' own setup; going on elsewhere from that FX leaves LOOPS then (specs/ui/screens/fx.md). FX
         * from DJ does not leave DJ either: it works on DJ's own setup */
        const loopsToFx = !!prev && prev.name === "LOOPS" && name === "FX", djToFx = !!prev && prev.name === "DJ" && name === "FX";
        const loopsWas = PTFM.player.loopsShown();
        if (prev && !loopsToFx && !djToFx) {
            if (prev.leave) prev.leave(s);
            if (prev.name === "FX" && U.fxLoops && name !== "LOOPS") U.screens.LOOPS.leave(s);
            if (prev.name === "FX" && U.fxDj && name !== "DJ" && U.screens.DJ.leave) U.screens.DJ.leave(s);
        }
        U.fxLoops = name === "FX" && loopsToFx;
        U.fxDj = name === "FX" && djToFx;
        U.overlays = U.overlays.filter(o => o.full || o.keep);   /* memory full stays until answered; HOME's
                                                                  * combination while SELECT turns the screens */
        PTFM.player.auditionEnd();                          /* a pattern list's preview ends with it */
        selArmed = false;
        if (!opts.deep) U.deep = [];
        if (name === "FX" && prev) U.fxFrom = prev.name;                                   /* FX again: back there (LOOPS too) */
        U.from = prev ? prev.name : null;
        arpOff();                                           /* ARP stays with its screen */
        U.screen = s;
        /* LOOPS shown or left with nothing playing: the keys through the loops' setup at their time, or back to what
         * was held (specs/ui/screens/fx.md#the-fx-setup) */
        if (PTFM.player.loopsShown() !== loopsWas && !PTFM.player.playing) PTFM.audio.fxNow();
        if (s.enter) s.enter(prev ? prev.name : null, opts);
    };
    /* EDIT jumps into the EDITOR go deep: HOME tap returns (5) */
    U.goDeep = function (name, opts, back) {
        const from = U.screen.name;
        U.go(name, Object.assign({ deep: true }, opts));
        U.deep.push({ from, back, locked: !!(opts && opts.locked) });
    };
    U.locked = () => U.deep.length && U.deep[U.deep.length - 1].locked;
    U.isDeep = () => U.deep.length > 0;
    /* HOME tap: back or cancel while something is open; at the top of a screen its combination */
    function homeTap() {
        const o = U.top();
        if (o) { if (o.cancel) o.cancel(); else U.close(o); return; }
        if (U.screen.back && U.screen.back()) return;
        if (U.deep.length) {
            const d = U.deep.pop();
            const keep = U.deep;
            U.go(d.from, { deep: true, force: true, returning: true });
            U.deep = keep;
            if (d.back) d.back();
            return;
        }
        openHome();
    }
    /* SELECT in HOME's combination: the next or previous screen of the round, wrapping */
    const screenStep = d => U.go(ROUND[U.wrap(ROUND.indexOf(U.screen.name) + d, ROUND.length)]);

    /* -------------------------------- HOME's combination (specs/ui/controls.md) --- */
    /* Open while HOME is held (or let go within the grace period), or, after a HOME tap at the top of a
     * screen, as an overlay until it closes. Its members: SELECT (the screens, one a click), a black key (that
     * screen), the white keys (mute a track, held: solo; SEQ switches them to solo; in LOOPS the loops),
     * PLAY (back to the start), REC (all sound off), LFO (the help). A member can act again and again; once
     * one has acted, any other control closes the combination and does its own job. */
    function home() {
        const o = U.overlays.find(x => x.kind === "home");
        if (o) return o;
        const h = held.get(B.HOME);
        if (h) return h.closed ? null : h;
        const g = grace.get(B.HOME);
        return g && U.now() < g.until ? g : null;
    }
    U.home = home;
    U.homeLines = c => homeLines(c);
    function closeHome(c) {
        if (c.kind === "home") U.close(c);
        else { c.closed = true; consume(B.HOME); }         /* held: no combination, and no tap, until let go */
    }
    /* may this member act? Not once another one has: then the combination closes and the control does
     * its own job */
    function member(c, kind) {
        if (c.lock && c.lock !== kind) { closeHome(c); return false; }
        c.lock = kind;
        consume(B.HOME);                                 /* a HOME held meanwhile: no tap when let go */
        return true;
    }
    const loopsKeys = () => !!U.screen.muteKey && !U.screen.soloKey;      /* (a screen whose white keys only mute: none now) */
    /* SEQ switches the white keys between mute and solo as long as they are members: before any member acted
     * and after they did, again and again */
    const seqSwitches = c => (!c.lock || c.lock === "white") && !loopsKeys();
    /* the black keys' screens, two a line by the keys' order, each as printed: `OP1: SONGS  OP2: EDITOR`
     * (the current screen white; a screen not built yet is left out) */
    function screenKeyLines() {
        const ks = Object.keys(SCREEN_KEY).map(Number).sort((a, b) => a - b).filter(n => U.screens[SCREEN_KEY[n]]);
        const label = n => (PTFM.LAYOUT.keys.find(k => k.note === n) || {}).label || "BLANK";
        const entry = n => [[label(n), D.YELLOW], [": " + SCREEN_KEY[n], SCREEN_KEY[n] === U.screen.name ? D.WHITE : D.LGREY]];
        const out = [];
        for (let i = 0; i < ks.length; i += 2) {
            const pad = 19 - (label(ks[i]) + ": " + SCREEN_KEY[ks[i]]).length;
            out.push(ks[i + 1] == null ? entry(ks[i]) : entry(ks[i]).concat([[" ".repeat(pad), D.LGREY]], entry(ks[i + 1])));
        }
        return out;
    }
    /* its lines: what can act; once a member has acted, only that one */
    function homeLines(c) {
        const lp = U.screen.name === "LOOPS", dj = U.screen.name === "DJ", what = lp ? "LOOP" : "TRACK";   /* LOOPS: the loops */
        return [
            ["select", "SELECT: THE SCREENS, ONE BY ONE"],
            ...screenKeyLines().map(l => ["black", l]),
            ["white", c.solo ? "WHITE KEY: SOLO A " + what : "WHITE KEY: MUTE A " + what + ", HELD: SOLO"],
            ["white", dj && "KEYS 1-8: LEFT DECK, 9-16: RIGHT"],       /* DJ: whose tracks they are */
            ["white", dj && U.screen.homeOct && (U.screen.homeOct(B.OCTP) ? "OCT+: TRACKS 08-0F" : U.screen.homeOct(B.OCTM) ? "OCT-: TRACKS 00-07" : null)],
            ["white", c.solo ? "SEQ: WHITE KEYS MUTE INSTEAD" : "SEQ: WHITE KEYS SOLO INSTEAD"],   /* (theirs: on while they act) */
            ["play", dj ? "PLAY: BACK TO THE CUES" : "PLAY: BACK TO THE START"],
            ["rec", "REC: CUT, SILENT WHILE HELD"],
            ["lfo", "LFO: HELP FOR THIS SCREEN"],
        ].filter(([k, l]) => l && (!c.lock || c.lock === k)).map(x => x[1]);
    }
    /* tapped open: an overlay. Its buttons act on their taps (PLAY and REC on press, in buttonDown); a
     * button that is no member closes it and goes on to do its own job (passes) */
    function openHome() {
        U.open({
            kind: "home", opener: B.HOME, keep: true, passes: true, lock: null, solo: false, at: U.now(),
            knob() {},
            tap(b) {
                if (b === B.LFO && member(this, "lfo")) { U.close(this); U.openHelp(); return true; }
                if (b === B.SEQ && seqSwitches(this)) { this.solo = !this.solo; return true; }
                U.close(this);
                return false;
            },
            draw() { homePanel(this, false); },
        });
    }
    /* HOME's combination's panel, held or tapped open (specs/ui/dialogs.md#homes-combination): at the body's
     * bottom; on a screen that gives it a strip (homeArea), once a white key has acted, one
     * line there, so what the keys mute stays in view */
    function homePanel(c, isHeld) {
        const lines = homeLines(c), area = c.lock === "white" && U.screen.homeArea ? U.screen.homeArea() : null;
        if (area && lines.length === 1) {
            const k = lines[0].indexOf(":");
            panel(4, area.y, 232, area.h, null);
            D.runs(8, area.y + 2, F.T, (isHeld ? [["HOLD ", D.CYAN]] : []).concat([["HOME", D.YELLOW], [" + ", D.CYAN],
                                                                                [lines[0].slice(0, k), D.YELLOW], [lines[0].slice(k), D.LGREY]]));
            if (!isHeld) D.runs(8, area.y + 14, F.T, [["HOME", D.YELLOW], [": CLOSE", D.LGREY]]);
            return;
        }
        if (isHeld) comboPanel([["HOLD ", D.CYAN], ["HOME", D.YELLOW], [" +", D.CYAN]], lines);
        else comboPanel([["HOME", D.YELLOW], [" +", D.CYAN]], lines.concat("HOME: CLOSE"));
    }
    function fxTap() {
        if (U.locked()) { U.refuse(B.EDIT); return; }
        if (U.screen.name === "FX") { U.go(U.fxFrom && U.fxFrom !== "FX" ? U.fxFrom : "PLAYBACK"); return; }
        U.go("FX");
    }

    /* ---------------------------------------------------------- saving (8) --- */
    U.saveWaiting = false;
    /* DJ's stand (specs/files/dj.md#the-stand): the decks as they stand count as a change once they differ from
     * the stand stored, one with DJ's setup (the firmware's one record: one change if that is changed already);
     * SAVE keeps them (after a pause, as everything), REVERT and power-on load them */
    const standChanged = () => PTFM.player.djStandChanged(M.state.dj.stand);
    U.unsavedCount = () => M.unsaved + (standChanged() && !M.djSetupUnsaved() ? 1 : 0);
    /* the current instrument is kept by SAVE (the settings record's, specs/files/settings.md): not a change by itself,
     * but SAVE writes it when it differs from the one kept */
    U.savedInst = 0xF000;
    function saveTap() {
        if (!U.unsavedCount() && U.inst === U.savedInst) { U.toast("NOTHING TO SAVE"); return; }
        if (PTFM.player.playing) { U.saveWaiting = true; U.toast("SAVE WAITS FOR A PAUSE"); return; }
        doSave();
    }
    function doSave(after, refs) {                       /* after: once saved (the MIDI CONTROLLER's switch); refs: a
                                                          * reference update's save, the references it updates */
        U.saveWaiting = false;
        /* storage full (specs/ui/saving.md): a save that does not fit in the user area writes nothing and
         * asks; CLEAN UP (its checklist), then the save if it fits now. Its figures count the user area only */
        const st = M.storage();
        if (st.userFree < 0) {
            const cl = M.cleanable(), kbs = b => Math.ceil(b / 1024) + " KB";
            const frees = cl.patterns.items.reduce((a, p) => a + M.bytes.pattern(p), 0) +
                          cl.samples.items.reduce((a, x) => a + M.bytes.sample(x.c.samples[x.k]), 0);
            U.ask(["NOT ENOUGH STORAGE TO SAVE", kbs(-st.userFree) + " MORE NEEDED", "CLEAN UP FREES " + kbs(frees)], ["CLEAN UP", "BACK"], a => {
                if (a === "CLEAN UP" && U.cleanUp) U.cleanUp(() => { if (M.storage().userFree >= 0) doSave(); });
            });
            return;
        }
        const n = U.unsavedCount() || 1;
        U.saving = true;
        U.progress(refs ? "UPDATING " + refs + " REFERENCE" + (refs === 1 ? "" : "S") : "SAVING " + n + " CHANGE" + (n === 1 ? "" : "S"),
                   300 + 40 * Math.min(n, 30), () => {
            M.state.dj.stand = PTFM.player.djStandToSave();   /* (the decks as they stand, paused: no edit of its own) */
            for (const k in U.screens) if (U.screens[k].toSave) U.screens[k].toSave();   /* (LOOPS' selection, as the stand) */
            U.savedInst = U.inst;
            M.save(); U.toast(refs ? "REFERENCES UPDATED, SAVED" : "SAVED");
            U.saving = false;
            if (after) after();
        });
    }
    /* YES to UPDATE REFERENCES? (specs/ui/saving.md#references), the change and its references made: playback pauses,
     * and everything pending is written as one save, UPDATING 3 REFERENCES, then REFERENCES UPDATED, SAVED */
    U.saveRefs = uses => { PTFM.player.pauseAll(); doSave(null, Math.max(1, uses)); };
    /* SETTINGS' MIDI CONTROLLER (specs/ui/screens/settings.md#midi-controller): no way back but switching off and
     * on (here: reloading the page), so it asks first, then about the unsaved changes (saved first, or dropped);
     * then what plays pauses, the sound fades out and stops for good, and the controller (ctl.js) has the panel */
    function controllerGo() {
        PTFM.player.pauseAll();
        PTFM.player.allSoundOff();
        U.overlays.slice().forEach(o => U.close(o));
        PTFM.audio.end();
        PTFM.ctl.start();
    }
    U.controller = function () {
        if (U.saving) { U.toast("A SAVE IS UNDER WAY"); return; }
        U.ask(["SWITCH TO MIDI CONTROLLER?", "PT/FM STOPS UNTIL THE FM-1", "IS SWITCHED OFF AND ON"], ["SWITCH", "BACK"], a => {
            if (a !== "SWITCH") return;
            const n = U.unsavedCount();
            if (!n) { controllerGo(); return; }
            U.ask(["SAVE BEFORE SWITCHING?", n + " UNSAVED CHANGE" + (n === 1 ? "" : "S"), "DROP: THEY ARE LOST"], ["SAVE", "DROP"], b => {
                if (b === "DROP") controllerGo();
                if (b === "SAVE") { PTFM.player.pauseAll(); doSave(controllerGo); }
            });
        });
    };
    /* what plays paused: the screens told (LOOPS: a song's recording ends there), then a SAVE waiting for it */
    U.pausedHook = () => {
        for (const k in U.screens) if (U.screens[k].paused) U.screens[k].paused();
        if (U.saveWaiting) doSave();
    };
    /* a change heard live (an edit, a punch-in): the screens told (LOOPS' RECORD LIVE writes it at its row; t: when it
     * was made, the player's time, an edit's start) */
    const live = t => { for (const k in U.screens) if (U.screens[k].live) U.screens[k].live(t); };
    function revert() {
        const n = U.unsavedCount();
        if (!n) { U.toast("NOTHING TO REVERT"); return; }
        U.ask(["REVERT TO SAVED?", "DROP " + n + " UNSAVED CHANGE" + (n === 1 ? "" : "S"),
               "AND RELOAD FROM FLASH"], ["REVERT", "KEEP"], a => {
            if (a !== "REVERT") return;
            PTFM.player.pauseAll();
            U.progress("RELOADING", 500, () => {
                M.revert();
                PTFM.player.djRestore(M.state.dj.stand);     /* (the decks as saved, as at power-on) */
                PTFM.audio.fxNow();                         /* (what is heard as saved too: the effects' setup again) */
                U.toast("REVERTED TO SAVED");
                if (U.screen.reverted) U.screen.reverted();
            });
        });
    }
    U.revert = revert;
    M.onFull = () => {
        if (U.overlays.some(o => o.full)) return;
        const o = U.ask(["MEMORY FOR EDITS IS FULL", "SAVE BEFORE MAKING MORE EDITS", M.unsaved + " UNSAVED CHANGES"],
                        ["SAVE NOW", "BACK"], a => { if (a === "SAVE NOW") { PTFM.player.pauseAll(); doSave(); } });
        o.full = true;
    };
    /* an edit: refused when the RAM for changes is full. Running low, when a SAVE would help
     * (specs/ui/saving.md, the edit memory): an edit that made it grow, under 25 % of it free, and a save
     * that would leave at least half of it free (here only changes fill it, so a save frees it all): for
     * 2 s, at most once a minute, never over a question */
    let lowAt = -Infinity;
    U.edit = function (fn) {
        const t0 = PTFM.player.time(), was = M.editsUsed, ok = M.edit(fn);
        const top = U.top(), now = U.now();
        if (ok && M.editsUsed > was && M.editsUsed > M.EDITS_MAX * .75 && !(top && top.kind === "question") && now - lowAt > 60000) {
            lowAt = now;
            U.toastRuns([["SAVE", D.YELLOW], [" CHANGES TO FREE MEMORY", D.WHITE]]);
        }
        if (ok) { PTFM.audio.changed(); live(t0); }
        return ok;
    };
    /* REVERT of one thing (specs/ui/saving.md#revert): its question (l1 under it, or none), OCT+ REVERT, OCT- KEEP
     * (HOME cancels); on REVERT then() runs and `REVERTED` shows */
    U.askRevert = function (l0, l1, then) {
        U.ask(l1 ? [l0, l1] : [l0], ["REVERT", "KEEP"], a => {
            if (a !== "REVERT") return;
            then();
            PTFM.audio.changed();
            U.toast("REVERTED");
        });
    };
    /* An edit mode entered with EDIT (specs/ui/controls.md): EDIT leaves it keeping the changes, HOME
     * leaves it dropping them. The mode's session keeps a copy of each thing it edits as it was at the start (add: a key, a
     * getter and a setter; a thing added again keeps its first copy). drop() puts back what changed; with
     * nothing else unsaved, nothing is unsaved again (only REVERT takes a change back after that). */
    U.session = function () {
        const kept = [], same = (a, b) => JSON.stringify(a ?? null) === JSON.stringify(b ?? null);
        return {
            add(key, get, set) {
                if (!kept.some(k => k.key === key)) kept.push({ key, get, set, was: structuredClone(get() ?? null) });
            },
            drop() {
                const ch = kept.filter(k => !same(k.get(), k.was));
                if (!ch.length) return false;
                U.edit(() => ch.forEach(k => k.set(structuredClone(k.was))));
                M.settle();
                U.toast("CHANGES DROPPED");
                return true;
            },
        };
    };

    /* -------------------------------------------- the grammar (specs/ui/controls.md) --- */
    const held = new Map();                      /* button -> {t, used, layer, timer} */
    const grace = new Map();                     /* released modifier -> {until, timer}: its tap waits */
    const keyUse = {};                           /* note -> what a combination took it for */
    const encT = new Array(7).fill(0);
    const holdMs = () => U.set("HOLD DELAY"), graceMs = () => U.set("COMBO GRACE");
    const active = b => (held.has(b) && !held.get(b).closed) || (grace.has(b) && U.now() < grace.get(b).until);
    U.held = b => held.has(b);
    U.holdOf = b => held.get(b);                    /* a held button's state: a screen's combination sets its action (end at release) */
    U.active = active;
    function consume(b) {
        const h = held.get(b);
        if (h) h.used = true;                    /* (its layer stays until it is let go) */
        const g = grace.get(b);
        if (g) { clearTimeout(g.timer); grace.delete(b); }
    }
    U.consume = consume;
    /* A held button's combination (specs/ui/controls.md), HOME's aside (above): a member can act again and
     * again; once one has, another closes the combination (no combination and no tap until the button is
     * let go) and does its own job. useCombo(b, kind): may this member act? */
    function useCombo(b, kind) {
        const h = held.get(b) || grace.get(b);
        if (h && h.lock && h.lock !== kind) { h.closed = true; consume(b); return false; }
        if (h) h.lock = kind;
        consume(b);
        return true;
    }
    /* a control that is no member of the held combinations closes them; it does its own job. Not PLAY,
     * REC, OCT (they act at once), nor a hold that is an action (a screen's own) or PLAY's take */
    function closeCombos(except) {
        for (const [m, h] of held) {
            if (m === except || m === B.HOME || m === B.PLAY || m === B.REC || m === B.OCTM || m === B.OCTP) continue;
            if (h.closed || h.action || h.custom || (U.screen.hold && U.screen.hold(m))) continue;
            h.closed = true; h.used = true;
        }
    }
    /* which buttons wait the grace period on this screen (specs/ui/controls.md) */
    function combines(b) {
        if ([B.HOME, B.LFO, B.SEL, B.FX, B.SAVE].includes(b)) return true;
        if (b === B.ARP) return genericArp() || U.screen.ownArp === "preview";     /* ARP held + SELECT, ALGORITHM, K1, LFO */
        return !!(U.screen.combines && U.screen.combines.includes(b));
    }
    /* button + button combinations, either order (OCT never waits, so it is only ever the second); HOME's
     * own are its combination's (above), LFO held then HOME included */
    function combo(m, b) {
        if (m === B.HOME && b === B.LFO) return () => U.openHelp();
        if (m === B.SAVE && b === B.OCTM) return () => revert();
        if (m === B.SAVE && b === B.OCTP) return () => saveTap();   /* (SAVE held past its layer: let go, no tap) */
        if (U.screen.combo) { const f = U.screen.combo(m, b); if (f) return f; }
        if (m === B.ARP && b === B.LFO && genericArp())             /* the latched notes go, ARP stays as it was */
            return () => { PTFM.player.repeatClear(); U.toast("LATCHED NOTES LET GO"); };
        return null;
    }
    /* FX held: the 16 punch-ins by key as a table, two a line (a group of four on two lines), their full names:
     * the keys' numbers yellow, right-aligned in 2 characters; the names light grey (the one running white),
     * the second column 19 characters in: 37 at most (specs/ui/dialogs.md#hold-layers) */
    const punchLines = () => Array.from({ length: 8 }, (_, l) => [0, 1].flatMap(c => {
        const k = 2 * l + c, name = PTFM.cmds.PUNCHES[k];
        return [[String(k + 1).padStart(2) + " ", D.YELLOW], [c ? name : name.padEnd(16), pin.k === k ? D.WHITE : D.LGREY]];
    }));
    /* ARP held, wherever the arpeggiator is (the EDITOR has its own lines; LOOPS none) */
    const ARP_LINES = ["SELECT: THE RHYTHM, # A NOTE, _ A REST", "ALGORITHM: THE PATTERN", "K1: HOW MANY NOTES LATCH",
                       "LFO: LET THE LATCHED NOTES GO"];
    U.ARP_LINES = ARP_LINES;
    const LAYERS = {
        [B.HOME]: () => () => homeLines(home() || held.get(B.HOME) || {}),
        [B.FX]: () => () => punchLines(),
        /* only what applies where you are (specs/ui/dialogs.md#hold-layers): in a name its groups; else the
         * screen's own lines (marking, big steps), then PRESETS, which turns cartridges everywhere */
        [B.SEL]: () => {
            const o = U.top();
            if (o && o.kind === "text") return ["SELECT IN A NAME: NEXT GROUP"];
            return (U.screen.selLines ? U.screen.selLines() : []).concat(U.screen.noKeyboard ? [] : ["PRESETS: OTHER CARTRIDGE"]);
        },
        [B.SAVE]: () => ["OCT+: SAVE", "OCT-: REVERT ALL"],
        /* DJ: then its LFO pads by key (scr-dj.js lfoLayer) */
        [B.LFO]: () => () => ["HOME: HELP FOR THIS SCREEN"].concat(U.screen.lfoLayer ? U.screen.lfoLayer() : []),
        [B.ARP]: () => U.screen.arpLayer ? U.screen.arpLayer() : genericArp() ? ARP_LINES : null,
    };

    /* an overlay that captures the next FM-1 control (MIDI learn: specs/connections.md): it gets the
     * control instead of its job; HOME stays HOME (it cancels) */
    const capturing = () => { const o = U.top(); return o && o.capture ? o : null; };
    function buttonDown(b) {
        if (U.blocking()) return;
        if (capturing() && b !== B.HOME) { held.set(b, { t: U.now(), used: true }); capturing().capture({ button: b }); return; }
        /* PRESETS' cartridge list closes at any other control, which does its own job; HOME only closes it */
        if (pcEnd() && b === B.HOME) { held.set(b, { t: U.now(), used: true }); return; }
        const t = U.now(), hc = home();
        if (b === B.ARP && pin.k != null && !pin.kept) {             /* a punch-in's key held: ARP holds it on */
            pin.kept = true;
            held.set(b, { t, used: true });
            U.toast(PTFM.cmds.PUNCHES[pin.k] + " HELD ON: ITS KEY STOPS IT");
            return;
        }
        /* HOME's combination: PLAY back to the start, REC all sound off (both on press, tapped open or
         * held); held, LFO the help and SEQ the white keys' mute / solo at once, and any other button closes
         * it. Tapped open, the other buttons go to it on their taps. */
        if (hc && b !== B.HOME) {
            /* DJ: OCT-, OCT+ the tracks the white keys reach, with them (a member of theirs); nowhere to go: nothing */
            if ((b === B.OCTM || b === B.OCTP) && U.screen.homeOct) {
                if (U.screen.homeOct(b) && member(hc, "white")) U.screen.homeOct(b, true);
                else if (hc.kind !== "home") consume(B.HOME);       /* (HOME held: its let-go no tap either way) */
                held.set(b, { t, used: true });
                return;
            }
            if ((b === B.PLAY || b === B.REC) && member(hc, b === B.PLAY ? "play" : "rec")) {
                if (hc.kind === "home") U.close(hc);
                held.set(b, { t, used: true, cut: b === B.REC });
                if (b === B.PLAY) PTFM.player.rewind();
                else U.cut(true);
                return;
            }
            if (hc.kind !== "home") {
                if (b === B.LFO && member(hc, "lfo")) { held.set(b, { t, used: true }); U.openHelp(); return; }
                if (b === B.SEQ && seqSwitches(hc)) { hc.solo = !hc.solo; consume(B.HOME); held.set(b, { t, used: true }); return; }
                if (!hc.closed && b !== B.PLAY && b !== B.REC) closeHome(hc);
            }
        }
        if (b === B.PLAY || b === B.REC) closeCombos();     /* (no button combines with them but HOME) */
        if (b === B.PLAY) {
            const o = U.top();
            if (o && o.play) { held.set(b, { t, used: true }); o.play(); return; }      /* the pattern list: a preview */
            const r = U.screen.playDown ? U.screen.playDown() : false;
            if (r === true) { held.set(b, { t, used: false, custom: true }); startHold(b); return; }
            if (r === "done") { held.set(b, { t, used: true }); return; }
            /* held past the hold delay: stop, back to the start (7) */
            const h = { t, used: true };
            h.timer = setTimeout(() => { if (held.get(b) === h) PTFM.player.stopToStart(); }, holdMs());
            held.set(b, h);
            PTFM.player.playPress(U.screen.name);
            return;
        }
        if (b === B.REC) {
            const h = { t, used: true };
            held.set(b, h);
            const took = U.top() && U.top().rec && U.top().rec();       /* an overlay's own (RECORD PATTERN) */
            if (!took && U.screen.rec) { U.screen.rec(); h.recUp = U.screen.recUp; }   /* (its release: the screen's, DJ's sound back) */
            /* REC held past the hold delay: the screen's own, where it has one */
            if (!took && U.screen.recHold) h.timer = setTimeout(() => { if (held.get(b) === h) U.screen.recHold(); }, holdMs());
            return;
        }
        for (const m of [...held.keys(), ...grace.keys()]) {
            if (m === b || m === B.HOME || m === B.OCTM || m === B.OCTP || !active(m)) continue;    /* HOME's: above */
            const c = combo(m, b) || (b !== B.OCTM && b !== B.OCTP && combo(b, m));
            if (c && useCombo(m, "button " + b)) { closeCombos(m); held.set(b, { t, used: true }); c(); return; }
        }
        closeCombos();
        held.set(b, { t, used: false });
        startHold(b);
        /* a screen's own holds (the EDITOR's pipette) only on the screen itself: under an
         * overlay, HOME's combination tapped open among them, the press is the overlay's (HOME, LFO: the help) */
        if (U.screen.holdStart && !U.top()) U.screen.holdStart(b, held.get(b), graceMs());
    }
    function startHold(b) {
        const h = held.get(b);
        h.timer = setTimeout(() => {
            if (!held.has(b) || h.used) return;
            if (h.custom) { if (U.screen.playHold) U.screen.playHold(); return; }
            if (!U.top() && U.screen.hold && U.screen.hold(b, h)) return;
            if (U.top() && U.top().kind === "home") return;      /* HOME's combination tapped open: a button there acts on its release, however long */
            const lines = LAYERS[b] && LAYERS[b]();
            if (lines) { h.layer = true; h.layerO = U.layer(b, lines); }
        }, holdMs());
    }
    function buttonUp(b) {
        const h = held.get(b);
        held.delete(b);
        if (!h) return;
        if (b === B.SEL && pcFind() && pcFind().held) pcEnd();     /* (PRESETS' list opened with SEL held: gone with it) */
        clearTimeout(h.timer);
        if (h.cut) U.cut(false);                                 /* (CUT let go: heard again) */
        if (h.recUp) h.recUp();
        if (h.layerO) { U.unlayer(h.layerO); if (U.screen.layerUp) U.screen.layerUp(b); }   /* (DJ: LFO's, into the LFO pads) */
        if (h.custom) { if (U.screen.playUp) U.screen.playUp(h.used); return; }
        if (h.used || h.layer || h.action) { if (h.action && h.action.end) h.action.end(); return; }
        if (b === B.PLAY || b === B.REC) return;
        if (combines(b)) {
            const g = { until: U.now() + graceMs() };
            g.timer = setTimeout(() => { grace.delete(b); tap(b); }, graceMs());
            grace.set(b, g);
        } else tap(b);
    }
    function tap(b) {
        if (U.blocking()) return;
        const o = U.top();
        if (b === B.HOME) return homeTap();
        if (o) {
            if (o.tap(b)) return;
            if (b !== B.SAVE && !o.passes) return;           /* HOME's combination: closed, the tap goes on */
        }
        if (b === B.SAVE) return saveTap();
        if (b === B.FX && !(U.screen.tap && U.screen.tap(b))) return fxTap();
        if (b === B.ARP && genericArp()) return arpTap();
        if (b === B.SEL) selArmed = !U.overlays.some(x => !x.passes) && !U.screen.noKeyboard;   /* (PRESETS next: its list) */
        if (U.screen.tap && U.screen.tap(b)) return;
        if (b === B.OCTM || b === B.OCTP) U.octaveStep(b === B.OCTP ? 1 : -1);
    }
    U.tap = tap;
    /* the keys' octave a step up or down, its toast (OCT; on FX's EDIT, SEL + OCT) */
    U.octaveStep = d => {
        U.octave = clamp(U.octave + d, -4, 4);
        U.toast("OCT " + (U.octave > 0 ? "+" : "") + U.octave);
    };

    /* the punch-ins played live (specs/ui/screens/fx.md#punch-in-effects): one at a time, while its key is
     * held; ARP pressed meanwhile holds it on after the key is let go, until its key or another punch-in's */
    const pin = { k: null, kept: false };
    function punchDown(k) {
        if (pin.k === k && pin.kept) { punchOff(); return; }          /* its own key again: off */
        PTFM.audio.punch(k, true);                                      /* (another running is replaced) */
        pin.k = U.punch = k; pin.kept = false;
        live();
    }
    function punchUp(k) {
        if (pin.k !== k || pin.kept) return;
        PTFM.audio.punch(k, false);
        pin.k = U.punch = null;
        live();
    }
    function punchOff() {
        if (pin.k == null) return;
        PTFM.audio.punch(pin.k, false);
        pin.k = U.punch = null; pin.kept = false;
        live();
    }
    U.punchDown = punchDown; U.punchUp = punchUp; U.punchOff = punchOff;
    /* all sound off: every voice let go at once, a held-on punch-in too */
    U.allSoundOff = function () { PTFM.player.allSoundOff(); punchOff(); U.toast("ALL SOUND OFF"); };
    /* CUT (HOME + REC, REC held as it; specs/ui/controls.md#cut): all sound off, silent while held, what plays playing
     * on unheard; let go, heard again, the effects starting dry. Held 2 s, every effect of the setup FX would edit from
     * this screen is switched off: an edit (SAVE keeps it, REVERT brings them back; none on: only the toast) */
    let cutFx = null;
    U.cut = function (on) {
        U.cutOn = !!on;
        clearTimeout(cutFx);
        cutFx = null;
        if (on) {
            PTFM.player.allSoundOff(); punchOff(); U.toast("SOUND CUT: LET GO OF REC");
            cutFx = setTimeout(cutFxOff, 2000);
        }
        PTFM.audio.hush(!!on);
    };
    function cutFxOff() {
        if (!U.cutOn) return;
        const s = PTFM.audio.fxSong();
        if (s && Object.values(s.fx.on).some(Boolean)) U.edit(() => { for (const k of Object.keys(s.fx.on)) s.fx.on[k] = false; });
        U.toast("SOUND CUT, EFFECTS OFF");
    }
    U.punchKept = () => pin.kept;
    /* the punch-ins' keys (FX held, FX LIVE): each group of four's first dim, the rest dark, the one running bright */
    U.punchLeds = keys => WHITE.forEach((n, t) => { keys[n] = pin.k === t ? 1 : t % 4 === 0 ? DIM : 0; });

    function noteDown(n) {
        if (U.blocking()) return;
        if (capturing()) { keyUse[n] = { kind: "overlay", o: capturing() }; capturing().capture({ key: n }); return; }
        pcEnd();                                            /* (PRESETS' cartridge list: closed, the key plays) */
        const bi = U.blackIndex(n), wi = U.whiteIndex(n), hc = home();
        /* HOME's combination: a black key goes to its screen (tapped open: and closes it), a white key mutes
         * its track on release, or solos it (SEQ's SOLO, or held past the hold delay); LOOPS: its loop */
        if (hc && member(hc, bi >= 0 ? "black" : "white")) {
            if (bi >= 0) {
                keyUse[n] = { kind: "screen" };
                if (hc.kind === "home") U.close(hc);
                const name = SCREEN_KEY[n];
                if (!name) return;
                if (U.locked()) { U.refuse(B.EDIT); return; }
                U.go(name);
            } else {
                const k = keyUse[n] = { kind: "mute", t: wi, solo: !!hc.solo && !loopsKeys(), held: false };
                if (!loopsKeys() && !k.solo) k.timer = setTimeout(() => { k.held = true; solo(wi); }, holdMs());
            }
            return;
        }
        if (active(B.FX) && wi >= 0 && useCombo(B.FX, "punch")) {
            closeCombos(B.FX);
            keyUse[n] = { kind: "punch", k: wi };
            punchDown(wi);
            return;
        }
        closeCombos();
        const o = U.top();
        if (o && o.key && o.key(n, true)) { keyUse[n] = { kind: "overlay", o }; return; }
        if (U.screen.key && U.screen.key(n, true)) { keyUse[n] = { kind: "screen-key", s: U.screen }; return; }   /* (its release goes back there) */
        U.lastKey = n;
        if (U.arp && genericArp()) {                        /* ARP on: the key feeds the arpeggio, not a note of its own */
            const m = U.midi(n);
            PTFM.player.repeatKey(m, U.inst, !U.keysHeld().length);
            keyUse[n] = { kind: "arp", midi: m };
            return;
        }
        U.keyOn(n);
        keyUse[n] = { kind: "play" };
    }
    function noteUp(n) {
        const k = keyUse[n];
        delete keyUse[n];
        if (!k) return;
        if (k.kind === "mute") {
            clearTimeout(k.timer);
            if (k.held) { /* solo: on at the hold */ }
            else if (k.solo) solo(k.t);                                 /* SEQ switched the white keys to solo */
            else if (U.screen.muteKey) U.screen.muteKey(k.t);          /* LOOPS: white key n mutes loop n */
            else { U.mutes[k.t] = !U.mutes[k.t]; U.toast("TRACK " + M.hex(k.t) + (U.mutes[k.t] ? " MUTED" : " ON")); PTFM.player.mutesChanged(); }
        } else if (k.kind === "punch") punchUp(k.k);
        else if (k.kind === "overlay") { if (k.o.key) k.o.key(n, false); }
        /* a key's release goes to the screen that took its press, though the screen changed meanwhile (FX
         * LIVE's punch-in held while going elsewhere: else it never ends, replaying the old sound under the new) */
        else if (k.kind === "screen-key") { const s = k.s || U.screen; if (s.key) s.key(n, false); }
        else if (k.kind === "arp") PTFM.player.repeatKeyUp(k.midi);
        else if (k.kind === "play") U.keyOff(n);
    }
    function solo(t) {
        if (U.screen.soloKey) return U.screen.soloKey(t);   /* DJ: a deck's track, within its deck */
        U.solo = U.solo === t ? -1 : t;
        U.toast(U.solo < 0 ? "SOLO OFF" : "TRACK " + M.hex(t) + " SOLO");
        PTFM.player.mutesChanged();
    }
    U.keysHeld = () => Object.keys(keyUse).filter(n => ["play", "screen-key", "arp"].includes(keyUse[n].kind)).map(Number);

    /* PRESETS' cartridge list (specs/ui/dialogs.md#presets-cartridge-list): SEL + PRESETS, SEL held or tapped first,
     * opens the cartridge list over the screen, the current cartridge highlighted; PRESETS goes on picking; any other
     * control closes it and does its own job (HOME only closes it); opened with SEL held, it closes when SEL is let go
     * (PRESETS the instruments again). selArmed (declared with the grammar's state): SEL's tap reached the screen and
     * nothing else was touched since; every overlay then open is one that tap opened */
    const pcFind = () => U.overlays.find(o => o.presetsList);
    function pcOpen() {
        const o = U.cartList("CARTRIDGES", U.inst >> 8, () => {});
        return Object.assign(o, { opener: B.SEL, presetsList: true, knob() {}, tap: () => false, cancel() { U.close(o); } });
    }
    /* a control other than PRESETS: the list closes (true if it was open) and SEL's tap no longer counts */
    function pcEnd() {
        selArmed = false;
        const o = pcFind();
        if (!o) return false;
        U.close(o);
        return true;
    }
    /* whether the list can open now: nothing over the screen, or only the menus SEL's tap opened (closed for it) */
    const pcRoom = armed => !U.screen.noKeyboard && U.overlays.every(o => armed && o.kind === "menu");
    U.presetsListOpen = () => !!pcFind();

    /* faster turns, bigger steps: by the time between clicks, as they came (a click from MIDI carries
     * its message's time stamp: clicks the page reads in one go are not a fast turn) */
    function accel(e, at) {
        const t = at != null ? at : U.now(), dt = t - encT[e];
        encT[e] = t;
        const sp = U.set("KNOB SPEED");
        if (sp === "OFF" || e === E.PRESETS) return 1;
        if (sp === "HIGH") return dt < 30 ? 8 : dt < 55 ? 4 : dt < 90 ? 2 : 1;
        return dt < 30 ? 4 : dt < 60 ? 2 : 1;
    }
    function encoder(e, step, at) {
        if (U.blocking()) return;
        if (capturing()) { capturing().capture({ knob: e }); return; }
        const steps = step * accel(e, at), hc = home();
        /* HOME's combination: SELECT a screen a click, unaccelerated; any other knob closes it and turns */
        if (hc) {
            if (e === E.SELECT && member(hc, "select")) { screenStep(step); return; }
            if (U.overlays.includes(hc) || !hc.closed) closeHome(hc);
        }
        /* the held combinations' members: SEL + PRESETS the cartridges, + SELECT / ALGORITHM a mark, + K1-K4
         * big steps; ARP + SELECT the rhythm. Any other knob closes them and turns as it would. */
        if (e === E.PRESETS && U.screen.noKeyboard) { pcEnd(); closeCombos(); return; }   /* (DJ: no instrument, no cartridges) */
        if (e === E.PRESETS) {
            let cart = active(B.SEL) && useCombo(B.SEL, "presets");
            closeCombos(cart ? B.SEL : undefined);
            /* PRESETS' cartridge list (specs/ui/dialogs.md#presets-cartridge-list): open already (its next click), or to
             * open: SEL held, in its grace, or tapped just before (its menu then gives the list its place) */
            const list = pcFind() ? 1 : (cart || selArmed) && pcRoom(!cart && selArmed) ? 2 : 0;
            selArmed = false;
            if (list) cart = true;
            const was = U.inst;                      /* the screen's own: INSTRUMENTS' (afloat: nothing) */
            const own = cart ? !!(U.screen.presetsCart && U.screen.presetsCart(step)) : !!(U.screen.presets && U.screen.presets(step));
            if (!own) { if (cart) U.stepCart(step); else U.stepInst(step); }
            const c = M.cart(U.inst >> 8);
            /* shown as every knob (specs/ui/frame.md): `PRESETS 01:03 E.PIANO`, with SEL `CARTRIDGE 01 KEYS/ORGANS`;
             * a screen's own that changed nothing has said why */
            if (!own || U.inst !== was) {
                if (cart) U.showKnob("CARTRIDGE", M.hex(U.inst >> 8) + " " + (c ? c.name : ""));
                else { const ins = M.inst(U.inst); U.showKnob("PRESETS", M.refText(U.inst) + " " + (ins ? ins.name : "EMPTY")); }
                if (list === 2) { U.overlays = U.overlays.filter(x => x.keep); pcOpen().held = held.has(B.SEL); }
                if (pcFind()) pcFind().sel = Math.max(0, M.cartIndex(U.inst >> 8));
            }
            if (U.screen.presetsMoved) U.screen.presetsMoved();
            return c;
        }
        pcEnd();
        if (U.screen.encHeld && U.screen.encHeld(e, steps)) return;
        /* ARP held: SELECT the rhythm (the EDITOR's row preview's too), ALGORITHM the pattern, K1 the latch
         * (the arpeggiator's: not on the EDITOR); in LOOPS nothing */
        const arpKnob = genericArp() ? e === E.SELECT || e === E.ALGORITHM || e === E.K1 : U.screen.ownArp === "preview" && e === E.SELECT;
        if (active(B.ARP) && arpKnob && useCombo(B.ARP, "arp")) {
            closeCombos(B.ARP);
            if (e === E.SELECT) U.stepRhythm(step);
            else if (e === E.ALGORITHM) U.stepArpPattern(step);
            else U.stepArpLatch(step);
            return;
        }
        const o = U.top();
        const sel = active(B.SEL) && useCombo(B.SEL, e === E.SELECT || e === E.ALGORITHM ? "mark" : "big");
        closeCombos(sel ? B.SEL : undefined);
        if (o) {
            if (sel && o.mark && (e === E.SELECT || e === E.ALGORITHM)) o.mark(e, steps);
            else o.knob(e, steps, sel);
            return;
        }
        if (sel && (e === E.SELECT || e === E.ALGORITHM) && U.screen.mark) U.screen.mark(e, steps);
        else if (U.screen.knob) U.screen.knob(e, steps, sel);
    }

    /* ----------------------------------------------------------------------- LEDs --- */
    const DIM = .25;
    function blink(period) { return Math.floor(U.now() / period) % 2 === 0; }
    U.blink = blink;
    /* a breath (specs/ui/leds.md): dim to full and back in 2 s, even to the eye (the level grows
     * exponentially), full at t0 */
    const BREATH_MS = 2000;
    function breath(t0 = 0) {
        const s = .5 + .5 * Math.cos(2 * Math.PI * ((U.now() - t0) % BREATH_MS) / BREATH_MS);
        return DIM * Math.pow(1 / DIM, s);
    }
    function leds(dev) {
        const lv = new Array(14).fill(0);
        const s = U.screen, does = s.does || [];
        for (const b of [B.HOME, B.SAVE, B.PLAY, B.FX, B.SEL, B.OCTM, B.OCTP]) lv[b] = DIM;
        for (const b of does) lv[b] = DIM;
        if (genericArp()) lv[B.ARP] = U.arp ? 1 : DIM;      /* the arpeggiator: lit while on */
        if (U.overlays.some(o => o.kind === "help")) lv[B.HOME] = 1;
        const o = U.top();
        if (o && o.opener !== undefined && o.kind !== "question") lv[o.opener] = 1;
        if (s.lit) for (let b = 0; b < 14; b++) { const v = s.lit(b); if (v !== undefined) lv[b] = v; }
        if (o && o.kind === "pattern") { lv[B.REC] = DIM; lv[B.SEQ] = o.noteKeys ? 1 : DIM; }   /* RECORD PATTERN: REC writes, SEQ the keys (PLAY below) */
        if (o && o.mv) lv[B.SEL] = 1;                       /* an item afloat in a list: SEL puts it down */
        if (U.isDeep()) lv[B.EDIT] = 1;
        if (pin.k != null) { if (pin.kept) lv[B.FX] = 1; else lv[B.ARP] = Math.max(lv[B.ARP], DIM); }   /* a punch-in: held on (ARP holds it) */
        /* SAVE: dim while unsaved, dark with nothing to save, blinking dim while a save waits */
        lv[B.SAVE] = U.saveWaiting ? (blink(250) ? DIM : 0) : U.unsavedCount() ? DIM : 0;   /* (DJ's stand counts) */
        /* OCT: their own rule, counted from OCT 0 */
        const oct = (b, k) => k <= 0 ? 0 : k === 1 ? DIM : k === 2 ? 1 : (blink(k === 3 ? 500 : 150) ? 1 : 0);
        if (s.name !== "FX" && s.name !== "DJ") { lv[B.OCTM] = oct(B.OCTM, -U.octave); lv[B.OCTP] = oct(B.OCTP, U.octave); }   /* (DJ: HOME's mutes' tracks) */
        /* PLAY: green playing (brighter on the beat), red dim with a loop; red paused; at the start red
         * dim; dark where PLAY has nothing to play (it would blink) */
        const P = PTFM.player;
        let green = 0, red = 0;
        if (P.playing) { green = P.onBeat() ? 1 : .55; if (P.what !== "song" && P.what !== "dj") red = DIM; }
        else if (P.canPlay(s.name)) red = P.what && !P.atStart ? 1 : DIM;
        if (o && o.kind === "pattern") { red = 0; green = o.pv ? 1 : DIM; }   /* RECORD PATTERN: PLAY its preview, green while heard */
        lv[B.PLAY] = red;
        if (o && o.lit) for (let b = 0; b < 14; b++) { const v = o.lit(b); if (v !== undefined) lv[b] = v; }   /* an overlay's own */
        /* a question (specs/ui/leds.md): its two answers lit, HOME dim (it cancels), the rest dark: nothing else acts */
        if (o && o.kind === "question") { lv.fill(0); lv[B.OCTM] = lv[B.OCTP] = 1; lv[B.HOME] = DIM; }
        /* HOME's combination: what can act lights, the rest is dark; HOME breathes, tapped open or held; SEQ
         * lit while the white keys solo. Once a member has acted, only its own. */
        const hc = home(), can = k => !hc.lock || hc.lock === k;
        if (hc) {
            lv.fill(0);
            lv[B.HOME] = breath(hc.at || hc.t || 0);                /* from when it opened: the overlay's, the press's */
            if (can("play")) lv[B.PLAY] = DIM;
            if (can("rec")) lv[B.REC] = DIM;
            if (can("lfo")) lv[B.LFO] = DIM;
            if (seqSwitches(hc)) lv[B.SEQ] = hc.solo ? 1 : DIM;
            if (can("white") && s.homeOct) for (const b of [B.OCTM, B.OCTP]) lv[b] = s.homeOct(b) ? DIM : 0;   /* DJ: tracks that way */
        }
        for (const b in U.flash) if (U.now() < U.flash[b]) lv[b] = blink(80) ? 1 : 0;     /* a refusal, over all */
        for (let b = 0; b < 14; b++) dev.led(b, hc && b === B.HOME ? lv[b] : Math.max(lv[b], held.has(b) ? .55 : 0));
        dev.led(dev.LED_PLAY_GREEN, green);
        /* the keys, in layers */
        const keys = new Array(27).fill(0);
        if (hc) {
            /* the screens: the current one bright; the tracks: dim while they play, dark muted, a solo bright */
            if (can("black") || hc.lock === "select") for (const n in SCREEN_KEY) keys[n] = SCREEN_KEY[n] === s.name ? 1 : DIM;
            if (!can("white")) { /* the white keys are no members any more */ }
            else if (s.muteLeds) s.muteLeds(keys);              /* LOOPS: a key a loop, dim while it sounds */
            else WHITE.forEach((n, t) => { keys[n] = U.solo === t ? 1 : U.muted(t) ? 0 : DIM; });
        } else if (active(B.FX)) {
            U.punchLeds(keys);
        } else {
            const took = (o && o.keyLeds && o.keyLeds(keys)) || (s.keyLeds && s.keyLeds(keys));   /* a dialog's or a screen's own: steps */
            if (!took && PTFM.audio.running) keyGlow(keys);
            else if (!took) for (const n in keyUse) if (keyUse[n].kind === "play" || keyUse[n].kind === "screen-key") keys[n] = Math.max(keys[n], 1);
        }
        for (let n = 0; n < 27; n++) dev.led(dev.NOTE0 + n, keys[n]);
    }

    /* ---------------------------------------------------------------------- frame --- */
    /* OVER 9000: the estimate's mean over each second (a sample a frame drawn); on above 90 %, off after a second at
     * 90 % or less (the firmware: its halves' own times, 172 halves) */
    const o9 = { at: 0, sum: 0, sum2: 0, n: 0, on: false };
    function over9000() {
        const t = U.now(), st = PTFM.audio.stats;
        if (!o9.at) o9.at = t;
        o9.sum += st.cpu;
        o9.sum2 += PTFM.audio.c2On() ? st.cpu2 : 0;          /* (either core's) */
        o9.n++;
        if (t - o9.at < 1000) return;
        o9.on = Math.max(o9.sum, o9.sum2) / o9.n > 90;
        o9.at = t; o9.sum = 0; o9.sum2 = 0; o9.n = 0;
    }
    U.over9000 = () => o9.on;
    function headerText() {
        /* the screen's name in its group's colour, its subpage white (FX LIVE, VISUAL SCOPE, EDITOR 0C), a
         * page count light grey (specs/ui/frame.md) */
        let text = null, col = D.WHITE, sub = null, page = null, seq = null, runs = null, mark = null;
        const help = U.overlays.find(o => o.kind === "help");
        if (knobShow && U.now() < knobShow.until) { text = knobShow.text; col = D.WHITE; }   /* (a knob turned) */
        else if (toast && U.now() < toast.until) { text = toast.text; col = D.WHITE; runs = toast.runs || null; }
        else if (help) { text = help.header; col = D.CYAN; page = help.pageText(); }
        else {
            const s = U.screen;
            text = s.title ? s.title() : s.name;
            col = GROUP_COL[s.group];
            sub = s.sub ? s.sub() : null;
            page = s.page ? s.page() : null;
            seq = s.seq && !U.overlays.length ? s.seq() : null;   /* where SEQ switches pages or views: where it stands */
            if (s.name === "LOOPS") mark = "BETA";      /* (the name's mark, small: frame.md) */
        }
        const full = [text, sub, page].filter(Boolean).join(" "), room = 200;
        let end;
        if (D.tw(F.S, full) <= room) {
            let x = runs ? D.runs(2, 0, F.S, runs) : D.text(2, 0, F.S, text, col);
            if (mark) x = D.text(x + 2, 0, F.T, mark, D.DGREY);   /* (T's capitals from row 2, as S's) */
            if (sub) x = D.text(x + 8, 0, F.S, sub, D.WHITE);
            end = page ? D.text(x + 8, 0, F.S, page, D.LGREY) : x;
        } else {
            let x = runs ? D.runs(2, 2, F.T, runs) : D.text(2, 2, F.T, text.slice(0, 33), col);   /* 33 end before the voices at 203 */
            if (sub && D.tw(F.T, [text, sub].join(" ")) <= room) x = D.text(x + 6, 2, F.T, sub, D.WHITE);
            end = page && D.tw(F.T, full) <= room ? D.text(x + 6, 2, F.T, page, D.LGREY) : x;
        }
        /* SEQ and where it stands (`SEQ 0/2`, `SEQ 8 TRACKS`), right-aligned before the transport, where the
         * header leaves room (specs/ui/frame.md) */
        if (seq) {
            const sx = 199 - D.tw(F.T, "SEQ " + seq);
            if (end + 8 <= sx) D.runs(sx, 2, F.T, [["SEQ", D.YELLOW], [" " + seq, D.LGREY]]);
        }
    }
    /* the header's right part (specs/ui/frame.md#the-header): the transport, the CPU bar and the voices, the battery,
     * each in a box of its own fixed width and the text's height, rows 2-11 (where S's digits and capitals are) */
    const DIGITS_3X10 = [                       /* 10 and more voices: two narrow digits */
        ["###", "#.#", "#.#", "#.#", "#.#", "#.#", "#.#", "#.#", "#.#", "###"],
        [".#.", "##.", ".#.", ".#.", ".#.", ".#.", ".#.", ".#.", ".#.", "###"],
        ["###", "..#", "..#", "..#", "###", "#..", "#..", "#..", "#..", "###"],
        ["###", "..#", "..#", "..#", "###", "..#", "..#", "..#", "..#", "###"],
        ["#.#", "#.#", "#.#", "#.#", "###", "..#", "..#", "..#", "..#", "..#"],
        ["###", "#..", "#..", "#..", "###", "..#", "..#", "..#", "..#", "###"],
        ["###", "#..", "#..", "#..", "###", "#.#", "#.#", "#.#", "#.#", "###"],
        ["###", "..#", "..#", "..#", "..#", ".#.", ".#.", ".#.", ".#.", ".#."],
        ["###", "#.#", "#.#", "#.#", "###", "#.#", "#.#", "#.#", "#.#", "###"],
        ["###", "#.#", "#.#", "#.#", "###", "..#", "..#", "..#", "..#", "###"],
    ];
    function voicesNow(x) {                     /* 7 x 10 at x, 2 */
        const n = Math.min(99, PTFM.audio.stats.usedHalf || 0), c = U.voicesCol(n, PTFM.audio.voicesInForce());   /* (the
                                                                                         * most over the last half second) */
        if (n < 10) { D.text(x - 1, 0, F.S, String(n), c); return; }      /* (S's digit: its glyph from x) */
        [Math.floor(n / 10), n % 10].forEach((d, k) =>                    /* (the tens, then the units, a column apart) */
            DIGITS_3X10[d].forEach((row, j) => { for (let i = 0; i < 3; i++) if (row[i] === "#") D.px(x + 4 * k + i, 2 + j, c); }));
    }
    /* the transport, 7 x 10 at x, 2 whichever it shows: ▶, ❚❚, ● (a disc 7 x 8 on the text's middle) */
    function transport(x, kind, c) {
        if (kind === "play") [1, 3, 4, 6, 7, 7, 6, 4, 3, 1].forEach((w, j) => D.rect(x, 2 + j, w, 1, c));
        else if (kind === "pause") { D.rect(x, 2, 2, 10, c); D.rect(x + 5, 2, 2, 10, c); }
        else [3, 5, 7, 7, 7, 7, 5, 3].forEach((w, j) => D.rect(x + ((7 - w) >> 1), 3 + j, w, 1, c));
    }
    /* the battery, 7 x 10 at x, 2: standing, its nub on top; inside, three bars lit from the bottom; with none left,
     * its shell red. Charging (USB: a computer powers it), the bars fill from the bottom in turn, light blue, a step
     * every half second */
    function battery(x, bars, usb) {
        if (usb) bars = 1 + Math.floor(U.now() / 500) % 3;
        const bcol = usb ? D.LBLUE : bars >= 2 ? D.GREEN : D.YELLOW, shell = !usb && !bars ? D.RED : D.LGREY;
        D.rect(x + 2, 2, 3, 1, shell);
        D.box(x, 3, 7, 9, shell);
        for (let i = 0; i < 3; i++) D.rect(x + 2, 9 - 2 * i, 3, 1, i < bars ? bcol : D.DEEP + D.DGREY);
    }
    function header() {
        D.rect(0, 0, 240, 16, D.BLACK);
        /* OVER 9000 (specs/ui/frame.md#the-header), the CPU's mean over a second above 90 %: it alone, the header's
         * text blank, the symbols on the right as ever */
        over9000();
        if (o9.on) D.text(52, 0, F.S, "!!! OVER 9000 !!!", D.RED);
        else headerText();
        /* the transport, the CPU bar and the voices (a pair: 1 px apart), the battery: drawn the same always */
        const P = PTFM.player;
        if (P.recording()) transport(207, "rec", D.RED);
        else if (P.playing) transport(207, "play", D.GREEN);
        else if (P.what) transport(207, "pause", D.RED);
        voicesNow(222);
        /* (each core's mean over the last tenth of a second; 4 x 10 at 217, two columns of 2: core 1's, core 2's only
         * while it runs) */
        const st = PTFM.audio.stats, late = U.now() - st.lateAt < 400;
        D.rect(217, 2, 4, 10, D.DEEP + D.DGREY);
        const bar = (x, cpu) => {
            const h = Math.min(10, Math.max(1, Math.round(cpu / 100 * 10)));
            D.rect(x, 12 - h, 2, h, late ? (blink(80) ? D.RED : D.BLACK) : cpu < 50 ? D.GREEN : cpu < 80 ? D.YELLOW : D.RED);
        };
        bar(217, st.cpuNow);
        if (PTFM.audio.c2On()) bar(219, st.cpu2Now);
        battery(232, st.battery, st.usb);
        D.hline(0, 16, 240, D.DGREY);
    }
    function footer() {
        D.rect(0, 227, 240, 13, D.BLACK);
        D.hline(0, 227, 240, D.DGREY);
        const own = U.screen.footInst ? U.screen.footInst() : null;    /* (LOOPS: a re-record's loop's, the preview's) */
        const ref = own != null ? own : U.inst, t = M.instType(ref), ins = M.inst(ref);
        let left = M.refText(ref) + " " + (ins ? ins.name : "EMPTY");
        if (U.screen.name !== "EDITOR" && t) left += " " + M.ENGINES[t].short;
        const right = U.screen.footer ? U.screen.footer() : null;   /* (a knob turned shows in the header) */
        let room = 40;
        if (right && right[0]) room = 40 - right[0].length - 1;
        /* a screen where the instrument plays no part (DJ) shows key hints there instead, as runs */
        const hints = U.screen.footHints ? U.screen.footHints() : null;
        if (hints) {
            let n = Math.max(0, room);
            D.runs(0, 228, F.T, hints.map(([s, c]) => { const p = s.slice(0, n); n -= p.length; return [p, c]; }));
        } else D.text(0, 228, F.T, left.slice(0, Math.max(0, room)), ins ? M.ENGINES[t].col : D.DGREY);
        if (right && right[0]) {                            /* (a screen's may come as runs: [text, colour, runs]) */
            const x = 240 - D.tw(F.T, right[0]);
            if (right[2]) D.runs(x, 228, F.T, right[2]);
            else D.text(x, 228, F.T, right[0], right[1]);
        }
    }
    /* the count-in's box (specs/ui/transport.md), over whatever screen counts in: in the body's middle,
     * the beats left as a digit 32 x 64, a dot a beat, the bar; armed (COUNT-IN KEY), what starts it */
    function countBox() {
        const c = PTFM.player.countInState(), armed = U.screen.armed && U.screen.armed();
        if (!c && !armed) return;
        const x = 50, w = 140, h = 118, y = BODY_Y + Math.floor((BODY_H - h) / 2);
        D.rect(x, y, w, h, D.BLACK);
        D.box(x, y, w, h, D.RED);
        if (!c) {
            D.text(D.centre(F.S, "TAKE ARMED", x, w), y + 12, F.S, "TAKE ARMED", D.RED);
            D.text(D.centre(F.L, "PLAY", x, w), y + 36, F.L, "PLAY", D.WHITE);
            D.text(D.centre(F.S, "A NOTE", x, w), y + 74, F.S, "A NOTE", D.WHITE);
            D.runs(D.centre(F.T, "HOME: CANCEL", x, w), y + h - 16, F.T, [["HOME", D.YELLOW], [": CANCEL", D.LGREY]]);
            return;
        }
        D.text(D.centre(F.T, "COUNT-IN", x, w), y + 6, F.T, "COUNT-IN", D.RED);
        const left = String(4 - c.beat);
        D.text(D.centre(F.X, left, x, w), y + 20, F.X, left, c.beat === 0 ? D.YELLOW : D.WHITE);
        for (let i = 0; i < 4; i++) D.rect(x + 45 + i * 14, y + 92, 8, 8, i > c.beat ? D.DGREY : i === 0 ? D.YELLOW : D.WHITE);
        const bar = "BAR " + (c.bar + 1) + " OF " + c.bars;
        D.text(D.centre(F.T, bar, x, w), y + h - 14, F.T, bar, D.LGREY);
    }
    U.render = function (lcd, dev) {
        if (PTFM.ctl && PTFM.ctl.on) { PTFM.ctl.render(lcd, dev); return; }   /* (MIDI CONTROLLER: the tracker ended) */
        D.noclip();
        D.clear(D.BLACK);
        D.clip(0, BODY_Y, 240, BODY_H);
        U.screen.draw();
        countBox();
        for (const o of U.overlays) o.draw();
        for (const l of U.layers) l.draw();
        D.noclip();
        header();
        footer();
        D.blitTo(lcd);
        leds(dev);
    };

    /* ------------------------------------------------------------------- start --- */
    U.attach = function (dev) {
        const ctl = () => PTFM.ctl && PTFM.ctl.on ? PTFM.ctl : null;   /* (MIDI CONTROLLER: the panel is its alone) */
        dev.on("button", ev => ctl() ? ctl().button(ev.id, ev.down) : ev.down ? buttonDown(ev.id) : buttonUp(ev.id));
        dev.on("note", ev => ctl() ? ctl().note(ev.note, ev.down) : ev.down ? noteDown(ev.note) : noteUp(ev.note));
        dev.on("encoder", ev => ctl() ? ctl().encoder(ev.id, ev.step, ev.t) : encoder(ev.id, ev.step, ev.t));
        dev.on("master", ev => {
            if (ctl()) { ctl().master(ev.value); return; }
            PTFM.audio.master(ev.value);
            if (capturing()) capturing().capture({ master: true });
        });
    };
    U.start = function () {
        U.octave = U.set("KEYS OCTAVE");
        if (M.state.dj.stand) PTFM.player.djRestore(M.state.dj.stand);   /* DJ's stand as saved, paused */
        const first = U.set("POWER-ON SCREEN");
        U.go(first === "LAST" ? "STATUS" : first);
    };

    function clamp(v, a, b) { return v < a ? a : v > b ? b : v; }
    U.clamp = clamp;
    U.wrap = (v, n) => ((v % n) + n) % n;
    /* a count of voices (specs/ui/colours.md) against the limit in force: red at it, yellow from 4 under it, light grey */
    U.voicesCol = (n, limit) => n >= limit ? D.RED : n >= limit - 4 ? D.YELLOW : D.LGREY;
    return U;
})();

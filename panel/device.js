/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM panel (shared by the playground and the emulator): the virtual FM-1's controls, made to be
 * easy to use rather than to look like the device (specs/panel.md). One big screen, and around it
 * small knobs, buttons and the 27 keys, each lit as its LED is. Three views, picked by the window:
 *   WIDE      a computer: SELECT, ALGORITHM, PRESETS, MASTER and OCT- / OCT+ left of the screen,
 *             K1-K4 and the 12 buttons right of it, the keys below; the screen's size is dev.zoom's;
 *   PORTRAIT  a phone held upright (or any upright window under 760 px): the screen across the top,
 *             the knobs, buttons and keys under it, sized for fingers; the keys in two rows;
 *   SIDEWAYS  a phone held sideways (or any window lying down at most 520 px high): the screen as high
 *             as the keys' row below it leaves, the knobs, MASTER and OCT left of it, K1-K4 and the
 *             buttons (as the panel has them, two rows of six) right of it.
 * The panel sizes itself to its container (the page's room for it), not to the window.
 *
 * const dev = PTFM.createDevice(container) builds it inside container and returns:
 *   dev.on(type, fn)    input events, with the firmware's ids (hal/fm1_input.h):
 *                         "button"  {id 0-13, name, down}
 *                         "note"    {note 0-26 (F3-G5), name, down}
 *                         "encoder" {id 0-6, name, step +1 / -1, t}: a click, the delta (the FM-1's
 *                                   encoders are endless: no position, no range); t, from dev.turn,
 *                                   when the click came (performance.now's clock), else undefined
 *                         "master"  {value 0-1}: MASTER is a pot, a position
 *                       and one output event:
 *                         "led"     {id, level}: an LED's level changed (dev.led with a new level)
 *                       "*" gets every event.
 *   dev.led(id, level)  an LED, 0 (off) to 1 (on); the firmware's dim is 1/4. Ids as fm1_led_key:
 *                       buttons 0-13, note keys dev.NOTE0 + note; PLAY has a second, green LED,
 *                       dev.LED_PLAY_GREEN (its own LED, 12, is red).
 *   dev.turn(id, step, t)  a knob's click from code (encoder id 0-6, step +1 / -1), as the wheel's;
 *                       t: when it came, if known (a MIDI message's time stamp), so a page that
 *                       speeds knobs up by the time between clicks is not fooled by clicks it reads late;
 *   dev.setMaster(v)    MASTER from code (0-1), as dragging it: both for another input (midi.js).
 *   dev.glass           what the screen shows: a 240 x 240 2D context; dev.present() puts it on the
 *                       page. Draw through the virtual screen driver (lcd.js), not here.
 *   dev.state           {buttons, notes}: bit masks as fm1_in's; master 0-1.
 *   dev.zoom(z)         WIDE's screen: z = 1, 2 or 3 (that many CSS pixels a screen pixel, rounded to
 *                       whole display pixels, so the pixels stay sharp) or "fit" (the largest whole
 *                       size the window holds); returns the display pixels a screen pixel gets.
 *   dev.view()          "wide", "portrait" or "sideways": the view now (again on every resize).
 *   dev.slot            an empty place in the panel for the page's own key, shown in SIDEWAYS only (where
 *                       the page has no header: its menu goes there).
 *   dev.press(id, src), dev.release(id, src), dev.releaseAll()
 *
 * A button or key is down while any source holds it: a pointer (fingers on a touch screen are
 * several), the computer keyboard, or a latch (Shift+click holds it, the next click lets go).
 * A held button keeps the pointer, so hold SEL, move over PRESETS and turn the wheel: that is
 * SEL + PRESETS. A note key's pointer glides: moved onto another note key, it plays that one. A knob: a tap on its - or + end steps it (held: again and again, faster), dragging
 * anywhere on it turns it (up or right clockwise), the wheel turns it. */
(function () {
    "use strict";
    const L = PTFM.LAYOUT;
    const NOTE0 = 14;                /* key id of note 0, as fm1_led_key */
    const LED_PLAY_GREEN = 41;       /* PLAY's green LED: no key (the matrix's column 8, row bit 1) */
    const P = 240;                   /* the screen's pixels */
    const DRAG_PX = 14;              /* a knob's drag per click, in page pixels */
    const DRAG_START = 8;            /* a pointer moved this far on a knob drags it (else a tap or a hold) */
    const WHEEL_NOTCH = 40;          /* wheel delta per click: a mouse's notch is one click, touchpads add up */
    const SHOW_MS = 1000;            /* a knob's clicks show this long after its last one */
    const REPEAT_FIRST = 380, REPEAT_MS = [120, 90, 70, 50, 35];   /* a held - or +: its clicks speed up */
    const POT_DRAG = 220;            /* page pixels for MASTER's whole range */
    const LED_COLOUR = { white: "#f4fffa", red: "#ff3b2f", green: "#3dff6a" };
    /* the views' rule (specs/panel.md): upright and either touch-driven or narrow, PORTRAIT; lying down and low
     * (a phone held sideways), SIDEWAYS; else WIDE. #wide, #portrait or #sideways in the page's address force one,
     * to try */
    const SIDEWAYS_H = 520;
    function viewNow() {
        const h = location.hash.slice(1);
        if (h === "wide" || h === "portrait" || h === "sideways") return h;
        if (innerHeight > innerWidth) return matchMedia("(pointer: coarse)").matches || innerWidth <= 760 ? "portrait" : "wide";
        return innerHeight <= SIDEWAYS_H ? "sideways" : "wide";
    }

    const el = (tag, cls, parent, text) => {
        const e = document.createElement(tag);
        if (cls) e.className = cls;
        if (text !== undefined) e.textContent = text;
        if (parent) parent.appendChild(e);
        return e;
    };
    const capture = (e, ev) => { try { e.setPointerCapture(ev.pointerId); } catch (_) { /* a scripted pointer */ } };

    function createDevice(container) {
        const listeners = {};
        const state = { buttons: 0, notes: 0, master: 0.5 };
        const keys = [];             /* by key id: {e, name, src} */
        const ledEls = [];           /* by LED id: its element */
        const ledLevel = [];         /* by LED id: the level last set */
        const turns = [];            /* by encoder id: its turn(step, t) */
        let master = null;           /* MASTER's setMaster(v) */

        function on(type, fn) { (listeners[type] = listeners[type] || []).push(fn); }
        function emit(type, ev) {
            ev.type = type;
            for (const fn of (listeners[type] || []).concat(listeners["*"] || [])) fn(ev);
        }

        const root = el("div", "panel", container);
        /* a finger on the panel is the panel's: no scrolling, zooming or the gestures of a page or app
         * around this one (an artifact viewer's sheet takes vertical drags otherwise); the controls listen
         * to pointer events, which still come */
        for (const t of ["touchstart", "touchmove"]) root.addEventListener(t, ev => { if (ev.cancelable) ev.preventDefault(); }, { passive: false });
        /* no long-press menu anywhere on the page (a phone buzzes and offers to select or save); only a
         * mouse's right-click on the screen keeps its menu, to save or copy the screen */
        document.addEventListener("contextmenu", ev => {
            const mouse = ev.pointerType === "mouse" || (ev.pointerType === undefined && !matchMedia("(pointer: coarse)").matches);
            if (!(ev.target.classList && ev.target.classList.contains("shot") && mouse)) ev.preventDefault();
        });
        const area = name => el("div", "area " + name, root);
        const A = { scr: area("scr"), nav: area("nav"), mst: area("mst"), oct: area("oct"), kn: area("kn"), btn: area("btn"), kb: area("kb"),
                    pg: area("pg") };                    /* (pg: the page's own key, SIDEWAYS only) */

        /* ------------------------------------------------------------------ the screen --- */
        const fb = document.createElement("canvas");
        fb.width = fb.height = P;
        const glass = fb.getContext("2d");
        glass.fillStyle = "#000";
        glass.fillRect(0, 0, P, P);
        const view = el("canvas", "lcd", A.scr);
        const vctx = view.getContext("2d");
        const big = document.createElement("canvas");
        /* over it, an invisible image of the screen's own pixels: right-click saves or copies it */
        const shot = el("img", "shot", A.scr);
        shot.alt = "the FM-1's screen, 240 x 240";
        shot.draggable = false;
        shot.addEventListener("pointerdown", ev => { if (ev.button === 2) updateShot(); });

        /* Screen pixels as even squares at any size: blown up by a whole number with no smoothing,
         * then scaled down to the canvas's real pixels with smoothing (only pixel edges blend). */
        function scaleTo(ctx, n) {
            if (n % P === 0) {
                ctx.imageSmoothingEnabled = false;
                ctx.drawImage(fb, 0, 0, n, n);
                return;
            }
            const k = Math.ceil(n / P);
            if (big.width !== P * k) big.width = big.height = P * k;
            const bctx = big.getContext("2d");
            bctx.imageSmoothingEnabled = false;
            bctx.drawImage(fb, 0, 0, big.width, big.height);
            ctx.imageSmoothingEnabled = true;
            ctx.drawImage(big, 0, 0, n, n);
        }
        /* the right-click image, a PNG: made at most 4 times a second */
        let shotAt = -1e9;
        function updateShot() {
            shotAt = performance.now();
            shot.src = fb.toDataURL("image/png");
        }
        function present() {
            if (performance.now() - shotAt >= 250) updateShot();
            scaleTo(vctx, view.width);
        }
        new ResizeObserver(() => {
            const n = Math.max(1, Math.round(view.getBoundingClientRect().width * devicePixelRatio));
            if (view.width !== n) view.width = view.height = n;
            present();
        }).observe(view);

        /* ---------------------------------------------------------- buttons and keys --- */
        function setDown(id, down) {
            const key = keys[id];
            key.e.classList.toggle("down", down);
            if (id < NOTE0) {
                state.buttons = down ? state.buttons | 1 << id : state.buttons & ~(1 << id);
                emit("button", { id, name: key.name, down });
            } else {
                const note = id - NOTE0;
                state.notes = down ? state.notes | 1 << note : state.notes & ~(1 << note);
                emit("note", { note, name: key.name, down });
            }
        }
        function press(id, src) {
            const key = keys[id];
            if (!key || key.src.has(src)) return;
            key.src.add(src);
            if (key.src.size === 1) setDown(id, true);
        }
        function release(id, src) {
            const key = keys[id];
            if (!key || !key.src.delete(src)) return;
            if (key.src.size === 0) setDown(id, false);
        }
        function latch(id, on) {
            (on ? press : release)(id, "latch");
            keys[id].e.classList.toggle("latched", on);
        }
        function releaseAll() {
            keys.forEach((key, id) => {
                if (!key || !key.src.size) return;
                key.src.clear();
                key.e.classList.remove("latched");
                setDown(id, false);
            });
        }
        /* a pointer on a note key glides: moved onto another note key it lets the one it holds go and plays that one,
         * as a finger along a keyboard; off the note keys it keeps the last. A button stays held wherever it goes */
        const gliding = new Map();       /* pointerId -> the note key it holds now */
        function noteAt(x, y) {
            const t = document.elementFromPoint(x, y), k = t && t.closest ? t.closest("[data-key]") : null;
            const id = k ? +k.dataset.key : -1;
            return id >= NOTE0 ? id : -1;
        }
        /* a button or key: pressed while a pointer holds it (Shift+click latches it); its LED lights it */
        function addKey(id, name, e, colour) {
            e.style.setProperty("--led", LED_COLOUR[colour]);
            e.dataset.key = id;
            ledEls[id] = e;
            const key = keys[id] = { e, name, src: new Set() };
            e.addEventListener("pointerdown", ev => {
                if (ev.button !== 0) return;
                ev.preventDefault();
                if (key.src.has("latch")) return latch(id, false);
                if (ev.shiftKey) return latch(id, true);
                capture(e, ev);
                press(id, "p" + ev.pointerId);
                if (id >= NOTE0) gliding.set(ev.pointerId, id);
            });
            if (id >= NOTE0) e.addEventListener("pointermove", ev => {
                const now = gliding.get(ev.pointerId);
                if (now === undefined) return;
                const to = noteAt(ev.clientX, ev.clientY);
                if (to < 0 || to === now) return;
                release(now, "p" + ev.pointerId);
                press(to, "p" + ev.pointerId);
                gliding.set(ev.pointerId, to);
            });
            for (const t of ["pointerup", "pointercancel", "lostpointercapture"])
                e.addEventListener(t, ev => {
                    const now = gliding.get(ev.pointerId);
                    gliding.delete(ev.pointerId);
                    release(now === undefined ? id : now, "p" + ev.pointerId);
                });
        }
        /* the buttons: OCT- and OCT+ apart; the 12 in the panel's columns as rows (FX beside HOME, as
         * the panel's first column), PLAY with its two LEDs */
        for (const id of [0, 1]) {
            const b = L.buttons[id], e = el("div", "button", A.oct, b.name);
            addKey(id, b.name, e, b.led);
        }
        for (const id of [2, 8, 3, 9, 4, 10, 5, 11, 6, 12, 7, 13]) {
            const b = L.buttons[id], e = el("div", "button", A.btn, id === 12 ? "PLAY" : b.name);
            addKey(id, b.name, e, b.led);
        }
        /* the keys: 16 white, the 11 black ones over the gaps they sit at (named as printed: OP1 ... POLY);
         * WIDE in one row, PORTRAIT in two (F3-F4 below, G4-G5 above, OP6 at the lower row's end) */
        const WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26];
        const rows = [el("div", "keyrow high", A.kb), el("div", "keyrow low", A.kb)];
        const blackStrips = rows.map(r => el("div", "blacks", r)), whiteStrips = rows.map(r => el("div", "whites", r));
        for (const k of L.keys) {
            const black = WHITE.indexOf(k.note) < 0, name = PTFM.noteName(k.note);
            const w = black ? WHITE.indexOf(k.note - 1) : WHITE.indexOf(k.note);   /* the white key it follows, or is */
            const e = el("div", black ? "key black" : "key white");
            if (black) el("span", "name", e, k.label || "");
            /* where: across the whole row (WIDE), or in its half (PORTRAIT): set as CSS variables */
            e.style.setProperty("--w", w);
            e.style.setProperty("--wh", w < 8 ? w : w - 8);
            const low = w < 8;
            (black ? blackStrips : whiteStrips)[low ? 1 : 0].appendChild(e);
            e.classList.toggle("edge", black && w === 7);          /* OP6, between the rows' halves */
            addKey(NOTE0 + k.note, name, e, "white");
        }

        /* ------------------------------------------------------------------- the knobs --- */
        /* An encoder: its - and + ends step it (held: repeating, faster and faster), the wheel turns it,
         * dragging its middle up or right turns it clockwise. It shows the clicks of the turn going on
         * (from 0, gone a second after the last). */
        function addKnob(k, parent) {
            const e = el("div", "knob", parent);
            const minus = el("span", "step minus", e, "−"), mid = el("span", "mid", e), plus = el("span", "step plus", e, "+");
            el("span", "label", mid, k.name);
            const val = el("span", "val", mid);
            let delta = 0, hide = 0;
            const turn = (step, t) => {
                if (!e.classList.contains("turning")) delta = 0;
                delta += step;
                val.textContent = (delta > 0 ? "+" : "") + delta;
                e.classList.add("turning");
                clearTimeout(hide);
                hide = setTimeout(() => e.classList.remove("turning"), SHOW_MS);
                emit("encoder", { id: k.enc, name: k.name, step, t });
            };
            turns[k.enc] = turn;
            /* One pointer on the knob. A tap on − or + is a click, on letting go; held still there, it
             * clicks again and again, faster and faster. Moved DRAG_START px from where it went down,
             * anywhere on the knob, it drags instead: up or right clockwise, a click every DRAG_PX. */
            let p = null, wheel = 0;
            const unheld = () => { minus.classList.remove("held"); plus.classList.remove("held"); };
            e.addEventListener("pointerdown", ev => {
                if (ev.button !== 0 || p) return;
                ev.preventDefault();
                capture(e, ev);
                const z = ev.target.closest(".step"), step = z === minus ? -1 : z === plus ? 1 : 0;
                p = { id: ev.pointerId, x0: ev.clientX, y0: ev.clientY, x: ev.clientX, y: ev.clientY, acc: 0, step, drag: false, repeats: 0, timer: 0 };
                if (!step) return;
                z.classList.add("held");
                const q = p, again = () => {
                    q.repeats++;
                    turn(step);
                    q.timer = setTimeout(again, REPEAT_MS[Math.min(q.repeats - 1, REPEAT_MS.length - 1)]);
                };
                p.timer = setTimeout(again, REPEAT_FIRST);
            });
            e.addEventListener("pointermove", ev => {
                if (!p || ev.pointerId !== p.id) return;
                if (!p.drag) {
                    if (p.repeats || Math.hypot(ev.clientX - p.x0, ev.clientY - p.y0) < DRAG_START) return;
                    p.drag = true;                       /* (counted from where it went down) */
                    clearTimeout(p.timer);
                    unheld();
                    e.classList.add("held");
                }
                p.acc += (p.y - ev.clientY) + (ev.clientX - p.x);
                p.x = ev.clientX; p.y = ev.clientY;
                for (; p.acc >= DRAG_PX; p.acc -= DRAG_PX) turn(1);
                for (; p.acc <= -DRAG_PX; p.acc += DRAG_PX) turn(-1);
            });
            for (const t of ["pointerup", "pointercancel", "lostpointercapture"]) {
                e.addEventListener(t, ev => {
                    if (!p || ev.pointerId !== p.id) return;
                    if (ev.type === "pointerup" && p.step && !p.drag && !p.repeats) turn(p.step);
                    clearTimeout(p.timer);
                    unheld();
                    e.classList.remove("held");
                    p = null;
                });
            }
            e.addEventListener("wheel", ev => {
                ev.preventDefault();
                const d = -ev.deltaY * (ev.deltaMode === 1 ? 33 : ev.deltaMode === 2 ? 600 : 1);
                if (Math.abs(d) >= WHEEL_NOTCH) {
                    wheel = 0;
                    return turn(Math.sign(d));
                }
                for (wheel += d; wheel >= WHEEL_NOTCH; wheel -= WHEEL_NOTCH) turn(1);
                for (; wheel <= -WHEEL_NOTCH; wheel += WHEEL_NOTCH) turn(-1);
            }, { passive: false });
        }
        const knob = name => L.knobs.find(k => k.name === name);
        for (const n of ["SELECT", "ALGORITHM", "PRESETS"]) addKnob(knob(n), A.nav);
        for (const n of ["K1", "K2", "K3", "K4"]) addKnob(knob(n), A.kn);

        /* MASTER, a pot: a bar to drag (or the wheel); double-click puts it back at half */
        {
            const e = el("div", "master", A.mst), fill = el("span", "fill", e), text = el("span", "label", e);
            const draw = () => {
                fill.style.width = (state.master * 100) + "%";
                text.textContent = "MASTER " + Math.round(state.master * 100) + "%";
            };
            const setMaster = v => {
                v = Math.min(1, Math.max(0, v));
                if (v === state.master) return;
                state.master = v;
                draw();
                emit("master", { value: v });
            };
            master = setMaster;
            let drag = null;
            e.addEventListener("pointerdown", ev => {
                if (ev.button !== 0) return;
                ev.preventDefault();
                capture(e, ev);
                drag = { id: ev.pointerId, x: ev.clientX, y: ev.clientY };
            });
            e.addEventListener("pointermove", ev => {
                if (!drag || ev.pointerId !== drag.id) return;
                const d = (ev.clientX - drag.x) + (drag.y - ev.clientY);
                drag.x = ev.clientX; drag.y = ev.clientY;
                setMaster(state.master + d / POT_DRAG);
            });
            for (const t of ["pointerup", "pointercancel", "lostpointercapture"]) e.addEventListener(t, () => { drag = null; });
            e.addEventListener("dblclick", () => setMaster(0.5));
            e.addEventListener("wheel", ev => { ev.preventDefault(); setMaster(state.master - Math.sign(ev.deltaY) / 50); }, { passive: false });
            draw();
        }

        /* ------------------------------------------------------------------- the LEDs --- */
        function show(id) {                                  /* PLAY's green over its red, as on the panel */
            const b = id === LED_PLAY_GREEN ? 12 : id, e = ledEls[b];
            if (!e) return;
            const green = b === 12 ? ledLevel[LED_PLAY_GREEN] || 0 : 0;
            e.style.setProperty("--led", LED_COLOUR[green > 0 ? "green" : b < NOTE0 ? L.buttons[b].led : "white"]);
            e.style.setProperty("--level", green > 0 ? green : ledLevel[b] || 0);
        }
        function led(id, level) {
            if ((ledLevel[id] || 0) === level) return;
            ledLevel[id] = level;
            show(id);
            emit("led", { id, level });
        }
        function turn(id, step, t) {
            if (turns[id]) turns[id](step > 0 ? 1 : -1, t);
        }
        function setMaster(v) {
            if (master) master(v);
        }

        /* ------------------------------------------------------------ views and sizes --- */
        /* WIDE: the screen `z` CSS pixels a screen pixel, rounded to whole display pixels (sharp), or the
         * largest whole size the room holds beside the controls ("fit"); PORTRAIT: across the room, as high as
         * the controls under it leave; SIDEWAYS: as high as the keys' row leaves, beside the controls. The
         * room is the container's (the page's place for the panel); whole display pixels a screen pixel where
         * that comes near enough, else the room filled */
        let zoomWanted = "fit";
        const MARGIN = 12;                                   /* (the room kept around the panel) */
        const SIDE_RIGHT = 264;                              /* SIDEWAYS: the right side's width at least */
        root.style.setProperty("--side-right", SIDE_RIGHT + "px");
        function whole(room, dpr) {
            let k = Math.max(1, Math.floor(room * dpr / P));
            if (k * P / dpr < room * .8) k = room * dpr / P;     /* (too small for whole steps: fill) */
            return k;
        }
        function zoom(z) {
            if (z !== undefined) zoomWanted = z;
            const v = viewNow(), dpr = devicePixelRatio;
            const W = container.clientWidth || innerWidth, H = container.clientHeight || innerHeight;
            for (const n of ["wide", "portrait", "sideways"]) root.classList.toggle(n, n === v);
            let k;
            root.style.setProperty("--scr", "0px");
            root.style.removeProperty("--white-h");
            root.style.removeProperty("--black-h");
            if (v === "portrait") {                          /* the height the controls leave */
                const controls = root.offsetHeight;
                k = whole(Math.min(W - MARGIN, H - controls), dpr);
            } else if (v === "sideways") {                   /* the height the keys leave, the width the sides leave (the
                                                              * right side at least SIDE_RIGHT: its buttons, six a row) */
                const sides = A.nav.offsetWidth + SIDE_RIGHT + 2 * 10 + MARGIN, below = A.kb.offsetHeight + 8 + MARGIN;
                k = whole(Math.min(W - sides, H - below), dpr);
            } else {
                const sides = A.nav.offsetWidth + A.kn.offsetWidth + 2 * 18 + 2 * MARGIN, below = A.kb.offsetHeight + 18 + 2 * MARGIN;
                const fit = Math.max(1, Math.floor(Math.min(W - sides, H - below) * dpr / P));
                k = zoomWanted === "fit" ? fit : Math.max(1, Math.min(fit, Math.round(zoomWanted * dpr)));
            }
            root.style.setProperty("--scr", (k * P / dpr) + "px");
            if (v === "portrait") {                          /* room left below the keys: they grow into it (each row half) */
                const free = H - root.offsetHeight;
                if (free > 0) {
                    root.style.setProperty("--white-h", Math.min(84, 58 + free * .6 / 2) + "px");
                    root.style.setProperty("--black-h", Math.min(60, 44 + free * .4 / 2) + "px");
                }
            } else if (v === "sideways") {                   /* the height the screen's whole steps leave: the keys' */
                const free = H - MARGIN - root.offsetHeight;
                if (free > 0) {
                    root.style.setProperty("--white-h", Math.min(60, 40 + free * .6) + "px");
                    root.style.setProperty("--black-h", Math.min(44, 30 + free * .4) + "px");
                }
            }
            snap();
            return k;
        }
        function snap() {                                   /* the screen onto whole display pixels */
            root.style.transform = "";
            requestAnimationFrame(() => {
                const dpr = devicePixelRatio, r = view.getBoundingClientRect(), d = v => (Math.round(v * dpr) - v * dpr) / dpr;
                root.style.transform = `translate(${d(r.x)}px, ${d(r.y)}px)`;
            });
        }
        window.addEventListener("resize", () => zoom());
        window.addEventListener("hashchange", () => zoom());
        /* the page's room for the panel changed by itself (a sidebar shown or hidden): sized again, once a frame */
        let roomW = 0, roomH = 0;
        new ResizeObserver(() => {
            if (container.clientWidth === roomW && container.clientHeight === roomH) return;
            roomW = container.clientWidth;
            roomH = container.clientHeight;
            zoom();
        }).observe(container);
        zoom();

        return { on, led, glass, present, zoom, view: viewNow, slot: A.pg, state, press, release, releaseAll,
                 turn, setMaster, NOTE0, LED_PLAY_GREEN };
    }

    PTFM.createDevice = createDevice;
})();

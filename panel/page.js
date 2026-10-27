/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM's pages around the panel (the playground, the emulator): the frame's behaviour, the same on both
 * (specs/panel.md#the-page; page.css its look). const page = PTFM.page(dev) wires:
 *   - the sidebar as a drawer where the window is narrow: the header's MENU, or SIDEWAYS's MENU key in the
 *     panel (dev.slot), opens it; the scrim, CLOSE or Escape closes it;
 *   - the view on the body (body.wide, body.portrait, body.sideways), again on every resize;
 *   - ZOOM (#sizes' keys): WIDE's screen at 1x, 2x, 3x or FIT, kept in this browser;
 *   - MIDI (#midi-on, #midi-state): the real FM-1 as the controller (panel/midi.js), window.midi;
 *   - the status line (#status): what is held, the last thing that happened;
 *   - the sound's sign (#sound-off): page.soundOn() once it runs, page.askForSound() when a press wants it,
 *     page.soundText(t) another word there (the emulator's: its firmware stopped);
 *   - ERASE DATA (#erase-saved): a second tap within 4 s (its label asks) erases what the page keeps in this
 *     browser (its ptfm keys), then page.onErase(fn)'s fn does the page's own (the emulator: its flash). */
(function () {
    "use strict";
    const $ = id => document.getElementById(id);

    PTFM.page = function (dev) {
        const body = document.body;

        /* ---------------------------------------------------------------- the drawer --- */
        const menu = $("menu"), scrim = $("scrim"), close = $("side-close");
        const slotMenu = document.createElement("button");
        slotMenu.className = "pgmenu";
        slotMenu.textContent = "MENU";
        slotMenu.title = "the page's tools";
        if (dev.slot) dev.slot.appendChild(slotMenu);
        function drawer(on) {
            body.classList.toggle("drawer", on);
            for (const m of [menu, slotMenu]) if (m) m.setAttribute("aria-expanded", on);
        }
        if (menu) menu.addEventListener("click", () => drawer(!body.classList.contains("drawer")));
        /* (the panel takes its touches whole, no click follows them: the key in it acts on its press) */
        slotMenu.addEventListener("pointerdown", ev => { if (ev.button === 0) { ev.preventDefault(); drawer(!body.classList.contains("drawer")); } });
        if (scrim) scrim.addEventListener("pointerdown", ev => { ev.preventDefault(); drawer(false); });
        if (close) close.addEventListener("click", () => drawer(false));
        window.addEventListener("keydown", ev => { if (ev.key === "Escape") drawer(false); });

        /* --------------------------------------------------------- the view, the zoom --- */
        /* WIDE's screen at 1x, 2x, 3x or as big as the room holds (kept in this browser), whole display pixels a
         * screen pixel; PORTRAIT and SIDEWAYS size it themselves. The page knows which view shows */
        const sizes = document.querySelectorAll("#sizes button"), sizeHint = $("sizes-hint");
        let current = "fit";
        function view() {
            const v = dev.view();
            for (const n of ["wide", "portrait", "sideways"]) body.classList.toggle(n, n === v);
            const sign = $("sound-off"), scr = document.querySelector(".area.scr");   /* (SIDEWAYS: the sign over the screen) */
            if (sign && scr) requestAnimationFrame(() => {
                const r = scr.getBoundingClientRect();
                sign.style.left = v === "sideways" ? (r.x + r.width / 2) + "px" : "";
                sign.style.top = v === "sideways" ? (r.y + r.height / 2) + "px" : "";
                sign.style.maxWidth = v === "sideways" ? (r.width - 16) + "px" : "";
            });
            sizes.forEach(b => { b.disabled = v !== "wide"; });
            if (sizeHint) sizeHint.textContent = v === "wide" ? "FIT: as big as the window holds; whole pixels at every size"
                                                                : `${v.toUpperCase()}: the screen as big as the controls leave`;
        }
        function zoom(z) {
            if (![...sizes].some(b => b.dataset.zoom === z)) z = "fit";
            current = z;
            dev.zoom(z === "fit" ? "fit" : +z);
            sizes.forEach(b => b.setAttribute("aria-pressed", b.dataset.zoom === z));
            view();
            try { localStorage.setItem("ptfm.zoom", z); } catch (_) { /* storage off: fine */ }
        }
        sizes.forEach(b => b.addEventListener("click", () => zoom(b.dataset.zoom)));
        window.addEventListener("resize", () => zoom(current));     /* (a zoom changes the pixel ratio too) */
        window.addEventListener("hashchange", () => zoom(current));
        try { current = localStorage.getItem("ptfm.zoom") || "fit"; } catch (_) { /* storage off */ }
        zoom(current);

        /* ---------------------------------------------------------------------- MIDI --- */
        /* the real FM-1, as the controller (MIDI CONTROLLER) in mirror mode, works this one; its LEDs follow the
         * page's (panel/midi.js). Its key's click is a first click too (the sound starts with it) */
        const midiOn = $("midi-on"), midiState = $("midi-state");
        if (midiOn && PTFM.attachMidi) {
            window.midi = PTFM.attachMidi(dev, {
                onState(state, label, why) {
                    midiState.textContent = label;
                    midiState.dataset.state = state;
                    midiState.title = why || "";
                    midiOn.setAttribute("aria-pressed", state !== "off");
                    midiOn.textContent = state === "off" ? "CONNECT" : "DISCONNECT";
                    if (why) console.warn("PT/FM MIDI:", why);
                },
            });
            midiOn.addEventListener("click", () => window.midi.toggle());
        }

        /* ---------------------------------------------------------- the status line --- */
        const status = $("status");
        function held() {
            const names = [];
            for (const b of PTFM.LAYOUT.buttons) if (dev.state.buttons >> b.id & 1) names.push(b.name);
            for (let k = 0; k < 27; k++) if (dev.state.notes >> k & 1) names.push(PTFM.noteName(k));
            return names.join(" ") || "NOTHING";
        }
        dev.on("*", ev => {
            if (ev.type === "led" || !status) return;      /* (the page's own output, not an input) */
            const lastEv = ev.type === "encoder" ? `${ev.name} ${ev.step > 0 ? "+1" : "-1"}`
                         : ev.type === "master" ? `MASTER ${Math.round(ev.value * 100)}%`
                         : `${ev.name} ${ev.down ? "DOWN" : "UP"}`;
            status.textContent = `HELD: ${held()}  ·  LAST: ${lastEv}`;
        });

        /* ---------------------------------------------------------- the sound's sign --- */
        const soundOff = $("sound-off"), soundState = $("sound-state");
        if (soundOff && soundOff.dataset.touch && matchMedia("(pointer: coarse)").matches) soundOff.textContent = soundOff.dataset.touch;

        /* ------------------------------------------------------------- ERASE DATA --- */
        const erase = $("erase-saved"), ERASE_WORD = erase ? erase.textContent : "";
        let eraseArmed = 0, eraseTimer = 0, onErase = null;
        function disarm() { eraseArmed = 0; clearTimeout(eraseTimer); if (erase) { erase.textContent = ERASE_WORD; erase.classList.remove("armed"); } }
        if (erase) erase.addEventListener("click", () => {
            if (!eraseArmed) {                           /* (the first tap asks) */
                eraseArmed = 1;
                erase.textContent = "TAP AGAIN";
                erase.classList.add("armed");
                eraseTimer = setTimeout(disarm, 4000);
                return;
            }
            disarm();
            try { for (const k of Object.keys(localStorage)) if (/^ptfm/.test(k)) localStorage.removeItem(k); } catch (_) { /* storage off */ }
            if (onErase) onErase();
        });
        return {
            zoom, drawer,
            onErase(fn) { onErase = fn; },
            soundOn() {
                if (soundOff) soundOff.hidden = true;
                if (soundState) { soundState.textContent = "ON"; soundState.className = "state ok"; }
            },
            soundText(t, bad) {
                if (soundOff) { soundOff.hidden = false; soundOff.textContent = t; }
                if (soundState) { soundState.textContent = t; soundState.className = "state " + (bad ? "bad" : "warn"); }
            },
            askForSound() {
                if (!soundOff || soundOff.hidden) return;
                soundOff.classList.remove("ask");
                void soundOff.offsetWidth;                  /* (the blinking starts again) */
                soundOff.classList.add("ask");
            },
        };
    };
})();

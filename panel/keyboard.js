/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM panel: the computer keyboard plays the FM-1's 27 keys, as a piano on two tiers:
 *   Z X C V B N M , . /  = F3-A4, with S D F  H J  L ; '  as the black keys between them;
 *   Q W E R T Y          = B4-G5, with 3 4  6 as the black keys between them;
 * and works its 14 buttons, held as the keys are: OCT- OCT+ on 1 2, the other twelve as the panel's two rows,
 * right of the piano:
 *   7 8 9 0 - =          = FX SEL ENV LFO EDIT GLO
 *   U I O P [ ]          = HOME SAVE ARP SEQ PLAY REC
 * Keys are matched by position (event.code), so every keyboard layout gets the same places. The pages' HOW TO PLAY
 * shows them as caps, drawn here from the same tables (an element marked data-keymap). */
(function () {
    "use strict";
    const CODES = {
        KeyZ: 0, KeyS: 1, KeyX: 2, KeyD: 3, KeyC: 4, KeyF: 5, KeyV: 6, KeyB: 7, KeyH: 8, KeyN: 9,
        KeyJ: 10, KeyM: 11, Comma: 12, KeyL: 13, Period: 14, Semicolon: 15, Slash: 16, Quote: 17,
        KeyQ: 18, KeyW: 19, Digit3: 20, KeyE: 21, Digit4: 22, KeyR: 23, KeyT: 24, Digit6: 25, KeyY: 26,
    };
    const BUTTONS = {                                /* the buttons' ids (fw/hal/fm1_input.h) */
        Digit1: 0, Digit2: 1,                                                       /* OCT- OCT+ */
        Digit7: 2, Digit8: 3, Digit9: 4, Digit0: 5, Minus: 6, Equal: 7,             /* FX SEL ENV LFO EDIT GLO */
        KeyU: 8, KeyI: 9, KeyO: 10, KeyP: 11, BracketLeft: 12, BracketRight: 13,    /* HOME SAVE ARP SEQ PLAY REC */
    };
    const typing = t => t && (t.tagName === "TEXTAREA" || t.isContentEditable ||
                              (t.tagName === "INPUT" && !["checkbox", "radio", "button"].includes(t.type)));

    /* the caps for HOW TO PLAY: an element marked data-keymap="upper" or "lower" gets a tier of the keys as a piano
     * (the black keys over the gaps they sit at, as the computer's rows stand), "buttons" the buttons as the panel's
     * grid; each cap the computer's key over what it plays */
    const LABEL = { Comma: ",", Period: ".", Slash: "/", Semicolon: ";", Quote: "'", Minus: "-", Equal: "=",
                    BracketLeft: "[", BracketRight: "]" };
    const label = code => LABEL[code] || code.replace(/^Key|^Digit/, "");
    const NOTE = ["F", "F#", "G", "G#", "A", "A#", "B", "C", "C#", "D", "D#", "E"];   /* from F3, the first key */
    const noteName = n => NOTE[n % 12] + (3 + Math.floor((n + 5) / 12));
    const BUTTON = ["OCT−", "OCT+", "FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"];
    function cap(code, text, col, row, span, black) {
        const s = document.createElement("span"), b = document.createElement("b");
        if (black) s.className = "black";
        s.style.gridColumn = col + " / span " + span;
        s.style.gridRow = String(row);
        b.textContent = label(code);
        s.append(b, text);
        return s;
    }
    function keymaps() {
        const codeOf = [];
        for (const code in CODES) codeOf[CODES[code]] = code;
        for (const el of document.querySelectorAll("[data-keymap]")) {
            el.textContent = "";
            if (el.dataset.keymap === "buttons") {           /* OCT- OCT+, then the panel's two rows of six */
                for (const code in BUTTONS) {
                    const id = BUTTONS[code];
                    el.appendChild(cap(code, BUTTON[id], id < 2 ? id + 1 : (id - 2) % 6 + 1, id < 2 ? 1 : id < 8 ? 2 : 3, 1));
                }
                continue;
            }
            /* a white key spans two half-columns, a black one the two between its neighbours */
            const [from, to] = el.dataset.keymap === "lower" ? [0, 17] : [18, 26];
            let w = 0;                                      /* white keys so far */
            for (let n = from; n <= to; n++) {
                const black = NOTE[n % 12].length > 1;
                el.appendChild(cap(codeOf[n], noteName(n), black ? 2 * w : 2 * w + 1, black ? 1 : 2, 2, black));
                if (!black) w++;
            }
        }
    }

    PTFM.attachKeyboard = function (dev) {
        keymaps();
        const held = new Set();
        const idOf = code => CODES[code] !== undefined ? dev.NOTE0 + CODES[code] : BUTTONS[code];
        window.addEventListener("keydown", ev => {
            const id = idOf(ev.code);
            if (id === undefined || ev.ctrlKey || ev.metaKey || ev.altKey || typing(ev.target)) return;
            ev.preventDefault();
            if (held.has(ev.code)) return;
            held.add(ev.code);
            dev.press(id, "kb");
        });
        window.addEventListener("keyup", ev => {
            if (held.delete(ev.code)) dev.release(idOf(ev.code), "kb");
        });
        window.addEventListener("blur", () => {          /* keys let go while the page was away */
            for (const code of held) dev.release(idOf(code), "kb");
            held.clear();
        });
    };
})();

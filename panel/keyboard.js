/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM panel: the computer keyboard plays the FM-1's 27 keys, as a piano on two tiers:
 *   Z X C V B N M , . /  = F3-A4, with S D F  H J  L ; '  as the black keys between them;
 *   Q W E R T Y          = B4-G5, with 3 4  6 as the black keys between them.
 * Keys are matched by position (event.code), so every keyboard layout gets the same places. */
(function () {
    "use strict";
    const CODES = {
        KeyZ: 0, KeyS: 1, KeyX: 2, KeyD: 3, KeyC: 4, KeyF: 5, KeyV: 6, KeyB: 7, KeyH: 8, KeyN: 9,
        KeyJ: 10, KeyM: 11, Comma: 12, KeyL: 13, Period: 14, Semicolon: 15, Slash: 16, Quote: 17,
        KeyQ: 18, KeyW: 19, Digit3: 20, KeyE: 21, Digit4: 22, KeyR: 23, KeyT: 24, Digit6: 25, KeyY: 26,
    };
    const typing = t => t && (t.tagName === "TEXTAREA" || t.isContentEditable ||
                              (t.tagName === "INPUT" && !["checkbox", "radio", "button"].includes(t.type)));

    PTFM.attachKeyboard = function (dev) {
        const held = new Set();
        window.addEventListener("keydown", ev => {
            const note = CODES[ev.code];
            if (note === undefined || ev.ctrlKey || ev.metaKey || ev.altKey || typing(ev.target)) return;
            ev.preventDefault();
            if (held.has(ev.code)) return;
            held.add(ev.code);
            dev.press(dev.NOTE0 + note, "kb");
        });
        window.addEventListener("keyup", ev => {
            if (held.delete(ev.code)) dev.release(dev.NOTE0 + CODES[ev.code], "kb");
        });
        window.addEventListener("blur", () => {          /* keys let go while the page was away */
            for (const code of held) dev.release(dev.NOTE0 + CODES[code], "kb");
            held.clear();
        });
    };
})();

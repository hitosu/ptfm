/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM panel: where everything is on the FM-1's panel.
 *
 * Measured from M-VAVE's official image of the black FM-1 (1000 x 1000 px; the panel 899 x 536 px
 * at 51, 232; the screen's active area 153.5 px square at 313.3, 301.5), scaled so the screen is
 * 240 units for its 240 pixels: 1 unit = 1 screen pixel, 1.5635 units a photo pixel; the panel is
 * 1405 x 837. Rectangles are {x, y, w, h, r} (top left, corner radius); knobs, printed labels and
 * marks are centres. Font sizes are in the same units.
 * Ids are the firmware's (fw/hal/fm1_input.h): buttons 0-13, encoders 0-6, note keys
 * 0-26 = F3-G5. */
window.PTFM = window.PTFM || {};

PTFM.noteName = function (note) {
    const NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
    const midi = 53 + note;                              /* note 0 = F3 */
    return NAMES[midi % 12] + (Math.floor(midi / 12) - 1);
};

PTFM.LAYOUT = (function () {
    "use strict";
    const COL = [809.1, 896.7, 984.2, 1071.8, 1159.3, 1246.9];   /* the 12 buttons' centres */
    const ROW = [285.3, 373.7];
    const BTN = 70.4;
    const grid = (id, name, c, r, label) => ({
        id, name, label: label || [name], x: COL[c] - BTN / 2, y: ROW[r] - BTN / 2, w: BTN, h: BTN, r: 12.5,
        font: label ? 17.5 : 19, led: "white",
    });

    const KEY_X0 = 111.0, KEY_DX = 78.88, KEY_W = 64.5;   /* white key k's centre: KEY_X0 + k * KEY_DX */
    const WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26];    /* their notes */
    const BLACK_AFTER = [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14];   /* the white key each black key follows */
    const BLACK_LABEL = ["OP1", "OP2", "OP3", "OP4", "OP5", "OP6", "PIT", "GLO", "MONO", "POLY", ""];

    const keys = WHITE.map((note, k) => ({
        note, x: KEY_X0 + k * KEY_DX - KEY_W / 2, y: 636.3, w: KEY_W, h: 126.6, r: KEY_W / 2,
        slot: [26.5, 73.5], led: "white",                /* slot: top offset, length */
    })).concat(BLACK_AFTER.map((k, j) => ({
        note: WHITE[k] + 1, x: KEY_X0 + (k + 0.5) * KEY_DX - KEY_W / 2, y: 498.4, w: KEY_W, h: 127.8, r: KEY_W / 2,
        slot: [31.3, j === 10 ? 71.1 : 37.5], label: BLACK_LABEL[j], labelY: 87.9, font: 16.5, led: "white",
    })));

    /* the dots between the key rows, where no black key is */
    const keyDots = [3, 6, 10, 13].map(k => ({ x: KEY_X0 + (k + 0.5) * KEY_DX, y: 632.6 }));

    return {
        width: 1405, height: 837, radius: 75,
        screen: { x: 410, y: 108.7, size: 240, pixels: 240, bezel: { x: 361.6, y: 61, w: 334, h: 333, r: 26.6 } },
        knobR: 32,                                       /* a knob's outer radius */
        prints: [                                        /* dot-matrix, 5 x 7 dots */
            { text: "MASTER", x: 119.3, y: 66.4 }, { text: "SELECT", x: 267.4, y: 66.4 },
            { text: "PRESETS", x: 119.3, y: 213.4 }, { text: "ALGORITHM", x: 267.4, y: 213.4 },
            { text: "KNOB1", x: 791.1, y: 66.4 }, { text: "KNOB2", x: 949, y: 66.4 },
            { text: "KNOB3", x: 1106.9, y: 66.4 }, { text: "KNOB4", x: 1264.1, y: 66.4 },
        ],
        printDots: { px: 2.16, py: 2.23, gap: .4 },     /* dot pitch across and down, the gap between dots */
        knobs: [                                         /* MASTER is a pot (analog), the rest encoders */
            { name: "MASTER", pot: true, x: 119.3, y: 130.6 },
            { name: "SELECT", enc: 0, x: 267.4, y: 130.6 },
            { name: "PRESETS", enc: 6, x: 119.3, y: 276 },
            { name: "ALGORITHM", enc: 1, x: 267.4, y: 276 },
            { name: "K1", enc: 2, x: 791.1, y: 130.6 }, { name: "K2", enc: 3, x: 949, y: 130.6 },
            { name: "K3", enc: 4, x: 1106.9, y: 130.6 }, { name: "K4", enc: 5, x: 1264.1, y: 130.6 },
        ],
        plates: [                                        /* the silicone under the buttons and keys */
            { x: 84.9, y: 357.6, w: 218.4, h: 70.4, r: 8 },
            { x: 748.9, y: 231.4, w: 559.7, h: 197, r: 14 },
            { x: 53.2, y: 478.9, w: 1297.7, h: 303.3, r: 29.7 },
        ],
        marks: [{ x: 1028.8, y: 330.1 }].concat(keyDots), /* the small dots on the silicone */
        buttons: [
            { id: 0, name: "OCT-", label: ["OCT-"], x: 140.4 - 40.9, y: 392.7 - 24.25, w: 81.8, h: 48.5, r: 9.4, font: 17, led: "white" },
            { id: 1, name: "OCT+", label: ["OCT+"], x: 247.3 - 40.9, y: 392.7 - 24.25, w: 81.8, h: 48.5, r: 9.4, font: 17, led: "white" },
            grid(2, "FX", 0, 0), grid(3, "SEL", 1, 0), grid(4, "ENV", 2, 0),
            grid(5, "LFO", 3, 0), grid(6, "EDIT", 4, 0), grid(7, "GLO", 5, 0),
            grid(8, "HOME", 0, 1), grid(9, "SAVE", 1, 1), grid(10, "ARP", 2, 1),
            grid(11, "SEQ", 3, 1), Object.assign(grid(12, "PLAY", 4, 1, ["PLAY", "STOP"]), { led: "red" }),
            Object.assign(grid(13, "REC", 5, 1), { led: "red" }),
        ],
        keys,
    };
})();

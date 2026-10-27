/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM panel: the dot-matrix lettering printed on the FM-1's body (MASTER, KNOB1, ...).
 * Each character is 5 x 7 dots, read off a photo of the device; only the characters the panel
 * prints are here. PTFM.dotText() draws a line of them as one SVG path of small squares. */
window.PTFM = window.PTFM || {};

PTFM.DOT_FONT = {
    A: [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    B: ["####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."],
    C: [".####", "#....", "#....", "#....", "#....", "#....", ".####"],
    E: ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
    G: [".####", "#....", "#....", "#..##", "#...#", "#...#", ".####"],
    H: ["#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    I: [".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."],
    K: ["#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"],
    L: ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
    M: ["#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"],
    N: ["#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"],
    O: [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    P: ["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."],
    R: ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
    S: [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
    T: ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
    1: ["..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."],
    2: [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"],
    3: ["#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."],
    4: ["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."],
};

/* path data for text centred on (cx, cy): dots px apart across and py down, with `gap` between
 * neighbours (the photo's strokes are near-solid, with faint breaks); a character takes 5 dots
 * and an empty column */
PTFM.dotText = function (text, cx, cy, px, py, gap) {
    const chars = [...text], w = px - gap, h = py - gap;
    const x0 = cx - (chars.length * 6 - 1) * px / 2 + gap / 2;
    const y0 = cy - 7 * py / 2 + gap / 2;
    let d = "";
    chars.forEach((ch, i) => {
        (PTFM.DOT_FONT[ch] || []).forEach((row, y) => {
            for (let x = 0; x < 5; x++) {
                if (row[x] === "#") d += `M${(x0 + (i * 6 + x) * px).toFixed(2)} ${(y0 + y * py).toFixed(2)}h${w}v${h}h${-w}z`;
            }
        });
    });
    return d;
};

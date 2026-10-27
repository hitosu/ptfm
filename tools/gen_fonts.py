#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The firmware's bitmap fonts as JS for the playground's screen:

  S  Terminus 8x16 (assets/fonts/ter-u16n.bdf)              labels, status
  L  S at 2x, 16x32                                         titles, large values (lcd.js scales S)
  T  Terminus 6x12 (assets/fonts/Lat15-Terminus12x6.psf.gz)  the pattern views
  B  Terminus 12x24 bold (assets/fonts/ter-u24b.bdf)        the focus row's notes, the note editor

ASCII 32..126 each.
Terminus Font: SIL OFL 1.1, Dimitar Toshkov Zhekov (assets/fonts/Terminus-LICENSE.txt).

  python3 tools/gen_fonts.py            -> playground/fonts.js
  python3 tools/gen_fonts.py --c OUT.h  -> S, T and L as C for fw/device/gfx.c (the device's own screens: rescue)
  python3 tools/gen_fonts.py --fw DIR   -> DIR/fonts.h, fonts.c: T, S, B, L, X for PT/FM's firmware

A glyph is h rows of w pixels; in fonts.js each row is one hex byte pair per 8 pixels, MSB = left.
In C (gfx.c's gfx_font_t) a row is 2 pixels a byte, high nibble first, alpha 0 or 15.
"""
import gzip
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FONTS = ROOT / "assets" / "fonts"
OUT = ROOT / "playground" / "fonts.js"
FIRST, LAST = 32, 126


def read_bdf(path):
    """{codepoint: rows (ints, MSB = left pixel)}, width, height; every glyph in its full cell"""
    glyphs, w, h, asc = {}, 0, 0, 0
    lines = iter(path.read_text(encoding="latin-1").splitlines())
    for line in lines:
        k = line.split()
        if not k:
            continue
        if k[0] == "FONTBOUNDINGBOX":
            w, h = int(k[1]), int(k[2])
        elif k[0] == "FONT_ASCENT":
            asc = int(k[1])
        elif k[0] == "ENCODING":
            cp = int(k[1])
        elif k[0] == "BBX":
            bw, bh, bx, by = map(int, k[1:5])
        elif k[0] == "BITMAP":
            rows = [0] * h
            top = asc - (bh + by)                    # cell row of the bitmap's first row
            for r in range(bh):
                bits = int(next(lines), 16)
                nbits = ((bw + 7) // 8) * 8
                rows[top + r] = (bits >> (nbits - bw)) << (w - bw - bx)
            glyphs[cp] = rows
    return glyphs, w, h


def read_psf2(path):
    """{codepoint: rows (ints, MSB = left pixel)}, width, height"""
    d = gzip.decompress(path.read_bytes()) if path.suffix == ".gz" else path.read_bytes()
    magic, _, hsize, flags, n, csize, h, w = struct.unpack_from("<8I", d, 0)
    if magic != 0x864AB572:
        raise SystemExit(f"gen_fonts: {path.name}: not a PSF2 font")
    bpr = (w + 7) // 8
    glyphs = [d[hsize + i * csize:hsize + (i + 1) * csize] for i in range(n)]
    cmap = {}
    if flags & 1:                                    # unicode table: per glyph, UTF-8 codepoints, 0xFF ends
        pos, gi = hsize + n * csize, 0
        while gi < n and pos < len(d):
            end = d.index(b"\xff", pos)
            for ch in d[pos:end].split(b"\xfe")[0].decode("utf-8", "replace"):
                cmap.setdefault(ord(ch), gi)
            pos, gi = end + 1, gi + 1
    else:
        cmap = {i: i for i in range(n)}
    return {cp: [int.from_bytes(glyphs[gi][r * bpr:(r + 1) * bpr], "big") >> (bpr * 8 - w) for r in range(h)]
            for cp, gi in cmap.items()}, w, h


def js_font(name, glyphs, w, h):
    missing = [chr(c) for c in range(FIRST, LAST + 1) if c not in glyphs]
    if missing:
        raise SystemExit(f"gen_fonts: {name}: no glyph for {''.join(missing)!r}")
    hexw = (w + 7) // 8 * 2
    shift = (w + 7) // 8 * 8 - w                     # rows stored left-aligned in whole bytes
    data = ["".join(f"{row << shift:0{hexw}x}" for row in glyphs[c]) for c in range(FIRST, LAST + 1)]
    body = ",\n        ".join(f'"{g}"' for g in data)
    return f"    {name}: {{ w: {w}, h: {h}, first: {FIRST}, glyphs: [\n        {body},\n    ] }},"


def c_font(name, glyphs, w, h):
    """gfx.c's gfx_font_t: fixed width, no pad; each row (w + 1) // 2 bytes, 2 pixels a byte"""
    missing = [chr(c) for c in range(FIRST, LAST + 1) if c not in glyphs]
    if missing:
        raise SystemExit(f"gen_fonts: {name}: no glyph for {''.join(missing)!r}")
    data, offs = [], []
    for c in range(FIRST, LAST + 1):
        offs.append(len(data))
        for row in glyphs[c]:
            px = [15 if row >> (w - 1 - x) & 1 else 0 for x in range(w)] + [0]
            data += [(px[x] << 4) | px[x + 1] for x in range(0, w, 2)]
    n = LAST - FIRST + 1
    rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 24]) for i in range(0, len(data), 24)]
    return "\n".join([
        f"static const uint8_t FONT_{name}_DATA[{len(data)}] = {{",
        *(f"    {r}," for r in rows),
        "};",
        f"static const uint16_t FONT_{name}_OFF[{n}] = {{{', '.join(map(str, offs))}}};",
        f"static const uint8_t FONT_{name}_W[{n}] = {{{', '.join([str(w)] * n)}}};",
        f"static const gfx_font_t FONT_{name} = {{{h}, 0, {FIRST}, {LAST}, 0, FONT_{name}_W, FONT_{name}_W, "
        f"FONT_{name}_OFF, FONT_{name}_DATA}};",
        ""])


def main_c(out):
    s = read_bdf(FONTS / "ter-u16n.bdf")
    t = read_psf2(FONTS / "Lat15-Terminus12x6.psf.gz")
    Path(out).write_text("\n".join([
        "/* SPDX-License-Identifier: OFL-1.1 */",
        "/* generated by tools/gen_fonts.py --c: do not edit. Terminus Font, SIL OFL 1.1, Dimitar Toshkov",
        " * Zhekov (assets/fonts/Terminus-LICENSE.txt). gfx.c's gfx_font_t, ASCII 32..126. */",
        "#pragma once",
        "",
        c_font("S", *s),
        c_font("T", *t),
        f"static const uint8_t FONT_L_W[{LAST - FIRST + 1}] = {{{', '.join([str(2 * s[1])] * (LAST - FIRST + 1))}}};",
        "/* L: S's own bitmaps drawn at 2x (sh = 1: gfx.c's cv_text enlarges them) */",
        f"static const gfx_font_t FONT_L = {{{2 * s[2]}, 0, {FIRST}, {LAST}, 1, FONT_L_W, FONT_L_W, "
        "FONT_S_OFF, FONT_S_DATA};",
        "",
    ]), encoding="utf-8")
    print(f"{out}: S {s[1]}x{s[2]}, T {t[1]}x{t[2]}, L = S at 2x, glyphs {FIRST}..{LAST}")


def fw_font(name, glyphs, w, h):
    """PT/FM's firmware (fw/core/ui): a glyph is h rows of uint16, bit w-1 = the left pixel"""
    missing = [chr(c) for c in range(FIRST, LAST + 1) if c not in glyphs]
    if missing:
        raise SystemExit(f"gen_fonts: {name}: no glyph for {''.join(missing)!r}")
    rows = []
    for c in range(FIRST, LAST + 1):
        g = ", ".join(f"0x{r:04x}" for r in glyphs[c])
        rows.append(f"    /* {chr(c)!r:>5} */ {g},")
    return "\n".join([f"static const uint16_t {name}_ROWS[{(LAST - FIRST + 1) * h}] = {{", *rows, "};", ""])


def main_fw(out):
    """fonts.h and fonts.c for PT/FM's firmware: T, S, B, and L (S at 2x), X (S at 4x)"""
    s = read_bdf(FONTS / "ter-u16n.bdf")
    t = read_psf2(FONTS / "Lat15-Terminus12x6.psf.gz")
    b = read_bdf(FONTS / "ter-u24b.bdf")
    lic = ["/* SPDX-License-Identifier: OFL-1.1 */",
           "/* generated by tools/gen_fonts.py --fw: do not edit. Terminus Font, SIL OFL 1.1, Dimitar Toshkov",
           " * Zhekov (assets/fonts/Terminus-LICENSE.txt). ASCII 32..126; a glyph is h rows of uint16, bit w-1",
           " * the left pixel. */"]
    h = lic + [
        "#ifndef UI_FONTS_H",
        "#define UI_FONTS_H",
        "#include <stdint.h>",
        "",
        "typedef struct {",
        "    uint8_t w, h;                       /* the cell drawn: the glyph's size times k */",
        "    uint8_t gw, gh, k;                  /* the glyph's own size, and its scale */",
        "    uint8_t first, count;               /* the characters held */",
        "    const uint16_t *rows;               /* gh rows a glyph */",
        "} ui_font_t;",
        "",
        "extern const ui_font_t FONT_T, FONT_S, FONT_B, FONT_L, FONT_X;",
        "",
        "#endif",
        ""]
    n = LAST - FIRST + 1
    c = lic + ['#include "fonts.h"', "",
               fw_font("T", *t), fw_font("S", *s), fw_font("B", *b),
               f"const ui_font_t FONT_T = {{{t[1]}, {t[2]}, {t[1]}, {t[2]}, 1, {FIRST}, {n}, T_ROWS}};",
               f"const ui_font_t FONT_S = {{{s[1]}, {s[2]}, {s[1]}, {s[2]}, 1, {FIRST}, {n}, S_ROWS}};",
               f"const ui_font_t FONT_B = {{{b[1]}, {b[2]}, {b[1]}, {b[2]}, 1, {FIRST}, {n}, B_ROWS}};",
               f"const ui_font_t FONT_L = {{{2 * s[1]}, {2 * s[2]}, {s[1]}, {s[2]}, 2, {FIRST}, {n}, S_ROWS}};   /* S doubled */",
               f"const ui_font_t FONT_X = {{{4 * s[1]}, {4 * s[2]}, {s[1]}, {s[2]}, 4, {FIRST}, {n}, S_ROWS}};   /* S at 4x: the count-in */",
               ""]
    d = Path(out)
    d.mkdir(parents=True, exist_ok=True)
    (d / "fonts.h").write_text("\n".join(h), encoding="utf-8")
    (d / "fonts.c").write_text("\n".join(c), encoding="utf-8")
    print(f"{d}/fonts.c: T {t[1]}x{t[2]}, S {s[1]}x{s[2]}, B {b[1]}x{b[2]}; L and X from S")


def main():
    if sys.argv[1:2] == ["--c"] and len(sys.argv) == 3:
        return main_c(sys.argv[2])
    if sys.argv[1:2] == ["--fw"] and len(sys.argv) == 3:
        return main_fw(sys.argv[2])
    s = read_bdf(FONTS / "ter-u16n.bdf")
    t = read_psf2(FONTS / "Lat15-Terminus12x6.psf.gz")
    b = read_bdf(FONTS / "ter-u24b.bdf")
    out = [
        "/* SPDX-License-Identifier: OFL-1.1 */",
        "/* generated by tools/gen_fonts.py: do not edit. Terminus Font, SIL OFL 1.1, Dimitar Toshkov",
        " * Zhekov (assets/fonts/Terminus-LICENSE.txt). A glyph: h rows of hex, MSB = left. */",
        "window.PTFM = window.PTFM || {};",
        "",
        "PTFM.FONTS = {",
        js_font("S", *s),
        js_font("T", *t),
        js_font("B", *b),
        "};",
        "PTFM.FONTS.L = Object.assign({}, PTFM.FONTS.S, { scale: 2 });",
        "",
    ]
    OUT.write_text("\n".join(out), encoding="utf-8")
    print(f"{OUT.relative_to(ROOT)}:", s[1], "x", s[2], ",", t[1], "x", t[2], "and", b[1], "x", b[2],
          f"glyphs {FIRST}..{LAST}")


if __name__ == "__main__":
    main()

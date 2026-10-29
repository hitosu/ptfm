#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The landing's pixel font: the FM-1's own Terminus 8x16 (SIL OFL 1.1, assets/fonts/Terminus-LICENSE.txt) as an
outline webfont, each pixel a square, cut to ASCII and a few signs; renamed (PTFM Pixel), its copyright kept. Two cuts:
the screen's (every glyph 8 pixels wide) and a proportional one for labels (letters as wide as their ink, a pixel of
space each side; digits stay 8 wide, so figures line up). WOFF2, about 2 KB each.

  python3 -I site/tools/pixfont.py      assets/fonts/ter-u16n.bdf -> site/fonts/ptfm-pixel.woff2, ptfm-pixel-prop.woff2
"""
from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

ROOT = Path(__file__).resolve().parents[2]
BDF = ROOT / "assets" / "fonts" / "ter-u16n.bdf"
OUT = ROOT / "site" / "fonts"
KEEP = set(range(32, 127)) | {0xB7, 0xD7, 0x2013, 0x2014, 0x2190, 0x2192, 0x25B6, 0x2665}
U = 64                                    # font units a pixel


def read_bdf(path):
    glyphs, cur, fbb = {}, None, None
    for line in path.read_text(encoding="latin-1").splitlines():
        p = line.split()
        if not p:
            continue
        if p[0] == "FONTBOUNDINGBOX":
            fbb = list(map(int, p[1:]))
        elif p[0] == "STARTCHAR":
            cur = {"rows": []}
        elif p[0] == "ENCODING":
            cur["code"] = int(p[1])
        elif p[0] == "DWIDTH":
            cur["dw"] = int(p[1])
        elif p[0] == "BBX":
            cur["bbx"] = list(map(int, p[1:]))
        elif p[0] == "BITMAP":
            cur["bm"] = True
        elif p[0] == "ENDCHAR":
            if cur["code"] in KEEP:
                glyphs[cur["code"]] = cur
            cur = None
        elif cur is not None and cur.get("bm"):
            cur["rows"].append(int(p[0], 16))
    return glyphs, fbb


def build(glyphs, fbb, family, prop, out):
    cw, ch, _, cy0 = fbb
    names = [".notdef"] + ["u%04X" % c for c in sorted(glyphs)]
    fb = FontBuilder(ch * U, isTTF=True)
    fb.setupGlyphOrder(names)
    fb.setupCharacterMap({c: "u%04X" % c for c in glyphs})
    outlines, metrics = {".notdef": TTGlyphPen(None).glyph()}, {".notdef": (cw * U, 0)}
    for c, g in glyphs.items():
        w, h, xo, yo = g["bbx"]
        nb = (w + 7) // 8
        on = lambda bits, x: bits >> (nb * 8 - 1 - x) & 1      # noqa: E731
        adv = g.get("dw", cw)
        if prop and not 48 <= c <= 57:
            cols = [x for x in range(w) if any(on(bits, x) for bits in g["rows"])]
            if cols:
                xo, adv = 1 - cols[0], cols[-1] - cols[0] + 3
            else:
                adv = max(3, adv // 2)                          # the space
        pen = TTGlyphPen(None)
        lsb = None                                              # where the ink starts (none: a space)
        for r, bits in enumerate(g["rows"]):
            y = (yo + h - 1 - r) * U
            x = 0
            while x < w:                                        # a row's runs of pixels, a rectangle each
                if on(bits, x):
                    x1 = x
                    while x1 < w and on(bits, x1):
                        x1 += 1
                    x0u, x1u = (xo + x) * U, (xo + x1) * U
                    lsb = x0u if lsb is None else min(lsb, x0u)
                    pen.moveTo((x0u, y)); pen.lineTo((x0u, y + U)); pen.lineTo((x1u, y + U)); pen.lineTo((x1u, y))
                    pen.closePath()
                    x = x1
                else:
                    x += 1
        outlines["u%04X" % c] = pen.glyph()
        # the left side bearing must be the ink's start: browsers place the outline by it, and a 0 drew every glyph
        # as far left as its blank columns ("I" 2 pixels: "MI DI")
        metrics["u%04X" % c] = (adv * U, lsb or 0)
    fb.setupGlyf(outlines)
    fb.setupHorizontalMetrics(metrics)
    asc, desc = (ch + cy0) * U, -cy0 * U
    fb.setupHorizontalHeader(ascent=asc, descent=-desc)
    fb.setupNameTable({"familyName": family, "styleName": "Regular",
                       "copyright": "Terminus Font, Copyright (C) 2020 Dimitar Toshkov Zhekov. "
                                    "SIL Open Font License 1.1."})
    fb.setupOS2(sTypoAscender=asc, sTypoDescender=-desc, sTypoLineGap=0, usWinAscent=asc, usWinDescent=desc)
    fb.setupPost()
    fb.font.flavor = "woff2"
    fb.save(str(out))
    print(out, out.stat().st_size, "B")


if __name__ == "__main__":
    glyphs, fbb = read_bdf(BDF)
    OUT.mkdir(parents=True, exist_ok=True)
    build(glyphs, fbb, "PTFM Pixel", False, OUT / "ptfm-pixel.woff2")
    build(glyphs, fbb, "PTFM Pixel Prop", True, OUT / "ptfm-pixel-prop.woff2")

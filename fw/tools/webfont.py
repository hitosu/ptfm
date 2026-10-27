#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The pages' font: Iosevka Aile, Iosevka's quasi-proportional cut (SIL OFL 1.1, no Reserved Font Name;
assets/fonts/Iosevka-LICENSE.txt), everywhere on a page, cut to what a page writes (ASCII and a few signs), WOFF2,
inlined into the page, its names (copyright, licence) kept. The FM-1's own screen keeps its Terminus bitmaps; the
pages do not use them (big screens get the font that reads well on big screens, the FM-1 the one that reads well on
its small one).

  fw/tools/webfont.py --iosevka REGULAR.ttc BOLD.ttc
        the upright font (the first) of the release's collections (PkgTTC-IosevkaAile: IosevkaAile-Regular.ttc,
        IosevkaAile-Bold.ttc), cut into assets/fonts (once a release; fontTools, brotli)
  fonts_css() -> str     @font-face rules with the font inlined (make_installer: the debug page)

  PTFM Iosevka Aile   400 and 700, assets/fonts/IosevkaAile-{Regular,Bold}.woff2
"""
import base64
import sys
from pathlib import Path

FONTS = Path(__file__).resolve().parents[2] / "assets" / "fonts"
WANT = list(range(0x20, 0x7F)) + [0xB0, 0xB1, 0xB5, 0xB7, 0xD7, 0x2013, 0x2014, 0x2022, 0x2026, 0x2190, 0x2191,
                                  0x2192, 0x2193, 0x2588, 0x2591, 0x2592, 0x25A0, 0x25B2, 0x25B6, 0x25BC, 0x25C0,
                                  0x2713]
FACES = [("PTFM Iosevka Aile", "Iosevka Aile", {400: FONTS / "IosevkaAile-Regular.woff2", 700: FONTS / "IosevkaAile-Bold.woff2"})]


def fonts_css():
    return "\n".join(f'@font-face {{ font-family: "{fam}"; font-weight: {w}; src: url(data:font/woff2;base64,'
                     f'{base64.b64encode(p.read_bytes()).decode()}) format("woff2"); font-display: block; }}'
                     for fam, _, faces in FACES for w, p in faces.items() if p.exists())


def cut_iosevka(sources):
    """each collection's first font (its upright one), checked to be the family and weight wanted, cut to WANT"""
    from fontTools import subset
    from fontTools.ttLib import TTCollection
    src = iter(sources)
    for _, family, faces in FACES:
        for w, out in faces.items():
            path = next(src)
            font = TTCollection(path).fonts[0]
            name = font["name"].getDebugName(16) or font["name"].getDebugName(1)
            if font["OS/2"].usWeightClass != w or name != family:
                raise SystemExit(f"webfont: {path}'s first font is {name} {font['OS/2'].usWeightClass}, not {family} {w}")
            opts = subset.Options()
            opts.flavor = "woff2"
            opts.hinting = False
            opts.name_IDs = ["*"]
            opts.name_languages = ["*"]
            opts.notdef_outline = True
            s = subset.Subsetter(opts)
            s.populate(unicodes=WANT)
            s.subset(font)
            font.flavor = "woff2"
            font.save(out)
            print(f"{out}  {out.stat().st_size} B")


if __name__ == "__main__":
    if len(sys.argv) != 4 or sys.argv[1] != "--iosevka":
        raise SystemExit(__doc__)
    cut_iosevka(sys.argv[2:])

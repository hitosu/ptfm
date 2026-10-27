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

The PDF manual's font (specs/manual.md): Archivo (SIL OFL 1.1, no Reserved Font Name;
assets/fonts/Archivo-LICENSE.txt), its variable release (weight 100-900, width 62-125 %) cut to ARCHIVO_CHARS, both
axes kept, WOFF2:

  fw/tools/webfont.py --archivo 'Archivo[wdth,wght].ttf'
        the release's variable font (its sha256 checked), cut into assets/fonts/Archivo.woff2 (once; fontTools, brotli)
  archivo_css() -> str   its @font-face rule, inlined (tools/build_manual.py: the print page)
"""
import base64
import hashlib
import sys
from pathlib import Path

FONTS = Path(__file__).resolve().parents[2] / "assets" / "fonts"
WANT = list(range(0x20, 0x7F)) + [0xB0, 0xB1, 0xB5, 0xB7, 0xD7, 0x2013, 0x2014, 0x2022, 0x2026, 0x2190, 0x2191,
                                  0x2192, 0x2193, 0x2588, 0x2591, 0x2592, 0x25A0, 0x25B2, 0x25B6, 0x25BC, 0x25C0,
                                  0x2713]
FACES = [("PTFM Iosevka Aile", "Iosevka Aile", {400: FONTS / "IosevkaAile-Regular.woff2", 700: FONTS / "IosevkaAile-Bold.woff2"})]
# what the manual prints (tests/manual/check_manual.py fails on any other character); the release has no geometric
# shapes, so the PDF's boxes and markers are drawn by its stylesheet
ARCHIVO_CHARS = "".join(map(chr, range(0x20, 0x7F))) + " °±·×½¼–—‘’“”•…←↑→↓−§©é"
ARCHIVO = FONTS / "Archivo.woff2"
ARCHIVO_SHA256 = "0e094a7d3c7c4c25cf1310c4b30014f1dae9332220b1c2c88f4fa996f0b05053"


def fonts_css():
    return "\n".join(f'@font-face {{ font-family: "{fam}"; font-weight: {w}; src: url(data:font/woff2;base64,'
                     f'{base64.b64encode(p.read_bytes()).decode()}) format("woff2"); font-display: block; }}'
                     for fam, _, faces in FACES for w, p in faces.items() if p.exists())


def archivo_css():
    return (f'@font-face {{ font-family: "PTFM Archivo"; font-weight: 100 900; font-stretch: 62% 125%; '
            f'src: url(data:font/woff2;base64,{base64.b64encode(ARCHIVO.read_bytes()).decode()}) format("woff2"); '
            f'font-display: block; }}')


def cut_archivo(path):
    """the variable release, checked by its hash and for every character wanted, cut to ARCHIVO_CHARS"""
    from fontTools import subset
    from fontTools.ttLib import TTFont
    data = Path(path).read_bytes()
    if hashlib.sha256(data).hexdigest() != ARCHIVO_SHA256:
        raise SystemExit(f"webfont: {path} is not the Archivo release this cut was made for (sha256)")
    font = TTFont(path)
    cmap = font.getBestCmap()
    lack = [c for c in ARCHIVO_CHARS if ord(c) not in cmap]
    if lack:
        raise SystemExit(f"webfont: Archivo has no {''.join(lack)!r}")
    opts = subset.Options()
    opts.flavor = "woff2"
    opts.hinting = False
    opts.name_IDs = ["*"]
    opts.name_languages = ["*"]
    opts.notdef_outline = True
    opts.layout_features = ["kern", "liga", "tnum", "lnum"]
    s = subset.Subsetter(opts)
    s.populate(unicodes=[ord(c) for c in ARCHIVO_CHARS])
    s.subset(font)
    font.flavor = "woff2"
    font.save(ARCHIVO)
    print(f"{ARCHIVO}  {ARCHIVO.stat().st_size} B")


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
    if len(sys.argv) == 3 and sys.argv[1] == "--archivo":
        cut_archivo(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == "--iosevka":
        cut_iosevka(sys.argv[2:])
    else:
        raise SystemExit(__doc__)

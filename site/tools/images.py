#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The landing's images (specs/site.md), from what site/tools/ makes:

  python3 -I site/tools/images.py screens DIR     DIR/*.ppm (site/tools/shots.c's) -> site/screens/*.png
  python3 -I site/tools/images.py hero HERO.png   hero.html's picture -> site/img/hero.webp (2400 px), hero-phone.webp
                                                  (the FM-1 cropped close, 1200 px) and og.jpg (1200 x 630, for link
                                                  previews)
  python3 -I site/tools/images.py video THUMB.png NAME
                                                  a video's YouTube thumbnail (1280 x 720) -> site/img/NAME.webp
                                                  (640 px, the video card's picture at twice its size)
  python3 -I site/tools/images.py card            site/screens/playback-acid.png -> site/img/card.png, the picture of
                                                  the firmware's card in The Firmware Hub's catalogue (fmhub.json):
                                                  the screen with PT/FM in its header alone, not a real screen
"""
import sys
from pathlib import Path

from PIL import Image

SITE = Path(__file__).resolve().parents[1]
CROP_PHONE = (.20, .08, .92, .92)         # fractions of the picture: the FM-1 close, for narrow screens
CROP_OG = (.20, .04, 1.0, .787)           # 1200 x 630's shape out of a 16 : 9 picture
# the card: the header's text is Terminus 8x16 (assets/fonts/ter-u16n.bdf, as the FM-1 draws it) in 8-pixel cells from
# x 2, rows 0-15, on black
CARD_SCREEN = "screens/playback-acid.png"       # PLAYBACK (8 cells), a space, the page 02/05: cells at x 2-113
CARD_LOGO = ((255, 125, 115), (131, 234, 238), (246, 226, 115), (131, 234, 131), (238, 133, 238))  # the start
                                                # screen's PT/FM, a colour a letter


def crop(im, box):
    w, h = im.size
    return im.crop((round(box[0] * w), round(box[1] * h), round(box[2] * w), round(box[3] * h)))


def scaled(im, width):
    return im.resize((width, round(im.size[1] * width / im.size[0])), Image.LANCZOS)


def screens(src):
    out = SITE / "screens"
    out.mkdir(exist_ok=True)
    for p in sorted(Path(src).glob("*.ppm")):
        dest = out / (p.stem + ".png")
        Image.open(p).convert("RGB").save(dest, optimize=True)
        print(dest, dest.stat().st_size, "B")


def hero(src):
    out = SITE / "img"
    out.mkdir(exist_ok=True)
    im = Image.open(src).convert("RGB")
    for name, pic in (("hero.webp", scaled(im, 2400)), ("hero-phone.webp", scaled(crop(im, CROP_PHONE), 1200))):
        pic.save(out / name, "WEBP", quality=82, method=6)
    scaled(crop(im, CROP_OG), 1200).resize((1200, 630), Image.LANCZOS).save(out / "og.jpg", quality=86, optimize=True)
    for name in ("hero.webp", "hero-phone.webp", "og.jpg"):
        print(out / name, (out / name).stat().st_size, "B")


def read_bdf(path):
    """code -> the glyph's 16 rows of 8 bits (every Terminus 8x16 glyph fills its cell)"""
    glyphs, code, rows = {}, None, None
    for line in path.read_text(encoding="latin-1").splitlines():
        p = line.split()
        if not p:
            continue
        if p[0] == "ENCODING":
            code = int(p[1])
        elif p[0] == "BITMAP":
            rows = []
        elif p[0] == "ENDCHAR":
            glyphs[code], rows = rows, None
        elif rows is not None:
            rows.append(int(p[0], 16))
    return glyphs


def card():
    im = Image.open(SITE / CARD_SCREEN).convert("RGB")
    font = read_bdf(SITE.parent / "assets" / "fonts" / "ter-u16n.bdf")
    im.paste((0, 0, 0), (2, 0, 114, 16))                # PLAYBACK and its page gone
    for i, (ch, colour) in enumerate(zip("PT/FM", CARD_LOGO)):  # PT/FM where PLAYBACK stood, nothing after it
        rows = font[ord(ch)]
        assert len(rows) == 16, ch
        for y, bits in enumerate(rows):
            for x in range(8):
                if bits >> (7 - x) & 1:
                    im.putpixel((2 + 8 * i + x, y), colour)
    dest = SITE / "img" / "card.png"
    im.save(dest, optimize=True)
    print(dest, dest.stat().st_size, "B")


def video(src, name):
    dest = SITE / "img" / (name + ".webp")
    scaled(Image.open(src).convert("RGB"), 640).save(dest, "WEBP", quality=82, method=6)
    print(dest, dest.stat().st_size, "B")


if __name__ == "__main__":
    if sys.argv[1:2] == ["video"] and len(sys.argv) == 4:
        video(sys.argv[2], sys.argv[3])
    elif sys.argv[1:] == ["card"]:
        card()
    elif len(sys.argv) == 3 and sys.argv[1] in ("screens", "hero"):
        (screens if sys.argv[1] == "screens" else hero)(sys.argv[2])
    else:
        raise SystemExit(__doc__)

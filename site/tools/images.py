#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The landing's images (specs/site.md), from what site/tools/ makes:

  python3 -I site/tools/images.py screens DIR     DIR/*.ppm (site/tools/shots.c's) -> site/screens/*.png
  python3 -I site/tools/images.py hero HERO.png   hero.html's picture -> site/img/hero.webp (2400 px), hero-phone.webp
                                                  (the FM-1 cropped close, 1200 px) and og.jpg (1200 x 630, for link
                                                  previews)
"""
import sys
from pathlib import Path

from PIL import Image

SITE = Path(__file__).resolve().parents[1]
CROP_PHONE = (.20, .08, .92, .92)         # fractions of the picture: the FM-1 close, for narrow screens
CROP_OG = (.20, .04, 1.0, .787)           # 1200 x 630's shape out of a 16 : 9 picture


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


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] not in ("screens", "hero"):
        raise SystemExit(__doc__)
    (screens if sys.argv[1] == "screens" else hero)(sys.argv[2])

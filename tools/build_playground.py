#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The playground as one self-contained page, to pass on as a single file: playground/index.html with
every stylesheet and script it loads (its own and the panel's) written into it, in the same order.

  python3 -I tools/build_playground.py           -> dist/playground/playground.html
  python3 -I tools/build_playground.py --check   whether that file is what the sources make

The playground itself needs no build (edit its files, reload the page); this is only its copy for
sharing. It opens from file:// as the playground does: classic scripts, the sound's worklet put together
from the dsp-*.js functions at start (audio.js). The same sources give the same bytes: no time stamp.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PAGE = ROOT / "playground" / "index.html"
OUT = ROOT / "dist" / "playground" / "playground.html"

LINK = re.compile(r'<link rel="stylesheet" href="([^"]+)">')
SCRIPT = re.compile(r'<script src="([^"]+)"></script>')
NOTE = "<!-- built from playground/ and panel/ by tools/build_playground.py; edit those -->\n"


def read(href):
    path = (PAGE.parent / href).resolve()
    if ROOT not in path.parents:
        sys.exit(f"build_playground: {href} is outside the repository")
    return path.read_text(encoding="utf-8")


def inline_script(m):
    js = read(m.group(1))
    # (inside <script>, "</script" would end it and "<!--" can change how the rest is read; in JS both
    # stay the same text escaped: in a string, a regular expression or a comment)
    js = re.sub(r"</(script)", r"<\\/\1", js, flags=re.I).replace("<!--", "<\\!--")
    return "<script>\n" + js + "\n</script>"


def build():
    html = PAGE.read_text(encoding="utf-8")
    html = LINK.sub(lambda m: "<style>\n" + read(m.group(1)) + "\n</style>", html)
    html = SCRIPT.sub(inline_script, html)
    left = re.findall(r'<(?:script|link)\b[^>]*\b(?:src|href)="[^"]*"', html)
    if left:
        sys.exit(f"build_playground: not written in: {left}")
    if not html.startswith("<!doctype html>\n"):
        sys.exit("build_playground: playground/index.html does not start with <!doctype html>")
    return html.replace("<!doctype html>\n", "<!doctype html>\n" + NOTE, 1)


def main():
    html = build()
    if "--check" in sys.argv[1:]:
        same = OUT.exists() and OUT.read_text(encoding="utf-8") == html
        print(f"{OUT.relative_to(ROOT)}: " + ("current" if same else "stale (python3 -I tools/build_playground.py)"))
        return 0 if same else 1
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(html, encoding="utf-8")
    print(f"{OUT.relative_to(ROOT)}: {len(html.encode('utf-8')) / 1024:.0f} KB, one file")
    return 0


if __name__ == "__main__":
    sys.exit(main())

# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The manual's panel drawing and key map (tools/manual_panel.py, specs/manual.md section 2). From ptfm/:
    python3 -I tests/manual/panel_test.py"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import manual_panel as mp  # noqa: E402

fails = checks = 0


def check(ok, what):
    global fails, checks
    checks += 1
    if not ok:
        fails += 1
        print("FAIL", what)


web = mp.panel_svg("web")
ids = re.findall(r'data-p="([^"]+)"', web)
check(sum(i.startswith("w-") for i in ids) == 16, "16 white keys")
check(sum(i.startswith("bk-") for i in ids) == 11, "11 black keys")
check(sum(i.startswith("b-") for i in ids) == 14, "14 buttons")
check(sum(i.startswith("k-") for i in ids) == 8, "8 knobs")
check(len(ids) == len(set(ids)), "no element twice")
check(set(ids) == set(mp.ELEMENTS), "ELEMENTS is what the drawing holds")
check(mp.kbd_targets("b", "GLO") == ["bk-GLO"], "the black key GLO")
check(mp.kbd_targets("", "GLO") == ["b-GLO"], "the button GLO")
check(mp.kbd_targets("", "PLAY") == ["b-PLAY"], "PLAY, the PLAY/STOP button")
check(mp.kbd_targets("", "K1") == ["k-K1"], "the knob K1")
check(mp.kbd_targets("", "OCT-") == ["b-OCT-"], "OCT-")
check(mp.kbd_targets("b", "BLANK") == ["bk-BLANK"], "the blank key")
check(mp.kbd_targets("w", "3") == ["w-3"], "white key 3")
check(mp.kbd_targets("w", "1-8") == ["w-%d" % k for k in range(1, 9)], "white keys 1-8")
check(len(mp.kbd_targets("w", "white keys")) == 16, "every white key")
check(len(mp.kbd_targets("b", "black key")) == 11, "every black key")
check(len(mp.kbd_targets("", "KEYS")) == 27, "all the keys")
for cls, text in (("", "LOOPS"), ("w", "17"), ("b", "OP7"), ("", "W3")):
    try:
        mp.kbd_targets(cls, text)
        check(False, "kbd %r %r refused" % (cls, text))
    except KeyError:
        check(True, "")
pr = mp.panel_svg("print", "keys", ["b-SEQ", "b-GLO"])
check('viewBox="40 470 1330 320"' in pr, "the keys' crop")
check(len(re.findall(r'class="p-callout"', pr)) == 2, "two callouts")
check(re.search(r'class="p-callout"[^>]*>\s*<circle[^>]*/>\s*<text[^>]*>1</text>', pr) is not None, "callout 1")
check("#C8102E" in pr.upper(), "print: the callouts in the spot red")

print("panel_test: %d of %d checks FAILED" % (fails, checks) if fails else "panel_test: %d checks passed" % checks)
sys.exit(1 if fails else 0)

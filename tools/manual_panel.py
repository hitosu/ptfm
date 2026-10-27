# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The manual's FM-1 (specs/manual.md section 2): the panel drawn as SVG from panel/layout.js's numbers, and the map
from the manual's key names to the drawing's elements.

  layout() -> dict                  panel/layout.js's PTFM.LAYOUT, evaluated by node (no number copied here)
  ELEMENTS                          the drawing's element ids: k-MASTER k-SELECT k-PRESETS k-ALGORITHM k-K1..k-K4,
                                    b-<button> (b-OCT- ... b-REC), bk-<print> (bk-OP1 ... bk-POLY, bk-BLANK), w-1..w-16
  panel_svg(style, crop, callouts)  style "web" (the dark panel, each element data-p, lit by the class "lit") or
                                    "print" (line art); crop "full" or "keys"; callouts: numbered circles beside the
                                    elements given, numbered in order
  kbd_targets(cls, text) -> ids     a <kbd>'s class ("" a button or knob, "b" a black key, "w" a white key) and text
                                    -> the elements it lights; KeyError for a name the panel has not
"""
import functools
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPOT = "#C8102E"
CROP = {"full": "-10 -10 1425 857", "keys": "40 470 1330 320"}


@functools.lru_cache(maxsize=None)
def layout():
    js = ("const vm = require('vm'), fs = require('fs'); const ctx = {}; ctx.window = ctx;"
          "vm.runInNewContext(fs.readFileSync(process.argv[1], 'utf8'), ctx);"
          "process.stdout.write(JSON.stringify(ctx.PTFM.LAYOUT));")
    out = subprocess.run(["node", "-e", js, str(ROOT / "panel" / "layout.js")], capture_output=True, text=True,
                         check=True)
    return json.loads(out.stdout)


def _parts():
    """[(id, kind, shape)] in drawing order: knobs, buttons, black keys, white keys"""
    L = layout()
    out = [("k-" + k["name"], "knob", k) for k in L["knobs"]]
    out += [("b-" + b["name"], "btn", b) for b in L["buttons"]]
    keys = L["keys"]
    out += [("bk-" + (k["label"] or "BLANK"), "black", k) for k in keys if "label" in k]
    out += [("w-%d" % (j + 1), "white", k) for j, k in enumerate(k for k in keys if "label" not in k)]
    return out


ELEMENTS = None


def _elements():
    global ELEMENTS
    if ELEMENTS is None:
        ELEMENTS = [p[0] for p in _parts()]
    return ELEMENTS


def kbd_targets(cls, text):
    els = _elements()
    t = text.strip()
    low = t.lower()
    if cls == "w":
        if low in ("white key", "white keys"):
            return [e for e in els if e.startswith("w-")]
        if "-" in t:
            a, _, b = t.partition("-")
            if a.isdigit() and b.isdigit() and 1 <= int(a) < int(b) <= 16:
                return ["w-%d" % k for k in range(int(a), int(b) + 1)]
        if t.isdigit() and "w-" + t in els:
            return ["w-" + t]
    elif cls == "b":
        if low in ("black key", "black keys"):
            return [e for e in els if e.startswith("bk-")]
        if "bk-" + t in els:
            return ["bk-" + t]
    elif cls == "":
        if t == "KEYS":
            return [e for e in els if e.startswith(("w-", "bk-"))]
        for p in ("b-", "k-"):
            if p + t in els:
                return [p + t]
    raise KeyError("the panel has no %s %r" % ({"w": "white key", "b": "black key"}.get(cls, "control"), text))


def _f(v):
    return ("%.1f" % v).rstrip("0").rstrip(".")


def _text(x, y, s, size, cls="p-print"):
    return '<text x="%s" y="%s" class="%s" font-size="%s">%s</text>' % (_f(x), _f(y), cls, _f(size), s)


def panel_svg(style, crop="full", callouts=None, screen=None, say=False):
    """screen: an image (a data: URI, 240 x 240) shown on the FM-1's screen; say: the screen can show the name of the
    element pointed at (the page's FM-1 beside the text)"""
    if style not in ("web", "print") or crop not in CROP:
        raise ValueError("panel_svg(%r, %r)" % (style, crop))
    L = layout()
    s = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="%s" class="panel panel-%s" role="img" '
         'aria-label="The FM-1\'s front panel">' % (CROP[crop], style)]
    s.append('<rect x="0" y="0" width="%s" height="%s" rx="%s" class="p-body"/>' % (L["width"], L["height"],
                                                                                    L["radius"]))
    for p in L["plates"]:
        s.append('<rect x="%s" y="%s" width="%s" height="%s" rx="%s" class="p-plate"/>' % (
            _f(p["x"]), _f(p["y"]), _f(p["w"]), _f(p["h"]), _f(p["r"])))
    sc = L["screen"]
    b = sc["bezel"]
    s.append('<rect x="%s" y="%s" width="%s" height="%s" rx="%s" class="p-bezel"/>' % (
        _f(b["x"]), _f(b["y"]), _f(b["w"]), _f(b["h"]), _f(b["r"])))
    s.append('<rect x="%s" y="%s" width="%s" height="%s" class="p-screen"/>' % (
        _f(sc["x"]), _f(sc["y"]), sc["size"], sc["size"]))
    if screen:
        s.append('<image class="p-shot" x="%s" y="%s" width="%s" height="%s" href="%s" style="image-rendering: '
                 'pixelated"/>' % (_f(sc["x"]), _f(sc["y"]), sc["size"], sc["size"], screen))
    if say and style == "web" and crop == "full":   # (the name of what is pointed at, on the screen: manual.js)
        cx, cy = sc["x"] + sc["size"] / 2, sc["y"] + sc["size"] / 2
        s.append('<g class="p-say"><rect x="%s" y="%s" width="%s" height="%s"/><text class="p-say-n" x="%s" y="%s">'
                 '</text><text class="p-say-k" x="%s" y="%s" font-size="22"></text></g>' % (
                     _f(sc["x"]), _f(sc["y"]), sc["size"], sc["size"], _f(cx), _f(cy + 8), _f(cx), _f(cy + 58)))
    for pr in L["prints"]:
        s.append(_text(pr["x"], pr["y"] + 6, pr["text"], 17))
    where = {}
    for pid, kind, k in _parts():
        lit = ' class="p-el p-%s" data-p="%s"' % (kind, pid)
        if style == "web":
            lit += ' data-name="%s" data-kind="%s"' % (
                {"knob": k.get("name"), "btn": k.get("name"), "black": k.get("label") or "BLANK"}.get(kind, pid[2:]),
                {"knob": "KNOB", "btn": "BUTTON", "black": "BLACK KEY", "white": "WHITE KEY"}[kind])
        if kind == "knob":
            r = L["knobR"]
            s.append('<g%s><circle cx="%s" cy="%s" r="%s"/><line x1="%s" y1="%s" x2="%s" y2="%s" class="p-tick"/></g>'
                     % (lit, _f(k["x"]), _f(k["y"]), r, _f(k["x"]), _f(k["y"] - r + 2), _f(k["x"]), _f(k["y"] - 14)))
            where[pid] = (k["x"] + r * 0.7, k["y"] - r * 0.7)
            continue
        x, y, w, h, r = k["x"], k["y"], k["w"], k["h"], k["r"]
        body = '<rect x="%s" y="%s" width="%s" height="%s" rx="%s"/>' % (_f(x), _f(y), _f(w), _f(h), _f(r))
        if kind == "btn":
            lines = k["label"]
            y0 = y + h / 2 - (len(lines) - 1) * 9 + 6
            body += "".join(_text(x + w / 2, y0 + i * 18, t, k["font"] * 0.85, "p-label") for i, t in enumerate(lines))
        elif kind == "black" and k["label"]:
            body += _text(x + w / 2, y + k["labelY"], k["label"], k["font"], "p-label")
        elif kind == "white":
            body += _text(x + w / 2, y + h - 16, pid[2:], 17, "p-label")
        s.append("<g%s>%s</g>" % (lit, body))
        where[pid] = (x + w - 4, y + 4)
    for n, pid in enumerate(callouts or [], 1):
        if pid not in where:
            raise KeyError("no panel element %r for a callout" % pid)
        cx, cy = where[pid]
        s.append('<g class="p-callout"><circle cx="%s" cy="%s" r="15" fill="%s"/>%s</g>' % (
            _f(cx), _f(cy), SPOT, _text(cx, cy + 6.5, str(n), 19, "p-num")))
    s.append("</svg>")
    return "\n".join(s)


ELEMENTS = _elements()

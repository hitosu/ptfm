# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The manual's build (specs/manual.md): the screenshots made anew by fw/tests/manual_shots.c, then the chapters
(manual/src/*.html, in the order of their names) rendered twice: the page, dist/manual/manual.html, and the print page,
build/manual/print.html, which --pdf prints to dist/manual/ptfm-manual.pdf (twice: the contents' and the index's page
numbers from the first print; the dates and the tagged structure's node ids pinned, so a build is the same byte for
byte; SOURCE_DATE_EPOCH sets the dates). Each screenshot is also a file for the web site, site/screens/manual/ID.png,
the screen's own 240 x 240 pixels.

    python3 -I tools/build_manual.py [--pdf] [--no-shots] [--src DIR] [--work DIR]      (from ptfm/)

  --no-shots   the screenshots made last (build/manual/shots) taken as they are
  --src DIR    the chapters from DIR instead of manual/src
  --work DIR   both pages and screens/ into DIR instead of dist/manual/, build/manual/ and site/screens/manual/
               (the tests)

Needs cc (the screenshots, as fw/tests/run_ui3.sh builds them) and node (the panel's numbers, panel/layout.js);
--pdf a headless Chrome (CHROME names another) and pdfinfo (poppler)."""
import argparse
import base64
import bisect
import functools
import html
import os
import re
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "fw" / "tools"))
import manual_panel  # noqa: E402
import webfont  # noqa: E402

MAN = ROOT / "manual"
WORK = ROOT / "build" / "manual"
DIST = ROOT / "dist" / "manual"
SITE_SHOTS = ROOT / "site" / "screens" / "manual"       # the screenshots as files, for the web site (specs/site.md)
DISK = ROOT / "dist" / "firmware" / "demo.ptfmdsk"
TITLE = "PT/FM OWNER'S MANUAL"
LOGO = "".join('<b class="c-%s">%s</b>' % (c, l) for c, l in zip("ptsfm", "PT/FM"))   # (the header's mark: the
                                                                                    # start screen's colours)
LICENCE = "<!-- SPDX-License-Identifier: GPL-3.0-only\n     Copyright (C) 2026 Vasily Kazantsev -->\n"
GROUPS = [("00", "START"), ("06", "THE SCREENS"), ("16", "CONNECTIONS"), ("A", "REFERENCE")]
PANEL_SCREEN = "status-ptfm"                            # what the FM-1 drawn on the page shows on its screen
PDF_NAME = "ptfm-manual.pdf"                            # the PDF, beside the page (the header's DOWNLOAD PDF MANUAL)


class BuildError(Exception):
    pass


def version():
    return (ROOT / "fw" / "VERSION").read_text().strip()


# ------------------------------------------------------------------------------------------------- screenshots ---
def _newest(paths):
    return max((p.stat().st_mtime for p in paths if p.is_file()), default=0)


def shots_program():
    """build/manual/manual_shots, compiled again when anything it is made of is newer"""
    exe = WORK / "manual_shots"
    deps = list((ROOT / "fw" / "core").rglob("*.[ch]")) + list((ROOT / "fw" / "tools").glob("*.py"))
    deps += list((ROOT / "specs" / "ui").rglob("*.md")) + [ROOT / "fw" / "tests" / "ui3_test.c",
                                                           ROOT / "fw" / "tests" / "manual_shots.c",
                                                           ROOT / "fw" / "VERSION", Path(__file__)]
    if exe.exists() and exe.stat().st_mtime >= _newest(deps):
        return exe
    gen = WORK / "gen"
    WORK.mkdir(parents=True, exist_ok=True)
    with open(WORK / "gen.log", "w") as log:
        subprocess.run([sys.executable, "-I", str(ROOT / "fw" / "tools" / "gen.py"), str(gen)], cwd=ROOT, stdout=log,
                       check=True)
    core = ROOT / "fw" / "core"
    inc = ["-I" + str(d) for d in sorted([core] + [d for d in core.rglob("*") if d.is_dir()])]
    src = sorted(str(p) for p in core.rglob("*.c"))
    cmd = [os.environ.get("CC", "cc"), "-std=gnu11", "-D_DEFAULT_SOURCE", '-DPTFM_VERSION="%s"' % version(),
           "-O1", "-g", "-Wall", "-Wextra",
           "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-type-limits", "-Wno-missing-field-initializers",
           "-fno-omit-frame-pointer", "-fsanitize=address,undefined", "-fno-sanitize=shift-base",
           "-fno-sanitize-recover=all"] + inc + ["-I" + str(ROOT / "fw" / "tests"), "-I" + str(gen)] + src + [
           str(gen / "builtin.c"), str(gen / "fonts.c"), str(gen / "texts.c"),
           str(ROOT / "fw" / "tests" / "manual_shots.c"), "-o", str(exe), "-lm"]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        raise BuildError("fw/tests/manual_shots.c: did not compile\n" + r.stderr[-3000:])
    return exe


def shot_ids(exe):
    return subprocess.run([str(exe), "--list"], capture_output=True, text=True, check=True).stdout.split()


def make_shots(exe):
    d = WORK / "shots"
    d.mkdir(parents=True, exist_ok=True)
    for p in d.glob("*.ppm"):
        p.unlink()
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
    r = subprocess.run([str(exe), str(DISK), str(d)], cwd=ROOT, capture_output=True, text=True, env=env)
    if r.returncode:
        raise BuildError("the screenshots failed:\n" + "\n".join(r.stdout.splitlines()[-30:]) + r.stderr[-2000:])


def read_ppm(path):
    data = path.read_bytes()
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", data)
    if not m:
        raise BuildError("%s: not a PPM the screenshots program writes" % path)
    w, h = int(m.group(1)), int(m.group(2))
    return w, h, data[m.end():m.end() + w * h * 3]


def png(w, h, rgb, scale=2):
    """an RGB image as PNG, each pixel scale x scale (the screen's pixels kept sharp)"""
    rows = []
    for y in range(h):
        line = rgb[y * w * 3:(y + 1) * w * 3]
        wide = b"".join(line[x * 3:x * 3 + 3] * scale for x in range(w))
        rows += [b"\0" + wide] * scale

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))


LCD = [(40, (205, 210, 196)), (110, (142, 148, 136)), (190, (78, 84, 74)), (256, (24, 28, 22))]


def lcd(rgb):
    """the screen printed as a reflective LCD: four greys on a grey-green ground, light pixels dark"""
    out = bytearray(len(rgb))
    for i in range(0, len(rgb), 3):
        g = (rgb[i] * 299 + rgb[i + 1] * 587 + rgb[i + 2] * 114) // 1000
        out[i:i + 3] = next(c for t, c in LCD if g < t)
    return bytes(out)


def shot_png(sid, target):
    w, h, rgb = read_ppm(WORK / "shots" / (sid + ".ppm"))
    return "data:image/png;base64," + base64.b64encode(png(w, h, rgb if target == "web" else lcd(rgb))).decode()


def write_screens(ids, out):
    """every screenshot as out/ID.png at the screen's own size, in colour; a PNG no longer made is removed"""
    out.mkdir(parents=True, exist_ok=True)
    for p in out.glob("*.png"):
        if p.stem not in ids:
            p.unlink()
    for sid in ids:
        (out / (sid + ".png")).write_bytes(png(*read_ppm(WORK / "shots" / (sid + ".ppm")), scale=1))


# ---------------------------------------------------------------------------------------------------- chapters ---
def chapters(src):
    out = []
    for f in sorted(Path(src).glob("*.html")):
        text = f.read_text(encoding="utf-8")
        text = re.sub(r"\A\s*<!--.*?-->\s*", "", text, flags=re.S)
        m = re.match(r'<section class="chapter" id="([a-z0-9-]+)" data-kind="(front|basics|screen|appendix)">\s*'
                     r"<h2>(.*?)</h2>", text, flags=re.S)
        if not m:
            raise BuildError("%s: does not start with its chapter's <section> and <h2>" % f.name)
        pre = f.name.split("-")[0]
        num = "" if pre in ("00", "Z") else (str(int(pre)) if pre.isdigit() else pre)
        out.append(dict(file=f.name, pre=pre, id=m.group(1), kind=m.group(2), num=num,
                        title=re.sub(r"<[^>]+>", "", m.group(3)).strip(), html=text))
    # appendix A: each chapter's key tables (its topic ID-keys) copied in where a fragment says <div data-keys-from>
    tables = {}
    for ch in out:
        t = re.search(r'<section class="topic" id="%s-keys">(.*?)</section>' % re.escape(ch["id"]), ch["html"], re.S)
        if t:
            tables[ch["id"]] = "\n".join(re.findall(r'<table class="keys">.*?</table>', t.group(1), flags=re.S))

    def copy(fname, m):
        if m.group(1) not in tables:
            raise BuildError('%s: <div data-keys-from="%s">: that chapter has no key tables (a topic %s-keys)'
                             % (fname, m.group(1), m.group(1)))
        return tables[m.group(1)]
    for ch in out:
        ch["html"] = re.sub(r'<div data-keys-from="([a-z0-9-]+)"></div>', lambda m: copy(ch["file"], m), ch["html"])
    return out


def _attr(s):
    return html.escape(s, quote=True)


def _plain(s):
    return html.unescape(re.sub(r"<[^>]+>", "", s)).strip()


KBD = re.compile(r'<kbd(?: class="([wb])")?>(.*?)</kbd>', re.S)


def _targets(fname, cls, text):
    try:
        return manual_panel.kbd_targets(cls or "", _plain(text))
    except KeyError as e:
        raise BuildError("%s: <kbd%s>%s</kbd>: %s" % (fname, ' class="%s"' % cls if cls else "", text, e.args[0]))


def _lit(ids):
    return ' data-k="%s" tabindex="0"' % " ".join(dict.fromkeys(ids))


def keys(fname, s):
    """data-k (the panel's elements to light) on every key outside a combination, every combination and every key
    table row"""
    held = []

    def combo(m):
        ids = [i for k in KBD.finditer(m.group(1)) for i in _targets(fname, k.group(1), k.group(2))]
        held.append('<span class="combo"%s>%s</span>' % (_lit(ids), m.group(1)))
        return "\0%d\0" % (len(held) - 1)
    s = re.sub(r'<span class="combo">(.*?)</span>', combo, s, flags=re.S)

    def row(m):
        cell = re.match(r"\s*<t[dh][^>]*>(.*?)</t[dh]>", m.group(2), flags=re.S)
        inner = re.sub("\0(\\d+)\0", lambda c: held[int(c.group(1))], cell.group(1)) if cell else ""
        ids = [i for k in KBD.finditer(inner) for i in _targets(fname, k.group(1), k.group(2))]
        if not ids:
            raise BuildError("%s: a key table row names no key: %s" % (fname, _plain(m.group(2))[:80]))
        return "<tr%s%s>%s</tr>" % (m.group(1), _lit(ids), m.group(2))

    def table(m):
        body = re.sub(r"<thead>.*?</thead>", lambda h: h.group(0).replace("<tr", "<tr data-head"), m.group(0), flags=re.S)
        body = re.sub(r"<tr(?! data-head)([^>]*)>(.*?)</tr>", row, body, flags=re.S)
        return body.replace("<tr data-head", "<tr")
    s = re.sub(r'<table class="keys">.*?</table>', table, s, flags=re.S)

    def one(m):
        ids = _targets(fname, m.group(1), m.group(2))
        cls = ' class="%s"' % m.group(1) if m.group(1) else ""
        return "<kbd%s%s>%s</kbd>" % (cls, _lit(ids), m.group(2))
    s = KBD.sub(one, s)
    return re.sub("\0(\\d+)\0", lambda c: held[int(c.group(1))], s)


def figures(fname, s, target, ids):
    def shot(m):
        sid = m.group(1)
        if sid not in ids:
            raise BuildError('%s: <figure data-shot="%s">: the screenshots program has no such figure' % (fname, sid))
        cap = re.search(r"<figcaption>(.*?)</figcaption>", m.group(2), flags=re.S)
        alt = _plain(cap.group(1)) if cap else sid
        img = '<div class="lcd"><img data-shot="%s" src="%s" alt="%s" width="480" height="480"></div>' % (
            sid, shot_png(sid, target), _attr(alt))
        return '<figure class="shot" data-shot="%s">%s%s</figure>' % (sid, img, m.group(2))
    s = re.sub(r'<figure data-shot="([^"]+)">(.*?)</figure>', shot, s, flags=re.S)

    def panel(m):
        crop = m.group(2) or "full"
        screen = shot_png(PANEL_SCREEN, target) if crop == "full" and PANEL_SCREEN in ids else None   # (STATUS on
        try:                                                                    # its screen: the page's colours or LCD)
            svg = manual_panel.panel_svg("print" if target == "print" else "web", crop, m.group(1).split(), screen)
        except (KeyError, ValueError) as e:
            raise BuildError("%s: <figure data-panel>: %s" % (fname, e))
        return '<figure class="panel-fig">%s%s</figure>' % (svg, m.group(3))
    return re.sub(r'<figure data-panel="([^"]*)"(?: data-crop="(keys|full)")?>(.*?)</figure>', panel, s, flags=re.S)


SCREENS = {"SONGS": "songs", "EDITOR": "editor", "INSTRUMENTS": "instruments", "PLAYBACK": "playback", "DJ": "dj",
           "VISUAL": "visual", "STATUS": "status", "SETTINGS": "settings", "LOOPS": "loops", "FX": "fx"}
# a screen's name as a word of its own: not inside a longer word, nor a term of several capitals (PLAYBACK SCROLL,
# DJ FILTER, UNUSED INSTRUMENTS); "the EDITOR page" is chapter 17's
SCREEN_WORD = re.compile(r"(?<![A-Za-z0-9/+_-])(?<![A-Z0-9] )(?<! a )(%s)(?![A-Za-z0-9+_-])(?! [A-Z0-9][A-Z0-9])"
                         r"( page\b)?" % "|".join(sorted(SCREENS, key=len, reverse=True)))   # ("like a DJ": a person)
NO_LINK = {"a", "kbd", "samp", "code", "h2", "h3", "h4", "dt", "th", "figcaption", "title"}


def screen_links(ch, s):
    """every screen's name in the text a link to its chapter (the user, 2026-10-27): not in that chapter, nor in
    headings, names (<dt>), table heads, captions, keys, quoted messages and links, nor where the name means something
    else (<span class="nolink">: FX the effects' knobs)"""
    out, skip = [], []
    for part in re.split(r"(<[^>]+>)", s):
        tag = re.match(r"<(/?)([a-zA-Z0-9]+)", part)
        if tag:
            name = tag.group(2).lower()
            if (name in NO_LINK or 'class="nolink"' in part or (tag.group(1) and skip and skip[-1] == name)) and \
                    not part.endswith("/>"):
                if tag.group(1):
                    if skip and skip[-1] == name:
                        skip.pop()
                else:
                    skip.append(name)
            out.append(part)
            continue
        if skip:
            out.append(part)
            continue

        def link(m):
            target = "editor-page" if m.group(1) == "EDITOR" and m.group(2) else SCREENS[m.group(1)]
            if target == ch["id"]:
                return m.group(0)
            return '<a href="#%s">%s</a>%s' % (target, m.group(1), m.group(2) or "")
        out.append(SCREEN_WORD.sub(link, part))
    return "".join(out)


def render(ch, target, ids):
    s = ch["html"].replace("{{VERSION}}", version())
    s = screen_links(ch, s)
    s = s.replace("<h2>", '<h2 data-num="%s">' % ch["num"], 1)
    s = keys(ch["file"], s)
    s = figures(ch["file"], s, target, ids)
    left = re.findall(r"\{\{[^}]*\}\}", s)
    if left:
        raise BuildError("%s: left unfilled: %s" % (ch["file"], left))
    return s


def topics(ch):
    return [(m.group(1), _plain(m.group(2)))
            for m in re.finditer(r'<section class="topic" id="([^"]+)">\s*<h3>(.*?)</h3>', ch["html"], flags=re.S)]


# ------------------------------------------------------------------------------------------------------- pages ---
def _group(pre):
    name = None
    for start, label in GROUPS:
        if (pre.isdigit() and start.isdigit() and int(pre) >= int(start)) or (not pre.isdigit() and not start.isdigit()):
            name = label
    return name


def web_page(chs, ids):
    toc, last = [], None
    for ch in chs:
        g = _group(ch["pre"])
        if g != last:
            toc.append('<li class="h">%s</li>' % g)
            last = g
        no = '<span class="n">%s</span>' % ch["num"] if ch["num"] else ""
        toc.append('<li class="c"><a href="#%s">%s%s</a>' % (ch["id"], no, html.escape(ch["title"])))
        sub = topics(ch)
        if sub:
            toc.append('<ul>%s</ul>' % "".join('<li><a href="#%s">%s</a></li>' % (t, html.escape(n)) for t, n in sub))
        toc.append("</li>")
    body = "\n".join(render(ch, "web", ids) for ch in chs)
    body = re.sub(r"<table\b.*?</table>", lambda m: '<div class="tw">%s</div>' % m.group(0), body, flags=re.S)   # (a
    # table wider than a phone scrolls in its own box, never the page)
    css = (MAN / "web.css").read_text(encoding="utf-8")
    js = (MAN / "manual.js").read_text(encoding="utf-8")
    return ("<!doctype html>\n" + LICENCE + '<html lang="en">\n<head>\n<meta charset="utf-8">\n'
            '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'
            "<title>%s</title>\n<style>\n%s\n</style>\n</head>\n<body class=\"manual\">\n"
            '<header class="mtop"><button class="menu" type="button" aria-label="Contents">&#9776;</button>'
            '<a class="mark" href="https://hitosu.github.io/ptfm/" title="PT/FM\'s page" aria-label="PT/FM\'s page">%s</a>'
            '<span class="mt">%s</span><span class="mv">for PT/FM %s</span>'
            '<a class="pdf" href="%s" download><span class="long">DOWNLOAD PDF MANUAL</span><span class="short">PDF'
            '</span></a></header>\n'
            '<nav class="side" aria-label="Contents"><input class="search" type="search" placeholder="Search the manual"'
            ' aria-label="Search the manual"><ul class="toc">%s</ul></nav>\n'
            '<main class="doc">\n%s\n</main>\n'
            '<aside class="pcol" aria-label="The FM-1"><div class="pstick">'
            '<button class="pfold" type="button" aria-expanded="true">HIDE THE FM-1</button>%s'
            '<p class="hint">Point at a key in the text, or a row of a key table: it lights here.</p>'
            '<div class="pshot" aria-live="polite"></div></div></aside>\n'
            "<script>\n%s\n</script>\n</body>\n</html>\n") % (
        TITLE, css, LOGO, TITLE.replace("PT/FM ", "", 1), version(), PDF_NAME, "".join(toc), body,
        manual_panel.panel_svg("web", screen=shot_png(PANEL_SCREEN, "web") if PANEL_SCREEN in ids else None,
                               say=True), js)


def _index_key(term):
    """an index entry's place: case aside, a leading "the" or "a" aside"""
    return re.sub(r"^(the|a|an) ", "", term.lower())


@functools.lru_cache(maxsize=None)
def panel_names():
    """the panel's printed names: {name: the panel chapter's topic that shows it}"""
    L = manual_panel.layout()
    out = {k["name"]: "panel-knobs-buttons" for k in L["knobs"]}
    out.update({b["name"]: "panel-knobs-buttons" for b in L["buttons"]})
    out.update({k["label"]: "panel-keys" for k in L["keys"] if k.get("label")})
    return out


# words a reader looks up that no heading or <dt> names: (term, the place that answers it)
INDEX_MORE = [("mute", "basics-home"), ("solo", "basics-home"), ("backup", "editor-page-fm1"),
              ("undo", "basics-saving"), ("speaker", "settings-sound"), ("headphones", "settings-sound"),
              ("battery", "panel-screen"), ("volume", "editor-knobs"), ("colours", "panel-screen"),
              ("lights", "panel-lights"), ("messages", "messages"), ("shortcuts", "shortcuts")]


def index_terms(ch, s):
    """the print page's index entries of one rendered chapter: its title, each topic's heading and each <dt> (one
    naming several, "CUT, RESO", each of them), every <dt> given an id to point at; a <dt> named as a panel control
    ("OP1", "LFO") is told apart by its topic, "OP1 (DX7)"; (s with the ids, [(term, id)])"""
    out = [(ch["title"], ch["id"])] if ch["kind"] != "front" else []
    out += [(n, t) for t, n in topics(ch)]
    starts = [(m.start(), _plain(m.group(1))) for m in
              re.finditer(r'<section class="topic" id="[^"]+">\s*<h3>(.*?)</h3>', s, flags=re.S)]
    count = [0]

    def dt(m):
        count[0] += 1
        did = "ix-%s-%d" % (ch["id"], count[0])
        where = [n for p, n in starts if p < m.start()]
        where = where[-1] if where and len(where[-1]) <= 16 else ch["title"]
        for term in re.split(r", | / ", _plain(m.group(1))):
            if len(term) > 1:                           # (an envelope's lone A, D: no entry of their own)
                out.append(("%s (%s)" % (term, where) if term in panel_names() else term, did))
        return '<dt id="%s">%s</dt>' % (did, m.group(1))
    s = re.sub(r"<dt>(.*?)</dt>", dt, s, flags=re.S)
    if ch["id"] == "panel":
        out += [(name, tid) for name, tid in panel_names().items()]
    return s, out


def _page(pages, tid):
    if pages is None:
        return 0
    if tid not in pages:
        raise BuildError("the PDF has no page for #%s (a place the contents or the index points at)" % tid)
    return pages[tid]


def index_section(entries, pages, main=()):
    """the index (H): every entry once, its pages in order, each a link to its place, a chapter's start (main) in
    bold; before the PDF's first pass knows them, a page reads 000"""
    by = {}
    for term, tid in entries:
        e = by.setdefault(_index_key(term), {"term": term, "ids": []})
        if tid not in e["ids"]:
            e["ids"].append(tid)
    out, letter = [], None
    for key in sorted(by):
        e = by[key]
        first = key[:1].upper() if key[:1].isalpha() else "#"
        if first != letter:
            out.append('<li class="ih">%s</li>' % first)
            letter = first
        seen, refs = set(), []
        for tid in sorted(e["ids"], key=lambda t: _page(pages, t)):
            pg = str(_page(pages, tid)) if pages else "000"
            if pg not in seen or not pages:             # (the first pass links every place: each gets its page)
                seen.add(pg)
                refs.append(('<a href="#%s"><b>%s</b></a>' if tid in main else '<a href="#%s">%s</a>') % (tid, pg))
        out.append('<li>%s <span class="ipg">%s</span></li>' % (html.escape(e["term"]), ", ".join(refs)))
    return ('<section class="chapter index" id="index" data-kind="appendix"><h2 data-num="H">Index</h2><ul>%s</ul>'
            '</section>' % "".join(out))


def print_page(chs, ids, pages=None):
    """the print page; pages (each id's page, from the PDF's first pass) fills the contents' and the index's numbers"""
    parts, contents, entries = [], [], []

    def pg(tid):
        return '<span class="pg" data-for="%s">%s</span>' % (tid, _page(pages, tid) if pages else "")
    for ch in chs:
        s, terms = index_terms(ch, render(ch, "print", ids))
        parts.append(s)
        if ch["kind"] != "front":
            entries += terms
            contents.append('<li class="c"><a href="#%s"><span class="n">%s</span>%s</a>%s</li>' % (
                ch["id"], ch["num"], html.escape(ch["title"]), pg(ch["id"])))
            contents += ['<li class="t"><a href="#%s">%s</a>%s</li>' % (t, html.escape(n), pg(t)) for t, n in topics(ch)]
        if ch["kind"] == "front":
            parts.append("\0CONTENTS\0")                # (the contents after the front, once all are known)
    contents.append('<li class="c"><a href="#index"><span class="n">H</span>Index</a>%s</li>' % pg("index"))
    entries += INDEX_MORE
    parts.append(index_section(entries, pages, {ch["id"] for ch in chs}))   # (last: its length moves no other page)
    body = "\n".join(parts).replace("\0CONTENTS\0", '<section class="contents" id="contents"><h2>Contents</h2><ol>'
                                    + "".join(contents) + "</ol></section>")
    css = webfont.archivo_css() + "\n" + (MAN / "print.css").read_text(encoding="utf-8")
    return ("<!doctype html>\n" + LICENCE + '<html lang="en">\n<head>\n<meta charset="utf-8">\n<title>%s</title>\n'
            "<style>\n%s\n</style>\n</head>\n<body class=\"print\">\n%s\n</body>\n</html>\n") % (TITLE, css, body)


# --------------------------------------------------------------------------------------------------------- PDF ---
def print_pdf(page, pdf):
    """the print page printed by a headless Chrome (a profile of its own in build/manual/chrome)"""
    pdf.unlink(missing_ok=True)
    cmd = [os.environ.get("CHROME", "google-chrome"), "--headless=new", "--no-pdf-header-footer", "--no-first-run",
           "--disable-gpu", "--user-data-dir=" + str(WORK / "chrome"), "--print-to-pdf=" + str(pdf),
           page.resolve().as_uri()]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    except (OSError, subprocess.TimeoutExpired) as e:
        raise BuildError("--pdf: Chrome did not print: %s" % e)
    if r.returncode or not pdf.exists():
        raise BuildError("--pdf: Chrome did not print:\n" + r.stderr[-2000:])


DEST = re.compile(r'^\s*(\d+) \[.*\] "(.*)"$')


def pages_of(pdf):
    """each place's page: Chrome names a destination, with its page, for every id a link of the page points at
    (pdfinfo -dests lists them)"""
    try:
        out = subprocess.run(["pdfinfo", "-dests", str(pdf)], capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        raise BuildError("--pdf: pdfinfo -dests: %s" % e)
    return {m.group(2): int(m.group(1)) for m in map(DEST.match, out.splitlines()) if m}


def stamp():
    """the PDF's dates: SOURCE_DATE_EPOCH, else the newest of what the manual is made of (D:YYYYMMDDHHmmSS, UTC)"""
    t = os.environ.get("SOURCE_DATE_EPOCH")
    if t is None:
        t = _newest(list((MAN / "src").glob("*.html")) + [MAN / "print.css", ROOT / "fw" / "VERSION", Path(__file__)])
    return time.strftime("%Y%m%d%H%M%S", time.gmtime(int(float(t)))).encode()


def pin(data, when):
    """two builds of the same page byte for byte: the Info dates set to when, and the tagged structure's node ids
    (the page's DOM node numbers, different at every load) numbered again from 0 in their order (so the ID tree stays
    sorted); every rewrite keeps its length, so the cross-reference table stays right"""
    n = len(data)
    data = re.sub(rb"/(CreationDate|ModDate) \(D:\d{14}", lambda m: b"/%s (D:%s" % (m.group(1), when), data)
    nodes = {k: i for i, k in enumerate(sorted(set(re.findall(rb"\(node(\d{8})\)", data)), key=int))}
    data = re.sub(rb"\(node(\d{8})\)", lambda m: b"(node%08d)" % nodes[m.group(1)], data)
    if len(data) != n:
        raise BuildError("--pdf: pinning changed the PDF's length")
    return data


def page_links(data):
    """every link a plain jump to its page (/Dest [page /XYZ x y z]) instead of a named one, and the names' table
    taken out of the catalog: nothing a viewer could take for a look-up elsewhere (a PDF opened from a network share,
    \\\\wsl.localhost among them, made one ask to connect). The cross-reference table follows the new lengths."""
    cat = re.search(rb"/Dests (\d+) 0 R\n", data)
    if not cat:
        return data
    obj = re.search(rb"\n%s 0 obj\n<<(.*?)>>\nendobj" % cat.group(1), data, flags=re.S)
    names = dict(re.findall(rb"/([^\s\[/]+) (\[\d+ 0 R /XYZ [^\]]*\])", obj.group(1)) if obj else [])
    edits = [(cat.start(), cat.end(), b"")]
    for m in re.finditer(rb"/Dest /([^\s/>]+)", data):
        if m.group(1) not in names:
            raise BuildError("--pdf: a link to %s, which the PDF does not place" % m.group(1).decode())
        edits.append((m.start(), m.end(), b"/Dest " + names[m.group(1)]))
    edits.sort()
    parts, pos, ends, shifts, delta = [], 0, [], [], 0
    for s, e, r in edits:
        parts += [data[pos:s], r]
        pos = e
        delta += len(r) - (e - s)
        ends.append(e)
        shifts.append(delta)
    parts.append(data[pos:])
    new = bytearray(b"".join(parts))

    def moved(off):                                     # (an object's offset: never inside an edit)
        i = bisect.bisect_right(ends, off)
        return off + (shifts[i - 1] if i else 0)
    sx = re.search(rb"startxref\n(\d+)\n%%EOF\s*$", data)
    xref = int(sx.group(1)) if sx else -1
    if data[xref:xref + 5] != b"xref\n":
        raise BuildError("--pdf: the PDF's cross-reference table is not where its end says")
    p = moved(xref) + 5
    while True:
        m = re.match(rb"(\d+) (\d+)\r?\n", bytes(new[p:p + 32]))
        if not m:
            break                                       # (the trailer)
        p += m.end()
        for _ in range(int(m.group(2))):
            if new[p + 17:p + 18] == b"n":
                new[p:p + 10] = b"%010d" % moved(int(new[p:p + 10]))
            p += 20
    end = re.search(rb"startxref\n(\d+)\n%%EOF\s*$", new)
    return bytes(new[:end.start(1)]) + b"%d" % moved(xref) + bytes(new[end.end(1):])


def make_pdf(chs, ids, prn_out, pdf_out):
    """two passes: the first finds each place's page, the second prints them in the contents and the index; a place
    on another page in the second fails the build"""
    tmp = WORK / "pass.pdf"
    WORK.mkdir(parents=True, exist_ok=True)
    print_pdf(prn_out, tmp)
    first = pages_of(tmp)
    prn_out.write_text(print_page(chs, ids, first), encoding="utf-8")
    print_pdf(prn_out, tmp)
    second = pages_of(tmp)
    used = set(re.findall(r'href="#([^"]+)"', prn_out.read_text(encoding="utf-8")))
    moved = sorted(k for k in used if first.get(k) != second.get(k))
    if moved:
        raise BuildError("--pdf: places moved between the passes: %s" % ", ".join(
            "#%s %s -> %s" % (k, first.get(k), second.get(k)) for k in moved[:8]))
    pdf_out.write_bytes(page_links(pin(tmp.read_bytes(), stamp())))
    info = subprocess.run(["pdfinfo", str(pdf_out)], capture_output=True, text=True).stdout
    m = re.search(r"^Pages:\s+(\d+)", info, flags=re.M)
    return int(m.group(1)) if m else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--pdf", action="store_true")
    ap.add_argument("--no-shots", action="store_true")
    ap.add_argument("--src", default=str(MAN / "src"))
    ap.add_argument("--work")
    a = ap.parse_args()
    try:
        exe = shots_program()
        if not a.no_shots:
            make_shots(exe)
        ids = shot_ids(exe)
        chs = chapters(a.src)
        web_out = Path(a.work) / "manual.html" if a.work else DIST / "manual.html"
        prn_out = Path(a.work) / "print.html" if a.work else WORK / "print.html"
        web, prn = web_page(chs, ids), print_page(chs, ids)
        for p, t in ((web_out, web), (prn_out, prn)):
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(t, encoding="utf-8")
        print("%s  %d B\n%s  %d B" % (web_out, len(web.encode()), prn_out, len(prn.encode())))
        scr_out = Path(a.work) / "screens" if a.work else SITE_SHOTS
        write_screens(ids, scr_out)
        print("%s  %d screens" % (scr_out, len(ids)))
        if a.pdf:
            pdf_out = (Path(a.work) if a.work else DIST) / PDF_NAME
            n = make_pdf(chs, ids, prn_out, pdf_out)
            print("%s  %d B, %d pages" % (pdf_out, pdf_out.stat().st_size, n))
    except BuildError as e:
        print("build_manual:", e)
        sys.exit(1)


if __name__ == "__main__":
    main()

# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The manual's completeness (specs/manual.md section 4): what the firmware has and the manual lacks, on the built
pages dist/manual/manual.html and build/manual/print.html. From ptfm/:
    python3 -I tests/manual/check_manual.py            FAIL lines, exit 1 on any
    python3 -I tests/manual/check_manual.py --report   what is missing, chapter by chapter (exit 0)

  1 shortcuts   every help line naming a key, every row of specs/ui/controls.md's key map and of the screens'
                controls tables: a key table row with the same keys and hold in its chapter and in appendix A
  2 messages    every toast and question (the specs' message tables matched to the firmware's texts, and every text
                given to a toast directly): its line in appendix B; every message quoted: a text of the firmware
  3 places      each screen its chapter; each SETTINGS item and tool, each effect and knob, each instrument page,
                group and parameter: a <dt> in its chapter
  4 integrity   links, ids, keys joined by + outside a combination
  5 never       what is left for players to find: FOMA's corners and score, the touch that steers it (K4), the
                header's warning
  6 version     on both pages
  7 characters  only the PDF font's"""
import re
import sys
from html.parser import HTMLParser
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "fw" / "tools"))
sys.path.insert(0, str(ROOT / "tools"))
import gen_texts  # noqa: E402

SPECS = ROOT / "specs" / "ui"
CORE = ROOT / "fw" / "core"
LEFT_TO_FIND = {"CORNER!", "SCORE: %d%s", "!!! OVER 9000 !!!"}
NEVER = ["CORNER!", "SCORE:", "OVER 9000"]
BLACK = ["OP1", "OP2", "OP3", "OP4", "OP5", "OP6", "PIT", "GLO", "MONO", "POLY"]
CONTROLS = ["OCT-", "OCT+", "FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC",
            "MASTER", "SELECT", "PRESETS", "ALGORITHM", "K1", "K2", "K3", "K4"]
# the user's own examples, checked by name: (chapter, keys, a word of the row)
EXAMPLES = [("basics", {"SEL", "PRESETS"}, "cartridge"), ("fx", {"ARP"}, "punch-in")]
TOAST_CALLS = ("ui_toast", "ui_toastf")
MESSAGE_TABLES = ("Toast", "Text", "Message", "Question")


# ----------------------------------------------------------------------------------------------------- shortcuts ---
class Shortcut(tuple):
    """the keys (names: a control's, B:<print> a black key, B:* any, WHITE KEY, KEYS) and whether held (None: either,
    a combination the specs name without saying)"""
    def __new__(cls, keys, held):
        return tuple.__new__(cls, (frozenset(keys), None if held is None else bool(held)))

    keys = property(lambda s: s[0])
    held = property(lambda s: s[1])

    def __repr__(self):
        return " + ".join(sorted(self.keys)) + {True: " held", False: "", None: " (held or not)"}[self.held]


MARK = re.compile(r"\{([A-Z0-9+\- ]+?)\*?\}")


def _mark_key(name, after):
    if name == "GLO":
        return "B:GLO" if after.startswith(" KEY") else "GLO"
    if name == "B-GLO":
        return "B:GLO"
    if name in BLACK:
        return "B:" + name
    return {"BLANK KEY": "B:BLANK", "BLACK KEY": "B:*", "BLACK KEYS": "B:*", "WHITE KEYS": "WHITE KEY",
            "KEY": "KEYS"}.get(name, name)


def line_shortcuts(line, parent=None):
    """a help line's shortcuts: the keys named before each ':' (a line with none, all its keys); a part saying HOLD or
    HELD with no key goes on from the line above (parent)"""
    line = re.sub(r"\{GLO\}(?=: \[)", "{B-GLO}", line)        # (GLO opening a screen: the black key)
    line = line.replace("BLACK {GLO}", "{B-GLO}")
    line = re.sub(r"\{K([1-4])\}-\{K([1-4])\}",
                  lambda m: " ".join("{K%d}" % k for k in range(int(m.group(1)), int(m.group(2)) + 1)), line)
    segs = line.split(":")
    parts = segs[:-1] if len(segs) > 1 else segs
    # each part's alternatives apart: ';' always, ' OR ' between keys, and a line without ':' a list at its commas
    pieces = []
    for p in parts:
        for q in p.split(";"):
            for r in (q.split(",") if len(segs) == 1 else [q]):
                pieces += re.split(r" OR (?=\{)", r)
    out = []
    for p in pieces:
        keys = {_mark_key(m.group(1), p[m.end():]) for m in MARK.finditer(p)}
        held = re.search(r"\b(HOLD|HELD)\b", p) is not None
        if keys:
            out.append(Shortcut(keys, held))
        elif held and len(segs) > 1 and (out or parent is not None):
            out.append(Shortcut((out[-1] if out else parent).keys, True))
    return out


def help_page_shortcuts(page):
    """a help page's shortcuts, its title left out; a line goes on from the last line with a key until a line with no
    key and no indent"""
    out, parent = [], None
    for line in page.split("\n")[1:]:
        got = line_shortcuts(line, parent)
        out += got
        if got and MARK.search(line):
            parent = got[-1]
        elif not line.startswith(" "):
            parent = None
    return out


_CELL_WORDS = [(r"\bwhite keys?\b", "WHITE KEY"), (r"\bblack keys?\b", "B:*"), (r"\bblank key\b", "B:BLANK"),
               (r"(?<![A-Za-z])(?:keys|KEYS)\b", "KEYS")]


def cell_shortcut(text):
    """a controls table's first cell (Markdown) as a shortcut, or None when it names no key"""
    t = re.sub(r"[*`]", "", text).split("(")[0]
    held = re.search(r"\b(held|hold|HELD|HOLD)\b", t) is not None
    keys = set()
    for pat, name in _CELL_WORDS:
        if re.search(pat, t):
            keys.add(name)
            t = re.sub(pat, " ", t)
    for m in re.finditer(r"\bK([1-4])-K([1-4])\b", t):
        keys |= {"K%d" % k for k in range(int(m.group(1)), int(m.group(2)) + 1)}
    t = re.sub(r"\bK[1-4]-K[1-4]\b", " ", t)
    if re.search(r"(?<![A-Za-z])OCT(?![-+A-Za-z])", t):
        keys |= {"OCT-", "OCT+"}
    for name in sorted(set(CONTROLS + BLACK), key=len, reverse=True):
        if re.search(r"(?<![A-Za-z0-9])%s(?![A-Za-z0-9+-])" % re.escape(name), t):
            keys.add("B:" + name if name in BLACK and name != "GLO" else name)
            t = t.replace(name, " ")
    if not keys:
        return None
    return Shortcut(keys, held if held or "+" not in t else None)


def satisfied(need, rows):
    want = need.keys - {"B:*"}
    for r, _ in rows:
        if (need.held is not None and r.held != need.held) or not want <= r.keys:
            continue
        if "B:*" in need.keys and not any(k.startswith("B:") for k in r.keys):
            continue
        return True
    return False


def find_text(rows, keys, word):
    return any(set(keys) <= r.keys and word in text.lower() for r, text in rows)


# ---------------------------------------------------------------------------------------------------- the page ---
class Node:
    __slots__ = ("tag", "attrs", "kids", "parent")

    def __init__(self, tag, attrs, parent):
        self.tag, self.attrs, self.kids, self.parent = tag, dict(attrs), [], parent

    def text(self):
        return "".join(k if isinstance(k, str) else k.text() for k in self.kids)

    def walk(self):
        for k in self.kids:
            if not isinstance(k, str):
                yield k
                yield from k.walk()

    def find(self, tag=None, cls=None):
        return [n for n in self.walk() if (tag is None or n.tag == tag)
                and (cls is None or cls in n.attrs.get("class", "").split())]

    def up(self, tag, cls=None):
        n = self.parent
        while n is not None and not (n.tag == tag and (cls is None or cls in n.attrs.get("class", "").split())):
            n = n.parent
        return n


VOID = {"img", "input", "meta", "br", "link", "hr", "source", "col", "area", "base", "wbr"}


class _Parser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root = Node("#root", {}, None)
        self.cur = self.root

    def handle_starttag(self, tag, attrs):
        n = Node(tag, attrs, self.cur)
        self.cur.kids.append(n)
        if tag not in VOID:
            self.cur = n

    def handle_startendtag(self, tag, attrs):
        self.cur.kids.append(Node(tag, attrs, self.cur))

    def handle_endtag(self, tag):
        n = self.cur
        while n is not None and n.tag != tag:
            n = n.parent
        if n is not None and n.parent is not None:
            self.cur = n.parent

    def handle_data(self, data):
        self.cur.kids.append(data)


class Page:
    def __init__(self, html_text):
        p = _Parser()
        p.feed(html_text)
        self.root = p.root
        self.chapters = {n.attrs.get("id"): n for n in self.root.find("section", "chapter")}

    def _in(self, ch, topic=None):
        node = self.chapters.get(ch)
        if node is not None and topic:
            node = next((n for n in node.walk() if n.attrs.get("id") == topic), None)
        return node

    def rows(self, ch, topic=None):
        node = self._in(ch, topic)
        out = []
        for t in (node.find("table", "keys") if node is not None else []):
            for tr in t.find("tr"):
                if tr.up("thead") is not None:
                    continue
                tds = [k for k in tr.kids if not isinstance(k, str) and k.tag in ("td", "th")]
                if not tds:
                    continue
                keys = set()
                for kb in tds[0].find("kbd"):
                    cls, txt = kb.attrs.get("class", ""), kb.text().strip()
                    if cls == "w":
                        keys.add("WHITE KEY")
                    elif cls == "b":
                        keys.add("B:*" if txt.lower().startswith("black key") else "B:" + txt)
                    else:
                        keys.add(txt)
                held = re.search(r"\b(held|hold)\b", tds[0].text().lower()) is not None
                if keys:
                    out.append((Shortcut(keys, held), tr.text()))
        return out

    def dts(self, ch):
        """the names a chapter explains: its <dt>s and its topics' headings (an effect has a section of its own)"""
        node = self._in(ch)
        out = set()
        for dt in (node.find("dt") if node is not None else []) + (node.find("h3") if node is not None else []):
            t = " ".join(dt.text().split())
            out.add(t)
            out |= {x.strip() for x in re.split(r", | / ", t)}
        return out

    def samps(self, ch=None):
        node = self._in(ch) if ch else self.root
        return [(" ".join(s.text().split()), s.attrs.get("data-t")) for s in
                (node.find("samp", "msg") if node is not None else [])]


# ---------------------------------------------------------------------------------------------------- messages ---
def template_re(t):
    parts = re.split(r"#|%[-0-9.]*[sdxXcu]", t)
    return re.compile(".{1,40}?".join(re.escape(p) for p in parts))


def _literal(t):
    return len(re.sub(r"#|%[-0-9.]*[sdxXcu]", "", t))


def pick_template(example, templates):
    c = [t for t in templates if _literal(t) >= 3 and template_re(t).fullmatch(example)]
    return max(c, key=lambda t: (_literal(t), len(t))) if c else None


def _sp(s):
    return " ".join(s.split())


def check_quoted(page, texts):
    """every samp.msg a firmware text (spaces counted as one, as the page's are)"""
    fails = []
    norm = {_sp(x) for x in texts}
    for text, t in page.samps():
        if t is not None:
            if t not in texts:
                fails.append("2 messages: a quoted template is not a firmware text: %r" % t)
            elif not template_re(t).fullmatch(text):
                fails.append("2 messages: %r does not fill its template %r" % (text, t))
        elif text not in norm:
            fails.append("2 messages: a quoted message is not a firmware text: %r" % text)
    return fails


def _release_code(src):
    """the source without its PTFM_DEBUG parts (the debug build's own)"""
    out, stack = [], []                                 # [is the condition about PTFM_DEBUG, is this branch the debug one]
    for line in src.split("\n"):
        s = line.strip()
        if s.startswith("#if"):
            about = "PTFM_DEBUG" in s
            stack.append([about, about and not (s.startswith("#ifndef") or "!defined" in s)])
        elif s.startswith(("#else", "#elif")) and stack:
            if stack[-1][0]:
                stack[-1][1] = not stack[-1][1]
        elif s.startswith("#endif") and stack:
            stack.pop()
            continue
        if not any(debug for _, debug in stack):
            out.append(line)
    return "\n".join(out)


def _calls(src, names):
    """the argument text of each call to one of names"""
    for m in re.finditer(r"\b(%s)\(" % "|".join(names), src):
        i, depth = m.end(), 1
        while i < len(src) and depth:
            depth += {"(": 1, ")": -1}.get(src[i], 0)
            i += 1
        yield src[m.end():i - 1]


def direct_toasts():
    out = set()
    for p in sorted(CORE.rglob("*.c")):
        src = _release_code(p.read_text(encoding="utf-8"))
        for args in _calls(src, TOAST_CALLS):
            out |= {gen_texts.unescape(m.group(2)) for m in gen_texts.code_marks(args)}
    return out


def spec_messages():
    """the examples of the specs' message tables (a Toast table's rows; a Text | Where table's rows shown as a toast, a
    question or a progress): (example, the file)"""
    out = []
    for f in sorted(list(SPECS.glob("*.md")) + list((SPECS / "screens").glob("*.md"))):
        head = None
        for line in f.read_text(encoding="utf-8").split("\n"):
            s = line.strip()
            if not s.startswith("|"):
                head = None
                continue
            cells = [c.strip() for c in s.strip("|").split("|")]
            if head is None:
                head = cells
                continue
            if set(cells[0]) <= set("-: "):
                continue
            if head[0] == "Toast" or (head[:2] == ["Text", "Where"] and len(cells) > 1
                                      and re.match(r"(toast|question|every question|progress)\b", cells[1])):
                whole = re.sub(r"\((?:[^()`]|`[^`]*`)*\)", "", cells[0])    # (the variants in brackets are parts)
                out += [(e, f.name) for e in re.findall(r"`([^`]+)`", whole)]
    return out


def required_messages(texts):
    need = {}
    for ex, f in spec_messages():
        t = pick_template(ex, texts)
        if t:
            need.setdefault(t, ex)
    for t in direct_toasts():
        need.setdefault(t, t)
    for t in LEFT_TO_FIND:
        need.pop(t, None)
    return need


# ------------------------------------------------------------------------------------------------------ places ---
def screens():
    md = (SPECS / "navigation.md").read_text(encoding="utf-8")
    return [m.group(1) for m in re.finditer(r"^\| (?:OP\d|PIT|GLO|MONO|POLY|FX button) \| \*\*([A-Z]+)\*\*", md, re.M)]


def settings_items():
    out, head = [], None
    for line in (SPECS / "screens" / "settings.md").read_text(encoding="utf-8").split("\n"):
        if not line.startswith("|"):
            head = None
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if head is None:
            head = cells
            continue
        if head[:2] == ["Item", "Values"] and not set(cells[0]) <= set("-: "):
            name = re.sub(r"[*`]", "", cells[0])
            out.append(name)
            if name == "TOOLS":
                out += [x.strip() for x in cells[1].split(",")]
    return out


def _tx(s):
    return [gen_texts.unescape(m.group(1)) for m in re.finditer(r'\bTX\("((?:[^"\\]|\\.)*)"\)', s)]


def fx_names():
    src = (CORE / "ui" / "fxdef.c").read_text(encoding="utf-8")
    names = [m.group(1) for m in re.finditer(r'\{TX\("([^"]+)"\), C_[A-Z]+,', src)]
    for m in re.finditer(r"fx_knob_t K_[A-Z_]+\[\] = \{(.*?)\};", src, flags=re.S):
        names += _tx(m.group(1))
    return names


def instrument_names():
    src = (CORE / "ui" / "ed_inst.c").read_text(encoding="utf-8")
    names = []
    for m in re.finditer(r"(?:ins_grp_t|ins_page_t) [A-Z_]+\[[A-Z0-9_]*\] = \{(.*?)\};", src, flags=re.S):
        names += _tx(m.group(1))
    for m in re.finditer(r"^#define [A-Z_]+(?:\([^)]*\))? (.*(?:\\\n.*)*)", src, flags=re.M):
        names += _tx(m.group(1))
    return names


# ------------------------------------------------------------------------------------------------- integrity ---
def check_combos(html_text):
    s = re.sub(r'<span class="combo"[^>]*>.*?</span>', "", html_text, flags=re.S)
    return ["4 integrity: keys joined by + outside a combination: ...%s..." % m.group(0)[:80]
            for m in re.finditer(r"<kbd[^>]*>[^<]*</kbd>\s*\+\s*<kbd[^>]*>[^<]*</kbd>", s)]


def _text_only(html_text):
    s = re.sub(r"<(style|script)\b.*?</\1>", " ", html_text, flags=re.S)
    return re.sub(r"<[^>]+>", " ", s)


def check_never(html_text):
    """FOMA's corners and score and the header's !!! OVER 9000 !!! by their texts; the touch that steers FOMA (K4) by
    any sentence, list item or definition naming both"""
    t = _text_only(html_text)
    out = ["5 never: %r is mentioned" % w for w in NEVER if w in t]
    for part in re.split(r"<(?:li|dd|dt|p|td|tr)\b[^>]*>|(?<=[.!?])\s", html_text):
        p = _text_only(part)
        if re.search(r"\bFOMA\b", p) and re.search(r"\bK4\b", p):
            out.append("5 never: the touch that steers FOMA (K4) is mentioned: %s" % " ".join(p.split())[:80])
    return out


def check_chars(html_text, chars):
    import html as h
    lack = sorted(set(h.unescape(_text_only(html_text))) - set(chars) - set("\n\t\r"))
    return ["7 characters: the PDF's font has no %r" % "".join(lack)] if lack else []


def check_links(page):
    ids, fails = {}, []
    for n in page.root.walk():
        i = n.attrs.get("id")
        if i:
            if i in ids:
                fails.append("4 integrity: the id %r twice" % i)
            ids[i] = n
    for n in page.root.find("a"):
        href = n.attrs.get("href", "")
        if href.startswith("#") and href[1:] not in ids:
            fails.append("4 integrity: a link to #%s, which is nowhere" % href[1:])
    return fails


# -------------------------------------------------------------------------------------------------------- main ---
def run():
    """[(chapter or '', failure)]"""
    web_t = (ROOT / "dist" / "manual" / "manual.html").read_text(encoding="utf-8")
    prn_t = (ROOT / "build" / "manual" / "print.html").read_text(encoding="utf-8")
    web = Page(web_t)
    texts = set(gen_texts.source_texts())
    out = []

    own, shared = gen_texts.help_pages()
    needs = [(sc.lower(), page) for sc, pages in own for page in pages] + [("basics", p) for p in shared]
    for ch, page in needs:
        for s in help_page_shortcuts(page):
            if not satisfied(s, web.rows(ch)):
                out.append((ch, "1 shortcuts: %r (the help: %s)" % (s, page.split("\n")[0])))
            if not satisfied(s, web.rows("shortcuts")):
                out.append(("shortcuts", "1 shortcuts: %r for %s (the help: %s)" % (s, ch.upper(), page.split("\n")[0])))
    tables = [(SPECS / "controls.md", "basics")] + [(f, f.stem) for f in sorted((SPECS / "screens").glob("*.md"))]
    for f, ch in tables:
        head = None
        for line in f.read_text(encoding="utf-8").split("\n"):
            if not line.startswith("|"):
                head = None
                continue
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if head is None:
                head = cells[0]
                continue
            if head in ("Control", "Key") and not set(cells[0]) <= set("-: "):
                s = cell_shortcut(cells[0])
                if s and not satisfied(s, web.rows(ch)):
                    out.append((ch, "1 shortcuts: %r (%s: %s)" % (s, f.name, cells[0][:60])))
                if s and not satisfied(s, web.rows("shortcuts")):
                    out.append(("shortcuts", "1 shortcuts: %r (%s: %s)" % (s, f.name, cells[0][:60])))
    for ch, keys, word in EXAMPLES:
        if not find_text(web.rows(ch), keys, word):
            out.append((ch, "1 shortcuts: the example %s with %r" % (" + ".join(sorted(keys)), word)))

    have = web.samps("messages")
    for t, ex in sorted(required_messages(texts).items()):
        r = template_re(_sp(t))
        if not any(dt == t or txt == _sp(t) or r.fullmatch(txt) for txt, dt in have):
            out.append(("messages", "2 messages: %r (e.g. %r)" % (t, ex)))
    out += [("", f) for f in check_quoted(web, texts)]

    for s in screens():
        if s.lower() not in web.chapters:
            out.append((s.lower(), "3 places: the screen %s has no chapter" % s))
    for ch, names in (("settings", settings_items()), ("fx", fx_names()), ("instruments", instrument_names())):
        have_dt = web.dts(ch)
        for n in dict.fromkeys(names):
            if n not in have_dt:
                out.append((ch, "3 places: %r has no <dt>" % n))

    out += [("", f) for f in check_links(web) + check_combos(web_t)]
    out += [("", f) for f in check_never(web_t) + check_never(prn_t)]
    v = (ROOT / "fw" / "VERSION").read_text().strip()
    for name, t in (("the page", web_t), ("the print page", prn_t)):
        if "for PT/FM " + v not in t:
            out.append(("", "6 version: %s does not say for PT/FM %s" % (name, v)))
    import webfont
    out += [("", f) for f in check_chars(prn_t, webfont.ARCHIVO_CHARS)]
    return out


def main():
    out = run()
    if "--report" in sys.argv:
        by = {}
        for ch, f in out:
            by.setdefault(ch or "(the pages)", []).append(f)
        for ch in sorted(by):
            print("== %s: %d" % (ch, len(by[ch])))
            for f in by[ch]:
                print("  " + f)
        print("check_manual: %d missing" % len(out))
        return 0
    for ch, f in out:
        print("FAIL %s%s" % (f, " (%s)" % ch if ch else ""))
    print("check_manual: %d FAILED" % len(out) if out else "check_manual: complete")
    return 1 if out else 0


if __name__ == "__main__":
    sys.exit(main())

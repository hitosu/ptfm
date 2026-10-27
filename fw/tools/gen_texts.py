# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The firmware's fixed texts, packed (specs/ui/texts.md).

    python3 -I fw/tools/gen_texts.py OUTDIR                    texts.c in OUTDIR
    python3 -I fw/tools/gen_texts.py OUTDIR --rewrite DIR      also copies of fw/core's sources in DIR,
                                                              each T("...") as T(n /* "..." */)
    python3 -I fw/tools/gen_texts.py OUTDIR --check            unpack every text again and compare

Texts: every T("...") in fw/core's sources (one literal, no concatenation), then the help's pages: each
screen's Help part (specs/ui/screens/<screen>.md) and the shared pages (specs/ui/help.md), one text a
page, its lines split by a line break. A text used in several places is stored once.

Packing: byte-pair encoding over all the texts together. The symbols start as the texts' characters; the
commonest pair of symbols becomes a new token, again and again. A token is one byte (a table of up to 256)
or 12 bits (up to 4,096); several table sizes are tried and the smallest total of texts, table and index
is kept. The table stores each token as its two halves.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CORE = os.path.join(ROOT, "fw", "core")
SPECS = os.path.join(ROOT, "specs", "ui")
MARK = re.compile(r'\b(TX?)\("((?:[^"\\]|\\.)*)"\)')   # T("...") where drawn, TX("...") in a table (txt.h)
FONT_CHARS = set(chr(c) for c in range(32, 127))


def sources():
    out = []
    for d, _, fs in os.walk(CORE):
        for f in sorted(fs):
            if f.endswith((".c", ".h")):
                out.append(os.path.join(d, f))
    return sorted(out)


def unescape(s):
    return s.encode("latin-1").decode("unicode_escape")


def code_marks(s):
    """the T("...") marks in code, not in comments (a comment may show one as an example)"""
    out, i, n = [], 0, len(s)
    while i < n:
        if s.startswith("/*", i):
            j = s.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif s.startswith("//", i):
            j = s.find("\n", i)
            i = n if j < 0 else j
        elif s[i] in "\"'":
            q, j = s[i], i + 1
            while j < n and s[j] != q:
                j += 2 if s[j] == "\\" else 1
            i = j + 1
        else:
            m = MARK.match(s, i) if s[i] == "T" and (i == 0 or not (s[i - 1].isalnum() or s[i - 1] == "_")) else None
            if m:
                out.append(m)
                i = m.end()
            else:
                i += 1
    return out


def source_texts():
    """the marked texts, each once, after an empty one: no text is numbered 0, which a table's TX field keeps for none"""
    seen = [""]
    for p in sources():
        with open(p, encoding="utf-8") as fh:
            for m in code_marks(fh.read()):
                t = unescape(m.group(2))
                if t not in seen:
                    seen.append(t)
    return seen


def code_blocks(path, heading):
    """the code blocks of a part, from its heading to the next heading of its level: each a page"""
    with open(path, encoding="utf-8") as fh:
        md = fh.read()
    at = md.find("\n" + heading + "\n")
    if at < 0:
        return []
    rest = md[at + len(heading) + 2:]
    nxt = re.search(r"\n## ", rest)
    sec = rest if not nxt else rest[:nxt.start()]
    return ["\n".join(l.rstrip() for l in m.group(1).rstrip().split("\n"))
            for m in re.finditer(r"```\n([\s\S]*?)```", sec)]


def help_pages():
    """[(screen, [pages])], and the shared pages"""
    own = []
    d = os.path.join(SPECS, "screens")
    for f in sorted(os.listdir(d)):
        if f.endswith(".md"):
            own.append((f[:-3].upper(), code_blocks(os.path.join(d, f), "## Help")))
    shared = code_blocks(os.path.join(SPECS, "help.md"), "## The shared pages")
    return own, shared


def help_readings():
    """{key: its reading}, as the help draws them (ui_dlg.c's READ_KEY and READ_TXT)"""
    with open(os.path.join(CORE, "ui", "ui_dlg.c")) as f:
        c = f.read()
    keys = re.findall(r'"([A-Z0-9]+)"', re.search(r"READ_KEY\[\d+\] = \{([^}]*)\}", c).group(1))
    txts = re.findall(r'TX\("([^"]*)"\)', re.search(r"READ_TXT\[\d+\] = \{([\s\S]*?)\};", c).group(1))
    if len(keys) != len(txts):
        sys.exit("gen_texts: ui_dlg.c's READ_KEY and READ_TXT differ in length")
    return dict(zip(keys, txts))


def help_drawn(line, read, named):
    """a help line as drawn (ui_dlg.c help_line): the marks' brackets gone, a reading written after its key kept, a
    {KEY*}'s added where this help has not named the key yet; named: the keys named so far, updated"""
    out, i = "", 0
    while i < len(line):
        ch = line[i]
        if ch not in "{[":
            out += ch
            i += 1
            continue
        e = line.index("}" if ch == "{" else "]", i)
        name, i = line[i + 1:e], e + 1
        auto = name.endswith("*")
        name = name.rstrip("*")
        out += name
        r = " (%s)" % read[name] if ch == "{" and name in read else None
        if r and auto and name not in named:
            out += r
            named.add(name)
        elif r and line.startswith(r, i):
            out += r
            named.add(name)
            i += len(r)
    return out


def merge(s, a, b, new):
    out, i = [], 0
    while i < len(s):
        if i + 1 < len(s) and s[i] == a and s[i + 1] == b:
            out.append(new)
            i += 2
        else:
            out.append(s[i])
            i += 1
    return out


def train(texts, base, vocab):
    """byte-pair merges up to vocab symbols: the pairs (left, right) of the new tokens, in the order
    merged (the first k are the merges of a table of base + k), and the token count after each"""
    seqs = [[base.index(c) for c in t] for t in texts]
    counts, where = {}, {}

    def add(j, sign):
        s = seqs[j]
        for i in range(len(s) - 1):
            k = (s[i], s[i + 1])
            counts[k] = counts.get(k, 0) + sign
            if sign > 0:
                where.setdefault(k, set()).add(j)
    for j in range(len(seqs)):
        add(j, 1)
    pairs, total, totals = [], sum(len(s) for s in seqs), []
    while len(base) + len(pairs) < vocab:
        best, bc = None, 1
        for k, c in counts.items():
            if c > bc or (c == bc and best is not None and c > 1 and k < best):
                best, bc = k, c
        if best is None or bc < 2:
            break
        a, b = best
        new = len(base) + len(pairs)
        pairs.append(best)
        for j in sorted(where.get(best, ())):
            add(j, -1)
            n0 = len(seqs[j])
            seqs[j] = merge(seqs[j], a, b, new)
            total -= n0 - len(seqs[j])
            add(j, 1)
        counts = {k: c for k, c in counts.items() if c > 0}
        totals.append(total)
    return pairs, totals


def apply_merges(texts, base, pairs):
    seqs = [[base.index(c) for c in t] for t in texts]
    for k, (a, b) in enumerate(pairs):
        new = len(base) + k
        seqs = [merge(s, a, b, new) if a in s else s for s in seqs]
    return seqs


def spell(tok, base, pairs):
    if tok < len(base):
        return base[tok]
    a, b = pairs[tok - len(base)]
    return spell(a, base, pairs) + spell(b, base, pairs)


def pack(texts, base):
    """the smallest of the table sizes tried: (bits, pairs, seqs, total bytes). One training run gives
    every size: a smaller table's merges are the first ones of a bigger table's."""
    pairs, totals = train(texts, base, 4096)
    best = None
    for bits, vocab in ((8, 256), (12, 512), (12, 1024), (12, 2048), (12, 4096)):
        k = min(len(pairs), vocab - len(base))
        ntok = totals[k - 1] if k > 0 else sum(len(t) for t in texts)
        data = ntok if bits == 8 else (ntok * 12 + 7) // 8 + 1
        table = k * (2 if bits == 8 else 3)
        total = data + table + 2 * (len(texts) + 1) + len(base)
        if best is None or total < best[3]:
            best = (bits, k, None, total)
    bits, k, _, total = best
    return bits, pairs[:k], apply_merges(texts, base, pairs[:k]), total


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def comment(s):
    return s.replace("*/", "* /").replace("\n", "|")


def unpack(bits, data, index, pairs, base, i):
    toks = []
    for k in range(index[i], index[i + 1]):
        if bits == 8:
            toks.append(data[k])
        else:
            b = k * 12
            v = data[b >> 3] | data[(b >> 3) + 1] << 8 if (b >> 3) + 1 < len(data) else data[b >> 3]
            toks.append((v >> (b & 7)) & 0xFFF)
    return "".join(spell(t, base, pairs) for t in toks)


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    out = os.path.abspath(args[0])
    src = source_texts()
    own, shared = help_pages()
    texts = list(src)
    help_ids = []
    for screen, pages in own:
        first = len(texts)
        texts += pages
        help_ids.append((screen, first, len(pages)))
    shared_first = len(texts)
    texts += shared
    # the help's pages fit the screen (specs/ui/help.md), as the playground checks them: 17 lines a page, a first page
    # 15 of its own (a blank and PRESS HOME TO GET BACK under it); STATUS's first is the shared GLOBAL KEYS page
    for screen, pages in own:
        first = (shared + pages if screen == "STATUS" else pages + shared)[:1]
        if first and first[0].count("\n") + 1 > 15:
            sys.exit("gen_texts: %s's first help page: %d lines of its own (15 at most)" % (screen, first[0].count("\n") + 1))
        for k, p in enumerate(pages + shared):
            if p.count("\n") + 1 > 17:
                sys.exit("gen_texts: %s's help page %d: %d lines (17 at most)" % (screen, k + 1, p.count("\n") + 1))
    # ... and every line 40 characters as drawn, with the readings it gets in that screen's page order
    read = help_readings()
    for screen, pages in own:
        named = set()
        for k, p in enumerate(shared + pages if screen == "STATUS" else pages + shared):
            for ln in p.split("\n"):
                d = help_drawn(ln, read, named)
                if len(d) > 40:
                    sys.exit("gen_texts: %s's help page %d: %d characters as drawn: %s" % (screen, k + 1, len(d), d))
    for t in texts:
        bad = set(t) - FONT_CHARS - {"\n"}
        if bad:
            sys.exit("gen_texts: characters not in the fonts: %r in %r" % ("".join(sorted(bad)), t[:40]))
        bare = re.sub(r"%[-0-9]*[sdxXcu]", "", t)       # (a text used as ui_fmt's format keeps its %s, %02X ...)
        if bare != bare.upper():
            sys.exit("gen_texts: lowercase in %r (the screens show capitals only)" % t[:40])
    base = sorted(set("".join(texts)) | {" "})
    bits, pairs, seqs, total = pack(texts, base)
    index = [0]
    for s in seqs:
        index.append(index[-1] + len(s))
    flat = [t for s in seqs for t in s]
    if bits == 8:
        data = bytes(flat)
    else:
        acc, nb, buf = 0, 0, bytearray()
        for t in flat:
            acc |= t << nb
            nb += 12
            while nb >= 8:
                buf.append(acc & 255)
                acc >>= 8
                nb -= 8
        if nb:
            buf.append(acc & 255)
        buf.append(0)                                   # (a spare byte: the decoder reads two at a time)
        data = bytes(buf)
    depth = {}
    for k, (a, b) in enumerate(pairs):                  # the decoder's stack (txt.c: 64) holds a token's depth
        depth[len(base) + k] = 1 + max(depth.get(a, 0), depth.get(b, 0))
    if depth and max(depth.values()) > 60:
        sys.exit("gen_texts: a token %d deep: more than the decoder's stack" % max(depth.values()))
    for i, t in enumerate(texts):                       # the tool's own check: every text comes back
        if unpack(bits, data, index, pairs, base, i) != t:
            sys.exit("gen_texts: text %d does not unpack" % i)
    if "--check" in args:
        print("gen_texts: %d texts unpack as written" % len(texts))
    plain = sum(len(t) for t in texts)

    lines = ["/* SPDX-License-Identifier: GPL-3.0-only",
             " * Copyright (C) 2026 Vasily Kazantsev */",
             "/* generated by fw/tools/gen_texts.py: do not edit. The fixed texts and the help, packed by",
             " * byte-pair tokens (specs/ui/texts.md): %d texts, %d B plain, %d B packed in all; tokens of %d bits."
             % (len(texts), plain, total, bits) + " */",
             '#include "txt.h"', "",
             "const uint8_t TXT_BITS = %d;" % bits,
             "const uint16_t TXT_BASE = %d;                /* tokens below this are characters */" % len(base),
             "const char TXT_CHARS[] = %s;" % cstr("".join(base)), ""]
    lines.append("/* the merged tokens: each its two halves; its spelled-out words in the comment */")
    if bits == 8:
        lines.append("const uint8_t TXT_PAIRS[%d] = {" % max(1, 2 * len(pairs)))
        for k, (a, b) in enumerate(pairs):
            lines.append("    %d, %d,   /* 0x%02X %s */" % (a, b, len(base) + k, cstr(spell(len(base) + k, base, pairs))))
    else:
        lines.append("const uint8_t TXT_PAIRS[%d] = {             /* 3 bytes a token: a | b << 12 */" % max(1, 3 * len(pairs)))
        for k, (a, b) in enumerate(pairs):
            v = a | b << 12
            lines.append("    %d, %d, %d,   /* 0x%03X %s */" % (v & 255, v >> 8 & 255, v >> 16, len(base) + k,
                                                           cstr(spell(len(base) + k, base, pairs)).replace("*/", "* /")))
    if not pairs:
        lines.append("    0")
    lines += ["};", ""]
    lines.append("const uint8_t TXT_DATA[%d] = {" % len(data))
    for i in range(0, len(data), 24):
        lines.append("    " + ", ".join(str(b) for b in data[i:i + 24]) + ",")
    lines += ["};", ""]
    lines.append("/* where each text starts, in tokens; text n is n's entry to n + 1's */")
    lines.append("const uint16_t TXT_INDEX[%d] = {" % len(index))
    for i, t in enumerate(texts):
        lines.append("    %d,   /* %d %s */" % (index[i], i, comment(cstr(t))[:110]))
    lines.append("    %d" % index[-1])
    lines += ["};", "const uint16_t TXT_COUNT = %d;" % len(texts), ""]
    lines.append("const txt_help_t TXT_HELP[] = {")
    for screen, first, n in help_ids:
        lines.append('    {"%s", %d, %d},' % (screen, first - len(src), n))
    lines += ["};", "const uint8_t TXT_HELP_N = %d;" % len(help_ids)]
    pages = list(range(len(src), len(texts)))
    lines.append("const uint16_t TXT_HELP_PAGES[%d] = {%s};" % (len(pages), ", ".join(map(str, pages))))
    lines.append("const uint16_t TXT_HELP_SHARED = %d;" % (shared_first - len(src)))
    lines.append("const uint8_t TXT_HELP_SHARED_N = %d;" % len(shared))
    lines.append("")
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "texts.c"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))
    print("gen_texts: %d texts (%d marked, %d help pages), %d B plain -> %d B packed, %d-bit tokens, %d merges"
          % (len(texts), len(src), len(texts) - len(src), plain, total, bits, len(pairs)))

    if "--rewrite" in args:
        dst = os.path.abspath(args[args.index("--rewrite") + 1])
        ids = {t: i for i, t in enumerate(texts) if i < len(src)}
        for p in sources():
            with open(p, encoding="utf-8") as fh:
                s = fh.read()
            parts, at = [], 0
            for m in code_marks(s):
                t = unescape(m.group(2))
                parts += [s[at:m.start()], "%s(%d /* %s */)" % (m.group(1), ids[t], comment(cstr(t)))]
                at = m.end()
            s = "".join(parts) + s[at:]
            q = os.path.join(dst, os.path.relpath(p, CORE))
            os.makedirs(os.path.dirname(q), exist_ok=True)
            with open(q, "w", encoding="utf-8") as fh:
                fh.write(s)
        print("gen_texts: copies with the texts' numbers in", dst)


if __name__ == "__main__":
    main()

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
commonest pair of symbols becomes a new token, again and again. A token is as many bits as the table's size
needs (9 for up to 512, 10 for 1,024 ...), every size is tried and the smallest total of texts, table and
index is kept. The table stores each token as its two halves, bit-packed as the texts' tokens are. The index:
each text's length in tokens, a byte, and every 16th text's start (txt.c's TXT_STEP).
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


def help_drawn(line, read, named, seen=None):
    """a help line as drawn (ui_dlg.c help_line): the marks' brackets gone, a reading written after its key kept, a
    {KEY*}'s added where this help has not named the key yet; named: the keys named so far, updated; seen: each mention
    of a key with a reading appended, (key, its reading drawn, the line as drawn up to it ending in `+`)"""
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
        held = out.rstrip().endswith("+")
        out += name
        r = " (%s)" % read[name] if ch == "{" and name in read else None
        drawn = False
        if r and auto and name not in named:
            out += r
            named.add(name)
            drawn = True
        elif r and line.startswith(r, i):
            out += r
            named.add(name)
            i += len(r)
            drawn = True
        if r and seen is not None:
            seen.append((name, drawn, held))
    return out


READ_FIRST = ("ENV", "SEQ", "ARP")      # readings given at the key's first mention (help.md's Readings)
SCREEN_KEYS = ("OP1", "OP2", "OP3", "OP4", "OP5", "OP6", "PIT", "MONO", "POLY")   # theirs on the shared SCREENS page


def reading_bad(screen, k, line, mine, name, drawn, held, counts):
    """a mention's reading out of its place (help.md's Readings), or None: ENV's, SEQ's and ARP's at the key's first
    mention and at no later one; LFO's (only where it clears) and the screen keys' at most once, the screen keys' only
    on the shared pages. The hold lines: ARP's reading is allowed again, not required, wherever ARP's mark comes right
    after a `+` (held: the line as drawn up to the mark ends in `+`, spaces aside: `+ {ARP}`, `HOLD {FX} + {ARP}`), and
    such a reading counts for no other. line: the line as drawn, named in the message; counts: per key, its mentions so
    far and the readings drawn, updated"""
    c = counts.setdefault(name, [0, 0])
    first = c[0] == 0
    c[0] += 1
    hold = name == "ARP" and held
    at = "%s's help page %d, %r" % (screen, k + 1, line)
    if first and name in READ_FIRST and not drawn:
        return "%s: %s's first mention, without its reading" % (at, name)
    if drawn and not hold:
        c[1] += 1
        again = not first if name in READ_FIRST else c[1] > 1
        if again:
            return "%s: %s's reading again (once, at its first mention)" % (at, name)
    if drawn and mine and name in SCREEN_KEYS:
        return "%s: %s's reading on the screen's own page (the SCREENS page gives it)" % (at, name)
    return None


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


IDX_STEP = 16                           # (SIZE) a text's start kept every 16 texts; each text's length a byte


def index_len(n):
    return n + 2 * ((n + IDX_STEP - 1) // IDX_STEP + 1)


def pack(texts, base):
    """the smallest of every table size: (bits, pairs, seqs, total bytes). A token is as many bits as its table's size
    needs (ceil(log2(characters + merges)), at least 8), a merge's two halves twice that; a merge stays in only while
    it saves more than its own room (SIZE). One training run gives every size: a smaller table's merges are the first
    ones of a bigger table's."""
    pairs, totals = train(texts, base, 4096)
    best = None
    for k in range(len(pairs) + 1):
        bits = max(8, (len(base) + k - 1).bit_length())
        ntok = totals[k - 1] if k > 0 else sum(len(t) for t in texts)
        total = (ntok * bits + 7) // 8 + 2 + (k * 2 * bits + 7) // 8 + 2 + index_len(len(texts)) + len(base)
        if best is None or total < best[2]:
            best = (bits, k, total)
    bits, k, total = best
    return bits, pairs[:k], apply_merges(texts, base, pairs[:k]), total


def best_parse(texts, base, pairs):
    """every text spelled with the fewest tokens of the table (any token anywhere: the decoder only expands them), by
    dynamic programming over the tokens' spellings; the merges' own order gives more (SIZE)"""
    spell_of = {}
    for t in range(len(base) + len(pairs)):
        spell_of[t] = base[t] if t < len(base) else spell_of[pairs[t - len(base)][0]] + spell_of[pairs[t - len(base)][1]]
    first = {}
    for t, s in spell_of.items():
        first.setdefault(s[0], []).append((s, t))
    out = []
    for txt in texts:
        n = len(txt)
        best, how = [0] + [n + 1] * n, [None] * (n + 1)
        for i in range(n):
            for s, t in first.get(txt[i], ()):
                j = i + len(s)
                if j <= n and best[i] + 1 < best[j] and txt.startswith(s, i):
                    best[j], how[j] = best[i] + 1, (i, t)
        seq, j = [], n
        while j:
            j, t = how[j]
            seq.append(t)
        out.append(seq[::-1])
    return out


def packed(texts, base, out):
    """pack and best_parse, kept in OUTDIR/texts.cache.json keyed by the texts and this file: a build with the same texts
    reads it back instead of training again (~5 s)"""
    import hashlib
    import json
    with open(os.path.abspath(__file__), "rb") as fh:
        key = hashlib.sha256(fh.read() + json.dumps([texts, base]).encode()).hexdigest()
    path = os.path.join(out, "texts.cache.json")
    try:
        with open(path) as fh:
            c = json.load(fh)
        if c.get("key") == key:
            return c["bits"], [tuple(p) for p in c["pairs"]], c["seqs"]
    except (OSError, ValueError):
        pass
    bits, pairs, _, _ = pack(texts, base)
    seqs = best_parse(texts, base, pairs)
    os.makedirs(out, exist_ok=True)
    with open(path, "w") as fh:
        json.dump({"key": key, "bits": bits, "pairs": pairs, "seqs": seqs}, fh)
    return bits, pairs, seqs


def bitpack(vals, bits):
    """vals at bits each, low bits first, and 2 spare bytes (the decoder reads three at a time)"""
    acc, nb, buf = 0, 0, bytearray()
    for v in vals:
        acc |= v << nb
        nb += bits
        while nb >= 8:
            buf.append(acc & 255)
            acc >>= 8
            nb -= 8
    if nb:
        buf.append(acc & 255)
    return bytes(buf) + b"\0\0"


def bits_at(p, b, n):
    v = p[b >> 3] | p[(b >> 3) + 1] << 8 | p[(b >> 3) + 2] << 16
    return v >> (b & 7) & ((1 << n) - 1)


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def comment(s):
    return s.replace("*/", "* /").replace("\n", "|")


def unpack(bits, data, lens, starts, ptab, base, i):
    """text i as txt.c unpacks it: its start from the one kept for its 16 and the lengths before it in them"""
    k = starts[i // IDX_STEP] + sum(lens[i - i % IDX_STEP:i])

    def spell_t(t):
        if t < len(base):
            return base[t]
        j = t - len(base)
        return spell_t(bits_at(ptab, 2 * j * bits, bits)) + spell_t(bits_at(ptab, (2 * j + 1) * bits, bits))
    return "".join(spell_t(bits_at(data, (k + m) * bits, bits)) for m in range(lens[i]))


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
    # ... and every line 40 characters as drawn, with the readings it gets in that screen's page order; each reading in
    # its place there (reading_bad)
    read = help_readings()
    for screen, pages in own:
        named, counts = set(), {}
        for k, p in enumerate(shared + pages if screen == "STATUS" else pages + shared):
            mine = k >= len(shared) if screen == "STATUS" else k < len(pages)
            for ln in p.split("\n"):
                seen = []
                d = help_drawn(ln, read, named, seen)
                if len(d) > 40:
                    sys.exit("gen_texts: %s's help page %d: %d characters as drawn: %s" % (screen, k + 1, len(d), d))
                for name, drawn, held in seen:
                    bad = reading_bad(screen, k, d, mine, name, drawn, held, counts)
                    if bad:
                        sys.exit("gen_texts: " + bad)
    for t in texts:
        bad = set(t) - FONT_CHARS - {"\n"}
        if bad:
            sys.exit("gen_texts: characters not in the fonts: %r in %r" % ("".join(sorted(bad)), t[:40]))
        bare = re.sub(r"%[-0-9]*[sdxXcu]", "", t)       # (a text used as ui_fmt's format keeps its %s, %02X ...)
        if bare != bare.upper():
            sys.exit("gen_texts: lowercase in %r (the screens show capitals only)" % t[:40])
    base = sorted(set("".join(texts)) | {" "})
    bits, pairs, seqs = packed(texts, base, out)
    lens = [len(s) for s in seqs]
    if max(lens) > 255:
        sys.exit("gen_texts: a text of %d tokens: more than its length's byte holds" % max(lens))
    starts, at = [], 0
    for i, n in enumerate(lens):
        if i % IDX_STEP == 0:
            starts.append(at)
        at += n
    starts.append(at)
    if at > 65535:
        sys.exit("gen_texts: %d tokens: more than a start's uint16 holds" % at)
    data = bitpack([t for s in seqs for t in s], bits)
    ptab = bitpack([h for ab in pairs for h in ab], bits)
    total = len(data) + len(ptab) + len(lens) + 2 * len(starts) + len(base)
    depth = {}
    for k, (a, b) in enumerate(pairs):                  # the decoder's stack (txt.c: 64) holds a token's depth
        depth[len(base) + k] = 1 + max(depth.get(a, 0), depth.get(b, 0))
    if depth and max(depth.values()) > 60:
        sys.exit("gen_texts: a token %d deep: more than the decoder's stack" % max(depth.values()))
    for i, t in enumerate(texts):                       # the tool's own check: every text comes back
        if unpack(bits, data, lens, starts, ptab, base, i) != t:
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
    lines.append("/* the merged tokens' halves, TXT_BITS each, a then b, bit-packed (low bits first); the tokens' words: */")
    for k in range(len(pairs)):
        lines.append("/*   0x%03X %s */" % (len(base) + k, comment(cstr(spell(len(base) + k, base, pairs)))[:100]))
    lines.append("const uint8_t TXT_PAIRS[%d] = {" % len(ptab))
    for i in range(0, len(ptab), 24):
        lines.append("    " + ", ".join(str(b) for b in ptab[i:i + 24]) + ",")
    lines += ["};", ""]
    lines.append("/* the texts' tokens, TXT_BITS each, bit-packed (low bits first) */")
    lines.append("const uint8_t TXT_DATA[%d] = {" % len(data))
    for i in range(0, len(data), 24):
        lines.append("    " + ", ".join(str(b) for b in data[i:i + 24]) + ",")
    lines += ["};", ""]
    lines.append("/* each text's length in tokens; text n starts at TXT_START[n / %d] plus the lengths before it there */"
                 % IDX_STEP)
    lines.append("const uint8_t TXT_LEN[%d] = {" % len(lens))
    for i, t in enumerate(texts):
        lines.append("    %d,   /* %d %s */" % (lens[i], i, comment(cstr(t))[:110]))
    lines += ["};"]
    lines.append("const uint16_t TXT_START[%d] = {%s};" % (len(starts), ", ".join(map(str, starts))))
    lines += ["const uint16_t TXT_COUNT = %d;" % len(texts), ""]
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

# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The manual's build (tools/build_manual.py, specs/manual.md section 2): what both pages hold, and the build refusing
a key the panel has not or a figure the screenshots program does not know. From ptfm/, after a build:
    python3 -I tests/manual/build_test.py"""
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
fails = checks = 0


def check(ok, what):
    global fails, checks
    checks += 1
    if not ok:
        fails += 1
        print("FAIL", what)


version = (ROOT / "fw" / "VERSION").read_text().strip()
web = (ROOT / "dist" / "manual" / "manual.html").read_text(encoding="utf-8")
prn = (ROOT / "build" / "manual" / "print.html").read_text(encoding="utf-8")

check(web.startswith("<!doctype html>"), "the page starts <!doctype html>")
check("<title>PT/FM OWNER'S MANUAL</title>" in web, "the page's title")
for name, page in (("page", web), ("print page", prn)):
    check("for PT/FM " + version in page, "the %s names the version (%s)" % (name, version))
    check("{{" not in page, "the %s: nothing left unfilled" % name)
    check(not re.search(r'src="https?:|<link\b[^>]*href="https?:|url\(\s*["\']?https?:|@import', page),
          "the %s loads nothing from the web" % name)   # (a link to PT/FM's home is a link, not a load)
    check(all(h.startswith(("https://hitosu.github.io/ptfm", "https://github.com/hitosu/ptfm"))
              for h in re.findall(r'<a [^>]*href="(https?:[^"]*)"', page)),
          "the %s links out only to PT/FM's home and repository" % name)
check(re.search(r'<img [^>]*data-shot="loops-control"[^>]*src="data:image/png;base64,', web) is not None,
      "the page: the LOOPS CONTROL screenshot inlined")
check(re.search(r'<img [^>]*data-shot="loops-control"[^>]*src="data:image/png;base64,', prn) is not None,
      "the print page: the LOOPS CONTROL screenshot inlined (as the LCD)")
check('font-family: "PTFM Archivo"' in prn, "the print page: Archivo inlined")
check('class="panel panel-web"' in web, "the page: the panel drawing")
pcol = web[web.find('<aside class="pcol"'):]
pcol = pcol[:pcol.find("</svg>")]
check(re.search(r'<image [^>]*class="p-shot"[^>]*href="data:image/png;base64,', pcol) is not None,
      "the FM-1 beside the text shows STATUS on its screen, not a black square")

# every key lights: kbds outside a combination, combinations, key table rows
body = web[web.find("<main"):]
kb = re.findall(r"<kbd[^>]*>", body)
combo = re.findall(r'<span class="combo"[^>]*>', body)
keys_tables = "".join(re.findall(r'<table class="keys">.*?</table>', body, flags=re.S))
rows = re.findall(r"<tr[^>]*>", re.sub(r"<thead>.*?</thead>", "", keys_tables, flags=re.S))
check(kb, "the page has keys")
inside = sum(len(re.findall(r"<kbd", m)) for m in re.findall(r'<span class="combo".*?</span>', body, flags=re.S))
check(sum("data-k=" in k for k in kb) == len(kb) - inside, "every kbd outside a combination has data-k")
check(all("data-k=" in c and 'tabindex="0"' in c for c in combo), "every combination has data-k and tabindex")
check(rows and all("data-k=" in r for r in rows), "every key table row has data-k")


def refused(add):
    t = ROOT / "build" / "manual" / "t-src"
    shutil.rmtree(t, ignore_errors=True)
    shutil.copytree(ROOT / "manual" / "src", t)
    f = t / "14-loops.html"
    f.write_text(f.read_text(encoding="utf-8").replace("</section>", add + "\n</section>", 1), encoding="utf-8")
    w = ROOT / "build" / "manual" / "t-work"
    r = subprocess.run([sys.executable, "-I", str(ROOT / "tools" / "build_manual.py"), "--no-shots", "--src", str(t),
                        "--work", str(w)], cwd=ROOT, capture_output=True, text=True)
    return r.returncode != 0 and "14-loops.html" in r.stdout + r.stderr


a_part = web[web.find('<section class="chapter" id="shortcuts"'):]
a_part = a_part[:a_part.find('<section class="chapter"', 10)] if '<section class="chapter"' in a_part[10:] else a_part
check(a_part.count('<table class="keys">') >= 11 and "Opens SONGS." in re.sub(r"<[^>]+>", "", a_part),
      "appendix A holds the chapters' key tables (basics' HOME + OP1 row among them)")
check('<a href="#songs">SONGS</a>' in a_part and '<h3>SONGS</h3>' in web and '<a href="#songs">SONGS</a>' not in
      web[web.find('<section class="chapter" id="songs"'):web.find('<section class="chapter" id="editor"')],
      "a screen's name links to its chapter, but not in its own chapter nor in a heading")
check(refused("<p><kbd>LOOPS</kbd></p>"), "a key the panel has not fails the build, naming the file")
check(refused('<div data-keys-from="nope"></div>'), "a key table copied from a chapter that has none fails the build")
check(refused('<figure data-shot="nope"><figcaption>x</figcaption></figure>'),
      "a figure the screenshots program does not know fails the build, naming the file")

print("build_test: %d of %d checks FAILED" % (fails, checks) if fails else "build_test: %d checks passed" % checks)
sys.exit(1 if fails else 0)

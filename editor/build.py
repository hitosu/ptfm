# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The editor's build (specs/editor.md): the firmware compiled for the browser with the editor's part
(emulator/build.py --extra editor/wasm --define PTFM_EDITOR, into build/editor/wasm), then the page, editor/index.html,
with every stylesheet, script, the pages' font and the WebAssembly written into one file:
dist/editor/editor.html.

    python3 -I editor/build.py              (from ptfm/) the WebAssembly, then the page
    python3 -I editor/build.py --page       the page only, on the WebAssembly built last

Opened as it is, editor/index.html loads its scripts from beside it and build/editor/ed_wasm.js (made here).
Needs clang and lld with the wasm32 target (the emulator's build)."""
import argparse
import base64
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ED = ROOT / "editor"
WORK = ROOT / "build" / "editor"
HEADER = "/* SPDX-License-Identifier: GPL-3.0-only\n * Copyright (C) 2026 Vasily Kazantsev */\n"


def wasm():
    """built aside (build/editor/wasm-new), then taken as build/editor/wasm/ptfm.wasm: a firmware that does not build
    now leaves the last one there"""
    r = subprocess.run([sys.executable, "-I", str(ROOT / "emulator" / "build.py"), "--extra", str(ED / "wasm"),
                        "--define", "PTFM_EDITOR", "--out", str(WORK / "wasm-new")], cwd=ROOT)
    if r.returncode:
        sys.exit("editor/build.py: the firmware's WebAssembly did not build; the last one is kept (--page uses it)")
    (WORK / "wasm").mkdir(parents=True, exist_ok=True)
    (WORK / "wasm" / "ptfm.wasm").write_bytes((WORK / "wasm-new" / "ptfm.wasm").read_bytes())


def wasm_script():
    data = (WORK / "wasm" / "ptfm.wasm").read_bytes()
    stamp = ROOT / "fw" / "VERSION"
    version = (stamp.read_text().strip() if stamp.exists() else "") or "DEV"
    body = json.dumps({"version": version, "wasm": base64.b64encode(data).decode()}, separators=(",", ":"))
    (WORK / "ed_wasm.js").write_text(f"{HEADER}/* PT/FM's firmware with the editor's part, for the preview and the virtual FM-1 "
                                     f"(made by editor/build.py) */\nwindow.PTFM_ED_WASM = {body};\n", encoding="utf-8")
    return len(data)


def fonts_css():
    sys.path.insert(0, str(ROOT / "fw" / "tools"))
    import webfont  # noqa: E402
    return webfont.fonts_css()


def page():
    html = (ED / "index.html").read_text(encoding="utf-8")
    base = ED

    def inline_css(m):
        return "<style>\n" + (base / m.group(1)).read_text(encoding="utf-8") + "</style>"

    def inline_js(m):
        src = (base / m.group(1)).resolve()
        js = src.read_text(encoding="utf-8").replace("</script", "<\\/script")
        return "<script>\n" + js + "</script>"

    html = re.sub(r'<link rel="stylesheet" href="([^"]+)">', inline_css, html)
    html = re.sub(r'<script src="([^"]+)"></script>', inline_js, html)
    if html.count("/*FONTS*/") != 1:
        sys.exit("editor/build.py: index.html needs /*FONTS*/ once")
    html = html.replace("/*FONTS*/", fonts_css())
    out = ROOT / "dist" / "editor" / "editor.html"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(html, encoding="utf-8")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--page", action="store_true", help="the page only, on the WebAssembly built last")
    a = ap.parse_args()
    WORK.mkdir(parents=True, exist_ok=True)
    if not a.page:
        wasm()
    n = wasm_script()
    out = page()
    print(f"editor.html  {out}  ({out.stat().st_size // 1024} KB; the firmware's WebAssembly {n // 1024} KB)")


if __name__ == "__main__":
    main()

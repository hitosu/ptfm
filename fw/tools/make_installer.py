#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""PT/FM's install page: web/install.html with the installer scripts fm1pkg.js + fm1ota.js and seed.js (as
classic scripts: pages open from file://, where Chrome refuses modules), the firmware package and the
demo disk (base64) inlined. Three buttons: INSTALL FIRMWARE (the update path), UPLOAD DEMO DISK and
UPLOAD CUSTOM DISK (a disk file picked; seeding, specs/files/blob.md), and REPLACE ALL, offered when a disk fits the
FM-1 only emptied. Nothing is fetched; the page checks what it
carries (sizes, SHA-256, the identity) before it offers them. The same inputs give the same page, byte for byte.

  fw/tools/make_installer.py PACKAGE.fwsc OUT.html [DISK.ptfmdsk] [--clean]   (fw/tools/build.py calls make())
  --clean: the CLEAN INSTALL switch on at first (as on the debug build's page)
"""
import base64
import hashlib
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import webfont  # noqa: E402

WEB = Path(__file__).resolve().parents[1] / "web"
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def make(pkg, product, out, version="", disk=None, debug=False, clean=None):
    """debug: the debug build's page (specs/debug.md): web/debug.html, with figures.js at its /*FIGURES*/,
    autotest.js at its /*AUTOTEST*/ and its font, Iosevka Aile (webfont.py), at its /*FONTS*/, written in at <!--DEBUG-->;
    clean (the debug page's by default): the CLEAN INSTALL switch on at first (INSTALL FIRMWARE then takes no
    backup, erases everything the FM-1 holds and writes the demo disk)"""
    clean = debug if clean is None else clean
    out = Path(out)
    if product_of(pkg) != product:
        raise SystemExit(f"make_installer: package identity {product_of(pkg)!r}, expected {product!r}")
    if b"FELUCCA-LOADER-1" not in pkg:              # the update loader's marker (loader/crt0_ldr.S)
        raise SystemExit("make_installer: no update loader in the package")
    html = (WEB / "install.html").read_text(encoding="utf-8")
    lib = strip_module((WEB / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((WEB / "fm1ota.js").read_text(encoding="utf-8"))
    seed = (WEB / "seed.js").read_text(encoding="utf-8") if (WEB / "seed.js").exists() else ""
    dbytes = Path(disk).read_bytes() if disk else b""
    if dbytes and dbytes[:8] != b"PTFMDISK":
        raise SystemExit(f"make_installer: {disk} is not a PT/FM disk")
    meta = json.dumps({"product": product, "version": version, "size": len(pkg),
                       "sha256": hashlib.sha256(pkg).hexdigest(),
                       "disk": {"size": len(dbytes), "sha256": hashlib.sha256(dbytes).hexdigest()} if dbytes else None,
                       "clean": bool(clean and dbytes)})
    for mark in ("/*LIB*/", "/*SEED*/", "/*META*/", "/*PKG*/", "/*DISK*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"make_installer: the template needs {mark} once")
    if html.count("<!--DEBUG-->") != 1:
        raise SystemExit("make_installer: the template needs <!--DEBUG--> once")
    dbg = ""
    if debug:
        dbg = (WEB / "debug.html").read_text(encoding="utf-8")
        for mark, js in (("/*FIGURES*/", "figures.js"), ("/*AUTOTEST*/", "autotest.js")):
            if dbg.count(mark) != 1:
                raise SystemExit(f"make_installer: debug.html needs {mark} once")
            dbg = dbg.replace(mark, (WEB / js).read_text(encoding="utf-8").replace("</script", "<\\/script"))
        if dbg.count("/*FONTS*/") != 1:
            raise SystemExit("make_installer: debug.html needs /*FONTS*/ once")
        dbg = dbg.replace("/*FONTS*/", webfont.fonts_css())
    for mark, text in (("/*LIB*/", lib), ("/*SEED*/", seed), ("/*META*/", meta),
                       ("/*PKG*/", base64.b64encode(pkg).decode()), ("/*DISK*/", base64.b64encode(dbytes).decode()),
                       ("<!--DEBUG-->", dbg)):
        html = html.replace(mark, text)
    out.write_text(html, encoding="utf-8")
    return out


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a != "--clean"]
    if len(args) not in (2, 3):
        raise SystemExit(__doc__)
    raw = Path(args[0]).read_bytes()
    print(make(raw, product_of(raw), args[1], disk=args[2] if len(args) == 3 else None, clean="--clean" in sys.argv))

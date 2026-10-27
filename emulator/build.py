# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""The emulator's build: the firmware's app (fw/core) and the web platform (emulator/plat_web.c)
compiled to WebAssembly, written as a classic script the page loads from the file system
(specs/emulator.md, specs/layout.md).

    python3 -I emulator/build.py              (from ptfm/) a dev build: build/fw/web/ptfm.wasm.js
    python3 -I emulator/build.py --release    the production build: dist/emulator/ptfm.wasm.js

Beside it, demo.ptfmdsk.js: the demo disk (dist/firmware/demo.ptfmdsk) as a script, for the page's DEMO DISK button
(a page opened from file:// cannot read a file by itself).

Options: --fw-end ADDR (hex) where the firmware's image ends on the FM-1 (FWS's free part starts
after it; by default the device build's image when there is one, else 0x50000); --out DIR a dev
build somewhere else (the tests' own); --extra DIR adds DIR's C files and headers (for trying the
platform before every part of the app exists); --debug the debug build's figures and state (PTFM_DEBUG:
the autotests' tests, fw/tests/run_autotest.sh), a dev build only; --define NAME (again for more) compiles with
-DNAME (PTFM_EDITOR: the editor page's parts of fw/core, editor/build.py's).

Needs clang and lld with the wasm32 target. The generated sources come from fw/tools/gen.py, into a
directory of this build's own (build/fw/web/gen).
"""
import argparse
import base64
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "fw" / "core"
EMU = ROOT / "emulator"
CFLAGS = ["--target=wasm32", "-O2", "-std=gnu11", "-ffreestanding", "-fno-builtin", "-nostdlib",
          "-Wall", "-Wno-unused-function"]
LDFLAGS = ["-Wl,--no-entry", "-Wl,--initial-memory=33554432", "-Wl,-z,stack-size=1048576"]
HEADER = "/* SPDX-License-Identifier: GPL-3.0-only\n * Copyright (C) 2026 Vasily Kazantsev */\n"


def sources(extra):
    files = sorted(CORE.rglob("*.c"))
    files += [EMU / "plat_web.c", EMU / "libc.c"]
    if extra:
        files += sorted(Path(extra).glob("*.c"))
    return files


def fw_end(arg):
    """Where the firmware ends on the FM-1: the device build's image, written from 0x4000 (plat_fw_end
    there), when the last device build was the whole firmware (not the bring-up's); else ~300 KB."""
    if arg:
        return int(arg, 16)
    image, whole, bringup = (ROOT / "build" / "fw" / n for n in ("ptfm.bin", "ptfm.fwsc", "ptfm-bringup.fwsc"))
    if image.exists() and whole.exists() and (not bringup.exists() or whole.stat().st_mtime >= bringup.stat().st_mtime):
        return 0x4000 + image.stat().st_size
    return 0x50000


def version(release):                              # fw/VERSION's number, as fw/tools/build.py's
    stamp = ROOT / "fw" / "VERSION"
    v = stamp.read_text().strip() if stamp.exists() else ""
    return (v or "DEV") if release else ((v + " DEV") if v else "DEV")


def script(name, obj, what):
    body = json.dumps(obj, separators=(",", ":"))
    return f"{HEADER}/* {what} (made by emulator/build.py) */\nwindow.{name} = {body};\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--release", action="store_true")
    ap.add_argument("--debug", action="store_true")       # (the debug build's figures: the autotests' tests)
    ap.add_argument("--fw-end")
    ap.add_argument("--extra")
    ap.add_argument("--out")
    ap.add_argument("--define", action="append", default=[])   # (-DNAME, as many as given)
    a = ap.parse_args()

    work = Path(a.out).resolve() if a.out else ROOT / "build" / "fw" / "web"
    gen = work / "gen"
    work.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, "-I", str(ROOT / "fw" / "tools" / "gen.py"), str(gen)], cwd=ROOT, check=True)

    end = fw_end(a.fw_end)
    # every directory of fw/core with headers, as the device's build gives the generated sources
    dirs = sorted({h.parent for h in CORE.rglob("*.h")} - {CORE})
    inc = ["-I" + str(EMU / "include"), "-I" + str(CORE)] + ["-I" + str(d) for d in dirs] + ["-I" + str(gen)]
    if a.extra:
        inc.append("-I" + str(Path(a.extra).resolve()))
    files = sources(a.extra) + sorted(gen.glob("*.c"))
    wasm = work / "ptfm.wasm"
    debug = ["-DPTFM_DEBUG", f'-DPTFM_VERSION="{version(a.release)} DEBUG"'] if a.debug else []
    if a.debug and a.release:
        sys.exit("emulator/build.py: --debug is a dev build (--out), never the release's")
    defines = ["-D" + d for d in a.define]
    cmd = ["clang", *CFLAGS, *inc, f"-DWEB_FW_END=0x{end:X}u", *debug, *defines, *map(str, files), *LDFLAGS, "-o", str(wasm)]
    r = subprocess.run(cmd, cwd=ROOT)
    if r.returncode:
        sys.exit("emulator/build.py: the WebAssembly did not build")

    out = ROOT / "dist" / "emulator" if a.release else work
    out.mkdir(parents=True, exist_ok=True)
    data = wasm.read_bytes()
    (out / "ptfm.wasm.js").write_text(script("PTFM_WASM", {
        "version": version(a.release), "fwEnd": end, "wasm": base64.b64encode(data).decode()},
        "PT/FM's firmware built for the emulator"), encoding="utf-8")
    print(f"ptfm.wasm.js     {out / 'ptfm.wasm.js'}  ({len(data)} B of WebAssembly; the firmware ends at 0x{end:X})")

    disk = ROOT / "dist" / "firmware" / "demo.ptfmdsk"
    if disk.exists():
        d = disk.read_bytes()
        (out / "demo.ptfmdsk.js").write_text(script("PTFM_DEMO", {
            "name": "demo.ptfmdsk", "ptd": base64.b64encode(d).decode()},
            "the demo disk, dist/firmware/demo.ptfmdsk, for the emulator's DEMO DISK button"), encoding="utf-8")
        print(f"demo.ptfmdsk.js  {out / 'demo.ptfmdsk.js'}  ({len(d)} B)")
    else:
        print("demo.ptfmdsk.js  not made: no dist/firmware/demo.ptfmdsk yet (tools/pack.py makes it)")


if __name__ == "__main__":
    main()

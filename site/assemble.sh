#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# hitosu.github.io/ptfm/ as GitHub publishes it (specs/site.md): the landing (site/) and the pages it links, out of
# what the repository holds; nothing is built. The workflow (.github/workflows/pages.yml) runs it; so does a preview:
#   sh site/assemble.sh OUT        then serve OUT's parent with OUT named ptfm, or OUT itself
set -eu
[ $# -eq 1 ] || { echo "usage: sh site/assemble.sh OUT"; exit 2; }
R=$(cd "$(dirname "$0")/.." && pwd)
OUT=$1
rm -rf "$OUT"
mkdir -p "$OUT"
V=$(cat "$R/fw/VERSION")

# the landing, its version from fw/VERSION
sed "s/@VERSION@/$V/g" "$R/site/index.html" > "$OUT/index.html"
cp -R "$R/site/img" "$R/site/screens" "$OUT/"
mkdir -p "$OUT/fonts"
cp "$R/site/fonts/"*.woff2 "$R/assets/fonts/Archivo.woff2" "$R/assets/fonts/Archivo-LICENSE.txt" \
   "$R/assets/fonts/Terminus-LICENSE.txt" "$OUT/fonts/"

# the pages, each one file but the emulator's
mkdir -p "$OUT/install" "$OUT/editor"
cp "$R/dist/firmware/install.html" "$OUT/install/index.html"
cp "$R/dist/editor/editor.html" "$OUT/editor/index.html"
if [ -f "$R/dist/manual/manual.html" ]; then        # (a checkout from before the manual: the site without it)
    mkdir -p "$OUT/manual"
    cp "$R/dist/manual/manual.html" "$OUT/manual/index.html"
    cp "$R/dist/manual/ptfm-manual.pdf" "$OUT/manual/"
else
    echo "site: no dist/manual/ yet, so no manual/"
fi

# the emulator: its one-file page; before there was one, emulator/ with what it loads at the paths it loads it from
# (../panel/, ../fw/web/, ../dist/emulator/)
mkdir -p "$OUT/emulator"
if [ -f "$R/dist/emulator/emulator.html" ]; then
    cp "$R/dist/emulator/emulator.html" "$OUT/emulator/index.html"
else
    mkdir -p "$OUT/panel" "$OUT/fw/web" "$OUT/dist/emulator"
    cp "$R/emulator/index.html" "$R/emulator/emulator.css" "$R/emulator/main.js" "$R/emulator/worklet.js" "$OUT/emulator/"
    cp "$R/panel/"*.js "$R/panel/"*.css "$OUT/panel/"
    cp "$R/fw/web/seed.js" "$OUT/fw/web/"
    cp "$R/dist/emulator/ptfm.wasm.js" "$R/dist/emulator/demo.ptfmdsk.js" "$OUT/dist/emulator/"
fi

touch "$OUT/.nojekyll"
echo "site: $OUT (PT/FM $V)"

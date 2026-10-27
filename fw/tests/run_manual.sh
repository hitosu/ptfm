#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# The manual (specs/manual.md): its screenshots made anew on the PC (fw/tests/manual_shots.c), both pages built
# (tools/build_manual.py), then tests/manual/: the panel drawing, the build, the checks' own test, and the manual's
# completeness against the firmware and the specs (check_manual.py). Needs cc, node and python3.
#   sh fw/tests/run_manual.sh           (--pdf: the PDF too, built twice and compared)
set -e
cd "$(dirname "$0")/../.."
python3 -I tools/build_manual.py $1
if [ "$1" = --pdf ]; then                               # the same PDF from a second build, byte for byte
    cp dist/manual/ptfm-manual.pdf build/manual/first.pdf
    python3 -I tools/build_manual.py --pdf --no-shots > /dev/null
    cmp build/manual/first.pdf dist/manual/ptfm-manual.pdf || { echo "run_manual.sh: two builds made different PDFs"; exit 1; }
    echo "the PDF: two builds the same ($(pdfinfo dist/manual/ptfm-manual.pdf | awk '/^Pages/{print $2}') pages)"
fi
python3 -I tests/manual/panel_test.py
python3 -I tests/manual/build_test.py
python3 -I tests/manual/check_test.py
python3 -I tests/manual/check_manual.py

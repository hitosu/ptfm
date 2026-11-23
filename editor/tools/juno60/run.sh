#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# The Juno-60 conversion: its generator (editor/js/juno60_tables.js, vectors.json here), the page's twin compared
# with it (tests/editor/juno60_test.js), then its check against KR-106 (build/juno60/check.md). KR106 names the
# directory of Ultramaster KR-106's sources (its Source/DSP headers, its tools' factory data). From ptfm/:
#   KR106=/path/to/ultramaster_kr106 sh editor/tools/juno60/run.sh [--no-check]
set -e
cd "$(dirname "$0")/../../.."
[ -n "$KR106" ] && [ -f "$KR106/Source/DSP/KR106_DSP.h" ] || { echo "run.sh: KR106 must name KR-106's sources (Source/DSP)"; exit 2; }
B=build/juno60
mkdir -p "$B"
CXX="${CXX:-g++}"
CC="${CC:-gcc}"
F="-std=c++17 -O2 -ffp-contract=off -w -I$KR106/Source/DSP -Ifw/core/eng/juno"
$CXX $F -o "$B/gen60" editor/tools/juno60/gen.cpp
"$B/gen60" editor/js/juno60_tables.js editor/tools/juno60/vectors.json
node tests/editor/juno60_test.js
[ "$1" = "--no-check" ] && exit 0
# PT/FM's engine as it is, built for the PC (its tables made as every build makes them)
python3 -I fw/tools/gen_tables.py "$B/fwgen" >/dev/null
$CC -std=c99 -O2 -ffp-contract=off -I"$B/fwgen" -Ifw/core -c -o "$B/juno.o" fw/core/eng/juno/juno.c
$CC -std=c99 -O2 -ffp-contract=off -I"$B/fwgen" -Ifw/core -c -o "$B/dx7.o" fw/core/eng/dx7/dx7.c
$CXX $F -o "$B/check" editor/tools/juno60/check.cpp "$B/juno.o" "$B/dx7.o"
# the renders in shards, one a core (at most 16), then the report from all their figures
J=$(nproc 2>/dev/null || echo 4)
[ "$J" -gt 16 ] && J=16
rm -f "$B"/part*.csv
i=0
while [ $i -lt $J ]; do
    "$B/check" "$KR106" figs $i $J > "$B/part$i.csv" &
    i=$((i + 1))
done
wait
"$B/check" "$KR106" report "$B/check.md" "$B"/part*.csv
cat "$B"/part*.csv > "$B/check.csv"

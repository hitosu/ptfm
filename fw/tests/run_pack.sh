#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# The packer (tools/pack.py): its codecs on made-up data, the demo disk packed again and read back against
# the playground's content, dist/firmware/demo.ptfmdsk up to date, the built-in instruments' C, and a damaged disk
# refused. Needs ffmpeg (the demo disk's FLAC files).
set -e
cd "$(dirname "$0")/../.."
OUT=build/fw/pack
mkdir -p "$OUT"
# pack.py puts tools/ first on its path: nothing there may be named after a standard module
python3 -I -c "
import os, sys
mine = {f[:-3] for f in os.listdir('tools') if f.endswith('.py')}
bad = sorted(mine & set(sys.stdlib_module_names))
sys.exit('run_pack: tools/ shadows standard modules: %s' % bad if bad else 0)
"

python3 -I tools/pack.py --selftest
python3 -I tools/pack.py --out "$OUT/demo.ptfmdsk"
python3 -I tools/pack.py --check "$OUT/demo.ptfmdsk"
cmp "$OUT/demo.ptfmdsk" dist/firmware/demo.ptfmdsk || { echo "run_pack: dist/firmware/demo.ptfmdsk is out of date: python3 -I tools/pack.py"; exit 1; }
python3 -I tools/pack.py --dump dist/firmware/demo.ptfmdsk "$OUT/dump"      # decoded records for the firmware's tests
python3 -I tools/pack.py --builtin "$OUT/gen"
# (the 11 synth cartridges: FUSION and its patterns are the demo disk's)
grep -q "const int builtin_count = 11;" "$OUT/gen/builtin.c" || { echo "run_pack: builtin.c: not 11 records"; exit 1; }
if command -v gcc >/dev/null; then
    gcc -std=c99 -Wall -Wextra -Werror -c -Ifw/core -o "$OUT/builtin.o" "$OUT/gen/builtin.c"
fi

# a damaged disk: one byte of a payload changed (its CRC no longer matches), and one of a CRC
python3 -I -c "
import sys
b = bytearray(open(sys.argv[1], 'rb').read())
c = bytearray(b)
b[40] ^= 0x10
open(sys.argv[2], 'wb').write(b)
c[24] ^= 0x01
open(sys.argv[3], 'wb').write(c)
" "$OUT/demo.ptfmdsk" "$OUT/bad1.ptfmdsk" "$OUT/bad2.ptfmdsk"
for bad in bad1 bad2; do
    if python3 -I tools/pack.py --check "$OUT/$bad.ptfmdsk" >/dev/null 2>&1; then
        echo "run_pack: a damaged disk ($bad) passed the check"; exit 1
    fi
done
echo "run_pack: ALL PASSED"

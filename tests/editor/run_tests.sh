#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# The editor's host tests (Node): the firmware's WebAssembly with the editor's part built first (editor/build.py),
# then the records against tools/ptfm_records.py, the loops' record (its version 3 converted), the FS against the virtual
# FM-1, the converters (modules given as
# arguments: MOD, S3M, XM files). From ptfm/:
#   sh tests/editor/run_tests.sh [MODULE...]        (NO_WASM=1: the WebAssembly built last, only the page again)
set -e
cd "$(dirname "$0")/../.."
if [ -n "$NO_WASM" ]; then python3 -I editor/build.py --page; else python3 -I editor/build.py; fi
mkdir -p build/editor
python3 -I tests/editor/py_dump.py dist/firmware/demo.ptfmdsk build/editor/py_dump.json
node tests/editor/records_test.js build/editor/py_dump.json
node tests/editor/loops_test.js
node tests/editor/model_test.js
node tests/editor/device_test.js
node tests/editor/preview_test.js
node tests/editor/conv_test.js "$@"
node tests/editor/pictures_test.js build/editor/wasm/ptfm.wasm
echo "EDITOR: ALL PASSED"

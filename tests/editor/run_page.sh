#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# The editor page in a headless Chrome (tests/editor/page_test.js, its scenarios in tests/editor/page/): the page built
# first (editor/build.py), then each scenario on dist/editor/editor.html?virtual; screenshots in build/editor/page/.
# Needs Chrome or Chromium (CHROME= to name it). From ptfm/:
#   sh tests/editor/run_page.sh [SCENARIO...]      (NO_WASM=1: the WebAssembly built last, only the page again)
set -e
cd "$(dirname "$0")/../.."
if [ -n "$NO_WASM" ]; then python3 -I editor/build.py --page; else python3 -I editor/build.py; fi
node tests/editor/page_test.js "$@"
echo "EDITOR PAGE: ALL PASSED"

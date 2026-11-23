#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
# The playground's tests, in Node (no browser): its behaviour, its sound, and every screen as a PNG
# in build/playground/ to look at; and the panel's MIDI (panel/midi.js: MIDI CONTROLLER's page side).
set -e
cd "$(dirname "$0")/.."
node tests/controller_page.js
node tests/playground/harness.js tests/playground/checks.js
node tests/playground/harness.js tests/playground/keys_only.js   # (no song at all: the keys' song)
node tests/playground/harness.js tests/playground/loops_cue.js   # (nothing playing: the keys' song held, LOOPS' loops; UNLOAD SONG)
node tests/playground/harness.js tests/playground/loops.js       # (LOOPS, the loop station: takes, QUANTIZE, lanes, CONTROL, the pattern recorder ...)
node tests/playground/harness.js tests/playground/loops_metro.js # (PLAY's LEDs while the loops play: the visual metronome)
node tests/playground/harness.js tests/playground/editor_own.js  # (the EDITOR's own pattern; FWS holds nothing of the user's)
node tests/playground/harness.js tests/playground/editor_takes.js # (the EDITOR's takes: the keys heard at once, written on the line)
node tests/playground/harness.js tests/playground/storage.js     # (the user area kept in the browser: clean, DEMO DISK, SAVE, a power cycle)
node tests/playground/harness.js tests/playground/refs_save.js   # (UPDATE REFERENCES?: YES pauses and saves as SAVE does)
node tests/playground/harness.js tests/playground/records.js     # (the list of records: a save past 560 refused, deletions first)
node tests/playground/harness.js tests/playground/revert_fx.js   # (REVERT ALL: the paused song's effects as saved at once)
node tests/playground/harness.js tests/playground/cut_fx.js      # (CUT held 2 s: every effect off, an edit)
node tests/playground/harness.js tests/playground/save_cut.js    # (a save in silence: CUT first, back at its end)
node tests/playground/harness.js tests/playground/loops_lfo.js   # (LFO held, stopped: the eraser and the keys' insert)
node tests/playground/harness.js tests/playground/favorites.js   # (FAVORITES, FB alone: ADD / REMOVE FROM FAVORITES, SEL + GLO, browsing, a favourite's edit mode ...)
node tests/playground/harness.js tests/playground/samples.js     # (samples numbered on their own: THE SAMPLES list, REPLACE SAMPLE, CREATE, the tools ...)
node tests/playground/harness.js tests/playground/lists_s.js     # (menus, properties, the MIDI page and the cartridge list in FONT_S: rows of one line or two)
node tests/playground/harness.js tests/playground/automations.js # (AUTOMATIONS: recording, playing, the menu, SAVE; fw/tests/ui3_test.c's steps, its shots' twins)
node tests/playground/harness.js tests/playground/velocity.js    # (the keys' velocity's twins: the map, the panel's press point, the top row, CONSTANT, MIDI CONTROLLER's aftertouch)
node tests/playground/sound.js
node tests/playground/ghosts.js                                    # (the pool's ghosts: the firmware's cases on the worklet's twin)
node tests/playground/forward.js                                   # (each engine's forward work against its own steps)
node tests/playground/harness.js tests/playground/screens.js > /dev/null
node tests/playground/harness.js tests/playground/mirror_ui.js > /dev/null   # (fw/tests/ui_test.c's steps: its shots' counterparts)
echo "screens: build/playground/*.png"
python3 -I tools/build_playground.py --check || true   # (the one-file copy in dist/: a note, not a failure)

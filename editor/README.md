<!-- SPDX-License-Identifier: GPL-3.0-only
     Copyright (C) 2026 Vasily Kazantsev -->
# PT/FM's editor, EDITOR [beta]

A page for the computer that adds content to the FM-1 and backs it up: **TABLE**, everything dropped on the page, and
**FM-1**, an image of what an FM-1 holds, side by side; items moved between them with everything they use; READ and
WRITE take the FM-1 panel to and from the device; every item edited in a tab of its own, with the FM-1's own pictures;
a repeating preview through the firmware's sound code. Its spec: [`../specs/editor.md`](../specs/editor.md).

## Using it

- **Build:** `python3 -I editor/build.py` (from `ptfm/`): the firmware compiled for the browser with the editor's part
  (`emulator/build.py --extra editor/wasm --define PTFM_EDITOR`, into `build/editor/wasm/`), then
  `dist/editor/editor.html`, one file with everything in it. `--page` writes the page again on the WebAssembly built
  last. Needs clang and lld with the wasm32 target, as the emulator does.
- **Open** `dist/editor/editor.html` in Chrome or Edge, from the file system. `editor/index.html` opened as it is loads
  its scripts from beside it and the WebAssembly from `build/editor/ed_wasm.js` (made by the build), for working on it.
- **READ / WRITE** find the FM-1 among the computer's MIDI ports each time and let it go after. Opened with `?virtual`
  after the file's name, the page's own firmware stands in for the FM-1 (its flash in this page only): the tests' way.
- **Both panels** are kept in this browser (IndexedDB) and survive a reload; one page at a time may change them.

## Files

| File | What |
|---|---|
| `index.html`, `editor.css` | the page and its look: the header with the tabs, the work area, the footer with the mini player and the status line; no sidebar; one panel at a time under 860 px |
| `js/model.js` | the panels' items and their rules, no DOM: items from records and back, references (patched in place, so what does not change keeps its bytes), content hashes, sizes, moves (reuse by hash, numbers kept or given, loose samples into a sampler, the system items replaced, changed built-ins), DELETE and ALSO WHAT ONLY IT USES, the space as the uploader's planner places it, what WRITE would change, UNDO |
| `js/files.js` | FILES: both panels (groups, DETAILS / LIST / ICONS, filter, sorting, picking, keys, the menu, the selection bar, renaming, dragging, the space bar, the FM-1 panel's header, its built-ins and defaults) |
| `js/fm1.js` | READ, WRITE (the plan, CHANGED, the dialog), UPLOAD DISK, DOWNLOAD DISK, REVERT, over `../fw/web/seed.js` |
| `js/drops.js` | files dropped on the TABLE: disks, MOD / S3M / XM, MIDI files, DX7 `.syx`, sounds |
| `js/player.js` | the preview: the firmware in an AudioWorklet, each kind's flash image, the repeat, the mini player |
| `js/pics.js` | the FM-1's pictures, drawn by the firmware (`wasm/ed_pics.c`) from an item's bytes |
| `js/tabs.js` | the tabs' frame (draft, SAVE, CLOSE, its own UNDO) and their controls (sections with the pictures beside their controls); `js/tab_pattern.js`, `tab_song.js` (with the FX setup), `tab_sample.js`, `tab_cart.js` (every engine, the keyboard), `tab_system.js` (SETTINGS, MIDI MAP, LOOPS, DJ SETUP) |
| `js/ui.js`, `js/keep.js`, `js/main.js` | the page's state, status line, dialogs, menus and icons; kept in the browser, one page at a time; the start |
| `js/records.js` | every record both ways, byte for byte (`tools/ptfm_records.py`'s twin: `tests/editor` keeps them equal); older versions converted wherever records come in (the loops' version 3 to 4) |
| `js/defs.js`, `js/cmds.js`, `js/util.js` | what is shown and edited, as tables (the FX's knobs, the settings, the engines' parameters, the MIDI map's controls); the commands as the grid shows and lists them; helpers |
| `js/optimize.js` | samples made smaller, shared by the SAMPLE tab's OPTIMIZE and the TABLE's OPTIMIZE SAMPLES: a sample's source, its instruments' use of it, the levels, a new cut put in a panel with its sampler following |
| `js/conv_mod.js`, `js/ibxm_load.js`, `js/conv_midi.js`, `js/conv_syx.js`, `js/conv_sound.js` | the converters: modules read as IBXM's loaders read them, MIDI files, DX7 bulk and single-voice dumps, sounds |
| `js/flash.js` | a flash image of records as a clean seeding lays them; `ED.Firmware`, one instance of the firmware on the page's thread (pictures, AUTO LEVEL, the tests' virtual FM-1) |
| `wasm/ed_glue.c`, `wasm/ed_pics.c` | the page's part of the firmware's WebAssembly: mounting an image, the edit memory, playing, AUTO LEVEL, the built-ins; the pictures (compiled with `PTFM_EDITOR`, which only this build defines) |
| `build.py` | the build |

## Tests

`sh tests/editor/run_tests.sh [MODULE...]` (from `ptfm/`; it builds first): the records against `tools/ptfm_records.py`,
the loops' record, the panels' rules (`model_test.js`), READ and WRITE against the virtual FM-1 (`device_test.js`), the
preview's worklet playing each kind (`preview_test.js`), the converters (`conv_test.js`, modules given as arguments),
the pictures against the firmware's own screens (`pictures_test.js`).

`sh tests/editor/run_page.sh [SCENARIO...]` (from `ptfm/`; it builds first; needs Chrome or Chromium, `CHROME=` to
name it): the page itself in a headless Chrome (`page_test.js`, through its DevTools protocol with Node's own fetch and
WebSocket), on `dist/editor/editor.html?virtual`, a scenario a file in `tests/editor/page/`: `files` (drops, a move,
the views, WRITE and READ, each kind's tab), `dialogs` (DELETE with what uses it, ALSO WHAT ONLY IT USES, REPLACE,
UNDO), `tabs` (each kind edited and saved, the FX chain dragged), `pictures`, `keys` (the side list, MIDI, the keyboard's
input kept, FILES' group headers), `new` (+ NEW), `backup` (WRITE's BACKUP), `down` (WRITE converting down), `optimize`
(a sample's OPTIMIZE, its instruments following on SAVE), `narrow` (a phone's width). Screenshots to look at in `build/editor/page/`. Run it before handing a build over.

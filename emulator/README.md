<!-- SPDX-License-Identifier: GPL-3.0-only
     Copyright (C) 2026 Vasily Kazantsev -->
# PT/FM's emulator

PT/FM's firmware in a browser (`../specs/emulator.md`): the virtual FM-1 of `../panel/`, the same page as
the playground's, but everything the screen shows, the LEDs light and the speakers play comes from the
firmware itself, `../fw/core` compiled to WebAssembly. No CPU is emulated: the same C is compiled a second
time, with `plat_web.c` in place of the device's hardware layer.

## Using it

- **Build:** `python3 -I emulator/build.py` (from `ptfm/`) makes a dev build, `build/fw/web/ptfm.wasm.js`;
  `--release` makes `dist/emulator/ptfm.wasm.js`. Beside it, `demo.ptfmdsk.js`: the demo disk for the UPLOAD DEMO DISK button,
  and `emulator.html`: the whole emulator in one file, the firmware and the demo disk inside, to pass on
  (`--page` writes only it, from what `dist/emulator/` holds).
  `--debug --out DIR` builds the debug build's figures and state in (a dev build only): what
  `fw/tests/run_autotest.sh` runs the debug page's autotests against.
  Needs clang and lld with the wasm32 target.
- **Open** `emulator/index.html` in Chrome or Edge, from the file system. It loads `../dist/emulator/ptfm.wasm.js`,
  else the dev build; `index.html?dev` tries the dev build first; with neither, it asks for a file.
- **The first click or key switches the FM-1 on** (browsers allow sound only after one, and the sound runs
  the device's time).
- **UPLOAD CUSTOM DISK** (or a disk dropped on the page) writes a disk (`.ptfmdsk`) into the emulated FM-1, **UPLOAD DEMO DISK** the demo disk, as the install page
  seeds the real one (`../fw/web/seed.js`): the device reports its layout, the plan is shown, WRITE writes.
- **The flash is kept in this browser** (IndexedDB), so what was seeded and saved is there after a reload.
  `emu.eraseFlash()` in the console starts again from a new FM-1.
- **DOWNLOAD DISK** saves what the emulated FM-1 holds in its user area as a disk (`.ptfmdsk`): its live records,
  asked of the firmware (`plat_web.c`'s `web_disk`), read from its flash and checked. Changes not saved are not in
  the flash: a box says so first (SAVE on the FM-1, or DOWNLOAD ANYWAY).
- **RECORD** (ITS USB AUDIO) keeps the emulated FM-1's USB audio, as a computer records a real one's
  (`../specs/connections.md`): `plat_web.c` makes it as the device's USB audio does, only while recording; STOP,
  then SAVE WAV (16-bit stereo, 44.1 kHz). A pause of the sound pauses it: no gaps.
- **MIDI:** the real FM-1 as the controller (MIDI CONTROLLER) in mirror mode works the emulator, as it works the
  playground (`../panel/midi.js`).
- **IN / OUT:** the emulated FM-1's own USB MIDI (`../specs/connections.md`): a keyboard or a sequencer picked
  as IN plays as its keys and drives its clock; OUT gets what its MIDI OUT sends (SETTINGS' MIDI page says
  what). The ports picked are remembered in this browser. MIDI is asked for when a list is first opened (or at
  once where the browser allows it already), never at load.

## Files

| File | What |
|---|---|
| `plat_web.c` | `../fw/core/plat.h` for the browser: the device's time from the audio (a half of 256 frames, a main-loop pass a millisecond when free; the screen's transfers and the flash's erases and programs keep the main loop busy as on the FM-1), the 1 MiB flash, the screen, LEDs, input, SysEx, USB audio while recording, the user area's records; what the page calls |
| `libc.c`, `include/string.h` | the few C library functions the app uses (WebAssembly is built without one) |
| `worklet.js` | the AudioWorklet that runs the firmware: audio out (resampled when the sound card is not at 44.1 kHz), the screen's changed rows, LEDs, SysEx, the flash, USB audio's frames and the records to the page |
| `index.html`, `main.js`, `emulator.css` | the page: the panel, the firmware's loading, switching on, input, the screen, seeding, the kept flash; its frame (the sidebar, ZOOM, MIDI, the status line) is the playground's, `../panel/page.js` and `page.css` |
| `build.py` | the build |

Tests: `../fw/tests/run_web.sh` seeds the demo disk through `seed.js` into the WebAssembly, then runs one
session on it and on the same C built natively: the audio, the screens and the LEDs must be equal.

<!-- SPDX-License-Identifier: GPL-3.0-only
     Copyright (C) 2026 Vasily Kazantsev -->
# PT/FM's firmware

The tracker itself, for the M-VAVE FM-1: what `../specs/` describes, built for the device and, from the
same sources, for the emulator (`../emulator/`). Engine only: no data but the built-in instruments
(`../specs/files/instruments.md#built-in-instruments`); songs, patterns and samples come as a disk
(`../dist/firmware/demo.ptfmdsk`) uploaded by `../dist/firmware/install.html`.

## Folders

| Path | What |
|---|---|
| `core/` | the app, shared by the device, the emulator and the host tests; plain portable C, integer only |
| `core/plat.h` | everything the app needs from the hardware; nothing in `core/` touches anything else |
| `core/app.h`, `app.c` | the entry points a platform calls: `app_init`, `app_poll`, `app_audio`, `app_sysex` |
| `core/store/` | the store (`../specs/files/store.md`): the scan, the map, reading records, saving, compaction; seeding's device side (`../specs/files/blob.md`) |
| `core/play/` | the player: the replay of songs and patterns tick by tick (`../specs/files/patterns.md`, `commands.md`, `songs.md`), the voice pool, the mix |
| `core/eng/` | the engines: DX7, Juno-106, TB-303, the sampler (PTR audio) |
| `core/fx/` | the effects, the sends, the punch-ins, the METRONOME (`../specs/ui/screens/fx.md`) |
| `core/ui/` | drawing, the fonts, the frame, the controls' grammar, every screen (`../specs/ui/`) |
| `core/midi/` | MIDI in and out (`../specs/connections.md`): notes as the keys, MIDI learn and its record (`../specs/files/midi.md`), MIDI OUT, the clock in and out; USB and the MIDI IN jack alike, through `plat_midi_rx` / `plat_midi_tx` |
| `device/` | the FM-1's side: boot, interrupts, audio, input, LEDs, the LCD, flash, USB-MIDI, the update protocol, the USB rescue |
| `hal/` | the hardware layer (header-only: registers, the panel's scan, audio, LCD, flash, USB) |
| `loader/` | the update loader the installers know, byte for byte |
| `tools/` | the build (`build.py`), the packager, the install page's maker, the generators |
| `web/` | the install page's template and its scripts (the installer's, seeding's) |
| `tests/` | host tests: `run_tests.sh` runs every `run_*.sh` beside it |

Generated sources (fonts, tables, the built-in instruments) go to `../build/fw/gen/`, never into the tree.

## The entry points

- **`app_init()`** once at power-on: `store_init()` (mount), `player_init()`, `ui_init()`.
- **`app_poll()`** a pass of the main loop: `seed_poll()`, `store_poll()` (not while seeding), `ui_poll()`.
  Never waits long; no endless loops in `core/`.
- **`app_audio(lr, 256)`** every half, from the audio context (on the FM-1 an interrupt): the player fills
  interleaved stereo int16; the platform applies MASTER and the speaker's mono. Silent while seeding.
- **`app_sysex(msg, n)`** a PT/FM SysEx (`F0 7D 50 54 ...`): seeding's commands.
- **The main loop only asks; the audio context acts:** the UI posts requests to the player (play, stop,
  a note), the player applies them at the start of its next half. Shared state is written by one side.

Each part keeps its own header with its entry points: `store/store.h` (`store_init`, `store_poll`, the
record readers), `store/seed.h` (`seed_poll`, `seed_active`, `seed_sysex`), `play/player.h` (`player_init`,
`player_audio`, the requests), `fx/fx.h`, `ui/ui.h` (`ui_init`, `ui_poll`).

## Rules

- **Integer arithmetic only** in `core/` (the FM-1's code runs without floating point); 64-bit multiplies
  are cheap there, 64-bit divides and any float are not.
- **No undefined behaviour:** check DSP code with `-fsanitize=signed-integer-overflow,undefined`; a signed
  overflow comes out differently from gcc and clang, and so does the audio.
- **No weak function outside `core/plat_weak.c`** (the build refuses one): the FM-1's compiler, seeing a
  weak body in the unit that calls it, keeps values across the call in registers the function linked in its
  place overwrites. With the store's lock defaults in `store.c`, every edit on the FM-1 was filed under a
  wrong record.
- **RAM:** the data region, 449,536 B: the pool, then the variables, then the spare zone (its own 24 KB at least,
  and the spare the build keeps); core 2's room, 40 KB, above it; the budget is `../specs/files/ram.md`'s, and
  every device build prints what each object takes.
- **Every file starts with its licence lines;** third-party ports keep theirs (MSFA Apache-2.0, Open303
  MIT, IBXM BSD-3-Clause; the Juno-106 follows KR-106, GPL-3.0).

## The sound (`core/play/`, `core/eng/`, `core/fx/`)

- **`play/player.h`** is all the UI needs: the transport's requests, the keys, mutes, the voice count, the FX
  screen's knobs, a source of its own for LOOPS (`player_play_source`), and what the screens read (a snapshot
  a half, the voices, the scopes, the costs).
- **`play/rec.c`** reads the records (`../specs/files/`): songs, a cursor a pattern track, cartridges and their
  slots, names. **`play/ibxm/`** is IBXM's replay, extended for PT/FM's cells (up to four commands each, E3x,
  E5x, PANBRELLO, Kxx at its tick), without audio: the player's engines sound its notes.
- **`eng/`**: the DX7 (MSFA in C), the Juno-106 (after KR-106), the TB-303 (Open303 in integer), the sampler
  (PTR decoded as it plays: 8 and 16 bits, START / END, sustain loops forward and ping-pong); each at its
  reference sound's -20 dB (`../specs/files/instruments.md`), a voice entering the mix at half.
- **`fx/`**: the chain (GATE, FILTER, DRIVE, LO-FI, CHORUS, DELAY, REVERB, LIMITER, in the setup's order, the
  sends in SENDS routing), the punch-ins from a 0.5 s buffer, the METRONOME; the REVERB (Dattorro's plate)
  and the DELAY at 29.4 kHz through 2:3 converters; a soft ceiling at the output.
- **Tables** come from `tools/gen_tables.py` (`gen_dx7.py`, `gen_fx.py`); the Juno-106's and the TB-303's,
  made once from KR-106 and Open303, are kept in the tree.
- **`tests/run_sound.sh`**: PTR against the packer's decoding; each engine's reference level; every demo
  song's notes against the playground's replay and its loudness against the playground's; the transport
  and the keys; E3x; gcc and clang giving the same samples. The FM-1's costs are to be measured there.

## Building for the FM-1

- **What it needs,** none of it in the repository:
  - **`JIELI_TOOLCHAIN`:** JieLi's Linux toolchain for the pi32v2 core (the directory with `pi32v2/` and
    `common/`; default `~/.jieli/toolchain`);
  - **`AC79_SDK`:** a checkout of JieLi's AC79 SDK, `AC79NN_SDK_V1.2.1_2023-12-13` (default
    `~/fw-AC79_AIoT_SDK`); three of its files go into the package, checked by SHA-256;
  - **`JIELI_DOCKER`:** 1 (the default) runs the compiler in an offline `debian:bookworm-slim` container; 0
    runs it natively (Linux x86-64).
- **`fw/build.sh`** (the three above):
  - a dev build by default: `../build/fw/ptfm.fwsc` and `../build/fw/install.html`, the version
    `fw/VERSION`'s number (set by hand) with ` DEV` after it;
  - `--release`: `../dist/firmware/ptfm.fwsc` and `../dist/firmware/install.html`, the version that number
    alone;
  - `--debug`: the debug build, `../dist/debug/ptfm-debug.fwsc` and `../dist/debug/install.html`: the
    same firmware with its figures, its bench and their page (`../specs/debug.md`; `core/dbg.c`,
    `core/bench.c`, `device/bench_core.c`), its own build time with ` DEBUG`;
  - `--bringup`: the device layer alone with `device/bringup.c` as the app (the panel on the screen, a
    tone a key, the LEDs following), for trying the hardware side; never released.
- **The package:** identity `FM-1_7101`; on USB the FM-1 is "FM-1 PT/FM". The update loader is the one the
  installers know ("ota-FM-1_900"), byte for byte (the build checks its SHA-256), so the update path and the
  USB rescue work as ever: hold OCT- alone at power-on, or two failed boots in a row, for USB RESCUE.
- **What the build checks:** the image fits the slot (581,564 B; what is left of it is FWS's free part, the firmware's room to grow);
  the variables and the pool fit the data region with the spare zone's floor and 8 KB spare (the build prints them by object, also when the
  link fails); RAM code makes no calls; registers only in `hal/` (and `core/` includes nothing of `hal/`
  or `device/`: `plat.h` only); no 64-bit divide in the sound's code (`device/main.c` has the compiler's
  helpers for the rest, slow, and the build lists who calls them); core 2's room (`device/app.ld`,
  `../specs/second-core.md#ram`) placed as the chip needs it, its code calling only code in it and naming no
  flash address; no instruction pair in the image writing one register twice; the stacks' worst cases, the
  interrupts' and core 2's apart, worked out from the code (`tools/stackdepth.py`).
- **How it compiles:** `core/eng/`, `core/play/` and `core/fx/` at `-O2`, the rest `-Os`; `PLAT_POOL` puts a
  buffer into the pool; `device/include/string.h` gives `core/` the few string routines a freestanding
  build has (`device/main.c` defines them); the generated sources get every `core/` header directory.
- **The install page** (`web/install.html`, made by `tools/make_installer.py`) carries the package and
  `../dist/firmware/demo.ptfmdsk`: INSTALL FIRMWARE (the FM-1's update path), UPLOAD DEMO DISK and UPLOAD CUSTOM DISK (a
  disk file picked) (seeding through `web/seed.js`: the device's layout, planned and written in one press).
- **On the FM-1** (`device/main.c`): TIMER5 at 10 kHz scans the panel and keeps the clocks, USB-MIDI at
  2 kHz, the LEDs' levels at 1 kHz (lit from 96, else the dim glow); ALNK0 asks for each half and
  `app_audio` fills it; `device/out.h` then applies MASTER (squared), LINE GAIN, mono and the soft ceiling
  (a full 16-bit sample at MASTER full and 0 dB is -6 dBFS), `app_audio` writing its 16-bit
  samples into the free half itself. Erases and programs blank the sound while the flash is away; the user
  area reads in place (the plain window).
- **MIDI on the FM-1:** USB-MIDI and the jack (`device/midi_uart.c`, cable 1) land in one ring that TIMER5
  empties at once into `plat_midi_rx`'s, stamped with the time they came, so MIDI never holds back SysEx (an
  update's included); out, the main loop and the audio context each have a ring that TIMER5 hands to USB.
  A MIDI knob learned onto MASTER rules until the pot itself moves.
- **RAM the device side takes** (`main`, ~11 KB): the audio's two DMA halves (4 KB), USB's rings and the
  SysEx frame, the MIDI rings (~1 KB), and one LCD buffer (3.75 KB) that the app's rows, the firmware's own
  screens, a received SysEx and a flash page take turns in. The update path's buffers (2.6 KB) are the
  store's scratch, claimed only while an update message is handled.

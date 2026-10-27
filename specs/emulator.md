# The emulator

Status: **settled in outline** (2026-10-07).

In short:
- code and web files; it loads the built firmware (or offers a file picker when it is not found), and
  has a button to load a data file into the emulator;
- the built emulator is committed too: the sources, the web loader and the compiled WebAssembly.

## What it is

- **PT/FM's firmware running in a browser,** on the panel, the virtual FM-1's controls (`panel/`,
  [panel.md](panel.md)): one big screen and the controls placed for ease of use, a WIDE view for a
  computer, a PORTRAIT one for a phone held upright and a SIDEWAYS one for a phone held sideways.
- **Its page is the playground's** ([panel.md](panel.md#the-page)), with three groups more in its sidebar:
  DISKS (SEED, DEMO DISK), ITS USB MIDI (the emulated FM-1's MIDI IN and OUT ports) and FIRMWARE (which file
  runs, its version, also in the header); POWER says whether it is switched on, or that its firmware stopped. Its
  PT/FM logos (the sidebar's, the header's on a narrow page) lead to PT/FM's page, https://hitosu.github.io/ptfm/.
- **Switching on** comes from a gesture the browser lets sound start from: a key, a click, a mouse's press, a
  finger lifted (a touch screen counts a tap only when the finger lifts: a sound made at the touch can stay
  silent, and its firmware with it, as its time is the sound's). Every such gesture asks again while the sound
  is not running. **POWER says ON only while the sound runs,** else `PAUSED: TAP TO GO ON`.
- **RESET** (POWER's group) switches the emulated FM-1 off and on again, as its power switch: its flash kept
  first. **ERASE DATA**, tapped twice (the first tap turns it to `TAP AGAIN`), erases what the page keeps in
  the browser (the flash, the page's settings) and switches on a new FM-1. The firmware's own restart
  (SETTINGS' FACTORY RESET, once it has erased the user area) is RESET's: the page switches it off and on, the
  flash kept.
- **Not a CPU emulator.** The firmware's C is compiled a second time, to WebAssembly. `emulator/`'s
  web hardware layer replaces `fw/hal` and the device's drivers. The device's `ptfm.fwsc` cannot run
  there.
- **The same app code** (`fw/core`) runs on both. The screen, LEDs and sound come from it unchanged.

## Files

- **`emulator/`** holds the sources: the web hardware layer and main (C), the page, and the audio
  worklet.
- **`dist/emulator/ptfm.wasm.js`** is the built part: the WebAssembly in base64, inside a classic script. A
  page opened from `file://` can't fetch a `.wasm` file, but it can load a script.
- **Loading:** the page loads `../dist/emulator/ptfm.wasm.js`. If that fails, it offers a file picker for a
  `.wasm.js` or `.wasm` file.
- **`dist/emulator/emulator.html`** is the emulator as one page, to pass on as a single file: `emulator/index.html`
  with every stylesheet and script written in, in order, `ptfm.wasm.js` and `demo.ptfmdsk.js` before `main.js`,
  which takes a firmware the page already carries. `emulator/build.py --release` writes it after the firmware;
  `--page` writes it alone from `dist/emulator/`'s scripts. The release build compiles the version in
  (`fw/VERSION`), so STATUS and ABOUT name it as the device's release does.
- **Toolchain:** clang and lld with the wasm32 target (`-ffreestanding -nostdlib`).

## Data

- **The Seed button** opens a PT/FM disk (the picker offers `.ptfmdsk` only) and seeds it into the emulated device,
  asking for MIDI nowhere. It uses the same
  module and protocol as `dist/firmware/seed.html` (the files proposal, 12.2:
  [proposals/202610080050-fs.md](proposals/202610080050-fs.md)), so seeding is tested without the
  FM-1.
- **The emulated flash is kept in the browser's storage,** so the library survives a reload, as on the
  device.

## Rules for the shared code

- **One source above the hardware.**
  - The main loop's pass is a function (`app_poll`).
  - The audio interrupt's work is a function (`app_audio`).
  - No endless loop or busy wait in shared code.
- **No undefined behaviour.** A signed overflow (in a polyBLEP) has been seen to come out differently
  from gcc and clang, and so did the audio.
  - Check new DSP code with `gcc -fsanitize=signed-integer-overflow`.
  - Compare a gcc build with a clang build of the same session.

## Time

- **The audio thread drives the device's time:** halves of 256 frames, as the device's audio
  interrupt. So nothing runs before the sound starts (the first gesture, [above](#what-it-is)): the screen
  stays dark until then.
- **The main loop** runs once per device millisecond whenever it is free.
- **Screen transfers keep it busy** as long as they would on the FM-1 (60 MHz SPI: a full frame is
  30.2 ms).
- **CPU figures are the PC's,** which is good enough. The worklet has only a 1 ms clock, so maxima
  move in whole milliseconds.
- **One core:** each block's job for core 2 runs at once where it is handed over, in the device's order, and its
  time counts as core 2's figure ([second-core.md](second-core.md#the-emulator-and-the-playground)); the second
  core never stops there.

## Testing

The WebAssembly build and the same C built natively run one scripted session, with a fake clock.
Their audio, screens and LEDs must be equal at every snapshot.

# PT/FM: main context

**PT/FM** (Pocket Tracker / Fusion Machine) is a tracker firmware for the **M-VAVE FM-1** synth: a
tracker in the ProTracker tradition whose tracks play synth engines and samples side by side.

This file is the main context for anyone working here, people and AI agents alike. Keep it current
and short: the state and how to work, not a history.

**This repository is public.** Nothing in it (specs, proposals, code, comments, this file) refers to
work or folders outside it. What was learned elsewhere is stated as plain fact ("measured on the
FM-1").

## State

- **The firmware is built and runs on the FM-1** (`fw/`, its README first), but its sound costs too much
  there (4 voices at 60-80 % CPU): measuring and optimising it is the work now. One core
  (`fw/core/`: the store and seeding, the player with its engines and effects, MIDI, every screen of the UI)
  shared by the device (`fw/device/`, `fw/hal/`, `fw/loader/`) and the emulator (`emulator/`); built three
  ways (`specs/layout.md`): dev (`build/fw/`), debug (`fw/build.sh --debug`: `dist/debug/`, the figures and
  their page, `specs/debug.md`, whose autotests play any build through MIRROR and the state query,
  `specs/connections.md`) and production (`fw/build.sh --release`: `dist/firmware/ptfm.fwsc`,
  `dist/firmware/install.html` with INSTALL FIRMWARE (the firmware alone; CLEAN INSTALL also empties the user
  area, as SETTINGS' FACTORY RESET does on the FM-1 alone), UPLOAD DEMO DISK and UPLOAD CUSTOM DISK (a disk file); copies of the library are the editor page's: READ, then DOWNLOAD DISK); `dist/firmware/demo.ptfmdsk` made by `tools/pack.py`. Host tests: `fw/tests/run_tests.sh` (every `run_*.sh`). The RAM
  is one 439 KB data region (with core 2's 40 KB room above it) and nearly full: every part keeps to `specs/files/ram.md`, and the build prints each
  object's share. The playground stays the UI's fast reference, and the firmware follows it.
- **The emulator is built** (`emulator/`, `dist/emulator/ptfm.wasm.js`): the playground's page and panel, with the
  screen, LEDs and sound all from the compiled firmware, the flash kept in the browser; UPLOAD CUSTOM DISK and UPLOAD DEMO DISK
  seed through `fw/web/seed.js`, as the install page does; DOWNLOAD DISK saves the user area as a disk (warning of
  changes not saved); RECORD keeps its USB audio, as a computer records a real FM-1's, for SAVE WAV.
- **The editor page is built** (EDITOR [beta]: `specs/editor.md`, `editor/README.md`, `dist/editor/editor.html`):
  TABLE and FM-1 side by side, items moved with what they use, READ and WRITE over the seeding protocol, UPLOAD and
  DOWNLOAD DISK, a tab for every item with the FM-1's own pictures (`fw/core/ui`'s drawing, built in under
  `PTFM_EDITOR` only), the preview through the firmware in a worklet. Tested against the virtual FM-1; not yet with
  an FM-1.
- **The controller is part of the firmware** (`specs/controller.md`): SETTINGS' MIDI page, MIDI CONTROLLER, ends
  the tracker (till a power cycle) and makes the FM-1 a USB-MIDI controller with a MIDI monitor on its screen.
  Its start asks for the mode (REC mirror, HOME general; SELECT the channel, DEFAULT 16 / 1); mirror mode
  (channel 16) works the virtual FM-1 through `panel/midi.js`, the page driving the FM-1's LEDs; general mode
  (channel 1) plays synths and DAWs; nothing is saved and it never writes to the flash; the handshake's answer
  carries the channel (protocol 2). The protocol is `fw/core/ctl.h` (tested by `fw/tests/run_ctl.sh` against
  `fw/tests/vectors.json`, the table `panel/midi.js` is tested against too), its platform `fw/core/ctlmode.c`.
  It first ran as a firmware of its own, whose mirror mode worked with the playground on the FM-1; that
  firmware is gone.
- **Every proposal is settled and its specs lead** (2026-10-09; the proposals stay as the record):
  - **the UI:** `specs/ui/` (controls, navigation, dialogs, LEDs, the frame, colours, the transport,
    saving, the help) and `specs/ui/screens/` (each screen: its controls, its layout in text, every
    text, its help pages), `specs/playground.md`;
  - **files and formats:** `specs/files/` (the store: a log of records in the flash, no directory;
    every record's bytes: patterns with up to four commands a cell, commands, songs, the loops,
    cartridges, PTR audio, settings; references, saving and RAM, the blob and seeding, disks); the
    player extends IBXM's code, and upstream IBXM is the test of every conversion;
  - the repository's layout (`specs/layout.md`), the emulator (`specs/emulator.md`, settled in
    outline), the controller (`specs/controller.md`).
- **LOOPS** (a loop station, `specs/proposals/202610240236-loops-station.md`: up to 16 loops of one length, each
  one instrument recorded in one pass, POLY up to 4 lanes or MONO, its notes once plus a repeat count; takes heard
  exactly as written by QUANTIZE; CONTROL mode on the keys for mutes, solos and QUANTIZE; a 4-row pattern recorder;
  RECORD SONG; SAVE TO / LOAD FROM PATTERN): `specs/ui/screens/loops.md` and `specs/files/loops.md` (record version
  4, kept in the edit memory as pieces, no RAM of its own); built in the playground (`scr-loops.js`) and the
  firmware; LOOPS is a screen of the round, and the loops play on outside it.
- **DJ** (HOME + OP5; FX has its own button and no black key; for performing only, nothing written into patterns): two songs on two
  decks, the main always on the left (SEQ swaps them), one song list with a LOAD key a deck, tempo as a
  share of the song's and SYNC to the main, a crossfader (ALGORITHM), a DJ FILTER a deck, the pads (PLAY and
  LOOPS on the blank key, the LFO pads on LFO), HOLD as rows by hand, PREVIEW (the jack a split cue, USB audio the mix); one
  pool of voices, the deck lower in the mix giving way. Specified (`specs/ui/screens/dj.md`,
  `specs/files/dj.md`), from `specs/proposals/202610101408-dj.md`; built in the playground (`scr-dj.js`, the
  decks in `player.js`, DJ's mix in the worklet).
- **Specs are written once a proposal is settled;** until then a subject lives only in its proposal.
  Once written, the specs lead: a change goes into the spec (and the playground), not the proposal.
- **The playground is built** (`playground/`, on the virtual FM-1 in `panel/`): every screen of the
  UI spec, its controls, the help, the transport, and sound from engines written in plain JS; it
  plays the commands tick by tick, after IBXM's rules. Its content is all of the demo disk
  (`demo-disk/`). Tests: `tests/run_playground.sh`.
- **Disks** (`specs/files/disk.md`): a disk is a plain file storing a set of PT/FM's files (the
  records, each with its type and the type's version), to be passed on; the web editor opens and saves
  disks and sends updates to the device.
- **The demo disk** (`demo-disk/`, its README): 17 cartridges to start playing right away: 8 themed DX7
  cartridges (our voices and free banks' voices, each credited in `demo-disk/CREDITS.md`, the Mega Drive and
  Commander X16 patch sets among them, converted from 4 operators), JUNO-106 (128 of our own Juno-106
  patches), CHIPTUNE (128 instruments of the CHIP engine), TB-303 (64), five sampler cartridges (an acoustic kit, percussion, band and
  orchestra, voices, dubstep and drum and bass basses) and FUSION (6, cartridge `06`, its patterns `E0`-`E5`;
  DRUM KIT among them: the synth cartridges' drums and the samplers' on the keys); every instrument with its LEVEL,
  every work from elsewhere with its own licence and source. Six songs, each showing a part of PT/FM
  (PUNCH IN the effects, SPICE ROAD the samplers, MIDNIGHT RUN the Juno-106, CITY LIGHTS the DX7, ACID
  TEST the TB-303, ADVENTURE the CHIP engine), then twelve DJ loops for DJ's decks and LOOPS (TRANCE,
  BREAKS, TECHNO, ACID, DISCO, CHIP, HIP-HOP, DNB, ROCK, METAL, ELECTRO and WORLD LOOP: a pattern that
  loops, DNB's, ROCK's, METAL's and CHIP's a verse and a bridge; six after an intro played once; each track one
  instrument, so LOOPS loads a track as a loop; 4 voices at most; tracks 1-4 the kick, hats, bass, hook in
  all twelve; all in A minor but WORLD LOOP's flute, nine at 120-128 BPM, TRANCE LOOP (psytrance) at 140,
  HIP-HOP LOOP at 86 and DNB LOOP at 172 for SYNC's half and double); no LOOPS record. **Its synth instruments are built into the firmware** (settled 2026-10-10,
  `specs/proposals/202610102045-builtin.md`: the DX7s, JUNO-106, CHIPTUNE and TB-303, 36 KB, in FWS, the
  firmware space); the songs, patterns, sampler cartridges, samples and FUSION are an uploaded disk in the user
  area. Everything in FWS (the firmware slot) is edited as anything else, its
  changes saved in the user area (a built-in's under its own number, found first; BACK TO BUILT-IN drops
  it); the device never writes FWS, whose free part is the firmware's room to grow
  (`specs/proposals/202610201630-fws-buffer.md`, with the EDITOR's own pattern `FE` and patterns of at most
  16 × 128).
  **The engines make what they can** (drums, chip sounds, pads, effects are patches); samples are
  natural instruments, real voices and the basses the engines cannot make, played near the notes they
  were recorded at (measured: libraries' file names can be an octave off), each at the lowest rate that
  loses nothing heard (measured), 16-bit masters kept at 8 bits. Made by `demo-disk/tools/` from the
  sources given as arguments (`check.py` checks it all and prints each song's most voices at once,
  every release counted to -60 dB; up to 16, 4 in a DJ loop). Riffs and arpeggios follow the chords on the key's scale
  (`demo-disk/tools/theory.py`).
  `tools/gen_playground.py` reads its notation (version 2) into `playground/content.js`: run it again
  when the disk changes. Instruments' LEVELs are measured against the engines' reference
  (`specs/files/instruments.md`): each engine is scaled so its plain reference sound comes out at
  −20 dB, by design and never to fit data.
- **Open questions:** all in `specs/open-questions.md`, which holds only what is open; a spec marks a
  part not yet settled *(proposed)*.

## Proposals and specs: how they are kept

- **Named** `specs/proposals/YYYYMMDDHHMM-<subject>.md`, listed in `specs/README.md`.
- **Written in one voice, as if in one go:** answers are woven into the text. No "decided" marks, no
  word about who chose what, no review history, no lists of conflicts or gaps.
- **Open questions** go in `specs/open-questions.md`, a proposal's too. Once one is answered, the answer
  goes into the spec (and the playground) and its item leaves the file: no "settled" records there.
- **Specs:** one subject a file, big subjects as folders (`ui/`, `ui/screens/`, `files/`); unnumbered
  headings, links with anchors between them; written in the same one voice. A screen is specified in
  text: for each state an ASCII mockup on the T grid (40 × 20 characters of 6 × 12 px), a table of its
  elements in pixels, its colours in words beside it (the coding itself is `ui/colours.md`'s), every
  text it shows, and its help pages. The playground's code must match them.
- **The review:** critical issues first, one question at a time, multiple choice with a
  recommendation. Mechanical inconsistencies with one sensible fix are fixed and reported, not asked.
- **The help's text** (each screen's spec, and `ui/help.md`) is part of the design: every line fits 40 characters
  and 17 lines a page, only the fonts' characters. It must read plainly to a newcomer (no guessing
  what a line means), then compress well; it never mentions keys kept for later.

## Layout

The layout in full, with what is committed: `specs/layout.md`.

| Path | What |
|---|---|
| `AGENTS.md` | this file |
| `README.md` | for users: what PT/FM is, the folders, the licence |
| `CHANGELOG.md` | what changes from version to version, an entry a change with its kind first (FEATURE, CHANGE, BUG, REMOVED): the top section, Next version, since the last release |
| `LICENSE` | GPL-3.0-only |
| `specs/` | the specifications, one subject per file; `specs/proposals/` the drafts |
| `fw/` | the firmware, engine only, no data but the built-in instruments (`fw/README.md`): `core/` (`store/`, `play/`, `eng/`, `fx/`, `midi/`, `ui/`, `plat.h`, `app.c`), `device/`, `hal/`, `loader/`, `tools/` (build, packager, install page, generators), `web/` (the install page's template, `seed.js`), `tests/` |
| `emulator/` | the firmware in a browser (`specs/emulator.md`, `emulator/README.md`): the web platform, the worklet, the page, `build.py` |
| `panel/` | the virtual FM-1, shared by `playground/` and `emulator/`; `page.css` / `page.js` the two pages' frame (sidebar, header, footer, the drawer on phones) |
| `editor/` | the editor, a page for the computer: EDITOR [beta] (`specs/editor.md`: TABLE and FM-1 side by side, items moved with what they use, READ / WRITE / UPLOAD DISK / DOWNLOAD DISK, an item's tab to edit it, the FM-1's own pictures from `fw/core/ui` built in under `PTFM_EDITOR`); `editor/build.py` makes `dist/editor/editor.html`; tests `tests/editor/run_tests.sh` (Node) and `tests/editor/run_page.sh` (the page in a headless Chrome) |
| `playground/` | the UI playground: the UI, the transport and the sound in plain JS; no build |
| `demo-disk/` | the demo disk's sources: patches, samples and songs under free licences (`demo-disk/README.md`, `CREDITS.md`) |
| `manual/` | the manual, PT/FM OWNER'S MANUAL (`specs/manual.md`): `src/` the chapters (HTML fragments in a fixed vocabulary), `web.css` + `manual.js` the page, `print.css` the PDF; `tools/build_manual.py` makes `dist/manual/manual.html` (`--pdf`: `ptfm-manual.pdf`, a headless Chrome), the screenshots by `fw/tests/manual_shots.c`, the panel by `tools/manual_panel.py`; tests `tests/manual/`, run by `fw/tests/run_manual.sh`. The manual and `CHANGELOG.md`'s released sections are the only documents that name a version |
| `site/` | the web site, hitosu.github.io/ptfm (`specs/site.md`): `index.html` the landing (`@VERSION@` filled in from `fw/VERSION`), `img/` its pictures (the 3D picture, a video's thumbnail), `screens/` real screens (`tools/shots.sh`; `screens/manual/` the manual's, by `tools/build_manual.py`), `fonts/` the FM-1's Terminus as webfonts (`tools/pixfont.py`), `tools/hero.html` + `tools/images.py` the 3D picture (`images.py video` the thumbnail); `site/assemble.sh OUT` puts the landing and the four pages together (nothing built), `.github/workflows/pages.yml` publishes it on every push to `main`. Its texts name no count that changes with the contents |
| `assets/fonts/` | Terminus (SIL OFL 1.1), from which the tools make the fonts; Iosevka Aile (the pages') and Archivo (the PDF manual's), both SIL OFL 1.1, cut once by `fw/tools/webfont.py` |
| `tools/` | the data packer (`pack.py`: `dist/firmware/demo.ptfmdsk`, the built-in instruments, `--check` against the playground; `ptfm_records.py`: every record's bytes both ways), `gen_fonts.py` (JS; C with `--c` for the device's own screens, `--fw` for the firmware's UI), `gen_playground.py`, `build_playground.py` (`dist/playground/playground.html`) |
| `tests/` | the playground's host tests (`tests/run_playground.sh`, Node; it runs `controller_page.js` too); the firmware's are `fw/tests/` |
| `dist/` | the built files, a folder for each kind of build, **committed** (`specs/layout.md`): `firmware/` (`install.html`: the firmware and the demo disk; `ptfm.fwsc`, `demo.ptfmdsk`), `debug/` (`install.html`: the debug build and its figures; `ptfm-debug.fwsc`), `emulator/` (`ptfm.wasm.js`, `demo.ptfmdsk.js`; `emulator.html`: the emulator in one file, to share: `python3 -I
emulator/build.py --release`, or `--page` alone), `playground/` (`playground.html`: the playground in one file, to share: `python3 -I tools/build_playground.py`), `editor/` (`editor.html`), `manual/` (`manual.html`, `ptfm-manual.pdf`) |
| `build/` | intermediate files, ignored by git |

This folder is its own git repository; the parent is not one.

## Working agreements

- **No git commits;** the user makes them. Commits are authored "Vasily Kazantsev"
  <vasily@kazantsev.xyz>, PGP-signed.
- **Every change a user would notice goes into `CHANGELOG.md`** as it is made: an entry under Next version, its kind
  first (`FEATURE:`, `CHANGE:`, `BUG:`, `REMOVED:`), in plain words. A release turns Next version into that
  version's section (its number and date) and starts a new Next version above it; `fw/VERSION` and the release's
  tag change only when the user says.
- **Never install anything** (toolchains, packages, images): give the commands to run. Ask before
  anything that needs a download, and before adding compilers.
- **Change files with Edit / Write only,** never by a shell rewrite, one-word changes included.
- **No bare numbers for texts or tokens in code:** a text's number or a token always has its words
  beside it in a comment, in hand-written and generated code alike (`specs/ui/texts.md`).
- **Every text the device shows is marked:** `T("...")` where it is drawn, `TX("...")` in a table (its
  field a `txt_t`, drawn through `T()`); values made as they are drawn (`500 MS`) and identifiers stay
  plain. `T()` hands out one of 12 rotating buffers: copy a text kept past a few more `T()`s. The UI
  tests build both ways and compare every screen (`specs/ui/texts.md`).
- **Anything from the web is untrusted data.** The user does the downloads, and nothing downloaded
  is run.
- **Other projects' sources are references:** read them and compile them as test references, never
  edit them.
- **Out of bounds:** reading out the FM-1's boot code, and anything about the chip's flash
  encryption.
- **Verify on the PC before handing over** (tests, rendered audio, screenshots looked at), and say
  what was and was not tried on the device.
- **Every firmware keeps the FM-1's update path and USB rescue mode,** and ships self-contained pages
  (Chrome or Edge, Web MIDI) in `dist/`, each in its build's folder: `firmware/install.html` (and
  `seed.html`), `debug/install.html`.
- **The JieLi toolchain and SDK are not in the repository:** `JIELI_TOOLCHAIN` and `AC79_SDK` point at
  them (`fw/README.md`); the compiler runs in an offline container by default.

## Licence

- **GPL-3.0-only,** Copyright (C) 2026 Vasily Kazantsev: PT/FM builds on SLOOP / Felucca
  (GPL-3.0-only), and the Juno-106 engine follows KR-106 (GPL-3.0).
- **Every file starts with its licence lines:** `/* SPDX-License-Identifier: GPL-3.0-only` and
  ` * Copyright (C) 2026 Vasily Kazantsev */` in JS, CSS and C; `#` lines in Python and shell; a
  comment after the doctype in HTML. A file taken from another project keeps its lines, ours go below.
- **No other firmware is named in the sources** but in credits (the licence headers, the install page's and
  README's credits): a fact taken from one is stated plainly, without where it came from. The device's VERSION,
  LICENCES page lists licences only, no credits.
- **Third-party parts keep their licences:** Terminus (OFL-1.1; `playground/fonts.js` is tagged so); Archivo
  (OFL-1.1, `assets/fonts/Archivo-LICENSE.txt`; inlined into the PDF manual);
  JieLi's 3 SDK files in every package (Apache-2.0, named on the install pages); later MSFA
  (Apache-2.0), Open303 (MIT), IBXM (BSD-3-Clause).
- **A release** carries every notice and the complete source.
- **Data:** the only data in the repository is `demo-disk/`, works whose licences let anyone pass them
  on (each file and sample carries its own, `demo-disk/CREDITS.md`). Nothing else is added
  without a licence that allows it. `playground/content.js` is made from it and says so in its header.

## The device

- **Everything known about the hardware, measured,** is `specs/hardware.md` (the chip, RAM and what restarts write,
  the caches and the cache RAM, the flash and what may touch it when, the second core, sound, screen, controls, USB,
  updates, the toolchain); keep it current when the FM-1 shows something new. The rest of this section is the short
  form.
- **Controls:**
  - 27 note keys, F3 to G5; 16 white, 11 black printed OP1 OP2 OP3, OP4 OP5, OP6 PIT GLO, MONO
    POLY, and one blank.
  - 14 buttons: OCT-, OCT+, FX, SEL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY/STOP, REC.
  - LEDs on every button and key (`fm1_led_key`: buttons 0-13, then 14 + note). An LED lights the
    printed symbols, not the silicone. PLAY has two LEDs, red (id 12) and green (the matrix's column 8,
    bit 1); REC's is red.
  - 7 endless knobs (SELECT, ALGORITHM, K1-K4, PRESETS): deltas only, 24 clicks a turn, no push.
  - MASTER, an analog pot (0-1023).
- **Screen:** 240x240 RGB565 (ST7789), ~30 ms a full frame at 60 MHz SPI, no tearing line: redraw
  only what changed.
- **Sound:** 44.1 kHz stereo, 24-bit codec; the internal speaker needs mono; halves of 256 frames
  (5.8 ms).
- **Chip:** JieLi AC791N, two cores at 360 MHz (the second runs the effects: below), no floating point in our
  code; 512 KiB RAM (the data region 439 KB: a 342 KB pool, ~49 KB of variables, the spare zone; core 2's 40 KB
  room); 1 MiB flash (the firmware slot, 581,564 B, called FWS on the screens, written only by
  installs; the user area, 412 KB of runtime data read in place). Erasing flash stalls audio.
- **Code cache:** the firmware runs from flash through a cache; a 64-B line missed costs ~465 cycles
  (~1.3 us), and an audio half comes to its control code cold (under load every block does, the engines'
  loops evicting it). An idle half costs about the lines its code touches. In the sound path: a branch
  rarely taken is `PL_RARE` (its code out of the usual way), a helper called many times a half is
  `noinline` (one copy, not one per call), and new control code is counted in lines walked, not
  instructions.
- **Two cores** (`specs/second-core.md`): core 2 runs each audio block's effects and output stage (the job,
  `pl_c2_job`), handed over as the block's voices are made, and at VOICES 16 the voices past 10 (the pool's places
  11-16, `PL_C2_FROM`: the voice job, run from the image, the one job that reads the flash; so every flash operation
  of core 1's waits for core 2 idle); everything else stays on core 1.
  - Code core 2 may run is marked `PLAT_C2` (a table `PLAT_C2_RO`) and lives in core 2's room in RAM; it calls only
    marked code: its own `c2_memset` / `c2_memcpy`, never a copy the compiler may turn into a `memcpy` call, and an
    inline helper it uses carries the mark too (the device's compiler inlines across sections only `always_inline`).
  - Core 1 reaches marked code only through `PLAT_FAR`; each such call the interrupts can reach needs its hook in
    `fw/tools/stackdepth.py`. The build refuses a call or a flash address leaving the room. The debug build's room
    has about 2 KB left.
  - What the job reads or writes is handed over with its block or belongs to one core
    (`specs/second-core.md#who-owns-what`); `fw/tests/run_sound.sh`'s c2_test renders every case with the jobs run
    inline, a block late and a half late, and each must equal core 1's alone.
- **Connections:** USB-MIDI (installs, data), USB audio (the FM-1 as the computer's audio input), TRS MIDI IN.
- **Look:** the user's FM-1 is black with mint silicone. `panel/layout.js` measures M-VAVE's official
  image; the body's labels are a 5x7 dot font; the silicone's face is drawn as Bahnschrift Bold (open:
  `specs/open-questions.md`).

## The virtual FM-1 (`panel/`, `playground/`)

The FM-1's controls in the browser, made for ease of use rather than the device's look
(`specs/panel.md`): one big screen, small knobs, buttons and keys around it, a WIDE view for a computer
and a PORTRAIT one for a phone held upright (picked by the window). `playground/index.html`, opened from
the file system in Chrome or Edge.

- **Pages from `file://`:** classic `<script>` files adding to one global, `PTFM` (Chrome refuses
  modules and `fetch` there); no CDNs, no web fonts.
- **Testing:** `tests/run_playground.sh` (Node, no browser): `panel/midi.js` against
  `fw/tests/vectors.json` (`tests/controller_page.js`), the behaviour checks, the sound
  offline, every screen as a PNG in `build/playground/`. A Chrome started with a debug profile can also be
  driven at `127.0.0.1:9222` (the DevTools protocol). Stop any Chrome you start yourself.
- **The screen:** a 240x240 buffer shown as sharp squares: each screen pixel a whole number of the
  display's pixels where the size allows (`dev.zoom`: the page's ZOOM 1×, 2×, 3×, FIT in WIDE; PORTRAIT
  sizes it to what the controls leave). Right-click saves or copies the 240x240 PNG.
- **Tried in a browser of our own:** a headless Chrome started from WSL (`google-chrome --headless=new
  --remote-debugging-port=...` with a scratch profile) for other window sizes and a phone's metrics and
  touch; never force the size of the user's debug Chrome (below).
- **The screen driver** (`lcd.js`) works like the firmware's display stack (lcd.c, gfx.c's strip).
  The playground's UI does not use the strip: it is not firmware code. It draws whole frames in its
  own buffer (`draw.js`: palette roles and themes, the fonts T, S, B, L) and sends them in one window.
- **The playground is not the firmware.** Write it as plain, readable JS; nothing in it is meant to
  become C. It is there to try the UI with sound.
- **Files:**
  - `panel/`: `layout.js` (positions, firmware ids; units are screen pixels, panel 1405 x 837),
    `device.js` (the panel's two views, input events, LEDs, screen; `LED_COLOUR` is a guess),
    `keyboard.js`, `panel.css`, `midi.js` (the real FM-1 as the controller over Web MIDI, `specs/controller.md`:
    `PTFM.attachMidi(dev, {onState})`, the pairing, mirror mode, the LEDs back);
  - the page's MIDI button beside ZOOM is `main.js`'s (`window.midi`);
  - `playground/`:
    - `index.html`, `playground.css`, `main.js` (the page, the frame loop, ZOOM, `body.portrait`);
    - `fonts.js` (generated by `tools/gen_fonts.py`), `content.js` (generated by
      `tools/gen_playground.py` from `demo-disk/`, committed), `lcd.js`, `draw.js`;
    - `model.js`: the content as edited, UNDO, and the "flash": SAVE keeps a copy in the tab, a reload
      starts from the demo data; numbers on screen go through its `M.hex` (indexes, from 0, and values),
      `M.dec` (counts, values with a unit), `M.sgn` (signed values) and `M.of` (N/M)
      (`specs/ui/frame.md`); `cmds.js`: the commands as the screens show them (3 characters,
      words, the list the EDITOR's K2 turns through);
    - `ui.js`: the controls' grammar (taps, holds, the grace period; a held button's layer is
      `U.layers`, not an overlay: it takes no input and stays until the button is let go; HOME's
      combination, tapped open, is an overlay of `kind: "home"`, held it is the held entry; every
      combination's lock: `member` / `useCombo`, `closeCombos`), overlays,
      edit sessions (`U.session`: what an EDIT-entered mode edits, copied at EDIT; EDIT keeps, HOME
      `drop()`s), the frame, LEDs, navigation; `help.js`: the help's text (the screens' specs' Help
      parts and `specs/ui/help.md`, kept equal to them), marked up;
    - the screens: `scr-grid.js` (the pattern grid, the pattern list, reference questions),
      `scr-songs.js`, `scr-editor.js`, `scr-instr.js`, `scr-play.js` (PLAYBACK, FX), `scr-loops.js`
      (LOOPS), `scr-hear.js` (VISUAL, STATUS, SETTINGS);
    - `player.js`: the transport and the replay (songs, the EDITOR's loop, the loops), tick by tick,
      scheduled a little ahead on the audio clock;
    - the sound: `audio.js` (the page's side; the CPU figures are the FM-1's, estimated), and the
      worklet's `dsp-core.js` (voices, events, the pool), `dsp-dx7.js` (after MSFA), `dsp-synth.js`
      (Juno-106, TB-303, sampler: the Juno-106 and the TB-303 follow the device's engines step by step,
      in floats), `dsp-fx.js` (mixer, chain, sends, DUCK, punch-ins, METRONOME). `tests/playground/sound.js`
      checks that each engine's instruments come out as their LEVELs say. An engine's fields on a voice
      must not reuse the core's names (`rel`, `gain`, `vol`, `midi`, `bend`, ...).
      The worklet is put together from their source when the sound starts (a `data:` URL, else a
      `blob:` one), so they are edited like any other file. Where neither loads (a page inside another
      site's frame), `audio.js` runs the same code on the page's thread (`onPageThread`, a
      ScriptProcessorNode).
- **The device API** (`window.fm1`): `on("button" | "note" | "encoder" | "master" | "*", fn)`
  (encoders give `step` ±1), `on("led", fn)` (an LED's level changed); `led(id, level)` (1 on, 1/4
  dim); `state` (`buttons`, `notes` as bit masks, MASTER); `press(id, src)`, `release(id, src)`;
  `turn(id, step, t)`, `setMaster(v)` (input from code: MIDI; `t` the message's time stamp, for knob
  speed); `zoom(z)` (WIDE's screen: 1, 2, 3 or "fit"; again on a resize), `view()` ("wide" or
  "portrait").
- **Testing in the debug Chrome:** use the user's window as it is; forcing the tab's size or pixel
  ratio (DevTools' device metrics) shows the page off-centre and its screen blurred in their window.
- **The sound starts with the first click or key** (browsers allow audio only after one).
- **Input:** buttons and keys press while held (several fingers on a touch screen; Shift+click holds
  one); a held button keeps the pointer, so SEL + a wheel turn over PRESETS works. Knobs: − / + (held:
  repeating, faster), dragging the middle (14 px a click, up or right clockwise) or the wheel; a turn's
  value shows from 0 and fades after a second. MASTER: a bar with its %, 100 % at first, double-click back to 100 %. Keyboard: `Z X C V B N M , . /` F3-A4 (`S D F H J L ; '` black), `Q W E R
  T Y` B4-G5 (`3 4 6` black); the buttons `1 2` OCT- OCT+, `7 8 9 0 - =` FX SEL ENV LFO EDIT GLO, `U I O P [ ]` HOME
  SAVE ARP SEQ PLAY REC.

## The engines and their limits

- **Engines,** in integer arithmetic, each tested on the PC against a reference implementation: DX7
  (after MSFA), Juno-106 (after KR-106), losslessly compressed samples, TB-303 (after Open303), MOD
  and S3M through an IBXM port, and global effects; CHIP, our own virtual retro sound chip
  (`specs/chip.md`), has no outside reference: its firmware and playground versions are tested equal sample
  for sample (`fw/tests/run_chip.sh`).
- **Voices:** one pool of 8, 12 or 16 for every engine and the keys, 16 the default: 10 on core 1 and
  6 on core 2 (`PL_VOICES_MOST`, `fw/core/play/player.h`), counted as 12 while core 2 does not run
  (`specs/second-core.md`). 16 on core 1 alone overran on the FM-1; 12 + 4 ran on the FM-1, 10 + 6 is not
  measured there yet.
- **Storage and song data:** the files specs (`specs/files/`).

## Later, not now

- 808 and 909 engines (paused).
- Bluetooth MIDI: not in PT/FM, which never enables the radio (reached only through closed libraries:
  `specs/hardware.md`, the radio).

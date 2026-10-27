# The playground

Status: **settled** (2026-10-09). The record: [the UI proposal](proposals/202610072229-ui.md),
section 11.

The playground (`playground/`) is for trying the UI fast, with sound, and without a build: the files
are edited and the page reloaded. It runs on the panel, the virtual FM-1's controls
([panel.md](panel.md)), as the emulator does ([emulator.md](emulator.md)), but it is neither the
firmware nor a build of it: the UI and
the sound are written again in plain JS ([layout.md](layout.md)). It is how the UI specs
([ui/](ui/README.md)) are tried; the specs lead, and the playground follows them.

## What it has

- **Its content is the demo disk** (`demo-disk/`, free licences only), all of it, with the disk's
  numbers: its cartridges, the pattern pool, the songs with their timing, mixer and FX setups (it keeps
  no loops: LOOPS starts empty). `tools/gen_playground.py` turns the
  disk's sources into `playground/content.js`; the playground knows nothing of disks
  ([files/disk.md](files/disk.md)).
- **It sounds:** the engines are written again in plain JS, after the same models as the firmware's
  (MSFA, KR-106, Open303, the effects), and run in the browser's audio thread (on the page's own
  thread where the page's rules refuse an audio worklet, as inside another site's frame). They are not the
  firmware's bits, but each instrument comes out as loud as on the firmware's engines, so the songs
  balance as they will on the device: each engine's reference sound is at −20 dB, and its instruments
  at their LEVELs too ([files/instruments.md](files/instruments.md)). The firmware's engines stay the
  reference.
- **It plays the commands** tick by tick, as XM players do, after IBXM's rules: each track's pitch,
  volume and pan drive the voice its note plays, a synth's as a sample's; parameter, mixer and FX
  commands turn the knobs they name. Amiga-period slides play as linear ones.
- **No build:** the page is opened from the file system and edited as it is. Only its content is
  generated (`tools/gen_playground.py`), when the demo data changes.

## What it fakes

- the CPU figures, estimated from what sounds and each engine's cost on the FM-1 (the header's CPU bar and
  OVER 9000 follow the estimate, [ui/frame.md](ui/frame.md#the-header)), split as on the FM-1: the effects' and the
  output's share is core 2's while SETTINGS' SECOND CORE is on, core 1's while it is off; the second core never
  stops there ([second-core.md](second-core.md#the-emulator-and-the-playground));
- the battery;
- AUTO LEVEL on INSTRUMENTS, which sets a level without measuring it;
- the flash: SAVE keeps the changes in the browser tab, and a reload starts from the demo data again;
- USB audio: there is none, so USB GAIN is kept but changes nothing; the page's sound is the analog
  out's (LINE GAIN, MASTER, OUTPUT);
- MIDI in and out ([connections.md](connections.md)): SETTINGS' MIDI page is there and keeps its
  settings, but nothing comes in from an outside controller or goes out, the clock included; LEARN's
  first step takes an FM-1 control and the second waits until HOME (the MIDI map shows what it holds);
- the packed texts: it draws the firmware's texts as plain strings ([ui/texts.md](ui/texts.md)).

## Using it

- **Opened from the file system** in Chrome or Edge: `playground/index.html`. The sound starts with a
  click on the page; a press arriving over MIDI does not count as one.
- **The page around the panel** is the emulator's too ([panel.md](panel.md#the-page)): a sidebar (SOUND, THE
  FM-1 OVER MIDI, SCREEN's ZOOM, HOW TO PLAY), a header, a footer with what is held; on a phone, or a window
  under 1000 px, the sidebar is a drawer MENU opens.
- **On a phone,** held upright, the panel shows its PORTRAIT view by itself, held sideways its SIDEWAYS one
  ([panel.md](panel.md)). The
  page's files can also be put together into one page (its scripts and styles inlined) to open there.
- **The real FM-1 as its panel:** with MIDI CONTROLLER in mirror mode, the FM-1's controls
  work the virtual one and its LEDs follow the page ([controller.md](controller.md)).
- **Its help** holds the same text as the specs' Help parts, marked the same way
  ([ui/help.md](ui/help.md)).
- **Its tests:** `tests/run_playground.sh` (Node, no browser): the behaviour checks, the sound offline,
  and every screen rendered as a PNG in `build/playground/`.

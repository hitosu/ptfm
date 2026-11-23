<!-- SPDX-License-Identifier: GPL-3.0-only
     Copyright (C) 2026 Vasily Kazantsev -->
# The Juno-60 conversion

The EDITOR reads a Juno-60's tape as a Juno-106 cartridge, each patch converted to the Juno-106 patch whose sound
comes closest (`specs/editor.md`, Juno-60 patches as Juno-106 ones). This folder makes the conversion's tables and
checks the conversion, programmatically: renders compared measure by measure.

The reference for the Juno-60 is **Ultramaster KR-106** (GPL-3.0), its Juno-60 model's sound code (its headers are
included from the path given; nothing of it is copied here); the Juno-106 side is PT/FM's own engine
(`fw/core/eng/juno`), its tables indexed by the patch's byte as on the FM-1.

| File | What |
|---|---|
| `quant.h` | every quantity compared, defined the same on both sides (KR-106's Juno-60 model; PT/FM's engine) |
| `convert.h` | the tables (`buildTab`) and the per-patch rules (`convert`): the twin of `editor/js/juno60.js` |
| `gen.cpp` | writes `editor/js/juno60_tables.js` and `vectors.json` (patches made up here, with their conversions) |
| `vectors.json` | made by `gen.cpp`: `tests/editor/juno60_test.js` converts them with the page's code and compares |
| `check.cpp` | the check: KR-106's Juno-60 mode against the conversions on PT/FM's engine, the yardstick from the Juno-106 factory patches through KR-106's Juno-106 mode and PT/FM's engine |
| `run.sh` | builds and runs all of it |

## Running

From `ptfm/`, `KR106` naming KR-106's source tree (its `Source/DSP` headers and its `tools` folder's factory data,
read at run time, never copied):

```sh
KR106=/path/to/ultramaster_kr106 sh editor/tools/juno60/run.sh               # the tables, the test, the check
KR106=/path/to/ultramaster_kr106 sh editor/tools/juno60/run.sh --no-check    # the tables and the test only
```

The tables and `vectors.json` are kept as generated: the page and its tests need neither KR-106 nor this folder. The
check writes `build/juno60/check.md` (its figures, the patches past the yardstick and why, the HPF's 122 Hz both
ways, PWM SOURCE ENV's choices, every conversion) and `build/juno60/check.csv`; its figures go into the version's
report. A table or a rule the check finds off is corrected here and the check run again.

## What the check does

- **The patches:** KR-106's Juno-60 factory data (both tapes' values) and its Juno-106 factory patches.
- **Rendered:** one voice, C2, C4 and C6, 2 s held and 2.5 s of release, RANGE 8', the chorus off on both sides;
  KR-106 with its per-voice variations, its drift and its analog noise floor off; given each Juno-106 byte as PT/FM's
  engine reads it (DCO LFO through KR-106's own undoing of its slider taper, DCO PWM against 105).
- **Measured** on what is heard (the sound high-passed at 20 Hz: a pulse's DC step through the coupling filters is
  each engine's own and not heard), each measure on its own, a patch's figure the worst of its three notes:
  - the attack: the time the sound's size (its largest sample over a period, every ms) takes to come within 3 dB of
    its largest while held;
  - the level while held: the level every 10 ms (its power the mean over 50 ms), each against its own peak, the
    mean difference in dB (50 dB down at most);
  - the release: the times each takes to fall 10, 20 and 30 dB from its level at the note's end, their ratios in
    octaves, the mean (a tail far down, where one release reaches silence and the other fades on, counts no more than
    it is heard);
  - the brightness: the spectrum's power-weighted centroid every 23 ms while held, where both sound;
  - the pitch and the vibrato's depth and rate: the autocorrelation's peak every 10 ms within the patch's vibrato
    depth and a semitone of the note (so a filter ringing elsewhere is not taken for the oscillators), while the sound
    is within 40 dB of its peak.

  The overall level is reported, not counted (LEVEL and AUTO LEVEL are there for it).
- **The yardstick:** the Juno-106 factory patches' figures, KR-106 against PT/FM; a converted patch passes a measure
  within that measure's 95th percentile. Patches whose filter self-oscillates or that have no oscillator (their
  sound the filter's ringing, or noise) are where the two models differ most, the yardstick's own included: the report
  marks them.
- **The HPF's 122 Hz** (the Juno-60's, between the Juno-106's FLAT and 236 Hz): both rendered, the closer kept as
  `HPF_122` in `convert.h` (236 Hz).
- **PWM SOURCE ENV** (no Juno-106 counterpart): the rule against the other choices, on the patches where it matters.
- **One patch's renders** to hear, or one byte swept to see where the check would have it:
  `build/juno60/check KR106_DIR wav JOB NOTE` (writes `build/juno60/jobJOB_kr106.wav` and `_ptfm.wav`),
  `build/juno60/check KR106_DIR sweep JOB BYTE STEP` (a job: its number in the run, `check.csv`'s first column).

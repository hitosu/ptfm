# Commands

Status: **settled** (2026-10-09). The record: [the files proposal](../proposals/202610080050-fs.md),
section 4; the punch-ins are new here.

A command is a code and a value, a byte each. A cell holds up to four
([patterns.md](patterns.md#a-cells-bytes)); they run in their order, and a converted XM cell lists its
volume column's first, as FastTracker 2 runs them. The note editor names each and says its value in
words (`SLIDE`, `ACCENT`, `ARPEGGIO 0 4 7`, `TAPE STOP 8 ROWS`), by its code and, for 0x40-0x7F, by the
engine of the track's instrument. The grid shows a command in 3 characters
([../ui/screens/editor.md](../ui/screens/editor.md)): a tracker effect as its letter and value (`A04`),
an XM volume column's as `~` with FastTracker 2's sign and digit (`~-4`), PT/FM's own by name (`ACC`),
a punch-in as `!` and its rows (`!08`), every other by its group's sign and value (`*40` a parameter,
`=70` the mixer, `#3C` the FX).

| Codes | Group | Scope |
|---|---|---|
| 0x00-0x3F | tracker effects, with XM's own numbers | the track; global ones marked |
| 0x40-0x7F | the instrument's parameters, a table for each engine | the track |
| 0x80-0xBF | the track's mixer; the FX | the track; the FX global |
| 0xC0-0xFF | PT/FM's own | as listed |

## Tracker effects

XM's effects, as XM players run them, so MOD, S3M and XM songs convert without loss.

- **The player runs them with IBXM's code** (BSD-3-Clause), extended: a cell's commands run in turn
  through its handlers; the pitch, volume and pan they make each tick drive whichever voice the track
  plays, a synth's as a sample's; the codes from 0x40 up go to the engines, the mixer and the FX.
- **IBXM is extended freely where PT/FM needs it,** each extension marked in its code. Among them,
  two effects upstream IBXM ignores, played as FastTracker 2 plays them:
  - `E3x` GLISSANDO: on (x not 0), a slide to the note (`3xx`, `5xy`, `~Mx`) is heard in semitones,
    on the note's finetune, while it slides smoothly underneath;
  - `E5x` FINETUNE: at the note's start, -8 to +7 in 1/8 semitones.
- **Upstream IBXM is the test:** a converted song must render as upstream IBXM renders the original
  file, sample for sample with sample instruments, except where an extension plays what upstream
  IBXM leaves out (a song using `E3x` or `E5x`); the test lists those.

| Code | Shown | Name | Notes |
|---|---|---|---|
| 0x00 | `0xy` | ARPEGGIO | |
| 0x01, 0x02 | `1xx` `2xx` | PORTA UP, PORTA DOWN | |
| 0x03 | `3xx` | TONE PORTA | slide to the note |
| 0x04 | `4xy` | VIBRATO | |
| 0x05, 0x06 | `5xy` `6xy` | TONE PORTA + VOL SLIDE, VIBRATO + VOL SLIDE | |
| 0x07 | `7xy` | TREMOLO | |
| 0x08 | `8xx` | PAN | the track's pan, as the MIXER's |
| 0x09 | `9xx` | SAMPLE OFFSET | × 256 samples, from the instrument's START |
| 0x0A | `Axy` | VOL SLIDE | |
| 0x0B | `Bxx` | JUMP | global: to a song position |
| 0x0C | `Cxx` | VOLUME | |
| 0x0D | `Dxx` | BREAK | global: to a row of the next position; the row in hex, as the row numbers (`D20` is row `20`, `00`-`FF`); converters rewrite MOD, S3M and XM's decimal (`D32` becomes `D20`) |
| 0x0E | `Exy` | extended: FINE PORTA UP / DOWN, GLISSANDO, VIBRATO WAVE, FINETUNE, LOOP, TREMOLO WAVE, RETRIG, FINE VOL UP / DOWN, NOTE CUT, NOTE DELAY, PATTERN DELAY | PATTERN DELAY and LOOP (`E6x`: it moves the whole pattern's rows) global |
| 0x0F | `Fxx` | TICKS (value below 0x20), TEMPO (0x20-0xFF); `F00` back to the song's own tempo and ticks | global |
| 0x10, 0x11 | `Gxx` `Hxy` | GLOBAL VOLUME, GLOBAL VOL SLIDE | global |
| 0x14 | `Kxx` | KEY OFF | |
| 0x19 | `Pxy` | PAN SLIDE | |
| 0x1B | `Rxy` | MULTI RETRIG | |
| 0x1D | `Txy` | TREMOR | |
| 0x1E | `Uxy` | FINE VIBRATO | S3M's, with no XM twin; IT's letter |
| 0x21 | `Xxy` | EXTRA FINE PORTA | |
| 0x22 | `Yxy` | PANBRELLO | IT's, with no XM twin; IT's letter |
| 0x24, 0x25 | `~-x` `~+x` | VOL SLIDE DOWN, UP | XM's volume column, as it runs there (no memory) |
| 0x26, 0x27 | `~Dx` `~Ux` | FINE VOL DOWN, UP | XM's volume column |
| 0x28, 0x29 | `~Sx` `~Vx` | VIBRATO SPEED, VIBRATO DEPTH | XM's volume column |
| 0x2A | `~Px` | PAN | XM's volume column |
| 0x2B, 0x2C | `~<x` `~>x` | PAN SLIDE LEFT, RIGHT | XM's volume column |
| 0x2D | `~Mx` | TONE PORTA | XM's volume column |

A code's letter is its number in the digits 0-9 and A-Z, as in XM. XM's volume-column effects take a
digit, 0-F, and are shown with FastTracker 2's signs after a `~`.

XM's envelope position (`L`) is not kept: sample instruments have envelopes of the DX7's form
([instruments.md](instruments.md#sample-instruments)), so XM envelopes convert approximately anyway.

## Time

- **A song has TEMPO, TICKS A ROW and ROWS A BEAT.** A row lasts TICKS A ROW ticks; a tick lasts
  2.5 s / TEMPO, as in trackers. Effects (slides, vibrato, retrigger, delay, cut) work tick by tick.
- **The musical tempo** the screens show is TEMPO × 24 / (TICKS A ROW × ROWS A BEAT): with the
  defaults, 6 ticks and 4 rows a beat (a row is a 1/16 note), it is TEMPO itself. A module at another
  speed converts exactly, and shows its musical tempo (speed 3 at 125 shows 250).
- **TICKS** (`F01`-`F1F`) changes the row's length and keeps the tick's; **TEMPO** (`F20`-`FFF`) changes
  the tick's, to TEMPO 32-255 (a song's own TEMPO reaches 3-600: [songs.md](songs.md#the-record)). `F00`
  returns to the song's own.
- **SWING** lengthens the first row of each pair and shortens the second, by stretching their ticks.
- The metronome, DELAY times, GATE rates and the punch-in loops follow the beat that the tempo and ROWS
  A BEAT give; QUANTIZE (the EDITOR's takes, LOOPS) counts rows, and its REAL TIME ticks.

## The instrument's parameters (0x40-0x7F)

A table for each engine, up to 64 codes each. A parameter command sets its value from its row on,
acts on the track's sounding notes as turning the knob would, and stays until changed; a new
instrument on the track drops it. A sweep is a command on every row (the editor's INTERPOLATE fills
one in between two values).

- **The value is the parameter's own,** as its slot stores it ([instruments.md](instruments.md)); the
  note editor says it as the edit page does (`DETUNE -3`, `RANGE 8'`, `LFO WAVE SINE`). A value past
  the range acts as its top.
- **The codes follow the slot's order,** with gaps where a regular map reads better.

**TB-303** (0-127 each):

| Code | Parameter |
|---|---|
| 0x40-0x45 | WAVE, CUTOFF, RESONANCE, ENV MOD, DECAY, ACCENT |

**Juno-106:**

| Code | Parameter | Values |
|---|---|---|
| 0x40-0x4F | the 16 sliders in the Juno-106's order: LFO RATE, LFO DELAY, DCO LFO, DCO PWM, DCO NOISE, VCF FREQ, VCF RES, VCF ENV, VCF LFO, VCF KYBD, VCA LEVEL, ENV A, ENV D, ENV S, ENV R, DCO SUB | 0-127 |
| 0x50 | RANGE | 0 16', 1 8', 2 4' |
| 0x51, 0x52 | PULSE, SAW | 0 OFF, 1 ON |
| 0x53 | CHORUS | 0 OFF, 1 I, 2 II |
| 0x54 | PWM SOURCE | 0 LFO, 1 MAN |
| 0x55 | ENV POLARITY | 0 +, 1 − |
| 0x56 | VCA MODE | 0 ENV, 1 GATE |
| 0x57 | HPF | 0-3 |

**DX7:** eight codes an operator, so the hex says which (`0x4x`, `0x5x`, `0x6x`; +8 an operator):

| Code | Parameter | Values |
|---|---|---|
| 0x40 + 8 × (operator − 1), + 0-7 | OUTPUT LEVEL, COARSE, FINE, DETUNE, R1, R2, R3, R4 | 0-99; COARSE 0-31; DETUNE 0-14, 7 none |
| 0x70-0x73 | ALGORITHM, FEEDBACK, OSC KEY SYNC, TRANSPOSE | 0-31 (shown 1-32); 0-7; 0-1; 0-48, 24 none |
| 0x74-0x77 | LFO SPEED, LFO DELAY, LFO PITCH DEPTH, LFO AMP DEPTH | 0-99 |
| 0x78-0x7A | LFO WAVE, LFO KEY SYNC, PITCH MOD SENS | 0-5 (TRI, SAW DOWN, SAW UP, SQUARE, SINE, S/H); 0-1; 0-7 |

The envelope levels, key scaling, the sensitivities and the pitch envelope are edited on the
instrument only.

**Sample instrument:**

| Code | Parameter | Values |
|---|---|---|
| 0x40, 0x41 | START, END | a position: the value × 1/256 of the sample's length |
| 0x42 | LOOP | 0 OFF, 1 FORWARD, 2 PING-PONG |
| 0x43, 0x44 | LOOP START, LOOP END | a position, as START |
| 0x45 | TUNE | 1/128 of a semitone, 0x80 none |
| 0x48-0x4B, 0x4C-0x4F | the envelope's R1-R4, L1-L4 | 0-99 |

START acts from the next note; END and the loop's points move under a sounding note.

### CHIP

[../chip.md](../chip.md): the slot's fields in their order; each takes effect on the sounding note from the next
64-frame block.

| Code | The note editor's name | Values | On the sounding note |
|---|---|---|---|
| 0x40 | `OSC` | 0-4 | finds its register again |
| 0x41 | `DUTY` | 0-3 | until a DUTY table's next frame |
| 0x42-0x44 | `BUZZ SHAPE`, `BUZZ STEPS`, `BUZZ GATE` | 0-2; 0-1; 0-1 | SHAPE and STEPS find the register again |
| 0x48-0x4B | `NOISE`, `NOISE MODE`, `NOISE RATE`, `NOISE KEY` | 0-1; 0-1; 0-F; 0-1 | RATE until a NOISE table's next frame |
| 0x4C-0x4F | `CLOCK`, `CURVE`, `FILTER`, `FRAMES` | 0-6; 0-3; 0-3; 0-1 | CLOCK finds the register again |
| 0x50-0x52 | `SWEEP`, `SWEEP SHIFT`, `SWEEP PACE` | 0-2; 0-7; 0-7 | a sweep starts or stops (a silenced note stays silent); OSC and CLOCK say again whether one runs |
| 0x54-0x57 | `VOL`, `ENV`, `ENV STEP`, `RELEASE` | 0-F; 0-3; 0-7; 0-F | VOL is the step envelope's level now; ENV restarts the PSG envelope |
| 0x58-0x5A | `PSG SHAPE`, `PSG PERIOD HI`, `PSG PERIOD LO` | 0-F; 0-FF; 0-FF | SHAPE restarts the PSG envelope; each byte of the PERIOD as it comes (on two rows, a period between for a row) |
| 0x5B | `WAVE` | 0-F | |
| 0x60-0x64 | `VOL TABLE`, `ARP TABLE`, `PITCH TABLE`, `DUTY TABLE`, `NOISE TABLE` | 00-3F | starts that table from its first step, the same number too |
| 0x68-0x6C | `VOL SPEED`, `ARP SPEED`, `PITCH SPEED`, `DUTY SPEED`, `NOISE SPEED` | 0-3 (`X1`-`X4`) | |
| 0x6D-0x6F | `VIB DELAY`, `VIB SPEED`, `VIB DEPTH` | 0-1F (frames ÷ 2); 0-3 (`X1`-`X4`); 0-1F (cents ÷ 2) | |

The note editor says the value as the edit pages do (`OSC TRIANGLE`, `ARP TABLE 06 MAJOR`, `VOL SPEED X2`, `VIB
DELAY 20 FRAMES`, `VIB DEPTH 40 CENTS`).

## Mixer and FX (0x80-0xBF)

- **The track's mixer:** 0x80 LEVEL, 0x81 CHORUS SEND, 0x82 DELAY SEND, 0x83 REVERB SEND, 0x84 DUCK
  AMOUNT. PAN is `8xx`. The values are a track's mixer bytes ([songs.md](songs.md#a-tracks-mixer-bytes)).
- **The FX, global:** a code for every knob ([../ui/screens/fx.md](../ui/screens/fx.md)): 0x90-0x93 GATE
  (PATTERN, RATE, DEPTH, SMOOTH), 0x94-0x95 FILTER (CUT, RESO), 0x96-0x97 DRIVE (DRIVE, TONE),
  0x98-0x9A LO-FI (BITS, RATE, DUST), 0x9B-0x9E CHORUS (MODE, RATE, DEPTH, MIX), 0x9F-0xA3 DELAY (TIME,
  FEEDBACK, TONE, PING-PONG, MIX), 0xA4-0xA7 REVERB (SIZE, DAMP, PRE-DELAY, MIX), 0xA8-0xA9 LIMITER
  (CEILING, RELEASE), 0xAA-0xAC DUCK (SOURCE, DEPTH, RELEASE), 0xB0-0xB5 FILTER's envelope (ENV, SOURCE,
  ATTACK, DECAY, SUSTAIN, RELEASE: [../ui/screens/fx.md](../ui/screens/fx.md#filters-envelope)); 0xAE FX ON and
  0xAF FX OFF, the value the effect's number ([below](#the-numbers-are-fixed)). A command's value is the knob's
  step, as the FX setup keeps it ([songs.md](songs.md#the-fx-setup)).
- They persist as the knobs do, until changed or until another song starts.

## PT/FM's own (0xC0-0xFF)

| Code | Shown | What |
|---|---|---|
| 0xC0 | `ACC` | TB-303 accent |
| 0xC1 | `SLD` | TB-303 slide into the next note |
| 0xC2 | `A+S` | both |
| 0xC3-0xD2 | `!xx` | the punch-in effects (below) |
| 0xD3 | `LEG` | legato: the track's held note moves to this row's note, no new attack ([below](#legato)) |
| 0xFF | | not a command: the start of a cell's list of 2-4 ([patterns.md](patterns.md#a-cells-bytes)) |

`ACC`, `SLD`, `A+S` and `LEG` have no value (`00` written, any other read as `00`).

### Legato

`LEG` on a note's row plays the note legato: the note the track holds moves to it at once, with no new attack. Its
sound goes on as it was (the envelopes, the LFO, a sample's place), only its pitch changing, as a slide to the note
(`3xx`) that arrives at once.

- **Only onto a held note of the same instrument:** the track's latest note, still sounding and not let go (no `===`,
  `Kxx` or note cut since), whose instrument is the row's (the cell's, else the track's). Otherwise the note plays
  as a plain note, its attack and all.
- **By engine:** DX7, JUNO-106 (its filter's key follow moving to the new note), CHIP and samplers (the sample
  playing on from its place, at the new pitch) play it legato. A TB-303 has its own legato, its slide: a note after
  `SLD` slides there whatever `LEG` says, and without `SLD` its gate has closed, so `LEG` changes nothing there. A
  FUSION instrument always plays a plain note (its layers depend on the note).
- **The rest of the row acts as with a slide to the note:** the cell's volume and its other commands work as they do
  on any note; an instrument in the cell sets the volume as it does with `3xx`. A later `3xx` slides from the new
  note.
- **Starting from a later row** (a chase, [songs.md](songs.md)) plays the notes before it plainly: the first note
  heard always has its attack.

### Punch-ins

The punch-in effects ([../ui/screens/fx.md](../ui/screens/fx.md#punch-in-effects)), which FX held + a
white key runs live, are commands too: a song or a loop can carry them. Their codes are fixed; the keys
in brackets are where they sit today ([below](#the-numbers-are-fixed)).

| Code | Effect (key) | Code | Effect (key) |
|---|---|---|---|
| 0xC3 | LOOP 1/4 (1) | 0xCB | LOW-PASS SWEEP (9) |
| 0xC4 | LOOP 1/8 (2) | 0xCC | HIGH-PASS SWEEP (10) |
| 0xC5 | LOOP 1/16 (3) | 0xCD | ECHO THROW (11) |
| 0xC6 | LOOP 1/32 (4) | 0xCE | REVERB SWELL (12) |
| 0xC7 | STUTTER 1/16T (5) | 0xCF | REVERSE (13) |
| 0xC8 | BIT CRUSH (6) | 0xD0 | HALF SPEED (14) |
| 0xC9 | GATE 1/16 (7) | 0xD1 | TAPE STOP (15) |
| 0xCA | GATE 1/32 (8) | 0xD2 | VINYL STOP (16) |

- **The value is how many rows it stays on,** `01`-`FF`. The command plays exactly as if its key were
  held that long: `TAPE STOP` with `08`, at 4 rows a beat, winds down over the first beat, is silent
  for the second, and comes back at row 8; `VINYL STOP` falls, stays silent, and spins back up when
  its rows end. `00` is not written: the smallest is `01`.
- **Shown** in the grid as `!` and the rows (`!08`); the note editor names it in words
  (`TAPE STOP 8 ROWS`).
- **Global:** it works from any track. One punch-in runs at a time: a later one replaces the one
  running. It is ignored in FUSION rows. Its rows run on through a pattern change; STOP ends it.
- **The punch-ins that replay the last beat** (the loops, the stutter, reverse, tape stop, half speed,
  vinyl stop) take it from the mix as they do live: the punch-in buffer
  ([ram.md](ram.md#where-the-ram-goes)) records whenever anything plays.
- **Not part of the FX setup** ([songs.md](songs.md#the-fx-setup)): punch-ins are events, not settings.

## Commands together

- **A cell holds up to four,** each code at most once; they run in their order.
- **Common pairs have one code** (`A+S`), so they take one place and 2 bytes.
- **Parameters persist,** so a command can also go on an earlier row.
- **Global commands** (tempo, jumps, breaks, the FX, the punch-ins) work from any track.
- **In FUSION rows,** global commands are ignored, and shown dim: a key's row has no song position.

## Room to grow

| Codes | Used | Free |
|---|---|---|
| 0x00-0x3F | 35 | 29: XM's unused letters, then 0x2E-0x3F (room for IT's effects) |
| 0x40-0x7F | each engine's own 64: TB-303 6, sample instruments 14, Juno-106 24, DX7 59, CHIP 37 | the rest of each; a new engine gets 64 of its own |
| 0x80-0xBF | 42 | 22: about five more effects |
| 0xC0-0xFF | 20, and 0xFF for lists | 43 |

Beyond that, the pattern's format version allows a new cell layout (a second code byte after an escape
code), old patterns read as before.

## The numbers are fixed

Every code above keeps its meaning for good (the base, 2026-10-10; the rule:
[README.md](README.md#extending)). A new command, effect knob or punch-in takes a free code; none is
renumbered or used twice; one removed is retired (kept in the patterns, skipped by the player, shown
dim by the EDITOR).

- **The punch-ins' codes are not their keys.** Today code 0xC3 + *n* is white key *n* + 1; a punch-in
  moved to another key keeps its code, and the keys' order ([../ui/screens/fx.md](../ui/screens/fx.md#punch-in-effects))
  is the firmware's table.
- **The FX knobs' codes** (0x90-0xAC, 0xB0-0xB5) are the knobs' numbers in the FX setup too
  ([songs.md](songs.md#the-fx-setup)): a knob added later takes a free code (0xAD, 0xB6-0xBF), not the
  place after its effect's last (FILTER's envelope so: 0xB0-0xB5, not after RESO's 0x95).
- **The effects' numbers,** for FX ON and FX OFF's value, the chain's order and the on / off bits:

  | Number | Effect | Number | Effect |
  |---|---|---|---|
  | 0 | GATE | 6 | REVERB |
  | 1 | FILTER | 7 | LIMITER |
  | 2 | DRIVE | 8 | DUCK |
  | 3 | LO-FI | 9 | METRONOME |
  | 4 | CHORUS | 10 | ◆ SENDS (the chain's item; not an effect) |
  | 5 | DELAY | 11-14 | free; 15 ends the chain's list |

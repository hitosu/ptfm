# CHIP, the engine of the retro sound chips

Status: **settled** (2026-10-25). The record: [the CHIP proposal](proposals/202610251559-chip-engine.md).

One virtual sound chip whose voice takes the parts of the sound chips of the 8-bit era as an instrument's
settings, made the way those chips made sound, in integers. Its instruments' record is
[files/instruments.md](files/instruments.md#chip), its commands [files/commands.md](files/commands.md#chip), its
screens [ui/screens/instruments.md](ui/screens/instruments.md#edit-mode-chip); the built-in cartridge `F9 CHIPTUNE` holds 128
of them.

## What it is

Chip music is a sound of its own: square waves at a few widths, a stepped triangle, a 32-step drawn wave, noise
from a shift register, 4-bit volumes that move in steps 50 or 60 times a second, pitches that land on what a period
register can hold. CHIP makes it by modelling the hardware, from the chips' public documentation:

- **the parts of three families of chips, under four names,** each a word of its own on the screens, never a
  product:
  - **BRK**, the handheld: two pulse channels with four widths, a 32-step 4-bit wave channel, a noise channel with a
    long and a short (metallic) shift register, a frequency sweep, a stepped volume envelope (the Game Boy's sound
    hardware);
  - **8BIT**, the console: two pulse channels with their own four widths, a 32-step triangle, a noise channel with
    a long and a short mode and its rate table, a sweep, the non-linear output of its mixer, its output filters
    (the NES's sound hardware);
  - **PSG**, the computers' programmable sound generator: square tones on a 12-bit period, noise from a 17-bit
    register mixed into a tone, a hardware envelope of 16 shapes that can also run fast enough to be a tone (the
    "buzzer"), volumes 3 dB a step, on its three common clocks (General Instrument's AY-3-8910 and AY-3-8912); and
    **16BIT**, the same chip's later version with a finer volume and envelope of 32 steps (Yamaha's YM2149);
- **one voice is one channel** of the virtual chip, any part on any voice: the pool's voices, no chip's limit of
  three or four channels, every part available to every instrument (a PSG square with the handheld's sweep, a
  console triangle on the handheld's clock);
- **canonical where it is heard:** the real clocks and period registers (so high notes go out of tune as on the
  chips), 4-bit volumes, envelopes, sweeps and tables at the chips' own frame rates, the real noise registers and
  rate tables, each family's volume curve and output filter;
- **clean where it is not:** the hard edges are band-limited (no aliasing folding back), fast noise is averaged
  over each output sample, and nothing of the chips' faults is kept (no mixer crosstalk, no DC offset left in).

## The voice

A voice is an oscillator, a volume, a pitch and a few things that move them once a frame.

### The oscillator (OSC)

| OSC | What | Its settings |
|---|---|---|
| `PULSE` | a pulse: 8 steps a period, high for its DUTY | DUTY: `12.5%`, `25%`, `50%`, `75%` (the BRK's four, or the 8BIT's four with their own phase; PSG and EXACT clocks take the 8BIT's) |
| `TRIANGLE` | the console's triangle: 32 steps, down 15 to 0 then up 0 to 15 | |
| `WAVE` | one of the engine's 16 waves ([below](#the-waves)): 32 steps of 4 bits | WAVE |
| `BUZZ` | the PSG's hardware envelope run as an oscillator | SHAPE: `SAW DN`, `SAW UP`, `TRI`; STEPS 16 or 32; GATE: off, or cut by a square at the same note (the "sync buzzer") |
| `NOISE` | the noise register's output, high or low | (the noise's settings) |

**Noise mixed in** (NOISE `ON` with any OSC but `NOISE`): the oscillator is heard only while the noise is high, as
the PSG mixes noise into a tone.

### The clock (CLOCK)

Each OSC steps at a rate a period register gives: the step rate is the clock's constant over the register, the
register is a whole number in its range, and a note lands on the nearest register (so tuning is the chips': close
in the bass, coarse at the top).

| CLOCK | The clock | PULSE steps at | TRIANGLE, WAVE steps at | BUZZ steps at | Register P |
|---|---|---|---|---|---|
| `BRK` | 4,194,304 Hz | 1,048,576 / P | 2,097,152 / P | 2,097,152 / P | 1-2,048 |
| `8BIT` | 1,789,773 Hz | 894,886.5 / P (silent under P 9) | 1,789,773 / P | 1,789,773 / P | 1-2,048 |
| `PSG 1.77` | 1,773,400 Hz | the clock / 2 / P | the clock / 8 / P | the clock / 8 / P (STEPS 32), / 16 / P (STEPS 16) | PULSE 1-4,095; else 1-65,535 |
| `PSG 1.00` | 1,000,000 Hz | as PSG 1.77 | | | |
| `PSG 1.79` | 1,789,772 Hz | as PSG 1.77 | | | |
| `16BIT` | 2,000,000 Hz | as PSG 1.77 | | | |
| `EXACT` | none | the note's own pitch, no register | | | |

A PULSE period has 8 steps, a TRIANGLE or WAVE 32, a BUZZ its STEPS (doubled for `TRI`): so on `BRK` a pulse plays
131,072 / P Hz and the wave 65,536 / P Hz, on `8BIT` a pulse 1,789,773 / (16 P) Hz, on the PSG clocks a tone the
clock / (16 P).

**The note's pitch** before the register: the note, plus the ARP table's semitones and the arpeggio command's
(`0xy`), plus the PITCH table's and the vibrato's cents, plus the track's pitch (portamento, the vibrato commands:
what moves the other engines' notes).

**A voice's ceiling:** the oscillator and the buzz's gate step at most twice a sample, the PSG envelope once; a
note past it sounds at the highest pitch it allows (a 32-step wave about 2,756 Hz, a pulse 11 kHz). So a voice
makes at most 128 + 128 + 64 band-limited steps a block, whatever its settings, and noise mixed in slower than
the sample rate up to 64 more (384 in all; faster noise is averaged a sample at a time).

### The volume

- **VOL**, 0-15, the channel's volume.
- **ENV:** `NONE` (VOL holds); `DOWN` or `UP`, the step envelope: VOL moves one step every STEP × 1/64 s (STEP 0:
  it holds), on CLOCK `8BIT` every (STEP + 1) × 1/240 s; `PSG`, the hardware envelope: SHAPE 0-F (the chip's 16
  shapes: down, up, down repeated, the triangles, held high or low) and PERIOD 1-65,535, its steps at the clock /
  (16 × PERIOD) (16 steps) or the clock / (8 × PERIOD) (32 steps, with CURVE `16BIT`), the PSG 1.77 clock where the
  CLOCK is not a PSG's; the envelope's level takes VOL's place. Shapes 0-7, 9 and F fall silent after one round, B
  and D hold high, 8, A, C and E go on.
- **The VOL table** ([below](#the-tables)) scales it, 0-15 (15 = as is), once a frame.
- **RELEASE,** 0-15 (frames a step): once the key is let go, the level falls a step of 15 every RELEASE frames; 0
  ends the note at once. A VOL table that ends at 0 without a loop ends the note.
- **The curve** turns the level (0-15, or 0-31 on a 32-step envelope) into amplitude: `BRK` straight; `8BIT` the
  console's mixer (95.88 / (8,128 / x + 100), scaled to 1 at 15); `PSG` 3 dB a step; `16BIT` 1.5 dB a step of 32.
  The level a step is `VOL × table × release`, each a fraction of 15, rounded to a whole step, as a 4-bit chip
  would.
- **The level meets the oscillator** in one of three ways:
  - `PULSE` and `NOISE`: the level through the curve, while the pulse or the noise is high;
  - `TRIANGLE` and `WAVE`: the oscillator's 4-bit step × the level ÷ its top (15, or 31), rounded, through the
    curve of 16 levels (as a wave channel's volume shifts its samples);
  - `BUZZ`: the buzz's own level through the curve (its 16 levels, or 32 with STEPS 32: `PSG` then 1.45 dB a
    step), × the level ÷ its top, in a straight line.

### The sweep

**SWEEP** `OFF`, `DOWN` (the pitch falls) or `UP` (it rises), with SHIFT 0-7 and PACE 0-7: from the note's first
frame, every PACE × 1/128 s (PACE 0: no sweep), on CLOCK `8BIT` every (PACE + 1) × 1/120 s (PACE 0 too), the
register moves by a share of itself, as the handheld's and the console's sweep units do:

| CLOCK | What moves (by itself >> SHIFT) | Its end |
|---|---|---|
| `BRK` | the chip's frequency x = 2,048 − P: DOWN x − (x >> SHIFT), UP x + (x >> SHIFT) | UP past 2,047 silences the note; DOWN stops where its share comes to 0 |
| `8BIT` | the period P − 1: DOWN up by its share, UP down by it | DOWN past 2,047 silences the note; UP stops where its share comes to 0 |
| the PSG clocks | P: DOWN up by its share, UP down by it | held in the register's range |
| `EXACT` | the step's length: DOWN × (1 + 2^−SHIFT), UP ÷ (1 + 2^−SHIFT) | held between half a sample (the ceiling) and 16,384 samples |

(SHIFT 0 moves by the whole: on `BRK` DOWN goes to x 0 at once.)

**The sweep owns the register for the rest of the note:** the tables', the commands' and the track's changes of
pitch are not heard on a sweeping note after its first frame, whose ARP and PITCH set where it starts (a sweep from
two octaves up: ARP `UP 2 OCT`); a silenced note stays silent. A NOISE oscillator is never swept.

### The noise

- **RATE** 0-15: `BRK` 524,288 Hz at 0, then 262,144 / 2^(RATE − 1) Hz; `8BIT` (and `EXACT`) the console's 16
  periods (4-4,068 cycles of 1,789,773 Hz); the PSG clocks the clock / (16 × the period), the periods 1, 2, 3, 4, 5,
  6, 8, 10, 12, 14, 16, 19, 22, 25, 28, 31.
- **MODE:** `LONG`, a 15-bit register (taps 0 and 1), the PSG clocks their own 17-bit one (taps 0 and 3);
  `SHORT`, metallic: on `BRK` the 7-bit mode, on every other clock the 93-step mode (tap 6).
- **KEY:** `KEY ON` moves the rate with the note and the track's pitch (not the ARP table), a step a semitone from
  C-4 (higher notes brighter), clamped to 0-15; on top of a NOISE table's rate too.
- **Faster than the sample rate,** the register steps as many times as fall in each output sample and the sample
  is their average, put out as it is (no impulse): the average is its own filter. A tone mixed with such noise
  steps on the sample's grid then.

### The output filter (FILTER)

Each family's own, after the voice: `BRK` a 30 Hz high-pass; `8BIT` high-passes at 90 and 440 Hz and a 14 kHz
low-pass; `PSG` a 20 Hz high-pass; `NONE` a 5 Hz high-pass (the DC taken out). One-pole filters, per voice (the chips
filter their mix; for a linear filter the two are the same).

### Frames

Everything that moves once a frame (the tables, the vibrato, the release) runs at **FRAMES** `60 HZ` (59.73 Hz on
`BRK`, 60.10 Hz on `8BIT`, 60 Hz else) or `50 HZ`, counted in output samples from the note's start. Each frame, in
this order: the VOL table, the ARP table, the PITCH table and the vibrato, the DUTY table, the NOISE table, the
release; then the register is found again (unless a sweep owns it). The step envelope and the sweep keep their own
time. A frame's work, an envelope's or a sweep's step is done at the start of the 64-frame block it falls in (up to
1.45 ms early). **Frames never follow the song:** its tempo, ticks and SWING move the arpeggio `0xy` and the vibrato
commands, not the tables.

### Band-limited, in integers

- **Every step of the output is band-limited:** a change of the voice's amplitude at a moment inside a sample is
  added as a short windowed-sinc impulse (16 taps, 64 phases, cut at 0.45 of the sample rate, a Blackman window) to
  a running sum. A pulse at 1 kHz is about 3 steps a block, a triangle's or wave's 32 steps a period about 20 a
  block at 440 Hz.
- **Integers only:** times in 1/65,536 of a sample, products of 32 by 32 bits into 64, no float and no 64-bit
  division (the few divisions in the sound's loops are exact multiplies); amplitudes from the curves' tables; the
  kernel's taps summing to exactly one at every phase.
- **Its state is the voice's own** (the running sum, the 16 samples of impulse tail past the block, the filters, the
  registers): two voices of CHIP may render at once on the two cores, and a voice parked (unheard) keeps its time,
  ending as it would.

## The tables

An instrument does not store lists of numbers: it picks **tables built into the engine**, by number, and how fast
each runs. So an instrument is 28 bytes and a cartridge holds 128.

- **Five kinds of table,** each a list of values, one a step; `|` marks where it loops back to (without one it holds
  its last value); a step lasts SPEED frames (`X1`-`X4`). A table starts at the note's first frame.
  - **VOL** 0-15: a fraction of 15 of the level (attacks, decays, plucks, tremolos, gates, echoes);
  - **ARP** semitones added to the note (drum drops, chords, octaves, trills, runs, effects);
  - **PITCH** cents added (scoops, falls, detunes, wobbles);
  - **DUTY** 0-3, the pulse's width (sweeps, PWM);
  - **NOISE** 0-15, the noise's RATE.
- **VIBRATO** is not a table but three settings: DELAY 0-62 frames (in 2s), SPEED `X1`-`X4` (frames a step), DEPTH
  0-62 cents (in 2s); its cycle is 8 steps: 0, half, full, half, 0, −half, −full, −half.
- **The numbers are fixed for good,** as commands' are: table `01` keeps its values forever; a table added later
  takes the next free number (`01`-`3F` a kind, `00` meaning none). A number past the firmware's (a later firmware's
  table) plays as none. The tables' names (10 characters) are shown on the TABLES page.
- Custom tables, stored in the user area beside the built-in ones, are for later
  ([open-questions.md](open-questions.md)).

**The library:** 33 VOL, 26 ARP, 10 PITCH, 8 DUTY and 9 NOISE tables (`|` where each loops back to):

| VOL | Values | VOL | Values |
|---|---|---|---|
| `01 SETTLE 11` | 15 14 13 12 12 11 \| 11 | `12 SHAKE` | 4 9 13 9 5 2 0 |
| `02 SETTLE 10` | 15 13 12 11 10 10 \| 10 | `13 GATE 7` | 15 15 15 15 15 15 15 0 |
| `03 SOFT 10` | 14 12 11 10 10 \| 10 | `14 THUD` | 15 15 15 14 13 12 10 8 5 0 |
| `04 SETTLE 12` | 15 14 13 12 12 \| 12 | `15 CRASH` | 15 14 14 13 13 12 12 11 11 10 10 9 9 8 8 7 7 6 6 5 5 4 4 3 3 2 2 1 1 0 |
| `05 SETTLE 13` | 15 14 14 13 13 \| 13 | `16 COIN` | 15 15 15 15 15 15 14 13 12 11 10 9 8 7 6 5 4 3 2 1 0 |
| `06 ATTACK 14` | 15 15 14 14 \| 14 | `17 BELL` | 15 14 13 12 11 10 9 8 7 6 5 4 \| 4 |
| `07 ACCENT` | 15 11 10 10 \| 10 | `18 SWELL` | 3 5 7 9 11 13 14 15 \| 15 |
| `08 TICK` | 15 9 5 2 0 | `19 SWELL SLOW` | 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 \| 15 |
| `09 TICK SOFT` | 12 8 5 3 1 0 | `1A PAD` | 4 8 11 13 14 15 \| 14 13 13 14 15 15 |
| `0A SHORT` | 15 13 0 | `1B REVERSE` | 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 0 |
| `0B BLIP` | 15 12 9 6 3 0 | `1C TREMOLO` | \| 15 13 11 9 11 13 |
| `0C PLUCK` | 15 12 10 8 7 6 5 4 4 3 3 2 2 1 1 0 | `1D TREM DEEP` | \| 15 11 6 3 6 11 |
| `0D PLUCK SUS` | 15 12 10 9 8 \| 8 | `1E GATE 8TH` | \| 15 15 15 0 0 0 |
| `0E RING` | 14 12 10 8 7 6 5 4 3 2 1 0 | `1F STUTTER` | 15 0 15 0 15 0 12 0 9 0 6 0 3 0 |
| `0F FADE 13` | 12 11 10 9 8 7 6 5 4 3 2 1 0 | `20 ECHOES` | 15 12 9 0 0 0 9 7 5 0 0 0 5 4 3 0 0 0 3 2 1 0 |
| `10 SNARE` | 15 13 11 9 7 6 5 4 3 2 2 1 1 0 | `21 DOUBLE` | 15 10 0 0 15 10 6 3 1 0 |
| `11 CLAP` | 15 0 14 0 15 11 8 6 4 3 2 1 0 | | |

| ARP | Semitones | ARP | Semitones |
|---|---|---|---|
| `01 KICK DROP` | 24 12 5 0 -5 -9 -12 -14 -16 -17 | `0E OCTAVE` | \| 0 12 |
| `02 KICK FAST` | 12 5 0 -5 -8 -11 -13 | `0F OCT DOWN` | \| 0 -12 |
| `03 TOM DROP` | 7 4 2 0 -2 -3 -4 -5 | `10 TRILL` | \| 0 2 |
| `04 SNAP` | 7 3 0 -2 | `11 HALF TRILL` | \| 0 1 |
| `05 OCT FLICK` | 12 0 | `12 RUN UP` | 0 4 7 12 16 19 24 |
| `06 MAJOR` | \| 0 4 7 | `13 RUN DOWN` | 24 19 16 12 7 4 0 |
| `07 MINOR` | \| 0 3 7 | `14 SCALE UP` | 0 2 4 5 7 9 11 12 |
| `08 MAJOR 7` | \| 0 4 7 11 | `15 RISE TO` | -12 -7 -4 -2 -1 0 |
| `09 MINOR 7` | \| 0 3 7 10 | `16 FALL OFF` | 0 0 0 0 0 0 -1 -2 -4 -7 -12 -19 -24 |
| `0A SUS 4` | \| 0 5 7 | `17 LASER` | 24 21 18 15 12 9 6 3 0 -3 -6 -9 -12 |
| `0B DIMINISHED` | \| 0 3 6 | `18 POWER UP` | 0 4 7 12 4 7 12 16 7 12 16 19 12 16 19 24 |
| `0C POWER` | \| 0 7 12 | `19 ZAP` | 36 24 12 0 |
| `0D FIFTH` | \| 0 7 | `1A UP 2 OCT` | \| 24 |

| PITCH | Cents | DUTY | Widths | NOISE | Rates |
|---|---|---|---|---|---|
| `01 SCOOP` | -80 -60 -40 -25 -15 -8 -4 -2 0 | `01 SWEEP` | \| 2 2 2 2 1 1 1 1 0 0 0 0 1 1 1 1 | `01 SNARE` | 4 6 6 7 7 7 8 |
| `02 SCOOP FAST` | -50 -25 -10 0 | `02 PWM` | \| 0 1 2 3 2 1 | `02 RISE` | 6 6 6 5 5 5 4 4 4 3 3 3 2 2 2 1 1 1 0 |
| `03 FALL` | 0 0 0 0 0 0 0 0 -5 -10 -20 -35 -55 -80 -110 -127 | `03 FLUTTER` | \| 2 1 | `03 FALL` | 0 0 1 1 2 2 3 3 4 4 5 5 6 6 7 7 8 8 9 10 11 12 |
| `04 DIVE` | 0 -10 -25 -45 -70 -100 -127 | `04 OPEN` | 0 1 2 | `04 ZAP` | 0 2 4 6 8 10 12 14 |
| `05 BEND UP` | 0 5 10 20 30 45 60 80 100 127 | `05 CLOSE` | 2 1 0 | `05 THUMP` | 10 12 14 15 |
| `06 DETUNE UP` | \| 8 | `06 BUZZY` | \| 0 2 | `06 SWEEP` | \| 2 3 4 5 6 7 6 5 4 3 |
| `07 DETUNE DN` | \| -8 | `07 THIN OUT` | 2 2 2 1 1 1 0 | `07 CLAP` | 3 3 6 3 3 6 4 5 6 |
| `08 WOBBLE` | \| 0 25 50 25 0 -25 -50 -25 | `08 PULSE 2` | \| 1 1 0 0 | `08 STEAM` | \| 1 2 |
| `09 SIREN` | \| 0 30 60 90 120 90 60 30 | | | `09 BREATH` | \| 5 6 5 4 |
| `0A SHIVER` | \| 0 6 0 -6 | | | | |

### The waves

16 waves, `0`-`F` (all their 4 bits used), each one period of 32 steps drawn from a formula over x from 0 to 1 and
scaled to 0-15; a step's value in hex, the first step first:

| Wave | Its formula | Its 32 steps |
|---|---|---|
| `0 SAW` | 1 − x | `FFEEDDCCBBAA99887766554433221100` |
| `1 SQUARE` | 1 for x < 1/2, else 0 | `FFFFFFFFFFFFFFFF0000000000000000` |
| `2 SINE` | sin 2πx | `89ACDEEFFFEEDCA98653211000112356` |
| `3 TRIANGLE` | x for x < 1/2, else 1 − x | `01234567889ABCDEFEDCBA9887654321` |
| `4 FAT` | 0.65 (1 − x) + 0.35 sin 2πx | `CDDEEFFFFEEDCBA98654321100001122` |
| `5 ORGAN` | sine, octave × 0.5, two octaves × 0.3 | `8BEFFEDDEEDB97667998642112210014` |
| `6 HOLLOW` | sine, third harmonic × 0.7 | `8BEFFDBA9ABDFFEB8410024565420014` |
| `7 BELL` | harmonics 1, 5 × 0.6, 9 × 0.35 | `8BB9A99CFC99A9BB8446566303665644` |
| `8 PULSE 25` | 1 for x < 1/4, else 0 | `FFFFFFFF000000000000000000000000` |
| `9 PULSE 12` | 1 for x < 1/8, else 0 | `FFFF0000000000000000000000000000` |
| `A HALF SINE` | the sine's upper half | `0368BCEFFFECB8630000000000000000` |
| `B DOUBLE SAW` | (1 − x) + 0.5 (1 − 2x mod 1) | `FEEDCCBAA9887765A988776554332110` |
| `C VOX` | harmonics 1, 2 × 0.8, 3 × 0.6, 5 × 0.2 | `7BEFEDCBA98889A97656777654321014` |
| `D CLAV` | 0.4 sin 2πx + (1 for x < 0.3, else −0.2) | `CDDEEEFFFF6555443221110000011122` |
| `E WARM` | sine, octave × 0.25 | `89BDEFFFEEDCBA988765432110001246` |
| `F GRIT` | harmonics 1, 7 × 0.5, 13 × 0.4 | `8C8A8DFACAFD8A8C7375720535027573` |

## LEVEL and its reference

The engine is scaled so its reference sound (PULSE 50 %, CLOCK EXACT, CURVE BRK, FILTER NONE, VOL 15, no table, no
envelope, no vibrato; C-4) comes out at −20 dB RMS, as every engine's
([files/instruments.md](files/instruments.md#level-and-the-engines-reference)); a CHIP instrument's LEVEL is measured
as any.

## Costs

- **CPU** (counted on the device compiler's code; to measure on the FM-1), a 64-frame block: a pulse about 5-7 µs;
  a triangle or wave at A-4 about 14 µs, twice that an octave up; the fastest noise 25-35 µs (12 register steps a
  sample, stepped in runs). A Juno-106 voice is 30.5-48.7 µs, a DX7 23.5-40. Its ceiling: 128 + 128 + 64
  band-limited steps a block, 64 more with slow noise mixed in ([above](#the-clock-clock)). The debug build's BENCH times a PULSE at C-4, a TRIANGLE
  at A-4, a WAVE at C-7 and the fastest NOISE; its figures count CHIP's time with the samples'.
- **RAM:** none of its own. A voice's state (228 B) fits the voice slot; nothing static, no tables built at start.
- **Flash:** the engine's code about 7 KB, its tables 5 KB (the kernel 2 KB, the curves 0.5 KB, the tables and waves
  1.5 KB, the clocks', noise's and steps' 1 KB); the built-in cartridge 3.6 KB.
- **Core 2:** the engine runs from the image as the others do, its state the voice's alone, so a CHIP voice runs on
  either core.

## The playground's twin

The playground's CHIP is the firmware's integers in JS (whole numbers in doubles, every one exact; the same tables
from one generator), not floats. `fw/tests/run_chip.sh` renders every instrument of the built-in cartridge at four
notes, held and let go, bent and with a command, and random instruments, notes, parkings and commands, through both,
and requires them equal sample for sample; the screens' tuning too.

# Cartridges and instruments

Status: **settled** (2026-10-09). The record: [the files proposal](../proposals/202610080050-fs.md),
section 5.

Instruments live in cartridges: a cartridge is one record holding a bank of instruments of one
engine, DX7, Juno-106, TB-303, CHIP, sample instruments (with their samples' table) or FUSION. Cells refer
to an instrument as cartridge:slot (`0B:05`).

## A cartridge record

Kind: its type (0x01-0x06, [store.md](store.md#a-record)); number: the cartridge, `00`-`FF`.

| Bytes | Field |
|---|---|
| 0-8 | name, 12 characters |
| 9 | slots stored: up to the last one used (empty ones before it marked by their name) |
| 10- | the slots, each at its type's fixed size |

A sampler cartridge's record also holds its samples' table ([below](#samples-and-fusion-slots)).

| Type | Slots | Bits used | Slot size | Spare | A full cartridge |
|---|---|---|---|---|---|
| DX7 | 32 | 852 | 112 B | 44 bits | 3,594 B |
| Juno-106 | 128 | 191 | 28 B | 33 bits | 3,594 B |
| TB-303 | 128 | 110 | 24 B | 82 bits | 3,082 B |
| CHIP | 128 | 189 | 28 B | 35 bits | 3,594 B |
| FUSION | 64 | 76 | 16 B | 52 bits | 1,034 B |
| sampler | 64 instruments, up to 64 samples | 234 + 102 | 40 B + 16 B | 86 + 26 bits | 3,595 B |

- **Every instrument has a name** (10 characters, 60 bits) **and a LEVEL:** 8 bits, signed, in 0.5 dB
  steps (−64 to +63.5 dB).
- **Every slot has spare bits,** and every full cartridge leaves room in its block
  ([README.md](README.md#extending)).

### LEVEL and the engines' reference

The engines are scaled by design, never by data: **each engine's reference sound comes out at −20 dB
RMS** (the loudest 300 ms of a note held a second, at the voice's output: before the track's level and
pan, the effects and MASTER; full scale is the mix's). So the engines sound equally loud on their own,
and an instrument's LEVEL says only how it differs from its engine's plain sound, never how one engine
differs from another. LEVELs are measured against PT/FM's engines so scaled; the demo disk's bring each
instrument's loudest 300 ms to the same −20 dB.

**How a LEVEL is measured** (the demo disk's tools and the device's AUTO LEVEL alike,
[../ui/screens/instruments.md](../ui/screens/instruments.md#auto-level)): the instrument's note alone, at
unity gain (no LEVEL, no track), from a clean start: a new voice, after 2 s of silence (a Juno-106 chorus's
line empty, its LFO and the Juno-106's firmware loop where those 2 s leave them, its noise generator from
its power-on state); held 1 s of 1.2 s; mono as (L + R) / 2. Its note: C-4; a TB-303's C-2; a sample
instrument's its BASE NOTE. Its LEVEL brings its loudest 300 ms to −20 dB, at most 30 dB either way, its
peak kept under −6 dB, rounded to half a dB. An instrument made on another's sample (another envelope, or
retuned) may keep that one's LEVEL on a disk; AUTO LEVEL measures it as itself.

**Voices go into the mix at half** (−6 dB): room for many voices at once. The effects work on the mix at
that level (DRIVE's drive, DUCK's source, LIMITER's CEILING), so a reference sound is at −26 dB there.

| Engine | Its reference sound |
|---|---|
| DX7 | algorithm 32, operator 1 alone: OUTPUT LEVEL 99, ratio 1.00, its EG rates 99 and levels 99, 99, 99, 0; the other operators' OUTPUT LEVEL 0; feedback 0, no LFO, no velocity or keyboard scaling; C-4 |
| Juno-106 | SAW alone, 8'; VCF FREQ 127, RES 0, ENV 0, LFO 0, KYBD 0; HPF 1 (flat); VCA ENV, LEVEL 127; ENV A 0, D 0, S 127, R 0; chorus off; C-4 |
| TB-303 | WAVE 0 (saw), CUTOFF 127, RESONANCE 0, ENV MOD 0, DECAY 64, ACCENT 0; C-2 (DECAY matters: the main envelope is part of the 303's amp) |
| sampler | a full-scale sine (8 bits, peaks ±127, 441 Hz) looped whole, at its BASE NOTE; TUNE 0, VOLUME 64, the envelope's rates 99 and levels 99, 99, 99, 0 |
| CHIP | PULSE 50 %, CLOCK EXACT, CURVE BRK, FILTER NONE, FRAMES 60 HZ, VOL 15, ENV NONE, RELEASE 0, no table, no vibrato, no noise, no sweep; C-4 |

## DX7

The 145 parameters of Yamaha's packed voice, in its order (operator 6 first, then 5 to 1, then the
voice), each in the bits its range needs; values past 99, as some factory voices have, are kept.

| Part | Fields (bits) |
|---|---|
| each operator, 113 bits | R1-R4 (7 each), L1-L4 (7 each), BREAK POINT 7, LEFT DEPTH 7, RIGHT DEPTH 7, LEFT CURVE 2, RIGHT CURVE 2, RATE SCALING 3, AMP MOD SENS 2, KEY VEL SENS 3, OUTPUT LEVEL 7, MODE 1, COARSE 5, FINE 7, DETUNE 4 |
| the voice, 106 bits | pitch R1-R4, L1-L4 (7 each), ALGORITHM 5, FEEDBACK 3, OSC KEY SYNC 1, LFO SPEED 7, LFO DELAY 7, LFO PITCH DEPTH 7, LFO AMP DEPTH 7, LFO KEY SYNC 1, LFO WAVE 3, PITCH MOD SENS 3, TRANSPOSE 6 |
| then | name 60, LEVEL 8, spare 44 |

A `.syx` cartridge converts both ways exactly, except the bits the DX7 itself ignores and the
names' characters outside the names' set ([README.md](README.md#names-numbers-and-bits): lowercase
becomes capitals, any other a space).

## Juno-106 and TB-303

| Engine | Fields (bits) |
|---|---|
| Juno-106 | the 16 sliders in the Juno-106's order (7 each); RANGE 2 (16', 8', 4'), PULSE 1, SAW 1, CHORUS 2 (off, I, II), PWM SOURCE 1, ENV POLARITY 1, VCA MODE 1, HPF 2; name 60; LEVEL 8; spare 33 |
| TB-303 | WAVE, CUTOFF, RESONANCE, ENV MOD, DECAY, ACCENT (7 each); name 60; LEVEL 8; spare 82 |

A Juno-106 patch dump converts both ways exactly.

## CHIP

The engine is [../chip.md](../chip.md). Its fields, in this order from the slot's first bit, each least
significant bit first:

| Field | Bits | Values |
|---|---|---|
| OSC | 3 | 0 PULSE, 1 TRIANGLE, 2 WAVE, 3 BUZZ, 4 NOISE |
| DUTY | 2 | 0 12.5 %, 1 25 %, 2 50 %, 3 75 % |
| BUZZ SHAPE, STEPS, GATE | 2, 1, 1 | 0 SAW DN, 1 SAW UP, 2 TRI; 0 16, 1 32; 0 off, 1 on |
| NOISE: mixed in, MODE, RATE, KEY | 1, 1, 4, 1 | off / on; LONG / SHORT; 0-15; off / on |
| CLOCK | 3 | 0 BRK, 1 8BIT, 2 PSG 1.77, 3 PSG 1.00, 4 PSG 1.79, 5 16BIT, 6 EXACT |
| CURVE | 2 | 0 BRK, 1 8BIT, 2 PSG, 3 16BIT |
| FILTER | 2 | 0 BRK, 1 8BIT, 2 PSG, 3 NONE |
| FRAMES | 1 | 0 60 HZ, 1 50 HZ |
| SWEEP, SHIFT, PACE | 2, 3, 3 | 0 OFF, 1 DOWN, 2 UP; 0-7; 0-7 |
| VOL, ENV, STEP, RELEASE | 4, 2, 3, 4 | 0-15; 0 NONE, 1 DOWN, 2 UP, 3 PSG; 0-7; 0-15 |
| PSG ENV: SHAPE, PERIOD | 4, 16 | 0-F; 1-65,535 (0 reads as 1) |
| WAVE | 4 | the wave, 0-F |
| VOL, ARP, PITCH, DUTY, NOISE: table, SPEED | 6 + 2 each | `00` none, `01`-`3F`; `X1`-`X4` stored 0-3 |
| VIBRATO: DELAY, SPEED, DEPTH | 5, 2, 5 | 0-62 frames stored halved (0-31); `X1`-`X4` stored 0-3; 0-62 cents stored halved |
| name, LEVEL | 60, 8 | as every instrument |
| spare | 35 | |

**A new CHIP instrument** (INIT) is the reference sound: PULSE 50 %, CLOCK EXACT, CURVE BRK, FILTER NONE,
FRAMES 60 HZ, VOL 15, ENV NONE, STEP 0, RELEASE 0, no table (each at `X1`), no vibrato (DELAY 0, `X1`, DEPTH
0), LEVEL 0; the fields it does not use: BUZZ SAW DN, 16, GATE off; NOISE off, LONG, RATE 4, KEY off; SWEEP
OFF, SHIFT 2, PACE 2; PSG ENV SHAPE 9, PERIOD `0320`; WAVE 0 (SAW).

## Sample instruments

| Field | Bits |
|---|---|
| SAMPLE: its index among the cartridge's samples | 6 |
| START, END, LOOP START, LOOP END: positions in the sample | 20 each |
| LOOP: OFF, FORWARD, PING-PONG | 2 |
| BASE NOTE | 7 |
| TUNE, in 1/128 of a semitone, 0x80 none | 8 |
| LEVEL | 8 |
| VOLUME: a module's default volume, 0-64 | 7 |
| the envelope: R1-R4 and L1-L4, as the DX7's | 56 |
| name | 60 |
| spare | 86 |

- **The envelope works as a DX7 operator's:** from silence it moves to L1 at R1, to L2 at R2, to L3
  at R3, and holds L3 while the key is down; at release it moves to L4 at R4.
- **The loop is a sustain loop:** playing goes from START into LOOP START - LOOP END, and repeats it
  while the key is held. At release it goes on from where it is, through the rest of the loop (a
  ping-pong pass going backwards back to LOOP START, then forwards) and on to END, while the envelope
  releases. Nothing jumps, so nothing clicks.
- **A new instrument** plays its sample whole: START 0, END its length, LOOP OFF.

## Samples and FUSION slots

- **A sampler cartridge's record** is: name (9 B), instruments stored (1 B), samples stored (1 B), the
  instruments (40 B each), the samples' table (16 B each). A sample's entry: its length in samples
  (20 bits), its rate in Hz (18 bits; an instrument's BASE NOTE plays it at that rate), its coding
  (4 bits: 1 PTR 8-bit, 2 PTR 16-bit; [ptr.md](ptr.md)), its name (60 bits), spare (26 bits). A sample
  has no loop of its own: loops belong to instruments, and one sample can serve several of them.
- **A FUSION slot:** its pattern (8 bits; [patterns.md](patterns.md#fusion-patterns)), name, LEVEL,
  spare 52.

## Built-in instruments

Settled 2026-10-10 ([the proposal](../proposals/202610102045-builtin.md)). The firmware carries the demo
disk's synth instruments ([../../demo-disk/README.md](../../demo-disk/README.md)), so they cost the user
no space.

- **Which:** the eight DX7 cartridges, JUNO-106, CHIPTUNE and TB-303. Never samples, songs or FUSION: the
  sampler cartridges, their audio, FUSION with its patterns (its layers take the samplers' sounds too) and
  the demo songs are only ever uploaded ([disk.md](disk.md#the-demo-disk)).
- **Their numbers: `F0`-`FF` belongs to the firmware,** for cartridges and for patterns alike: a range
  kept for the built-in instruments, the firmware's cartridges in it. A record there is only a changed
  built-in, and no NEW, CLONE or MOVE takes one ([store.md](store.md#a-record)); the user's cartridges and
  patterns have `00`-`EF`. Pattern `FE` is the EDITOR's own, `FF` "no pattern"
  ([patterns.md](patterns.md#numbers)).

  | Number | Cartridge | Patterns |
  |---|---|---|
  | `F0`-`F7` | the DX7 cartridges: KEYS/ORGANS, BELLS/PLUCKS, GUITAR/STRGS, WINDS/BRASS, PADS/CHOIRS, LEADS/BASSES, FM DRUM KIT, PERC/FX | `F0`-`FD`: for later built-in patterns |
  | `F8` | JUNO-106 | |
  | `F9` | CHIPTUNE (CHIP, 128) | |
  | `FA` | TB-303 | |
  | `FB`-`FF` | for later built-in instruments | `FE` the EDITOR's own |

- **In FWS, the firmware space, and edited as anything else** ([../ui/saving.md](../ui/saving.md#fws)):
  a change (a slot's parameters, a name, a LEVEL, AUTO LEVEL) is saved as a record in the user area under
  the same number, found before the firmware's own from then on; BACK TO BUILT-IN deletes that record and
  the firmware's own is back. They never MOVE, and DELETE does not take them away. COPY and PASTE of a slot,
  and CLONE of a cartridge to a free number, make a copy in the user area as with any other.
- **The firmware's own are frozen once released:** songs refer to them by number (`F0:05`), so the sound
  a built-in number has as shipped never changes (a user's change is that user's, on that FM-1). A later
  firmware may add built-in instruments only at the range's free numbers; it never moves, changes or
  removes one ([README.md](README.md#extending)). So nothing the user made ever needs a new number for
  them.
- **Read in place, as the firmware's own data:** as fast as the user area; no records, no scan, no RAM.
  They are in the record's own layout, so every reader takes them as it takes a cartridge from the
  flash.
- **Their licences travel with the firmware:** ABOUT ([../ui/screens/settings.md](../ui/screens/settings.md))
  and the install page list them.
- **Size:** 36 KB, in a firmware slot of 568 KB; what the firmware leaves of the slot is FWS's free part,
  the room it grows into ([store.md](store.md#where-things-go)).

## Deleting

- **Deleting an instrument never deletes its sample.**
- **Deleting a sample** (its cartridge's samples list, SEL → DELETE) warns first: `DELETE SAMPLE?
  3 INSTRUMENTS USE IT AND ARE DELETED TOO`. Accepted, the sample and those instruments are deleted in
  one save; cells that played those instruments point at empty slots
  ([references.md](references.md)).
- **CLEAN UP** (SETTINGS → TOOLS, [../ui/screens/settings.md](../ui/screens/settings.md)) also deletes
  samples no instrument uses.

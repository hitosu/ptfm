# The demo disk

The demo disk's sources: patch cartridges, sampler cartridges, FUSION instruments and songs, under free
licences, to show what PT/FM does and to start playing right away. Everything here may be passed on by
anyone; [CREDITS.md](CREDITS.md) lists every licence and author. The packer (`tools/pack.py`) builds the
demo disk (`dist/firmware/demo.ptfmdsk`, a disk: [../specs/files/disk.md](../specs/files/disk.md)) from these files; the
playground has their content built in (`tools/gen_playground.py`).

- **The notation follows the files** ([../specs/files/](../specs/files/README.md)): every field here is
  one of their records' fields, in words and plain numbers, so a file converts to its record and back.
- **The rule:** every bank, voice taken from elsewhere, sample and song carries its licence, and only
  licences that let anyone pass the work on, changed or not, commercially or not: CC0, the Public Domain
  Mark, CC BY, CC BY-SA, MIT, BSD, Apache-2.0, GPL, the Unlicense. No NonCommercial or NoDerivatives
  licences, no "royalty-free" packs, nothing without a stated licence.
- **The engines make what they can; samples are what they cannot:** drums of the drum machines, chip
  sounds, pads and effects are DX7, Juno-106 and TB-303 patches (and FUSION layers of them). Samples are
  recordings of natural instruments (an acoustic kit, hand percussion, band and orchestra instruments), of
  real voices, and the basses the engines cannot make (dubstep wobbles and growls: the engines have no
  distortion of their own).
- **Our own work here is CC0:** our DX7 voices, the Juno-106 patches, our TB-303 presets, the FUSION
  instruments, the cartridges' choice of voices and their names, and the songs.
- **Made by the tools in `tools/`** (below); edit those, not the files they write.

## What it holds

The synth cartridges are the firmware's **built-in instruments**, numbered `F0`-`FA` in the range `F0`-`FF` kept
for them; the sampler cartridges, FUSION, the samples and the songs are the **uploaded disk**, from `01`, FUSION's
patterns `E0`-`E5` and the songs' from `00` ([../specs/files/instruments.md](../specs/files/instruments.md#built-in-instruments),
[../specs/files/disk.md](../specs/files/disk.md#the-demo-disk)).

| No. | Cartridge | Engine | Slots | What |
|---|---|---|---|---|
| 01 | DRUM KIT | sampler | 10 | an acoustic kit: kick, snare, hats, crash, shaker, three toms |
| 02 | PERCUSSION | sampler | 11 | wind chimes, darbuka, three congas, bongo, tabla, timpani |
| 03 | BAND/ORCH | sampler | 15 | piano, vibraphone, guitars, violins, tenor sax, flute, upright bass, and muted or short strokes of them |
| 04 | VOICES | sampler | 6 | dance shouts, a warning, a count-in |
| 05 | BASS | sampler | 4 | dubstep basses (a wobble, a "yoi" and a growl), a drum and bass bass |
| 06 | FUSION | FUSION | 6 | SPACE PAD, DUB CHORDS, SPLIT, CHIPIN, COIN, DRUM KIT |
| F0 | KEYS/ORGANS | DX7 | 32 | pianos, electric pianos, clavinets and harpsichords, organs, an accordion |
| F1 | BELLS/PLUCKS | DX7 | 32 | marimbas, vibraphones, glockenspiels, celestas, bells and chimes, plucks |
| F2 | GUITAR/STRGS | DX7 | 32 | guitars, harps, kotos, a sitar, a banjo; strings, solo and in sections |
| F3 | WINDS/BRASS | DX7 | 32 | brass, flutes and pipes, reeds and saxophones, bagpipes |
| F4 | PADS/CHOIRS | DX7 | 24 | choirs and voices, pads, an orchestra hit |
| F5 | LEADS/BASSES | DX7 | 32 | saw, square, sync and sine leads; FM, synth and acoustic basses |
| F6 | FM DRUM KIT | DX7 | 32 | kicks, snares, claps, hats, cymbals, toms and hand drums |
| F7 | PERC/FX | DX7 | 32 | percussion (triangles, whistles, agogo, surdos), effects |
| F8 | JUNO-106 | Juno-106 | 128 | keys, organs, mallets, plucks, strings, choirs, brass, pads, stabs, leads, basses, drums, effects |
| F9 | CHIPTUNE | CHIP | 128 | the sound chips of the 8-bit era, four families (BRK, 8BIT, PSG, 16BIT): leads, echoes and keys, arpeggios and chords, basses, pads and bells, kicks and snares, hats and percussion, effects |
| FA | TB-303 | TB-303 | 64 | acid: saws, squares, squelches, basses, plucks, sweeps |

- **The DX7 voices** are our own (`tools/dx7_voices.py`) and free banks' (each credited in `CREDITS.md`):
  three banks' factory voices (MIT, GPL-3.0-only), Idiomdrottning's classix (CC BY-SA 4.0), and two
  sets of 4-operator patches converted to the DX7: ulalume's ym2612-patches (Mega Drive, CC0) and the
  Commander X16 ROM's (YM2151, BSD-2-Clause). A cartridge's slots are listed in `tools/dx7_carts.py`.
- **The TB-303 presets** are our own and six of mpc-vst-303's (GPL-3.0-only).
- **The samplers** keep 8-bit audio; instruments on one sample share it (the toms are one tom retuned, the
  congas one conga, a choked crash, muted strokes, short stabs and slow swells are other envelopes on the
  same samples).
- **FUSION:** SPACE PAD (two pads and a deep bass on every key, a bell from C-4), DUB CHORDS (a minor 9
  chord, rolled, a sub bass on every key), SPLIT (a held bass under C-4, a space lead from C-4), CHIPIN (a
  chip arpeggio on any key in C minor pentatonic, its echo, a triangle bass on C, a blip), COIN (a game's
  coin on any key: CHIPTUNE's COIN PULSE, a 50 % square, a fourth under the key for 5 ticks, then the key's
  note ringing out), DRUM KIT (a drum on
  every key from F-2 to G-8, layers of the synth cartridges' drums and the DRUM KIT and PERCUSSION samplers'
  (every sample of the two plays somewhere): General MIDI's kit two octaves up, the kick on C-4, B-3 to G-5 as
  GM's 35-55, hand percussion on the six keys under it; combos on the octave under (fills, rolls, a flam, a
  drag, DUM-TSS); on the octave over, the DRUM KIT sampler's drums alone, a drum a key on the kit's letters (C
  the kick, D the snare, F# and A# the hats, four toms rising between); the TB-303's drums on the next, hand
  drums and chimes on the one over that (timpani, darbuka, conga, bongos, tablas); each drum rings out however
  short the tap, its release made its decay in its cell, but CHIME RISE, a swell to hold).
- **Songs:** six demo songs (`tools/songs.py`), each showing one part of the disk in one style, six patterns
  of 8 bars played once (about 1:15 to 2:00), stopping at the end after an outro, the same loudness:

| # | Song | Shows | Style |
|---|---|---|---|
| 00 | PUNCH IN | the effects | dub techno into progressive house, F minor, 124 BPM; DUCK, FILTER, GATE, the punch-ins, a VINYL STOP ending |
| 01 | SPICE ROAD | the samplers | a band in D Dorian turning to D Hijaz: tablas, flute, a tenor sax solo |
| 02 | MIDNIGHT RUN | the JUNO-106 | outrun synthwave, F# minor, 130 BPM; a galloping bass, the VCF, CHORUS and PWM turned by commands, a lead solo |
| 03 | CITY LIGHTS | the DX7 cartridges | 80s synthpop, D minor, 120 BPM; a sequenced FM bass, a staccato hook, brass stabs, ORCH HITs |
| 04 | ACID TEST | the TB-303 | acid techno into acid trance, A minor, 138 BPM; accents, slides, the knobs by commands, the DRIVE |
| 05 | ADVENTURE | CHIPTUNE | game music through time, 132-160 BPM: one theme through the eras of the sound chips, each with its chips, key and tempo; a time machine between them, coins, home again |

Every song is spread across the stereo picture by its tracks' pans, notes panned one by one (`8xx`) and the
DELAY's PING-PONG; the kick, the bass, the snare and the lead stay in the middle.

- **DJ loops:** twelve loops for DJ's decks ([../specs/ui/screens/dj.md](../specs/ui/screens/dj.md)), also
  in `tools/songs.py`: a pattern of 8 bars that loops (AT THE END = LOOP), or two: DNB LOOP's, ROCK LOOP's,
  METAL LOOP's and CHIP LOOP's a verse and a bridge. **Each track plays one instrument,** so LOOPS loads a pattern of them as loops, a
  loop a track ([../specs/ui/screens/loops.md](../specs/ui/screens/loops.md#storing-and-loading)); a track's
  note ends before its next one (a note off, or a NOTE CUT), and a drum ends where the one after it on the
  kit would have choked it, so a loop sounds **4 voices at most** and two decks 8, never taking each
  other's. All in A minor, so any two mix in key (WORLD LOOP's flute aside: its B flat and C sharp go with
  the others' drums and bass, not their hooks); nine at 120 to 128 BPM, so SYNC has a tempo to match,
  TRANCE LOOP at psytrance's 140, and **HIP-HOP LOOP at 86 with DNB LOOP at 172**, a pair SYNC matches at
  half or double
  ([../specs/ui/screens/dj.md](../specs/ui/screens/dj.md#tempo-and-sync)); bar 8 a fill, so the loop's
  start is heard. **Mastered by the instruments' jobs** (each loop's mix in `tools/songs.py`: each instrument's
  loudness against the kick, or the lead in ROCK LOOP and METAL LOOP, as a mix engineer sets it, by genre:
  hats 12 dB under, pads 8, the bass 1-4), and all twelve at the same **-26.0 LUFS**, the most the quietest
  reaches with its loudest track full, so any two match on the decks. **The same tracks in all twelve:** 1 the kick (WORLD LOOP's under
  the darbuka), 2 the hats (WORLD LOOP's the hand drums), 3 the bass, 4 the hook (ACID LOOP's 303 is both,
  on 3; its 4 the claps), then the other drums and sounds, a track each, so a bass is swapped by the same
  mute on any of them. **TECHNO LOOP, ACID LOOP, DISCO LOOP, DNB LOOP, ROCK LOOP and METAL LOOP have an
  intro** of 8 bars, played once, to mix in over another deck, with no bass: the drums alone for four bars,
  then the hook coming in (DNB LOOP's the hats and the pad, then the break; ROCK LOOP's and METAL LOOP's
  the riff palm-muted); they loop from position 01, so their CUES have two parts (three with a bridge).
  The others are the loop alone.

| # | Song | Shows | Style |
|---|---|---|---|
| 06 | TRANCE LOOP | the JUNO-106 | psytrance, 140 BPM; the Tech Kick, the Seq Bass rolling in threes after each kick (kick, bass, bass, bass), sixteenth hats with open ones on the offbeats and claps, a Psy Pluck squelching in threes across the bar, its VCF opening and closing by commands, a Laser Zap into the loop's start |
| 07 | BREAKS LOOP | the samplers | breakbeat, 124 BPM; the DRUM KIT's break, the UPRIGHT walking, a VIBRAPHONE riff |
| 08 | TECHNO LOOP | the DX7 | techno, 126 BPM; the FM DRUM KIT, the FM BASS rolling after the kick, a PROG PLUCK in dotted eighths brightening and darkening by commands; its intro the drums, then the pluck coming in dark, no bass |
| 09 | ACID LOOP | the TB-303 | acid house, 128 BPM; a 303 line with accents and slides, its CUTOFF opening over four bars and closing over four; its intro the drums, then the 303 coming in nearly closed |
| 10 | DISCO LOOP | the DRUM KIT and the JUNO-106 | disco, 120 BPM; four on the floor, open hats on the offbeats, Syn Tom fills; the Funk Bass in octaves on Am7 and D9, a Wah Clav riff in sixteenths, its VCF opening and closing by commands; its intro the kick and hats, then the snares and the clav coming in dark |
| 11 | CHIP LOOP | CHIPTUNE | chiptune, 125 BPM; a verse and a bridge: the triangle kick, the console's noise groove, a sync-buzzer bass, chords arpeggiated on one voice by `0xy` in 3-3-2, each pattern ending on a run |
| 12 | HIP-HOP LOOP | SWING and LO-FI | boom bap, 86 BPM, SWING 58 %; the DRUM KIT, the DX7's AC BASS after the kick and a TINE EP phrase, all through the LO-FI |
| 13 | DNB LOOP | the BASS and VOICES samplers | drum and bass, 172 BPM; the DRUM KIT's two-step, hats and a shaker, the DNB BASS, a JUNO-106 Space Pad; then a bridge: the same break and bass, the BASS cartridge's WOBBLE 140 in the pad's place, low (E1 two bars at a time, three times), opening slowly, then the Space Pad's C5 finishing (the loop: positions 01 and 02); its intro the hats and the pad, then the break, R U READY at its end |
| 14 | ROCK LOOP | the guitars | rock, 126 BPM; the DRUM KIT with tom fills, the DIST GTR's power chords and palm mutes in a riff, the DX7's DIST GTR on a hook with bends (TONE PORTA) and vibrato; then a bridge: power chords ringing on C G D A, a half-time beat, the lead soaring (the loop: positions 01 and 02); its intro the beat, then the riff palm-muted |
| 15 | METAL LOOP | the guitars | metal, 128 BPM; the double kick in sixteenths, the GTR MUTE galloping palm-muted for two beats a bar, then the DX7's DIST GTR struck and held for two with vibrato, three hits (A C D) closing the verse, the DIST GTR's recorded power chord tremolo-picking a line (its middle looping while a note is held); then a bridge: a half-time breakdown, choked crashes, the riff chugging, the lead bending long notes (the loop: positions 01 and 02); its intro the drums, then the gallop palm-muted |
| 16 | ELECTRO LOOP | the JUNO-106's 808 sounds | electro, 126 BPM; the 808 Kick broken, the 808 Snare, hats and a clave, a Seq Bass in sixteenths, the TIMPANI booming a figure answered by a robotic Seq Lead, a Laser Zap into the loop's start |
| 17 | WORLD LOOP | the PERCUSSION sampler | world house, 120 BPM, after SPICE ROAD's turn; the kick on every beat, the darbuka with tabla strokes, the UPRIGHT on A and E, the FLUTE's melody round B flat and C sharp, its last bar a pickup into its first |

## Files

| Path | What |
|---|---|
| `index.json` | what the disk holds: its cartridges and songs, with their numbers |
| `patches/dx7/<cartridge>.json` | the 8 DX7 cartridges (`keys-organs`, `bells-plucks`, `guitar-strings`, `winds-brass`, `pads-choirs`, `leads-basses`, `fm-drum-kit`, `perc-fx`) |
| `patches/juno106/juno-106.json` | the JUNO-106 cartridge (`chip.json` beside it: the Juno-106 chiptune patches that were the CHIP cartridge, not built) |
| `patches/chip/chiptune.json` | the CHIPTUNE cartridge: 128 CHIP instruments and their LEVELs |
| `patches/tb303/tb-303.json` | the TB-303 cartridge |
| `samples/<set>/` | the sampler cartridges (`drum-kit`, `percussion`, `band-orch`, `voices`, `bass`): `samples.json` (the samples and the instruments) and a FLAC file a sample |
| `fusion/fusion/` | the FUSION cartridge (`fusion.json`) and its patterns (`patterns.json`) |
| `songs/<song>/` | the songs: `song.json` and `patterns.json` |
| `tools/` | the tools that make all of it |

All files are JSON (ASCII), and FLAC for audio. Every JSON file starts with `"format"`
(`demo-disk/<kind>`) and `"version"`.

## `index.json`

```json
{"format": "demo-disk/index", "version": 2, "name": "DEMO",
 "cartridges": [{"number": 1, "type": "dx7", "path": "patches/dx7/keys-organs.json", "name": "KEYS/ORGANS"}, ...],
 "songs": [{"number": 0, "path": "songs/<song>/song.json", "name": "..."}, ...]}
```

- **`number`:** the cartridge's number (`01`-`FF`) or the song's (`00`-`FF`) on the device.
- **`type`:** `dx7`, `juno106`, `tb303`, `sampler` or `fusion`.
- **Patterns** live in one pool, `00`-`FF`, as on the device: each song's and FUSION's files hold the
  patterns they use, with their numbers, and no number is used twice.

## Licences

```json
"license": {"id": "CC0-1.0", "credit": "Kick 1 by someone, from somewhere", "url": "https://..."}
```

- **`id`** is an SPDX identifier; **`credit`** the attribution (given for CC0 too); **`url`** where the
  licence is stated for that work.
- **At the top of a bank, a FUSION cartridge or a song,** and on every sample. A bank's covers the
  cartridge itself (its choice of voices and their names) and every slot without one of its own.
- **A voice, patch, preset or sample instrument may carry its own,** the bank's being the default: a
  themed cartridge holds voices of several sources.
- **`source`** (works from elsewhere): the file it came from (within its repository or download), the
  repository's `commit` (when it came from one) and the file's `sha256`; where the file holds several,
  which one: `voice` (its name there; an X16 patch its number and title), `preset`; `not_kept`, settings
  of the original that PT/FM's engine does not have.

## Patch banks

Every value as the instrument's panel shows it, with names instead of codes; **`level`** (dB, in 0.5 dB
steps, 0 when left out) is the instrument's LEVEL, set so every instrument sounds equally loud
(`tools/levels.py`): measured on the engines scaled to their reference sounds, so it says how the
instrument differs from its engine's plain sound ([LEVEL and the engines' reference](../specs/files/instruments.md#level-and-the-engines-reference)).

- **DX7** (`voices`, up to 32): `name` (10 characters, padded as stored), `algorithm` 1-32, `feedback` 0-7,
  `osc_key_sync`, `transpose` (-24..+24), `pitch_eg` (`rates`, `levels`: 4 each, 0-99), `lfo`
  (`speed`, `delay`, `pm_depth`, `am_depth` 0-99, `key_sync`, `wave`, `pitch_mod_sens` 0-7), and 6
  `ops`, **OP1 first**: `eg`, `level` 0-99, `mode` (`ratio` / `fixed`), `coarse` 0-31, `fine` 0-99,
  `detune` -7..+7, `rate_scaling` 0-7, `amp_mod_sens` 0-3, `key_vel_sens` 0-7, `kbd` (`break_point`,
  `left_depth`, `right_depth`, `left_curve`, `right_curve`). They convert to the DX7's 128-byte packed
  voice and back exactly; `ignored_bits` (a voice's or an operator's: `{byte: bits}`) keeps bits the DX7
  ignores where a source's packed voice had them set.
- **Juno-106** (`patches`, up to 128): `label` (`A11`..`A88`, then `B11`..`B88`), `name`, `lfo` (`rate`,
  `delay`), `dco` (`lfo`, `pwm`, `noise`, `sub` 0-127; `range` `16'` / `8'` / `4'`; `pulse`, `saw`;
  `pwm_source` `lfo` / `manual`), `hpf` 0-3, `vcf` (`freq`, `res`, `env`, `lfo`, `kybd` 0-127;
  `env_polarity` `+` / `-`), `vca` (`level`; `mode` `env` / `gate`), `env` (`a`, `d`, `s`, `r`), `chorus`
  (`off`, `I`, `II`).
- **TB-303** (`patches`, up to 128): `label` (`TB01`..), `name`, `wave` (0 saw - 127 square), `cutoff`,
  `reso`, `envmod`, `decay`, `accent` (0-127).

A slot is its place in the list: the first patch is slot `00`. A name is as the device shows it (lowercase
shows as capitals).

## Sampler cartridges: `samples.json` and a FLAC file a sample

`name` (the cartridge's, 12 characters), `samples` and `instruments` (up to 64 each).

**A sample:** `id` (two letters for its source and a number: `VC01`), `file`, `name`, `kind` (`kick`
`snare` `hat` `crash` `perc` `bass` `lead` `pad` `pluck` `fx` `voice`), `length` (in samples), `rate` (its
own rate in Hz: an instrument's BASE NOTE plays it at that rate), `bits` (8 or 16: how the device keeps it,
PTR 8-bit or 16-bit, [../specs/files/ptr.md](../specs/files/ptr.md); 8 when left out), `license`, `source`;
`optimize` (8-bit samples, picked by ear against the master): `soften`, a 2-pole low-pass at that fraction of the
rate, and `gain` in dB (0 or less), applied to the master before it is kept at 8 bits, so PTR codes it in fewer
bits; the build raises the LEVEL of the instruments on it by as much (their `level` stays the one measured on the
master), so they sound as loud as before. The editor's OPTIMIZE makes the same.

- **The audio:** mono, a 16-bit FLAC file: the master. Kept at 8 bits, each value is rounded to the
  nearest 256th: (v + 128) >> 8, at most 127. (A master of 8-bit values × 256 reads back exactly.)
- **How they were made** (`tools/import_samples.py`): mixed to mono at a working rate (22,050 Hz; 11,025
  for low ones); one-shots from the first sound to where it falls 42 dB under its loudest; sustained
  sounds a loop where the waveform matches best, crossfaded; the dubstep basses a sustain loop of whole
  bars at their tempo; then, the flash being small, a one-shot's tail under -45 dB of its loudest cut and
  the rate lowered where what it loses is under -36 dB of the whole (8,000 to 22,050 Hz).
- **Their notes were measured** on the recordings (two pitch detectors agreeing): the libraries name
  their files in more than one octave convention, so a name can be an octave off. A recording a little
  off its note got a TUNE that brings it in.

**An instrument** (its slot is its place in the list); several can play one sample:

| Field | What |
|---|---|
| `name` | 10 characters |
| `sample` | the sample's `id` |
| `start`, `end` | the part of the sample it plays (`end` exclusive) |
| `loop` | `off`, `forward` or `ping-pong`, then `loop_start` and `loop_end` (`loop_end` exclusive) |
| `base_note` | the note that plays the sample at its own rate (`C-4`) |
| `tune` | in 1/128 of a semitone, -128..127 (0: none; + raises) |
| `level` | dB, in 0.5 dB steps |
| `volume` | a module's default volume, 0-64 |
| `envelope` | `rates` R1-R4 and `levels` L1-L4, 0-99, as a DX7 operator's |

The loop is a sustain loop: it repeats while the note is held, and at release the sound goes on to
`end` while the envelope releases.

## FUSION cartridges: `fusion.json` and `patterns.json`

`name`, `license`, `slots`: each `name`, `pattern` (its number in the pool), `level` (dB: the
instrument's LEVEL, over its cells' volumes). A FUSION pattern has 128 rows: row *r* plays for MIDI note
*r* (`C-4` is row `3C`; rows `00`-`0B` never play); every cell of the row sounds when that note plays,
each a voice, with its volume and commands (`0xy` arpeggio, `EDx` note delay, `E9x` retrigger, `ECx` note
cut, `Axy` volume slide run tick by tick from the key's press).

## Songs: `song.json` and `patterns.json`

**`song.json`:**

| Field | What |
|---|---|
| `name`, `what`, `license` | 16 characters; a line on the song; its licence |
| `tempo` | TEMPO, 32-300: a tick lasts 2.5 s / TEMPO |
| `ticks_a_row`, `rows_a_beat` | 1-31 and 1-16 (6 and 4: a row is a 1/16 note, and the musical tempo is TEMPO) |
| `swing` | 50-75 (%): the first row of each pair lengthened, the second shortened |
| `at_the_end` | `loop` (from `loop_from`, a position) or `stop` |
| `slides` | `linear` or `amiga` (periods, as MOD and S3M slide); `fast_volume_slides` (S3M's) |
| `global_volume` | at the start, 0-64 |
| `positions` | a pattern number for each position |
| `mixer` | a track each, from track 1: `level` 0-127 (127 full), `pan` 0-255 (128 the middle), `duck` 0-127, and the sends `chorus`, `delay`, `reverb` 0-127 |
| `fx` | the FX setup (below) |

**`fx`:** `routing` (`global` or `sends`), `chain` (the 9 items in their order: `GATE`, `FILTER`,
`DRIVE`, `LO-FI`, `CHORUS`, `DELAY`, `REVERB`, `LIMITER` and `SENDS`, where the sends' returns join
when `routing` is `sends`), then each effect with `on` and its knobs, as the FX screen shows them:

| Effect | Knobs |
|---|---|
| `gate` | `pattern` 1-32, `rate` (`1/8` `1/16` `1/32` `1/8T` `1/16T`), `depth`, `smooth` (%) |
| `filter` | `cut` (`LP 64`..`LP 1`, `OFF`, `HP 1`..`HP 63`), `reso` (%) |
| `drive` | `drive`, `tone` (%) |
| `lofi` | `bits` 2-16, `rate` 1-44 (kHz), `dust` (%) |
| `chorus` | `mode` (`I` `II` `I+II`), `rate`, `depth`, `mix` (%) |
| `delay` | `time` (`1/32` `1/16T` `1/16` `1/8T` `1/16.` `1/8` `1/4T` `1/8.` `1/4` `1/2T` `1/4.` `1/2`), `feedback`, `tone` (%), `ping_pong`, `mix` (%) |
| `reverb` | `size`, `damp` (%), `pre_delay` 0-200 (ms), `mix` (%) |
| `limiter` | `ceiling` -12..0 (dB), `release` 10-1000 (ms) |
| `duck` | `source` (a track), `depth` (%), `release` 20-1000 (ms) |
| `metronome` | `tempo` (`FOLLOW` or 30-300), `beats_a_bar` 1-16, `sound` (`CLICK` `BEEP` `WOOD` `COWBELL`), `level` (%) |

**`patterns.json`:** `patterns`, each `number` (in the pool), `tracks` (1-16), `edit_step`, `what` (a line
on what it plays) and `rows` (1-256), each row a string of its tracks' cells joined by `" | "`. A cell
is `NNN CC:SS VV` and up to four commands:

| Part | Holds |
|---|---|
| `NNN` note | `C-0` .. `G-9` (C-4 is MIDI 60); `---` none; `===` note off |
| `CC:SS` instrument | its cartridge and slot, hex (`0B:05`); `..:..` the track's current one |
| `VV` volume | `00`-`7F`, hex: the note's level, `7F` the instrument at its own; `..` its default |
| commands | 0-4, each as below, run in their order; one of each, but two `Exy` of different kinds (`E93 EC9`) are two |

| Command | Written | Codes |
|---|---|---|
| a tracker effect | its letter and value, as XM shows it: `A04`, `E91`, `F06`, `0x47` arpeggio written `047` | 0x00-0x23 |
| XM's volume column | `~` with FastTracker 2's sign and its digit: `~-4`, `~+2`, `~V8`, `~M3` | 0x24-0x2D |
| PT/FM's own | `ACC`, `SLD`, `A+S`; `LEG` (legato) | 0xC0-0xC2; 0xD3 |
| an instrument's parameter | `*`, the code and the value, hex: `*41:60` (on a TB-303, CUTOFF 0x60) | 0x40-0x7F |
| the track's mixer | `=`, the code and the value: `=80:70` (LEVEL) | 0x80-0x84 |
| the FX | `#`, the code and the value: `#A7:40` (REVERB MIX) | 0x90-0xAF |
| a punch-in | `!`, the code and its rows: `!D1:08` (TAPE STOP for 8 rows) | 0xC3-0xD2 |

A parameter command sets its value from its row on, as turning the knob would: a sweep is a command on
every row. A cell with nothing but commands (`--- ..:.. .. *41:60`) is a cell too.

- **An FX command's value** is the knob's step from the bottom of its range, as the FX setup keeps it:
  a % knob 0-100; FILTER CUT 0-127 (`LP 64` 0, `OFF` 64, `HP 63` 127); a choice its place in the
  list (DELAY TIME `1/32` 0 .. `1/2` 11); a number its step (LO-FI BITS 2-16: 0-14).
- **FX ON (`#AE`) and FX OFF (`#AF`)** name the effect by its number: GATE 0, FILTER 1, DRIVE 2, LO-FI 3,
  CHORUS 4, DELAY 5, REVERB 6, LIMITER 7, DUCK 8, METRONOME 9.

## The tools

Python 3 with numpy and scipy, and ffmpeg. Run them in this order; each writes its part, and `check.py`
reads all of it. The sources are data: the tools read them and run nothing from them (the FM6 and DX
banks' voice lists, written in Python, are parsed and checked to hold only literals before they are
read).

| Tool | Makes |
|---|---|
| `tools/patches.py --dxheader FILE --fm6 DIR --dxbank DIR --classix FILE --ym2612 DIR --x16 DIR --mpc303 DIR` | the patch cartridges: the 8 DX7 cartridges (`dx7_carts.py`: their slots; `dx7_voices.py`: our voices; `fm_banks.py`: the free banks read and converted), JUNO-106 (our patches, written in it), TB-303 |
| `tools/import_samples.py --from DIR` | the sampler cartridges: DIR holds the libraries, a folder each (`--list` names the files needed) |
| `tools/fusion.py` | the FUSION cartridge |
| `tools/songs.py` | the songs and the DJ loops (written with `compose.py`; riffs and arpeggios follow the chords on the key's scale: `theory.py`; ADVENTURE's and CHIP LOOP's cells kept as made, in `tools/chip_songs.json`) |
| `tools/index.py` | `index.json` and `CREDITS.md` (the cartridges' numbers and the songs: `catalog.py`) |
| `tools/check.py` | checks every reference, limit, command, level, licence and sample, and prints the most voices each song sounds at once, every release counted until it is 60 dB down (`voices.py`, with the engines' measured times in `catalog.py`; `peaks.py` shows what sounds at a song's most; a looping song is counted round twice); more than 16, the largest pool, is a problem, and more than 4 in a DJ loop |

**The sources** (each tool records the commit or file it was made from in the works' `source`):

| Argument | What |
|---|---|
| `--dxheader FILE` | the DX header bank, 32 DX7 voices packed in a C header: `engines/src/dx7_bank.h` of its repository (github.com/ip2k/lunar-modulator) |
| `--fm6 DIR` | the FM6 bank, factory patches of a 6-operator FM engine: a checkout of its repository (github.com/hugelton/Felucca), `tools/gen_fm6_patches.py` |
| `--dxbank DIR` | the DX bank, factory voices of a DX7-style engine: a checkout of its repository (github.com/zvenson/dxsloop), `tools/gen_dx7_bank.py` |
| `--classix FILE` | Idiomdrottning's `idiomdrottning-classix.dx7` (idiomdrottning.org/dx7-old) |
| `--ym2612 DIR` | a checkout of ym2612-patches (github.com/ulalume/ym2612-patches) |
| `--x16 DIR` | a checkout of the Commander X16 ROM (github.com/X16Community/x16-rom): `audio/fmpatches.s` |
| `--mpc303 DIR` | a checkout of mpc-vst-303 (github.com/saustin2010/mpc-vst-303): `presets.json` |
| `--from DIR` | the sample libraries, in folders named as `import_samples.py`'s REPOS says: `VCSL`, `VSCO-2-CE`, `sonic-pi`, `world-percussion`, `Discord-SFZ-GM-Bank`, `karoryfer.emilyguitar` (repositories at their commits), `producer-space` (Producer Space's CC0 library as archive.org has it), `dubstep` (Freesound's previews of CC0 sounds, `fs<sound>.mp3`) |

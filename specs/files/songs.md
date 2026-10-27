# Songs

Status: **settled** (2026-10-09). The record: [the files proposal](../proposals/202610080050-fs.md),
section 7.

A song is an order of patterns with its time, its mixer and its own FX setup. Kind 0x21, number
`00`-`FF`; the list on SONGS ([../ui/screens/songs.md](../ui/screens/songs.md)) is in numbers' order.
Nothing refers to songs, so moving one needs no reference update.

## The record

| Bytes | Field |
|---|---|
| 0-11 | name, 16 characters |
| 12-13 | TEMPO, 3-600 (a tick 0.83 s to 4.2 ms; read held within) |
| 14 | TICKS A ROW, 1-31 |
| 15 | ROWS A BEAT, 1-16 |
| 16 | SWING, 50-75 (%) |
| 17 | flags: bit 0 AT THE END = STOP (else LOOP); bit 1 linear slides (else Amiga periods); bit 2 fast volume slides |
| 18 | LOOP FROM: the position the song loops to (0 unless a module set it) |
| 19 | the global volume at the start, 0-64 |
| 20 | positions − 1 (1-256) |
| 21- | the order: a pattern number for each position |
| then | the mixer: tracks stored (0-16, up to the last that differs from the default), then for each LEVEL, PAN, DUCK, CHORUS SEND, DELAY SEND, REVERB SEND (a byte each, [below](#a-tracks-mixer-bytes)) |
| then | the FX setup, 64 B ([below](#the-fx-setup)) |
| then | spare, 8 B |

- **Size:** ~170 B for 20 positions and 10 tracks; at most ~450 B.
- **Converted modules keep their sound:** TEMPO and TICKS A ROW from their BPM and speed, the pans from
  their channels (MOD's left, right, right, left; S3M's pan table), their global volume and restart
  position, and their slides' kind: Amiga periods for MOD and S3M, linear for XM; S3M files saved
  for fast volume slides (Scream Tracker 3.00, or their header's flag) keep them. Our own songs slide
  linearly and normally.
- **Punch-ins can be in a song,** as commands in its patterns
  ([commands.md](commands.md#punch-ins)); the FX setup does not include them.
- **Not in a song:** mutes.

## A track's mixer bytes

Used by songs, the loops ([loops.md](loops.md)), and the commands 0x80-0x84 and `8xx`
([commands.md](commands.md#mixer-and-fx-0x80-0xbf)): LEVEL `00`-`7F`, `7F` the track at full level, as
a cell's volume; PAN `00` left, `80` centre, `FF` right; DUCK and the three SENDS `00`-`7F`, `00` none.
The default: LEVEL `7F`, PAN `80`, the rest `00`.

## The FX setup

Songs and the loops each have one ([../ui/screens/fx.md](../ui/screens/fx.md)), 64 B. Everything in it
is placed by its fixed number ([commands.md](commands.md#the-numbers-are-fixed)), so a knob or an
effect added later takes its own place and moves nothing.

| Bytes | Field |
|---|---|
| 0 | ROUTING: GLOBAL or SENDS |
| 1-6 | the chain's order: its items' effect numbers, 4 bits each, up to 12; 15 after the last (today 9: the 8 effects of the chain and ◆ SENDS) |
| 7-8 | on / off: bit *n* the effect numbered *n* (today 0-9: the 8 chain effects, DUCK, METRONOME) |
| 9-13 | METRONOME: TEMPO (2 B: 0 FOLLOW, else 30-300), BEATS A BAR, SOUND, LEVEL |
| 14 | retired, never reused: was the METRONOME's COUNT-IN, now SETTINGS' (the EDITOR's takes) and the loops' own ([loops.md](loops.md#the-record)) |
| 15 | spare |
| 16-63 | the knobs: the one whose FX command is code *c* at byte 16 + (*c* − 0x90), as its step from the bottom of its range (FILTER's CUT 0-127 with OFF at 64, a % knob 0-100). Today GATE 16-19 (its PATTERN among them), FILTER 20-21, DRIVE 22-23, LO-FI 24-26, CHORUS 27-30, DELAY 31-35, REVERB 36-39, LIMITER 40-41, DUCK 42-44 (SOURCE 0-15 the tracks `00`-`0F`, 16 `KEYS`); 45 free (0xAD); 46-47 unused (0xAE-0xAF, FX ON and OFF, have no knob); FILTER's envelope 48-53 (0xB0-0xB5: ENV 0-100, SOURCE 0 `KEYS`, 1-16 the tracks `00`-`0F`, 17-21 `4 BEATS`, `2 BEATS`, `BEAT`, `1/2 BEAT`, `1/4 BEAT`, ATTACK 0-127, DECAY 0-127, SUSTAIN 0-100, RELEASE 0-127, the times' steps as [../ui/screens/fx.md](../ui/screens/fx.md#filters-envelope) gives them); 54-63 the knobs to come (0xB6-0xBF) |

- **A knob added later** finds 0 in a song saved before it: its step 0 is the sound as it was without
  it, or, where that cannot be, the setup's version goes up and older songs load with its default
  ([README.md](README.md#extending)). FILTER's envelope so: a song saved before it reads ENV 0 %, the envelope
  off; a new song's setup has ENV 0 %, SOURCE `KEYS`, ATTACK 2.0 ms (0), DECAY and RELEASE 296 ms (75), SUSTAIN
  0 %.

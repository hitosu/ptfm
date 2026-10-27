# DJ's setup and stand

Status: **settled** (2026-10-10), with DJ; the stand added 2026-10-11. The record: [the DJ proposal](../proposals/202610101408-dj.md),
section 12.

DJ ([../ui/screens/dj.md](../ui/screens/dj.md)) keeps its own setup, its FX setup and its properties, and its
**stand**: the decks as they stand, so that a SAVE, a power cycle and PLAY go on from the same point with the
same settings. One record: kind 0x23, number `00`, 136 B, kept by SAVE like any change.

## The record

| Bytes | Field |
|---|---|
| 0 | retired, never reused: was DJ's VOICES (DJ plays with SETTINGS' VOICES now); written 0, read as nothing |
| 1 | CROSSFADER: 0 SMOOTH, 1 SHARP |
| 2 | FILTER RESONANCE, the DJ FILTERs': 0-100 (%) |
| 3 | FILTER THE SENDS: 0 ON, 1 OFF (0 the default, as the fields before it) |
| 4 | FX LFO, what the LFO pads 13-15 swing: 0 REVERB MIX, 1 DELAY MIX, 2 CHORUS MIX |
| 5 | the stand: the crossfader's place, 0 (all the way to the left) to 32 |
| 6 | the stand: bit 0 SYNC; bit 1 retired, never reused (was PREVIEW: written 0, read as nothing); bits 2-3 SYNC's multiple: 0 the main's BPM, 1 its half, 2 its double |
| 7 | spare |
| 8-71 | the FX setup ([songs.md](songs.md#the-fx-setup)): DJ's own. ROUTING is always SENDS; DJ uses CHORUS, DELAY, REVERB, the LIMITER and the METRONOME, and keeps the other effects' bytes at their defaults |
| 72-79 | spare |
| 80-83 | the crossfader's LFO ([an LFO's setup](#an-lfos-setup)); its range: 0 `L TO HERE`, 1 `L TO R`, 2 `SWING`, 3 `MIRROR` |
| 84-87 | the FX LFO; its range: 0 `HERE TO 100 %`, 1 `0 TO 100 %` |
| 88-111 | the left deck, the main ([a deck](#a-deck)) |
| 112-135 | the right deck |

### A deck

| Bytes | Field |
|---|---|
| 0 | its song's number; `FF` no song (the deck's other bytes then 0) |
| 1-2 | where it stands: the position, the row |
| 3-4 | its cue point: the position, the row |
| 5-6 | its tempo: the BPM × 10, 16 bits little-endian (`1400` for 140.0) |
| 7 | KEY: −6 to +6, signed |
| 8 | VOLUME: −24 to +6 dB, signed; −25 OFF |
| 9 | the DJ FILTER's setting: −64 (LP's end) to 63 (HP's end), 0 OFF, signed |
| 10 | bit 0 the FILTER key has it on; bit 1 REPEAT PATTERN; bit 2 stopped at the song's end |
| 11 | the loop set: 0 none, else its LOOPS pad, 2-8 |
| 12-13 | the loop's start: the position, the row |
| 14 | the loop's length in half beats |
| 15 | bit 0: a REPEAT of bars (on the pattern's bar grid), else a free loop |
| 16-17 | the mutes: a bit a track, track `00` bit 0 of byte 16 |
| 18 | the track soloed; `FF` none |
| 19-22 | its DJ FILTER's LFO ([an LFO's setup](#an-lfos-setup)); its range: 0 `OFF TO HERE`, 1 `OFF TO FULL`, 2 `HERE TO FULL`, 3 `SWING` |
| 23 | spare |

### An LFO's setup

| Byte | Field |
|---|---|
| 0 | bit 0 on, bit 1 reversed (the crossfader's) |
| 1 | its shape: 0 SINE, 1 SQUARE, 2 TRIANGLE, 3 SAW UP, 4 SAW DOWN, 5 RANDOM |
| 2 | its range, as its field says |
| 3 | its speed: 0 `32 BARS`, 1 `16 BARS`, 2 `8 BARS`, 3 `4 BARS`, 4 `2 BARS`, 5 `1 BAR`, 6 `1/2`, 7 `1/4`, 8 `1/8` |

## The stand

- **What it is:** the decks as they stand paused: each deck's song, place, cue point, tempo, KEY, VOLUME, filter
  (its setting and whether its key has it on), the loop or REPEAT set, mutes and solo, and its filter's LFO; the
  crossfader, SYNC and its multiple, and the crossfader's and the FX LFO's setups. **Not stored:** where
  an LFO is in its swing (an LFO on sets off afresh once the decks play), anything held (a HOLD, a ROLL, a stop),
  the pads' mode and what ENV has K2 and K3 turn (the hands', not the stand's).
- **Written by every SAVE.** SAVE while the decks play waits for a pause, as every SAVE does
  ([../ui/saving.md](../ui/saving.md)), so the places written are where the decks paused: as if STOP were pressed.
  The decks standing otherwise than written count as one unsaved change (SAVE dim): they changed.
- **RESET DJ** (DJ's properties, GLO) sets the decks to a new store's stand, below; SAVE then keeps it.
- **Loaded at power-on** (and by REVERT, which reloads what the flash holds): both decks paused as written, so PLAY
  goes on from the same point with the same settings, on DJ or on any screen that plays what played last. A song
  no longer there leaves its deck empty; a place past its song's end is put at its last row.
- **With no record yet** (a new store): VOICES AS SET, CROSSFADER SMOOTH, FILTER RESONANCE 30 %, FILTER THE
  SENDS ON, FX LFO REVERB MIX; the FX setup a new song's, but DELAY on at 1/8., PING-PONG ON, FEEDBACK 40 %, MIX 30
  %, and REVERB on at SIZE 70 %, MIX 35 %; the stand: both decks empty, each deck's FILTER on at OFF, the
  crossfader all the way to the left, the main, SYNC on, every LFO off at its first setup (TRIANGLE, `1/2`, the
  first range; the crossfader's `MIRROR`).

# Patterns

Status: **settled** (2026-10-09). The record: [the files proposal](../proposals/202610080050-fs.md),
section 3.

A pattern is a grid of cells: tracks across, rows down. Songs play patterns in their order, FUSION
instruments play their rows, and LOOPS' loops keep their notes as a pattern's tracks do, a stream a loop
([loops.md](loops.md#a-loops-stream)).

- **A cell** is one track's entry in one row: a note (`C-4`; `---` none; `===` note off), an
  instrument (`0B:05`), a volume (`7F`) and **up to four commands** (`A04`, `ACC`). Trackers call
  them effects; here they are commands (CMD), since FX means the audio effects.
- **Every tracker cell fits:** a MOD, S3M or XM cell becomes one cell, its effect and an XM volume
  column's effect as two commands ([commands.md](commands.md#tracker-effects)).

## Numbers

- **A shared pool** of patterns `00`-`FE`: the user's `00`-`EF`, the firmware's `F0`-`FE`. Song positions and
  FUSION slots refer to them, so two songs, or a song and a FUSION instrument, can use the same pattern.
- **`F0`-`FD`** are kept for later built-in patterns ([instruments.md](instruments.md#built-in-instruments));
  the firmware has none yet (FUSION's patterns are the demo disk's: `E0`-`E5`, [disk.md](disk.md#the-demo-disk)).
- **`FE` is the EDITOR's own pattern** ([../ui/screens/editor.md](../ui/screens/editor.md#the-editors-own-pattern)):
  empty in the firmware (8 tracks × 64 rows, EDIT STEP 1), kept by SAVE as a changed built-in is, with room kept
  for it ([store.md](store.md#the-room-for-the-editors-pattern)). No song position or FUSION slot names it, and
  no list shows it.
- **`FF` is "no pattern"** where a song position or a FUSION slot names none.

## Size

- **1-16 tracks** (`00`-`0F`) **and 1-128 rows** (`00`-`7F`), set for each pattern. 16 is the most, as the
  white keys are 16; the FM-1 sounds at most 12 voices for now, so a pattern of more busy tracks parks notes
  ([../ui/screens/settings.md](../ui/screens/settings.md#voices)): the writer watches the voices (STATUS,
  VISUAL).
- **128 rows** are 8 bars at 4 rows a beat, 5 at 6, 4 at 8. The largest pattern, every cell full with four
  commands, is then ~30 KB in at most 8 parts ([below](#parts-sizes-and-reading)), so the EDITOR's room is
  32 KB and any pattern's whole edit fits the edit memory ([ram.md](ram.md#editing-and-saving)).
- **A pattern record of more than 128 rows** (made before this rule) reads as its first 128; a copy of it
  (the EDITOR's LOAD A PATTERN, a CLONE, a MOVE) and its next save keep those, its cells past row 127 left out.

A pattern record (kind 0x20, number `00`-`FE`):

| Bytes | Field |
|---|---|
| 0 | tracks, 1-16 |
| 1 | rows − 1 (1-128 rows; 128-255 are not written) |
| 2 | EDIT STEP, 0-16 |
| 3-4 | the tracks stored in this record, a bit each (empty tracks take nothing) |
| 5- | one stream for each stored track, in track order: its cells in row order, then 0x00 |

## A cell's bytes

A head byte, then the fields it announces, in this order:

| Head bits | Meaning |
|---|---|
| 7 | a note: 1 B, MIDI 12-127 (`C-0` to `G-9`); 0x01 a note off (`===`) |
| 6-5 | the instrument: 00 none; 01 the same as this track's last one in this record (0 B); 10 that one's cartridge, another slot (1 B: the slot); 11 cartridge and slot (2 B) |
| 4 | a volume: 1 B, `00`-`7F` |
| 3 | commands ([commands.md](commands.md)): one is 2 B, its code and its value; 2-4 are the code 0xFF, their count, then each as code and value |
| 2-0 | rows skipped since this track's previous cell (from row 0 for the first): 0-6; 7: a byte follows with 0-255 |

- **A cell has at least one field,** so a head byte of 0x00 ends the track's stream.
- **The instrument's first appearance** on a track in a record is always written in full, so a record
  decodes on its own ([ram.md](ram.md#editing-and-saving)).
- **No instrument:** the note plays with the track's current instrument, as in XM.
- **Volume:** `7F` plays the instrument at its own level; no volume, the instrument's default.
  Volumes of 0-64 (MOD, S3M, XM) convert exactly both ways: v becomes 2v, 64 becomes `7F`; back,
  round(u × 64 / 127).
- **A cell takes 2 to 15 bytes** (16 after a long skip): 7 at most with one command. So one track's
  stream stays under 4 KB, within one record.

## Parts, sizes and reading

- **A pattern too big for one record** is split between tracks into as many parts as it needs (sub
  0-15); every part repeats bytes 0-2, with its own track bits. A track's stream is at most ~2 KB (128
  cells of at most 15 B), so two always fit a part: a pattern of 16 × 128 cells, every one full with one
  command, is ~14 KB in 4 parts; with four commands each, ~30 KB in 8.
- **Measured on six songs** (two converted modules, four written for PT/FM; 121 patterns): 81.8 KB, 3.4 B a
  cell; the largest pattern 1.76 KB; 675 B a pattern on average, about 6 to a block. That is 9 % less
  than packing row by row (a mask of tracks for each row), and 21-23 % less on sparse songs of 10-12
  tracks.
- **Reading a row** means advancing each track's stream to it: a jump into a pattern scans at most
  ~4,000 cell heads (~0.1 ms). The player keeps a cursor for each track.

## FUSION patterns

- **Row = MIDI note:** row *r* plays for the note *r* (`C-4` is row `3C`). A pattern of 128 rows
  (`00`-`7F`) covers every note. Rows `00`-`0B` (below `C-0`) never play.
- **No nesting:** a FUSION row that names a FUSION instrument is silent.
- **A row's tick commands** ([commands.md](commands.md)) run as the note plays, though the row has no length of its
  own: its ticks count from the note's start, at the tempo of what plays, for as long as the note sounds (a key
  held; in a song, until the track's next note, a note off or a cut). `EDx` starts a layer x ticks late; `ECx` cuts
  it at tick x (at or before its start: it never sounds); `E9x` starts it again every x ticks; `0xy` arpeggiates it
  (its note, + x, + y semitones, a tick each); `Axy` slides its volume (x up, y down, 2 a tick, but on its starts).
  The note's end stops them; a layer still to start never does. The keys, the arpeggiator and a song's FUSION cells
  play them alike.
- **EDIT STEP 0:** a pattern made from a FUSION slot starts with EDIT STEP 0, so the cursor stays on the
  key's row while its layers are written; every other new pattern starts with 1.

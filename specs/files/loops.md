# The loops

Status: **settled** (2026-10-24), revised 2026-11-03. The records: [the loop station
proposal](../proposals/202610240236-loops-station.md), sections 12 and 13, which replaced the first LOOPS' record
([the files proposal](../proposals/202610080050-fs.md), section 8); [the stage
proposal](../proposals/202611030330-loops-redesign.md), section 16 (the length always, VIEW, K1-K4's jobs, SHIFT).

LOOPS is a loop station ([../ui/screens/loops.md](../ui/screens/loops.md)). Kind 0x22, number `00`: the loops, kept
by SAVE like any change, so a power cycle brings back the same LOOPS: everything kept and the whole setup, packed
as tightly as the rest of the store allows.

## What is kept

- **One length** for all the loops, in rows, whole bars of it (a bar the METRONOME's BEATS A BAR beats, each ROWS A
  BEAT rows), **always,** with loops or none: 4 bars at first, or the most whole bars that fit 128 rows where 4 do not
  (4 bars fit only while ROWS A BEAT × BEATS A BAR is 32 or less). At most 128 rows, a pattern's
  ([patterns.md](patterns.md#size)): 8 bars at 1/16, 5 at 1/16T, 4 at 1/32 (4 beats a bar).
- **The loops,** 16 at most, in the order they were made: each its instrument (one a loop), its colour, MONO or
  POLY, its lanes (1-4: the most notes it holds at once), its base (the rows its notes cover, from the loops' start)
  and repeat count, its SHIFT, muted, and its mix (a track's). **A solo is never saved:** solos live in RAM until a
  power-off (a REVERT keeps them), so after a power-on no loop is soloed and the mutes are heard as saved.
- **Each loop's notes, once:** its base's cells, a lane at a time interleaved into one stream, a pattern track's
  ([below](#a-loops-stream)). The base plays from the loops' start as many times as the repeat count says, then the
  loop rests until the length's end; a repeat the length's end falls in is cut there.
- **A loop's SHIFT,** 0-127: the rows the loop plays later, round the length: it plays its row (row − SHIFT) mod the
  length, its repeats and rest turned with it; 0 none. Its notes do not move. A repeat the length cuts ends at the
  loop's own row 0 (the loops' row SHIFT), where its first repeat begins again: what it still holds ends there, in the
  first pass as in every other, and where a jump lands on that row. A SHIFT not under the length (written
  by another firmware after a shorter LENGTH) is read modulo the length. Whatever reads a loop reads it as it is heard,
  written out over the length and then turned SHIFT rows: the player, SAVE TO PATTERN, EDIT's copy onto the stage, the
  loop's QUANTIZE, LENGTH ([below](#storing-and-loading)). RECORD LIVE's song takes what the player plays.
- **What the cells hold is notes:** their volumes and note offs; with QUANTIZE REAL TIME, note delays (`EDx`) and
  key offs (`Kxx`); a MONO loop's glides and legatos (a TB-303's `SLD`, elsewhere `3xx` or `LEG`). No other command
  ([commands.md](commands.md)): a loop holds no knob turns, FX or punch-ins (RECORD LIVE writes those into its song).
- **The setup:** TEMPO, TICKS A ROW, the grid, SWING, QUANTIZE, MONO LEGATO, COUNT-IN, the keys' mode (MONO or
  POLY: how the keys play on LOOPS and how REC keeps the stage), VIEW, K1-K4's jobs, the next loop's mix, LOOPS' own FX
  setup (METRONOME included), the selected loop, so LOOPS comes back as it was left.
- **Not stored:** what is playing and the play position (the loops come back paused at the start); the stage (a piece
  of its own in the edit memory, never saved: [ram.md](ram.md#the-stage-in-the-edit-memory)); an arm or a song armed;
  ADJUST; a planned LENGTH; OVERWRITE; the keys' CONTROL mode; the pattern recorder's steps.

## Recording on the grid

QUANTIZE places what the keys write onto the stage:
- **`2 ROWS`, `3 ROWS`, `4 ROWS`:** the nearest line of that many rows, from the loops' start;
- **`ROW`:** the nearest row, whatever the grid (the 1/32 rows too at 1/32);
- **`REAL TIME`:** the timing kept to the tick. The note goes on the row it falls in, with a note delay (`EDx`) of
  the ticks since that row started; its end goes in as a key off at its tick (`Kxx`). A note played just before a
  row goes on the row before, delayed. At 6 ticks a row a tick is 1/24 of a beat. Each takes one of the cell's four
  commands. `EDx` reaches 15 ticks, so with more than 16 ticks a row a note late in its row lands on the 15th.

A note off goes on the nearest row, at least a row after its note (with the grid's values; REAL TIME: the key off at
its tick). Nothing wraps over the length's end: a note still held there ends at the line, a note off at row 0 (its
lane's note there, if it has one, ends it instead), and its key, still held, writes nothing more. The notes EDIT brought onto the stage are written as they are: one
crossing the line keeps its end past it, as it was heard; so are the notes DOUBLE copied into the stage's second half.
A note off or key off that ends nothing (its lane silent there, counted round from the length's end) is left out when
the stage is sent.

## A loop's stream

- **One stream a loop,** a pattern track's ([patterns.md](patterns.md#a-cells-bytes)): **the cell of lane *l* at
  row *r* is the stream's row 4*r* + *l*,** so a stream covers 4 × its base rows (up to 512) and a cell's skip
  counts those rows. A lane's cells end each other as a track's do: a note ends the lane's note before it.
- **A gap past the skip byte's 255 rows** (64 of the loop's rows) is bridged with a cell holding only `ED0` (a note
  delay with no note: nothing happens there), at a skip of 255 from the cell before it (256 stream rows on), as often
  as needed.
- **No instrument in the cells:** every cell's instrument bits are 00; a note plays its loop's instrument, which
  the player gives it (a loop has one instrument, so nothing is lost).
- **A cell:** a note with its volume, 3 B (head, note, volume); a note off 2 B; a note delay or a glide 2 B more; a
  key off alone 3 B. A stream is at most 3,800 B, so it fits any part with the head before it. The stage's notes are
  a loop's stream too, under the same limit: a note that would take it past is neither heard nor written, and EDIT,
  DOUBLE or REC (the ends at the line it adds) that would is refused, nothing changed (`THE STAGE IS FULL`).
- **Lanes:** a POLY loop's notes on up to 4 lanes, as the keys took them on the stage (a key a lane, the first no
  other key down holds: [../ui/screens/loops.md](../ui/screens/loops.md#the-stage); a fifth note neither heard nor
  written); the loop's lanes count is the most it uses. A MONO loop uses lane 0 alone: a POLY stage sent in MONO is
  merged into one lane as it played (the later of overlapping notes).

## Storing and loading

- **Storing the loops in a pattern** writes them out as they are heard: the loops' length; every loop's notes, its
  repeats written out, a note off where it goes to rest, the cut at the wrap, then turned SHIFT rows; each note naming
  its instrument; lanes packed into the pattern's 16 tracks where they do not overlap (a lane's notes onto the first
  track free over all their rows; what does not fit left out and counted); each loop's mix as mixer commands (`=`)
  with its first note on a track, and again where a shared track turns to another loop. The stage is not written.
- **Loading a pattern** replaces the loops: the length is the pattern's rows in whole bars (rounded up, at most 128
  rows); a loop an instrument, its notes from every track that plays it, up to 4 at once as its lanes, more at once
  making another loop of the same instrument, at most 16 loops; each POLY, its repeat count 1, its SHIFT 0, its mix
  from the `=` commands with its notes; its glides, legatos, note delays and key offs kept, every other command left
  out ([../ui/screens/loops.md](../ui/screens/loops.md#storing-in-a-pattern-and-loading-one)). The stage stays,
  fitted to the new length.
- **A length change** (LENGTH, shorter or longer) first writes a shifted loop out as it is heard over the old length,
  its SHIFT 0, then cuts it or adds silence; before a longer length a loop whose last repeat the old length cuts is
  written out over it too (its base the old length, once), and repeats the old length never reached are dropped from
  the count, so the rows added are silence. DOUBLE keeps the SHIFT (the loop plays the same). REC writing a loop sets
  its SHIFT 0.

## The record

The head (an 8-byte header, the FX setup, the next loop's mix, two bytes of the setup, the loop table), then the
loops' streams as a pattern's tracks: packed, nothing stored twice.

| Bytes | Field |
|---|---|
| 0-1 | TEMPO, as a song's ([songs.md](songs.md#the-record)) |
| 2 | bits 4-0 TICKS A ROW (1-31); bits 6-5 COUNT-IN (0 OFF, 1 1 BAR, 2 2 BARS; 3 not written, read as OFF); bit 7 MONO LEGATO (0 GLIDE, 1 JUMP), unless byte 4's bit 6 makes it LEGATO. With no record yet: TICKS A ROW 6, COUNT-IN OFF, JUMP (`0x86`); a record keeps its own |
| 3 | SWING |
| 4 | bits 1-0 the grid (0 1/16: 4 rows a beat; 1 1/16T: 6; 2 1/32: 8); bits 4-2 QUANTIZE (0 ROW, 1 2 ROWS, 2 3 ROWS, 3 4 ROWS, 4 REAL TIME; 5-7 not written, read as ROW); bit 5 the keys' mode (0 POLY, 1 MONO): how the keys play on LOOPS and how REC keeps the stage; bit 6 MONO LEGATO is LEGATO (byte 2's bit 7 then 0; records from before LEGATO have 0 here and keep their GLIDE or JUMP); bit 7 0 |
| 5 | the loops, 0-16 (0: none yet, and no streams follow) |
| 6 | the loop selected, 0-15; the loops' count: the placeholder, `+ NEW LOOP` |
| 7 | the length in rows, 1-128, always, with loops or none; 0 (a record from before with no loops) read as 4 bars, or the most whole bars that fit 128 rows where 4 do not ([above](#what-is-kept)) |
| 8-71 | the FX setup ([songs.md](songs.md#the-fx-setup)): LOOPS has its own, METRONOME included. With no record yet, or a record whose 64 bytes here are all 0, it is a new song's (only LIMITER on) with ROUTING SENDS, so FX can switch any effect on and the loops' sends act. A chain that names an effect twice (bytes 1-6 all 0 read as twelve GATEs) takes that chain and routing, the rest kept |
| 72-77 | the next loop's mix: LEVEL, PAN, DUCK, CHORUS SEND, DELAY SEND, REVERB SEND, as a song track's ([songs.md](songs.md#a-tracks-mixer-bytes)); all 0 read as a new song's track. Its three sends are kept as written and not read: the next loop's sends are FX MIXER's KEYS column's (the settings record's, [settings.md](settings.md)), heard on the stage on `+ NEW LOOP` and written into the loop REC makes there ([../ui/screens/loops.md](../ui/screens/loops.md#the-loops)) |
| 78 | bits 2-0 K1's job, bits 5-3 K2's (the jobs, by number: 0 LEVEL, 1 PAN, 2 DUCK, 3 CHORUS SEND, 4 DELAY SEND, 5 REVERB SEND, 6 TEMPO, 7 VIEW); bits 7-6 VIEW (0 2 BARS, 1 1 BAR, 2 4 BARS; 3 not written, read as 2 BARS) |
| 79 | bits 2-0 K3's job, bits 5-3 K4's; bit 6 the jobs are stored (0: K1-K4 LEVEL, PAN, REVERB SEND, TEMPO, whatever bits 2-0 and 5-3 of bytes 78 and 79 hold; written 1); bit 7 0 |
| 80- | the loop table, 12 B a loop, in the order they were made |
| then | with loops, the streams: a pattern payload's bytes ([patterns.md](patterns.md#size)): byte 0 the loops' count, byte 1 the length's rows − 1, byte 2 0, bytes 3-4 the streams stored in this part (bit *i*: loop *i*'s), then each stored loop's stream in loop order |

**A loop:**

| Bytes | Field |
|---|---|
| 0-1 | its instrument: cartridge, slot |
| 2 | bits 3-0 its colour (0-11: light red, yellow, light green, light cyan, light blue, light magenta, brown, green, cyan, red, magenta, light grey); bit 4 MONO; bit 5 muted; bit 6 0 (a solo once: written 0, never read); bit 7 its SHIFT's bit 6 |
| 3 | bits 1-0 its lanes − 1 (1-4); bits 7-2 its SHIFT's bits 5-0 |
| 4 | its base: rows − 1 (1-128) |
| 5 | its repeat count, 1-255 (the base × the count may pass the length: the last repeat is cut there) |
| 6-11 | its mix: LEVEL, PAN, DUCK, CHORUS SEND, DELAY SEND, REVERB SEND, as a song track's ([songs.md](songs.md#a-tracks-mixer-bytes)) |

- **Parts,** as a pattern's ([patterns.md](patterns.md#parts-sizes-and-reading)): part 0 holds the head and the
  streams that fit after it; the next parts the other streams, each part repeating the pattern payload's bytes 0-2
  with its own stream bits. A stream always fits a part with the head, so 16 parts are enough.
- **The edit memory** keeps the record as pieces ([ram.md](ram.md#editing-and-saving)): the head (bytes 0 to the
  table's end) whole, the payload's bytes 0-2, and each loop's stream; REC rewrites its loop's stream (and the head),
  a mute the head. Saved loops play from the flash; only what changed takes edit memory. The stage is no part of the
  record: a piece of its own ([ram.md](ram.md#the-stage-in-the-edit-memory)).
- **Sizes:** the head 80 B and 12 B a loop; a note 3-5 B (REAL TIME +2, a note off +2): a jam of 8 loops over 4 bars
  ~5-8 KB; the most (16 loops of 4 lanes, 128 rows, every row full) ~35 KB in 10 parts.

**Versions:** this is version 4 of the record (2026-10-24: the loop station). Its spare bits and bytes took meanings
on 2026-11-03, each with 0 as before ([README.md](README.md#extending)): the length with no loops (byte 7), VIEW and
K1-K4's jobs (bytes 78-79), each loop's SHIFT (its bytes 2-3). So the version stays 4 and nothing is converted: a
record written before reads with the defaults (4 bars with no loops, VIEW 2 BARS, K1-K4 LEVEL, PAN, REVERB SEND,
TEMPO, SHIFT 0). **An older PT/FM** reading a record written since plays every loop unshifted (SHIFT is lost to it),
keeps its own K1-K4 and view, and takes no length while there are no loops (its first loop sets one; it writes 0 there
itself when it rewrites the head with no loops, which reads as 4 bars here). Its edits keep the bits it does not know
(a mute, a lane count), but a loop it makes or re-records starts with SHIFT 0.

Version 3 (2026-10-11) held a loop a track: its header (byte 2's bits 6-5 COUNT-IN with 3 KEY, byte 4's bits 3-2
QUANTIZE: 0 STEP, 1 1/8, 2 OFF, and bit 4 ALGORITHM WHILE PLAYING; byte 7 the cursor's row), the FX setup, 10 B a loop
(its sound, its track, bit 0 muted, its mixer), then one 16-track pattern as long as the loops, each loop's cells on
its track. The device reads version 4 only (a version 3 record is kept as it is, and LOOPS starts empty); the PC side
converts it ([disk.md](disk.md#versions)):

- each old loop becomes a POLY loop of one lane, in the same place, with its sound, mute and mixer, its base the
  length and its repeat count 1, SHIFT 0, its colour by its place; its cells kept but for commands other than note
  delays and key offs, which are left out; with no loops the length is 4 bars (or the most whole bars that fit);
- QUANTIZE STEP becomes ROW, 1/8 the line of a half beat (2, 3 or 4 ROWS by the grid), OFF REAL TIME; COUNT-IN KEY
  becomes OFF; ALGORITHM WHILE PLAYING and the cursor go; the next loop's mix is a new song's track's; VIEW and K1-K4's
  jobs the defaults.

For an FM-1 that reads only version 3, the editor's WRITE converts version 4 down ([disk.md](disk.md#versions)):
each lane of a loop becomes a loop of its own (16 at most), with the loop's sound, mute and mixer, its base written
out over the length as it is heard (its repeats, a note off where it goes to rest, the cut at the wrap, then nothing,
turned its SHIFT rows), so it plays the same; QUANTIZE ROW
becomes STEP, REAL TIME OFF, the others 1/8; the selection follows its loop. What version 3 cannot hold is named
before writing: MONO (played POLY), MONO LEGATO JUMP or LEGATO, the next loop's mix, a QUANTIZE that is not the half
beat's, loops past 16, VIEW and K1-K4's jobs when they are not the defaults, and with no loops the length when it is not
the one version 3 brings back (4 bars, or the most whole bars that fit) (`NOT KEPT: VIEW, K1-K4'S JOBS, THE LENGTH`). A
version 4 record made from version 3 goes down and back up byte for byte.

Version 2 had 14 B a loop (its tracks as bits, at most 8, the flags, its mixer, its layers), version 1 11 B: the PC
side takes them to version 3 first, each of a loop's tracks a loop of its own.

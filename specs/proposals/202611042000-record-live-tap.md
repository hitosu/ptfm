# Proposal: RECORD LIVE records what is heard

Status: **settled** (2026-11-04); the specs lead: [../ui/screens/loops.md](../ui/screens/loops.md#record-live) (RECORD
LIVE, the stage, REC, the pattern recorder, Controls, Texts, Help), [../ui/controls.md](../ui/controls.md),
[../files/ram.md](../files/ram.md) and [../debug.md](../debug.md). It changes RECORD LIVE as [the loop station
proposal](202610240236-loops-station.md) made it. Built in the playground (`playground/scr-loops.js`, `player.js`,
`dsp-core.js`) and the firmware (`fw/core/play/live.c`, `player.c`, `fw/core/ui/scr_loops.c`).

## 1. Why

RECORD LIVE used to write a song from the loops' streams, row by row, and to keep the song's picture true it froze
LOOPS while it recorded: the stage rested, and everything that changed a loop was refused. So a performance could not
be recorded the way it is played. A song now records **what was heard, whatever LOOPS does**: it is another layer
before the output, a tap where notes enter the voices.

- **Goals:** every note that sounds written as it sounds, from any source; LOOPS working as usual during a song, GRID
  included; a song's rows its own, so nothing LOOPS does later changes how a note is written; only notes with a voice
  written; REC keeping its jobs, HOME ending the song; the sound bit for bit where nothing records.
- **Not goals:** parameters turned from a MIDI map; a replay that parks exactly as the performance did; a song that
  plays the loops' own streams.
- **A failure even if it works:** a note heard and not written with tracks free; a note written that never had a voice;
  a function of LOOPS refused because a song records. Its rule replaces the `RECORDING A SONG` refusal of [the stage
  switch proposal](202611041720-loops-stage-switch.md).

## 2. The tap

- **Where:** where notes enter the voices, after the lanes and the stage, so every source is one stream: the loops, the
  stage, the keys and the pattern recorder's preview. A FUSION note is written as one note, its layers following on
  playback.
- **What it costs:** an event in a small ring, per note, never per frame; the main loop writes the song from the ring.
- **Only notes with a voice** (the VOICES count, 8, 12 or 16): a note given no voice is not written; a note losing its
  voice (taken by another, or parked past the count) gets a note off where it lost it; a parked note coming back is not
  written again. A replay of the song may park other notes, so their releases can overlap.

## 3. The song's time

Its rows are fixed at its start: LOOPS' tempo, ticks a row, swing and FX setup then. Time is counted in the loops'
replay's ticks, which run on whatever the loops do: a note's row is its tick count over the song's ticks a row, what is
left its note delay `EDx`; its end a note off on a row's first tick, else a key off `Kxx` at its tick. So GRID, LENGTH,
DOUBLE and any change of a loop leave a song's rows alone. Tempo changes are commands, as ever.

## 4. The tracks

A note goes to a track whose mix is its source's: a loop's LEVEL, PAN and sends; the stage's and the pattern recorder's
the mix REC would use; the keys' on another screen the KEYS column's. Sources share tracks freely: a track free at the
note's row goes first to the lane that held it, then to any track with that mix, then to one not used yet, then to the
one whose mix differs least, which mixer commands bring to the source's. A mix turned is commands on the tracks it
holds. A track takes one note a row; what finds no track is counted left out, as ever.

## 5. REC and HOME

REC keeps its jobs during a song: it sends the stage as anywhere (a change of a loop like any other), it blinks with the
stage off or nothing to send; armed, it still starts the song. **HOME tapped ends the song,** its last row the row HOME
was pressed in, what was written after it cut away; armed, HOME cancels it. HOME held stays HOME's combination, and a
combination that leaves LOOPS ends the song as leaving does. Inside the pattern recorder or a dialog HOME is theirs.

## 6. What it takes

The pattern being written waits in RAM, a borrow of 16 KB from the spare zone while a song is armed or recorded, since
a song and DJ never run together; the writer's own variables take no more than the old ones'. The keys of a song no
longer have lanes of their own: they are the stage's or the stage-off keys'. Figures:
[../reports/0.27.md](../reports/0.27.md).

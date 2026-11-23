# Proposal: the EDITOR's realtime takes heard at once

Status: **settled** (2026-11-20); its specs are written with the work, and lead from then on. It changes
[../ui/screens/editor.md](../ui/screens/editor.md#realtime-takes) (How a take records), the firmware's player and
EDITOR screen, the playground and the manual. Nothing of what a take writes changes.

LOOPS' stage keys sound at their press since [its proposal](202611201337-loops-keys-at-once.md). The EDITOR's realtime
take still plays a key exactly as it is written: a note played before its QUANTIZE line waits for it, up to half a line
(a whole beat at `4 ROWS`). It is the same lag, and it goes the same way.

## 1. Heard at once, written as before

- **A key sounds at its press and ends at its release,** with every QUANTIZE, on its track as the replay would start it
  (its instrument, the track's mix): no wait for the line. REAL TIME's note delay `EDx` is written, not waited for.
- **What a take writes is unchanged:** QUANTIZE's landing, the track the note goes to (a melody on one track, a chord
  spreading), the note off on the nearest row a row after its note at least (REAL TIME's key off `Kxx`), cells kept
  unless a note lands on them, the take going round.
- **Heard once in the pass it is played:** a cell the take writes on a row still ahead (a note, or its end) stands in
  for itself there that once: the replay does not play it again in that pass, so a note is not started twice, and an
  end written ahead does not cut a note played on that track meanwhile. From the next pass the replay plays the take as
  written, quantized.
- **The start stays:** PLAY held + a key, or COUNT-IN `KEY`, starts the take with that note on the cursor's row, heard
  as the take starts, as before; the count-in's keys are heard, not written.

## 2. Choices made

- The same rule as LOOPS' stage, so the FM-1 has one way of playing live over a grid: heard as played, kept on the line.
- The end stands in as well as the note (unlike the stage, whose keys sound on lanes of their own): a take's keys sound
  on the replay's own tracks, so an end the replay played later would cut the next note on that track.

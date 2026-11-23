# Proposal: the stage's keys sound at once

Status: **settled** (2026-11-20); its specs are written with the work, and lead from then on. It changes
[../ui/screens/loops.md](../ui/screens/loops.md) (the stage, its help), the firmware's player and LOOPS screen, the
playground and the manual. The stage's file and every other screen stay as they are.

With the stage on and the loops playing, a key played on LOOPS was heard exactly as it was written: a note played
before its QUANTIZE line waited for it, so a key pressed a little early sounded late, up to half a line (a whole beat at
`4 ROWS`). It was meant to let the player hear the take as it will be kept; played live it feels laggy. A key now
sounds the moment it is pressed, as it does everywhere else, and the stage still keeps it on QUANTIZE's line.

## 1. Heard at once, kept on the line

- **A key sounds at its press and ends at its release,** with every QUANTIZE: no wait for the line. Its note is
  written where QUANTIZE lands it, as before (the nearest line, before or after the key; REAL TIME the row it falls
  in, a note delay `EDx` and a key off `Kxx` at its tick); its note off on the nearest row, at least a row after its
  note. Nothing of the writing changes: OVERDUB's lanes, a note moved on with a held key, OVERWRITE, the line, the
  insert, the stage's room.
- **Heard once in the pass it is played:** a note landing on a row still ahead is not started again by the stage when
  the line reaches that row (it stands in for its cell, as a note asked for ahead did); from the next pass the stage
  plays it where it is written, quantized. A key let go before its row comes is heard as it was played, short; the
  stage keeps the note from its row.
- **What is heard and what is kept differ by the landing only:** the stage's next passes play the take on the grid.

## 2. What stays

- **The limits:** POLY four keys at once on the stage's four lanes (a fifth key neither heard nor written), MONO one.
  The stage-off keys keep their 16.
- **MONO LEGATO** (GLO): a key played over a held one takes over as `GLIDE`, `LEGATO` or `JUMP` says, now at the
  press, and is written so (`3xx`, `LEG`, a TB-303's `SLD`), as before.
- **The stage plays as before:** only the keys' own sound moves to their press. What the stage holds, writes and plays
  is unchanged: the notes go onto it with QUANTIZE and MONO LEGATO as before, a key's lane (MONO the one lane) is held
  from where its note lands, and a note sounding there is cut there, so the stage's note plays on until the key's note
  lands, in MONO as in POLY.

## 3. RECORD LIVE

The song writes the stage's keys as before: as they land on the stage, by QUANTIZE (the stage-off keys as played). A
key played early is written on its line ahead, its end on the row the stage gives it; one let go before its row is
written as the stage keeps it.

## 4. Choices made

- At once for every QUANTIZE, not only for small ones: a key that sounds late is what was felt as lag.
- The stage's playback untouched, MONO too: a MONO key's sound and the stage's note it will cut may overlap until the
  landing, at most half a line, rather than the stage playing anything other than what it holds.
- RECORD LIVE keeps QUANTIZE's landing, so the song holds the take as the stage keeps it.
- The EDITOR's realtime takes on the FM-1 ([../ui/screens/editor.md](../ui/screens/editor.md#realtime-takes)) keep
  their wait for the grid line: this change is LOOPS' stage only.

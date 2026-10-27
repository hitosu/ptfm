# LOOPS

Status: **settled** (2026-10-24). The record: [the loop station proposal](../../proposals/202610240236-loops-station.md),
which replaced the first LOOPS ([the UI proposal](../../proposals/202610072229-ui.md), section 6.5).

LOOPS is for playing live, like a loop station: a layer is recorded in one pass, then the next over it, without
stopping the music; what is kept is a stack of whole takes, muted, soloed and mixed while playing over them, and
then the whole performance can be recorded into a song. The loops' record: [../../files/loops.md](../../files/loops.md).
LOOPS has its own FX setup and metronome ([fx.md](fx.md)), so it sounds the same whatever song is current, and SAVE
keeps the loops like any change.

There is no precise editing in LOOPS: SAVE TO PATTERN stores the loops in a pattern to edit in the EDITOR, and LOAD
FROM PATTERN brings a pattern back as loops ([below](#storing-in-a-pattern-and-loading-one)).

**LOOPS is a screen of the round** (HOME + POLY, or HOME + SELECT between FX and SETTINGS;
[../navigation.md](../navigation.md)), with a playback of its own:
- **what plays plays on into LOOPS** (a song, the EDITOR's pattern loop) until the loops start here: PLAY, or a
  take's start; it then pauses and keeps its place. With no loops yet and nothing armed, PLAY pauses it;
- **the loops play on outside LOOPS** until a song or a pattern starts; there PLAY pauses and resumes them, as what
  played last ([../transport.md](../transport.md));
- **leaving LOOPS for any screen but FX ends a take** as REC does, disarms an armed one, and ends a song's recording
  ([below](#record-live)), so the keys elsewhere never write into a loop;
- HOME tap in LOOPS opens HOME's combination as on every screen ([../controls.md](../controls.md#homes-combination));
- FX tap does not leave LOOPS: a take or a song's recording goes on, FX works on LOOPS' own setup, and FX again goes
  back ([fx.md](fx.md)); going on elsewhere from that FX leaves LOOPS then.

## The loops

- **Up to 16 loops,** listed top to bottom in the order they were made, the newest at the bottom; under them the
  placeholder `+ NEW LOOP` while fewer than 16 exist. A loop's number is its place, `00`-`0F`, renumbered when a
  loop above it goes; in CONTROL mode ([below](#control-mode-mutes-and-solos)) the rows show their white keys,
  `1`-`16`.
- **One length for all,** set by the first loop in whole bars (a bar the METRONOME's BEATS A BAR beats, each GRID's
  rows a beat: [fx.md](fx.md)); 128 rows at most: 8 bars at 1/16, 5 at 1/16T, 4 at 1/32 (4 beats a bar). After the
  first loop it changes only with GLO's LENGTH and SEL's DOUBLE.
- **A loop has one instrument and one colour of its own** (not its engine's: [below](#colours)), given when it is
  made. PRESETS and ENV pick the instrument the keys play and the next loop takes, never a recorded loop's; SEL's
  APPLY INSTRUMENT gives the selected loop the current one. Selecting a loop leaves the current instrument as it
  is. A kit is just a loop's instrument, each note a drum.
- **A loop keeps its notes once, its base, plus a repeat count.** It plays its base from the loops' start as many
  times as its count says, then rests until the length's end; where the length ends inside a repeat, that repeat is
  cut there, drawn and played so. So DOUBLE, a pattern repeated and a length change never copy notes.
- **POLY or MONO, chosen before it records** (the black keys MONO and POLY in CONTROL mode; POLY at first). A POLY
  loop holds up to 4 notes at once, its lanes. A MONO loop holds one: a note played over a held one takes over as
  GLO's MONO LEGATO says: `GLIDE` (on a TB-303 its slide, elsewhere a short, clear glide to the note), `LEGATO` (the
  held note moves to the new one at once, with no new attack: its sound goes on, [../../files/commands.md](../../files/commands.md#legato);
  on a TB-303 its slide, on a FUSION instrument a plain new note) or `JUMP` (a plain new note: the held one ends, the
  new one starts with its attack).
- **Keys pressed together that land on the same row compete for it:** the one registered last wins (the keys keep
  their order of registration, also within one scan).
- **What a loop holds is notes:** their volumes (KEYS VOLUME), note offs, and with QUANTIZE REAL TIME their note
  delays (`EDx`) and key offs (`Kxx`); a MONO loop's glides and legatos (a TB-303's `SLD`, elsewhere `3xx` or
  `LEG`). The loops' mix
  turned, FX changes and punch-ins are heard live and written only by RECORD LIVE.
- **A loop's mix** is a track's mix (LEVEL, PAN, DUCK, CHORUS, DELAY and REVERB SENDS), set by K1-K4 and FX's
  MIXER, where the loops are the columns, with the KEYS column ([fx.md](fx.md#mixer)). **A new loop starts with the
  next loop's mix,** LOOPS' own (K1-K4 on `+ NEW LOOP`; plain at first, as a new song's track; the KEYS column is not
  touched).
- **Voices:** 16 loops of 4 lanes can sound more notes than VOICES allows; parking handles them as any notes
  ([settings.md](settings.md#voices)).

## Recording

- **REC records where the selection is:** on `+ NEW LOOP` a new loop of the current instrument, POLY or MONO as the
  next loops record; on a loop, a re-record of that loop.
- **While the loops play, REC arms:** the take starts at the next loops' start. The row says how long until then
  (`ARMED: STARTS IN 3 BARS`, `... 1 BAR`, `... 2 BEATS`, `... 1 BEAT`) and the top line's `REC` is red.
- **With nothing playing, REC arms and waits:** the first key starts the loops from their start and the take with
  that note, at once; PLAY starts them after GLO's COUNT-IN (OFF at first: at once; 1 or 2 bars of clicks,
  [../transport.md](../transport.md#count-in)). The place the loops were paused at is dropped: a take starts at the
  loops' start. What played on into LOOPS pauses as the take or the count starts.
- **A take is one pass of the length and stops by itself,** the loops playing on; REC during it stops it early, what
  was played kept and the rest of the pass empty. It never rolls into a new take. All other loops play while it
  records. REC while armed disarms (`CANCELLED`), as do PLAY and HOME during a count-in.
- **The first loop** has no length to fill: it grows a bar at a time while it records, up to 128 rows, where it
  closes by itself and plays on. REC closes it on a bar line: before a line it runs on to the line, the rest of the
  bar silent; up to a beat after a line it is cut back to that line, and what was played after it is not kept. The
  bars recorded are the length. Nothing played: no loop (`NOTHING PLAYED: NO LOOP`).
- **A re-record deletes the loop's notes the moment it fires,** armed included (`LOOP 03 RE-RECORDING`): the loop is
  silent from then on and never comes back with its old notes. It keeps its place, colour, instrument, mix and MONO
  / POLY; from REC on the keys play its instrument, until the take ends.
- **The selection after a take:** recorded on `+ NEW LOOP`, the selection is on `+ NEW LOOP` again, ready for the
  next layer; a re-record leaves it on the loop.
- **RE-RECORD and PEEL:** EDIT re-records and LFO peels (removes) the selected loop; on `+ NEW LOOP` both act on the
  last loop. EDIT arms as REC does. A peel leaves the selection where it was, so after peeling a loop in the middle
  the selection is on the loop that moved into its place (`LOOP 03 PEELED`); peeling the only loop takes the length
  with it, as CLEAR LOOPS does. Neither asks: SEL's REVERT brings back what was saved.
- **The rule while recording: the keys sound exactly as they are recorded.** Quantized as QUANTIZE says (a note
  played before its line waits for it; one just after the line sounds at once, as close to the line as it can, and
  is written on the line); MONO one note at a time, legato as MONO LEGATO says; POLY at most 4 notes at once, a
  fifth neither heard nor written. Not recording, the keys play freely: every voice, no grid.
- **During a take the keys sound through the recording loop's mix and LOOPS' effects,** not through KEYS GAIN or the
  keys' DRY ([fx.md](fx.md#the-keys-dry)), so the loop plays back as it sounded; not recording, the keys sound as on
  every screen.
- **Note offs** go on the nearest row, at least a row after their note (REAL TIME: the key off at its tick). A note
  still held when the take ends ends at the length's end.
- **Lanes:** a POLY loop's note takes the first lane free where it lands (a lane is taken from its note to its note
  off); a MONO loop's notes all go on one lane, and a key let go ends its note only while that note sounds (no
  return to a key still held). Keys pressed together on a MONO loop that land on the same row: the last registered
  is written and heard.
- **The edit memory full** ([../../files/ram.md](../../files/ram.md)) ends a take there, what was written kept
  (`MEMORY FOR EDITS IS FULL`).

## QUANTIZE

GLO's QUANTIZE and CONTROL mode's black keys OP1-OP5; `ROW` at first. The EDITOR's realtime takes have the same
values in SETTINGS' QUANTIZE ([settings.md](settings.md), [editor.md](editor.md#realtime-takes)).

| Value | What a note lands on |
|---|---|
| `2 ROWS`, `3 ROWS`, `4 ROWS` | the nearest line of that many rows, from the loops' start |
| `ROW` | the nearest row (the 1/32 rows too at GRID 1/32) |
| `REAL TIME` | the row it falls in, with a note delay `EDx` of the ticks since, and its end as a key off `Kxx` at its tick ([../../files/loops.md](../../files/loops.md#recording-on-the-grid)) |

## The pattern recorder (ARP)

ARP opens it over the loops; the loops play on, so the pattern is heard in the mix while it is built. Refused while a
take records or is armed (ARP blinks) and while a song is armed or recorded (`RECORDING A SONG`).

- **4 rows of 16 steps, a note a row** (with a kit, each row a drum). **SELECT** picks the row the white keys switch;
  a white key switches its step on or off, and a step switched on sounds its note while the preview is stopped. Keys
  past LENGTH do nothing.
- **The selected row's note:** K3 a semitone a click, heard; or **SEL tapped, then a key:** that key, white or black,
  sets the note at the keyboard's octave, heard, and the keys go back to the steps (`KEYS: THE NOTE`, `KEYS: THE
  STEPS`; SEL again before a key: back to the steps); **SEL held + a key** does the same at once, a shortcut. OCT- /
  OCT+ move the keyboard's octave as on every screen, and the selected row's note with it, heard (`NOTE C-5`); of the
  other rows only those never set below it change (they take its note). The first row starts on C at the keyboard's octave when the recorder first opens, and keeps
  it from then on; a row never set starts on the note of the row above it. SEQ does
  nothing here.
- **K1 STEP SIZE:** a step's length, those GRID makes whole rows of (`1/32`, `1/16T`, `1/16`, `1/8T`, `1/8`, `1/4`:
  at 1/16 `1/16`, `1/8`, `1/4`; at 1/16T `1/16T`, `1/8T`, `1/8`, `1/4`; at 1/32 `1/32`, `1/16`, `1/8`, `1/4`; `A ROW`
  first where none is a row long); 1/16 at first. **K2 LENGTH:** `4`, `8`, `12` or `16 STEPS`; 16 at first.
- **K4 TIMES, ×1-×8, only for the first loop:** the length is the pattern's rows (LENGTH × the step's rows) × TIMES,
  rounded up to whole bars (128 rows at most: TIMES stops there), the rest of the last bar silent (16 steps of 1/16
  ×4: 4 bars); ×1 at first. **With loops, the pattern always covers their length:** its steps repeat from the loops'
  start and the last repeat is cut at the length's end (12 steps over 4 bars of 16 rows: 5 repeats and 4 steps), so
  it starts again with the loops each pass; the K4 line reads `FILLS 4 BARS`, dark grey, and K4 does nothing.
- **The notes' length, GLO:** `RING TO THE NEXT` (each note rings until its row's next note; at first) or `A STEP
  LONG` (a note off a step after each note whose next step is off); the toast `NOTES RING TO THE NEXT`, `NOTES A STEP
  LONG`.
- **The preview plays from the moment it opens,** in the instrument the new loop takes (opened on a loop, the loop's)
  and through the mix it takes. ALGORITHM turns the tempo and PLAY plays and pauses the loops, as on the screen.
  - **While the loops play,** the step playing is where the loops are: the place the pattern lands after REC (its
    first step on the loops' start), moving with them. ARP only switches the pattern's sound off and on.
  - **While they do not,** ARP starts and stops the preview, alone at LOOPS' tempo, from its first step.
  - **PLAY pausing the loops** leaves the preview going on its own from where it was; ARP then stops it.
  - **PLAY starting the loops** makes a preview that plays jump to the loops' place, in time with them from then on.
- **REC writes it, at once, and closes:** on `+ NEW LOOP` a new loop of the current instrument; opened on a loop, it
  replaces that loop's notes, its place, colour, instrument and mix kept. Rows with no step on are left out: the
  rows filled are the loop's lanes, a POLY loop, or a MONO one with one row filled (MONO / POLY play no part here).
  Each step on is a note at KEYS VOLUME; its base is the pattern (repeated as the length needs: its repeat count, the
  last repeat cut), so nothing is written out. The new loop goes on from where the preview was, without a break; with
  no loops, the loops start then with the preview's place in the new length. No step on: refused (`NO STEPS ON`, REC
  blinks). **HOME** closes it, nothing written.
- **What is set stays** for the next time until power-off (the steps, notes, sizes); it is not saved.

## CONTROL mode, mutes and solos

**SEQ switches the keys between PLAY (notes) and CONTROL** (`KEYS: CONTROL`, `KEYS: PLAY`); in CONTROL mode SEQ's
place in the header reads `SEQ MUTE` or `SEQ SOLO` ([../frame.md](../frame.md#the-header)) and the loops' rows show
their keys.

- **White keys 1-16: loops 1-16** from the top. In MUTE, a tap mutes or unmutes the loop at once; held past the hold
  delay ([../controls.md](../controls.md)), it waits for the next bar line, the row showing a yellow `M` meanwhile.
  In SOLO the same for solos, one loop at a time: a key solos its loop alone (the other solos off), and on the soloed
  loop takes its solo off; held, each solo that changes waits for the bar line (a yellow `S` meanwhile). While a
  loop is soloed only it sounds; mutes are kept for when the solo clears. Stopped, both act at once. Mutes are
  saved with the loops; solos are not (none after a power-on, [../../files/loops.md](../../files/loops.md)). A key
  with no loop does nothing.
- **Black keys, by their print:** OP1, OP2, OP3 QUANTIZE `2 ROWS`, `3 ROWS`, `4 ROWS`; OP4 `ROW`; OP5 `REAL TIME`
  (`QUANTIZE: 2 ROWS`, ...); PIT all back on, every mute and solo cleared (`ALL LOOPS ON`); the key printed GLO MUTE
  ↔ SOLO; MONO and POLY how the next loops record (`NEXT LOOPS: MONO`, `NEXT LOOPS: POLY`; the footer shows it). OP6
  and the unprinted key do nothing.
- **HOME's combination,** on every screen ([../controls.md](../controls.md#homes-combination)): white key *n* mutes or
  unmutes loop *n* at once, held solos it, after SEQ solos (key 1 the top loop; a solo as CONTROL's, one loop at a
  time).
- **A muted loop runs on silently,** in time; its row is dark grey. So is a loop silenced by another's solo.
- **Mutes are kept with the loops** (SAVE keeps them); solos are not.

## RECORD LIVE

- **SEL → RECORD LIVE opens its panel** ([below](#the-record-live-panel)): the new song's **NAME** (`LOOPS 01`, or
  one past the highest `LOOPS ##` a song has; EDIT opens the text entry, [../dialogs.md](../dialogs.md#text-entry))
  and **K1 TEMPO**, LOOPS' own tempo, changed at once and heard (the song starts at it). **REC (ARM)** arms it and
  closes the panel; **HOME (CANCEL)** closes it, nothing armed. Armed: REC blinks, the top line's `REC` reads `SONG`,
  red, and SEL's item reads `CANCEL RECORD LIVE` (`CANCELLED`).
- **Armed, it starts with:** REC (nothing else starts: the loops go on as they were, playing or not); a note (the note
  its first); PLAY, when the loops did not play as it was armed (the loops and the song start together, after
  COUNT-IN as a take from silence). Whether the loops play does not matter for arming.
- **The song's time runs on from its start whatever the loops do:** while it records, the loops' replay never stops,
  and PLAY, mutes and solos work any time without ending it:
  - **PLAY, the loops not playing:** they play on the song's next row, from where they stopped;
  - **PLAY, the loops playing:** they fall silent from the song's next row, their place kept; silent rows are written
    as they are heard;
  - **PLAY held:** the loops go to their start: playing, from the top on the song's next row; not playing, they wait at
    their start;
  - **the keys** sound as they are written, the loops playing or not (the rule of [Recording](#recording): QUANTIZE,
    MONO / POLY as the next loops record).
- **It writes a new song** at the first free number, under the panel's NAME: a pattern every 128 rows from its start,
  a position each, in order; no pattern is shared and none is aligned to the loops. Each pattern holds what was heard
  in its rows:
  - the loops' notes as they played, repeats written out; rows they were silent, empty; a muted or non-soloed loop
    left out from the row its mute took effect; a note sounding where the loops fall silent gets a note off there;
  - the keys on their own tracks, in the current instrument;
  - FX turns, effects switched on and off, punch-ins, tempo changes and the loops' mix turned, as commands at their
    rows; the loops' mix at the start as their tracks' mix. Instruments' parameters (turned from a MIDI map) are not
    written.
- **The song takes LOOPS' tempo, swing and FX setup at its start;** it plays once and stops at its end.
- **REC ends it at once:** the row REC came in is the song's last, the notes sounding end there, and the loops play on
  (`SONG 12 RECORDED: 1:23, 5 PATTERNS`: the time recorded and the patterns written). Ended before its first row:
  nothing written (`CANCELLED`).
- **A song records a performance over the loops as they are:** while it is armed or running, nothing changes the
  loops: takes, RE-RECORD, PEEL, the pattern recorder, APPLY INSTRUMENT, DOUBLE, LENGTH, GRID, CLEAR LOOPS, LOAD FROM
  PATTERN and REVERT are refused (`RECORDING A SONG`, the key blinking). Mutes, solos, the keys, FX, punch-ins, tempo
  and the loops' mix stay live, and are written.
- **16 tracks:** the loops' lanes and the keys' notes are packed into the pattern's tracks, lanes sharing a track
  where they do not overlap; what does not fit is heard live, left out, and counted (`SONG 12: 3 NOTES LEFT OUT`, at
  its end).
- **Leaving LOOPS for another screen ends it at once,** what was written kept; FX and back, ENV's cartridge list,
  PRESETS and the help are not leaving.
- **It is an edit, kept in the edit memory:** the FM-1 cannot write the flash while sound plays, so the song is
  built in RAM and SAVE keeps it. It runs until the edit memory, the pattern numbers or the song's 256 positions run
  out, then stops and says why (`SONG 12 STOPPED: MEMORY FOR EDITS IS FULL`, `SONG 12 STOPPED: ALL 240 PATTERNS
  USED`, `SONG 12 STOPPED: 256 POSITIONS`). With no song number free it is refused at once (`NO SONG NUMBER FREE`).

## Storing in a pattern and loading one

- **SEL → SAVE TO PATTERN** opens the pattern list ([../dialogs.md](../dialogs.md#the-pattern-list)), titled `SAVE
  THE LOOPS TO`, with `NEW` first and picked (the first free number); SEL takes it, or another pattern, which asks
  first when it is not empty (`PATTERN 2A IS NOT EMPTY` / `REPLACE IT WITH THE LOOPS?`). It writes one pattern of the
  loops' length (`LOOPS STORED IN PATTERN 2A`): every loop's notes, repeats written out, each note naming its
  instrument, lanes packed into the 16 tracks where they do not overlap (what does not fit left out and counted:
  `PATTERN 2A: 3 NOTES LEFT OUT`); each loop's mix as mixer commands (`=`) with its first note on a track, and again
  where a shared track turns to another loop. The loops go on as they were. A built-in pattern (brown in the list;
  the firmware has none yet) takes them as any: a SAVE keeps the change in the user area
  ([../saving.md](../saving.md#fws)). The EDITOR's own pattern is never listed.
- **SEL → LOAD FROM PATTERN** opens the pattern list, titled `LOAD INTO LOOPS` (its PLAY previews each pattern at the
  timing LOAD brings), and replaces the loops after a question when there are loops (`REPLACE THE LOOPS` / `WITH
  PATTERN 2A?`).
  - **The length** is the pattern's rows in whole bars (rounded up, at most 128 rows).
  - **The timing,** as the pattern plays in its song (the current song if it plays the pattern, else the first song
    that does): its TEMPO, TICKS A ROW, ROWS A BEAT and SWING. ROWS A BEAT that GRID lacks keeps the loops' GRID,
    the TEMPO scaled to the same BPM; a pattern in no song keeps the loops' own.
  - **A loop an instrument:** its notes from every track that plays it, up to 4 at once as its lanes; more at once
    makes another loop of the same instrument. Loops are made in the order their instruments first play. Each loop's
    repeat count is 1, POLY, its mix from the `=` commands it finds with its notes (else plain), its glides (`SLD`,
    `3xx`), legatos (`LEG`) and its note delays and key offs kept.
  - **As much as fits:** 16 loops, 4 lanes each; the rest, and the commands a loop does not hold, left out and
    listed (`PATTERN 2A IN LOOPS: 3 LOOPS`; `PATTERN 2A IN LOOPS: 3 INSTRUMENTS LEFT OUT`, `... 12 COMMANDS LEFT
    OUT`).

## Controls

| Control | Does |
|---|---|
| SELECT | the loops, top to bottom, then `+ NEW LOOP` (clockwise down, as in every list) |
| PLAY | plays / pauses the loops (with no loops: pauses what plays, else refused); armed with nothing playing: starts them and the take after COUNT-IN. Held: stop, back to the start ([../transport.md](../transport.md)). Recording a song: the loops play or fall silent on the song's next row; held, they go to their start ([above](#record-live)) |
| REC | records at the selection: a new loop or a re-record ([above](#recording)); again: stops the take early (the first loop: closes it on a bar line); armed: disarms. RECORD LIVE armed: starts it; recording a song: ends it at once |
| EDIT | RE-RECORD: the selected loop (on `+ NEW LOOP`: the last), armed as REC arms |
| LFO | PEEL: removes the selected loop (on `+ NEW LOOP`: the last) |
| keys | PLAY mode: play the current instrument (from a re-record's REC, the loop's), writing into the take while one records. CONTROL mode: [above](#control-mode-mutes-and-solos) |
| SEQ | the keys: PLAY ↔ CONTROL |
| ARP | the pattern recorder ([above](#the-pattern-recorder-arp)); held: nothing (no arpeggiator in LOOPS) |
| ALGORITHM | TEMPO, any time, playing or not, as GLO's TEMPO turns it (SEL held: by 10) |
| K1-K4 | the selected loop's LEVEL, PAN, DELAY SEND, REVERB SEND (CHORUS and DUCK on FX's MIXER); on `+ NEW LOOP` the next loop's; SEL held: steps of 16 |
| PRESETS | the current instrument: the keys' and the next loop's, never a recorded loop's |
| ENV | the cartridge list, titled `NEW LOOP: A CARTRIDGE`: the current instrument becomes the picked cartridge's first |
| OCT- / OCT+ | the keys' octave |
| FX | LOOPS' FX setup, FX again back; FX held + a white key: a punch-in (heard; written only by RECORD LIVE) |
| GLO | the properties ([below](#the-properties-glo)) |
| SEL | the menu ([below](#the-sel-menu)) |
| HOME | HOME's combination, as on every screen ([above](#control-mode-mutes-and-solos)) |

- **The LEDs** ([../leds.md](../leds.md)): REC dim, blinking while armed (a take or a song), full while a take or a
  song records; EDIT and LFO dim while there is a loop, dark with none; SEQ dim in PLAY mode, full in CONTROL mode;
  ARP dim, full while the pattern recorder is open; PLAY as for a loop ([../transport.md](../transport.md)). In
  CONTROL mode: white key *n* full while loop *n* sounds, dim while it is muted or silenced, blinking while its mute
  or solo waits for the bar line, dark with no loop; the black keys OP1-OP5 full on the QUANTIZE chosen, dim
  otherwise; MONO or POLY full by the next loops' mode, the other dim; GLO full in SOLO, dim in MUTE; PIT dim. In
  PLAY mode the keys light with their notes. HOME's combination open: white key *n* as in CONTROL mode.

## Layout

The body is y 17-226; the frame's header and footer: [../frame.md](../frame.md). The top line, then a loop a row,
all 16 on show.

### The loops

Six loops, loop 04 selected, the play line in bar 3 of 4:

```
LOOPS                         > |##9     S, 16 px: the name; the transport, CPU, voices, battery
120 BPM 4 BARS  Q ROW    REC     ## .. ..  T: tempo, length, QUANTIZE, REC; the beats
----------------------------------------
00 KICK     P X4   ##.#.##.#.##.#.##.#.  a loop a row: number, sound, P / M, repeats, state,
01 OPEN HAT P X4   .#..#..#..#..#..#..#    its strip (4 bars, a lane a voice)
02 SNARE    P X2   ..#...#...#...#...#.
03 SUB BASS M      ##.##.###.##.##.###.
04 WARM PAD P      ====....====....====  the selected loop, boxed
05 E.PIANO  P      ..=...=....=...=....
   + NEW LOOP      01:04 BRASS           the placeholder and the next loop's instrument
----------------------------------------
01:04 BRASS JUNO                    POLY the current instrument; the next loops' mode
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the tempo | 2, 19 | | T | `120 BPM`: the loops' tempo as BPM |
| the length | 50, 19 | | T | `4 BARS`, `1 BAR`; `-- BARS` before the first loop; while the first loop records, its bars so far |
| QUANTIZE | 98, 19 | | T | `Q 2 ROWS`, `Q 3 ROWS`, `Q 4 ROWS`, `Q ROW`, `Q REAL` |
| REC | 152, 19 | | T | `REC`; `REC 3/4` while a take records (its bar of the length; the first loop: `REC 3`); `SONG` while a song is armed |
| the beats | `194 + k × w`, 21 | `(w − 2)` × 8, `w = floor(44 / BEATS A BAR)` | | the bar's beats; while the loops play, the playing one lit |
| the line | 0, 31 | 240 × 1 | | |
| a loop's row | 0, `y = 33 + 12 i` | 240 × 12 | | loop `i`, all on show |
| its number | 2, y | | T | `00`-`0F`; in CONTROL mode its key, ` 1`-`16` |
| its sound | 20, y | | T | the instrument's name, 9 characters |
| its mode | 80, y | | T | `P` POLY, `M` MONO |
| its repeats | 91, y | | T | `X2`-`X9` while its base repeats, `X+` past 9; nothing when it plays once |
| its state | 104, y | | T | `M` muted, `M` a mute waiting, `S` soloed, `S` a solo waiting, `R` recording; else nothing |
| its strip | 111, y | 128 × 11 | | [below](#the-strips) |
| its box, when selected | 0, y − 1 | 240 × 13 | | an outline |
| an armed loop | 80, y | | T | `ARMED: STARTS IN 3 BARS` in place of its mode, repeats, state and strip |
| the placeholder | 20, y; 111, y | | T | `+ NEW LOOP` and the next loop's instrument (`01:04 BRASS`, its name 9 characters); armed: `06 ARMED: STARTS IN 1 BEAT` at 20 in place of both |
| its box, when selected | 0, y − 1 | 240 × 13 | | an outline |
| the play line | the play row's x in the strips, 33 | 1 × (12 × rows shown − 1) | | through every strip, the placeholder's row excepted |

### The strips

- **A strip** shows the length, or 4 bars of it when longer, the rows spread over its 128 px: a lane a voice, its
  11 px split among the loop's lanes (1 lane: 11 px; 2: 5 + 5; 3: 3 × 3; 4: 2 × 4, a pixel apart), so a MONO loop
  fills it all. Each note is a block on its lane from its start to its end (its lane's next note, note off or key
  off, or the base's end), a pixel's gap only before a note that starts right where it ends; the loop's own notes
  (its base, the first time) in its colour, its repeats in that colour's deep shade, nothing past them. Black lines
  at the bars; the strip's ground deep grey.
- **The window:** a length of up to 4 bars shows whole. A longer one shows 4 bars: while the loops play, the play
  line moves from the start to the middle, stays there while the strips scroll under it, and goes on to the end
  once the window meets the length's end (the window's first row clamp(play − 2 bars, 0, length − 4 bars)); when the
  loops start again, the window and the line jump back to the start. Stopped, the window stays where the play place
  puts it.
- **The first loop while it records** shows 4 bars; past 4 its window scrolls as above, the loop's end the bars
  recorded so far.

### Colours

- **Each loop's colour,** given when it is made and kept: light red, yellow, light green, light cyan, light blue,
  light magenta, brown, green, cyan, red, magenta, light grey, then round again (the next colour after the newest
  loop's; the first loop light red).
- The header's `LOOPS` in the PLAY group's colour, `BETA` after it small (T, 2 px on, top-aligned with LOOPS'
  capitals) in dark grey ([../frame.md](../frame.md#the-header)). The top line light grey; QUANTIZE cyan; the length red while the
  first loop records; `REC` dark grey, red while armed or recording, `SONG` red; the beats deep green, the one
  playing yellow on the bar's first beat and green on the others.
- A loop's number cyan (in CONTROL mode, its key white), its sound in its colour, its mode and repeats light grey; a
  muted or silenced loop's number, sound, mode, repeats and notes dark grey; the recording loop's number, sound and
  notes red. `M` dark grey on a muted loop, yellow while a mute waits; `S` yellow; `R` red. `ARMED: ...` red.
- The placeholder's `+ NEW LOOP` white (dark grey while a take records), the next loop's instrument in its engine's
  colour. The boxes white; the line dark grey; the play line white.

### The footer

- **Left:** the current instrument (`01:04 BRASS JUNO`, as on every screen); from a re-record's REC until its take
  ends, the loop's.
- **Right:** `POLY` or `MONO`, how the next loops record; `16 OF 16` with all 16 loops; `MEMORY 81% FREE` while a
  song records (the edit memory left), red.
- **A knob turned shows in the header,** as on every screen ([../frame.md](../frame.md#the-header)): `TEMPO 78 (120
  BPM)`, `LEVEL 7F 100%`, `PAN 80 MIDDLE`, `DELAY SEND 20 25%`, `REVERB SEND 20 25%`.

### No loops yet

```
LOOPS                           |###
120 BPM -- BARS  Q ROW   REC     .. .. ..
----------------------------------------
   + NEW LOOP      11:03 CHIPIN          the placeholder, selected
                                         
REC, THEN PLAY OR A NOTE:                at 2, 77
THE FIRST LOOP SETS THE LENGTH           at 2, 89

PRESETS, ENV: ITS SOUND                  at 2, 113
----------------------------------------
11:03 CHIPIN FUSION                 POLY
```

Armed, the first line reads `PLAY OR A NOTE: THE FIRST LOOP` (and the second goes). Colours: the lines light grey,
`PRESETS` and `ENV` yellow.

### Recording

- **Armed:** `REC` red; the row armed (the placeholder, or the loop re-recorded, its strip empty) with `ARMED: STARTS
  IN 3 BARS` in red, counting down; from silence `ARMED: A NOTE OR PLAY`.
- **A take:** the header's transport ● red ([../frame.md](../frame.md)); `REC 3/4` red; the recording loop's number,
  sound and notes red, its state `R`, its strip filling as it records; the placeholder dark grey.
- **The first loop:** the length counts up in red (`1 BAR`, `2 BARS` …), `REC 3` red.
- **A song recording:** the top line reads `SONG 0:42  5 PATTERNS  120 BPM`: `SONG` red at 2, the time recorded at
  32 (`M:SS`; `MM:SS` from 10 minutes, at 26), its patterns so far at 68, the one being written counted (`1 PATTERN`,
  `5 PATTERNS`, in decimal), the tempo at 146, light grey; the beats as ever; the loops' play line, while they are
  silent, at their place. The footer's right `MEMORY 81% FREE`, red.

### The RECORD LIVE panel

A panel over the loops, the top line in view, as RECORD PATTERN's ([../dialogs.md](../dialogs.md#record-pattern)):

```
LOOPS                           |###
120 BPM 4 BARS  Q ROW    REC     ## .. ..
 +-------------------------------------+
 |             RECORD LIVE             |   the title, T
 | NAME                       LOOPS 03 |
 | K1 TEMPO                    124 BPM |
 | EDIT: NAME                          |
 |                                     |
 |         HOME              REC       |   the buttons, T
 |        CANCEL             ARM       |   the answers, S, 16 px
 +-------------------------------------+
11:03 CHIPIN FUSION                 POLY
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 8, 68 | 224 × 108 | | a black box with a 1-px outline, centred in the body |
| the title | centred in 8-231, 74 | | T | `RECORD LIVE` |
| line *i* | 14, 92 + 12 × *i*; the value right-aligned to 226 | | T | `NAME` `LOOPS 03`, `K1 TEMPO` `124 BPM` |
| EDIT | 14, 120 | | T | `EDIT: NAME` |
| the buttons, the answers | as a question's, from 136 (no on the left, yes on the right) | | T, S | `HOME` `CANCEL`, `REC` `ARM` |

Colours: the outline light grey; the title white; `K1` and `EDIT` yellow, the lines' names light grey, their values
white; the buttons and answers as a question's. ALGORITHM turns the tempo too, as on the screen; the keys play as
on the screen (nothing recorded); REC blinks while the panel is open.

### The pattern recorder

A box over the loops, the top line in view:

```
LOOPS                         > |##3
120 BPM 4 BARS  Q ROW    REC     ## .. ..
+--------------------------------------+
| RECORD PATTERN: NEW LOOP     PREVIEW |   the title; PREVIEW green while it plays
|     1       2       3       4        |   the beats over the steps
| C-2 ## .. .. .. ## .. .. .. ## ..    |   4 rows: the note, LENGTH's steps in fours
|[D-2 .. .. .. .. ## .. .. .. .. ..   ]|   the selected row, boxed
| F#2 .. .. ## .. .. .. ## .. .. ..    |
| D#2 .. .. .. .. .. .. .. .. .. ..    |   an empty row: its note dark grey
| K1 STEP SIZE                    1/16 |
| K2 LENGTH                   16 STEPS |
| K3 NOTE                          D-2 |
| K4 TIMES                          X4 |   (later loops: FILLS 4 BARS, dark grey)
| 16 X 4 = 64 ROWS = 4 BARS, 3 LANES   |   what it makes
| SELECT: ROW  SEL: NOTE  KEYS: STEPS  |
+--------------------------------------+
F4:00 KIT FUSION              REC: WRITE
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the box | 4, 36 | 232 × 186 | | black, a 1-px outline |
| the title | 10, 40 | | T | `RECORD PATTERN: NEW LOOP`, `RECORD PATTERN: INTO LOOP 03` |
| PREVIEW | 188, 40 | | T | `PREVIEW` |
| the beats | `x(k) + 3`, 56, `k` = 0, 4, 8, 12 | | T | `1`-`4`, over each four steps of LENGTH |
| row `r`'s note | 8, `71 + 17 r` | | T | `C-2`, `F#2` |
| step `k` of row `r` | `x(k) = 30 + 12 k + 2 × floor(k / 4)`, `70 + 17 r` | 11 × 13 | | LENGTH's steps |
| the selected row's box | 6, `68 + 17 r` | 228 × 17 | | an outline |
| line `i`, K1-K4 | 12, `140 + 12 i`; the value right-aligned to 228 | | T | `K1 STEP SIZE` `1/16`, `K2 LENGTH` `16 STEPS`, `K3 NOTE` `D-2`, `K4 TIMES` `X4` (later: `FILLS 4 BARS`) |
| what it makes | 12, 192 | | T | the first loop: `16 X 4 = 64 ROWS = 4 BARS, 3 LANES` (the pattern's rows × TIMES; rounded up: `12 X 1 = 12 ROWS IN 1 BAR, 1 LANE`); later loops: `5 X 12 + 4 = 64 ROWS, 2 LANES` (the whole repeats × the pattern's rows + the cut one's rows = the length), `4 X 16 = 64 ROWS, 2 LANES` |
| the keys' line | 12, 206 | | T | `SELECT: ROW  SEL: NOTE  KEYS: STEPS` |

Colours: the outline light grey; the title cyan; `PREVIEW` green while the preview plays, dark grey otherwise; the
beats dark grey; a row's note in the loop's colour (the new loop's: the next colour), white on the selected row,
dark grey on an empty row; a step on in the loop's colour, the step playing white; off deep grey, the step playing
deep blue; the box white; the K lines' names cyan, their values light grey, K3's white; K4's name and value dark
grey on a later loop; what it makes light grey; the keys' line dark grey. The footer: the instrument the preview
plays, and `REC: WRITE`.

**The keys' LEDs:** a step on full; off dark, but steps 1, 5, 9 and 13 dim; past LENGTH dark; the step playing turned
over (on: dark, off: full). While a key sets the note (after SEL): the row's note's key full when the keyboard's
octave has it, the rest dark. ARP full; REC dim; SEL full while it waits for a key.

### The properties (GLO)

The properties dialog ([../dialogs.md](../dialogs.md#properties)), titled `LOOPS PROPERTIES`:

| Item | Shows | K1 |
|---|---|---|
| TEMPO | `78 (120 BPM)`: as a song's, and its BPM (ALGORITHM turns it too) | 3-600; SEL held: by 10 |
| SWING | `50 %` | 50-75 % |
| LENGTH | `4 BARS`, `1 BAR`; `THE FIRST LOOP SETS IT` | a bar more or less, 1 to the most: longer adds silence at the end; shorter cuts bars off the end, asking first when notes would go (`CUT TO 2 BARS?` / `NOTES IN THE BARS CUT GO`, OCT+ `CUT`, OCT- `KEEP`) |
| GRID | `1/16`, `1/16T`, `1/32`: 4, 6 or 8 rows a beat | only before the first loop (`GRID: ONLY BEFORE THE FIRST LOOP`) |
| QUANTIZE | `2 ROWS`, `3 ROWS`, `4 ROWS`, `ROW`, `REAL TIME` | |
| MONO LEGATO | `GLIDE`, `LEGATO`, `JUMP` | |
| COUNT-IN | `OFF`, `1 BAR`, `2 BARS` ([../transport.md](../transport.md#count-in)): before a take PLAY starts; a key always starts at once | |

LENGTH shorter cuts each loop's base and repeats at the new end; a loop left with no notes stays, empty. A shorter
length never asks when no note would go.

### The SEL menu

The menu ([../dialogs.md](../dialogs.md#menus)), titled `LOOP 03 SUB BASS` on a loop or `LOOPS` on `+ NEW LOOP`:

- **APPLY INSTRUMENT** (on a loop): the selected loop takes the current instrument (`LOOP 03: 01:04 BRASS`).
- **DOUBLE** (with loops): the length ×2, every loop playing twice what it played; nothing copied: a loop that fills
  the length exactly doubles its repeat count; one that rests or is cut at the end has its notes written out to the
  old length first, so the second half is the first again (`DOUBLED: 8 BARS`). Refused past 128 rows (`DOUBLE: AT
  MOST 8 BARS`, the most at the GRID) and when the edit memory cannot hold it (`MEMORY FOR EDITS IS FULL`).
- **RECORD LIVE** (with loops): its panel, to arm the song's recording ([above](#record-live)); while armed, the item
  reads `CANCEL RECORD LIVE`.
- **SAVE TO PATTERN** (with loops), **LOAD FROM PATTERN** ([above](#storing-in-a-pattern-and-loading-one)).
- **CLEAR LOOPS** (with loops): every loop goes, after a question (`CLEAR ALL 6 LOOPS?` (`1 LOOP`) / `THE NEXT ONE
  SETS THE LENGTH`, OCT+ `CLEAR`, OCT- `KEEP`; then `LOOPS CLEARED`); the length goes with them (the next first loop
  sets it), the properties, the FX setup and the next loop's mix stay.
- **REVERT** (while LOOPS has something unsaved): LOOPS back as saved, after a question (`REVERT LOOPS TO SAVED?`,
  OCT+ `REVERT`, OCT- `KEEP`; then `REVERTED`): LOOPS' unsaved pieces are dropped from the edit memory and what the
  flash holds plays again, from the next row. No copy is kept for it; it is not an undo.

## Saving

**SAVE keeps the whole LOOPS setup,** as DJ's stand is kept: the loops (notes, instruments, colours, mixes, MONO /
POLY, mutes; never a solo), the length, the properties, the next loop's mix, the next loops' mode, the FX setup, the
selection. A power cycle brings it back, paused at the start. Not kept: what plays, an armed take or song, the keys'
mode (PLAY at first), the pattern recorder's steps. LOOPS lives in the edit memory as any edit: saved loops play
from the flash, and only what changed since takes edit memory ([../../files/ram.md](../../files/ram.md)).

## Texts

- **The header:** `LOOPS` and `BETA`; SEQ's place `SEQ MUTE`, `SEQ SOLO` in CONTROL mode.
- **The top line:** `120 BPM`, `4 BARS`, `1 BAR`, `-- BARS`, `Q 2 ROWS`, `Q 3 ROWS`, `Q 4 ROWS`, `Q ROW`, `Q REAL`,
  `REC`, `REC 3/4`, `REC 3`, `SONG`; a song recording: `SONG`, `0:42`, `5 PATTERNS` (`1 PATTERN`).
- **The rows:** the numbers, the sounds' names, `P`, `M`, `X2`-`X9`, `X+`, `M`, `S`, `R`; `+ NEW LOOP`; `ARMED: STARTS IN 3
  BARS` (`1 BAR`, `2 BEATS`, `1 BEAT`), `ARMED: A NOTE OR PLAY`; with no loops `REC, THEN PLAY OR A NOTE:`, `THE
  FIRST LOOP SETS THE LENGTH`, `PLAY OR A NOTE: THE FIRST LOOP`, `PRESETS, ENV: ITS SOUND`.
- **The footer's right:** `POLY`, `MONO`, `16 OF 16`, `MEMORY 81% FREE`, `REC: WRITE`.
- **The header, a knob turned:** `TEMPO 78 (120 BPM)`, `LEVEL 7F 100%`, `PAN 80 MIDDLE`, `DELAY SEND 20 25%`, `REVERB
  SEND 20 25%`.
- **Dialogs:** `LOOPS PROPERTIES` (TEMPO, SWING, LENGTH, GRID, QUANTIZE, MONO LEGATO, COUNT-IN; `THE FIRST LOOP SETS
  IT`, `2 ROWS`, `3 ROWS`, `4 ROWS`, `ROW`, `REAL TIME`, `GLIDE`, `LEGATO`, `JUMP`, `OFF`, `1 BAR`, `2 BARS`); `CUT TO 2
  BARS?` / `NOTES IN THE BARS CUT GO`, CUT, KEEP; the menus `LOOP 03 SUB BASS`, `LOOPS` (APPLY INSTRUMENT, DOUBLE,
  RECORD LIVE, CANCEL RECORD LIVE, SAVE TO PATTERN, LOAD FROM PATTERN, CLEAR LOOPS, REVERT); `CLEAR ALL 6 LOOPS?`
  (`1 LOOP`) / `THE NEXT ONE SETS THE LENGTH`, CLEAR, KEEP; `REVERT LOOPS TO SAVED?`, REVERT, KEEP; the count-in's
  box ([../transport.md](../transport.md#the-count-ins-box)); `SAVE THE LOOPS TO` with `NEW`, `LOAD INTO LOOPS`;
  `PATTERN 2A IS NOT EMPTY` / `REPLACE IT WITH THE LOOPS?`, YES, NO; `REPLACE THE LOOPS` / `WITH PATTERN 2A?`, YES,
  NO; `NEW LOOP: A CARTRIDGE`; the RECORD LIVE panel's (`RECORD LIVE`, `NAME`, `LOOPS 01`, `K1 TEMPO`, `124 BPM`,
  `EDIT: NAME`, `HOME` `CANCEL`, `REC` `ARM`); the pattern recorder's (`RECORD PATTERN: NEW LOOP`, `RECORD PATTERN: INTO LOOP 03`,
  `PREVIEW`, `K1 STEP SIZE`, `K2 LENGTH`, `K3 NOTE`, `K4 TIMES`, `1/32` ... `1/4`, `A ROW`, `16 STEPS`, `X4`, `FILLS
  4 BARS`, what it makes, `SELECT: ROW  SEL: NOTE  KEYS: STEPS`).
- **Toasts:**

  | Toast | When |
  |---|---|
  | `LOOP 03 RE-RECORDING` | REC or EDIT on a loop (its notes gone) |
  | `LOOP 03 PEELED` | LFO |
  | `LOOP 00: 4 BARS`, `LOOP 00: 1 BAR` | the first loop closed |
  | `NOTHING PLAYED: NO LOOP` | the first loop closed with no notes |
  | `REC, THEN PLAY: THE FIRST LOOP` | PLAY with no loops, nothing armed and nothing playing (refused) |
  | `CANCELLED` | REC while armed; PLAY or HOME during the count-in; SEL → CANCEL RECORD LIVE; a song ended before its first row |
  | `KEYS: CONTROL`, `KEYS: PLAY` | SEQ |
  | `LOOP 03 MUTED`, `LOOP 03 ON`, `LOOP 03 SOLO`, `LOOP 03 SOLO OFF` | a white key in CONTROL mode, HOME + white key |
  | `LOOP 03 MUTED AT THE BAR`, `LOOP 03 ON AT THE BAR`, `LOOP 03 SOLO AT THE BAR`, `LOOP 03 SOLO OFF AT THE BAR` | the same, held: waiting for the bar line |
  | `ALL LOOPS ON` | PIT in CONTROL mode |
  | `QUANTIZE: 2 ROWS` (`3 ROWS`, `4 ROWS`, `ROW`, `REAL TIME`) | OP1-OP5 in CONTROL mode |
  | `NEXT LOOPS: MONO`, `NEXT LOOPS: POLY` | MONO, POLY in CONTROL mode |
  | `SEQ: MUTE`, `SEQ: SOLO` | GLO's black key in CONTROL mode |
  | `NO LOOPS YET` | EDIT, LFO, DOUBLE, RECORD LIVE, SAVE TO PATTERN or CLEAR LOOPS with no loops |
  | `RECORDING A SONG` | a change of the loops while a song is armed or recorded (refused) |
  | `KEYS: THE NOTE`, `KEYS: THE STEPS`, `NOTE C-5` | the pattern recorder: SEL; OCT |
  | `NOTES RING TO THE NEXT`, `NOTES A STEP LONG` | the pattern recorder: GLO |
  | `NO STEPS ON` | the pattern recorder: REC with no step on (refused) |
  | `PATTERN: LOOP 06, 4 BARS`, `PATTERN INTO LOOP 03` | the pattern recorder: REC: a new loop (with no loops: the first, its length); into a loop |
  | `GRID: ONLY BEFORE THE FIRST LOOP` | GLO, GRID with loops |
  | `DOUBLE: AT MOST 8 BARS` | DOUBLE past 128 rows (SEL blinks) |
  | `DOUBLED: 8 BARS` | DOUBLE |
  | `LOOP 03: 01:04 BRASS` | APPLY INSTRUMENT |
  | `SONG 12 RECORDED: 1:23, 5 PATTERNS` (`1 PATTERN`) | the song's recording ended (REC) |
  | `SONG 12: 3 NOTES LEFT OUT` (`1 NOTE`) | ... with notes that did not fit its 16 tracks |
  | `SONG 12 STOPPED: MEMORY FOR EDITS IS FULL`, `SONG 12 STOPPED: ALL 240 PATTERNS USED`, `SONG 12 STOPPED: 256 POSITIONS` | the song's recording stopped by itself |
  | `NO SONG NUMBER FREE` | RECORD LIVE with every song number taken |
  | `LOOPS STORED IN PATTERN 2A`, `PATTERN 2A: 3 NOTES LEFT OUT` | SAVE TO PATTERN |
  | `PATTERN 2A IN LOOPS: 3 LOOPS`; `PATTERN 2A IN LOOPS: 3 INSTRUMENTS LEFT OUT`, `PATTERN 2A IN LOOPS: 12 COMMANDS LEFT OUT` | LOAD FROM PATTERN |
  | `CARTRIDGE 0A IS EMPTY` | ENV picked an empty cartridge |
  | `LOOPS CLEARED` | SEL → CLEAR LOOPS, CLEAR |
  | `REVERTED` | SEL → REVERT, after its question |
  | `MEMORY FOR EDITS IS FULL` | a take, the pattern recorder, DOUBLE or LOAD past the edit memory: a take ends there, what was written kept; the others refused, nothing written |

## Help

```
[LOOPS]: A LOOP STATION
WHAT PLAYS: GOES ON UNTIL LOOPS START
LEAVING: THE LOOPS PLAY ON
A LOOP: ONE INSTRUMENT, ONE TAKE,
  ALL OF ONE LENGTH, NEWEST AT THE END
THE FIRST LOOP SETS THE LENGTH:
  {REC} ON + NEW LOOP, THEN A NOTE OR
  {PLAY}; {REC} AGAIN: CLOSE ON A BAR
{SELECT}: A LOOP, OR + NEW LOOP
{REC}: RECORD THERE, ONE PASS
  PLAYING: IT STARTS WITH THE LOOPS
  ON A LOOP: RECORDS IT AGAIN
{EDIT}: RECORD THE LOOP AGAIN
{LFO}: PEEL, THE LOOP GOES
{PLAY}: PLAY / PAUSE, HOLD: STOP
```

```
{PRESETS}: THE SOUND YOU PLAY,
  {ENV}: ITS CARTRIDGE; A NEW LOOP
  TAKES IT
{ALGORITHM}: TEMPO, ANY TIME
{K1}-{K4}: LEVEL, PAN, DELAY, REVERB
  OF THE LOOP; ON + NEW LOOP: THE NEXT
WHAT YOU HEAR RECORDING IS KEPT:
  NOTES ON QUANTIZE'S GRID, MONO ONE
  AT A TIME, POLY 4 AT MOST
{SEQ} (SHOW ELSE, QUICK): THE {KEYS}
  PLAY NOTES, OR CONTROL THE LOOPS
{HOME}, {WHITE KEY}: MUTE A LOOP
  HELD: SOLO
```

```
CONTROL: THE {KEYS}
{WHITE KEY}: LOOP 1-16, MUTE / SOLO
  TAP: NOW, HOLD: AT THE BAR LINE
{GLO} KEY: MUTE OR SOLO
{PIT}: ALL LOOPS ON
{OP1} {OP2} {OP3}: QUANTIZE 2, 3, 4 ROWS
{OP4}: A ROW, {OP5}: REAL TIME
{MONO}, {POLY}: HOW NEXT LOOPS RECORD
  MONO: ONE NOTE, AS MONO LEGATO SAYS
  POLY: UP TO 4 NOTES
```

```
{ARP} (AGAIN, REPEAT, PLEASE): RECORD
  PATTERN, 4 ROWS OF STEPS
  {SELECT}: THE ROW
  {WHITE KEY}: A STEP ON / OFF
  {K3} OR {SEL}, THEN A KEY: ITS NOTE
  {K1} STEP SIZE, {K2} LENGTH,
  {K4} TIMES: THE FIRST LOOP'S LENGTH
  {GLO}: NOTES RING ON, OR A STEP LONG
  {ARP}: THE PREVIEW ON / OFF
  {REC}: WRITE IT, {HOME}: CLOSE
{GLO}: TEMPO, SWING, LENGTH, GRID,
  QUANTIZE, MONO LEGATO, COUNT-IN
{SEL}: APPLY INSTRUMENT, DOUBLE,
  RECORD LIVE, SAVE TO / LOAD FROM
  PATTERN, CLEAR LOOPS, REVERT
{FX}: THE LOOPS' EFFECTS
```

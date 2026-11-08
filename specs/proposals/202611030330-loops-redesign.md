# Proposal: LOOPS with a stage

Status: **settled** (2026-11-03); the specs lead: [../ui/screens/loops.md](../ui/screens/loops.md),
[../files/loops.md](../files/loops.md), [../files/ram.md](../files/ram.md#the-stage-in-the-edit-memory) and the others
[section 19](#19-what-else-changes) names. It changes LOOPS as [the loop station proposal](202610240236-loops-station.md)
made it. The playground has it (`playground/scr-loops.js`, `player.js`, `model.js`, `help.js`;
`tests/playground/loops.js` pins its behaviour and texts); where the specs and the playground differ, the playground
changes ([section 18](#18-the-playground)). The firmware follows ([section 17](#17-the-firmware)). Its open points are
in [../open-questions.md](../open-questions.md#the-ui-ui).

## 1. Why

LOOPS records a layer as a take: armed, one pass of the length, kept as played. A part has to come out right in one
pass, a retake throws the loop's notes away before the new pass is played, and the first loop sets the length by
growing a bar at a time. Sixteen loops share the screen in rows too thin to read.

A looper is played the other way: the music goes round, the keys play over it again and again, and what is good is
kept. So LOOPS gets **a stage:** one loop of the length that the keys play into while the loops go round, cycle after
cycle, each cycle fixing a part (a note played replaces what the stage had where it is held), until REC keeps it as a
loop. **The length is set from the start** (4 bars, changed in GLO before anything is played), so PLAY plays from the
first moment. **Four loops in view,** big enough to read, the sixteen in a column beside them.

- **Goals:** play over the loops without a pass to get right; keep a part the moment it sounds right; fix one bar of a
  loop without playing the rest again; hear exactly what is kept; every action at once, no arming in the way; four
  loops readable, all sixteen seen.
- **Not goals:** editing notes in LOOPS (SAVE TO PATTERN and the EDITOR do that); loops of different lengths; an undo
  of REC (REVERT brings back what was saved).
- **A failure even if it works:** a key that writes where it was not heard; a cut that keeps something other than what
  played in the bars kept; the stage swallowing a part without a way to keep it.

## 2. What goes and what stays

**Goes:** takes (armed for the loops' next start, one pass, ending by themselves); the first loop growing a bar at a
time and closing on a bar line, with `-- BARS` before it and `NOTHING PLAYED: NO LOOP`; a loop with no length set; the
re-record (EDIT) and PEEL (LFO); `ARMED: STARTS IN ...` and `REC 3/4`; ALGORITHM turning TEMPO on the screen (the
pattern recorder and the RECORD LIVE panel keep it); all 16 loops as rows;
the strips scrolling under a play line held in the middle; `NEXT LOOPS: MONO` (MONO / POLY is the keys' now); the
pattern recorder's STEP SIZE, its K4 TIMES and GLO's `RING TO THE NEXT` / `A STEP LONG`; the recorder writing a loop;
LOOPS' `BETA` mark.

**Stays:** one length for all loops, in whole bars, 128 rows at most; up to 16 loops, each one instrument and
colour, MONO or POLY, its notes once plus a repeat count, its mix, muted, soloed (a solo never saved); LOOPS' own FX
setup and METRONOME, FX tap working on it; QUANTIZE's values; MONO LEGATO; COUNT-IN; CONTROL mode and HOME's mutes and
solos; RECORD LIVE; SAVE TO / LOAD FROM PATTERN; DOUBLE; CLEAR LOOPS; REVERT; APPLY INSTRUMENT; the colours; voices past
VOICES parked; LOOPS a screen of the round between FX and DJ, and on HOME + POLY, its playback of its own (what plays
plays on into LOOPS until the loops start there; the loops play on outside LOOPS until a song or a pattern starts);
the record's form ([section 16](#16-the-record-stays-version-4)).

## 3. The length

- **Always set:** 4 bars at first, or the most whole bars that fit 128 rows where 4 do not (4 bars fit only while
  GRID's rows a beat × BEATS A BAR is 32 or less: at 1/16 up to 8 beats a bar, at 1/16T 5, at 1/32 4). GLO's LENGTH
  and TEMPO change it any time, with loops or none. **PLAY plays the loops with none:** the length goes round (the
  METRONOME heard when LOOPS' FX has it on, the top line's beats lit), and the stage fills.
- **LENGTH,** a bar a click: **longer** at once, silence added at the end; **shorter** only planned while GLO is open
  (its value yellow, `2 BARS (NOW 4 BARS)`), nothing cut, and cut as GLO closes (GLO or HOME), after one question when
  a note that plays would go (`CUT TO 2 BARS?` / `NOTES IN THE BARS CUT GO`, OCT+ `CUT`, OCT- `KEEP`: the length as it
  was). A shorter length with nothing to lose cuts without asking.
- **A cut** cuts each loop's base and repeats at the new end: a base past it cut there and played once (a note
  sounding over the cut stops at the wrap: a note off at row 0), else as many repeats as reach it; a loop left with no
  notes stays, empty. **A shifted loop** ([section 8](#8-k1-k4-view-and-adjust)) is first written out as it is heard
  over the old length (its repeats one after another, its SHIFT turned into its notes, SHIFT 0), so the bars kept play
  as they did; the same before a longer length. The question counts the notes as heard.
- **LENGTH steps whole bars:** from the bars the length holds, rounded down, when BEATS A BAR has left it a part bar.
- **GRID** only with no loops and an empty stage (`GRID: ONLY WITH NO LOOPS`; with notes on the stage
  `GRID: ONLY WITH THE STAGE EMPTY`); the length keeps its bars (as many as the new grid allows).
- **The length stays** through CLEAR LOOPS, DELETE LOOP of the only loop, and REC. LOAD FROM PATTERN sets it as before
  (the pattern's rows in whole bars).
- **Any change of the length** (LENGTH, DOUBLE, GRID, LOAD FROM PATTERN, REVERT) takes the stage with it (cut, a note
  sounding over the cut ending at the line, or longer with empty rows; DOUBLE plays it twice), and the keys down stop
  writing. The play position is taken round the new length (its row modulo the length's rows).

## 4. The stage

- **One loop of the length, belonging to no loop.** It cycles with the loops and is heard in the keys' instrument and
  MONO / POLY (the current ones, [section 7](#7-the-instrument-and-mono-or-poly)), through the mix REC would put it in:
  the selected loop's, on `+ NEW LOOP` the next loop's. **Mutes and solos never silence it:** what is played is
  heard. It sounds through LOOPS' effects, not KEYS GAIN or the keys' DRY.
- **While the loops play on LOOPS, the keys overdub it.** Each note lands as QUANTIZE says (a note played before its
  line waits for it; one just after the line sounds at once, as close to the line as it can, and is written on the
  line; REAL TIME: the row it falls in with a note delay `EDx`, its end a key off `Kxx` at its tick), and is heard
  exactly as it is written:
  - **POLY:** while its key is held, the note replaces what the stage had over the span the key is held, on every
    lane: notes starting inside it go, a note running into it is cut where it starts. Notes played together share
    the lanes (a key a lane, the first no other key down holds), each clearing its own span, keys landing on one row
    making a chord; a fifth key is neither heard nor written.
  - **MONO:** one lane; a note placed on it cuts what was there over its span; keys landing on one row compete for
    it, the one registered last written and heard. A note played over a held one takes over as MONO LEGATO says, as
    today: `GLIDE` a glide (`3xx`), `LEGATO` a legato (`LEG`; a FUSION instrument's played as a plain note), on a
    TB-303 both its slide (`SLD` on the held note); `JUMP` a plain note. A MONO overdub on a stage holding chords
    first makes it one lane for good (the later of overlapping notes).
  - **A note off** goes on the nearest row, at least a row after its note (REAL TIME: the key off at its tick).
  - **The span grows with the play line:** a held note replaces, and is drawn, only up to the line; ahead of it the
    stage keeps its notes until the line gets there. **The rows not played keep their notes,** so each cycle fixes
    parts.
  - **A note is heard once as it is played,** then from the stage each time the line reaches its row.
- **The line** (the length's end, played through): there is no wrap of a note's end. A note held over the line is
  released there (a note off at the length's end), its release ringing on into the start until it ends or its lane's
  next note cuts it; a key still held as the next cycle starts is written into it as pressed again at its first row.
- **OVERWRITE** instead of OVERDUB while **LFO** is held: from LFO's press the play line erases everything on the
  stage it passes, on every lane (notes starting there removed, a note running into it cut where the line came in,
  the stage's sounding notes there stopped), and what is played meanwhile is written: that stretch becomes what is
  played, silence included. LFO let go: OVERDUB again. **LFO held + REC tapped keeps OVERWRITE** (`OVERWRITE KEPT: LFO
  ENDS IT`, LFO blinking) until LFO is pressed again (`OVERDUB`; that press is OVERWRITE while LFO is held, as any).
  **LFO held + REC held** past the hold delay empties the stage (`STAGE CLEARED`, the selected loop heard again; empty:
  REC blinks). LFO has no tap job on LOOPS; stopped, OVERWRITE erases nothing (the line does not move).
- **The selected loop is silent while the stage holds notes** (the stage is what REC would put in its place): drawn
  grey, its state `STAGED` in red. With an empty stage it sounds; selecting a loop alone silences nothing. A loop
  falling silent lets its notes go at once.
- **Moving the selection keeps the stage:** the loop moved to falls silent, the one left is heard again; REC sends the
  stage wherever the selection is then, so EDIT, a move and REC copy a loop.
- **A new instrument or mode** (PRESETS, ENV, the pipette, SEQ held + MONO / POLY, CONTROL mode's) keeps the stage's
  notes, played in the new sound; a POLY stage in MONO plays one note at a time (the later of overlapping notes takes
  over, an end counting only on the lane whose note sounds), and POLY again brings its chords back. The keys down stop
  writing.
- **What empties it:** REC (sent), LFO + REC held, CLEAR LOOPS, a power cycle; nothing else (OVERWRITE or a shorter
  length may leave it with no note). EDIT and the pattern recorder's REC replace what it holds.
- **Stopped,** the keys play freely (every voice, no grid, through KEYS GAIN), unless REC has armed the loops
  ([section 5](#5-rec-edit-and-the-arm)); the stage keeps its notes.
- **Never saved, as an edit mode's copy is not** ([../ui/controls.md](../ui/controls.md): EDIT keeps, HOME drops):
  it is no unsaved change (SAVE's LED, the unsaved count and LOOPS' REVERT item take no notice of it). **SAVE and
  REVERT ALL leave it as it is,** and so do LOOPS' REVERT and LOAD FROM PATTERN (fitted to the length they bring), so
  a SAVE mid-jam loses no take. A power cycle drops it.
- **Outside LOOPS** it plays on with the loops, the selected loop staying silent; the keys elsewhere never write it.
  Going to FX from LOOPS (which does not leave LOOPS) the keys down stop writing, and on FX the keys are FX's.
- **While RECORD LIVE is armed or records, the stage rests:** not heard, silencing no loop, LFO doing nothing; it is
  kept as it was and heard again once the song ends ([section 10](#10-record-live-save-to-and-load-from-pattern)).
- **Its notes are kept in the edit memory** as a piece of a kind of its own, beside the edit modes' copies, encoded
  as a loop's stream, at most 3,800 B ([the stage's piece](#the-stages-piece)); no fixed RAM. EDIT keeps no undo copy:
  REVERT is the way back to what was saved. **A note the stage cannot take is neither heard nor written,** and the
  header says why:
  - the edit memory full: `MEMORY FOR EDITS IS FULL`;
  - its stream at its 3,800 B, a loop's most: `THE STAGE IS FULL`;
  - the store busy (a save being written, or seeding from the computer): `STAGE NOT WRITTEN: STORAGE BUSY`. The
    keys overdub only while the loops play, a save is written while nothing plays and every control waits
    ([../ui/dialogs.md](../ui/dialogs.md#progress)), and seeding silences the FM-1, so no overdub meets a busy store;
    the rule guards what else changes the stage (EDIT, REC, a length change) should one meet it.

## 5. REC, EDIT and the arm

- **REC acts at its press.** With notes on the stage it **sends the stage where the selection is:**
  - **on `+ NEW LOOP`** a new loop: the keys' instrument and mode, the next colour, the next loop's mix, its base the
    length, once, no SHIFT; the selection on `+ NEW LOOP` again (`STAGE AS LOOP 06`);
  - **on a loop** its notes replaced by the stage's, the keys' instrument and mode written into it, its SHIFT 0, its
    place, colour, mix and mute kept (`STAGE INTO LOOP 03`);
  - the stage emptied; while the loops play, what of it sounds goes on as the loop's, without a break. What is
    written is what was heard: MONO one lane (a POLY stage merged as it played), no wrap (a note sounding at the
    length's end ends at the line), and a note off or key off that ends nothing (its lane silent there, counted
    round from the length's end) left out.
  - **An arm ends with it.**
- **The stage empty:** while the loops play REC is refused (`THE STAGE IS EMPTY`, REC blinks); stopped, **REC arms:**
  REC blinks, the header's ● is red, the top line's `REC` reads `ARMED` in red, and the hint line says
  `ARMED: A NOTE OR PLAY STARTS AT 2.4.3` (the play position). Then the first key starts the loops from the play
  position at once, its note the stage's first (it lands as any overdub's); PLAY starts them after COUNT-IN (OFF:
  at once). REC again disarms (`CANCELLED`). **During the count** REC, PLAY and HOME cancel it and disarm
  (`CANCELLED`), REC sending nothing even with notes on the stage (EDIT can fill it while armed); the keys play freely
  then, nothing written.
- **The order REC's press is taken in:** a count-in running, it cancels; RECORD LIVE armed, REC starts it; RECORD LIVE
  recording, REC ends it at once ([section 10](#10-record-live-save-to-and-load-from-pattern)), LFO held or not; then,
  with LFO held, REC is OVERWRITE's ([section 4](#4-the-stage)); else the stage's, as above. In ADJUST REC does nothing
  (it blinks), LFO held or not.
- **EDIT copies the selected loop onto the stage,** as it is heard (its repeats written out, a note off where it goes
  to rest or a repeat is cut, its SHIFT applied), in place of what the stage held; its instrument and MONO / POLY
  become the keys' (as ENV held does); the loop falls silent at once (`LOOP 03 ON THE STAGE`). The loop itself is
  unchanged until REC writes the stage back. On `+ NEW LOOP` EDIT does nothing (it blinks); refused while a song is
  armed or recorded (`RECORDING A SONG`) and during a count-in.

## 6. The play position, PLAY and jumps

- **The play position** is where PLAY starts and where an armed key starts the loops. **ALGORITHM** moves it a row a
  click (SEL held: a bar), round the length: stopped, the header shows `PLAY FROM 2.4.3` (bar, beat, and the step when
  not on a beat); while the loops play they **jump** there at once (`JUMP +1 ROW`, `JUMP -16 ROWS`), from the next row.
- **A jump** is no line: the notes sounding ring on as at a tracker's jump (each until its lane's next cell), a loop
  landing in its rest falls silent there, a key held stops writing (its note ends as when it is let go, its span what
  the line passed), OVERWRITE goes on from where the loops land, and the stage plays what it holds from there (a jump
  back plays what was just written). A jump back keeps the loops' pass; one forward past the length's end counts the
  next pass.
- **PLAY** (at its press): stopped, the loops play from the play position, with loops or none; armed, after COUNT-IN;
  **playing, they stop and the play position goes back to the start** (the stage kept, the keys down no longer
  writing), PLAY's red then dim: at the start, as everywhere. **PLAY held:** stop, back to the start (`BACK TO THE
  START`). What played on into LOOPS pauses as the loops start or the count begins.
- **While a song is armed or recorded, PLAY is RECORD LIVE's,** as today
  ([section 10](#10-record-live-save-to-and-load-from-pattern)): armed, when the loops did not play as it was armed
  and do not play now, PLAY starts the loops and the song after COUNT-IN (else PLAY is the loops' own, above);
  recording, PLAY acts as it is let go, a tap silencing the loops or bringing them
  back on the song's next row, held taking them to their start (playing from the top on the song's next row, or
  waiting there silent), the song going on.
- **Outside LOOPS** PLAY pauses and resumes the loops when they played last, as anything that played last, with loops
  or none; paused there they keep their place, and PLAY on LOOPS goes on from it (the play position).
- **Leaving LOOPS** for any screen but FX disarms an arm, ends a count-in, cancels a song armed and ends a song
  recording (what was heard written), cancels ADJUST, closes the pattern recorder, and the keys down stop writing; the
  loops and the stage play on.
- **SYNC EXTERNAL** ([../connections.md](../connections.md#clock)): the master's START plays the loops from bar 1 (the
  play position 0), STOP pauses them where they are (CONTINUE goes on from there), the song position moves the play
  position (at once while they play: a jump); armed, START or CONTINUE starts them as PLAY does, without the count-in;
  with no loops they play all the same. RECORD LIVE as now.

## 7. The instrument and MONO or POLY

- **The keys' setup is the current instrument and its MONO / POLY:** the stage plays in it, and REC writes both into
  the loop it sends to. The footer shows the instrument at the left, `MONO` or `POLY` at the right.
- **PRESETS** the instrument; **ENV** the cartridge list (`NEW LOOP: A CARTRIDGE`: its first instrument); **ENV held,
  the pipette:** the selected loop's instrument and MONO / POLY become the keys' (`KEYS: 01:04 BRASS, MONO`; on `+ NEW
  LOOP` nothing); SEL's **TAKE ITS INSTRUMENT** is its tap route.
- **MONO / POLY:** CONTROL mode's MONO and POLY keys, or **SEQ held + MONO or POLY** (`KEYS: MONO`, `KEYS: POLY`).
- **APPLY INSTRUMENT** (SEL) gives the selected loop the keys' instrument and MONO / POLY (`LOOP 03: 01:04 BRASS,
  MONO`).

## 8. K1-K4, VIEW and ADJUST

- **K1-K4 each have a job,** set in GLO (`K1 TURNS` ... `K4 TURNS`), from: LEVEL, PAN, DUCK, CHORUS SEND, DELAY SEND,
  REVERB SEND (the selected loop's; on `+ NEW LOOP` the next loop's), TEMPO (the loops', as GLO's), VIEW. At first K1
  LEVEL, K2 PAN, K3 REVERB SEND, K4 TEMPO. SEL held: a mix value by 16, TEMPO by 10. The header shows the turn as
  everywhere (`LEVEL 7F 100%`, `TEMPO 78 (120 BPM)`, `VIEW 2 BARS`). Kept with LOOPS' setup.
- **VIEW:** 1, 2 or 4 bars a screen, 2 at first (GLO's `VIEW`: `2 BARS A SCREEN`; a knob's job too).
- **ADJUST** (SEL, on a loop) moves the whole loop round the length without moving a note: **its SHIFT,** the rows it
  plays later (`-3 ROWS` shown the nearer way round). **K1** a row, **K2** a bar; ALGORITHM still the play position;
  SELECT, K3 and K4 nothing meanwhile. The loops play on, the shift heard and drawn at once. **SEL keeps it** (`LOOP
  03: SHIFT +17 ROWS`), **HOME cancels** (`CANCELLED`: the SHIFT as it was). Open: the header's subpage `ADJUST`, the
  top line `LOOP 03: SHIFT +17 ROWS`, the hint line `K1 ROW  K2 BAR  SEL KEEP  HOME CANCEL`; REC, LFO, EDIT, GLO, ENV,
  SEQ and ARP do nothing. Leaving LOOPS cancels it. **On the loop the stage stands for** (silent while the stage holds
  notes) the shift is drawn at once and heard once the loop sounds again; REC writing the stage into it sets SHIFT 0.
- **SHIFT is honoured wherever a loop is read:** the player (a loop plays its row (row − SHIFT) round the length, its
  repeats and rest turned with it), the strips, EDIT, RECORD LIVE's song, SAVE TO PATTERN, SEL's QUANTIZE (its lines
  as heard), LENGTH (written out as heard first). Each reads the loop as it is heard: written out over the length,
  then turned SHIFT rows. **SHIFT goes to 0** when REC writes the loop, when LENGTH writes it out, and on LOAD FROM
  PATTERN; DOUBLE keeps it (the loop plays the same).

## 9. SEL's menu and GLO's properties

**The menu,** titled `LOOP 03 SUB BASS` on a loop, `LOOPS` on `+ NEW LOOP`:

- **TAKE ITS INSTRUMENT** (on a loop): the pipette's tap route ([section 7](#7-the-instrument-and-mono-or-poly)).
- **APPLY INSTRUMENT** (on a loop): [section 7](#7-the-instrument-and-mono-or-poly); refused while a song is armed or
  recorded.
- **ADJUST** (on a loop): [section 8](#8-k1-k4-view-and-adjust).
- **QUANTIZE** (on a loop): a list, titled `QUANTIZE LOOP 03`: `2 ROWS`, `3 ROWS`, `4 ROWS`, `ROW`. The loop's notes go
  to the nearest line of that size, the lines counted as the loop is heard (from the loops' start, its SHIFT counted,
  its first repeat), back in its base; REAL TIME's note delays and key offs go; each note off moves with its note (its
  length kept, a row at least); two notes landing on one lane's row: the later wins; a note rounded to the base's end
  goes to its start (`LOOP 03 QUANTIZED: 4 ROWS`). REVERT is the way back.
- **DELETE LOOP** (on a loop): the loop goes at once, no question (`LOOP 03 DELETED`); the loops after it move up with
  their mixes and waiting mutes, the selection on the one moved into its place; the length stays; REVERT is the way
  back. The stage stays: with notes on it, the loop now selected falls silent, as any selected loop does.
- **DOUBLE,** listed only when it can be done (loops exist, twice the length fits 128 rows): the length ×2, every loop
  playing twice what it played, nothing copied (a loop filling the length doubles its repeat count; one that rests or
  is cut at the end is written out to the old length first), the stage played twice (`DOUBLED: 8 BARS`).
- **RECORD LIVE** / **CANCEL RECORD LIVE**, **SAVE TO PATTERN**, **LOAD FROM PATTERN**
  ([section 10](#10-record-live-save-to-and-load-from-pattern)).
- **CLEAR LOOPS:** every loop and the stage's notes, after a question (`CLEAR ALL 6 LOOPS?` (`1 LOOP`) / `THE STAGE
  TOO; THE LENGTH STAYS`, OCT+ `CLEAR`, OCT- `KEEP`; then `LOOPS CLEARED`); the length, the properties, the FX setup
  and the next loop's mix stay; the loops play on, empty.
- **REVERT** (while LOOPS has something unsaved): LOOPS as saved, after its question (`REVERT LOOPS TO SAVED?`), the
  stage kept ([section 4](#4-the-stage)).
- With no loops, RECORD LIVE, SAVE TO PATTERN and CLEAR LOOPS say `NO LOOPS YET`. While a song is armed or recorded,
  everything that changes the loops is refused (`RECORDING A SONG`).

**GLO's properties,** `LOOPS PROPERTIES`, twelve lines: **TEMPO** (`78 (120 BPM)`; SEL held: by 10), **SWING**,
**LENGTH** ([section 3](#3-the-length)), **GRID** (dark grey while it cannot change), **QUANTIZE**, **MONO LEGATO**,
**COUNT-IN** (the bars counted when PLAY starts armed loops; an armed key always starts them at once), **VIEW**,
**K1 TURNS**, **K2 TURNS**, **K3 TURNS**, **K4 TURNS**. Twelve lines take 192 px of the body, the dialog's own layout.

## 10. RECORD LIVE, SAVE TO and LOAD FROM PATTERN

As today ([../ui/screens/loops.md](../ui/screens/loops.md#record-live)), with:
- **SHIFT applied:** RECORD LIVE writes each loop's notes as they played, SHIFT included; SAVE TO PATTERN writes each
  loop out as heard (its repeats, its rests' note offs, the cut at the wrap, then turned SHIFT rows), so the pattern
  plays what LOOPS played.
- **The stage rests while a song is armed or recorded** ([section 4](#4-the-stage)): the song writes the loops as they
  are heard, the keys on its own tracks; the stage is neither heard nor written, and the selected loop plays.
- **LOAD FROM PATTERN** gives every loop SHIFT 0; the stage stays, fitted to the new length; the loops stop, the play
  position and the selection at their start.
- **The RECORD LIVE panel's ALGORITHM** turns TEMPO with its K1, as GLO's TEMPO does (the panel has no play position;
  on the screen ALGORITHM is the play position now).
- **PLAY while a song is armed or recorded** as today ([section 6](#6-the-play-position-play-and-jumps)); the footer's
  right reads `MEMORY 81% FREE` in red while it records, as today.
- The things a song refuses now: EDIT, the pattern recorder (and so its REC onto the stage), APPLY INSTRUMENT, ADJUST,
  QUANTIZE, DELETE LOOP, DOUBLE, LENGTH, GRID, CLEAR LOOPS, LOAD FROM PATTERN and REVERT (`RECORDING A SONG`, the key
  blinking). REC is the song's ([section 5](#5-rec-edit-and-the-arm)).

## 11. CONTROL mode, SEQ held and HOME's mutes

- **SEQ tapped** switches the keys between PLAY and CONTROL (`KEYS: CONTROL`, `KEYS: PLAY`), at its release; the
  header's SEQ place reads `SEQ MUTE` or `SEQ SOLO` in CONTROL mode.
- **SEQ held + a key** acts as CONTROL mode's key once, the keys staying in PLAY (SEQ's own tap then does not happen):
  white keys mute or solo, OP1-OP5 QUANTIZE, PIT all on, the GLO key MUTE / SOLO, MONO and POLY the keys' mode. While
  SEQ is held (or in CONTROL mode) the slots show their keys `1`-`16` white and the hint line says `WHITE KEYS: MUTE
  HELD: AT THE BAR` (`SOLO`).
- **CONTROL mode's keys,** as today: white key *n* loop *n*, a tap at once, held past the hold delay at the next bar
  line (a yellow `M` / `S` meanwhile); one solo at a time; OP1-OP3 `2 ROWS`, `3 ROWS`, `4 ROWS`, OP4 `ROW`, OP5 `REAL
  TIME` (`QUANTIZE: 2 ROWS`); PIT all back on (`ALL LOOPS ON`); the GLO key MUTE ↔ SOLO (`SEQ: MUTE`, `SEQ: SOLO`);
  MONO and POLY the keys' mode (`KEYS: MONO`, `KEYS: POLY`).
- **HOME's combination** on every screen: white key *n* mutes loop *n*, held (or after SEQ) solos it.

## 12. The pattern recorder (ARP)

ARP opens it over the loops, as today, the loops playing on; refused while armed, during a count-in, and while a song
is armed or recorded.
- **4 rows of up to 16 steps, a note a row; each step a grid row** (16 steps at 1/16 are a bar); the pattern repeats
  over the length, the last repeat cut (`4 X 16 = 64 ROWS, 2 LANES`; `5 X 12 + 4 = 64 ROWS, 1 LANE`).
- **K1 NOTE LENGTH,** how long each step's note is held: the lengths GRID makes whole rows of (`1/32`, `1/16T`, `1/16`,
  `1/8T`, `1/8`, `1/4`, `1/2`, `1 BAR`) and `TO THE NEXT` (it rings until its row's next note); `1/16` at first (a
  row's length where GRID lacks it). **The row's next note always ends the one before;** notes on other rows overlap
  (a pad's chord rings). **K2 LENGTH** 4, 8, 12 or 16 steps; **K3 NOTE** the row's note; the line `REPEATS` `OVER 4
  BARS`, dark grey (no knob: it says what the pattern fills); **SELECT** the row.
- **A white key tapped** switches its step on or off, at its release. **Held past the hold delay,** the step becomes a
  tie, held from the step before, when that step is on or a tie; after a step off, or as the first step, it becomes a
  plain note (a held run starts with one). No tie is ever left alone: switching off a run's first note, or a tie in
  its middle, makes the next tie a plain note. A tie adds its steps to the note's length. The keys' LEDs: a step on
  full, a tie blinking, off dark (steps 1, 5, 9, 13 dim), past LENGTH dark, the step playing turned over.
- **SEL, then a key** (or SEL held + a key) sets the row's note; OCT moves the keyboard and the row's note; ARP the
  preview's sound (the loops playing) or the preview alone; PLAY plays and pauses the loops, as today (a pause: the
  preview goes on alone from where it was). **ALGORITHM turns TEMPO** here, as GLO's TEMPO does (SEL held: by 10): the
  recorder has no play position.
- **REC puts the pattern onto the stage** (`PATTERN ON THE STAGE`), replacing what it held, repeated over the length,
  and closes; the preview's sounding notes go on as the stage's, without a break: with the loops playing, in their
  time; with the preview playing alone, the loops start from where it was; with nothing playing, nothing starts. The
  stage plays in the sound the preview had: opened on a loop, that loop's instrument becomes the keys'; POLY when
  the pattern fills more than one row. REC on the screen then sends the stage, as any. No step on: refused (`NO STEPS
  ON`). **HOME** closes it, nothing written. The footer's right reads `REC: TO STAGE`, the title `RECORD PATTERN: TO
  STAGE`, the keys' line `KEY: A STEP  HELD: A TIE  SEL: NOTE`.
- What is set stays until power-off, never saved.

## 13. The screen

The body (y 17-226, [../ui/frame.md](../ui/frame.md)): the top line, four slots, the overview column at the right, the
stage, the length's bar, the hint line. **The header** shows `LOOPS` with no mark, and the subpage `ADJUST` while
ADJUST is open. Nothing moves or changes size when a hint, the arm or OVERWRITE comes or goes.

```
LOOPS                         > |##9     S: the name; the transport, CPU, voices, battery
120 BPM 4 BARS  Q ROW   REC     ## .. .. T: tempo, length, QUANTIZE, REC; the beats
----------------------------------------
   SYN PIANO P X2 SHIFT +3            #  slot 0: the number (S) in the gutter; sound, P / M,
00 ##.##..#|#.##..##.|..              O    repeats, SHIFT, state; its strip; the overview at x 229
+--------------------------------------+
|  SYN PIANO M                 STAGED  |#  the selected slot boxed; silent while the stage
|01 ........|.........                |+    holds notes: grey, STAGED red
+--------------------------------------+
   NEW LOOP 02          08:00 SYN PIANO    + NEW LOOP: the number it takes, the keys' instrument
 + :REC: A NEW LOOP...............:        a dashed strip: where REC puts the stage

-STAGE SYN PIANO M ----- REC: INTO LOOP 01 the stage: its sound and mode, REC's target
|o  ####..##|##.....                   |   its strip, in the selected loop's colour
+--------------------------------------+
   1 ------ 2 ------ 3 ------ 4 ------     the length's bar: a segment a bar, the play tick
WHITE KEYS: MUTE  HELD: AT THE BAR         the hint line (only where a state changes the keys)
08:00 SYN PIANO JUNO                MONO   the footer: the keys' instrument; MONO / POLY
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the tempo | 2, 19 | | T | `120 BPM`, light grey |
| the length | 50, 19 | | T | `4 BARS`, `1 BAR`, light grey |
| QUANTIZE | 98, 19 | | T | `Q 2 ROWS` ... `Q ROW`, `Q REAL`, cyan |
| REC | 146, 19 | | T | `REC` dark grey; `ARMED` red (armed, counting in); `SONG` red (a song armed) |
| the beats | `194 + k × w`, 21 | `(w − 2)` × 8, `w = floor(44 / BEATS A BAR)` | | deep green; the playing one yellow on the bar's first beat, green on the others |
| ADJUST's top line | 2, 19 | | T | `LOOP 03: SHIFT +17 ROWS`, white, in place of tempo, length, QUANTIZE and REC |
| a song recording | 2, 19 ... | | T | as today: `SONG` red at 2, the time at 32 (26 past 10 minutes), its patterns at 68, the tempo at 146 |
| the line | 0, 31 | 240 × 1 | | dark grey |
| slot *k* (0-3) | 0, `y = 33 + 32 k` | 228 × 28 | | the loop `top + k`, or `+ NEW LOOP` |
| its number | 5, y + 8 | | S | `00`-`0F` cyan (dark grey silent, muted or silenced); with the keys shown (CONTROL, SEQ held) ` 1`-`16` white, at 2 from `10` |
| its sound | 28, y | | T | 9 characters, in its colour (dark grey silenced; red when `STAGED`) |
| its mode, repeats | 88, y; 100, y | | T | `P` / `M`; `X2`-`X9`, `X+`; light grey (dark grey silenced) |
| its SHIFT | 118, y | | T | `SHIFT +3`, `SHIFT -3` (the nearer way round; none at 0), light grey |
| its state | right-aligned to 226, y | | T | `STAGED` red; `M` / `S` yellow (waiting for the bar line); `S` yellow (soloed); `M` dark grey (muted) |
| its strip | 28, y + 13 | 180 × 14 | | [below](#the-strips) |
| the selected slot's box | 0, y − 1 | 228 × 29 | | white |
| `+ NEW LOOP` | 9, y + 8; 28, y; right-aligned to 226, y | | S; T; T | `+` white; `NEW LOOP 02` white; the keys' instrument (`08:00 SYN PIANO`, its name 10 characters) in its engine's colour |
| its strip | 28, y + 13 | 180 × 14 | | dashed: a dark grey dot every 4 px top and bottom, solid ends; `REC: A NEW LOOP` (with notes on the stage `REC: THE STAGE GOES HERE`) at 34, y + 14, dark grey |
| the overview column | 229, 33 | 11 × 128 | | [below](#the-overview-column) |
| the stage's frame | 0, 168 | 240 × 29 | | dark grey, its top edge broken for its title and REC's target |
| its title | 6, 162 | | T | `STAGE` cyan, the keys' instrument (10 characters) in the stage's colour, ` M` / ` P` light grey; while OVERWRITE is on `STAGE OVERWRITE`, OVERWRITE red, a red dot (radius 3) at 104, 168 while it is kept |
| REC's target | right-aligned to 234, 162 | | T | `REC` yellow, `: INTO LOOP 01` / `: NEW LOOP 02` light grey |
| its dot | centre 13, 185 | radius 4 | | red while keys overdub, deep red while the loops play, deep grey stopped |
| its strip | 28, 176 | 180 × 18 | | the stage as it plays (MONO: one note at a time), in the stage's colour |
| `EMPTY` | 208, 179 | | T | dark grey, while the stage holds no note (its last pixel at 236, two clear of the frame) |
| the length's bar | 28, 199 | 180 × 12 | | a segment a bar: its number (T) at the segment's x, white in view, dark grey out of it; a 3-px rule from x + 7 to the next segment's x − 2 at y + 5, light grey in view, deep light grey out; the play position's tick, white, 1 × 11 at y + 1 |
| the hint line | 2, 214 | | T | [below](#the-hint-line) |
| the footer | the frame's | | | the keys' instrument at the left; at the right `MONO` / `POLY`, `REC: TO STAGE` in the pattern recorder, `MEMORY 81% FREE` red while a song records |
| no loops yet | 2, 70 / 82 / 94; 2, 112; 2, 124 | | T | `PLAY, THEN PLAY NOTES:`, `THEY GO ON THE STAGE`, `REC KEEPS THE STAGE AS A LOOP` light grey; `PRESETS, ENV: THE SOUND` and `GLO: LENGTH, TEMPO, VIEW` (the buttons yellow) |

### The strips

- **The window:** VIEW's bars (at most the length) from the page the play position is on, pages of VIEW's bars from
  the loops' start, the last page ending at the length's end. While the loops play the window turns a page as the
  play line leaves it; stopped, it shows the play position's page.
- **A row a cell,** `x = 28 + floor((row − start) × 180 / rows shown)`, a pixel's gap after each cell 4 px or wider; a
  lane a band of the strip's height (`floor((h − (lanes − 1)) / lanes)` px each, a pixel apart; a MONO loop's one lane
  fills it).
- **Each cell:** empty, the ground (deep light grey on a beat's first row, deep dark grey otherwise); **a note's
  sustain** in the loop's colour; **its attack** (its first cell, when it is longer than one) and **its release** (the
  cells after its end, up to the lane's next note: 2 for a DX7 or JUNO-106 instrument, 1 for the others) in that
  colour's deep shade; a note one cell long full. A silenced loop's notes in dark grey and its deep shade. Repeats
  drawn as played, the rest empty; SHIFT applied.
- **Bar lines** light grey in the gap before each bar's first cell, a pixel over and under the strip; **the play line**
  white the same way, in every strip and the stage's.
- **The stage's strip:** a held key's note drawn only up to the play line, cell by cell as it moves; ahead of it the
  lane shows what the stage had, up to its next note there; a note landing ahead (quantized forward, or in the next
  cycle) drawn when the line reaches it.

### The overview column

A cell a loop, 16 of them, 8 px apart from y 33: **a 6 × 6 square** at 231, `34 + 8 i` in the loop's colour (red
while the stage stands for it, dark grey muted or silenced), a yellow 8 × 8 frame round it while soloed, a yellow 2 × 2
dot in it while a mute or solo waits; **`+ NEW LOOP`** a plus filling the same 6 × 6 box, 2-px strokes, dark grey;
the cells after it black. **The selection** a small circle in its cell: a white ring round black, its loop's colour a
2 × 2 dot in the middle (red standing for the stage, dark grey silenced; light grey on `+ NEW LOOP`, the circle in the
plus's place). **The slots in view** in a white frame, 11 px wide, round their cells.

### The hint line

Only where a state changes what the keys or REC do; empty otherwise (its row kept, so nothing moves):

| State | Hint |
|---|---|
| ADJUST | `K1 ROW  K2 BAR  SEL KEEP  HOME CANCEL` (the buttons yellow) |
| armed, not playing | `ARMED: A NOTE OR PLAY STARTS AT 2.4.3` (`ARMED:` red, `PLAY` yellow) |
| LFO held | `OVERWRITE  +REC: KEEP  +REC HELD: CLEAR` (`OVERWRITE` red, `+REC`, `+REC HELD` yellow) |
| OVERWRITE kept | `OVERWRITE KEPT  LFO: BACK TO OVERDUB` (`OVERWRITE KEPT` red, `LFO` yellow) |
| CONTROL mode, SEQ held | `WHITE KEYS: MUTE  HELD: AT THE BAR` (`SOLO`) (`WHITE KEYS`, `HELD` yellow) |

### The stage's colour

The selected loop's colour (on `+ NEW LOOP` the colour the new loop will get), coloured as the loops' strips;
OVERWRITE's marks red.

### The pattern recorder

The box as today (4, 36, 232 × 186), with: the title `RECORD PATTERN: TO STAGE`; a tie drawn in the deep shade of
the colour (light grey on the step playing), a 3-px bar in the colour joining it to the step before, at its middle;
the K lines `K1 NOTE
LENGTH` `1/16`, `K2 LENGTH` `16 STEPS`, `K3 NOTE` `G-4`, `REPEATS` `OVER 4 BARS` (dark grey, both); what it makes at
12, 192; the keys' line `KEY: A STEP  HELD: A TIE  SEL: NOTE` at 12, 206.

## 14. The LEDs

- **REC** blinking while armed, a song armed or a count-in runs; full while a song records or the stage holds notes
  (REC sends them); else dim.
- **LFO** dim; blinking while OVERWRITE is kept.
- **EDIT** dim on a loop, dark on `+ NEW LOOP`.
- **SEQ** full in CONTROL mode or held, else dim. **ARP**, **ENV** dim.
- **PLAY** as for a loop ([../ui/transport.md](../ui/transport.md#plays-two-leds)): its red never dark on LOOPS (the
  loops play with none); dim at the start, after a stop too.
- **ADJUST open:** SEL full, HOME dim; REC, LFO, EDIT, GLO, ENV, SEQ, ARP dark.
- **The pattern recorder:** ARP full, REC dim, SEL full while it waits for a key (else dim); GLO, SEQ, EDIT, LFO, ENV
  dark; the keys as [section 12](#12-the-pattern-recorder-arp) says.
- **The keys** in CONTROL mode or with SEQ held: white key *n* full while loop *n* sounds, dim muted or silenced,
  blinking while its mute or solo waits, dark with no loop; OP1-OP5 the QUANTIZE chosen full, the others dim; PIT dim;
  the GLO key full in SOLO; MONO or POLY full by the keys' mode, the other dim. In PLAY mode the keys light with their
  notes.

## 15. Texts and help

**New:** `THE STAGE IS EMPTY`; `STAGE AS LOOP 06`, `STAGE INTO LOOP 03`; `LOOP 03 ON THE STAGE`; `STAGE CLEARED`;
`OVERWRITE KEPT: LFO ENDS IT`, `OVERDUB`; `LOOP 03 DELETED`; `LOOP 03 QUANTIZED: 4 ROWS`, `QUANTIZE LOOP 03`; `LOOP
03: SHIFT +17 ROWS`; `KEYS: MONO`, `KEYS: POLY`; `KEYS: 01:04 BRASS, MONO` (the pipette, TAKE ITS INSTRUMENT);
`PATTERN ON THE STAGE`; `THE STAGE IS FULL`, `STAGE NOT WRITTEN: STORAGE BUSY`; `GRID: ONLY WITH NO LOOPS`, `GRID:
ONLY WITH THE STAGE EMPTY`; the knobs' `PLAY FROM
2.4.3`, `JUMP +1 ROW`, `JUMP -16 ROWS`, `VIEW 2 BARS`; GLO's `VIEW` (`1 BAR A SCREEN`, `2 BARS A SCREEN`, `4 BARS A
SCREEN`), `K1 TURNS`-`K4 TURNS` and their jobs, LENGTH's `2 BARS (NOW 4 BARS)`; the menu's TAKE ITS INSTRUMENT,
ADJUST, QUANTIZE, DELETE LOOP; the screen's `ARMED`, `STAGE`, `OVERWRITE`, `REC: INTO LOOP 01`, `REC: NEW LOOP 02`,
`NEW LOOP 02`, `REC: A NEW LOOP`, `REC: THE STAGE GOES HERE`, `STAGED`, `SHIFT +3`, `EMPTY`, `ADJUST`, the hint lines,
the empty screen's five lines; the recorder's `RECORD PATTERN: TO STAGE`, `K1 NOTE LENGTH`, `1/2`, `1 BAR`, `TO THE
NEXT`, `REPEATS`, `OVER 4 BARS`, `KEY: A STEP  HELD: A TIE  SEL: NOTE`, `REC: TO STAGE`.

**Changed:** CLEAR LOOPS' second line `THE STAGE TOO; THE LENGTH STAYS` (was `THE NEXT ONE SETS THE LENGTH`); APPLY
INSTRUMENT's toast with the mode (`LOOP 03: 01:04 BRASS, MONO`); `CUT TO 2 BARS?` / `NOTES IN THE BARS CUT GO` asked
as GLO closes, not at the turn.

**Gone:** `LOOP 03 RE-RECORDING`, `LOOP 03 PEELED`, `LOOP 00: 4 BARS`, `NOTHING PLAYED: NO LOOP`, `REC, THEN PLAY: THE
FIRST LOOP`, `NEXT LOOPS: MONO` / `POLY`, `PATTERN: LOOP 06, 4 BARS`, `PATTERN INTO LOOP 03`, `NOTES RING TO THE NEXT`,
`NOTES A STEP LONG`, `GRID: ONLY BEFORE THE FIRST LOOP`, `THE FIRST LOOP SETS IT`, `THE NEXT ONE SETS THE LENGTH`,
`-- BARS`, `REC 3/4`, `REC 3`, `ARMED: STARTS IN 3 BARS` (and its others), `ARMED: A NOTE OR PLAY`, `REC, THEN PLAY OR A
NOTE:`, `THE FIRST LOOP SETS THE LENGTH`, `PLAY OR A NOTE: THE FIRST LOOP`, `PRESETS, ENV: ITS SOUND`, `16 OF 16`,
`RECORD PATTERN: NEW LOOP`, `RECORD PATTERN: INTO LOOP 03`, `K1 STEP SIZE`, `K4 TIMES`, `X4`, `FILLS 4 BARS`, `A ROW`,
`SELECT: ROW  SEL: NOTE  KEYS: STEPS`, `REC: WRITE`, `BETA`.

**Every other LOOPS text stays,** among them the pattern recorder's `KEYS: THE NOTE` and `KEYS: THE STEPS` (SEL there),
which sit beside the new `KEYS: MONO` / `KEYS: POLY` / `KEYS: 01:04 BRASS, MONO` with no clash: the recorder's are
shown only while it is open.

**The help,** four pages; each line fits 40 characters as drawn, a page 17 lines (the first 15 of its own), and each
page packs within a text's 255 tokens (the longest, 563 characters, is shorter than pages that pack today):

```
[LOOPS]: A LOOP STATION
{PLAY}: THE LOOPS GO ROUND THE LENGTH
  (4 BARS AT FIRST; {GLO}: LENGTH)
THE STAGE: ONE LOOP YOU PLAY OVER
  AND OVER; A NOTE REPLACES WHAT IT
  HAD WHILE YOU HOLD IT
{REC}: THE STAGE TO THE LOOPS, ADDED
  ON + NEW LOOP, OR INTO THE LOOP;
  STOPPED, EMPTY: ARM
{SELECT}: A LOOP, OR + NEW LOOP
{EDIT}: THE LOOP ONTO THE STAGE
{LFO} HELD: OVERWRITE, NOT OVERDUB
  + {REC}: KEEP IT, HELD: CLEAR STAGE
{PLAY}: PLAY / STOP, HOLD: TO THE START
```

```
{ALGORITHM}: WHERE PLAY IS, A ROW
  (HOLD {SEL}: A BAR); PLAYING: A JUMP
{K1}-{K4}: LEVEL, PAN, REVERB, TEMPO
  ({GLO}: EACH KNOB'S JOB)
{PRESETS}: THE SOUND YOU PLAY,
  {ENV}: ITS CARTRIDGE
HOLD {ENV}: THE LOOP'S SOUND AS YOURS
THE KEYS SOUND AS KEPT: QUANTIZED,
  MONO ONE NOTE, POLY 4 AT MOST
THE STAGE PLAYS IN THE SOUND YOU PLAY
{SEQ} (SHOW ELSE, QUICK): THE {KEYS}
  PLAY NOTES, OR CONTROL THE LOOPS
HOLD {SEQ} + A KEY: AS IN CONTROL
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
{MONO}, {POLY}: HOW YOU PLAY AND KEEP
  MONO: ONE NOTE, AS MONO LEGATO SAYS
  POLY: UP TO 4 NOTES
```

```
{ARP} (AGAIN, REPEAT, PLEASE): RECORD
  PATTERN ONTO THE STAGE, 4 ROWS
  {SELECT}: THE ROW
  {WHITE KEY}: A STEP, HOLD: A TIE
  {K3} OR {SEL}, THEN A KEY: ITS NOTE
  {K1} NOTE LENGTH, {K2} STEPS: A ROW
  EACH; A ROW'S NEXT NOTE ENDS ITS LAST
  {ARP}: THE PREVIEW ON / OFF
  {REC}: ONTO THE STAGE, {HOME}: CLOSE
{GLO}: TEMPO, SWING, LENGTH, GRID,
  QUANTIZE, MONO LEGATO, COUNT-IN,
  VIEW, K1-K4'S JOBS
{SEL}: TAKE ITS INSTRUMENT, APPLY,
  ADJUST, QUANTIZE, DELETE, DOUBLE,
  RECORD LIVE, SAVE TO / LOAD FROM
  PATTERN, CLEAR LOOPS, REVERT
{FX}: THE LOOPS' EFFECTS
```

## 16. The record stays version 4

Everything new is held in spare bits and bytes, each given its meaning with 0 as before
([../files/README.md](../files/README.md#extending)), so the record keeps version 4 and nothing is converted: a record
written before reads as the new one with its defaults.

**The head** (the rest as [../files/loops.md](../files/loops.md#the-record) has it):

| Bytes | Field |
|---|---|
| 4 | bit 5 **the keys' mode** (0 POLY, 1 MONO): how the keys play on LOOPS and how REC keeps the stage (the same bit as the next loops' mode before) |
| 7 | **the length in rows, 1-128, always,** with loops or none; 0 (a record from before with no loops) read as 4 bars, or the most whole bars that fit 128 rows where 4 do not ([section 3](#3-the-length)) |
| 78 | bits 2-0 K1's job, bits 5-3 K2's (the jobs, by number: 0 LEVEL, 1 PAN, 2 DUCK, 3 CHORUS SEND, 4 DELAY SEND, 5 REVERB SEND, 6 TEMPO, 7 VIEW); bits 7-6 **VIEW** (0 2 BARS, 1 1 BAR, 2 4 BARS; 3 not written, read as 2 BARS) |
| 79 | bits 2-0 K3's job, bits 5-3 K4's; bit 6 **the jobs are stored** (0: K1-K4 LEVEL, PAN, REVERB SEND, TEMPO, whatever bits 2-0 and 5-3 of bytes 78 and 79 hold; written 1); bit 7 0 |

**A loop's entry:**

| Bytes | Field |
|---|---|
| 2 | bit 7 **its SHIFT's bit 6** (the rest as before: its colour, MONO, muted; bit 6 a solo once, written 0, never read) |
| 3 | bits 1-0 its lanes − 1; bits 7-2 **its SHIFT's bits 5-0** |

- **SHIFT,** 0-127: the rows the loop plays later, round the length (it plays its row (row − SHIFT) mod the length,
  its repeats and rest turned with it); 0 none. A SHIFT not under the length (written by another firmware after a
  shorter LENGTH) is read modulo the length.
- **Not stored:** the stage, the play position (the loops come back at the start), an arm, ADJUST, a planned LENGTH,
  OVERWRITE kept, the keys' CONTROL mode, the pattern recorder's steps.
- **An older PT/FM** reading a record written by this one plays every loop unshifted (SHIFT is lost to it), keeps its
  own K1-K4 and view, and takes no length while there are no loops (its first loop sets one; it writes 0 there itself
  when it rewrites the head with no loops, which this firmware reads as 4 bars). Its edits keep the bits it does not
  know (a mute, a lane count), but a loop it makes or re-records starts with SHIFT 0.
- **The PC side:** `editor/js/records.js` is the only reader and writer of the record's bytes (`unpackLoops`,
  `packLoops`, `loopsV3`, `loopsV4to3`); `tools/ptfm_records.py` only gives the kind its place in a disk's order, and
  `tools/pack.py` writes no LOOPS record (the demo disk has none). `records.js` reads and writes the new fields and
  writes byte 7 with no loops (today 0); it already carries an entry's and the head's spare bits through. Its version
  3 converter writes the length with no loops too (4 bars); **version 4 down to 3** (WRITE to an FM-1 that takes only
  version 3) writes each loop out as heard, SHIFT included, so it plays the same, and names what version 3 cannot
  hold: VIEW and K1-K4's jobs when they are not the defaults, and with no loops the length (`NOT KEPT: VIEW, K1-K4'S
  JOBS, THE LENGTH`). The install page and the emulator
  have nothing of LOOPS to convert: their conversion step after an install
  ([../files/blob.md](../files/blob.md#converting-after-an-install)) carries the samplers and the loops' version 3, as
  it does.
- **The EDITOR's LOOPS tab** ([../editor.md](../editor.md)): each loop's SHIFT in its row (0 to the length − 1, shown
  signed the nearer way round), the notes' grid as stored with `SHIFT +3: PLAYS 3 ROWS LATER` in its hint; the preview
  plays through the firmware, so it hears SHIFT. VIEW and K1-K4's jobs are the FM-1's own and not shown.

## 17. The firmware

### The stage in the player

- **The stage is a loop more** in the player's view of the loops (as the pattern recorder's preview is): its stream
  read from the store as a loop's ([below](#the-stages-piece)), its base the length, played once, its instrument and
  mode the keys', its voices through the stage's track's mix (the selected loop's, or the next loop's on
  `+ NEW LOOP`) and never that track's mute or the solos (a column of their own carrying that mix, or the mute check
  passing the stage's voices by: the builder's choice). **MONO with more than one lane** it plays on one voice: a
  note on any lane takes over, an end counts only on the lane whose note sounds.
- **The keys on the stage have 4 lanes of their own,** as RECORD LIVE's keys' lanes: each note asked for at its
  landing row (ahead) or at once, with its note delay, glide, `LEG` or slide, through the stage's mix. **A queued
  note stands in for the stage's cell** at its row and lane that once (the stage's lane does not start it then), so a
  note is heard once as it is played and from the stage after.
- **The lanes held:** the main loop tells the player which of the stage's lanes a key down replaces (POLY all four,
  MONO lane 0), from which row; from that row those lanes are not read and their notes end, until the key is let go.
  OVERWRITE does the same with all four from the line. Only the replaced lanes fall silent, and only from the landing
  row.
- **The selected loop's silence:** the loop the stage stands for (or none) is told to the player; its lanes are not
  read and its notes end; the stage rests (and the silence with it) while a song is armed or recorded.
- **SHIFT:** each loop's row read at (row − SHIFT) mod the length, its repeats and rest from there.
- **A jump:** the replay's next row moved d rows round the length; forward past the end counts a pass; a loop landing
  in its rest has its lanes ended.
- **Goes:** the take (`player_lp_take` and its state), the first loop's clock.
- **The preview** gains each row's ties (16 bits) and NOTE LENGTH's rows (or to the next); its TIMES, its clock and the
  notes' switch go; REC keeps its sounding notes going on as the stage's. It and its single notes (K3, OCT, SEL + a key,
  a step switched on) sound through the stage's mix as the stage does, never a mute or a solo.
- **RECORD LIVE** (`play/live.c`) reads each loop at (row − SHIFT); the stage is not among what it writes.

### The stage's piece

The stage's notes are kept as the edit modes' copies are today ([../files/ram.md](../files/ram.md#editing-and-saving),
`store/store.c`): in the edit memory, under a key of their own that no record has.
- **The copies today:** an edit mode keeps each entry it changes as it was, under its kind with bit 7 set (`K_SESS`).
  Those keys sort after every edit's, so `store_edit_count` stops at them, a SAVE's building never reaches them, a
  thing's own entries (`store_edit_obj`, `store_edit_forget`, REVERT of a thing) are other keys, and they are never
  told to the watch. They take room as any piece, squeezes move them, and a full edit memory refuses them. A SAVE's
  end and REVERT ALL drop every entry, the copies with them (`edits_none`); an edit mode's start or end drops the
  copies alone (`store_edit_session`, `store_edit_session_drop`).
- **The stage the same way:** one piece under a kind of its own in that range (bit 7 set, its low bits no record
  kind's: 0xFF), so it is no edit: not counted, not saved, not LOOPS' (LOOPS' REVERT and the LOOPS object leave it).
  Three differences from the copies:
  - an edit mode's start or end leaves it (the session code takes only the copies' keys, those under 0xFF);
  - it is told to the player when it changes, as a loop's stream is, so the player's stage reads it again;
  - **a save's end and REVERT ALL keep it.** Its key is the highest there is, so its entry is the list's last;
    `edits_none` drops the entries from the list's first up to it and stops there, so the entry stays where it lies,
    and its bytes too: the pieces' top becomes its end and the room in use its own, the room under it free between
    pieces, which the next squeeze joins by moving it down as any piece. Nothing is copied; about ten lines of
    `edits_none`, and the player told of no change.
- **Its changes** are `store_edit_piece`'s as any piece's: `STORE_ERR_FULL` (`MEMORY FOR EDITS IS FULL`),
  `STORE_ERR_BIG` past 3,800 B (`THE STAGE IS FULL`), `STORE_ERR_STATE` while a save is written or seeding runs
  (`STAGE NOT WRITTEN: STORAGE BUSY`); each refuses the note ([section 4](#4-the-stage)), and EDIT or DOUBLE whole,
  nothing changed.
- **No undo copy:** EDIT replaces the stage and REC writes a loop with no copy kept; REVERT is the way back.

### RAM

| What | Where | Bytes |
|---|---|---|
| the stage's notes | the edit memory, a piece of a kind of its own beside the edit modes' copies ([above](#the-stages-piece)), only while the stage holds notes | up to 3,800, and its list entry's 8 |
| the stage in the player: its loop entry (28) and 4 lanes (72); the keys on it, 4 lanes (72) | the player's state | 172 |
| the lanes held and their row, the silent loop, each loop's SHIFT | the player's state | about 20 |
| the take's state goes; the preview's ties come, its TIMES and clock go | the player's state | about +7 |
| **the player, in all** | the data region's variables: the release keeps 8,192 B spare (8,768 now), the debug build 7,168 (8,096 now) | **about +200** |
| the take's fields go (the loop armed and the take's flags, its pass and start, the lanes' rows: about 25 B); the stage's come: each held key's span (in `lp.kpass[27]`, RECORD LIVE's per-key bytes in the same struct, which a song and the stage never use at once), OVERWRITE's eraser and latch (3), the first slot in view (1), ADJUST (2), a planned LENGTH (1), where PLAY started (1), the loop told silent (1), the setup seen (3), the last line handled (2) | `ui/scr_loops.c`'s state (188 B now) | about −11 |
| the recorder's ties (8) and NOTE LENGTH; TIMES and the notes' switch go; a step key's hold timed with CONTROL mode's key-hold state (the recorder open, CONTROL's keys never act) | `ui/lp_rec.c`'s state (30 B now) | about +6 |
| **the UI's variables** | 28,662 of their 28,672 B now (`fw/tests/run_ui.sh` counts them) | **about −5** |
| the strips' cells (attack, sustain, release) | the screens' shared scratch, as each strip is drawn; the spans' cache as today | 0 |

The figures are the latest builds'; the build measures them again. The margin is thin (the UI's variables about 15 B
under their budget after the change). Should the UI's count still pass 28,672 B once built, its budget is raised by
the excess, at most 64 B, in `run_ui.sh` and in [../files/ram.md](../files/ram.md), whose rows for the screens (their
state, 21,919, and the room for their next additions under the 28 KB cap) the specs step reconciles with the build's
count first; the release keeps 8,192 B spare.

### The slot

The take, the first loop's growing and closing, the re-record, the peel and the armed countdown go (about 3-4 KB of
`scr_loops.c`'s 16.8 KB); the stage's overdub and OVERWRITE, REC and EDIT, the play position and jumps, K1-K4's jobs,
VIEW, ADJUST, QUANTIZE, the new drawing (four slots, the overview, the stage's panel, the length's bar, the hint line)
come (about 6-9 KB); the player's stage, SHIFT and jump (about 1.5-2 KB), the recorder's NOTE LENGTH and ties (about
0.5 KB), `play/loops.c`'s SHIFT through LENGTH, DOUBLE and SAVE TO PATTERN and the loop's QUANTIZE (about 1-1.5 KB),
the texts and help (about 0.5 KB packed). About +6-10 KB in all, of 35,112 B left in the release (32,152 in the debug
build); each task's build prints it.

### What changes where

- `play/loops.c`, `loops.h`, `play/rec.c`, `rec.h`: the length always (no 0 with no loops, kept by CLEAR LOOPS and
  DELETE LOOP), VIEW, K1-K4's jobs, SHIFT (read, set, written out by LENGTH, kept by DOUBLE, applied by SAVE TO
  PATTERN, 0 by LOAD), the loop's QUANTIZE, REC's write (a loop's notes, instrument, mode, lanes, SHIFT 0, the stray
  ends dropped), the stage's piece.
  - Before a longer LENGTH a loop whose last repeat the old length cuts is written out
    over it (repeats it never reached dropped), so the new bars are silence; REC writes the notes EDIT brought as they
    are (a note across the line keeps its end past it), "no wrap" holding for the notes played onto the stage; EDIT or
    DOUBLE that would take the stage past 3,800 B is refused whole (`THE STAGE IS FULL`).
- `store/store.c`, `store.h`: the stage's piece ([above](#the-stages-piece)): its key, its reader, the session code
  leaving it, its changes told.
- `play/player.c`, `player.h`: [above](#the-stage-in-the-player). `play/live.c`: SHIFT; the stage left out.
- `ui/scr_loops.c`, `ui/ui_lp.h`: the screen; `ui/lp_rec.c`: the recorder; `ui/ui.c`: no `BETA`.
- `ui/scr_loops.c`'s state cursor and `remote.c`'s state (LOOPS' bytes at its end, [section 19](#19-what-else-changes)),
  `fw/web/autotest.js` (the LOOPS scenarios).
- The help is the specs' Help part ([section 15](#15-texts-and-help)), packed by `fw/tools/gen_texts.py`.

### Building it

One task at a time, each tested on its own:

1. **The record:** `play/loops.c`, `rec.c`, and the PC side (`editor/js/records.js`): the fields both ways, a record
   from before read with the defaults, the length with no loops, SHIFT through LENGTH, DOUBLE and SAVE TO PATTERN,
   QUANTIZE, REC's write. Tested: `fw/tests/run_loops.sh`'s `loops_test.c`; the EDITOR's record tests both ways, the
   version 3 converters with SHIFT and the length. The build's figures (the RAM spare, the slot's room, the UI's
   variables) measured again here, as the plan's base.
2. **The stage's piece:** the store keeps it as [above](#the-stages-piece). Tested: `run_store.sh` (not counted, not
   saved, not in LOOPS' object; LOOPS' REVERT and an edit mode's start, EDIT and HOME leave it; **it survives a save
   and REVERT ALL,** byte for byte, also a save that writes with the stage at its 3,800 B and one that compacts, and the
   edits made after either fit as before, the next squeeze moving it down; a squeeze moves it and its reader follows;
   a full edit memory refuses a change, a stream past 3,800 B too, and a change while a save is written gets
   `STORE_ERR_STATE`). Where the store's code must change besides `edits_none`:
   - the session code (`sess_none`, `store_edit_session_drop`) walks every key from kind 0x80 up: it stops under 0xFF,
     or it drops the stage or turns it into an edit;
   - `edits_none` is also the path of a save with nothing to write and of REVERT ALL: all three keep the stage and
     set the pieces' top and the room in use to its end and its size;
   - `store_edit_piece` refuses kinds that are not a piece's: the stage gets its own entry point;
   - the watch, which skips keys with bit 7 set, is told of the stage (as kind `STORE_STAGE`), so its changes and
     moves reach the player as a loop's stream's do.
3. **The player:** the stage, its keys' lanes and their stand-in, the lanes held, MONO on one voice, the silent loop,
   SHIFT, the jump, the take gone, the preview's ties and NOTE LENGTH, RECORD LIVE's SHIFT. Tested: `lanes_test.c`
   (SHIFT 3 plays 3 rows later; a note heard once, then from the stage; the lanes held from their row; a jump back and
   forward past the end; a POLY stage played MONO); `run_sound.sh`'s core-2 test; and **the sound before and after,
   sample for sample:** the demo disk's 18 songs and DJ's stand, where LOOPS plays no part, rendered by the old player
   and the new one, equal.
4. **The screen:** `scr_loops.c` and `ui.c`'s mark, the UI's RAM within its budget. Tested: `fw/tests/ui3_test.c`'s
   LOOPS tests rewritten as twins of `tests/playground/loops.js` (each check's steps and texts), their shots against
   the playground's (`fw/tests/ui_shots.js`), the packed texts' run (`run_ui.sh`).
5. **The pattern recorder:** `lp_rec.c`. Tested: the ui3 twins of `loops.js`'s recorder checks.
6. **The help and texts:** the Help part from the specs; `gen_texts.py`'s checks; the help's host test.
7. **The state and the AUTOTEST:** the state's LOOPS bytes; `fw/web/autotest.js`'s LOOPS scenarios
   ([section 19](#19-what-else-changes)); SYNC EXTERNAL on LOOPS (`ui3_test.c`'s sync test). Tested: `run_autotest.sh`
   at its scale, `run_ui3.sh`.
8. **The EDITOR page:** its LOOPS tab's SHIFT; `tests/editor/run_tests.sh`.
9. **The manual:** [section 19](#19-what-else-changes); `fw/tests/run_manual.sh`.
10. **CHANGELOG.md, the version's report, `dist/`** (firmware, debug, emulator, playground, editor, manual) rebuilt;
    every suite (`fw/tests/run_tests.sh`, `tests/run_playground.sh`, the EDITOR's) green. Not tried on the FM-1 until
    a run there.

## 18. The playground

The playground has LOOPS as above, but for these, which it changes before the firmware is written (each with a check
in `tests/playground/loops.js`):

- **SHIFT where a loop is read:** RECORD LIVE's song (`laneAt`, `songPlan`), SAVE TO PATTERN (`store`), SEL's QUANTIZE: today each ignores it, so what is written differs from what was heard.
- **LENGTH with a SHIFT** writes the loop out as heard first (today `shift % n` moves the loop's wrap point, and a
  longer length moved what plays at its start); the CUT question counts the notes as heard. Before a longer length a
  loop whose last repeat the old length cuts is written out over it too, so the new bars are silence.
- **REC ends the arm** when it sends the stage (today, armed + EDIT + REC leaves it armed, and the next key starts the
  loops unasked).
- **After LOOPS' stop PLAY's red is dim** (`P.atStart`): the play position is at the start.
- **`EMPTY` at x 208** (today at 210, its last column touching the frame).
- **PLAY outside LOOPS resumes the loops with none** (today it plays the current song instead, though LOOPS plays
  empty loops).
- **The new fields are filled where the record is loaded,** in the flash's copy and the state alike (today `syncSel`
  fills the state alone, so LOOPS shows unsaved after such a load).
- **A key or LFO silences only the stage's lanes it replaces, from its landing row** (today everything the stage
  scheduled for the next ~120 ms goes, so a short overdub can lose the stage's next row for a cycle).
- **The stage is heard whatever the mutes and solos** (today a muted loop's stage plays through its muted track: the
  keys go unheard); the pattern recorder's preview and its single notes with it.
- **Stray note offs are dropped when the stage is sent** (OVERWRITE's erasing leaves some: bytes in the stream, and
  `===` cells in a pattern SAVE TO PATTERN writes from it); the notes EDIT brought are written as they are, a note
  across the line keeping its end past it.
- **The stage rests while a song is armed or recorded** (today the selected loop is silent but written into the song,
  and the stage is heard only when the song started with the loops playing).
- **A jump:** the keys held stop writing (today a forward jump makes a held key's span erase every row it skipped), a
  loop landing in its rest falls silent, and a note on the stage plays each time the line reaches it (today what was
  written since the pass began is silent after a jump back).
- **The keys down stop writing** when the length changes and when FX opens from LOOPS (today they go on, their spans
  growing only in LOOPS' drawing).
- **GRID refused with notes on the stage;** the length at first **clamped** to 128 rows; **LENGTH steps whole bars**
  (from a part bar to the next whole bar each way; a part bar shown in beats, `21 BEATS`; GRID from a part bar keeping
  its whole bars, rounded down). EDIT or DOUBLE past the stage's 3,800 B refused (`THE STAGE IS FULL`).
- **The stage counted in the edit memory** (its stream's bytes, as a loop's piece), a note refused when the memory is
  full or the stream would pass 3,800 B, with section 4's texts (today the stage takes no edit memory).
- **The keys' numbers `10`-`16` at x 2** in CONTROL mode (today `10` is drawn at 5: the test is `i > 9`, loop 9 being
  key `10`).
- **In ADJUST, LFO held + REC does nothing** (today the screen's REC, which ADJUST passes on, takes LFO's latch before
  ADJUST's refusal).

SYNC EXTERNAL ([section 6](#6-the-play-position-play-and-jumps)) is the firmware's alone: the playground does not follow
a clock.

**Kept as built:** GLO's twelve items (they fit the body: 192 px); the recorder's dark grey `REPEATS` line (it says in
bars what the pattern fills, as no other line does); the hint lines while LFO is held and while OVERWRITE is kept
(what REC does then changes, the hint line's rule); the release cells by engine (2 for a DX7 or JUNO-106, 1 for the
others), in the firmware too: a drawing aid, the same everywhere whatever the tempo and VIEW.

## 19. What else changes

- **The specs,** from this proposal: [../ui/screens/loops.md](../ui/screens/loops.md) (rewritten: sections 3-15, its
  Help part the playground's `help.js` pages, then `tests/playground/checks.js` compares LOOPS' help again);
  [../files/loops.md](../files/loops.md) (section 16; "What is kept"; storing and loading with SHIFT; the versions
  paragraph); [../files/ram.md](../files/ram.md) (the edit memory holds, besides the edits, the edit modes' copies and
  LOOPS' stage, neither saved: the copies gone when a save ends and at REVERT ALL, the stage kept through both; the
  player's state; the screens' rows reconciled with the build's count, the UI's budget raised there if it must be);
  [../ui/saving.md](../ui/saving.md) (SAVE and REVERT ALL leave LOOPS' stage as it is: it is never saved, and no
  change);
  [../ui/controls.md](../ui/controls.md) (LFO, REC, SEQ, ENV held, ALGORITHM on LOOPS; the per-screen table's LOOPS
  column); [../ui/transport.md](../ui/transport.md) (PLAY on LOOPS and outside it; COUNT-IN's LOOPS lines; the LED
  table); [../ui/leds.md](../ui/leds.md); [../ui/frame.md](../ui/frame.md) (the name's mark goes);
  [../ui/navigation.md](../ui/navigation.md) (leaving LOOPS); [../ui/screens/fx.md](../ui/screens/fx.md) (the keys'
  DRY and KEYS GAIN on LOOPS; the MIXER's column the stage sounds through);
  [../ui/screens/playback.md](../ui/screens/playback.md) (PLAYBACK while the loops are what plays; since 2026-11-04 it shows
  no view of the loops, only that they play);
  [../connections.md](../connections.md) (the state; SYNC EXTERNAL on LOOPS); [../debug.md](../debug.md) (the
  AUTOTEST's LOOPS rows); [../editor.md](../editor.md) (the LOOPS tab; WRITE's version 3 conversion);
  [../open-questions.md](../open-questions.md) ([section 20](#20-open)); [../README.md](../README.md) (this proposal);
  `AGENTS.md`'s LOOPS paragraph.
- **The state** ([../connections.md](../connections.md#the-state)): LOOPS' cursor bit 0 says armed (REC's arm, no
  longer a take's), bit 1 the stage holds notes (a take recording before), the length its whole bars rounded down
  (never 0 now: a part bar is possible, [section 20](#20-open)); the state grows at its end by LOOPS' 4 bytes: the
  notes the stage starts (u16), the selected loop's SHIFT, and bits for ADJUST open, OVERWRITE on, OVERWRITE kept.
  Its version stays 1: a reader takes a shorter answer as older, and the only readers of LOOPS' bytes are this
  repository's own pages (the AUTOTEST, the debug page), which change with them.
- **The AUTOTEST's LOOPS scenarios** ([../debug.md](../debug.md#the-scenarios)), with the stage: the loop station's
  twelve loops each played onto the stage and sent with REC (the first started by a key, armed); a beat (the pattern
  recorder onto the stage, REC; a TB-303 line overdubbed in MONO, REC; a chord in POLY, REC; EDIT on the bass, a bar
  overdubbed, REC back; DELETE LOOP in place of the peel; CONTROL mode as today); RECORD PATTERN (onto the stage, REC);
  KEYS' TB-303 part (POLY loops of 4 lanes, each a cycle of chords onto the stage, REC); the rest as today.
- **The manual's LOOPS chapter** rewritten (the stage, the length, OVERDUB and OVERWRITE, REC and EDIT, the play
  position, ADJUST, QUANTIZE, CONTROL mode and SEQ held, the recorder, RECORD LIVE, the menu and properties); its
  shots made again with no `BETA` (`loops-staged`, `loops-overwrite` and `loops-adjust` new; `loops-armed` and
  `loops-first` the new screens); the messages' LOOPS table ([section 15](#15-texts-and-help)); the shortcuts from the
  help; the glossary (stage, overdub, overwrite).
- **CHANGELOG.md:** FEATURE the stage (overdub, OVERWRITE, REC keeping it, EDIT bringing a loop back), the length from
  the start, ADJUST, QUANTIZE and DELETE LOOP in the menu, K1-K4's jobs, VIEW, the recorder's NOTE LENGTH and ties, the
  four-slot screen; CHANGE PLAY on LOOPS, ALGORITHM the play position, MONO / POLY the keys'; REMOVED takes, the
  re-record, the peel, TIMES, `BETA`.

## 20. Open

These go into [../open-questions.md](../open-questions.md) with the specs:

- **LOOPS' holds with no tap route** ([../ui/controls.md](../ui/controls.md#buttons-tap-hold-and-layers)):
  a hold that is itself the playing needs no tap route, as a note key held is the note sounding. LFO held is the
  eraser on the tape (OVERWRITE), a white key held in the recorder a tie; LFO held + REC held, the stage's clear, is a
  shortcut, its tap route REC on `+ NEW LOOP`, then that loop's DELETE LOOP. No new controls.
- **BEATS A BAR** changed with loops, or now with the length alone (it is always set): the length keeps its rows and
  may be left a part bar (already open).
- **The notes' colouring,** to try on the FM-1 against the one above: a note's first cell bright and its held cells
  dim; or only the attack dimmed, no release cells. Perhaps a setting.
- **The release cells from each instrument's envelope** (its release at the tempo) in place of the engine's fixed
  count, if the fixed count misleads.
- **LOOPS' controls on the FM-1:** the overdub's feel (the span, the line), OVERWRITE's hold and latch, SEQ held, the
  jumps, ADJUST, the recorder's ties.

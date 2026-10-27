# The EDITOR

Status: **settled** (2026-10-09), revised 2026-10-24. The records: [the UI proposal](../../proposals/202610072229-ui.md),
sections 6.2 and 13.2; the takes' QUANTIZE, [the loop station proposal](../../proposals/202610240236-loops-station.md),
section 5.

The EDITOR writes patterns: a grid of rows and tracks, the keys writing notes with the current
instrument, K1-K4 the cell's volume and up to four commands, a note editor that lists a cell's fields,
blocks, a row preview, and realtime takes. The patterns' format: [../../files/patterns.md](../../files/patterns.md);
the commands: [../../files/commands.md](../../files/commands.md). Its black key is OP2
([../navigation.md](../navigation.md)); it is in the MAKE group.

## What it shows

**Subviews (SEQ):**

| Subview | Rows | A cell shows |
|---|---|---|
| 8 tracks | 16 | the note only (8 × 16) |
| 4 tracks | 12 | the note and the instrument ID (6 × 12); 4 whole cells do not fit ([../README.md](../README.md#limits-the-screens-must-respect)) |
| 2 tracks | 8 | the cell with one of its commands: the note in 8 × 16, the rest in 6 × 12 (the cell in 8 × 16 is 4 characters too wide) |
| 1 track | 8 | the whole cell: the note in 8 × 16; beside it, in two lines of 6 × 12, the instrument over the volume, then the four commands two a line, each its 3 characters and a short name |

In every subview the footer shows the current instrument and, on the right, the command (or volume)
in K1's slot of the cell under the cursor ([../frame.md](../frame.md#the-footer)).

**One command at a time, or all four.** A cell holds up to four commands. The 8-, 4- and 2-track views
show one: the one in the slot K1 picks (below), the first while K1 is on the volume. The 2-track view
shows it after the volume, with a pip under it for each command the cell holds (the one shown bright),
and names the slot above the column (`VOL`, `CMD1`-`CMD4`); a line under the cursor's cell marks the
field K1 picked. The 1-track view shows all four on every row (2026-10-09): the note in the big font,
the commands in two lines beside it, fewer rows a page. There the field K1 picked, in the cursor's
cell, is drawn white, nothing else marking it, so that white has to stand out in every colour theme.
The note editor lists all four, a line each.

**A command takes 3 characters:**
- a tracker effect: its letter and value (`A04`);
- an XM volume column's effect: `~`, FastTracker 2's sign, and its digit (`~-4` a volume slide down);
- PT/FM's own: its name (`ACC`, `SLD`, `A+S`, `LEG`); a punch-in: `!` and the rows it stays on (`!08`)
  ([../../files/commands.md](../../files/commands.md#punch-ins));
- every other command: its group's sign and its value, so a sweep shows as its values climbing:
  - `*40` an instrument parameter, the sign in the instrument's engine colour;
  - `=70` the track's mixer;
  - `#3C` the FX, the sign in the effect's colour.

  Which parameter it is, or which punch-in, the note editor says.

**Commands never change what you are editing.** A song's `*`, `=` and `#` commands change what they
name as they play, except what is being edited right now; those commands are skipped, and nothing
jumps when the page opens (what last played stays until your hand or the next command that may act):
- **FX, LIVE and EDIT pages:** the `#` commands of the effect under the cursor (its knobs, its ON / OFF);
  the other effects follow theirs, and a song's start keeps that effect as it plays
  ([fx.md](fx.md#the-fx-setup)).
- **FX, MIXER page:** the `=` commands of the mixer shown (the song's).
- **An instrument's edit page:** the `*` commands for that instrument; its notes play as edited,
  without the parameters commands gave its tracks.
- **A song's properties (GLO on SONGS):** its `Fxx` commands; while they are open it
  plays at its own TEMPO, TICKS A ROW, ROWS A BEAT and SWING, as they change.

Tracker effects stay music, not edits: `8xx` and the pan slides still pan a track while the MIXER is
open.

Once the page is left, commands act again: the song's next command for a knob you turned moves it.
The FX page and the MIXER show what plays: a value a command moved off the setup's is drawn in the
effect's colour (on the MIXER: white, `BY THE SONG`) ([fx.md](fx.md)).

**Drawing:**
- **The focus row** (the cursor's) is larger, with a 1-px line above and below. The cursor's cell is a
  bright box.
  - **2-track view:** the notes are in B (12 × 24 bold), and the gaps in that row shrink to 4 px to
    make room.
  - **4- and 8-track views:** B is too wide (8 notes in B take 288 px), so the row is only taller,
    with more space around the text: 16 px in the 8-track view (12-px rows), 20 px in the 4-track
    view (16-px rows).
  - **1-track view:** the focus row is as the others, 24 px; the cursor's box and the lines mark it.
- **The playing row,** when this pattern plays, sits on a deep blue band.
- **The view never follows the playback,** except during a realtime take, when it follows the row being
  recorded.
- **Row numbers** are hex, `00`-`7F` ([../frame.md](../frame.md)). Beat rows are cyan, and the first
  row of each bar (4 beats) light cyan.
- **In a FUSION pattern** the row numbers are hex too; a row is the note it plays (`C-4` is row
  `3C`), so the rows of the Cs (`0C`, `18`, ... `78`) are the cyan ones, and the note editor names the
  row's note (`ROW 3C C-4`). Opened from a FUSION's edit page, the rows the held keys play sit on a
  deep green band ([instruments.md](instruments.md)).

**Scrolling** (nothing wraps):
- **Vertical:** the cursor moves a row at a time. Passing the last row of a page shows the next page.
- **Horizontal:**
  - The 1-track view shows the cursor's track, `<` and `>` saying more lie either side.
  - The 2-track view pages by 2 tracks.
  - The 4- and 8-track views keep one track of look-ahead. Moving onto the last visible track, when
    more follow, shifts the view by 2 tracks, and the cursor appears to jump back.

## Controls

| Control | Does |
|---|---|
| SELECT | the cursor up / down the rows |
| ALGORITHM | the cursor across the tracks |
| SEQ | the subview: 8, 4, 2, 1 tracks, round |
| PRESETS | the current instrument, the one the keys write ([../navigation.md](../navigation.md)) |
| keys | REC off: play the current instrument; REC on: write ([Writing](#writing-rec)) |
| REC | writing on / off; while the EDITOR's loop plays, it plays on and the keys write step by step ([Writing](#writing-rec)) |
| PLAY | REC off: loops the shown pattern from the cursor's row ([../transport.md](../transport.md)); REC on: stops the EDITOR's loop if it plays, else a realtime take that loops round the pattern ([Realtime takes](#realtime-takes)) |
| K1-K4 | the cell's volume and commands ([K1-K4](#k1-k4-the-cells-volume-and-commands)) |
| LFO | clears the cell and advances; on an empty cell writes a note off (`===`) |
| EDIT | the note editor |
| GLO | the pattern's properties |
| ENV | the pattern list |
| ENV held | the pipette: the cursor's cell's instrument becomes the current one (on a cell naming none, the one its track plays at that row); a shortcut for SEL's TAKE ITS INSTRUMENT |
| SEL | the menu: MARK (then SELECT / ALGORITHM mark a block), TAKE ITS INSTRUMENT, or the block's menu; SEL held + SELECT / ALGORITHM marks too |
| ARP | the row preview, in SETTINGS' ARP RHYTHM (whatever plays stops first); ARP held + SELECT changes the rhythm |
| HOME | closes what is open (the note editor dropping its changes, a carried block, a mark); when entered deep, back where it came from ([../navigation.md](../navigation.md)) |
| HOME + white keys | mute tracks; held, or after SEQ: solo ([../controls.md](../controls.md#homes-combination)) |

The buttons it uses light dim: EDIT, SEL, ENV, GLO, SEQ, ARP, LFO, REC. REC is lit while writing is
on and blinks during a take or its count-in; EDIT is lit in the note editor; ARP while the
row preview plays; SEL while a block is marked, carried or transposed ([../leds.md](../leds.md)).

## Writing (REC)

- **REC switches writing.** With REC off the keys only play the current instrument, so sounds and
  notes can be tried freely. With REC on (REC lit), the keys write.
  - REC gates only the note keys in the grid. K1-K4, LFO, the SEL menu and the note editor change
    cells whether REC is on or off: each is a deliberate edit.
  - **REC while the EDITOR's loop plays:** the loop plays on, and the keys write step by step at the
    cursor, as when stopped (below): the cursor moves on by the edit step, and the view stays with it,
    not with the playing row. The loop plays what is written as it reaches it.
    - REC off: back to listening; the loop goes on.
    - PLAY: the loop pauses and REC stays on: writing goes on step by step, and the next PLAY starts a
      take.
    - Realtime writing is the looping take ([below](#realtime-takes)), in silence until it starts.
  - **REC while a song plays:** the song pauses (it keeps its place), since the pattern shown may not
    be the one playing. Writing starts in silence.
- **Every change is heard at once:** a cell written, cleared or changed is heard from the next row
  whatever plays this pattern (the EDITOR's loop, a song, a DJ deck); a row already passed is
  heard on the next pass. Nothing waits for the pattern to start again.
- **Step by step** (REC on, stopped):
  - a key writes its note at the cursor, with the current instrument and KEYS VOLUME
    ([settings.md](settings.md)), so it plays back as it was heard; the cell's commands stay;
  - the note sounds;
  - the cursor moves down by the edit step (GLO; 0 = stay);
  - keys pressed together (a chord) land on the same row, spreading to the next tracks.
- **The instrument** comes with the note: a key writes it with the current instrument (PRESETS). No
  knob changes it.
- **A FUSION's pattern** opened from its edit page ([instruments.md](instruments.md)): with REC off
  the keys play that FUSION as it is being written, its rows lighting as they play.
- **LFO (CLR):** clears the cell and advances. On an empty cell it writes a note off (`===`).

## K1-K4: the cell's volume and commands

They do not advance the cursor.

| Knob | Does |
|---|---|
| K1 | picks the slot: the volume, or command 1-4, up to one past the last command (to add one) |
| K2 | the slot's command: no command, the tracker effects (each E and X one an entry of its own), XM's volume-column ones, PT/FM's own (`ACC`, `SLD`, `A+S`, `LEG`, then the 16 punch-ins, each an entry of its own), then the instrument's parameters (`*`), the track's mixer (`=`) and the FX (`#`). A code another slot of the cell holds is skipped: each at most once. Back to no command removes it, and the later ones move up |
| K3, K4 | its value, a digit each: K3 the first, K4 the second; a one-digit command (E, X, `~`) takes K4. In the three groups K3 picks which parameter, mixer value or FX knob (for `*`, from the table of the track's instrument's engine) and K4 sets its value |

- **On the volume,** K3 steps by 16 and K4 by 1. Past `7F` the cell has none: the instrument's
  default.
- No command has more than two values: a command is a code and one byte.
- **A punch-in's value** is the rows it stays on, `01`-`FF`, K3 and K4 its two digits; it is never
  `00` (picked, it starts at `01`).
  The punch-ins sit in K2's list after `LEG`, in the keys' order (`LOOP 1/4` ... `VINYL STOP`).
- **SEL held** makes K4 step by 16 on the volume and in the three groups.
- **While a knob turns** the header shows its name and value, in the screen name's place
  ([Texts](#knob-readouts)).

## EDIT: the note editor

The cell as a list of fields, a line each, moved through up / down and across rather than with the
pattern's knobs; SEL a tap, not a held combination (2026-10-09):

- **The lines:** NOTE, INSTR (the instrument), VOLUME, CMD 1 to CMD 4: everything the cell holds. A
  command line has two parts, what it is (`TAPE STOP`, `CUTOFF`) and its value (`8 ROWS`, `60`); `ACC`,
  `SLD`, `A+S` and `LEG` have no value. Below the last command, the first empty line reads `+ COMMAND`.
- **SELECT** moves up / down the lines (down to `+ COMMAND`), **ALGORITHM** across a command line's
  two parts. The part at the cursor is drawn white, its line's name too.
- **K1** changes the part at the cursor a step; **K2** in big steps: the note an octave, the
  instrument a cartridge, the volume and a value by 16, a command to the next group's first. K3 and K4
  do nothing here; SEL held + a knob gives no big step (K2 does).
  - **NOTE:** none (`---`), a note off (`===`), then `C-0` ... `G-9`.
  - **INSTR:** `AS THE TRACK` (the cell names none), then every instrument, cartridge by cartridge.
  - **VOLUME:** `00`-`7F`; past `7F`, `AS THE INSTRUMENT`.
  - **What a command is:** K2's list ([K1-K4](#k1-k4-the-cells-volume-and-commands)) with its groups
    opened: each tracker effect, each parameter of the track's engine, each mixer value, each FX code.
    The groups K2 jumps by: none, the tracker effects, XM's volume column, `ACC` / `SLD` / `A+S` / `LEG`, the
    punch-ins, `*`, `=`, `#`. A code another line holds is skipped. Picking a command starts its value
    as K2 does in the grid; back to `NO COMMAND` removes it, and the later ones move up. On
    `+ COMMAND` K1 adds one.
  - **A command's value:** its digits, or the group's range (`*`: the parameter's).
- **A SEL tap** switches SELECT / ALGORITHM to the neighbouring cells, rows and tracks, without
  leaving the note editor; SEL lit, the where line white, a toast `SELECT, ALG: CELLS`. SEL again goes
  back to the fields (`SELECT, ALG: FIELDS`). K1 / K2 keep changing the field the cursor is on. The
  line SELECT picked is kept: a cell with fewer commands shows its nearest line, and the next cell that
  has the line picked shows it again.
- **The keys** change the note, with the current instrument: it sounds, and the cursor does not
  advance.
- **LFO** clears the cell; the cursor stays.
- **EDIT** closes it, keeping the changes; **HOME** closes it, dropping them: every cell changed
  since EDIT as it was ([../controls.md](../controls.md#buttons-tap-hold-and-layers)). The grid's K1
  then picks the line the note editor was on (VOLUME, or that command); opening it starts on K1's.
- **The footer's right side** says the field at the cursor in words (`!08 TAPE STOP 8 ROWS`).

## GLO: the pattern's properties

- TRACKS (1-16), ROWS (1-128), EDIT STEP (0-16); SELECT picks, K1 changes (ROWS by 16 with SEL held).
- USED BY: the songs that play it, and `FUSION` when a FUSION slot does; not changeable. The title is
  `PATTERN 12 PROPERTIES`; on the EDITOR's own pattern `PATTERN PROPERTIES`, without the USED BY line.
- A change that would drop notes asks first.

## ENV: the pattern list

- **Each line:**
  - the number, `00`-`FD` (the EDITOR's own pattern `FE` is never listed);
  - tracks × rows;
  - a mini map (notes per track, as dots);
  - how many songs use it, or `FUSION` (an unused pattern is dim).
- **ENV again** opens the highlighted pattern; when the EDITOR's own pattern holds anything, the warning
  asks first ([below](#the-editors-own-pattern)).
- **PLAY** plays the highlighted pattern on its own, looped from its start (wherever the list opens:
  the EDITOR, a song's order, a FUSION slot). Whatever played pauses and keeps its place. Moving the
  highlight moves the preview; PLAY again stops it; picking a pattern or HOME ends it, and PLAY then
  resumes what played before.
- **SEL menu:** NEW, CLONE, MOVE, DELETE. NEW makes an empty pattern of 8 tracks × 64 rows at the first
  free number; CLONE copies the highlighted one to it. MOVE floats the pattern to another free number
  ([../dialogs.md](../dialogs.md#move)). MOVE and DELETE ask about references
  ([../saving.md](../saving.md#references)). A built-in pattern (none in the firmware yet): NEW, CLONE, and BACK
  TO BUILT-IN once changed ([../saving.md](../saving.md#fws)), its number brown while the firmware's own.
- **HOME** closes the list.

## The EDITOR's own pattern

A pattern that belongs to the EDITOR alone: pattern `FE` ([../../files/patterns.md](../../files/patterns.md#numbers)),
in the firmware's range, empty in the firmware (8 tracks × 64 rows, EDIT STEP 1, as NEW makes one).
- **Never listed:** no pattern list shows it (ENV's, SONGS', a FUSION slot's, LOAD A PATTERN's, STORE IN A
  PATTERN's, LOOPS'); no song position or FUSION slot can name it; NEW, CLONE and MOVE never take `FE`.
- **Played and edited as any pattern** while the EDITOR is on it: PLAY, REC, RECORD PATTERN, GLO's
  properties (TRACKS, ROWS, EDIT STEP), the blocks, REVERT.
- **Kept by SAVE,** as a changed built-in is: a pattern record `FE` in the user area, with room kept for it
  ([../../files/store.md](../../files/store.md#the-room-for-the-editors-pattern)). Emptied, its record is
  deleted and the firmware's empty one is back. Switched off without a SAVE, its changes are gone, as every
  change is.
- **Empty** means no cell holds anything (note, instrument, volume or command), whatever its size.
- **The page for the computer** and disks leave it alone, as the keys' song `FE`; the editor page's backup
  keeps it, as it keeps every changed built-in.

**What the EDITOR edits** is one pattern at a time: its own, or a numbered one.
- On a fresh FM-1, and whenever the pattern it was on is gone, the EDITOR opens on its own pattern.
- **SAVE keeps which one,** in the settings record ([../../files/settings.md](../../files/settings.md)), as it
  keeps the current instrument: moving to another pattern is no change of its own; a SAVE writes it whenever
  it differs from the one kept.
- **It goes to a numbered pattern** by ENV's pattern list, SONGS' ENV list (`PATTERN 1A FOR THE EDITOR`),
  EDIT PATTERN on a song position, EDIT on PLAYBACK, a FUSION slot's PATTERN, and STORE IN A PATTERN.
- **It comes back to its own pattern** only by NEW PATTERN and LOAD A PATTERN.

**The cell's SEL menu** (`CELL`) has three items for it, after the block's items and before RECORD PATTERN and
REVERT:
- **NEW PATTERN:** the EDITOR's pattern emptied (8 × 64 again), and the EDITOR on it. No toast: the empty
  grid says it.
- **LOAD A PATTERN:** the pattern list, titled `LOAD INTO THE EDITOR`, without a `+ NEW PATTERN` line; the
  picked pattern is copied into the EDITOR's pattern, and the EDITOR on it (`PATTERN 1A LOADED`). The picked
  pattern is not changed.
- **STORE IN A PATTERN:** the pattern list, titled `STORE THE PATTERN IN`, `+ NEW PATTERN` first, the pattern
  the EDITOR is on left out; what the EDITOR shows, its unsaved changes included, is copied into the picked
  pattern (`+ NEW PATTERN`: the first free number; `NO FREE PATTERN` when there is none), and the EDITOR
  follows it there (`STORED IN PATTERN 1A`). Stored from its own pattern, the EDITOR's pattern is emptied;
  stored from a numbered pattern, that pattern stays as it is, its changes included.
- **Storing over a pattern that holds anything** asks first: `PATTERN 0C WILL BE REPLACED` /
  `DO YOU WANT TO CONTINUE?`, YES (OCT+), NO (OCT-). Songs that play it hear the new notes at once; REVERT on
  it brings it back until a SAVE.
- Each is an edit like any other: SAVE goes dim, REVERT takes it back. LOAD and STORE are a whole pattern's
  edit, at most ~30 KB of the edit memory; a full edit memory asks as for any edit
  ([../saving.md](../saving.md#the-edit-memory)).

**The warning.** Whenever the EDITOR's pattern holds anything and something would replace it (another pattern
opened in the EDITOR by any of the ways above, NEW PATTERN, LOAD A PATTERN), a question comes first:
`EDITOR CHANGES WILL BE LOST` / `DO YOU WANT TO CONTINUE?`, YES (OCT+), NO (OCT-).
- YES empties the EDITOR's pattern (an edit, saved by the next SAVE) and goes on; NO leaves everything as it
  was.
- So the EDITOR's pattern holds notes only while the EDITOR is on it. (After REVERT ALL brings back a saved one
  while the EDITOR is elsewhere, the next of these asks the same.)
- An empty one never asks.

**On its own pattern the EDITOR shows no number** where it names the pattern:
- the header `EDITOR` (`EDITOR 12` on a numbered one); GLO's title `PATTERN PROPERTIES`, without its USED BY
  line;
- the note editor's where line `ROW 14  TRACK 01`;
- REVERT's question `REVERT THE PATTERN TO SAVED?`;
- PLAYBACK, while the EDITOR's loop plays it: the header `EDITOR`, the footer `ROW 1F`
  ([playback.md](playback.md)).

## SEL: blocks

- **MARK,** the first item of the cell's menu, starts a block at the cursor: SELECT / ALGORITHM then
  stretch it over rows × tracks, and SEL opens its menu. As a shortcut, hold SEL and turn SELECT /
  ALGORITHM, like Shift and the arrow keys.
- **Tap SEL** for the menu (on the cursor's cell when nothing is marked, MARK first, then TAKE ITS
  INSTRUMENT: the pipette, below):
  - COPY, CUT, PASTE, PASTE MIX (into empty cells only), CLEAR;
  - MOVE, SWAP: the marked block floats with the cursor; SEL puts it down. MOVE leaves its old place
    empty; SWAP puts what was at the new place into the old one. HOME cancels;
  - **ROWS** (a group, [../dialogs.md](../dialogs.md#levels)): INSERT ROW, DELETE ROW;
  - **TRANSFORM** (a group): TRANSPOSE (with K1, by a semitone; SEL held: an octave; SEL ends it),
    INTERPOLATE: a sweep. On each track a command that the block's first and last rows both hold goes
    on every row between, its value stepping from one to the other; else the volumes step. INTERPOLATE
    needs two rows or more, so on one row TRANSPOSE shows in the group's place;
  - RECORD PATTERN (nothing marked): [below](#sel-record-pattern);
  - **REVERT** (nothing marked, while the pattern has something unsaved): the pattern shown back as
    saved, every cell, its tracks, rows and edit step, after a question (`REVERT PATTERN 0A TO SAVED?`,
    OCT+ `REVERT`, OCT- `KEEP`; then `REVERTED`); what plays it hears it so from the next row.
- **The clipboard** holds 2 KB of cells, packed as a pattern's tracks are (each track's stream with its end
  byte, [../../files/patterns.md](../../files/patterns.md)), and stays across patterns until power-off. A bigger
  block is neither copied nor cut (`BLOCK TOO BIG TO COPY`), and the clipboard is left empty.
- **HOME** drops the mark.
- **The pipette, TAKE ITS INSTRUMENT** (nothing marked; **ENV held** past the hold delay as a shortcut):
  the cursor's cell's instrument becomes the current one, the one the keys play and write and PRESETS
  turns from; on a cell naming none, the instrument its track plays at that row. The toast names it
  (`01:05 EL PIANO DX7`); with none there, `NO INSTRUMENT HERE` and the button blinks. ENV's tap stays
  the pattern list.

## SEL: RECORD PATTERN

The dialog of steps on the white keys ([../dialogs.md](../dialogs.md#record-pattern)), the EDITOR's alone
(LOOPS has a pattern recorder of its own: [loops.md](loops.md#the-pattern-recorder-arp)); here only from
the menu, since ARP is the row preview.
- **REC puts its notes into the cursor's track, from the cursor's row** (HOME cancels): `TO THE END`
  repeats the pattern down to the pattern's last row, `ONCE` writes its steps once (cut at the last
  row). The EDITOR's REC (writing on / off) stays as it was.
- **Nothing there is removed:** a step on writes the note at its row, the current instrument's, at KEYS
  VOLUME, keeping the cell's commands; a step off leaves its cell as it is. With the dialog's
  `RING TO THE NEXT` (the first) no note off is written; with `A STEP LONG` a note off goes a step after
  a note where the next step is off, on an empty cell only.
- **The step** is counted in the song's ROWS A BEAT; the where line reads `FROM ROW 3C OF 80` (the
  cursor's row of the pattern's rows). A step switched on sounds its note; the steps do not play
  along with the pattern. PLAY previews them on their own, at the song's tempo
  ([../dialogs.md](../dialogs.md#record-pattern)).
- The toast `PATTERN: 12 NOTES` says how many were written. They go in as one change: with the edit
  memory full nothing is written, the dialog stays and the memory's question asks
  ([../saving.md](../saving.md#the-edit-memory)).

## ARP: row preview

- **Tap:** the cursor's row repeats at the song's tempo, in a rhythm; tap again to stop. **Whatever
  plays stops first** (a song, the EDITOR's loop, the loops, the decks: paused, keeping its place), so
  the row is heard alone.
  - The rhythm is SETTINGS → ARP RHYTHM ([settings.md](settings.md)), the one INSTRUMENTS' and FX's
    note repeat use too; ARP held + SELECT changes it as a shortcut. A row a step, `#` played and `_`
    a rest, round and round:
    `#`, `#_`, `#__`, `#___` (the first), `##_`, `###_`, `##__`, `#_##_`, and two slow ones for long
    sounds, `#` and 7 or 15 rests. The header shows it as it changes (`RHYTHM #___`).
  - Moving the cursor moves the preview.
- **How long a note rings:**
  - to the end of its row, if the next row would end it (a new note or a note off on its track);
  - otherwise through the rest.
- **Jumps and pattern breaks** are ignored.

## Realtime takes

With REC on and nothing playing (with the EDITOR's loop playing, PLAY only pauses it, and the next PLAY
starts a take: [above](#writing-rec)). **A take loops:** from the cursor's row to the pattern's end, then round the
whole pattern again and again, recording on every pass, until PLAY or REC stops it:
- **Starting a take:**
  - **PLAY tap:** the take starts when PLAY is let go, after SETTINGS' COUNT-IN: 1 or 2 bars of 4 beats
    with a click of its own, or none (OFF) ([../transport.md](../transport.md#count-in)). With COUNT-IN
    **KEY** the take is armed instead: the first key pressed starts it, on the cursor's row; HOME, PLAY
    or REC cancel it.
  - **PLAY held + a key:** the take starts with that note, on the cursor's row, whatever COUNT-IN says.
    PLAY can then be let go. A chord pressed this way lands on that row, on the next tracks; so does any
    key in a take's first moments with no count-in, before its first row has sounded.
  - **PLAY held past the hold delay** shows an overlay with both ways: `LET GO: START THE TAKE`,
    `PRESS A KEY: START ON THAT NOTE`. A quick tap never shows it; a key starting the take closes it.
- **How a take records:**
  - **Notes land as SETTINGS' QUANTIZE says** ([settings.md](settings.md)), the values LOOPS' takes
    have ([loops.md](loops.md#quantize)): `2 ROWS`, `3 ROWS`, `4 ROWS`, the nearest line of that many
    rows from the pattern's start; `ROW` (at first), the nearest row; `REAL TIME`, the row it falls in,
    with a note delay `EDx` of the ticks since and its end as a key off `Kxx` at its tick, as LOOPS
    writes them ([../../files/loops.md](../../files/loops.md#recording-on-the-grid)).
  - **The keys sound exactly as they are written:** a note played before its grid line waits for it;
    one played just after the line sounds at once and is written on the line.
  - A note goes to the cursor's track, unless an earlier note is still held there; then it goes to
    the next track with no note held. So a melody stays on one track, and a chord spreads.
  - Releasing a key writes a note off (`REAL TIME`: the key off). A note off that would land on its own
    note's row goes to the next row.
  - Existing cells stay unless a note lands on them, those written on an earlier pass of the take
    too: each pass plays what the passes before wrote, and adds to it.
- **At the pattern's end** the take goes on from its first row, as the loop would.
- **PLAY or REC stops it** (so does leaving the EDITOR): REC stays on (back to step by step), and the
  cursor returns to the row the take started from, ready to hear it or to record again.
- **The edit memory full** ends a take there (`TAKE STOPPED`), what was written kept, and the memory's
  question asks ([../saving.md](../saving.md#the-edit-memory)).
- **REC blinks** during a take or its count-in, and the header shows ●.

**PLAY with REC off** loops the shown pattern from the cursor's row ([../transport.md](../transport.md)).

## Layout

The mockups are on the T grid (40 × 20 characters of 6 × 12 px); S, B and L text is placed where it
falls, so a mockup is close, not exact: the tables give the pixels. The body is y 17-226 (210 px),
between the header and the footer ([../frame.md](../frame.md)).

### The grid

The grid is the EDITOR's and PLAYBACK's ([playback.md](playback.md)). Its geometry by view:

| View | Track width (colW) | Track numbers' height | Row | Focus row | Rows a page |
|---|---|---|---|---|---|
| 8 tracks | 28 px | 12 px | 12 px | 16 px | 16 |
| 4 tracks | 56 px | 12 px | 16 px | 20 px | 12 |
| 2 tracks | 113 px | 14 px | 24 px | 24 px | 8 |
| 1 track | 226 px | 14 px | 24 px | 24 px | 8 |

Track *k* of the view starts at x = 14 + *k* × colW (the row numbers take x 0-11).

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| track numbers (8, 4 tracks) | 14 + *k* × colW + ⌊(colW − 12) / 2⌋, 17 | 12 × 12 | T | the track, `00`-`0F` |
| track numbers (2 tracks) | 14 + *k* × 113, 19 | 12 × 12 | T | the track |
| the slot's name (2 tracks) | 14 + *k* × 113 + 61 (`VOL`) or + 79 (`CMD1`-`CMD4`), 19 | 18 or 24 × 12 | T | what K1 picked (not on PLAYBACK) |
| the track (1 track) | 14, 19 | 12 × 12 | T | the track |
| its instrument (1 track) | 32, 19 | up to 60 × 12 | T | the track's instrument at the cursor's row, by name (`CLASSIC`) |
| more tracks left / right | 0, 17 / 234, 17 | 6 × 12 | T | `<` / `>` |
| a row | 0, from 17 + the track numbers' height, rows stacked | 240 × the row's height | | |
| its number | 0, the row's y + ⌊(h − 12) / 2⌋ | 12 × 12 | T | `00`-`7F` |
| the focus row's lines | 0, its y and y + h − 1 | 240 × 1 | | |
| the cursor's box | 14 + *k* × colW − 1, the focus row's y | colW × h | | |
| the playing row's band | 0, the row's y | 240 × h | | |
| a FUSION row a key plays | 0, the row's y | 240 × h | | (opened from a FUSION) |
| a marked block, a cell | 14 + *k* × colW, the row's y (+1 in the focus row) | (colW − 1) × h (− 2 in the focus row) | | |
| a carried block, a cell | 14 + *k* × colW, the row's y | (colW − 1) × h | | the carried cells, drawn over the grid |

**A cell, by view** (x is the track's left edge, y the row's top, h its height):

| View | Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|---|
| 8 | the note | x, y | 24 × h | S, cropped to its rows 2-13 in a 12-px row; whole in the focus row | `C-4`, `===`; `---` empty; `...` no note but a volume or commands |
| 8 | a missing instrument | x, y | 24 × h | | a deep red block behind the note |
| 4 | the note | x, y + ⌊(h − 12) / 2⌋ | 18 × 12 | T | `C-4`, `===`, `---` |
| 4 | the instrument | x + 24, same | 30 × 12 | T | `0A:0D`; `..:..` none (the track's instrument plays) |
| 4 | a missing instrument | x + 24, same | 30 × 12 | | a deep red block behind it |
| 2 | the note | x, y + 4 (focus row: x, y) | 24 × 16 (focus row: 36 × 24) | S (focus row: B) | `F-2`, `===`, `---` |
| 2 | the instrument | x + 28 (focus row: x + 40), y + 6 | 30 × 12 | T | `0A:0D`, `..:..` |
| 2 | the volume | x + 64 (+ 76), y + 6 | 12 × 12 | T | `68`; `..` none |
| 2 | the command | x + 82 (+ 94), y + 6 | 18 × 12 | T | the command in K1's slot (the first when K1 is on the volume); `...` none |
| 2 | the pips | the command's x + 5*i*, y + 20 | 3 × 2 each | | one for each command the cell holds |
| 2 | K1's field | the volume's or the command's x, y + 18 | 12 or 18 × 1 | | a line under the field K1 picked, in the cursor's cell |
| 1 | the note | x, y + 4 | 24 × 16 | S | `F-2`, `===`, `---` |
| 1 | the instrument | x + 28, y | 30 × 12 | T | `0A:0D`, `..:..` |
| 1 | the volume | x + 28, y + 12 | 12 × 12 | T | `68`, `..` |
| 1 | command *i* (0-3) | x + 64 + 84 × (*i* mod 2), y + 12 × ⌊*i* / 2⌋ | 78 × 12 | T | its 3 characters, a space and its short name ([Short names](#short-names-the-1-track-view)): `!08 TAPE STOP`; `...` none |

Colours: a note in its instrument's engine colour (the track's current instrument's when the cell
names none); `===` light grey; a missing instrument red, on deep red; empty cells, `..:..`, `..` and
`...` dark grey. Row numbers dark grey, beat rows cyan, a bar's first row light cyan (in a FUSION
pattern: the rows of the Cs cyan). Track numbers cyan, the cursor's track white, a muted track's dark
grey, its cells too. The volume brown; a command light blue, its sign in its group's colour (`*` the
engine's, `#` the effect's); a global command in a FUSION row all dark grey. In the 1-track view the
short names are light blue too, and the field K1 picked in the cursor's cell is all white, its dots
too: white against light blue stays plain in every theme, the one-colour ones included (white is
their brightest shade); a parameter's `*` takes the colour of the track's instrument at that row. The
pip of the command shown white, the others light blue. The cursor's box and K1's line white; the
focus row's lines dark grey. The playing row deep blue; a marked or carried block deep magenta; a FUSION row a key plays deep
green.

### 8 tracks

```
EDITOR 12 1/8                    > | |||   header: S, 16 px
   00  01  02  03  04  05  06  07          track numbers: T, 12 px
00--- --- C-4 --- --- G#3 D#4[F-2]         the focus row: 16 px, S whole
01--- --- --- --- --- --- --- ---          rows: 12 px, S cropped
02--- F-3 --- --- --- --- --- F-2
03--- F-3 --- --- C-4 --- --- F-3
04--- --- C-4 --- --- --- --- ...
05--- --- --- --- --- --- --- F-2
06--- F-3 --- --- D-4 --- --- G#2
07--- F-3 --- --- --- --- --- ---
08--- --- C-4 --- --- --- --- F-2
09--- --- --- --- --- --- --- ---
0A--- F-3 --- --- --- --- --- C-3
0B--- F-3 --- --- C#4 --- --- F-2
0C--- --- C-4 --- --- --- --- ---
0D--- --- --- --- --- --- --- G#3
0E--- F-3 --- --- C-4 --- --- F-2
0F--- F-3 --- --- --- --- --- ---
01:00 EL PIANO               ACC ACCENT    footer: T, 12 px
```

12 + 15 × 12 + 16 = 208 px of the body's 210. The header names the pattern (`EDITOR 12`, the
number white, its subpage; `EDITOR` on its own pattern) and the page of rows (`1/8`: 128 rows in pages of
16, counted from 1).

### 4 tracks

```
EDITOR 12 1/11                   > | |||
<        04       05       06       07
00--- ..:..G#3 09:23D#4 09:23F-2 0A:0D     the focus row: 20 px
01--- ..:..--- ..:..--- ..:..--- ..:..     rows: 16 px

02--- ..:..--- ..:..--- ..:..F-2 0A:0D
03C-4 10:01--- ..:..--- ..:..F-3 0A:0D

04--- ..:..--- ..:..--- ..:..--- ..:..
05--- ..:..--- ..:..--- ..:..F-2 0A:0D
   ...                                     12 rows a page
01:00 EL PIANO               ACC ACCENT
```

12 + 11 × 16 + 20 = 208 px. Here tracks 04-07 of 8 are shown, the cursor on the last: `<` says more
lie to the left.

### 2 tracks

```
EDITOR 12 1/16                   > | |||
< 06        VOL     07        VOL          track numbers, the slot: 14 px
00D#4   09:23 4C *60  F-2   0A:0D 68 ACC   the focus row, 24 px: B notes
                 .                  ...    the pips
01---   ..:.. .. ...  ---   ..:.. .. ...   rows: 24 px, S notes, T rest

02---   ..:.. .. ...  F-2   0A:0D 68 ...

03---   ..:.. .. ...  F-3   0A:0D 68 SLD
                                     .
04---   ..:.. .. ...  ---   ..:.. .. *5B
                                     .
   ...                                     8 rows a page
01:00 EL PIANO                VOLUME 68
```

14 + 8 × 24 = 206 px. In the focus row a cell takes 36 + 4 + 30 + 6 + 12 + 6 + 18 = 112 px of its
track's 113. With K1 on a command the slot's name reads `CMD1`-`CMD4` and the column shows that slot's
command; with K1 on the volume (as here) `VOL`, and the first command.

### 1 track

```
EDITOR 9A 2/3                    > | |||
< 01 CLASSIC                           >   the track, its instrument: 14 px
10C-2  0A:00 ACC ACCENT    *40 CUTOFF      a row: 24 px, two lines
       7F    ...           ...
11---  ..:.. *48 CUTOFF    ...
       ..    ...           ...
12C-3  ..:.. SLD SLIDE     *50 CUTOFF
       ..    ...           ...
13D#2  ..:.. ACC ACCENT    *58 CUTOFF
       ..    A04 VOL SLIDE ...
14G-2  ..:.. A+S ACC+SLIDE *60 CUTOFF      the focus row, boxed
       60    !08 TAPE STOP #1F FILT CUT    K1 on CMD3: drawn white
   ...                                     8 rows a page
0A:00 CLASSIC       !08 TAPE STOP 8 ROWS
```

14 + 8 × 24 = 206 px. A cell takes 24 + 4 + 30 + 6 + 78 + 6 + 78 = 226 px, the track's whole width:
the note, the instrument over the volume, then CMD1 and CMD2 on the first line, CMD3 and CMD4 on the
second. Here a TB-303 line (track 01, its instrument `CLASSIC`), its accents, slides and a cutoff
sweep, a punch-in and an FX knob on row 14. Colours as the grid's (above).

### The note editor

```
EDITOR 9A 2/3                    > | |||
ROW 14  TRACK 01  PATTERN 9A               where: T, cyan (white: SEL on cells)
NOTE   G-2                                 a line: 24 px, the name T, the parts S
INSTR  0A:00 CLASSIC
VOLUME 60                          76 %
CMD 1  ACCENT + SLIDE
CMD 2  CUTOFF                        60
-----------------------------------------  the cursor's line: lines above, below
CMD 3  TAPE STOP                 8 ROWS    the part at the cursor white
-----------------------------------------
CMD 4  + COMMAND
K1 STEP  K2 BY 16  SEL CELLS               the knobs: T
0A:00 CLASSIC       !08 TAPE STOP 8 ROWS
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the body | 0, 17 | 240 × 210 | | black, over the grid |
| where | 4, 18 | | T | `ROW 14  TRACK 01  PATTERN 9A`; on the EDITOR's own pattern `ROW 14  TRACK 01`; in a FUSION pattern the row's note after the row (`ROW 3C C-4  TRACK ...`) |
| line *i* (0-6) | 0, 33 + 24*i* | 240 × 24 | | NOTE, INSTR, VOLUME, CMD 1-CMD 4 |
| its name | 4, the line's y + 6 | | T | `NOTE`, `INSTR`, `VOLUME`, `CMD 1`-`CMD 4` |
| its first part | 52, the line's y + 4 | up to 184 × 16 (23 characters) | S | the note (`G-2`, `===`, `---`); the instrument (`0A:00 CLASSIC`, `AS THE TRACK`); the volume (`60`, `AS THE INSTRUMENT`); what a command is (`TAPE STOP`, `CUTOFF`, `FILTER CUT`, `FX ON`); `NO COMMAND`; `+ COMMAND` on the first empty line, nothing below it |
| its value | right-aligned to 236, the line's y + 4 | | S | a command's value (`8 ROWS`, `60`, `LP 1.5K`, `REVERB`; a tracker effect's digits, `04`); the volume as a share (`76 %`) |
| the cursor's line | 0, its y and y + 23 | 240 × 1 each | | a line above and below |
| the knobs | 0, 203 | 240 × 12 | T | `K1 STEP  K2 OCTAVE  SEL CELLS` (K2: `OCTAVE`, `CARTRIDGE`, `BY 16`, `NEXT GROUP`); on cells `SELECT ROWS  ALG TRACKS  SEL FIELDS` |

Colours: where, cyan, white while SEL has SELECT / ALGORITHM on the cells; the lines' names cyan, the
cursor's white; the note and the instrument in the instrument's engine colour (a note off light grey);
what the cell does not hold (`---`, `AS THE TRACK`, `AS THE INSTRUMENT`, `+ COMMAND`) dark grey; the
volume brown; the commands light blue; the part at the cursor white; the cursor's line's lines dark
grey; the knobs' names yellow, their words light grey.

### Recording

- **The count-in, and an armed take:** the count-in's box over the grid
  ([../transport.md](../transport.md#the-count-ins-box)). REC blinks.
- **A take:** the grid as above; the page and the rows shown follow the playing row round the pattern,
  and the cursor sits on the row a key pressed now lands on. The header shows ● ([../frame.md](../frame.md)).
- **Writing while the loop plays:** the grid as stopped, the cursor where the keys write; the playing row
  on its deep blue band wherever it is, the view not following it.
- **PLAY held past the hold delay** (REC on): a panel at 4, 167, 232 × 40:

```
+--------------------------------------+
| LET GO: START THE TAKE               |   T at 10, 173
| PRESS A KEY: START ON THAT NOTE      |   T at 10, 189
+--------------------------------------+
```

Colours: `COUNT-IN` red; in the PLAY panel the keys (`LET GO`, `PRESS A KEY`) yellow, the rest light
grey; panels black with a light grey frame ([../dialogs.md](../dialogs.md)).

### ARP held

A hold layer ([../dialogs.md](../dialogs.md#hold-layers)) of one line: a panel at 4, 187, 232 × 38;
`HOLD ARP +` at 8, 192; `SELECT: THE RHYTHM, # A ROW, _ A REST` at 8, 206.

### GLO: the pattern's properties

```
+--------------------------------------+   a panel at 6, 21, 228 x 80
| PATTERN 12 PROPERTIES                |   the title, T at 10, 24
|[TRACKS                              8]|   items, T, every 14 px from 40
| ROWS                              128|
| EDIT STEP                           1|
| USED BY                        1 SONG|
+--------------------------------------+
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 6, 21 | 228 × 80 | | |
| the title | 10, 24 | | T | `PATTERN 12 PROPERTIES`; `PATTERN PROPERTIES` on the EDITOR's own pattern, whose panel ends after EDIT STEP (no USED BY) |
| item *i* | 12, 40 + 14*i* | | T | its name |
| its value | right-aligned to 228, same | | T | `8`, `128`, `1`, `1 SONG` / `3 SONGS` (`, FUSION` added when a FUSION plays it) |
| the selected item | 8, its y − 2 | 224 × 14 | | a box |

Colours: the title cyan; names cyan, the selected one's white; values light grey, the selected one's
white; USED BY's value dark grey (it cannot change).

A change that drops cells asks first, a question ([../dialogs.md](../dialogs.md#questions)):
`THIS DROPS 12 CELLS` / `MAKE THE PATTERN SMALLER?`, OCT+ `YES`, OCT- `NO`.

### ENV: the pattern list

```
+--------------------------------------+   a panel at 2, 19, 236 x 206
| PATTERNS                             |
| NR  SIZE   NOTES BY TRACK     SONGS  |   T at 8, 35
| 0B  08X128 ........           1      |   lines: T, every 12 px from 49
| 0C  08X128 ........           1      |
|[12  08X128 ........           1     ]|   the highlight
| 13  08X128 ........           1      |
| 2A  04X064 ....               FUSION |
| 30  08X064 ........           -      |   unused: dim
+--------------------------------------+
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 2, 19 | 236 × 206 | | |
| the title | 6, 22 | | T | `PATTERNS` |
| the column heads | 8, 35 | | T | `NR  SIZE   NOTES BY TRACK     SONGS` |
| line *i* (14 shown) | from y 49, every 12 px | | | the list scrolls to keep the highlight 7 lines from the top |
| the number | 8 | | T | `00`-`FD` |
| the size | 32 | | T | tracks × rows: `08X128` |
| the map | 80 + 6*t*, the line's y + 4 | 4 × 4 a track | | a dot for each track the pattern has |
| the songs | 186 | | T | how many songs use it; `FUSION`; `-` none |
| the highlight | 4, the line's y − 1 | 232 × 12 | | a box |

Colours: the title cyan, the heads dark grey; the number light grey (the highlighted one's white), the
size cyan, the songs light grey, `FUSION` light magenta; a dot by its track's notes: none dark grey,
under an eighth of the rows light grey, under a third white, more yellow; an unused pattern (no song,
no FUSION) all dark grey. The highlight white, yellow while the pattern floats (MOVE).

**Its SEL menu** ([../dialogs.md](../dialogs.md#menus)): titled `PATTERN 12`: NEW, CLONE, MOVE,
DELETE; on a built-in pattern (none in the firmware yet) NEW, CLONE and, once changed, BACK TO BUILT-IN. **MOVE** floats the highlighted pattern: SELECT takes it to the next free number either way,
the list showing it under the number it would take, boxed yellow; SEL puts it down, after the question
about references; HOME takes it back.

### SEL: a block

- **Marked:** its cells on a deep magenta ground in the grid; SEL lit.
- **The menu** ([../dialogs.md](../dialogs.md#menus)): a panel at 84, 21, 150 wide, titled
  `BLOCK 4 X 1` (rows × tracks) or `CELL`; `CELL`'s starts with MARK:

```
             +-----------------------+
             | BLOCK 4 X 1           |
             | TAKE ITS INSTRUMENT   |   `CELL` only, after MARK
             |[COPY                 ]|
             | CUT                   |
             | PASTE                 |
             | PASTE MIX             |
             | CLEAR                 |
             | MOVE                  |
             | SWAP                  |
             | ROWS                 >|   INSERT ROW, DELETE ROW
             | TRANSFORM            >|   TRANSPOSE, INTERPOLATE
             | NEW PATTERN           |   `CELL` only
             | LOAD A PATTERN        |   `CELL` only
             | STORE IN A PATTERN    |   `CELL` only
             | RECORD PATTERN        |   `CELL` only
             | REVERT                |   `CELL` only, while the pattern has something unsaved
             +-----------------------+
```

`CELL`'s menu, with MARK and TAKE ITS INSTRUMENT, has up to 16 items: 15 show, and it scrolls
([../dialogs.md](../dialogs.md#menus)).

The groups open in the same panel ([../dialogs.md](../dialogs.md#levels)): `BLOCK 4 X 1 > ROWS` with
BACK, INSERT ROW, DELETE ROW; `BLOCK 4 X 1 > TRANSFORM` with BACK, TRANSPOSE, INTERPOLATE.

- **MOVE, SWAP:** the block floats at the cursor, its cells drawn there on deep magenta over the grid;
  SELECT and ALGORITHM carry it, SEL puts it down, HOME cancels.
- **TRANSPOSE:** the block stays marked; K1 transposes, the footer showing `TRANSPOSE +02`; SEL ends
  it.

## Texts

### The header and the grid

| Text | When |
|---|---|
| `EDITOR 12` | the screen's name and the pattern shown, in the MAKE group's colour; `EDITOR` alone on its own pattern |
| `1/8` | the page of rows, N/M ([../frame.md](../frame.md)); hidden with one page; during a take, the playing row's page |
| `00`-`0F` | the track numbers |
| `<`, `>` | more tracks to the left, to the right |
| `VOL`, `CMD1`-`CMD4` | over a track's column in the 2-track view: K1's slot |
| `---` | no note |
| `...` | (8 tracks) a cell with no note but a volume or commands; (2 tracks) no command |
| `===` | a note off |
| `..:..` | no instrument: the track's plays |
| `..` | no volume: the instrument's default |
| `COUNT-IN`, `TAKE ARMED` | the count-in's box ([../transport.md](../transport.md#the-count-ins-box)) |
| `TAKE CANCELLED` | toast: an armed take cancelled |

### The footer's right side

| Text | When |
|---|---|
| `VOLUME 68` | K1 on the volume; `VOLUME ..` (dim) when the cell has none |
| `A04 VOLUME -4` | K1 on a command: its 3 characters, then its words |
| `CMD 2 EMPTY` | K1 on a slot with no command (dim) |

### Knob readouts

Shown in the header's place while a knob turns, in white, for 1.2 s ([../frame.md](../frame.md#the-header)).

| Text | When |
|---|---|
| `K1 VOLUME` | K1 onto the volume |
| `K1 CMD 1 ACCENT` | K1 onto a command: its slot and words |
| `K1 CMD 2 EMPTY` | K1 onto the slot past the last command |
| `K3, K4 THE VOLUME` | K2 turned while K1 is on the volume |
| `VOLUME 68` | K3 / K4 on the volume |
| `VOLUME .. AS THE INSTRUMENT` | the volume turned past `7F` |
| `K2 ACC ACCENT` | K2: the command's 3 characters and its name (the list below) |
| `K2 NO COMMAND` | K2 back to no command |
| `K3 K2 PICKS A COMMAND` | K3 / K4 on an empty slot (`K4 K2 PICKS A COMMAND` for K4) |
| `K3 K4 SETS IT` | K3 on a one-digit command |
| `A04 VOLUME -4` | K3 / K4 on a command's value: its 3 characters and its words |
| `K3 CUTOFF` | K3 in a group: the parameter, mixer value or FX knob picked |
| `ACC NO VALUE` | K3 / K4 on a command without a value |
| `TRANSPOSE +02` | K1 in TRANSPOSE |
| `RHYTHM ##_` | ARP held + SELECT |

### Commands: the grid, K2 and the note editor

K2 turns through these entries in this order. *Grid* is how the grid shows the command (x, y its
digits); *Name* what K2 shows; *Words* what the footer says (the note editor shows them in its two
parts, what the command is and its value).

| Grid | Name | Words |
|---|---|---|
| `...` | NO COMMAND | `NO COMMAND` |
| `0xy` | ARPEGGIO | `ARPEGGIO 0 X Y` |
| `1xx` | SLIDE UP | `SLIDE UP XX` |
| `2xx` | SLIDE DOWN | `SLIDE DOWN XX` |
| `3xx` | SLIDE TO NOTE | `SLIDE TO NOTE XX` |
| `4xy` | VIBRATO | `VIBRATO X Y` |
| `5xy` | SLIDE TO NOTE + VOL | `TO NOTE, VOL +X` (the slide below) |
| `6xy` | VIBRATO + VOL | `VIBRATO, VOL +X` |
| `7xy` | TREMOLO | `TREMOLO X Y` |
| `8xx` | PAN | `PAN MIDDLE` (`80`), `PAN LEFT XX`, `PAN RIGHT XX` (from the middle) |
| `9xx` | SAMPLE OFFSET | `START AT XX00` |
| `Axy` | VOLUME SLIDE | `VOLUME +X`, `VOLUME -Y`, `VOLUME FINE +X` (`AxF`), `VOLUME FINE -Y` (`AFy`) |
| `Bxx` | JUMP | `JUMP TO POS XX` |
| `Cxx` | VOLUME | `VOLUME XX` (at most `40`) |
| `Dxx` | BREAK | `BREAK TO ROW XX` |
| `E1x` | FINE SLIDE UP | `FINE SLIDE UP X` |
| `E2x` | FINE SLIDE DOWN | `FINE SLIDE DOWN X` |
| `E3x` | GLISSANDO | `GLISSANDO ON`, `GLISSANDO OFF` |
| `E4x` | VIBRATO WAVE | `VIBRATO WAVE SINE` (`SAW`, `SQUARE`, `RANDOM`) |
| `E5x` | FINETUNE | `FINETUNE +3` (x − 8, signed) |
| `E6x` | LOOP | `LOOP FROM HERE` (`E60`), `LOOP 3 TIMES` |
| `E7x` | TREMOLO WAVE | `TREMOLO WAVE SINE` |
| `E9x` | RETRIGGER | `RETRIGGER EVERY 3` |
| `EAx` | FINE VOLUME UP | `FINE VOLUME UP X` |
| `EBx` | FINE VOLUME DOWN | `FINE VOLUME DOWN X` |
| `ECx` | NOTE CUT | `NOTE CUT AT TICK X` |
| `EDx` | NOTE DELAY | `NOTE DELAY 3 TICKS` |
| `EEx` | PATTERN DELAY | `ROW DELAY 3 ROWS` |
| `Fxx` | TICKS / TEMPO | `THE SONG TEMPO` (`F00`), `TICKS A ROW 6` (under `20`), `TEMPO 7D` |
| `Gxx` | GLOBAL VOLUME | `GLOBAL VOLUME XX` (at most `40`) |
| `Hxy` | GLOBAL VOL SLIDE | `GLOBAL VOL +X` |
| `Kxx` | KEY OFF | `NOTE OFF AT TICK XX` |
| `Pxy` | PAN SLIDE | `PAN RIGHT X`, `PAN LEFT Y` |
| `Rxy` | MULTI RETRIGGER | `RETRIGGER 3, VOL X` |
| `Txy` | TREMOR | `TREMOR ON 2 OFF 3` |
| `Uxy` | FINE VIBRATO | `FINE VIBRATO X Y` |
| `X1x` | EXTRA FINE UP | `EXTRA FINE UP X` |
| `X2x` | EXTRA FINE DOWN | `EXTRA FINE DOWN X` |
| `Yxy` | PANBRELLO | `PANBRELLO X Y` |
| `~-x` | VOL SLIDE DOWN | `VOL SLIDE DOWN X` |
| `~+x` | VOL SLIDE UP | `VOL SLIDE UP X` |
| `~Dx` | FINE VOL DOWN | `FINE VOL DOWN X` |
| `~Ux` | FINE VOL UP | `FINE VOL UP X` |
| `~Sx` | VIBRATO SPEED | `VIBRATO SPEED X` |
| `~Vx` | VIBRATO DEPTH | `VIBRATO DEPTH X` |
| `~Px` | PAN | `PAN XX` (x × 17) |
| `~<x` | PAN SLIDE LEFT | `PAN SLIDE LEFT X` |
| `~>x` | PAN SLIDE RIGHT | `PAN SLIDE RIGHT X` |
| `~Mx` | SLIDE TO NOTE | `SLIDE TO NOTE X` |
| `ACC` | ACCENT | `ACCENT` |
| `SLD` | SLIDE | `SLIDE` |
| `A+S` | ACCENT + SLIDE | `ACCENT + SLIDE` |
| `LEG` | LEGATO | `LEGATO` |
| `!xx` | LOOP 1/4 ... VINYL STOP (the order and names as the punch-ins' table) | `TAPE STOP 8 ROWS`: the punch-in and its rows, a count, in decimal |
| `*xx` | PARAMETER | the parameter's name and value: `CUTOFF 60`, `OP2 LEVEL 50`, `LFO WAVE SINE`, `ARP TABLE 06 MAJOR`, `VIB DEPTH 40 CENTS` (the tables per engine: [../../files/commands.md](../../files/commands.md), CHIP's [there](../../files/commands.md#chip)); `PARAMETER 4A 10` for a code the engine does not have |
| `=xx` | MIXER | `LEVEL 7F`, `CHORUS SEND 20`, `DELAY SEND 20`, `REVERB SEND 20`, `DUCK AMOUNT 40` |
| `#xx` | FX | the effect, its knob and the value: `FILTER CUT LP 1.3K`; `REVERB ON`, `REVERB OFF` |

A volume slide's words: `+X` up, `-Y` down; `FINE +X` (y is F) and `FINE -Y` (x is F) once a row.

### Short names: the 1-track view

The 1-track view names each command in at most 9 characters, its value being in its 3 characters:
its *Name* above where that fits; else, and for the groups:

| Command | Short name |
|---|---|
| tracker effects | `SLIDE DN`, `TO NOTE` (`3xx`, `~Mx`), `NOTE+VOL`, `VIB+VOL`, `OFFSET`, `VOL SLIDE`, `FINE UP`, `FINE DN`, `VIB WAVE`, `TREM WAVE`, `FINE VOL+` (`EAx`, `~Ux`), `FINE VOL-` (`EBx`, `~Dx`), `NOTE DLY`, `ROW DELAY`, `GLOB VOL`, `GLOB SLD`, `RETRIG+V`, `FINE VIB`, `XFINE UP`, `XFINE DN`, `VOL DOWN`, `VOL UP`, `VIB SPEED`, `VIB DEPTH`, `PAN LEFT`, `PAN RIGHT` |
| `Fxx` | `SONG BPM` (`F00`), `TICKS` (under `20`), `TEMPO` |
| PT/FM's own | `ACC+SLIDE`; the punch-ins `STUTTER`, `HALF SPD`, `LP SWEEP`, `HP SWEEP`, `ECHO THRW`, `REV SWELL`, `VINYL STP` (the others whole) |
| `*xx` | the parameter's name: `CUTOFF`, `OP2 LEVEL`; `OP1 CRS`, `OP1 DTUNE` (COARSE, DETUNE), `OSC SYNC`, `LFO SYNC`, `PITCH MOD`, `PWM SRC`, `ENV POL`, `LOOP STRT`; CHIP's `BUZ SHAPE`, `BUZ STEPS`, `NSE MODE`, `NSE RATE`, `SWP SHIFT`, `SWP PACE`, `PERIOD HI`, `PERIOD LO`, `PIT TABLE`, `DTY TABLE`, `NSE TABLE`, `PIT SPEED`, `DTY SPEED`, `NSE SPEED`; `PARAM 4A` for a code the engine does not have |
| `=xx` | `LEVEL`, `CHO SEND`, `DLY SEND`, `REV SEND`, `DUCK` |
| `#xx` | the effect and its knob: `FILT CUT`, `DLY FDBK`, `REV PRE`; the effects `FILT`, `DRV`, `LOFI`, `CHO`, `DLY`, `REV`, `LIM`, `METRO`, the rest whole; the knobs `DPTH`, `SMTH`, `PATT`, `FDBK`, `PING`, `PRE`, `CEIL`, `REL`, `SRC`, the rest whole; ON / OFF as `REV ON`, `METRO OFF` |

### Toasts

In the header, for 2 s ([../frame.md](../frame.md#the-header)).

| Toast | When |
|---|---|
| `8 TRACKS`, `4 TRACKS`, `2 TRACKS`, `1 TRACK` | SEQ |
| `01:05 EL PIANO DX7` | the pipette: the instrument taken (its ID, name, engine) |
| `NO INSTRUMENT HERE` | the pipette on a cell and track with none |
| `REC ON: KEYS WRITE` | REC on (the EDITOR's loop playing on, if it plays) |
| `REC OFF: KEYS PLAY` | REC off |
| `SONG PAUSED: WRITING` | REC on while a song plays |
| `LOOP STOPPED: PLAY FOR A TAKE` | PLAY with REC on while the EDITOR's loop plays |
| `TAKE STOPPED` | PLAY or REC during a take, or leaving the EDITOR |
| `ROW PREVIEW #___`, `ROW PREVIEW OFF` | ARP tap: on with its rhythm, off |
| `COPIED 4 X 1` | COPY: rows × tracks |
| `NOTHING COPIED` | PASTE with an empty clipboard |
| `BLOCK TOO BIG TO COPY` | COPY or CUT of a block over the clipboard's 2 KB |
| `MOVE: SEL PUTS IT DOWN`, `SWAP: SEL PUTS IT DOWN` | MOVE, SWAP |
| `CANCELLED` | HOME while a block is carried |
| `K1: TRANSPOSE, SEL: DONE` | TRANSPOSE |
| `TRANSPOSED +02` | SEL ending TRANSPOSE |
| `REVERTED` | REVERT, after its question |
| `ALL 240 PATTERNS USED` | NEW or CLONE in the list with no free number (`00`-`EF`) |
| `PATTERN 13 MADE` | NEW in the list |
| `12 CLONED TO 13` | CLONE in the list |
| `PATTERN 12 DELETED` | DELETE in the list |
| `NO FREE NUMBER` | MOVE in the list with no free number |
| `SELECT MOVES IT, SEL PUTS IT DOWN` | MOVE in the list |
| `NOT MOVED` | HOME while a pattern floats |
| `PATTERN 12 MOVED TO 20` | a pattern put down |
| `REFERENCES UPDATED, SAVED` | after YES to the question about references |
| `PATTERN 1A LOADED` | LOAD A PATTERN |
| `STORED IN PATTERN 1A` | STORE IN A PATTERN |
| `NO FREE PATTERN` | STORE IN A PATTERN's `+ NEW PATTERN` with no free number (`00`-`EF`) |
| `SAVE CHANGES TO FREE MEMORY` (`SAVE` yellow) | an edit grows the edit memory, and under 25 % of it is free ([../saving.md](../saving.md#the-edit-memory)) |

### Menus, questions, overlays

| What | Text |
|---|---|
| the block's menu | `BLOCK 4 X 1` or `CELL`: MARK and TAKE ITS INSTRUMENT (`CELL` only), COPY, CUT, PASTE, PASTE MIX, CLEAR, MOVE, SWAP, ROWS (INSERT ROW, DELETE ROW), TRANSFORM (TRANSPOSE, INTERPOLATE), NEW PATTERN, LOAD A PATTERN, STORE IN A PATTERN, RECORD PATTERN and REVERT (`CELL` only); in a group, `BACK` first and the title `BLOCK 4 X 1 > ROWS` |
| REVERT's question | `REVERT PATTERN 0A TO SAVED?` (`REVERT THE PATTERN TO SAVED?` on the EDITOR's own), OCT+ `REVERT`, OCT- `KEEP` |
| LOAD A PATTERN, STORE IN A PATTERN | the pattern list titled `LOAD INTO THE EDITOR`, `STORE THE PATTERN IN` (`+ NEW PATTERN` first) |
| storing over a pattern with anything in it | `PATTERN 0C WILL BE REPLACED` / `DO YOU WANT TO CONTINUE?`, OCT+ `YES`, OCT- `NO` |
| the warning | `EDITOR CHANGES WILL BE LOST` / `DO YOU WANT TO CONTINUE?`, OCT+ `YES`, OCT- `NO` |
| RECORD PATTERN | the dialog's ([../dialogs.md](../dialogs.md#record-pattern)): `RECORD PATTERN INTO TRACK 03`, `K4 WRITE TO THE END` / `ONCE`, `FROM ROW 3C OF 80`; the toast `PATTERN: 12 NOTES` (`1 NOTE`) |
| the pattern list | `PATTERNS`; `NR  SIZE   NOTES BY TRACK     SONGS` |
| the list's menu | `PATTERN 12`: NEW, CLONE, MOVE, DELETE; a built-in one NEW, CLONE, BACK TO BUILT-IN ([../saving.md](../saving.md#texts)) |
| the references question | `PATTERN 12 IS USED 3 TIMES` / `UPDATE REFERENCES?`, OCT+ `YES`, OCT- `NO`, `HOME: CANCEL`; YES shows `UPDATING 3 REFERENCES` with a progress bar ([../saving.md](../saving.md#references)) |
| the properties | `PATTERN 12 PROPERTIES`: `TRACKS`, `ROWS`, `EDIT STEP`, `USED BY` (`1 SONG`, `3 SONGS`, `, FUSION`); `PATTERN PROPERTIES` on the EDITOR's own, without `USED BY` |
| the shrink question | `THIS DROPS 12 CELLS` (`1 CELL`) / `MAKE THE PATTERN SMALLER?`, OCT+ `YES`, OCT- `NO` |
| PLAY held, REC on | `LET GO: START THE TAKE` / `PRESS A KEY: START ON THAT NOTE` |
| ARP held | `HOLD ARP +` / `SELECT: THE RHYTHM, # A ROW, _ A REST` |
| the note editor | `ROW 14  TRACK 01  PATTERN 9A` (`ROW 14  TRACK 01` on the EDITOR's own); `NOTE`, `INSTR`, `VOLUME`, `CMD 1`-`CMD 4`; `AS THE TRACK`, `AS THE INSTRUMENT`, `76 %`; `NO COMMAND`, `+ COMMAND`; `FX ON`, `FX OFF`; `8 ROWS`, `1 ROW`; `K1 STEP`, `K2 OCTAVE` (`CARTRIDGE`, `BY 16`, `NEXT GROUP`), `SEL CELLS`; `SELECT ROWS`, `ALG TRACKS`, `SEL FIELDS`; the toasts `SELECT, ALG: CELLS`, `SELECT, ALG: FIELDS` |

## Help

HOME + LFO ([../help.md](../help.md)). The EDITOR's own pages, then the shared ones. The help draws
the first page's last line, `PRESS HOME TO GET BACK`, itself; `{...}` and `[...]` mark keys and
screens as [../help.md](../help.md) says. The commands' pages name each command as the grid shows it.

```
[EDITOR]: WRITE PATTERNS
PATTERN: TRACKS ACROSS, ROWS DOWN
  A CELL: A NOTE, ITS SOUND, VOLUME
  AND UP TO 4 COMMANDS
{SELECT}: UP / DOWN THE ROWS
{ALGORITHM}: ACROSS THE TRACKS
{SEQ} (SHOW ELSE, QUICK): SEE 8, 4, 2 OR 1
  TRACKS AT ONCE
{REC}: WRITE WITH THE {KEYS}, ON / OFF
{PRESETS}: THE SOUND THE {KEYS} WRITE
{PLAY}: LOOP IT FROM THE CURSOR'S ROW
{ENV} (EXPLORE NAMED VAULTS): PATTERNS
  {ENV} AGAIN OPENS THE ONE PICKED
  HOLD {ENV}: TAKE THE CURSOR'S SOUND
{GLO}: TRACKS, ROWS, EDIT STEP
```

```
WHICH PATTERN YOU EDIT
HEADER [EDITOR], NO NUMBER: THE EDITOR'S
  OWN PATTERN; NO SONG CAN PLAY IT
  {SAVE} KEEPS IT AFTER SWITCHING OFF
{SEL}, STORE IN A PATTERN: GIVE IT A
  NUMBER, SO SONGS CAN PLAY IT
{SEL}, NEW PATTERN: START AN EMPTY ONE
{SEL}, LOAD A PATTERN: START FROM A
  COPY OF A NUMBERED ONE
ANOTHER PATTERN OPENED: IT IS EMPTIED
  (A QUESTION ASKS FIRST)
HEADER [EDITOR] 12: PATTERN 12; YOUR
  EDITS GO STRAIGHT INTO IT
PATTERN LIST, {ENV}: {PLAY} HEARS ONE,
  {SEL}: NEW, CLONE, MOVE, DELETE
```

```
WRITING NOTES
{REC} OFF: THE {KEYS} ONLY PLAY SOUNDS
{REC} ON: {KEYS} WRITE AT THE CURSOR
  WITH THE SOUND {PRESETS} PICKED
  THEN THE CURSOR MOVES DOWN
  A SONG PAUSES; THE LOOP PLAYS ON
{K1}: THE VOLUME, OR COMMAND 1 TO 4
  THE GRID SHOWS ONE COMMAND AT A TIME
{K2}: THE COMMAND, LIKE SLIDE OR ACCENT
{K3}, {K4}: ITS TWO DIGITS, OR VOLUME
{LFO} (LEAVE FIELD OPEN): CLEAR THE CELL
  ON AN EMPTY CELL: A NOTE OFF, ===
{SEL}, RECORD PATTERN: STEPS ON THE
  {WHITE KEYS}, ITS NOTES DOWN THE TRACK
{SEL}, REVERT: THE PATTERN AS SAVED
{EDIT}: THE NOTE EDITOR, NEXT PAGE
FOOTER RIGHT: THE COMMAND, IN WORDS
```

```
THE NOTE EDITOR: {EDIT} ON A CELL
ON SCREEN: THE CELL, A FIELD A LINE
  NOTE, INSTR (ITS SOUND), VOLUME,
  CMD 1 TO 4
{SELECT}: A LINE UP / DOWN
{ALGORITHM}: A COMMAND, OR ITS VALUE
{K1}: CHANGE THE PART AT THE CURSOR
{K2}: THE SAME, IN BIG STEPS
  AN OCTAVE, A CARTRIDGE, 16, OR THE
  NEXT GROUP OF COMMANDS
{SEL}: {SELECT}, {ALGORITHM} MOVE TO
  OTHER CELLS, {SEL} AGAIN: BACK
{KEYS}: CHANGE THE NOTE
{LFO}: CLEAR THE CELL
FOOTER: THE FIELD, IN WORDS
{EDIT}: DONE, {HOME}: DROP THE CHANGES
```

```
RECORDING LIVE: WITH {REC} ON
{PLAY}: RECORDING STARTS AS YOU LET GO
HOLD {PLAY} + {KEY}: START ON THAT NOTE
  HOLD {PLAY} ALONE: SHOWS HOW
COUNT-IN FIRST: SET IN [SETTINGS]
  KEY: YOUR FIRST NOTE STARTS IT
FROM THE CURSOR: ROUND AND ROUND THE
  PATTERN, ADDING EACH PASS
NOTES: ON QUANTIZE'S GRID, IN [SETTINGS]
LET GO OF A {KEY}: A NOTE OFF IS WRITTEN
CHORDS: SPREAD TO THE NEXT TRACKS
{PLAY} OR {REC}: STOP RECORDING
AFTER: BACK TO THE ROW IT STARTED ON

{REC} ON WHILE THE LOOP PLAYS: IT PLAYS
  ON, {KEYS} WRITE AT THE CURSOR
  {PLAY}: STOP, THEN {PLAY} FOR A TAKE
```

```
BLOCKS: SEVERAL CELLS AT ONCE
{SEL}, MARK: {SELECT}, {ALGORITHM} MARK
  ROWS AND TRACKS FROM THE CURSOR
  OR HOLD {SEL} + {SELECT}, {ALGORITHM}
{SEL} AGAIN: MENU FOR THE BLOCK
  COPY, CUT, PASTE, CLEAR
  PASTE MIX: INTO EMPTY CELLS ONLY
  MOVE, SWAP: CARRY IT, {SEL} DROPS IT
  ROWS: INSERT ROW, DELETE ROW
  TRANSFORM: TRANSPOSE ({K1}), INTERPOLATE
  INTERPOLATE: A SWEEP, FIRST TO LAST
CLIPBOARD: KEPT UNTIL POWER OFF
LISTEN TO A ROW
{ARP} (AGAIN, REPEAT, PLEASE): REPEAT ROW
  AT THE TEMPO, WHAT PLAYS STOPS
THE RHYTHM: [SETTINGS] (HOLD {ARP} + {SELECT})
  ##_ = PLAY TWO ROWS, REST ONE
```

```
COMMANDS: UP TO 4 IN A CELL
ORDER: THEY ACT AS WRITTEN, 1 TO 4
NUMBERS: HEX, 0-9 THEN A-F, FROM 00
X, Y: A DIGIT 0-F, XX: TWO DIGITS
0XY: ARPEGGIO, NOTE +X, +Y SEMITONES
1XX, 2XX: PITCH SLIDES UP, DOWN
3XX: SLIDE TO THE NOTE, AT SPEED XX
4XY: VIBRATO, SPEED X, DEPTH Y
5XY: SLIDE TO NOTE AND VOLUME SLIDE
6XY: VIBRATO AND VOLUME SLIDE
7XY: TREMOLO, SPEED X, DEPTH Y
8XX: PAN, 00 LEFT, 80 MIDDLE, FF RIGHT
9XX: START THE SAMPLE XX00 IN
AXY: VOLUME UP BY X, OR DOWN BY Y
BXX: JUMP TO A SONG POSITION
CXX: SET THE VOLUME, 00 TO 40
DXX: ON TO THE NEXT POSITION, ROW XX
```

```
COMMANDS E TO R
E1X, E2X: FINE SLIDE UP, DOWN, ONCE
E3X: GLISSANDO, SLIDES IN SEMITONES
E4X, E7X: VIBRATO, TREMOLO WAVE
E5X: FINETUNE THE NOTE
E6X: LOOP, E60 MARKS, E6X: X TIMES
E9X: RETRIGGER EVERY X TICKS
EAX, EBX: FINE VOLUME UP, DOWN
ECX: CUT THE NOTE AT TICK X
EDX: DELAY THE NOTE BY X TICKS
EEX: HOLD THE ROW X ROWS LONGER
FXX: TICKS A ROW, UNDER 20, OR TEMPO
  F00: BACK TO THE SONG TEMPO
GXX, HXY: GLOBAL VOLUME, ITS SLIDE
KXX: NOTE OFF AT TICK XX
PXY: PAN SLIDE, RIGHT X, LEFT Y
RXY: RETRIGGER EVERY Y TICKS, VOLUME X
```

```
COMMANDS T TO Y, ~
TXY: TREMOR, ON X TICKS, OFF Y TICKS
UXY: FINE VIBRATO
X1X, X2X: EXTRA FINE SLIDE UP, DOWN
YXY: PANBRELLO, PAN SWINGS, X SPEED

XM VOLUME COLUMN, AFTER ~
~-X, ~+X: VOLUME DOWN, UP EACH TICK
~DX, ~UX: VOLUME DOWN, UP, ONCE
~SX, ~VX: VIBRATO SPEED, DEPTH
~PX: PAN, ~<X, ~>X: PAN SLIDES
~MX: SLIDE TO THE NOTE
```

```
PT/FM'S OWN COMMANDS
ACC: ACCENT, SLD: SLIDE TO THE NEXT
  A+S: BOTH (FOR A TB-303)
LEG: LEGATO, ON A NOTE: THE TRACK'S
  HELD NOTE MOVES TO IT, NO NEW ATTACK
  SAME INSTRUMENT ONLY, ELSE A NEW NOTE
  A TB-303: SLD ON THE NOTE BEFORE
  FUSION: ALWAYS A NEW NOTE
!XX: PUNCH-IN, ITS KEY HELD XX ROWS
```

```
COMMANDS THAT TURN KNOBS
*: THE INSTRUMENT, AS A KNOB TURNED
  {K3} PICKS WHICH, {K4} SETS IT
  LIKE CUTOFF, OR OP2 LEVEL ON A DX7
  IT STAYS UNTIL CHANGED
  ANOTHER INSTRUMENT ON THE TRACK: GONE
=: THE TRACK IN THE MIXER
  LEVEL, A SEND OR THE DUCK AMOUNT
#: THE FX, ANY KNOB, OR ON / OFF
  UNTIL THE NEXT SONG STARTS
WHAT YOU ARE EDITING: SKIPPED
A SWEEP: ONE ON EVERY ROW
  {SEL}, INTERPOLATE FILLS THE ROWS
{EDIT}: SAYS EACH IN WORDS
B, D, E6, EE, F, G, H, # AND !: GLOBAL
  THEY WORK FROM ANY TRACK
  IN FUSION ROWS: IGNORED, SHOWN DIM
```

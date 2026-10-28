# Overlays, menus and dialogs

Status: **settled** (2026-10-09), revised 2026-10-24. The records: [the UI proposal](../proposals/202610072229-ui.md),
sections 1.5 and 1.7, and the shared lists of 6.2 and 6.3; [the loop station proposal](../proposals/202610240236-loops-station.md)
(RECORD PATTERN the EDITOR's alone, LOOPS' pattern lists).

Everything that opens over a screen and is the same on every screen: menus (SEL), MOVE, questions,
RECORD PATTERN (the EDITOR's), properties (GLO), HOME's combination, the hold layers' overlays, the pattern and cartridge lists (ENV),
text entry for names, the progress bar, and the shared toasts. A screen's own menus, lists and dialogs are in its
file ([screens/](README.md#the-files)); the help's overlay is in [help.md](help.md); where toasts show
is in [frame.md](frame.md#the-header).

## Overlays

- **An overlay** covers part of the screen: a list, a menu or a set of properties.
  - SELECT moves through it.
  - The key that opened it confirms the highlighted item: ENV in the pattern list opens that pattern.
  - HOME cancels.
  - The button that opened it is lit while it is open ([leds.md](leds.md)).
- **Overlays stack:** a menu opened from a list sits over the list; HOME closes the top one.
- **A change of screen closes them,** except the question that the full edit memory asks
  ([saving.md](saving.md#the-edit-memory)), which stays until it is answered, and HOME's combination
  while SELECT turns the screens ([below](#homes-combination)).
- **The progress bar** takes no input: every control waits until it has gone.

## Menus

- **SEL** opens the actions menu for the marked block, or for the item at the cursor.
- **MARK** heads the menu where blocks or slots can be marked (the EDITOR's cells, INSTRUMENTS' slots)
  and nothing is marked yet: after it, SELECT and ALGORITHM stretch a block from the cursor (the toast
  `SELECT, ALG: MARK  SEL: ITS MENU`), and SEL opens the menu for the block. HOME drops the mark. SEL
  held with SELECT / ALGORITHM marks the same way, as a shortcut.
- **The last action used is preselected,** so repeating it is SEL, SEL. Each menu remembers its own.
- **SELECT** moves the highlight; it stops at both ends.
- **SEL** (the key that opened it) does the highlighted action; HOME closes the menu and does nothing.
- **Items that cannot act are left out,** not shown dim: REVERT is listed only when what it takes back
  has a change not saved ([saving.md](saving.md#revert)), and EDIT PATTERN only on a FUSION instrument.
- **At most 15 items show;** a longer menu scrolls, keeping the highlight in the middle while it can.

### Levels

Added 2026-10-10, so the menus' items can be grouped.

- **A group** is an item that holds other items, marked `>` at the panel's right edge. The rarer
  actions go in groups; the frequent ones stay at the top.
- **SEL on a group** shows its items in the same panel: `BACK` first, then the group's items. The title
  becomes the menu's title and the group's name, `CELL > ROWS`; the group's name alone when that is
  longer than 23 characters.
- **The highlight** starts on the group's last-used item, else on its first after BACK.
- **BACK** (SEL on it) shows the level above, with the group highlighted, and the menu reopens there
  next time, on the group, not inside it. **HOME** closes the whole menu, at any level.
- **BACK looks unlike the actions:** `<` before it and the word in cyan, a dark grey rule under it.
- **One level only:** a group holds actions, never another group.
- **The last action used** reopens the menu at its level, highlighted, if it can act there, so
  repeating it stays SEL, SEL from a group too; BACK leads to the rest.
- **Groups follow the items' rule:** a group with nothing that can act is left out; a group with one
  item that can act shows that item in its place, without the group.
- **15 items** show at most at either level, BACK counted.

### Layout

```
EDITOR 12 1/8                          |   header (frame.md)
----------------------------------------
              +-----------------------+
              |BLOCK 4 X 1            |    the title, T
              |[COPY                 ]|    the highlight
              | CUT                   |
              | PASTE                 |
              | PASTE MIX             |
              | CLEAR                 |
              | MOVE                  |
              | SWAP                  |
              | ROWS                 >|    a group
              | TRANSFORM            >|
              +-----------------------+
----------------------------------------
01:00 EL PIANO                SLD SLIDE    footer (frame.md)
```

A group opened, in the same place:

```
              +-----------------------+
              |BLOCK 4 X 1 > ROWS     |
              | < BACK                |    `<` and BACK cyan
              | --------------------- |    a dark grey rule
              |[INSERT ROW           ]|
              | DELETE ROW            |
              +-----------------------+
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 84, 21 | 150 × (min(items, 15) × 12 + 22) | | a black box with a 1-px outline, at the body's right |
| the title | 88, 24 | | T | what the menu acts on (`BLOCK 4 X 1`, `SLOT 01:05`, `PATTERN 1A`); in a group, ` > ` and the group's name after it |
| item *i* (of those shown) | 90, 39 + 12 × *i* | | T | the action; in a group, `BACK` first |
| BACK | `<` at 90, `BACK` at 102, 39 | | T | the first line in a group |
| BACK's rule | 88, 49 | 142 × 1 | | under BACK, unless it is highlighted |
| a group's mark | 222, 39 + 12 × *i* | | T | `>` |
| the highlight | 86, 38 + 12 × *i* | 146 × 12 | | a bar under the highlighted item, outlined |

The first item shown is `max(0, min(sel − 7, items − 15))`.

Colours: the panel's outline light grey; the title cyan; items and groups' marks light grey; `<` and
`BACK` cyan, its rule dark grey; the highlighted item white on a deep blue bar with a white outline.

## MOVE

**MOVE (and SWAP), in every SEL menu, never asks for a number:** the item floats where it is, boxed
yellow (SEL lit).
- **SELECT carries it,** and the list shows it in its new place and what that would change.
- **SEL puts it down;** a used item asks about its references first
  ([saving.md](saving.md#references)). **HOME** takes it back (`NOT MOVED`).
- **Where the places are fixed numbers that other things refer to** (instrument slots, pattern and
  cartridge numbers), nothing is pushed aside: MOVE goes only to empty slots or free numbers, SWAP to
  any slot. SELECT skips the numbers in use.
- **Cartridge and pattern numbers `F0`-`FF` are the built-in instruments'**
  ([../files/instruments.md](../files/instruments.md#built-in-instruments)): no MOVE goes there, and no
  NEW or CLONE takes one. "The first free number" is always one of `00`-`EF`.
- **Songs and the FX chain are orders:** there the others make room.
- **Nothing built in floats:** MOVE is left out of a built-in thing's menu, and a thing afloat is never
  put down on a built-in number.
- **The EDITOR's blocks** float with the cursor the same way ([screens/editor.md](screens/editor.md)).
- **When it starts,** the toast says how: `SELECT MOVES IT, SEL PUTS IT DOWN`.

In the pattern and cartridge lists ([below](#lists)), an item afloat is listed under the number it
would take, in order, and its line is boxed yellow instead of white. How a screen draws its own float
(SONGS' list, INSTRUMENTS' slots, the FX chain) is in its file.

## Questions

- **All yes or no,** the answers in their own words (`REVERT` / `KEEP`, `FILL` / `EMPTY`).
- **OCT+** answers yes and **OCT-** no, whatever started the question. The two answers sit over their
  buttons' names, no on the left and yes on the right, as the buttons sit.
- **HOME** cancels: nothing happens.
- **Nothing else acts while a question shows.**

### Layout

```
REVERT TO SAVED?   (a toast)           |
----------------------------------------


 +-------------------------------------+
 |          REVERT TO SAVED?           |   the first line, T
 |       DROP 3 UNSAVED CHANGES        |
 |        AND RELOAD FROM FLASH        |
 |                                     |
 |         OCT-              OCT+      |   the buttons, T
 |         KEEP             REVERT     |   the answers, S, 16 px
 |                                     |
 |            HOME: CANCEL             |
 +-------------------------------------+



----------------------------------------
01:00 EL PIANO DX7
```

The panel is centred in the body; with *n* lines its height is *h* = 12 × *n* + 62 and its top
*y* = 17 + floor((210 − *h*) / 2).

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 8, *y* | 224 × *h* | | a black box with a 1-px outline |
| line *i* | centred in 8-231, *y* + 8 + 12 × *i* | | T | the question |
| the no button | centred in 8-119, *y* + 12 + 12 × *n* | | T | `OCT-` |
| the yes button | centred in 120-231, same | | T | `OCT+` |
| the no answer | centred in 8-119, *y* + 25 + 12 × *n* | | S | its word (`KEEP`) |
| the yes answer | centred in 120-231, same | | S | its word (`REVERT`) |
| the cancel line | centred in 8-231, *y* + *h* − 16 | | T | `HOME: CANCEL` |

Colours: the outline light grey; the first line white, the others light grey; the buttons' names and
`HOME` yellow; the answers white; `: CANCEL` light grey.

### A question with a checklist

A question can ask which of several things to do (CLEAN UP: [screens/settings.md](screens/settings.md)).
- **SELECT** picks a line; **K1** ticks it (clockwise) or clears it. A line with nothing in it stays
  clear, dim. **Up to 8 lines show;** more scroll with the line picked.
- **OCT+** answers yes with the lines ticked; it is refused (OCT+ blinks) with none ticked, unless the
  question's yes stands alone (SONGS' DELETE: the song, its patterns ticked to go too,
  [screens/songs.md](screens/songs.md#deletes-question)). **OCT-** no; **HOME** cancels.
- **The first line** may follow the ticks (`CLEAN UP: FREES 6.9K`); a second one, light grey, may name
  what is asked about, and a dark-grey line under the ticks may add a note; each makes the panel a line
  taller. With one line to tick, the hints leave `SELECT: PICK` out.

```
 +-------------------------------------+
 |        CLEAN UP: FREES 1.3K         |   the first line, T
 |[# UNUSED PATTERNS          1  1.3K ]|   the line picked, boxed; # ticked
 |  . UNUSED SAMPLES              NONE |   nothing in it: dim
 |  . UNUSED INSTRUMENTS     99  5.5K  |
 |                                     |
 |         OCT-              OCT+      |
 |         KEEP            CLEAN UP    |   S, 16 px
 |                                     |
 | SELECT: PICK  K1: TICK  HOME: CANCEL|
 +-------------------------------------+
```

With *n* lines shown (at most 8) its height is *h* = 13 × *n* + 94, its top *y* = 17 + floor((210 − *h*) / 2).

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 8, *y* | 224 × *h* | | a black box with a 1-px outline |
| the first line | centred in 8-231, *y* + 8 | | T | the question |
| line *i* | *y* + 24 + 13 × *i* | | | |
| its box | 16, line y + 1 | 9 × 9 | | an outline; ticked: filled inside (18, line y + 3, 5 × 5) |
| its name | 32, line y | | T | `UNUSED PATTERNS` |
| its note | right-aligned to 220, line y | | T | how many, and what they take (`1  1.3K`), or `NONE` |
| the line picked | 12, line y − 1 | 216 × 13 | | a box |
| the buttons, the answers | as a question's, from *y* + 30 + 13 × *n* | | T, S | |
| the keys' line | centred in 8-231, *y* + *h* − 16 | | T | `SELECT: PICK  K1: TICK  HOME: CANCEL` |

Colours: as a question's; the lines light grey (the picked one white), a line with nothing in it dark
grey; the boxes as their line, the tick white; the picked line's box white; `SELECT`, `K1` and `HOME`
yellow.

## RECORD PATTERN

Steps on the white keys, written as if they had been played: the EDITOR's SEL menu
([screens/editor.md](screens/editor.md#sel-record-pattern)). LOOPS has a pattern recorder of its own
([screens/loops.md](screens/loops.md#the-pattern-recorder-arp)). REC writes it, HOME cancels; OCT- / OCT+
stay free for the octave.
- **The steps:** up to 16, one a white key (key 1 the first); a tap switches a step on or off, and a
  step switched on sounds its note. Keys past the size do nothing.
- **K1, the note:** one for the whole pattern, the current instrument's; a semitone a click, SEL held an
  octave; heard as it turns. The first time: the cursor's note, else C-4.
- **SEQ, the keys:** the steps (always at first, since K1 sets the note too) or the note: then every
  key, white and black, sets the note at the keyboard's octave, heard, and SEQ goes back to the steps
  (toasts `KEYS: THE NOTE`, `KEYS: THE STEPS`).
- **OCT- / OCT+, the octave:** the keyboard's octave as on every screen (refused at its ends), and the
  note an octave with it, heard; the toast `NOTE C-5`.
- **K2, the step:** a step's length, those the grid makes whole rows of: `1/32`, `1/16T`, `1/16`,
  `1/8T`, `1/8`, `1/4` (at 4 rows a beat 1/16, 1/8, 1/4; at 6 1/16T, 1/8T, 1/8, 1/4; at 8 1/32, 1/16,
  1/8, 1/4); with none a row long, `A ROW` comes first. 1/16 at first.
- **K3, the size:** 1-16 steps; 16 at first.
- **K4, how it is written,** a click switching: round and round (`TO THE END`) or `ONCE`, the size's
  steps once.
- **SELECT, the notes' length:** `RING TO THE NEXT` (the first: no note offs, each note rings until
  its track's next, so a pad holds through the rests) or `A STEP LONG` (a note off a step after each
  note where the next step is off, on an empty cell only: nothing is removed); clockwise a step,
  anticlockwise ring on.
- **REC (RECORD)** writes it ([screens/editor.md](screens/editor.md#sel-record-pattern)) and closes;
  with no step on in the size it is refused (`NO STEPS ON`; REC blinks). **HOME (CANCEL)** closes it,
  nothing written.
- **What is set stays** for the next time until power-off; it is not saved.
- **PLAY previews it:** the steps heard on their own, from the first, round and round the size, at the
  song's tempo, each change heard at once; the step playing boxed and its key turned over. Whatever
  played pauses and keeps its place, as with the pattern list's preview. PLAY again stops it; REC and
  HOME end it too. The other buttons and knobs do nothing. REC is dim while it is open; SEQ dim, full
  while the keys set the note; PLAY green dim, full while the preview plays.

### Layout

```
EDITOR 12 4/8                          |
30C-4 --- --- A-2 --- --- --- ---          (the screen goes on under it)
 +-------------------------------------+
 |    RECORD PATTERN INTO TRACK 03     |   the title, T
 | ## .. .. ## .. .. ## .. .. .. ## .. |   the steps in fours; on: filled
 | K1 NOTE  D-5  SYN PIANO             |
 | K2 STEP  1/16                       |
 | K3 SIZE  16 STEPS                   |
 | K4 WRITE TO THE END                 |
 | SELECT NOTES RING TO THE NEXT       |
 | FROM ROW 3C OF 80                   |   where it starts
 | PLAY: HEAR THE STEPS ON THEIR OWN   |
 |                                     |
 |         HOME              REC       |   the buttons, T
 |        CANCEL            RECORD     |   the answers, S, 16 px
 |                                     |
 | KEYS: STEPS  SEQ: NOTE  OCT: OCTAVE |
 +-------------------------------------+
----------------------------------------
09:00 SYN PIANO              CMD 1 EMPTY
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 8, 32 | 224 × 180 | | a black box with a 1-px outline, centred in the body |
| the title | centred in 8-231, 38 | | T | `RECORD PATTERN INTO TRACK 03` |
| step *k* | 13 + 13 × *k* + 2 × floor(*k* / 4), 54 | 11 × 14 | | filled: on, the sound's engine colour; off, deep grey. Past the size: an outline, deep grey |
| the step playing | one pixel around its step | 13 × 16 | | a white box (the preview's) |
| line *i*, K1-K4 | 14, 76 + 12 × *i* | | T | `K1 NOTE  D-5  SYN PIANO`, `K2 STEP  1/16`, `K3 SIZE  16 STEPS`, `K4 WRITE TO THE END` |
| the length | 14, 124 | | T | `SELECT NOTES RING TO THE NEXT`, `SELECT NOTES A STEP LONG` |
| where | 14, 136 | | T | `FROM ROW 3C OF 80`: the cursor's row of the pattern's rows |
| PLAY | 14, 148 | | T | `PLAY: HEAR THE STEPS ON THEIR OWN`; while the preview plays `PLAY: STOP HEARING THEM` |
| the buttons, the answers | as a question's, from 164 (no on the left, yes on the right) | | T, S | `HOME` `CANCEL`, `REC` `RECORD` |
| the keys' line | centred in 8-231, 196 | | T | `KEYS: STEPS  SEQ: NOTE  OCT: OCTAVE`; after SEQ `KEYS: NOTE  SEQ: STEPS  OCT: OCTAVE` |

Colours: the outline light grey; the title white; `K1`-`K4`, `SELECT`, `PLAY`, `KEYS`, `SEQ` and `OCT` yellow, the lines' names
and the rest light grey, their values white, the instrument in its engine's colour; the buttons and
answers as a question's.

**The keys' LEDs:** a step in the size full while on; off, dark, but steps 1, 5, 9 and 13 dim, where
each block of four starts (as FX LIVE's punch-ins); past the size dark; the step playing turned over
(on: dark, off: full). While the keys set the note: its key full (when the
keyboard's octave has it), the rest dark.

## Properties

- **GLO** opens the current container's properties: the pattern, song, cartridge, the loops, FX chain
  or view.
- **SELECT** picks a property and **K1** changes it, at once; SEL held + K1 takes coarse steps.
- **GLO or HOME** closes the overlay; there is no cancel: a change stays, and REVERT
  ([saving.md](saving.md#revert)) takes the thing back to what was saved.
- **A name** opens text entry with EDIT ([below](#text-entry)); EDIT is then its DONE.
- **Some lines only inform** (what uses a pattern, a cartridge's type): K1 does nothing there, and
  their values are dim.

### Layout

```
SONGS                                  |
----------------------------------------
+--------------------------------------+
|SONG 00 PROPERTIES                    |   the title, T
|[NAME                       PUNCH IN ]|   the selected property
| TEMPO                 7C (124 BPM)   |
| TICKS A ROW                      6   |
| ROWS A BEAT                      4   |
| SWING                         50 %   |
| AT THE END                    STOP   |
| LOOP FROM              POSITION 00   |
+--------------------------------------+
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 6, 21 | 228 × (14 × properties + 24) | | a black box with a 1-px outline |
| the title | 10, 24 | | T | `SONG 00 PROPERTIES`, `PATTERN 1A PROPERTIES`, ... |
| property *i*'s name | 12, 40 + 14 × *i* | | T | its name |
| property *i*'s value | right-aligned to 228, same | | T | its value |
| the selection | 8, 38 + 14 × *i* | 224 × 14 | | an outline |

Colours: the outline light grey; the title cyan; names cyan and values light grey, both white on the
selected line; values that only inform dark grey; a value may carry its own colour where the screen
gives it one.

## HOME's combination

A HOME tap at the top of a screen opens HOME's combination
([controls.md](controls.md#homes-combination)) as an overlay at the body's bottom: what can act, a line
each, and `HOME: CLOSE`. It stays through the change of screen that SELECT makes; a black key, PLAY,
REC, LFO, HOME, and any control that is no member close it. **Once a member has acted, only its lines
are left,** with `HOME: CLOSE`.

| Line | Shows | Left after |
|---|---|---|
| `SELECT: THE SCREENS, ONE BY ONE` | always | SELECT |
| `OP1: SONGS         OP2: EDITOR` … `POLY: LOOPS` | always: each screen's black key, as printed, two a line in the keys' order (the key yellow, the screen light grey, the current one white) | |
| `WHITE KEY: MUTE A TRACK, HELD: SOLO` | the white keys mute | a mute or solo |
| `WHITE KEY: SOLO A TRACK` | after SEQ: the white keys solo | a solo |
| `WHITE KEY: MUTE A LOOP, HELD: SOLO`, `WHITE KEY: SOLO A LOOP` | in LOOPS, instead of the two above (the loops mute and solo as tracks do: [screens/loops.md](screens/loops.md#control-mode-mutes-and-solos)) | a mute or solo |
| `KEYS 1-8: LEFT DECK, 9-16: RIGHT` | on DJ, after the white keys' line: whose tracks they are | a mute or solo |
| `OCT+: TRACKS 08-0F` / `OCT-: TRACKS 00-07` | on DJ, where a deck's song has tracks that way | a mute or solo |
| `SEQ: WHITE KEYS SOLO INSTEAD` / `SEQ: WHITE KEYS MUTE INSTEAD` | always | a mute or solo |
| `PLAY: BACK TO THE START` | always; on DJ `PLAY: BACK TO THE CUES` | |
| `REC: CUT, SILENT WHILE HELD` | always ([controls.md](controls.md#cut)) | |
| `LFO: HELP FOR THIS SCREEN` | always | |
| `HOME: CLOSE` | always, last | |

### Layout

```
EDITOR 00 1/8                          |
----------------------------------------
00C-4 --- --- A-2 --- --- --- ---          (the screen goes on under it)
+--------------------------------------+
|HOME +                                |   the heading, T
|SELECT: THE SCREENS, ONE BY ONE       |
|OP1: SONGS         OP2: EDITOR        |   the current screen white
|OP3: INSTRUMENTS   OP4: PLAYBACK      |
|OP5: DJ            PIT: VISUAL        |
|GLO: STATUS        MONO: SETTINGS     |
|POLY: LOOPS                           |
|WHITE KEY: MUTE A TRACK, HELD: SOLO   |
|SEQ: WHITE KEYS SOLO INSTEAD          |
|PLAY: BACK TO THE START               |
|REC: CUT, SILENT WHILE HELD           |
|LFO: HELP FOR THIS SCREEN             |
|HOME: CLOSE                           |
+--------------------------------------+
----------------------------------------
01:00 EL PIANO               CMD 1 EMPTY
```

The panel is a hold layer's ([below](#hold-layers)): with *n* lines, *h* = 12 × *n* + 26 and
*y* = 225 − *h*; the heading `HOME +` at 8, *y* + 5, line *i* at 8, *y* + 19 + 12 × *i*.

Colours: as a hold layer's: the outline light grey; `HOME` yellow and `+` cyan; in each line the control
(up to the colon) yellow, the rest light grey.

## Hold layers

A button held alone past the hold delay shows its layer: what the keys and knobs do with it
([controls.md](controls.md#buttons-tap-hold-and-layers)). It is not an overlay: it takes no input,
stays through a change of screen, and is drawn over everything else in the body until its button is
let go.

| Button | Its lines |
|---|---|
| HOME | its combination's, as tapped open ([above](#homes-combination)), without `HOME: CLOSE` |
| FX | the heading `HOLD FX + KEYS`; then the 16 punch-ins by key as a table of two columns, two a line (a group of four on two lines), by their full names: each key's number right-aligned in 2 characters and yellow, a space, the name light grey (the one running white); the second column 19 characters in, so a line is 37 characters at most (T is the smallest font): ` 1 LOOP 1/4          2 LOOP 1/8` … ` 9 LOW-PASS SWEEP   10 HIGH-PASS SWEEP` … `15 TAPE STOP        16 VINYL STOP` (below) |
| SEL | only what applies where you are: in a name being typed, `SELECT IN A NAME: NEXT GROUP` alone; else the screen's own lines, then `PRESETS: OTHER CARTRIDGE` (every screen but DJ). The screens' own: SONGS, editing the order, `K1: 8 PATTERNS AT A TIME`; the EDITOR's grid `SELECT, ALGORITHM: MARK A BLOCK` and `K4: BY 16` (`K1: AN OCTAVE` while TRANSPOSE turns; none in the note editor); INSTRUMENTS browsing `SELECT, ALGORITHM: MARK SLOTS`, on the edit page `K1-K4: BIG STEPS`; FX `K1-K4: BIG STEPS`, on EDIT then `OCT: THE OCTAVE` (on the MIXER `K1: BY 16`); LOOPS `K1-K4: BIG STEPS`; DJ `K1, K4: THE TEMPO BY 1 BPM`, `K2, K3: THE FILTERS BY 10` (`K2, K3: THE VOLUMES BY 6 DB` while ENV has them on the volumes); PLAYBACK, VISUAL, STATUS and SETTINGS none |
| SAVE | `OCT+: SAVE`, `OCT-: REVERT ALL` |
| LFO | `HOME: HELP FOR THIS SCREEN`; on DJ then its LFO pads by key, as FX's punch-ins (the names white while on: an LFO running, the reverse, a deck's KEY not 0): `1 FADER\|2 SHAPE\|3 RANGE\|4 REVERSE`, `5 FILTER L\|6 SHAPE\|7 RANGE\|8 KEY`, `9 FILTER R\|10 SHAPE\|11 RANGE\|12 KEY`, `13 FX\|14 SHAPE\|15 RANGE\|16 ALL OFF`, `1 5 9 13: ON / OFF; + ALG: SPEED`, `8 12 HELD + K1 / K4: THE KEY` (the keys and knobs yellow); let go there, the pads are the LFO pads ([screens/dj.md](screens/dj.md#the-lfo-pads)) |
| ARP | the arpeggiator's, on every screen but two: `SELECT: THE RHYTHM, # A NOTE, _ A REST`, `ALGORITHM: THE PATTERN`, `K1: HOW MANY NOTES LATCH`, `LFO: LET THE LATCHED NOTES GO` ([controls.md](controls.md)); the EDITOR's own, the row preview's rhythm ([screens/editor.md](screens/editor.md)); in LOOPS none |

Buttons with no lines on a screen show no layer.

### Layout

```
STATUS DEVICE                          |
----------------------------------------
BATTERY                       2/3 BARS
FIRMWARE          0.1-202610072229 DEV
OUTPUT                          STEREO     (the screen goes on under it)
+--------------------------------------+
|HOLD HOME +                           |   the heading, T
|SELECT: THE SCREENS, ONE BY ONE       |
|OP1: SONGS         OP2: EDITOR        |
|OP3: INSTRUMENTS   OP4: PLAYBACK      |
|OP5: DJ            PIT: VISUAL        |
|GLO: STATUS        MONO: SETTINGS     |   the current screen white
|POLY: LOOPS                           |
|WHITE KEY: MUTE A TRACK, HELD: SOLO   |
|SEQ: WHITE KEYS SOLO INSTEAD          |
|PLAY: BACK TO THE START               |
|REC: CUT, SILENT WHILE HELD           |
|LFO: HELP FOR THIS SCREEN             |
+--------------------------------------+
----------------------------------------
01:00 EL PIANO DX7           FREE 237K
```

The panel sits at the body's bottom: with *n* lines, *h* = 12 × *n* + 26 and *y* = 225 − *h*.

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 4, *y* | 232 × *h* | | a black box with a 1-px outline |
| the heading | 8, *y* + 5 | | T | `HOLD`, the button's name, `+` |
| line *i* | 8, *y* + 19 + 12 × *i* | | T | a control, `:`, what it does |

Colours: the outline light grey; `HOLD` and `+` cyan, the button's name yellow; in each line the
control (up to the colon) yellow, the rest light grey.

FX's layer, the punch-ins as a table (8 lines, *h* = 122, *y* = 103; the keys' numbers at 8 and 122,
right-aligned in 2 characters, their names at 26 and 140):

```
+--------------------------------------+
|HOLD FX + KEYS                        |
| 1 LOOP 1/4          2 LOOP 1/8       |   the one running white
| 3 LOOP 1/16         4 LOOP 1/32      |
| 5 STUTTER 1/16T     6 BIT CRUSH      |
| 7 GATE 1/16         8 GATE 1/32      |
| 9 LOW-PASS SWEEP   10 HIGH-PASS SWEEP|
|11 ECHO THROW       12 REVERB SWELL   |
|13 REVERSE          14 HALF SPEED     |
|15 TAPE STOP        16 VINYL STOP     |
+--------------------------------------+
```

## Lists

### The pattern list

ENV opens it on the EDITOR, on a song's position and on a FUSION instrument's PATTERN; LOOPS' SEL
menu opens it for SAVE TO PATTERN, titled `SAVE THE LOOPS TO` with `NEW` first and picked (the first free
number), and for LOAD FROM PATTERN, titled `LOAD INTO LOOPS`
([screens/loops.md](screens/loops.md#storing-in-a-pattern-and-loading-one)); SONGS' NEW opens it for the new song's first pattern, with
`+ NEW PATTERN` as its first line, white ([screens/songs.md](screens/songs.md)); the EDITOR's LOAD A
PATTERN (`LOAD INTO THE EDITOR`) and STORE IN A PATTERN (`STORE THE PATTERN IN`, `+ NEW PATTERN` first, the
pattern the EDITOR is on left out) open it too ([screens/editor.md](screens/editor.md#the-editors-own-pattern)).
(GATE's list of gate patterns is the FX screen's own: [screens/fx.md](screens/fx.md).)

- **The EDITOR's own pattern `FE` is never listed** ([../files/patterns.md](../files/patterns.md#numbers)).

- **Each line:**
  - the number, `00`-`FF`;
  - tracks × rows (`08X128`);
  - a mini map: a square for each track, by how many notes it holds;
  - how many songs use it, or `FUSION` for a FUSION instrument's pattern (an unused pattern is dim).
- **The key that opened it** (ENV; SEL in LOOPS) picks the highlighted pattern.
- **PLAY** plays the highlighted pattern on its own, looped from its start (wherever the list opens:
  the EDITOR, a song's order, a FUSION slot), at the current song's timing; in LOOPS' `LOAD INTO LOOPS`,
  at the timing LOAD would bring, the pattern's song's
  ([screens/loops.md](screens/loops.md#storing-in-a-pattern-and-loading-one)). Whatever
  played pauses and keeps its place. Moving the highlight moves the preview; PLAY again stops it; picking
  a pattern or HOME ends it, and PLAY then resumes what played before. **ARP** does what PLAY does (a shortcut
  the hint does not name), and is lit while the preview plays. The list's last line says it: `PLAY: HEAR IT`,
  `PLAY: STOP HEARING IT` while it plays.
- **For a FUSION slot,** the keys play the highlighted pattern as the FUSION instrument would.
- **SEL menu** (title `PATTERN 1A`): NEW, CLONE, MOVE, DELETE.
  - NEW makes an empty pattern of 8 tracks × 64 rows at the first free number; CLONE copies the
    highlighted one there. The list goes to the new one.
  - MOVE floats the pattern to another free number ([MOVE](#move)).
  - MOVE and DELETE ask about references ([saving.md](saving.md#references)).
  - On a built-in pattern (`F0`-`FD`; the firmware has none yet): NEW, CLONE, and once it was changed BACK TO
    BUILT-IN ([saving.md](saving.md#fws)).

```
EDITOR 12 1/8                          |
----------------------------------------
+--------------------------------------+
|PATTERNS                              |   the title, T
| NR  SIZE   NOTES BY TRACK     SONGS  |   the column heads
| 0B  12X128  ############      1      |
| 0C  12X128  ############      1      |
| 0D  12X128  ############      1      |
| 0E  12X128  ############      1      |
| 0F  12X128  ############      1      |
| 10  12X128  ############      1      |
| 11  12X128  ############      1      |
|[12  12X128  ############      1     ]|   the highlight
| 13  12X128  ############      1      |
| 14  12X128  ############      1      |
| 15  12X128  ############      1      |
| 16  12X128  ############      1      |
| 17  12X128  ############      1      |
| PLAY: HEAR IT                        |   the hint
+--------------------------------------+
----------------------------------------
01:00 EL PIANO               ACC ACCENT
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 2, 19 | 236 × 206 | | a black box with a 1-px outline |
| the title | 6, 22 | | T | `PATTERNS`, `PATTERN FOR 11:03`, `SAVE THE LOOPS TO`, `LOAD INTO LOOPS`, `NEW SONG STARTS WITH`, `LOAD INTO THE EDITOR`, `STORE THE PATTERN IN` |
| the column heads | 8, 35 | | T | `NR  SIZE   NOTES BY TRACK     SONGS` |
| line *i*'s number | 8, 49 + 12 × *i* | | T | `0B` |
| its size | 32, same | | T | tracks (2 digits) `X` rows |
| its map | 80 + 6 × *t*, 53 + 12 × *i* | 4 × 4 each | | one square for each track *t* the pattern has |
| its use | 186, 49 + 12 × *i* | | T | the songs using it, `FUSION`, or `-` |
| the highlight | 4, 48 + 12 × *i* | 232 × 12 | | an outline |
| the hint | 8, 205 | | T | `PLAY: HEAR IT`; `PLAY: STOP HEARING IT` while the preview plays |

13 lines show; the first is `max(0, min(sel − 6, patterns − 13))`.

Colours: the outline light grey; the title cyan; the column heads dark grey; numbers light grey (white
when highlighted), a built-in one as the firmware has it brown ([saving.md](saving.md#fws)); sizes cyan; a map square dark grey for a track without notes, light grey under
1/8 of the rows, white under 1/3, yellow above; the use light grey, `FUSION` light magenta; a pattern
no song or FUSION uses is all dark grey. The highlight is boxed white, an item afloat yellow. The hint's `PLAY`
yellow, its words light grey.

### The song list

ENV opens it on PLAYBACK and STATUS ([screens/playback.md](screens/playback.md),
[screens/status.md](screens/status.md)).
- **Each line:** the song's number, name, tempo (BPM) and length; the song playing in green.
- **13 songs show;** with more, SELECT scrolls a song at a time, keeping the highlight in the middle while
  it can (SONGS' list and DJ's scroll the same way).
- **PLAY** starts the highlighted song from its start, as the current song; **ENV again** or **HOME**
  closes the list. It opens on the song playing, else the current one.
- **On PLAYBACK** the song then plays there, PLAYBACK following it. **On STATUS it is a shortcut to the
  songs:** PLAY goes to SONGS with it.

```
PLAYBACK 00/05                         |
----------------------------------------
+--------------------------------------+
|SONGS                                 |   the title, T
| NR NAME               BPM  TIME      |   the column heads
| 00 PUNCH IN           124  01:33     |
| 01 SPICE ROAD          96  02:00     |   playing: green
| 02 MIDNIGHT RUN       130  01:29     |
|[03 CITY LIGHTS        120  01:36    ]|   the highlight
+--------------------------------------+
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 2, 19 | 236 × 206 | | a black box with a 1-px outline |
| the title | 6, 22 | | T | `SONGS` |
| the column heads | 35 | | T | each over its column: `NR` at 8, `NAME` 26, `BPM` right-aligned to 152, `TIME` 164 |
| line *i*'s number | 8, 49 + 13 × (*i* - top) | | T | `03`; 13 lines show, top = max(0, min(highlight - 6, songs - 13)) |
| its name | 26, same | | T | up to 16 characters |
| its tempo | right-aligned to 152, same | | T | `120` |
| its length | 164, same | | T | `01:36` |
| the highlight | 4, 48 + 13 × (*i* - top) | 232 × 13 | | an outline |

Colours: the outline light grey; the title cyan; the column heads dark grey; numbers, names and lengths
light grey (the song playing green, the highlighted one white); tempos cyan. The highlight is boxed
white.

### The cartridge list

ENV opens it on INSTRUMENTS while browsing, and on LOOPS for the next loop's instrument
([screens/instruments.md](screens/instruments.md), [screens/loops.md](screens/loops.md#controls)).

- **Each line:** the cartridge's ID, name, type (in its colour) and slots used of its slots.
- **ENV again** picks the highlighted cartridge.
- **SEL** opens the screen's menu for it, where the screen has one (INSTRUMENTS: NEW, CLONE, MOVE,
  DELETE, RENAME, AUTO LEVEL ALL).

```
INSTRUMENTS 1/4                        |
----------------------------------------
+--------------------------------------+
|CARTRIDGES                            |   the title, T
| ID NAME         TYPE      SLOTS      |   the column heads
| 05 BASS         SAMPLER   004/064    |
| 06 FUSION       FUSION    006/064    |
| F0 KEYS/ORGANS  DX7       032/032    |   a built-in cartridge
| F1 BELLS/PLUCKS DX7       032/032    |
| F2 GUITAR/STRGS DX7       032/032    |
| F3 WINDS/BRASS  DX7       032/032    |
| F4 PADS/CHOIRS  DX7       024/032    |
| F5 LEADS/BASSES DX7       032/032    |
| F6 FM DRUM KIT  DX7       032/032    |
| F7 PERC/FX      DX7       032/032    |
| F8 JUNO-106     JUNO-106  128/128    |
| F9 CHIPTUNE     CHIP      128/128    |
|[FA TB-303       TB-303    064/128   ]|   the highlight
+--------------------------------------+
----------------------------------------
FA:40 EMPTY TB-303             FA TB-303
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 2, 19 | 236 × 206 | | a black box with a 1-px outline |
| the title | 6, 22 | | T | `CARTRIDGES`, or the screen's own (`NEW LOOP: A CARTRIDGE`: [screens/loops.md](screens/loops.md#controls)) |
| the column heads | 8, 35 | | T | `ID NAME         TYPE      SLOTS` |
| line *i*'s ID | 8, 49 + 13 × (*i* - top) | | T | `01`; 13 lines show, top = max(0, min(cursor - 6, cartridges - 13)) |
| its name | 26, same | | T | up to 12 characters |
| its type | 110, same | | T | `DX7`, `JUNO-106`, `TB-303`, `SAMPLER`, `FUSION` |
| its slots | 170, same | | T | used `/` size, 3 digits each |
| the highlight | 4, 48 + 13 × (*i* - top) | 232 × 13 | | an outline |

Colours: the outline light grey; the title cyan; the column heads dark grey; IDs and names light grey,
white when highlighted, the ID of a built-in cartridge brown while unchanged ([saving.md](saving.md#fws)); each type in its engine's colour ([colours.md](colours.md)); slots light grey.
The highlight is boxed white, a cartridge afloat yellow.

### PRESETS' cartridge list

SEL + PRESETS opens the same list, on every screen but DJ, SEL held or tapped first
([controls.md](controls.md#knobs)), titled `CARTRIDGES`.

- **The first click** picks the next (or previous) cartridge, as SEL + PRESETS does, and opens the list with it
  highlighted; a menu the SEL tap opened closes first, the list taking its place.
- **Each click** picks the next cartridge at once, at its first used slot, the highlight following; the header names it, `CARTRIDGE 01 KEYS/ORGANS`.
  Wrapping, from the last to the first.
- **Any other control** closes the list and does its own job, as if the list had not been open: SELECT moves the
  screen's cursor, ENV opens ENV's own list, a key plays. HOME and SEL close it and do nothing more (SEL's press
  opens no menu; held on, SEL + PRESETS turns the cartridges again).
- **With SEL held,** the list shows while it is held and closes when it is let go; PRESETS then turns the
  instruments of the cartridge picked. Tapped first (or let go within COMBO GRACE before PRESETS turns), the list
  stays until another control.
- **Its LED:** SEL lit while it is open.

### PRESETS' instrument list

PRESETS turned shows the instruments it turns through, titled `INSTRUMENTS`, while SETTINGS' PRESETS LIST is ON
([screens/settings.md](screens/settings.md)).

- **It opens** at a click that changed the current instrument, with nothing else over the screen: not over a menu
  or any other overlay (PRESETS changes the instrument there, the header naming it, as before), not on INSTRUMENTS
  (its own list), not on DJ, not where a screen takes PRESETS for its own job, and not when the click changed nothing
  (a cartridge of one instrument).
- **It shows** the cartridge on its first line (its ID, brown while built in and unchanged, its name light grey, its
  type right-aligned in its engine's colour), then its used slots, the ones PRESETS steps through, as the
  cartridges' list shows its rows: 13 at a time, the current one kept near the middle, `03 GLASS KEYS`, coloured
  as INSTRUMENTS colours them ([screens/instruments.md](screens/instruments.md)): the slot's number dark grey, its
  name in the cartridge type's colour, the current one white and boxed white. Each click moves the highlight; the header names the instrument as
  ever ([frame.md](frame.md#the-header)).
- **It closes** a second after the last click, or at any other control, which does its own job as if it had not
  been open. No LED.

## Text entry

Names are edited the arcade way:
- **ALGORITHM** moves along the name; **SELECT** changes the character there, through every character
  in turn, wrapping.
- **SEL + SELECT** jumps between letters, digits and signs: to the first of the next group.
- **LFO** deletes a character (the rest move left); **OCT+** inserts a space (the rest move right, the
  last one drops off).
- **Done:** the key that opened the entry: EDIT for every name, a menu's RENAME included. **Cancel:** HOME.
- **Characters:** capitals A-Z, digits, space and `: . - ? | / \ * # $ @ %`, in that order.
- **The lengths:** instrument names 10 characters, cartridge names 12, song names 16
  ([README.md](README.md#limits-the-screens-must-respect)). Trailing spaces are dropped; a name left
  empty becomes `NO NAME`.

### Layout

```
INSTRUMENTS                            |
----------------------------------------


   +------------------------------------+
   |INSTRUMENT 01:00                    |   the title, T
   |                                    |
   |          TINE_EP___                |   the name, B, 24 px
   |                                    |
   | ALGORITHM: MOVE  SELECT: LETTER    |
   | EDIT: DONE  HOME: CANCEL           |
   | LFO: DELETE  OCT+: SPACE           |
   +------------------------------------+



----------------------------------------
01:00 EL PIANO DX7        01 KEYS/ORGANS
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 10, 61 | 220 × 104 | | a black box with a 1-px outline |
| the title | 14, 64 | | T | what is named: `INSTRUMENT 01:00`, `CARTRIDGE 0B`, `SONG NAME` |
| character *i* | *x0* + 12 × *i*, 84 | 12 × 24 | B | the character; a space as `_` |
| the cursor | *x0* + 12 × *pos*, 83 | 12 × 26 | | a bar behind the character |
| the hints | 16, 117 / 131 / 145 | | T | `ALGORITHM: MOVE  SELECT: LETTER`; the opener's name `: DONE  HOME: CANCEL`; `LFO: DELETE  OCT+: SPACE` |

*x0* centres the whole length: *x0* = 10 + floor((220 − 12 × length) / 2): 60 for 10 characters, 48
for 12, 24 for 16.

Colours: the outline light grey; the title cyan; the characters light grey, the one at the cursor
white on a deep blue bar; spaces as dark grey `_`; in the hints each control yellow, the rest light
grey.

## Progress

A save, a reload or a reference update shows a bar while it works; every control waits.

```
 +---------------------------------+
 |        SAVING 3 CHANGES         |   T
 |  [##############.............]  |
 +---------------------------------+
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 20, 97 | 200 × 44 | | a black box with a 1-px outline |
| the text | centred in 20-219, 103 | | T | what is being done |
| the bar's frame | 30, 121 | 180 × 10 | | an outline |
| the bar | 32, 123 | up to 176 × 6 | | filled as the work goes on |

Colours: the outline and the bar's frame light grey; the text white; the bar green.

## Toasts and refusals

- **A toast** is a message for 2 s in the header, in place of the screen's name and page
  ([frame.md](frame.md#the-header)). What the footer already shows gets none.
- **A refusal** blinks the button that cannot act, fast, for 0.6 s ([leds.md](leds.md)); where the
  reason is not plain, a toast says it (`NO SONG TO PLAY`).

## Texts

The shared texts; a screen's own are in its file. `1A`, `0B:05`, `3` stand for the actual number,
ID or count; a count of 1 takes the singular (`1 CHANGE`, `1 TIME`).

| Text | Where | When |
|---|---|---|
| `SELECT MOVES IT, SEL PUTS IT DOWN` | toast | a MOVE starts |
| `NOT MOVED` | toast | HOME while an item floats |
| `PATTERN 1A` | the pattern list's menu title | SEL in the list |
| `NEW`, `CLONE`, `MOVE`, `DELETE`, `BACK TO BUILT-IN` | the pattern list's menu | |
| `PATTERN 1C MADE` | toast | NEW |
| `1A CLONED TO 1C` | toast | CLONE |
| `ALL 240 PATTERNS USED` | toast | NEW or CLONE with no free number (`00`-`EF`) |
| `NO FREE NUMBER` | toast | MOVE with no free number |
| `PATTERN 1A MOVED TO 1C` | toast | a pattern put down at another number |
| `PATTERN 1A DELETED` | toast | DELETE |
| `NR  SIZE   NOTES BY TRACK     SONGS` | the pattern list's heads | |
| `ID NAME         TYPE      SLOTS` | the cartridge list's heads | |
| `HOME: CANCEL` | every question | |
| `OCT-`, `OCT+` | every question | over the answers |
| `HOLD`, `+` and the layers' lines | the hold layers | [above](#hold-layers) |
| `HOME +`, its lines, `HOME: CLOSE` | HOME's combination | [above](#homes-combination) |
| `MARK` | the menus where blocks or slots are marked | the first item, nothing marked |
| `SELECT, ALG: MARK  SEL: ITS MENU` | toast | MARK |
| `ALGORITHM: MOVE  SELECT: LETTER` | text entry | |
| `EDIT: DONE  HOME: CANCEL` | text entry | the opener's name before `: DONE` |
| `LFO: DELETE  OCT+: SPACE` | text entry | |
| `NO NAME` | the name | a name left empty |
| `OCT +3`, `OCT -1`, `OCT 0` | toast | OCT- / OCT+ |
| `TRACK 02 MUTED`, `TRACK 02 ON` | toast | HOME + white key |
| `TRACK 02 SOLO`, `SOLO OFF` | toast | HOME + white key held; HOME, SEQ, white key |
| `SOUND CUT: LET GO OF REC` | toast | CUT: HOME + REC ([controls.md](controls.md#cut)) |
| `SOUND CUT, EFFECTS OFF` | toast | CUT held 2 s: every effect switched off ([controls.md](controls.md#cut)) |
| `CHANGES DROPPED` | toast | HOME leaves an edit mode that changed something |
| `SECOND CORE STOPPED` | toast, on any screen, once | the second core fell back to one core during this start ([../second-core.md](../second-core.md#start-switch-fallback)); STATUS' DEVICE page says why. A start that begins with it stopped (after a crash) shows none: DEVICE says `STOPPED: AFTER A CRASH` |
| `RECORD PATTERN INTO TRACK 03` | RECORD PATTERN's title | [above](#record-pattern) |
| `K1`, `NOTE`, `K2`, `STEP`, `K3`, `SIZE`, `16 STEPS`, `1 STEP`, `K4`, `WRITE`, `TO THE END`, `ONCE`, `SELECT`, `NOTES`, `RING TO THE NEXT`, `A STEP LONG` | RECORD PATTERN's lines | |
| `1/32`, `1/16T`, `1/16`, `1/8T`, `1/8`, `1/4`, `A ROW` | RECORD PATTERN's step | |
| `HOME`, `REC`, `CANCEL`, `RECORD`, `KEYS: STEPS  SEQ: NOTE  OCT: OCTAVE`, `KEYS: NOTE  SEQ: STEPS  OCT: OCTAVE` | RECORD PATTERN's answers and keys' line | |
| `KEYS: THE NOTE`, `KEYS: THE STEPS` | toast | RECORD PATTERN's SEQ |
| `NO STEPS ON` | toast | RECORD PATTERN's REC with no step on (refused) |
| `NOTE C-5` | toast | RECORD PATTERN's OCT- / OCT+ |

The texts of saving, REVERT, the edit memory and references are in [saving.md](saving.md#texts); the
transport's in [transport.md](transport.md#texts).

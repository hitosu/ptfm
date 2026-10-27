# The screen's frame

Status: **settled** (2026-10-09). The record: [the UI proposal](../proposals/202610072229-ui.md),
section 3.

What every screen shares: the 240 × 240 screen, how numbers and IDs are written, the header at the
top, the footer at the bottom, and the fonts. Each screen draws only its body, between them
([screens/](README.md#the-files)); its colours follow [colours.md](colours.md).

## The screen

- **240 × 240:** the header, 16 px and a 1-px line; the body, 210 px; a 1-px line and the footer, 12 px.
- **An instrument ID** is the cartridge and the slot, both in hex: `0B:05` is slot 05 of cartridge
  0B. Cells, lists, the footer and messages all use this form.

## Numbers

- **Indexes and values are hex; counts are decimal.**
  - **Indexes,** which one of several, are hex and start at 0: cartridges, slots (so instrument IDs),
    songs, patterns, samples, rows, tracks, song positions, the GATE patterns, LOOPS' loops, bars and
    steps.
  - **Pages are counted,** as a reader counts them: decimal, from 1, of the total (`1/4`): the header's
    page, SEQ's place, the help's pages, the knob pages (`ALG 1/2`), INSTRUMENTS' edit pages. SONGS'
    header counts its songs the same way (`3/18`).
    - **Every row number, shown or referred to:** the row numbers left of the EDITOR's and
      PLAYBACK's rows (FUSION patterns' too), the cursor's and the playing row (`ROW 3F`), and a
      command's row (`D20` breaks to row `20`).
    - **Tracks** are `00`-`0F`: white key 1 is track `00`, white key 16 track `0F`.
    - **Song positions** are `00`-`FF`, the numbers `Bxx` jumps to.
  - **Values** are hex: every value in a cell (its volume, its command's parameters: `7F`, `05`,
    `A04`; the note editor also says them in words, [screens/editor.md](screens/editor.md)), and every
    value without a unit: an instrument's parameters (a DX7's 0-99 are `00`-`63`), the MIXER's.
    Signed values keep their sign (`+18`, `-07`).
  - **Counts and values with a unit are decimal:** TRACKS, ROWS, EDIT STEP, ROWS A BEAT, TICKS A
    ROW, BITS, BEATS A BAR, voices, times, and everything shown with its unit (BPM, %, dB, ms, µs,
    kHz); FILTER's CUT in Hz (`LP 3.8K`, `HP 347`). Note values stay fractions (`1/16`).
- **Numbers keep a constant length,** so nothing shifts as they change:
  - a number from a fixed range is zero-padded to the range's width: tracks `00`-`0F`, rows
    `00`-`FF`, slots `00`-`FF`;
  - **N/M, where you are of several,** is the index and the last index, both hex, N padded to M's
    width: `00/11` is the first of 18 song positions. Pages are the exception (above): `1/4` is the
    first of 4 pages, not padded (it ends the header). The voices are counted in decimal too (the header's
    number, [below](#the-header));
  - live figures sit right-aligned in fixed fields ([screens/status.md](screens/status.md)).

## The header

16 px, then a 1-px line. From left to right, in two colours, the screen and its subpage, on every
screen (2026-10-10):
- **The screen's name,** in its group's colour ([navigation.md](navigation.md)).
- **Its subpage,** white: which of the screen's pages or what it shows, by name. Hidden where a screen
  has none.
- **The page, N/M,** light grey: where you are in a list or a pattern, counted from 1 of the total
  (`1/4`, [Numbers](#numbers)). It is hidden when there is only one page.

  | Screen | Subpage (white) | Page (light grey) |
  |---|---|---|
  | EDITOR | its pattern (`0C`) | pages of rows |
  | PLAYBACK | | song positions (`02/0B`); while the EDITOR's pattern loop plays, the pattern (`PAT 0C`); while LOOPS' loops play on, `LOOPS` |
  | INSTRUMENTS | `EDIT` on the edit page | pages of slots, while browsing |
  | SONGS | | the cursor's song of how many (`3/18`) |
  | FX | its page: `LIVE`, `EDIT`, `MIXER` | |
  | VISUAL | its view: `VOICES`, `SCOPE`, `WAVE`, `SPECTRUM`, `LEVELS` | |
  | STATUS | its page: `PT/FM`, `PERFORMANCE`, `STORAGE`, `DEVICE` | |
  | SETTINGS | `MIDI` on its second page | |
  | LOOPS | | |
  | DJ | the pads' mode: `PLAY`, `LOOPS`, `LFO` | |
  | the help | | its pages |
- **SEQ's place,** where SEQ switches a screen's pages or views: `SEQ` yellow and where it stands light
  grey, in T, right-aligned before the voices. FX `SEQ 1/3` (LIVE, EDIT, MIXER), VISUAL `SEQ 1/5`,
  INSTRUMENTS' edit pages `SEQ 1/3`, SETTINGS `SEQ 1/2`, EDITOR `SEQ 8 TRACKS` (8, 4, 2, 1), PLAYBACK `SEQ 8 TRACKS` (8, 4,
  2, 1). Only where the header leaves room after the name, subpage and page, and not while a menu or a
  dialog is open. LOOPS `SEQ MUTE` or `SEQ SOLO` while its keys are in CONTROL mode, none in PLAY mode
  ([screens/loops.md](screens/loops.md#control-mode-mutes-and-solos)).
- **Toasts:** messages for 2 s (`SAVED`, `OCT +3`, `12 VOICES`, `TRACK 02 MUTED`).
  - A toast takes the place of the screen's name and page while it shows; the voices, transport, CPU
    and battery stay. That leaves 25 characters of S; a longer toast (`SAVE CHANGES TO FREE MEMORY` is
    27) falls back to T ([below](#text-that-does-not-fit)).
  - A toast is white; where it names a key for the user to press, that key is drawn in the keys' yellow,
    as the help draws keys: `SAVE` in `SAVE CHANGES TO FREE MEMORY` ([saving.md](saving.md#the-edit-memory)).
  - What the footer already shows (the current instrument, its cartridge) gets no toast.
- **A knob turned** shows in the same place, on every screen: that knob's name and value (`TEMPO L 124.0 BPM
  +3.2%`, `A04 SLIDE DOWN 4`), white, for 1.2 s after its last click. Toasts and knobs share the place: the
  newest shows.
  - PRESETS too, though the footer shows the instrument: `PRESETS 01:03 E.PIANO`; with SEL (held, or tapped
    first) the cartridge, `CARTRIDGE 01 KEYS/ORGANS`. Where a screen refuses the turn, its toast says why instead.
  - `12 VOICES` stays decimal; it is a count of voices, not an index.
- **OVER 9000:** when the sound takes more than 90 % of either core for a second (the mean of the halves' own
  times over each second, each core's apart: 172 halves of 5.8 ms, or a few more as the main loop comes round),
  `!!! OVER 9000 !!!` shows in the header's middle, red in S, and nothing else of the header's text: the
  name, subpage, page, toasts, knobs turned and SEQ's place are all left blank while it shows; the
  voices, transport, CPU bar and battery stay. It goes at the end of the first second whose means are both 90 % or less. It warns before the late halves (the
  dropouts) begin. The help does not mention it.
- **The voices:** how many voices sound, the most at once over each half second, shown as it ends (a count that
  comes and goes stays readable), a number after the CPU bar, in a voice count's colours
  (against the limit in force: [colours.md](colours.md#what-carries-meaning)), on every screen, so
  no screen counts them again; STATUS keeps its figures, VOICES and its MAX ([screens/status.md](screens/status.md)).
  - `0` to `9` is a digit of S.
  - `10` and more keep the same place, 7 px: their two digits side by side in a narrow 3 × 10 font of their
    own, as high as S's digits.
- **The right part's indicators,** in this order: the transport, CPU and the voices (a pair, the sound's load:
  1 px apart), the battery; 3 px between the others. All are as high as the text's digits and capitals, 10 px,
  and each keeps its own width whatever it shows, so nothing beside it moves.
- **The transport:** ▶ green while playing, ❚❚ red while paused, ● red while recording, each 7 px wide.
- **CPU:** a small bar, 4 × 10 px, as two columns of 2 px: core 1's on the left, core 2's on the right
  ([../second-core.md](../second-core.md)), each its core's share of
  the time, its mean over the last tenth of a second, read again every tenth (no faster).
  - Each fills with its core's load, and its colour follows it: green, then yellow, then red.
  - While core 2 is not running (switched off, stopped, not started yet) its column is the ground only.
  - Both flash red at a late audio half (a dropout).
  - The figures are on STATUS ([screens/status.md](screens/status.md)).
- **Battery:** a battery standing, its nub on top, three bars inside lit from the bottom; its shell red when no
  bar is left. **The bars:** the battery's level is its ADC reading ([../hardware.md](../hardware.md#controls)),
  read every 2 s, taken from 501 (0 %) to 621 (100 %): three bars from 67 %, two from 34 %, one from 10 %; a
  level not known shows as USB power. **Charging:** while a computer has the FM-1 connected over USB (USB configured and the computer's
  frames still coming: it powers the FM-1; the FM-1 has no charger line to tell more, so a plain USB charger shows
  as the battery), the bars fill from the bottom in turn, one, two, three, light blue, a step every half second.
  Unplugged, the battery is back within half a second (an unplug resets nothing on the bus: the frames stopping
  tell it); a computer asleep stops its frames too, so the FM-1 then shows its battery.

## The footer

A 1-px line, then 12 px of 6 × 12 text (40 characters), **on every screen**.
- **Left: the current instrument,** the one a key plays, in its engine's colour:
  - its ID and its name;
  - its engine (DX7, JUNO, TB-303, CHIP, SAMPLE, FUSION), except on the EDITOR.
  - **On DJ**, where the keys are the decks' and no key plays an instrument, key hints instead: what the pads do
    in their mode ([screens/dj.md](screens/dj.md#texts)).
- **Right, on the EDITOR: what K1's slot of the cell under the cursor holds,** in words, as the
  knobs show it while turned: the command (`A04 SLIDE DOWN 4`) or the volume; an empty slot dim.
- **Right, on other screens:** what that screen needs:

  | Screen | The footer's right side |
  |---|---|
  | SONGS | the song's tempo and length |
  | INSTRUMENTS | the cartridge's ID and name |
  | PLAYBACK | the play position (`POS 02 ROW 1F`) |
  | FX | the selected effect's CPU share |
  | VISUAL | the view's name |
  | STATUS | the free storage |
  | SETTINGS | the item's default |
  | LOOPS | `POLY` or `MONO`, how the next loops record; `16 OF 16` with all 16 loops; `REC: STOP AT END` while a song records; `REC: WRITE` in the pattern recorder ([screens/loops.md](screens/loops.md#the-footer)) |
  | DJ | the FX LFO's MIX while it runs, `REV 62%` ([screens/dj.md](screens/dj.md)) |
- **A knob turned shows in the header,** not here ([above](#the-header)): the right side stays the screen's.

## Text that does not fit

**Text that does not fit falls back to a smaller font,** in the header and the footer alike. The
heights never change, and the smaller text is centred in the same space.
- **The header:** S falls back to T, so up to 33 characters fit beside the voices and the symbols.
- **The footer** is in T already, the smallest font, so a name that still does not fit is cut short.
- **The voices, the transport, the CPU bar and the battery** are drawn the same always.

## Fonts

| Font | Size | Across the screen | Used for |
|---|---|---|---|
| T | 6 × 12 | 40 characters | numbers, cells, lists, the footer |
| S | 8 × 16 | 30 characters | the header, labels, notes |
| B | 12 × 24, Terminus bold | 20 characters | the focus row's notes in the 2-track view, the note editor |
| L | 16 × 32 (S doubled) | 15 characters | the note editor's note |

- **All from Terminus** (SIL OFL 1.1). B is Terminus's 12 × 24 bold, `ter-u24b`.
- **Capitals only, everywhere,** the help included. The fonts hold the printable ASCII characters but
  the lowercase letters, 69 glyphs:
  - space, `A`-`Z`, `0`-`9`;
  - the characters names may use: `: . - ? | / \ * # $ @ %` ([dialogs.md](dialogs.md));
  - the other signs: `= + < > ( ) , ! ~ ' ; _ " & [ ] { } ^` and the backtick (`~` for XM's
    volume-column commands, [screens/editor.md](screens/editor.md)). The help's marks use `{ }` and
    `[ ]`, and its text never shows them ([help.md](help.md#marks)).

  That is about 5 KB of flash for all four fonts (L is S drawn doubled).
- **Symbols are drawn, not glyphs:** the transport's ▶ ❚❚ ●, the `◆ SENDS` mark, the knobs' arrows,
  bars, dots and meters. A ratchet shows as `X2`.
- **The voices' digits, 3 × 10:** a font of its own, the ten digits only, for a count of 10 and more in the
  header's one-digit place ([above](#the-header)), two side by side, as high as S's digits: `1` a stem with its
  flag and its foot, `0` a box, the others' middle bar on their fifth row; each of 1-px strokes.
- **S in 12-px rows:** S's capitals, digits, `-` and `#` sit in its rows 2-11, so in a 12-px row S
  is drawn cropped to its middle 12 rows (glyph rows 2-13). A note (`C#4`) fits whole.

## Layout

The frame drawn around any body. The mockup is on the T grid (40 × 20 characters of 6 × 12 px); the
header's S line is 16 px, so the rows below it sit 4 px lower than the grid shows.

### The frame

```
EDITOR 0C 1/3                  > #6 []      S, 16 px: name, page; transport, CPU + voices, battery
----------------------------------------     the line, 1 px, y 16
                                             the body: 240 x 210, y 17-226
            (the screen's body)
                                             overlays and layers draw in it

----------------------------------------     the line, 1 px, y 227
0B:05 SCREAMER          A04 SLIDE DOWN 4     T, 12 px, y 228: left, right
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the header's ground | 0, 0 | 240 × 16 | | cleared every frame |
| the name | 2, 0 | its text | S | the screen's name, a toast or a knob turned, or `LOOK FOR ONSWERS` |
| the name's mark | the name's end + 2, 0 | its text | T | `BETA` after `LOOPS` (dark grey: its capitals' top on S's capitals' row 2); hidden while a toast or a knob shows in the name's place |
| the subpage | the name's end + 8, 0 | its text | S | its name; hidden where the screen has none |
| the page | the subpage's end (else the name's) + 8, 0 | its text | S | N/M; hidden with one page |
| the header's right part | 207, 2 to 238, 11 | | | the transport, the CPU bar, the voices and the battery: each in a box of its own fixed width, all 10 px high, rows 2-11, where S's digits and capitals are; the CPU bar and the voices 1 px apart, 3 px between the others |
| the transport: ▶ | 207, 2 | 7 × 10 | | a triangle, rows 1, 3, 4, 6, 7, 7, 6, 4, 3, 1 px wide |
| the transport: ❚❚ | 207, 2 | 7 × 10 | | two bars of 2 × 10, 3 px apart |
| the transport: ● | 207, 3 | 7 × 8 | | a disc, rows 3, 5, 7, 7, 7, 7, 5, 3 px wide, centred: on the text's middle |
| the CPU bar's ground | 217, 2 | 4 × 10 | | always drawn, both columns |
| the CPU bar, core 1 | 217, 12 − h | 2 × h | | core 1's load: h = its CPU share × 10, rounded, at least 1 |
| the CPU bar, core 2 | 219, 12 − h | 2 × h | | core 2's load, the same way; not drawn while core 2 is not running |
| the voices, 0-9 | 221, 0 | 8 × 16 | S | the voices sounding, a digit (its glyph 222-227, rows 2-11) |
| the voices, 10 and more | 222, 2 and 226, 2 | 3 × 10 each | its own | the tens, then the units, in a 3 × 10 font of digits drawn for it; the same 7 px as a digit of S and a column |
| the battery | 232, 2 | 7 × 10 | | standing: its nub 3 × 1 at 234, 2; its shell a 7 × 9 outline at 232, 3; inside, three bars of 3 × 1 at 234, 9 / 7 / 5, lit from the bottom |
| OVER 9000 | 52, 0 | 136 × 16 | S | `!!! OVER 9000 !!!`, red, the header's only text while it shows ([above](#the-header)) |
| the header's line | 0, 16 | 240 × 1 | | |
| the body | 0, 17 | 240 × 210 | | the screen's; its drawing is clipped to it |
| the footer's line | 0, 227 | 240 × 1 | | |
| the footer's ground | 0, 228 | 240 × 12 | | cleared every frame |
| the footer's left | 0, 228 | its text | T | the current instrument; cut to 40 characters less the right side's and a space |
| the footer's right | 240 − its width, 228 | its text | T | the screen's own |

Colours: the name in its group's colour (MAKE light blue, PLAY green, HEAR brown, SYSTEM magenta),
the subpage white, the page light grey; in every theme the name and the subpage differ (in the
one-colour themes by brightness). The voices in a voice count's colours (against the limit in force: at 16 light
grey, yellow from 12, red from 16). ▶ green, ❚❚ and ● red. The CPU
bar's ground is dark grey's deep shade; each column
green under 50 %, yellow under 80 %, red above, by its own core's share, and after a late half both blink red and
black (80 ms each) for 400 ms. The battery's shell light grey, red with no bar left; its bars light blue while charging, else
green with 2 or 3 bars, yellow with 1; a bar not lit is dark grey's deep shade. Both lines dark grey. The footer's left in the
instrument's engine colour, dark grey for an empty slot; its right in the colour the screen gives
it.

### The header's forms

```
EDITOR 0C 1/3              6 o    # |||      a screen: its name in its group's colour, the subpage white, the page
FX LIVE                    4 >    # |||      a subpage by name, no page
SAVED                      4 >    # |||      a toast: white, for 2 s
TEMPO L 124.0 BPM +3.2%    4 >    # |||      a knob turned: white, for 1.2 s after its last click
LOOK FOR ONSWERS 1/7       0      # |||      the help: cyan, its page light grey
         !!! OVER 9000 !!! 9 >    # |||      the CPU over 90 % for a second: red, the header's only text
INSTRUMENTS: A LONGER NAME THAN F  6 o    # |||   T: the text at 2, 2, cut to 33 characters
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the text in S | 2, 0 | its text | S | when the text, its subpage and its page fit 200 px |
| the subpage and the page in S | each the end before it + 8, 0 | their text | S | |
| the text in T | 2, 2 | up to 33 characters (to x 200, before the voices) | T | when they do not fit in S |
| the subpage in T | the text's end + 6, 2 | its text | T | only when the text and the subpage fit 200 px in T |
| the page in T | the end before it + 6, 2 | its text | T | only when all three fit 200 px in T |
| SEQ's place | right-aligned to x 199, 2 | its text | T | only when it ends 8 px or more after the page (else the text) |

Colours: a toast and a knob turned white; the help's header cyan; a subpage always white, a page always light grey.

### The footer's forms

```
0B:05 SCREAMER TB-303       124 BPM 01:33         SONGS: the instrument and its engine; the right side
0B:05 SCREAMER          A04 SLIDE DOWN 4         EDITOR: no engine; K1's slot of the cell
00:00 EMPTY                     DEFAULT 7F       an empty slot
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the instrument | 0, 228 | its text | T | `0B:05 SCREAMER`, then ` TB-303` (` DX7`, ` JUNO`, ` SAMPLE`, ` FUSION`) except on the EDITOR; `EMPTY` for an empty slot |
| the right side | 240 − its width, 228 | its text | T | the screen's ([the footer](#the-footer)) |

Colours: the instrument in its engine's colour (DX7 light red, JUNO light cyan, TB-303 light green,
SAMPLE yellow, FUSION light magenta), dark grey when empty.

## Texts

- **The header's name:** the screen's (`SONGS`, `EDITOR`, `INSTRUMENTS`, `PLAYBACK`, `FX`, `VISUAL`,
  `STATUS`, `SETTINGS`, `LOOPS`, `DJ`).
- **The header's subpage:** the EDITOR's pattern (`0C`); INSTRUMENTS' `EDIT`; FX's `LIVE`, `EDIT`,
  `MIXER`; VISUAL's `VOICES`, `SCOPE`, `WAVE`, `SPECTRUM`, `LEVELS`; STATUS's `PT/FM`, `PERFORMANCE`,
  `STORAGE`, `DEVICE`.
- **The header's page:** `N/M` as [Numbers](#numbers) writes it; PLAYBACK's pattern loop `PAT 0C`.
- **The help's header:** `LOOK FOR ONSWERS` and its page ([help.md](help.md)).
- **Either core over 90 % for a second:** `!!! OVER 9000 !!!`, red ([The header](#the-header)).
- **Toasts:** in the header's place, white, 2 s; each screen's own are in its file's Texts
  ([screens/](README.md#the-files)), the shared ones in [dialogs.md](dialogs.md) and [saving.md](saving.md).
- **A knob turned:** in the header's place, white, 1.2 s, `<KNOB NAME> <VALUE>` as that screen names its knobs.
- **The footer's left:** `<ID> <NAME>` and, but on the EDITOR, ` DX7`, ` JUNO`, ` TB-303`, ` SAMPLE`
  or ` FUSION`; `<ID> EMPTY` for an empty slot.
- **The footer's right:** each screen's, in its file.

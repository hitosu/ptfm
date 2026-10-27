# SONGS

Status: **settled** (2026-10-09). The record: [the UI proposal](../../proposals/202610072229-ui.md),
sections 6.1 and 13.1.

SONGS is the first screen of the MAKE group (OP1): every song in a list, and below it the
current song's order of patterns. The cursor's song is the current song everywhere: PLAY plays it
([../transport.md](../transport.md)), and the EDITOR opens on its patterns. A song's record:
[../../files/songs.md](../../files/songs.md).

## What it shows

- **The list of songs:** number, name, tempo (BPM), positions, length (mm:ss). The cursor's song is the
  current song. 7 songs show; with more, the list scrolls a song at a time, keeping the cursor in the
  middle while it can, as the song list does ([../dialogs.md](../dialogs.md#the-song-list)). The header
  counts the songs: the cursor's of how many (`3/18`, decimal, from 1).
- **Below it, that song's order:** a grid of pattern numbers, 8 a line, each line led by its first
  position's number. The playing position is on a band; the position PLAY starts from is boxed
  ([below](#where-play-starts)).
- **The length** counts the positions' rows at the song's own TEMPO and TICKS A ROW; tempo commands in
  the patterns are not counted.
- **The playing song** has a play mark beside it and its name in green while it plays.
- **A position whose pattern is missing** shows its number in red ([../saving.md](../saving.md)).
- **The footer's right side:** the current song's BPM and length (`140 BPM 02:45`); the left side is
  the current instrument, as on every screen ([../frame.md](../frame.md)).

## Controls

**On the list:**

| Control | Does |
|---|---|
| SELECT | picks the song: the current song |
| ALGORITHM | the position PLAY starts from, boxed in the order below; playing, only the box moves ([below](#where-play-starts)) |
| PLAY | plays the current song, from the boxed position ([../transport.md](../transport.md)) |
| EDIT | moves the cursor into the song's order, onto the boxed position (EDIT lit) |
| GLO | the song's properties |
| SEQ | the play mode, round: ONE PLAY, REPEAT SONG, PLAY NEXT: how a song PLAY starts here ends ([below](#the-play-modes)) |
| SEL menu | NEW, CLONE, MOVE, DELETE, RENAME; UNLOAD SONG while a song is held; then REVERT while the song has something unsaved |
| ENV | the pattern list ([../dialogs.md](../dialogs.md)); the pattern picked becomes the EDITOR's (the EDITOR's warning first when its own pattern holds anything: [editor.md](editor.md#the-editors-own-pattern)) |
| keys, OCT, PRESETS | as on every screen ([../controls.md](../controls.md)) |
| ARP | the arpeggiator, as on every screen but the EDITOR and LOOPS ([../controls.md](../controls.md)) |

**EDIT on a song** moves the cursor into its order (EDIT lit). In the order:

| Control | Does |
|---|---|
| SELECT / ALGORITHM | move through the positions: SELECT a line (8 positions) at a time, ALGORITHM one |
| K1 | changes the pattern at the position, through the patterns that exist (SEL held: 8 at a time) |
| LFO | removes the position |
| ENV | opens the pattern list; the highlighted pattern goes in at the position (in an empty order: the first position) |
| SEL menu | EDIT PATTERN (the EDITOR on the position's pattern; HOME tap comes back), NEW PATTERN, INSERT, DELETE, COPY, PASTE, CLONE PATTERN (copy the pattern to a new number and use the copy here), REVERT |
| EDIT | leaves the order, keeping the changes |
| HOME | leaves the order, dropping them ([../controls.md](../controls.md)): the order as it was at EDIT; a pattern made or cloned meanwhile is deleted again |

- **The order's menu:**
  - **EDIT PATTERN** shows only when the order has positions. The EDITOR is entered deep, on row 0 (the
    EDITOR's warning first when its own pattern holds anything: [editor.md](editor.md#the-editors-own-pattern)).
  - **NEW PATTERN:** an empty pattern at the first free number, the size of the position's pattern
    (8 tracks of 64 rows when there is none), put in after the position (at the first position in an
    empty order); the cursor moves onto it.
  - **INSERT** puts in a position before the cursor's, with the same pattern.
  - **DELETE** removes the position, as LFO does.
  - **COPY** takes the position's pattern number; **PASTE** puts it in after the position and moves
    the cursor onto it.
  - **CLONE PATTERN** copies the position's pattern to the first free number and uses the copy at the
    position.
  - **REVERT** takes the song back as saved, as the list's REVERT does (below), after the same question;
    the order then shows the saved one, and EDIT keeps that. It shows only while the song has something
    unsaved ([../saving.md](../saving.md)).
  - With all 240 of the user's patterns used (`00`-`EF`; `F0`-`FE` are the firmware's, `FE` the EDITOR's
    own), NEW PATTERN and CLONE PATTERN say so and do nothing.
  - **K1 goes through the user's patterns alone (`00`-`EF`):** a song's order never names the firmware's (`F0`-`FD`
    kept for built-in patterns, `FE` the EDITOR's own). The lists never offer `FE`.
- **The order scrolls** to keep the cursor on its first four lines; five lines show.
- **GLO:** the song's properties: NAME, TEMPO, TICKS A ROW, ROWS A BEAT, SWING, AT THE END (LOOP /
  STOP) and LOOP FROM (the position it loops to). SELECT picks an item, K1 changes it at once (SEL held:
  TEMPO by 10, SWING by 5); EDIT on NAME opens the name entry; GLO or HOME closes
  ([../dialogs.md](../dialogs.md)).

  | Item | Values |
  |---|---|
  | NAME | 16 characters |
  | TEMPO | 3-600, shown in hex with the BPM it makes: `8C (140 BPM)`, `03 (3 BPM)` |
  | TICKS A ROW | 1-31 |
  | ROWS A BEAT | 1-16 |
  | SWING | 50-75 % |
  | AT THE END | LOOP / STOP |
  | LOOP FROM | `POSITION 00` to the last position |

  The BPM is TEMPO × 24 / (TICKS A ROW × ROWS A BEAT), rounded.
- **SEL menu on the list:** NEW, CLONE, MOVE, DELETE, RENAME, UNLOAD SONG, REVERT.
  - **NEW** first asks for the song's first pattern: the pattern list
    ([../dialogs.md](../dialogs.md#the-pattern-list)) titled `NEW SONG STARTS WITH`, `+ NEW PATTERN` its first
    line. SEL picks the highlighted line; HOME makes nothing. Then `NEW SONG` is added at the end of the list
    (TEMPO 120, 6 ticks, 4 rows a beat, LOOP, a new song's FX setup), its one position the pattern picked, or on
    `+ NEW PATTERN` a new empty one of 8 tracks × 64 rows at the first free number (`NO FREE PATTERN` when there
    is none, and no song is made); it becomes current. A song always has at least one position. Past song
    `FD` (`FE` is the keys' song): `NO FREE SONG NUMBER`.
  - **CLONE** puts a copy after the song, named with ` 2` after its first 12 characters, and makes it
    current.
  - **MOVE** floats the song along the list ([../dialogs.md](../dialogs.md)): SELECT shows it at its new
    place, boxed yellow (SEL lit); SEL puts it down; HOME takes it back. Nothing refers to songs, so
    moving one needs no reference update, and no question.
  - **DELETE** asks first, its own patterns ticked to go with it ([DELETE's question](#deletes-question)).
  - **RENAME** opens the name entry; EDIT is its DONE. An empty name becomes `NO NAME`.
  - **UNLOAD SONG** lets go of what is held (whichever song the list's cursor is on): listed only while
    something other than THE KEYS is, playing or not: a song played since power-on, the EDITOR's pattern on the
    current song's setup, the loops once they have played, DJ's decks. What plays stops at once, as SOUND CUT
    cuts it (every voice, the effects' tails), a song's place is forgotten, and the keys play through THE KEYS'
    setup and tempo again, as at power-on ([fx.md](fx.md#the-fx-setup)); FX edits THE KEYS' until something
    plays. The loops themselves and DJ's stand stay as they are (DJ's decks paused, DJ off). Toast `SONG
    UNLOADED`. PLAYBACK's GLO has it too ([playback.md](playback.md#glo-the-views-options)). PLAY then plays the
    current song from its start.
  - **REVERT** takes the song back as saved, the whole record: its order, name, properties, mixer and FX
    setup (not its patterns: each is the EDITOR's to revert), after a question (`REVERT SONG 00 TO SAVED?`, OCT+ `REVERT`, OCT-
    `KEEP`; then `REVERTED`); listed only while the song has something unsaved
    ([../saving.md](../saving.md)). Playing, the song goes on with what is saved from its next row.
- **PLAY** plays the current song.

### Where PLAY starts

On the list, the order below boxes the position PLAY starts from, and **ALGORITHM moves the box** a position a
click (`PLAY FROM POS 03`):
- **picked with ALGORITHM,** PLAY plays the song from that position's first row;
- **while the song plays,** ALGORITHM moves only the box: playing goes on where it is, and the box waits for the
  next PLAY (after a pause, or once the song has stopped);
- **not picked,** the box shows where PLAY would go on: the playing position while the song plays, the one it
  was paused in, else the first (`00`); PLAY then goes on as ever ([../transport.md](../transport.md)), from where
  it paused;
- **another song picked with SELECT:** the box goes to its first position (where it would go on, if it is the
  song that plays or was paused); PLAY NEXT's next song likewise.

EDIT moves the cursor into the order on the boxed position.

### The play modes

How the songs play when PLAY starts them here (SEQ, for a sequence): **SEQ** turns the mode round, `ONE PLAY`,
`REPEAT SONG`, `PLAY NEXT` (a toast each); ONE PLAY at first. SEQ is dim on ONE PLAY, lit on the other two, and
the header says `REPEAT` or `NEXT` after `SONGS` while one of them is set. The `ORDER` line shows the key's hint at
its end, `SEQ ONE PLAY`, `SEQ REPEAT SONG`, `SEQ PLAY NEXT`, the mode green while it is not ONE PLAY.
- **A looping song loops in every mode,** from its LOOP FROM, as its AT THE END says. **The mode decides the end
  of a song that stops:**
  - **ONE PLAY:** it stops there.
  - **REPEAT SONG:** it plays again from its start.
  - **PLAY NEXT:** the next song in the list plays, from its start, at once (within a half of the audio, 6 ms). The
    next song is the one after the song that played, wherever the cursor is; songs with no positions are skipped;
    **the cursor follows it,** on the list (not in an order being edited, nor with a song afloat), so it is the
    current song. A looping song reached so loops there. After the last song the playing stops: the list does not
    wrap round.
- **It takes hold when PLAY starts a song here,** and SEQ turned while a song plays changes it at once. **It
  holds on every screen** while songs play, through pauses and PLAY again, K1's jumps, the song list's songs and
  PLAYBACK's speed and volume, **until something else plays** (the EDITOR's pattern loop, the pattern list's
  PLAY, LOOPS, DJ): from then on a song ends as its AT THE END says, until PLAY starts one here again.
- **It is not stored:** it stays as it is until the FM-1 is switched off, performance state.

## Layout

The mockups are on the T grid (40 × 20 characters of 6 × 12 px); the list's rows are 13 px, drawn here
a line each. The header and footer are the frame's ([../frame.md](../frame.md)).

### The list

```
SONGS 1/18                             S, the frame's header; the cursor's song of how many
 NR NAME              BPM LEN  TIME
 00 PUNCH IN          124   6  01:33   the current song, boxed
 01 SPICE ROAD         96   6  02:00
 02 MIDNIGHT RUN      130   6  01:29
 03 CITY LIGHTS       120   6  01:36
 04 ACID TEST         138   6  01:23
>05 ADVENTURE         132   6  01:27   > the playing song
 06 TRANCE LOOP       140   1  00:14
----------------------------------------
ORDER PUNCH IN              SEQ ONE PLAY   SEQ's hint: the play mode
00  00  01   02  03   04  05



----------------------------------------
01:00 EL PIANO DX7         124 BPM 01:33   the frame's footer
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| column names | 19 | | T | each over its column: `NR` at 8, `NAME` 26, `BPM` 132, `LEN` 156 (the positions: the song's length), `TIME` 186 |
| a song's row, *i* = 0-6 on the page | 0, 33 + 13 *i* | 240 × 13 | T | |
| – the cursor's box | 0, row − 1 | 240 × 13 | | around the current song (not while in the order) |
| – the play mark | 1, row + 1 | the transport's play symbol | | the playing song, while it plays |
| – number | 8, row | 2 characters | T | hex |
| – name | 26, row | up to 16 characters | T | |
| – BPM | 132, row | 3, right-aligned | T | decimal |
| – positions | 156, row | 3, right-aligned | T | decimal |
| – length | 186, row | 5 | T | mm:ss |
| the rule | 0, 125 | 240 × 1 | | |
| ORDER | 0, 128 | | T | `ORDER ` and the song's name; in the order `  (EDIT)` after it |
| SEQ's hint | right-aligned to 240, 128 | 12-15 characters | T | `SEQ ONE PLAY`, `SEQ REPEAT SONG`, `SEQ PLAY NEXT` ([the play modes](#the-play-modes)); not in the order (EDIT) |
| an order line, *l* = 0-4 shown | 0, 142 + 13 *l* | 240 × 13 | T | |
| – first position | 0, line | 2 characters | T | the line's first position, hex |
| – position *k* = 0-7 | 22 + 27 *k*, line | 2 characters | T | its pattern number |
| – the playing position's band | position − 3, line − 1 | 24 × 13 | | filled |
| – the cursor's box (in the order), where PLAY starts (on the list) | position − 3, line − 1 | 24 × 13 | | |
| an empty order | 22, 142 | | T | `EMPTY: ENV ADDS A PATTERN` |

Colours: the column names, `ORDER` and the lines' first positions' labels cyan, the numbers before the
names dark grey; the current song white (its number too), the others light grey; the playing song's
name and play mark green. The rule dark grey. The song's name after ORDER white, `(EDIT)` light blue. SEQ's
hint: `SEQ` yellow (a key, as the help draws one), the mode light grey, green while it is REPEAT SONG or PLAY NEXT.
The first positions dark grey; pattern numbers light grey, the cursor's white, the playing one light
green on a deep blue band, a missing pattern red. `EMPTY: ENV ADDS A PATTERN` dark grey. The footer's
right side light grey.

### The order (EDIT)

```
SONGS
 NR NAME              BPM LEN  TIME
 00 PUNCH IN          124   6  01:33
 01 SPICE ROAD         96   6  02:00
 ...
----------------------------------------
ORDER PUNCH IN  (EDIT)
00  00  01   02 [03]  04  05              the cursor's position, boxed
```

The list stays as it is, without the cursor's box; the order's cursor is a white box around its
position (the elements above). EDIT is lit.

### MOVE: a song afloat

```
SONGS                                  the header: SELECT MOVES IT, SEL PUTS IT DOWN (2 s)
 NR NAME              BPM LEN  TIME
 00 SPICE ROAD         96   6  02:00
 01 MIDNIGHT RUN      130   6  01:29
 02 PUNCH IN          124   6  01:33   afloat: boxed yellow, at the place it would go
 03 CITY LIGHTS       120   6  01:36
```

The list is drawn as it would be with the song at its new place, the numbers following the places;
the song afloat is boxed in yellow. SEL is lit. The order below shows the song afloat's.

### The properties (GLO)

```
 +------------------------------------+
 |SONG 00 PROPERTIES                  |
 |[NAME                     PUNCH IN] |   the selected item, boxed
 | TEMPO                7C (124 BPM)  |
 | TICKS A ROW                     6  |
 | ROWS A BEAT                     4  |
 | SWING                        50 %  |
 | AT THE END                   STOP  |
 | LOOP FROM             POSITION 00  |
 +------------------------------------+
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 6, 21 | 228 × 122 | | |
| title | 10, 24 | | T | `SONG 00 PROPERTIES` |
| item *i* = 0-6 | 12, 40 + 14 *i* | | T | its name |
| – value | right-aligned to x 228 | | T | |
| – the selected item's box | 8, item − 2 | 224 × 14 | | |

Colours: the panel's border light grey, the title cyan; item names cyan, values light grey; the
selected item's name and value white, its box white.

### The SEL menus

```
              +------------------------+
              |SONG 00                 |
              |NEW                     |
              |[CLONE                 ]|   the last one used, preselected
              |MOVE                    |
              |DELETE                  |
              |RENAME                  |
              |REVERT                  |   while the song has something unsaved
              +------------------------+
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 84, 21 | 150 × (12 × items + 22) | | |
| title | 88, 24 | | T | `SONG 00`; in the order `POSITION 03` |
| item *i* | 90, 39 + 12 *i* | | T | |
| – the selected item's band | 86, item − 1 | 146 × 12 | | filled, and boxed |

The order's menu has up to 8 items: EDIT PATTERN, NEW PATTERN, INSERT, DELETE, COPY, PASTE, CLONE
PATTERN, REVERT.

Colours: the title cyan; items light grey; the selected item white on a deep blue band, boxed white.

### DELETE's question

```
 +--------------------------------------+
 |           DELETE SONG 00?            |
 |               PUNCH IN               |
 |[# ITS PATTERNS TOO           4  1.3K]|   the one line, boxed; # ticked
 |    2 SHARED WITH OTHER SONGS STAY    |   only when some are shared
 |                                      |
 |        OCT-               OCT+       |
 |        KEEP              DELETE      |   S
 |                                      |
 |        K1: TICK  HOME: CANCEL        |
 +--------------------------------------+
```

A question with a checklist ([../dialogs.md](../dialogs.md#a-question-with-a-checklist)) of one line,
`ITS PATTERNS TOO`, **ticked at first**: the song's own patterns, those in its order that no other
song and no FUSION instrument uses, go with it. Its figures are how many and the room they free, as
CLEAN UP's; with none of its own the line is dim, `NONE`, and cannot be ticked.
- **Patterns other songs or FUSION instruments use** stay, and so do the built-in ones (F0-FF); the
  dark-grey line under the tick says how many of the song's stay so (`2 SHARED WITH OTHER SONGS STAY`),
  shown only when there are some.
- **OCT+** deletes the song, and its own patterns when ticked (the toast `SONG 00 DELETED, 4 PATTERNS
  TOO`); with the tick cleared, the song alone (no toast). **OCT-** keeps it; **HOME** cancels.
- Its layout: the checklist's, a line taller for the song's name (light grey, under the first line)
  and another for the shared line when it shows; the hints without `SELECT: PICK` (one line, nothing
  to pick).

Colours: the first line white, the song's name light grey; the tick line light grey (dim with none),
boxed white; the shared line dark grey; OCT- and OCT+ yellow, the answers white; K1 and HOME yellow,
`: TICK` and `: CANCEL` light grey.

### The name entry (RENAME, GLO → NAME)

The dialogs' name entry ([../dialogs.md](../dialogs.md)), titled `SONG NAME`, 16 places; its hints
end with `EDIT: DONE  HOME: CANCEL`.

### The pattern list (ENV)

The dialogs' pattern list ([../dialogs.md](../dialogs.md)), titled `PATTERNS`, the position's pattern
highlighted (on the list: the song's first). PLAY hears the highlighted one; ENV picks.

## Texts

- **The header:** `SONGS`; `REPEAT` or `NEXT` while that play mode is set (white, a subpage); and the cursor's song of how many
  (`1/18`) when there are songs.
- **Labels:** `NR`, `NAME`, `BPM`, `LEN`, `TIME`; `ORDER ` and the name; `  (EDIT)`; `SEQ ONE PLAY`, `SEQ REPEAT SONG`, `SEQ PLAY NEXT`;
  `EMPTY: ENV ADDS A PATTERN` (an empty order).
- **The footer's right side:** `140 BPM 02:45`. **In the header's place** ([../frame.md](../frame.md#the-header)),
  1.2 s: while K1 turns in the order, `K1 PATTERN 0A`; while ALGORITHM turns on the list, `PLAY FROM POS 03`.
- **The list's menu** `SONG 00`: NEW, CLONE, MOVE, DELETE, RENAME; UNLOAD SONG while a song is held; REVERT
  last while the song has something unsaved.
- **The order's menu** `POSITION 03`: EDIT PATTERN, NEW PATTERN, INSERT, DELETE, COPY, PASTE, CLONE
  PATTERN, REVERT.
- **REVERT's question:** `REVERT SONG 00 TO SAVED?`; REVERT (OCT+) / KEEP (OCT-).
- **The properties** `SONG 00 PROPERTIES`: NAME, TEMPO (`8C (140 BPM)`), TICKS A ROW, ROWS A BEAT,
  SWING (`50 %`), AT THE END (`LOOP`, `STOP`), LOOP FROM (`POSITION 00`).
- **DELETE's question:** `DELETE SONG 00?` and the song's name; the tick line `ITS PATTERNS TOO` (`4  1.3K`,
  `NONE`); `2 SHARED WITH OTHER SONGS STAY`; DELETE (OCT+) / KEEP (OCT-); `K1: TICK  HOME: CANCEL`.
- **The name entry:** `SONG NAME`. A new song: `NEW SONG`; an empty name: `NO NAME`.
- **NEW's pattern list:** `NEW SONG STARTS WITH`; its first line `+ NEW PATTERN`.
- **With no song:** `NO SONGS: SEL MAKES ONE OR UPLOAD A DISK` under the list (SEL's menu has NEW).
- **Toasts** (in the header, 2 s):

  | Toast | When |
  |---|---|
  | `SELECT MOVES IT, SEL PUTS IT DOWN` | MOVE starts |
  | `SONG MOVED TO 02` | SEL puts the song down at a new place |
  | `NOT MOVED` | HOME takes a song afloat back |
  | `PATTERN 0C MADE` | NEW PATTERN |
  | `NO FREE PATTERN` | NEW PATTERN or CLONE PATTERN with all 240 of the user's used; NEW's `+ NEW PATTERN` too |
  | `NO FREE SONG NUMBER` | NEW or CLONE past song `FD` |
  | `SONG 00 DELETED, 4 PATTERNS TOO` | DELETE with its own patterns ticked (`1 PATTERN TOO` for one) |
  | `PATTERN 03 COPIED` | COPY |
  | `03 CLONED TO 0C` | CLONE PATTERN |
  | `REVERTED` | REVERT, after its question |
  | `SONG UNLOADED` | UNLOAD SONG (here or PLAYBACK's GLO) |
  | `CHANGES DROPPED` | HOME leaves the order with something changed |
  | `PATTERN 0A FOR THE EDITOR` | ENV on the list picks a pattern |
  | `ONE PLAY`, `REPEAT SONG`, `PLAY NEXT` | SEQ ([the play modes](#the-play-modes)) |

## Help

Each page as the help shows it ([../help.md](../help.md)): `{...}` a key, `[...]` a screen. The help
draws the first page's last line, `PRESS HOME TO GET BACK`, itself. The shared pages follow.

```
[SONGS]: YOUR SONGS, PICK AND ARRANGE
A SONG: PATTERNS PLAYED IN AN ORDER
TOP: THE SONGS; BELOW: THE ORDER OF
  THE ONE PICKED
{SELECT}: PICK A SONG
{PLAY}: PLAY / PAUSE IT
{ALGORITHM}: WHERE PLAY STARTS, BOXED
{EDIT}: CHANGE ITS ORDER (NEXT PAGE)
{GLO}: NAME, TEMPO, SWING, LOOP OR STOP
{SEQ} (SHOW ELSE, QUICK): WHEN IT ENDS
  ONE PLAY, REPEAT SONG OR PLAY NEXT
{ENV} (EXPLORE NAMED VAULTS): PATTERNS
  {ENV} AGAIN: [EDITOR] WILL SHOW IT
  {PLAY}: HEAR THE HIGHLIGHTED ONE
{SEL}: NEW, CLONE, DELETE... (PAGE 3)
```

```
THE ORDER: {EDIT} ON A SONG
POSITIONS: ITS PATTERNS, IN PLAY ORDER
{SELECT}: A LINE (8) UP / DOWN
{ALGORITHM}: A POSITION LEFT / RIGHT
{K1}: ANOTHER PATTERN HERE
{LFO} (LEAVE FIELD OPEN): REMOVE IT
{ENV}: THE PATTERNS; {ENV} AGAIN PUTS
  THE HIGHLIGHTED ONE HERE
{SEL}: MENU FOR THIS POSITION
  INSERT: THE SAME PATTERN, BEFORE IT
  DELETE, COPY; PASTE: AFTER IT
  NEW PATTERN: AN EMPTY ONE, AFTER IT
  CLONE PATTERN: USE A COPY HERE
  EDIT PATTERN: IN [EDITOR], {HOME} BACK
  REVERT: THE SONG AS LAST SAVED
{EDIT}: DONE, KEEP THE CHANGES
{HOME}: LEAVE, DROP THE CHANGES
```

```
THE SONG MENU: {SEL} ON THE LIST
NEW: A NEW SONG; FIRST PICK THE
  PATTERN IT STARTS WITH (OR A NEW ONE)
CLONE: A COPY OF THE SONG, AFTER IT
MOVE: ANOTHER PLACE IN THE LIST
DELETE: IT ASKS FIRST
RENAME: TYPE ITS NAME
UNLOAD SONG: ALL STOPS, AS AT POWER-ON
  THE {KEYS}: THEIR OWN EFFECTS AGAIN
REVERT: THE SONG AS LAST SAVED

{ARP} (AGAIN, REPEAT, PLEASE): ARPEGGIO
  {KEYS} HELD TAKE TURNS
  HOLD {ARP}: RHYTHM, ORDER, LATCH
```

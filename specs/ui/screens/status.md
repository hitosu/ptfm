# STATUS

Status: **settled** (2026-10-09). The record: [the UI proposal](../../proposals/202610072229-ui.md),
sections 6.8 and 13.8; the two cores' figures: [the second core's](../../proposals/202610220009-second-core.md),
section 9.

STATUS is the start screen: the FM-1 opens on it unless SETTINGS → POWER-ON SCREEN says otherwise
([settings.md](settings.md)). It is in the HEAR group; HOME + the black key marked GLO goes to it
([../navigation.md](../navigation.md)).
It also holds the figures behind the header's bars ([../frame.md](../frame.md)).

## The pages

SELECT or ALGORITHM turns the pages, one a step, stopping at both ends:

| Page | Shows |
|---|---|
| **PT/FM** (the first) | the title: `PT/FM` in L, under it in T the name in full, `POCKET TRACKER / FUSION MACHINE`, and `BY HITOSU` with the version (`fw/VERSION`'s number); then, together, the navigation hints: a line of help in S, `PRESS HOME + LFO FOR HELP`, and under it in T, wordless, the two knobs that move about: `SELECT` with an up-and-down arrow, `ALGORITHM` with a left-and-right one; then the live figures, below |
| **PERFORMANCE** | each core's CPU now / average / most; the worst audio half (µs), late halves and steals; each engine's share; voices used / most / limit; the effects' cost; a CPU graph over the last 30 s |
| **STORAGE** | the user area, where everything the user keeps goes ([../saving.md](../saving.md#fws)), used and free; records by kind; unsaved changes and the RAM they take |
| **USER FLASH MAP** | the user area as a map, a square a KB, each in the colour of what it holds; a legend with each kind's size |
| **RAM MAP** | the 512 KB of RAM as a map, a square a KB, each in the colour of what it holds; a legend with each part's size |
| **DEVICE** | battery, firmware version, output (STEREO, MONO, SYNC PULSE L / R), MASTER, frames a second and the frame time, uptime, VOICES, THEME, the second core's state |

**The first page.** `PT/FM`, the version and the help line are centred. The figures below them are in
two columns; CPU and VOICES are read again every tenth of a second (no faster), over the tenth gone by. The sizes
step down from the name to the numbers:

| Line | Font | Why |
|---|---|---|
| `PT/FM` | L, 16 × 32 | the name, the largest thing on the screen |
| the version | T, 6 × 12 | read rarely, so the smallest |
| the help line | S, 8 × 16 | the one thing a newcomer must read: 25 characters, 200 px |
| the knobs | T, with 5 × 9 and 9 × 5 arrows | as a rule, SELECT moves up and down (rows, lists, pages) and ALGORITHM sideways (tracks, columns, positions): shown once, without words |
| the figures | T | 40 characters across fit two columns |
| the song's name | S, after `SONG` in T | what plays, readable at a glance |

With the gaps between them, the page takes about 196 of the body's 210 px.

| Figure | What it counts |
|---|---|
| CPU 1, MAX | core 1's share of the time for the sound (the audio interrupt's time over the half), its mean over the last tenth of a second; the most since the last reset |
| CPU 2, MAX | core 2's ([../../second-core.md](../../second-core.md)): its blocks' time over the half, the same way; `OFF` while core 2 is not running |
| HALF | the longest audio interrupt (core 1's) since the reset, in µs (the limit is 5,804) |
| LATE | audio halves not ready in time: each one is a dropout |
| STEALS | notes that gave their voice up to a new note, the pool full: a held one parked until a voice is free, a tail faded out ([settings.md](settings.md#voices)) |
| VOICES, MAX | voices sounding, the most at once over the last tenth of a second / the limit in force (SETTINGS → VOICES; 16 counts as 12 while the second core does not run: [settings.md](settings.md#voices)); the most since the reset |
| EDITS FREE | the room left for unsaved changes: the edit memory less what the changes take, as MEMORY's ([../saving.md](../saving.md)) |
| FLASH FREE | free storage in the user area, where saves go |
| UNSAVED | how many things have changed since the last SAVE |
| BATTERY, UP | the battery's bars; the time since power-on |
| SONG | the playing song, else the current song |

**Its colours** (by role, so every theme maps them; the default palette's names,
[../colours.md](../colours.md)):

| What | Colour |
|---|---|
| `PT/FM` | a letter in each engine's colour: P light red (DX7), T light cyan (Juno-106), / yellow (samples), F light green (TB-303), M light magenta (FUSION) |
| the name in full | light grey |
| `BY HITOSU` | `BY` light grey, `HITOSU` white |
| the version | dark grey; `DEV` after it in yellow |
| the help line | light grey, with `HOME + LFO` in white |
| the figures' names | cyan, as labels everywhere |
| CPU 1, CPU 2, their MAX, HALF | green up to 50 % of the time, yellow up to 80 %, red above; `OFF` (after `CPU 2`, the name cyan as every name) dark grey |
| LATE, STEALS | dark grey at zero, red above |
| EDITS FREE, FLASH FREE | green, yellow under 25 % free, red under 10 % |
| BATTERY | the header's three bars as a count: green, yellow at 1/3, red at 0/3 |
| UNSAVED | dark grey at zero, yellow above (as the unsaved state everywhere) |
| VOICES and its MAX | a voice count's colours, against the limit in force: red at it, yellow from 4 under it, light grey below ([../colours.md](../colours.md#what-carries-meaning)) |
| UP | light grey |
| the song's name | white |

- **LFO** resets MAX, HALF, LATE and STEALS, here and on PERFORMANCE.
- **STATUS's help** is where the global keys are explained: it opens on them
  ([../controls.md](../controls.md), the key map's "on every screen"), before STATUS's own controls.
- **The version** is a number set by hand in the repository (`fw/VERSION`): a production build shows it alone,
  a dev build with `DEV` after it, a debug build with its own build time (month, day, hour, minute) and `DEBUG`
  ([../../layout.md](../../layout.md)).
  - Dev builds show `DEV` after the version, so they are never mistaken for a release.
- **The footer's right side:** the user area's free flash (`FREE 237K`), in the free-space colours
  above.

## Controls

| Control | Does |
|---|---|
| SELECT, ALGORITHM | turn the pages (either, the same way) |
| LFO | resets MAX, HALF, LATE and STEALS |
| ENV | the song list ([../dialogs.md](../dialogs.md#the-song-list)): PLAY starts the highlighted song from its start, as the current song, and goes to SONGS; ENV again just closes the list |
| EDIT | SETTINGS, from any page: the same as HOME + MONO ([../navigation.md](../navigation.md#the-screens)), a plain switch with nothing to come back to |
| ARP | the arpeggiator, as on every screen but the EDITOR and LOOPS ([../controls.md](../controls.md)) |
| everything else | as on every screen ([../controls.md](../controls.md)) |

## Layout

The mockups are on the T grid (40 × 20 characters of 6 × 12 px); the header and footer are the
frame's ([../frame.md](../frame.md)). Sizes print as bytes under 1 KB (`512 B`), with one decimal under
10 KB (`9.5K`), else whole (`237K`).

### PT/FM, the first page

```
STATUS PT/FM                           S, the frame's header: the name, the page white

             PT/FM                      L, 32 px
    POCKET TRACKER / FUSION MACHINE     T
  BY HITOSU  0.1-202610072229 DEV       T

   PRESS HOME + LFO FOR HELP            S, the hints together
        SELECT ^v  ALGORITHM <>         T, the arrows drawn

CPU 1  23% MAX  41%  CPU 2  12% MAX  20%   T, 6 rows of 12 px
LATE    0            STEALS   0
VOICES 06/12 MAX 09  HALF 1932 US
EDITS FREE 62K       UNSAVED 3
FLASH FREE 237K
BATTERY 2/3          UP 01:23:45

SONG PUNCH IN                           T for SONG, S for the name
01:00 EL PIANO DX7             FREE 237K
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| `PT/FM` | 80, 25 | 80 × 32 | L | the name, a letter a colour |
| the name in full | centred, 59 | 186 × 12 | T | `POCKET TRACKER / FUSION MACHINE` |
| the author and the version | centred, 71 | 6 × its length | T | `BY HITOSU`, two spaces, `0.1-202610072229`, and ` DEV` in a dev build |
| the help line | 20, 89 | 200 × 16 | S | `PRESS HOME + LFO FOR HELP` |
| `SELECT` | 57, 107 | 36 × 12 | T | |
| the up-and-down arrow | 96, 108 | 5 × 9 | | drawn |
| `ALGORITHM` | 117, 107 | 54 × 12 | T | |
| the left-and-right arrow | 174, 110 | 9 × 5 | | drawn |
| the figures, left | 0, 125 + 12 *r*, *r* = 0-5 | | T | CPU 1 and its MAX; LATE; VOICES and MAX; EDITS FREE; FLASH FREE; BATTERY |
| the figures, right | 126, 125 + 12 *r* | | T | CPU 2 and its MAX; STEALS; HALF; UNSAVED; none; UP |
| `SONG` | 0, 207 | | T | |
| the song's name | 30, 205 | | S | |

The figures' formats: `CPU 1` and `CPU 2`, a space, the share in 3 digits (right-aligned) and `%`, a space, `MAX`,
a space, its share the same way (19 characters: `CPU 1  23% MAX  41%`); while core 2 is not running `CPU 2  OFF`
(`OFF` in the share's 4 places), the rest blank;
`HALF` 4 digits and ` US`; `LATE` 4 digits; `STEALS` 3 digits; `VOICES 06/12 MAX 09` (2 decimal digits each);
`BATTERY 2/3`, `USB` on USB power (light blue, as the header's bars); `UP` hh:mm:ss. The frames a second are on
[DEVICE](#device).

The two knobs together take 126 px (the words, the arrows and the gap), centred: x 57.

### PERFORMANCE

```
STATUS PERFORMANCE
             NOW   AVERAGE     MOST
CPU 1        23%       31%      41%
CPU 2        12%       14%      20%
WORST HALF  1932 US  LATE         0
VOICES     06/09/12  STEALS       0

DX7          0 X 5.6%    JUNO     0 X 4.4%
TB-303       0 X 2.8%    SAMPLE   0 X 1.6%
EFFECTS            0.0%

+--------------------------------------+
|CPU, LAST 30 S                        |
|--------------------------------------|   80 %
|                                      |
|--------------------------------------|   50 %
|                          ||  ||||| ||||   2 px a sample, the newest right
+--------------------------------------+
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the cores' head | *y* = 19 | | T | `NOW` right-aligned to x 96, `AVERAGE` to x 156, `MOST` to x 210 |
| a core's row | *y* = 31 (`CPU 1`), 43 (`CPU 2`) | | T | its name at x 0; under each head, right-aligned: its share now (its mean over the last tenth of a second, as the first page's), its average (the mean of the graph's half-second samples since the counts were zeroed; core 2's of those it ran in, so a late start or a switch does not pull it down) and its most (its longest half since then over a half's length, as the first page's MAX); while core 2 is not running its row reads `OFF` under NOW, the rest blank |
| a figure, left | 0, *y* | 116 wide | T | its name; its value right-aligned to x 116 |
| a figure, right | 124, *y* | 116 wide | T | its name; its value right-aligned to x 240 |
| rows | *y* = 55, 67 | | | WORST HALF (core 1's longest audio interrupt) and LATE; VOICES (used / most / limit) and STEALS; the voices used as the first page's VOICES, over each tenth of a second |
| the engines | *y* = 83, 95 | | T | DX7 and JUNO; TB-303 and SAMPLE: voices × the share of one (`0 X 5.6%`), counted again every tenth of a second |
| EFFECTS | 0, 107 | | T | the effects' cost (`0.0%`), on whichever core runs them: measured only while STATUS is open, with one pair of timer reads around the whole effects stage in each audio block |
| the graph | 0, 125 | 240 × 100 | | a box |
| – its label | 2, 127 | | T | `CPU, LAST 30 S` |
| – 50 % and 80 % | 1, 175 and 145 | 238 × 1 | | lines |
| – core 1 | 2 px a sample from the right | 1 × up to 98 | | bars, 1 px wide with a 1-px gap: a sample each half second, core 1's mean over it (AVERAGE: theirs since the counts were zeroed) |
| – core 2 | the same samples | 2 × 1 | | a mark at the height of core 2's mean over each half second; none while it is not running |

Colours: the names and the cores' head cyan; each core's figures and WORST HALF in the CPU colours, `OFF` dark grey;
LATE, STEALS dark grey at zero, red above; VOICES in a voice count's colours
([../colours.md](../colours.md#what-carries-meaning)), the sounding count by itself, the most and the limit after it
by the most; EFFECTS light grey; an engine's figure in its colour while it has voices, dark grey without. The graph's
box dark grey, its label dark grey, the 50 % line deep yellow, the 80 % line deep red, each bar in the CPU colours,
core 2's marks white.

### STORAGE

```
STATUS STORAGE
FLASH
USER     [##############      ]  96K FREE

FILES BY TYPE
PATTERNS           61K   COUNT           42
INSTRUMENTS         3K   CARTRIDGES       5
SAMPLES           180K   SONGS           10

NOT SAVED
CHANGES              0   THEIR RAM      0 B
EDITS FREE         32K
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| `FLASH` | 0, 19 | | T | |
| `USER` | 0, 33 | | T | the user area |
| – its bar | 60, 33 | 120 × 10 | | a box; the used part filled inside it (61, 34, up to 118 × 8): the user area's records and audio, with the reserve kept free for saving and the room kept for the EDITOR's own pattern ([../../files/store.md](../../files/store.md#the-room-for-the-editors-pattern)) counted as used |
| – free | 186, 33 | | T | `237K FREE`: where a save can go |
| `FILES BY TYPE` | 0, 51 | | T | |
| files | rows 65, 77, 89; left x 0, right x 124, 116 wide | | T | PATTERNS (size) and COUNT; INSTRUMENTS (size) and CARTRIDGES; SAMPLES (size) and SONGS |
| `NOT SAVED` | 0, 107 | | T | |
| changes | rows 121, 133 | | T | CHANGES and THEIR RAM (the edit memory they take); EDITS FREE (the edit memory left: its 67 KB less theirs, [../../files/ram.md](../../files/ram.md#the-edit-memory-and-the-spare-zone)) |

Colours: `FLASH`, `FILES BY TYPE`, `NOT SAVED` and the figures' names cyan; USER light grey, its bar's box
dark grey, filled green; the free amount and the files' values light grey; USER's free amount and EDITS
FREE in the free-space colours; CHANGES and THEIR RAM yellow while something is unsaved, dark grey when
not.

### USER FLASH MAP

```
STATUS USER FLASH MAP
FLASH 412K, A SQUARE 1K
################################################   48 squares a line, 4 x 4 px with a 1 px gap
################################################   (the user area's 412: 8 lines and 28)
...
############################
# SAMPLES            178K
# INSTRUMENTS        2.4K
# PATTERNS            79K
# SONGS              2.9K
# LOOPS                0K
# OTHER                0K
# KEPT FOR SAVING     40K
# FREE               100K
```

The user area alone ([../../files/store.md](../../files/store.md#where-things-go)), its two fast regions one
after the other as one, a square a KB in the order the flash holds them.

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the first line | 0, 19 | | T | `FLASH 412K, A SQUARE 1K`: the user area's size |
| the map | 0, 33 | 4 × 4 a square, 5 px apart, 48 a line | | a square for each KB of the user area, the last line as long as it comes |
| the legend | 0, 8 below the map, 12 a line | | T | a 6 × 6 swatch (its y 3 below the line's), the kind's name at x 10, its size right-aligned to x 150 |

What a square shows, KB by KB:
- **A KB that holds records** takes the colour of the kind with the most bytes in it: SAMPLES (the samples'
  audio), INSTRUMENTS (the cartridges), PATTERNS, SONGS, LOOPS, OTHER (the settings, DJ's stand, the MIDI map,
  deleted records' headers). A KB of a block in use that holds no live record (an older copy, until
  compaction frees its block) takes its block's colour: it is not free yet.
- **FREE:** a KB of a free block, and the rest of the block being written after its last record.
- **KEPT FOR SAVING:** the 40 KB kept for compaction and the EDITOR's own pattern
  ([../../files/store.md](../../files/store.md#the-room-for-the-editors-pattern)), an amount and not a place:
  drawn as the last free blocks' squares, as many as there are up to 40.

The legend's sizes: SAMPLES, INSTRUMENTS, PATTERNS, SONGS as STORAGE counts them (their records' bytes); LOOPS
and OTHER the same; KEPT FOR SAVING 40 KB; FREE the footer's figure, the same as STORAGE's free amount. A square
counts a whole KB, so the squares can differ from the legend by a square or two.

Colours: the first line cyan; SAMPLES yellow, INSTRUMENTS light blue, PATTERNS cyan, SONGS green, LOOPS light
green, OTHER light grey, KEPT FOR SAVING magenta, FREE deep light grey; the legend's names light grey, the sizes white,
FREE's in the free-space colours.

### RAM MAP

```
STATUS RAM MAP
RAM 512K, A SQUARE 1K
################################################   48 squares a line, as USER FLASH MAP's
...                                                 (512: 10 lines and 32)
################################
# EFFECTS 147K            # VARIABLES 48K
# EDITS 3K OF 67K         # SPARE 89K
# SCREEN 60K              # DJ 13K
# DX7 28K                 # SYSTEM 37K
# SOUND 28K
DIM: ITS ROOM NOT IN USE NOW
```

The RAM in the order its parts lie: what runs from RAM first, then the effects' lines, the rest of the big
buffers, the variables, the spare zone, the stacks ([../../files/ram.md](../../files/ram.md)).

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the first line | 0, 19 | | T | `RAM 512K, A SQUARE 1K` |
| the map | 0, 33 | as USER FLASH MAP's | | a square for each KB |
| the legend | two columns at x 0 and 120, 8 below the map, 12 a line | | T | a 6 × 6 swatch, the part's name at x + 10, its size after it |
| the last line | 0, 4 below the legend | | T | `DIM: ITS ROOM NOT IN USE NOW` |

The parts:
- **EFFECTS:** the effects' delay lines and the punch-in buffer.
- **EDITS:** the edit memory, 67 KB; the part the unsaved changes take bright (`3K OF 67K`), the rest dim. LOOPS has no RAM of its own: what changed in it since the last SAVE is here,
  as any edit ([loops.md](loops.md#saving)).
- **SCREEN:** the screen's drawing strip and the platform's buffers that send it to the display.
- **DX7:** the DX7's tables, made at power-on.
- **SOUND:** the player's voices, mixes and scopes, the module replay's tables, LOOPS' lanes (~2 KB).
- **VARIABLES:** every other variable of the firmware.
- **SPARE:** the spare zone, dim; **DJ:** the part DJ borrows from its top while it plays, bright
  ([../../files/ram.md](../../files/ram.md#the-edit-memory-and-the-spare-zone)).
- **SYSTEM:** the rest, the firmware's own working memory: the code that runs from RAM (core 2's room with it:
  its code, tables, stacks and queue, [../../second-core.md](../../second-core.md#ram)), the two stacks and their
  guard bands, the store's index and scratch, the screens' scratch, the records kept across a restart, the chip's
  own. One total.

Colours: the first line cyan; EFFECTS green, EDITS yellow (its free part deep yellow), SCREEN light blue, DX7
light red, SOUND light cyan, VARIABLES light grey, SPARE deep light grey, DJ brown, SYSTEM magenta; the legend's
names light grey, the sizes white; the last line dark grey.

Both maps draw in the theme's roles, so every theme shows them ([../colours.md](../colours.md)).

### DEVICE

```
STATUS DEVICE
BATTERY                         2/3 BARS
FIRMWARE            0.1-202610072229 DEV
OUTPUT                            STEREO
MASTER                               50%
SCREEN FRAME               12 FPS  31 MS
UP                              00:00:21
VOICES                                16
THEME                            DEFAULT
SECOND CORE                           ON
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| a line, *i* = 0-8 | 0, 19 + 14 *i* | 236 wide | T | its name; its value right-aligned to x 236 |

The lines: BATTERY (`2/3 BARS`, or `USB POWER`), FIRMWARE (the version, ` DEV` in a dev build),
OUTPUT (`STEREO` / `MONO`), MASTER (`50%`), SCREEN FRAME (the frames a second in 2 digits and ` FPS`, two spaces,
the last frame's time in 2 digits and ` MS`), UP (hh:mm:ss), VOICES (the limit in force), THEME, and SECOND CORE
([../../second-core.md](../../second-core.md)): `STARTING` (the start's first 2 s, SECOND CORE on), `ON` (running),
`OFF` (SETTINGS' SECOND CORE off), or why it stopped for the rest of this start: `STOPPED: NO ANSWER`, `STOPPED:
MISREAD`, `STOPPED: STUCK`, `STOPPED: FAULT`, `STOPPED: AFTER A CRASH`.

Colours: names cyan; BATTERY in the battery's colours, `USB POWER` light blue; SECOND CORE's `STOPPED: ...` red;
the other values light grey.

## Texts

- **The header:** `STATUS` and the page by name, white: `PT/FM`, `PERFORMANCE`, `STORAGE`, `USER FLASH MAP`,
  `RAM MAP`, `DEVICE`.
- **PT/FM:** `PT/FM`; `POCKET TRACKER / FUSION MACHINE`; `BY HITOSU`, the version and ` DEV`;
  `PRESS HOME + LFO FOR HELP`; `SELECT`, `ALGORITHM`; `CPU 1`, `CPU 2`,
  `MAX`, `OFF`, `HALF`, ` US`, `LATE`, `STEALS`, `VOICES`, `MAX`,
  `EDITS FREE`, `FLASH FREE`, `UNSAVED`, `BATTERY`, `UP`, `SONG`.
- **PERFORMANCE:** `NOW`, `AVERAGE`, `MOST`, `CPU 1`, `CPU 2`, `OFF`, `WORST HALF`, `LATE`, `STEALS`, `VOICES`,
  `DX7`, `JUNO`, `TB-303`, `SAMPLE`, `EFFECTS`, `CPU, LAST 30 S`.
- **STORAGE:** `FLASH`, `USER`, ` FREE`, `FILES BY TYPE`, `PATTERNS`, `COUNT`,
  `INSTRUMENTS`, `CARTRIDGES`, `SAMPLES`, `SONGS`, `NOT SAVED`, `CHANGES`, `THEIR RAM`, `EDITS FREE`.
- **USER FLASH MAP:** `FLASH #, A SQUARE 1K`; `SAMPLES`, `INSTRUMENTS`, `PATTERNS`, `SONGS`, `LOOPS`, `OTHER`,
  `KEPT FOR SAVING`, `FREE`.
- **RAM MAP:** `RAM #, A SQUARE 1K`; `EFFECTS`, `EDITS`, `# OF #`, `SCREEN`, `DX7`, `SOUND`,
  `VARIABLES`, `SPARE`, `DJ`, `SYSTEM`; `DIM: ITS ROOM NOT IN USE NOW`.
- **DEVICE:** `BATTERY`, ` BARS`, `USB POWER`, `FIRMWARE`, `OUTPUT`, `MASTER`, `SCREEN FRAME`, ` FPS`, ` MS`,
  `UP`, `VOICES`, `THEME`, `SECOND CORE`, `STARTING`, `ON`, `OFF`, `STOPPED: NO ANSWER`, `STOPPED: MISREAD`,
  `STOPPED: STUCK`, `STOPPED: FAULT`, `STOPPED: AFTER A CRASH`.
- **The footer's right side:** `FREE 237K`.
- **Toasts:** `COUNTS ZEROED` (LFO).

## Help

On STATUS the shared pages come first ([../help.md](../help.md)); their first page carries the help's
own last line, `PRESS HOME TO GET BACK`. Then STATUS's own page, as the help shows it (`{...}` a key,
`[...]` a screen):

```
[STATUS]: HOW THE FM-1 IS DOING
{SELECT} OR {ALGORITHM}: THE PAGES
  PT/FM, PERFORMANCE, STORAGE,
  USER FLASH MAP, RAM MAP, DEVICE
{LFO}: ZERO MAX, HALF, LATE, STEALS
{ENV} (EXPLORE NAMED VAULTS): SONGS
  {PLAY} THERE: PLAY ONE, IN [SONGS]
{EDIT}: [SETTINGS]
CPU 1, CPU 2: HOW BUSY THE SOUND KEEPS
  EACH OF THE TWO PROCESSORS
HALF: THE SLOWEST SOUND WORK, IN
  MICROSECONDS; OVER 5804: TOO SLOW
LATE: SOUND NOT READY IN TIME: CLICKS
STEALS: NOTES PUSHED OUT, VOICES BUSY
VOICES: NOW / THE LIMIT; MAX: THE MOST
EDITS FREE: MEMORY LEFT FOR EDITS
RED: WORTH A LOOK
```

# VISUAL

Status: **settled** (2026-10-09). The record: [the UI proposal](../../proposals/202610072229-ui.md),
sections 6.7 and 13.7.

VISUAL shows the sound: the voices, the output's wave and its spectrum. It is in the HEAR group; HOME
+ the black key marked PIT goes to it ([../navigation.md](../navigation.md)).

## The visualisers

**SELECT** turns the visualisers as pages; **SEQ** cycles them:

| Visualiser | What |
|---|---|
| **VOICES** | a scope for each voice |
| **SCOPE** | the master output as an oscilloscope: left and right, triggered on a rising zero crossing |
| **WAVE** | the master's waveform scrolling by, as peaks over the last several seconds |
| **SPECTRUM** | the master's spectrum as bars on a log frequency scale |
| **LEVELS** | a meter for each track |
| **FOMA** | the mascot: a little FM-1 with a cat's ears flying round the screen, its rainbow tail waving with the sound ([below](#foma)) |

- **K1:** time / zoom. **K2:** gain. **GLO:** options (stereo or mono view, freeze).
- **The cost:**
  - computed in the main loop from a copy of the latest audio, never in the audio interrupt;
  - the spectrum is an integer FFT of 256-512 points a frame, with no floating point.
- **SELECT** takes one visualiser a step and stops at both ends; **SEQ** goes to the next and wraps
  from the last to the first. The header names it, white after `VISUAL` (`VISUAL SCOPE`).
- **K1, ZOOM 1-6:** SCOPE's window across the screen, 128, 256, 384, 512, 768 or 1,024 samples (3 at
  first). **K2, GAIN** ×0.125 to ×16 in doublings (×1 at first): every visualiser's height; SPECTRUM's
  bars rise 4 dB a step.
- **GLO, the options** (`VISUAL OPTIONS`, as properties: [../dialogs.md](../dialogs.md)): VIEW
  STEREO / MONO; FREEZE OFF / ON (the picture holds still).
- **The footer's right side:** the visualiser's name, and ` FROZEN` while frozen.

## Controls

| Control | Does |
|---|---|
| SELECT | turns the visualisers |
| SEQ | the next visualiser |
| K1 | zoom (time) |
| K2 | gain |
| GLO | the options |
| ARP | the arpeggiator, as on every screen but the EDITOR and LOOPS ([../controls.md](../controls.md)) |
| everything else | as on every screen ([../controls.md](../controls.md)) |

## Layout

The mockups are on the T grid (40 × 20 characters of 6 × 12 px); the header and footer are the
frame's ([../frame.md](../frame.md)). Every visualiser fills the body, 240 × 210 px from y 17.

### VOICES

```
VISUAL VOICES                          S, the frame's header: the name, the view white
+--------++--------++--------++--------+
|T00  C-4||T03  E-3||KEY  G-4||--      |
|        ||        ||        ||        |
| /\  /\ ||_/\_/\_||  /\/\  ||________|
|/  \/  \||        ||\/  \/ ||        |
+--------++--------++--------++--------+
+--------++--------++--------++--------+
|T05  A-2||--      ||--      ||--      |
   ...                                    3 rows at 12 voices
01:00 EL PIANO DX7                VOICES
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| a voice's tile, *i* = 0 to VOICES − 1 (the limit in force: [settings.md](settings.md#voices)) | 60 (*i* mod 4), 17 + *th* ⌊*i* / 4⌋ | 59 × (*th* − 1) | | a box; *th* = ⌊210 / (VOICES / 4)⌋: 105, 70 or 52 px (a parked note, unheard, has no tile) |
| – who plays it | tile + 3, tile + 2 | | T | `T0A` (a song's track), `KEY` (the keys), `L03` (a loops' track); `--` when free |
| – its note | tile + 38, tile + 2 | | T | `C-4` |
| – its scope | tile x + 2 to + 57; centred at tile y + 14 + (*th* − 16) / 2 | 56 points, ± (*th* − 18) / 2 | | the voice's latest wave: its last 64 frames (the block the player keeps of each voice, 1.45 ms) across the 56 points |

Colours: the boxes dark grey; who plays it and the scope in the engine's colour; the note light grey;
a releasing voice's note and scope dark grey; `--` dark grey.

### SCOPE

```
VISUAL SCOPE
L
     /\        /\        /\        /\
----/--\------/--\------/--\------/--\--   the left channel, the top half
   /    \    /    \    /    \    /    \
R
     /\        /\        /\        /\
----/--\------/--\------/--\------/--\--   the right channel, the bottom half
   /    \    /    \    /    \    /    \
01:00 EL PIANO DX7                 SCOPE
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| a trace | 0, the half's top | 240 × 105 (stereo, two halves) or 240 × 210 (mono) | | the wave, a point a column |
| – its centre line | 0, the half's middle | 240 × 1 | | |
| – `L`, `R` | 2, the half's top + 2 | | T | stereo only |

The trace starts at the left channel's first rising zero crossing; the window (K1) is spread over the
240 columns; the height is the sample × the gain × 2, clipped 2 px inside its half. In MONO one trace
shows (left + right) / 2.

Colours: the left trace (and the mono one) light green, the right light cyan; the centre lines deep
dark grey; `L` and `R` dark grey.

### WAVE

```
VISUAL WAVE

                         |  |||| || |||||
          | ||  ||| |||||||||||||||||||||
----------------------------------------    the centre
          | ||  ||| |||||||||||||||||||||
                         |  |||| || |||||
                                        ^ the newest, at the right edge
01:00 EL PIANO DX7                  WAVE
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| a column | from the right edge leftwards, up to 240 | 1 × 2 *h* | | the peak of a frame's audio: *h* = min(1, peak × gain × 1.5) × 101, centred at y 122 |
| the centre line | 0, 122 | 240 × 1 | | |

A column is added each screen frame; the oldest leaves at the left.

Colours: the newest column white, the older ones brown; the centre line deep dark grey.

### SPECTRUM

```
VISUAL SPECTRUM

     #
    ###   #
   #####  ##  #
  ######## ####  #       #
 ##################### ####  #    #
########################################
30                 700                18K
01:00 EL PIANO DX7              SPECTRUM
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| a bar, *b* = 0-39 | 6 *b*, up from y 215 | 5 × up to 194 | | the loudest bin from 30 · 600^(*b*/40) to 30 · 600^((*b*+1)/40) Hz: (dB + 90 + 4 × the gain's step) / 80 of the height |
| `30` | 0, 216 | | T | |
| `700` | 108, 216 | | T | |
| `18K` | 210, 216 | | T | |

Colours: bars 0-11 light red (the bass), 12-25 yellow, 26-39 light cyan; the labels dark grey.

### LEVELS

```
VISUAL LEVELS
 .  .  .  .  .  .  .  .  ...  .     16 meters, 15 px apart
 .  #  .  .  .  .  .  .  ...  .
 #  #  .  #  .  .  .  .  ...  .
 #  #  #  #  .  #  .  .  ...  .
00 01 02 03 04 05 06 07  ... 0F
01:00 EL PIANO DX7                LEVELS
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| track *t* = 0-15's meter | 15 *t* + 2, 19 | 11 × 190 | | the ground |
| – its level | 15 *t* + 2, up from y 209 | 11 × min(1, level × gain × 4) × 190 | | |
| – its number | 15 *t* + 1, 214 | | T | `0A` |

Colours: the ground deep dark grey; the level green, red above 85 % of the height, dark grey while
the track is muted; the numbers cyan, dark grey while muted.

### FOMA

PT/FM's mascot, FOMA: a little FM-1 with a cat's ears, flying through the night as Nyan Cat does. Its body is
the box, its face the screen (two eyes, a small mouth), a row of keys on its belly, four legs under it.

```
VISUAL FOMA
   +         .          *         .       stars going by to the left: a dot, a cross, a star
==========================  /\    /\      the tail: six stripes from its back to the left edge
==========================[  o   o ]     FOMA: 16 x 12 cells of 3 x 3 px, always facing right
==========================[##_#_##]
     .          +              .
01:00 EL PIANO DX7                     SCORE: 2
```

- **It moves as a DVD player's logo does,** always facing right: straight on (2 px a frame across, 1.25 down, a
  frame each 33 ms), bouncing off the body's edges; it takes the next colour at each bounce (light red, yellow,
  light green, light cyan, light blue, light magenta). **A corner** (both edges at once) throws sparkles round
  it and `CORNER!` in the middle for 1.5 s; the footer's right side is its score, the corners hit (`SCORE: 2`),
  and says nothing before the first (not even the view's name).
- **Its tail** is a rainbow straight back from it to the left edge, waving as Nyan Cat's does, and moving with
  it whichever way it goes: six stripes, top down light red, brown, yellow, light green, light blue, light
  magenta, in columns of 3 px. Its upper edge is the left channel's wave and its lower edge the right's, as SCOPE
  takes them (from the left channel's rising zero crossing, ZOOM's window: K1 sets how many waves fit along it),
  from its back on: a column every 3 px of SCOPE's 240, smoothed so a busy wave (hats, noise) never tears it
  (each column the mean of the 17 points round it, then a quarter of each neighbour's and half its own), so the
  kick's and the bass's swing shows. It is held at its back: the swing grows from none there to whole over its
  first 8 columns (24 px), so it leaves the box's top and bottom rows. Both edges move the same way, ±9 px at the louder
  channel's peak, round the box's top and bottom rows (so a mono sound snakes the whole rainbow, as thick as the
  box, and stereo pinches and widens it); near silence (under 1,024 of 32,768) leaves it straight. The six
  stripes share what lies between (6-42 px). **Its shape is taken anew once a step:** a row while something plays,
  each 4 frames otherwise; in between it keeps its shape (SCOPE's wave is taken then only, on this view).
- **The stars** go by to the left, 3 px a frame, as if it flew right: 24 of them, one in each of 6 columns × 4
  bands of the body, at a place in it by chance (so they are spread evenly), a new height each time round.
  Each is a dot, a cross or a star (cells of 2 × 2 px), on the beat: a star on its beat's first row, a cross on
  the next, a dot on the two after (4 rows a beat). Half of them, a checkerboard of the 6 × 4 cells, are half a
  beat later (2 rows), so the sky flashes on the beat and again on the half beat. With nothing playing they step
  each 4 frames (a "beat" of ~0.5 s).
- **It lives:** its legs paddle (a step each 4 frames), its eyes blink every 3 s and shut at each bump (a wall:
  8 frames, ~0.27 s), and while something plays it
  hops 3 px on each beat's first ticks, its tail with it.
- **FREEZE** holds it still with the rest; GAIN and the stereo option change nothing here.
- **K4 lifts it or drops it** (clockwise up): a turn (24 clicks) is the body's height, 210 px (8.75 px a
  click), within the body; it flies on from there as ever, the nudge only a moment's change. A touch kept for
  those who find it: the help never mentions it.
- **The cost:** only the main loop's drawing (a frame each 33 ms, as every view), never the sound's.

Colours: the night black; a star's middle white (a dot's light grey), its arms light grey, a star's inner arms
white; FOMA's box its colour of the moment, its ears' insides and mouth light magenta, its screen dark grey, its
eyes and white keys white, its black keys black, its legs light grey; the tail's stripes as above; the
sparkles the box's colours in turn; `CORNER!` yellow in S. These are roles, as everywhere: in a light theme
([../colours.md](../colours.md#themes-settings--theme)) FOMA flies by day, the sky the theme's light ground, the
stars and its eyes dark.

### The options (GLO)

```
 +------------------------------------+
 |VISUAL OPTIONS                      |
 |[VIEW                       STEREO] |
 | FREEZE                        OFF  |
 +------------------------------------+
```

The dialogs' properties ([../dialogs.md](../dialogs.md)): a panel at 6, 21, 228 × 52; items at y 40
and 54. K1 switches the selected one.

## Texts

- **The header:** `VISUAL` and the view, white: `VOICES`, `SCOPE`, `WAVE`, `SPECTRUM`, `LEVELS`, `FOMA`.
- **FOMA:** `CORNER!`.
- **VOICES:** `T00`-`T0F`, `KEY`, `L00`-`L0F`, `--`; the notes (`C-4`).
- **SCOPE:** `L`, `R`.
- **SPECTRUM:** `30`, `700`, `18K`.
- **LEVELS:** `00`-`0F`.
- **The options:** `VISUAL OPTIONS`; `VIEW` (`STEREO`, `MONO`), `FREEZE` (`OFF`, `ON`).
- **The footer's right side:** `VOICES`, `SCOPE`, `WAVE`, `SPECTRUM`, `LEVELS`; on FOMA `SCORE: 2` (nothing before
  its first corner); and ` FROZEN`.
- **In the header's place while a knob turns** (1.2 s, [../frame.md](../frame.md#the-header)): `K1 ZOOM 3` (1-6) or
  `K2 GAIN X1` (`X0.13`, `X0.25`, `X0.5`, `X1`, `X2`, `X4`, `X8`, `X16`).

## Help

As the help shows it ([../help.md](../help.md)): `{...}` a key, `[...]` a screen. The help draws the
last line, `PRESS HOME TO GET BACK`, itself. The shared pages follow.

```
[VISUAL]: SEE THE SOUND
{SELECT} OR {SEQ} (SHOW ELSE, QUICK):
  THE VIEWS, ONE AFTER ANOTHER
  VOICES: A SCOPE FOR EACH VOICE
  SCOPE: THE OUTPUT WAVE, LEFT, RIGHT
  WAVE: THE LAST SECONDS, SCROLLING
  SPECTRUM: BASS LEFT, TREBLE RIGHT
  LEVELS: A METER FOR EACH TRACK
  FOMA: INTO THE STARS
{K1}: ZOOM IN TIME, ON SCOPE
{K2}: MAKE IT TALLER OR FLATTER
{GLO}: STEREO OR MONO VIEW, FREEZE
{ARP} (AGAIN, REPEAT, PLEASE): ARPEGGIO
  HOLD {ARP}: RHYTHM, ORDER, LATCH
```

# Navigation

Status: **settled** (2026-10-09). The record: [the UI proposal](../proposals/202610072229-ui.md),
sections 1.4 and 5.

What the screens act on (the current song, pattern and instrument), which screens there are, how the
black keys and HOME's combination move between them, what plays as you go, and the jumps that take
the EDITOR deep. HOME's combination as a control is in [controls.md](controls.md#homes-combination).

## Current song, pattern and instrument

- **The current song** is the song under the cursor on SONGS. PLAY on SONGS plays it, and so does PLAY
  on the other screens when nothing has played yet ([transport.md](transport.md)).
- **The current pattern** is the one the EDITOR shows: its own pattern, or a numbered one
  ([screens/editor.md](screens/editor.md#the-editors-own-pattern)). It is set by ENV on the EDITOR, by EDIT
  PATTERN on a song position or a FUSION instrument's edit page (their SEL menus), by EDIT on PLAYBACK, and by
  the EDITOR's NEW PATTERN, LOAD A PATTERN and STORE IN A PATTERN. SAVE keeps it: at power-on it is the one
  kept, the EDITOR's own at first ([../files/settings.md](../files/settings.md)).
- **The current instrument** is set by PRESETS, and by the cursor on INSTRUMENTS. The footer shows it
  on every screen ([frame.md](frame.md#the-footer)). SAVE keeps it: at power-on it is the one kept, F0:00
  (the first built-in instrument) at first ([../files/settings.md](../files/settings.md)).
- Switching screens keeps all three.

## The screens

| Key | Screen | Group | For |
|---|---|---|---|
| OP1 | **SONGS** ([screens/songs.md](screens/songs.md)) | MAKE | songs: the list and their order of patterns |
| OP2 | **EDITOR** ([screens/editor.md](screens/editor.md)) | MAKE | editing patterns |
| OP3 | **INSTRUMENTS** ([screens/instruments.md](screens/instruments.md)) | MAKE | cartridges and instruments: choose, try, edit |
| OP4 | **PLAYBACK** ([screens/playback.md](screens/playback.md)) | PLAY | what plays, following it; not editable |
| OP5 | **DJ** ([screens/dj.md](screens/dj.md)) | PLAY | live playing: two songs on two decks, in time, mixed with a crossfader; a playback of its own |
| OP6 | reserved | HEAR | kept for a screen later |
| PIT | **VISUAL** ([screens/visual.md](screens/visual.md)) | HEAR | visualisers |
| GLO | **STATUS** ([screens/status.md](screens/status.md)) | HEAR | the start screen: PT/FM, its version, how to get help; performance, storage, the device |
| MONO | **SETTINGS** ([screens/settings.md](screens/settings.md)) | SYSTEM | global settings and tools |
| POLY | **LOOPS** ([screens/loops.md](screens/loops.md)) | (SYSTEM's key, PLAY's colour) | live playing: a loop station, loops stacked over one length; a playback of its own |
| (blank) | none | | kept for later |
| FX button | **FX** ([screens/fx.md](screens/fx.md)) | PLAY | global effects, punch-in effects, the metronome, the mixer: its own button, no black key |

- **There is no manual screen.** Help comes on every screen: HOME + LFO, "LOOK FOR ONSWERS"
  ([help.md](help.md)).
- **The groups follow the gaps between the black keys** (3, 2, 3, 2), so the keys can be found by
  touch:
  - **MAKE:** the hierarchy from song down to sound;
  - **PLAY:** what plays, and played live: PLAYBACK and DJ (FX, the effects played on it, by its own
    button);
  - **HEAR:** what comes out, seen and measured, with room for one more screen;
  - **SYSTEM:** the settings, and LOOPS beside them;
  - **the blank key,** alone at the end: no screen, kept for later.

  LOOPS' name is in the PLAY group's colour.

  Each screen's name in the header is in its group's colour ([colours.md](colours.md)).
- **HOME + black key:** go to that screen ([controls.md](controls.md#homes-combination): HOME tapped,
  then the key; or HOME held).
  - While HOME's combination is open, the screen keys light dim and the current one bright
    ([leds.md](leds.md)).
  - A black key with no screen (OP6, the blank key) does nothing. FX has no black key: its own button
    opens it, and it is in the round.
- **HOME + SELECT: the round,** a screen a click, wrapping both ways:

  STATUS → SONGS → EDITOR → INSTRUMENTS → PLAYBACK → VISUAL → FX → LOOPS → DJ → SETTINGS → STATUS …

  The start screen comes first and SETTINGS last; between them the order of making, playing and hearing
  (FX beside LOOPS and DJ). It is the round's own order: the black keys keep theirs (above). A screen
  given to OP6 or the blank key later takes a place in it.
- **What plays plays on** from screen to screen, until something else starts
  ([transport.md](transport.md#play)): a song or the EDITOR's pattern loop plays on into LOOPS until the
  loops start there; the loops play on outside LOOPS until a song or a pattern starts. Leaving LOOPS
  ends a take as REC does, disarms an armed one and ends a song's recording ([screens/loops.md](screens/loops.md)). DJ's decks are one more thing that
  plays: a song playing when DJ opens moves onto its main deck, playing on; the pattern loop or the loops
  play on into DJ until a deck starts; the decks play on outside DJ until a song, a pattern or the loops
  start ([screens/dj.md](screens/dj.md)).
- **FX from LOOPS does not leave LOOPS:** a take or a song's recording goes on, FX works on LOOPS' own
  setup, and FX again goes back ([screens/fx.md](screens/fx.md)); going on elsewhere from that FX leaves LOOPS then.
  FX from DJ does not leave DJ either: it works on DJ's own setup.
- **FX tap** goes to FX; pressed again, it goes back to the screen it came from.
- **The screen at power-on** is STATUS, the start screen, unless SETTINGS → POWER-ON SCREEN says
  otherwise: LAST (the screen used last) or any screen.

## Jumps into the EDITOR

- **They go deep:**
  - EDIT on PLAYBACK: the playing pattern, with the cursor on the playing row;
  - SEL → EDIT PATTERN on a song position (in its order): that position's pattern;
  - SEL on a FUSION instrument's PATTERN, on its edit page: its pattern.

  EDIT stays lit while you are there, and HOME tap returns to where you came from, still in the order
  or on the edit page. Inside, EDIT still opens the note editor.
- **One of them is locked:** a FUSION instrument's pattern
  ([screens/instruments.md](screens/instruments.md)).
  - HOME + black key and FX tap are refused, and EDIT flashes.
  - The way out is HOME tap, back to the instrument's edit page.
  - From PLAYBACK and SONGS, HOME + black key may leave for another screen; the deep level is then
    dropped.

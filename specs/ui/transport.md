# The transport

Status: **settled** (2026-10-09), revised 2026-10-24 and 2026-11-03. The records: [the UI
proposal](../proposals/202610072229-ui.md), section 7; LOOPS' parts, [the loop station
proposal](../proposals/202610240236-loops-station.md) and [the stage proposal](../proposals/202611030330-loops-redesign.md).

PLAY, what it plays on each screen, tempo, rewinding and stopping, and PLAY's two LEDs. There is one
transport for the song, the EDITOR's pattern loop, the loops and DJ's decks. LOOPS' own playback (the
loops and their stage, the play position, the arm) is in [screens/loops.md](screens/loops.md), DJ's decks (each with a
PLAY key of its own) in [screens/dj.md](screens/dj.md), the EDITOR's takes in
[screens/editor.md](screens/editor.md).

## PLAY

- **PLAY** (acts on press) plays or pauses. There is one transport, and one thing plays at a time:
  the current song, the EDITOR's pattern loop, the loops, or DJ's decks.
  - **On SONGS, the EDITOR, LOOPS and DJ, PLAY plays the screen's own thing:** the current song (from the
    position ALGORITHM boxed, [screens/songs.md](screens/songs.md#where-play-starts)), the shown pattern looped
    from the cursor's row, the loops from the play position, with loops or none
    ([screens/loops.md](screens/loops.md#the-play-position-play-and-jumps)), both decks together
    ([screens/dj.md](screens/dj.md)).
    - If something else is playing, PLAY switches at once. What was playing pauses and keeps its
      place.
    - On DJ, PLAY with something else playing only pauses it, and the next PLAY plays the decks: DJ stays
      apart until it is played, and its first start cuts the sound of what played
      ([screens/dj.md](screens/dj.md)).
    - If the screen's own thing is already playing, PLAY pauses it; on LOOPS it stops the loops and takes the play
      position back to the start (the stage kept), so PLAY's red is then dim.
    - On the EDITOR, a song that is playing the shown pattern hands over to the loop on the same row,
      so the switch is seamless.
    - On the EDITOR with REC on, PLAY pauses the EDITOR's loop if it plays (REC on while it plays, the
      loop plays on and the keys write step by step); else it records a take, which goes round the
      pattern until PLAY or REC stops it ([screens/editor.md](screens/editor.md#realtime-takes)). The
      EDITOR's ARP, its row preview, stops whatever plays before it starts.
    - In LOOPS PLAY plays the loops with no loops too: the length goes round, the stage filling as the keys play
      over it ([screens/loops.md](screens/loops.md#the-length)); what played on into LOOPS pauses. With the loops
      armed (REC with the stage empty), PLAY starts them after LOOPS' COUNT-IN ([below](#count-in)), from the play
      position ([screens/loops.md](screens/loops.md#rec-edit-and-the-arm)). While a song is armed or recorded there,
      PLAY is RECORD LIVE's ([screens/loops.md](screens/loops.md#record-live)).
  - **On every other screen, PLAY pauses and resumes what played last** (the song, the pattern loop,
    the loops or the decks), so on FX or VISUAL it toggles what you hear; the loops, when they played last, with
    loops or none. If nothing has played yet, it plays the current song.
  - **What plays plays on** from screen to screen until something else starts: going to another
    screen never pauses it ([navigation.md](navigation.md#the-screens)).
  - **What plays hears every edit at once:** a cell changed is heard from the next row, an instrument
    changed from its next note, whatever plays them (the song, the EDITOR's loop, the loops, a DJ deck);
    nothing waits for a pattern to start again.
  - Each of the four continues from where it was paused, else starts from the beginning.
  - **A start past the beginning runs through the rows before it first:** their instruments, parameters,
    mixer and FX commands, and the song's jumps and loops, 64 rows each 5.8 ms (a song from its fourth
    position, ~40 ms). Its notes start once there, or 5.8 ms later when that last stretch was long, so the
    work of the run-through and of the first notes never meets in one audio buffer. A DJ deck armed for
    its start is ready at once.
  - **Switching songs rewinds:** another song starts from its beginning, and the one left goes back to
    its start; only the song played last keeps its place. The song, the pattern loop and the loops keep
    theirs against each other.
  - **No song to play** (every song deleted, or the song has no positions): PLAY is refused. It
    blinks quickly and says `NO SONG TO PLAY`.
- **In an overlay that plays** (the pattern list), PLAY is the overlay's: it previews the highlighted
  pattern ([dialogs.md](dialogs.md#the-pattern-list)).

## COUNT-IN

Before a recording starts, a count-in: the EDITOR's realtime takes ([screens/editor.md](screens/editor.md#realtime-takes))
and LOOPS' armed loops and song recordings that PLAY starts ([screens/loops.md](screens/loops.md#rec-edit-and-the-arm)).
- **Where it is set:** the EDITOR's in SETTINGS → COUNT-IN, for every take (saved with the settings);
  LOOPS' own in its properties (GLO), kept with the loops.
- **The EDITOR's choices:** OFF; 1 BAR; 2 BARS; **KEY**: nothing counts, the take is armed and starts with
  the first key pressed. It starts at 1 BAR.
- **LOOPS' choices:** OFF, 1 BAR, 2 BARS, before PLAY starts armed loops; there is no KEY, as a key always starts
  armed loops at once, with its note the stage's first. It starts at OFF.
- **Always 4 beats a bar,** at the tempo of what is recorded (the song's, the loops'), **with a click of
  its own:** BEEP, the bar's first beat higher, at a fixed level (60 %), whether the METRONOME is on or
  off.
- **A big count** shows over the screen ([below](#the-count-ins-box)); REC blinks.
- **PLAY or REC** during the count stops it, nothing recorded; so does HOME on LOOPS. On LOOPS that
  disarms the loops (`CANCELLED`), REC sending nothing even with notes on the stage. Armed (the EDITOR's KEY), HOME,
  PLAY or REC cancel the take (`TAKE CANCELLED`); keys pressed during a count are heard, not written.

### The count-in's box

```
          +----------------------+
          |       COUNT-IN       |      T, red
          |                      |
          |          ##          |      the beats left (4, 3, 2, 1), 32 x 64
          |         ###          |
          |          ##          |
          |          ##          |
          |         ####         |
          |    [#] [#] [ ] [ ]   |      a dot a beat
          |      BAR 1 OF 2      |      T
          +----------------------+
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the box | 50, 63 | 140 × 118 | | black, a red outline |
| the title | centred, 69 | | T | `COUNT-IN` |
| the count | centred, 83 | 32 × 64 | S's glyphs at 4× | the beats left in the bar: `4` on its first beat, then `3`, `2`, `1` |
| the beats | 95 + 14 × *i*, 155 (*i* = 0-3) | 8 × 8 each | | filled |
| the bar | centred, 167 | | T | `BAR 1 OF 2` |

Armed (the EDITOR's KEY): the same box with `TAKE ARMED` (S, at y 75), `PLAY` (L, y 99), `A NOTE` (S, y 137) and
`HOME: CANCEL` (T, y 165), centred.

Colours: the outline and the title red; the count white, yellow on the bar's first beat; the beats
sounded white (the first yellow), the rest dark grey; the bar light grey. Armed: `TAKE ARMED` red,
`PLAY` and `A NOTE` white, `HOME` yellow and `: CANCEL` light grey.

## Tempo

- **A song starts at its TEMPO** (a property, SONGS → GLO).
- **A TEMPO command** (a global CMD, from any track) changes it from its row on.
- **`TEMPO 00`** returns to the song's own tempo.
- **A pattern played on its own** (from the EDITOR) uses the current song's tempo, and each pass
  of the loop starts at it again.

## Rewinding and stopping

- **HOME + PLAY** (HOME tapped, then PLAY; or held) rewinds what plays, or played last, to its start:
  the song, the pattern or the loops. If it is playing, it restarts from there. The toast:
  `BACK TO THE START`. DJ's decks go back to their cue points, paused (`BACK TO THE CUES`).
- **PLAY held** past the hold delay stops: its press has paused (or started) what plays, and holding
  on takes it back to its start (`BACK TO THE START`), so the next PLAY plays it from the beginning
  (on the EDITOR, from the cursor's row, as always there; DJ's decks go back to their cue points). With
  nothing played yet, holding does nothing more. On the EDITOR with REC on, PLAY held is the take's instead
  ([screens/editor.md](screens/editor.md)).
- **At the song's end:** it loops or stops, as the song's property says; a song PLAY started on SONGS ends as
  SONGS' play mode says (SEQ: ONE PLAY, REPEAT SONG, PLAY NEXT), on every screen, until something else plays
  ([screens/songs.md](screens/songs.md#the-play-modes)).
- **All sound off:** HOME + REC lets every voice go at once, whatever plays
  ([controls.md](controls.md#what-each-button-means)).

## PLAY's two LEDs

| State | Green | Red |
|---|---|---|
| at the start (after power-on, HOME + PLAY while paused, or LOOPS' PLAY stopping the loops): nothing to resume | off | dim |
| playing a song, or DJ's decks (on the main's beats) | on, brighter on each beat | off |
| playing a loop (the EDITOR's pattern) | on, brighter on each beat | dim |
| playing the loops (LOOPS) | a 100 ms flash on each beat but the bar's first; else dark | a 100 ms flash on the bar's first beat; else dark |
| paused | off | on |
| not playing, on a screen where PLAY has nothing to play (it would refuse) | off | off |

Lit together, the two read as amber where they blend, or as green beside a dim red where they do not:
either way a loop looks unlike a song.
- **At the start, red dim** says PLAY works here, as every dim light does ([leds.md](leds.md)); it
  cannot be taken for paused (red full) or a loop playing (green with it).
- **The loops playing, PLAY is a visual metronome:** a flash on each beat the METRONOME of LOOPS' FX setup counts
  ([screens/fx.md](screens/fx.md#metronome)), whether it is on or off: red on the bar's first beat, green on the
  others, each a quick flash of 100 ms at full (the other LED dark meanwhile); between the flashes both are dark. Its
  TEMPO and BEATS A BAR say where the beats are: at FOLLOW the loops' own beats (each GRID's rows, the bar's first the
  bar's start), at a number its own tempo. On, the flashes come with its clicks. Only while the loops play: stopped,
  PLAY's LEDs are as at the start; a song, DJ or the EDITOR's pattern playing, as above.
- **Dark where PLAY has nothing to play,** paused or not: SONGS on a song with no positions; the screens
  without a PLAY of their own when what played last, else the current song, has no positions. PLAY then blinks and
  says why ([Texts](#texts)). On LOOPS its red is never dark while they do not play: the loops play with none.
- **REC:** lit = writing on (EDITOR), or a song recording or notes on a stage that is on, which REC sends (LOOPS);
  blinking = a take is recording (EDITOR), or the loops armed, a song armed or a count-in (LOOPS)
  ([leds.md](leds.md)).
- **The header** shows the same state as a symbol: ▶ green while playing, ❚❚ red while paused, ● red
  while recording ([frame.md](frame.md#the-header)).

## Texts

| Text | Where | When |
|---|---|---|
| `NO SONG TO PLAY` | toast, PLAY blinking | PLAY with no song to play |
| `BACK TO THE START` | toast | HOME + PLAY; PLAY held |
| `SOUND CUT: LET GO OF REC` | toast | CUT: HOME + REC ([controls.md](controls.md#cut)) |
| `SOUND CUT, EFFECTS OFF` | toast | CUT held 2 s: every effect switched off ([controls.md](controls.md#cut)) |
| `COUNT-IN`, `BAR 1 OF 2`, `4`-`1` | the count-in's box | a count-in |
| `TAKE ARMED`, `PLAY A NOTE`, `HOME: CANCEL` | the count-in's box | the EDITOR's COUNT-IN KEY, armed |
| `TAKE CANCELLED` | toast | the EDITOR: an armed take cancelled |
| `CANCELLED` | toast | LOOPS: a count-in or the loops' arm cancelled |

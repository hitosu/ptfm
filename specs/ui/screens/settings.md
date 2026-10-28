# SETTINGS

Status: **settled** (2026-10-09), revised 2026-10-24. The records: [the UI proposal](../../proposals/202610072229-ui.md),
sections 6.9 and 13.9; QUANTIZE, [the loop station proposal](../../proposals/202610240236-loops-station.md),
section 5.

SETTINGS holds the global settings and the tools. It is the SYSTEM group's screen; HOME + the black
key marked MONO goes to it ([../navigation.md](../navigation.md)). The settings are kept in the
settings record ([../../files/settings.md](../../files/settings.md)).

**Two pages,** turned with **SEQ** (round) or **ALGORITHM** (the header's `SEQ 1/2`, as on FX): the
list, and **MIDI** (the header `SETTINGS MIDI`): MIDI in and out, the clock and MIDI learn
([below](#the-midi-page); what they do: [../../connections.md](../../connections.md)).

## The list

A list. **SELECT** picks an item, **K1** changes it, **LFO** resets it to the default.

| Item | Values | Default |
|---|---|---|
| OUTPUT | the analog out's, not USB audio's: STEREO; MONO (for the internal speaker, or a mono jack); SYNC PULSE L or SYNC PULSE R: a click on that side at every half beat, for instruments that follow a sync pulse (Volca, Pocket Operator), the music in mono on the other side; with SYNC INTERNAL ([../../connections.md](../../connections.md#clock)) | **STEREO** |
| LINE GAIN | the level of the analog out, the speaker and the jack, with MASTER: −24 to +12 dB, in 1 dB steps | 0 dB |
| USB GAIN | the level of USB audio, which MASTER does not touch: −24 to +12 dB, in 1 dB steps | 0 dB |
| VOICES | 8 / 12 / 16: the notes heard at once, everywhere (DJ too); 16 with the second core (6 of them on it, voices 11-16), 12 while it does not run ([VOICES](#voices)) | 16 |
| SECOND CORE | ON / OFF: the effects and the output stage worked out on the chip's second core, beside the voices on the first ([../../second-core.md](../../second-core.md)); OFF: one core does it all. Applies from the next audio half, both ways; STATUS shows each core's load and, on DEVICE, why the second stopped if it did | ON |
| THEME | DEFAULT, EGA, PASTEL, NEON, SUNSET, ICE, CLEAR, GREEN, AMBER, WHITE, DAYLIGHT, PAPER, MANUAL, SILVER, LCD, COMMANDER ([../colours.md](../colours.md)) | DEFAULT |
| KEYS VOLUME | the volume of notes from the keys (the keys have no velocity): `01`-`7F` | `60` |
| KEYS GAIN | the level of the keys as heard while playing them, the FM-1's keys and MIDI's notes alike: −24 to +12 dB, in 1 dB steps. Only what is heard: a note recorded (the EDITOR's takes, LOOPS) keeps its volume (KEYS VOLUME, a MIDI note's velocity) and plays back as written, without it; during a LOOPS take the keys sound through the recording loop's mix instead ([loops.md](loops.md#recording)). FX's MIXER shows it too, as the KEYS column's LVL, beside the keys' PAN, DUCK amount and sends ([fx.md](fx.md#mixer)) | 0 dB |
| KEYS | what the effects do to the keys as heard (the FM-1's, MIDI's notes, the arpeggiator's, the previews): THROUGH FX, through the effects as everything else; DRY, past every effect, the LIMITER and the punch-ins too, joining the mix just before the output stage ([fx.md](fx.md#the-keys-dry)), so the effects work on what plays in the background alone. One setting for every song, the loops and DJ (where the keys play no notes); during a LOOPS take the keys go through the recording loop's mix and LOOPS' effects whatever it says; FX's GLO shows it too | THROUGH FX |
| KEYS OCTAVE | the default octave: -4 to +4 | 0 |
| KNOB SPEED | acceleration: OFF, LOW, HIGH | LOW |
| HOLD DELAY | 300-1500 ms, in steps of 50 (layers, solo) | 500 ms |
| COMBO GRACE | 0-250 ms, in steps of 10: how long a released modifier still counts ([../controls.md](../controls.md)) | 100 ms |
| PLAYBACK SCROLL | ROWS / PAGES | ROWS |
| PRESETS LIST | ON / OFF: PRESETS' instrument list over the screen while it turns ([../dialogs.md](../dialogs.md#presets-instrument-list)) | ON |
| COUNT-IN | before the EDITOR's takes: OFF, 1 BAR, 2 BARS, KEY (the first note starts the take) ([../transport.md](../transport.md#count-in)); LOOPS has its own | 1 BAR |
| QUANTIZE | where the EDITOR's takes put a note ([editor.md](editor.md#realtime-takes)): 2 ROWS, 3 ROWS, 4 ROWS (the nearest line of that many rows, from the pattern's start), ROW (the nearest row), REAL TIME (the row it falls in, with a note delay `EDx` and its end as a key off `Kxx`); the keys sound as they are written, a note played before its line waiting for it. LOOPS has its own ([loops.md](loops.md#quantize)) | ROW |
| ARP RHYTHM | the rhythm of the arpeggiator and of the EDITOR's row preview, a row a step: `#`, `#_`, `#__`, `#___`, `##_`, `###_`, `##__`, `#_##_`, and two slow ones (`#` and 7 or 15 rests); ARP held + SELECT changes it too | `#___` |
| ARP PATTERN | the order the arpeggiator's notes take turns in: UP, DOWN, UP-DOWN, RANDOM, AS PLAYED (the order the keys were pressed); ARP held + ALGORITHM changes it too | UP |
| ARP LATCH | how many notes the arpeggiator keeps once their keys are let go: OFF (a note stops with its key) or 1-8 NOTES (the oldest goes past it; a key pressed with none held starts a new set); ARP held + K1 changes it too ([../controls.md](../controls.md)) | 4 NOTES |
| POWER-ON SCREEN | STATUS, LAST, or any screen: SONGS, EDITOR, INSTRUMENTS, PLAYBACK, FX, VISUAL, SETTINGS, LOOPS, DJ | STATUS |
| TUNING | A4 = 430-450 Hz | 440 Hz |
| TOOLS | CHECK REFERENCES, CHECK STORAGE, CLEAN UP, AUTO LEVEL ALL, REVERT ALL, FACTORY RESET | |
| ABOUT | version, licences | |

- **K1 takes one step a click** and stops at both ends.
- **14 items show;** the list scrolls, keeping the selected one in the middle while it can.
- **Output is stereo by default.** The internal speaker needs mono, but no headphone-detect pin is
  known, so the device cannot switch by itself.
- **Two gains, one for each kind of output.** The FM-1 has no volume in its hardware: the chip sends
  the codec digital audio, so every level, MASTER's too, is set by the firmware. The speaker and the
  jack play the same analog signal (a plug in the jack cuts the speaker, and nothing tells the firmware
  which plays), so LINE GAIN sets both. USB audio is a stream the firmware makes itself, so USB GAIN
  sets it alone.
  - **The analog out:** the mix after the effects and the METRONOME, times LINE GAIN and MASTER, made
    mono if OUTPUT says so, then a soft ceiling: peaks above 0.8 of full scale (−1.9 dB) are rounded
    off, never past full scale. At LINE GAIN 0 dB and MASTER full, a full-scale sample of the mix comes
    out at −2 dBFS of the codec.
  - **USB audio:** the same mix times USB GAIN, in MASTER's place (a computer's input seldom has a
    level of its own, and MASTER turned during a recording would be in it), always stereo, then the
    same soft ceiling. At USB GAIN 0 dB the mix comes out 2 dB over its own 16-bit level.
- **Settings apply at once** and are saved by SAVE, like everything else: each change counts as an
  unsaved change ([../saving.md](../saving.md)).
- **SEL's REVERT** takes the page's own back as saved: on the list, the settings, the MIDI page's
  channels, SYNC and CLOCK OUT among them (one record: [../../files/settings.md](../../files/settings.md)),
  applied again at once; on the MIDI page, the MIDI map, its mappings
  ([../../files/midi.md](../../files/midi.md)). A question first (`REVERT THE SETTINGS TO SAVED?`,
  `REVERT THE MIDI MAP TO SAVED?`; OCT+ `REVERT`, OCT- `KEEP`), then `REVERTED`. With nothing unsaved
  there, SEL is refused (`NOTHING TO REVERT`, SEL blinks).
- **There is no backlight setting.** The backlight pin must never be switched: the same line enables the
  matrix of the keys and buttons, and switching it freezes the FM-1.
- **The tools** ([../saving.md](../saving.md)):
  - **CHECK REFERENCES** lists the missing ones.
  - **CHECK STORAGE** reads every record in the user area and checks its CRC (~0.5 s, a progress bar;
    [../../files/store.md](../../files/store.md#power-on)), and lists, as CHECK
    REFERENCES does, what fails: a thing that fell back to an older version, or one now missing.
    It writes nothing.
  - **CLEAN UP** asks what goes, as a checklist ([../dialogs.md](../dialogs.md#a-question-with-a-checklist)):
    - **UNUSED PATTERNS:** those no song position and no FUSION slot plays;
    - **UNUSED SAMPLES:** those no instrument of their cartridge uses (the index stays, empty:
      `(DELETED)`);
    - **UNUSED INSTRUMENTS:** those no cell plays, nor a loop.

    The built-in instruments, any built-in patterns and the EDITOR's own pattern are never listed.

    Each line says how many and what they take; the first line, what the ticked ones free. Patterns
    and samples come ticked, instruments not: most of a bank is unused by any song, a library rather
    than leftovers.
  - **AUTO LEVEL ALL** measures every sound in the user area and sets its level; the built-in
    instruments keep theirs (a cartridge's own menu measures one, [../saving.md](../saving.md#fws)). It asks first, since it changes every level at once
    (`AUTO LEVEL 486 SOUNDS?`, `EACH ONE'S LEVEL SET AGAIN`, OCT+ `LEVEL`, OCT- `KEEP`).
  - **REVERT ALL** drops every unsaved change after a question, as SAVE + OCT- does; its question and
    texts are REVERT TO SAVED's as before ([../saving.md](../saving.md)).
  - **FACTORY RESET** erases the user area ([../../files/store.md](../../files/store.md)): every song,
    pattern, loop, sampler cartridge and sample, the user's instruments and the built-ins' edits, DJ's
    stand, the settings and the MIDI map, saved or uploaded; the firmware and its built-in instruments
    stay. The FM-1 is then as after a first install, before any upload. Two questions first, both answered
    by taps: `FACTORY RESET?` (RESET / KEEP), then `ERASE IT ALL NOW?` (ERASE / KEEP), which also counts the
    changes not saved, lost with the rest. While a save runs it is refused (`A SAVE IS UNDER WAY`).
    - It erases every block of the user area, one at a time, the sound silent from the first (~1.8 s, a
      progress bar, every control waiting; a block already erased is skipped). Then the FM-1 restarts, to
      the screen POWER-ON SCREEN's default names, with the settings' defaults.
    - A power cut midway leaves the blocks not yet erased: what they hold is read at the next power-on as
      after any power cut ([../../files/store.md](../../files/store.md#power-on)). FACTORY RESET again
      finishes the job.
    - The install page's CLEAN INSTALL does the same from a computer, with an install
      ([../../files/blob.md](../../files/blob.md)).
  - K1 picks the tool on the TOOLS line and **EDIT runs it**; EDIT on ABOUT opens it, and
    EDIT closes it.
- **The footer's right side:** the selected item's default (`DEFAULT STEREO`); on TOOLS and ABOUT,
  `EDIT: RUN`.

## VOICES

Every note, a song's, the loops', a DJ deck's or the keys', takes a voice of one pool, and VOICES says how many
are heard at once: 8, 12 or 16. The pool has 20 places: at 16 places 1-10 are the first core's, 11-16 the second
core's, at 8 and 12 all the first core's ([../../second-core.md](../../second-core.md#the-voices-on-core-2)), and the
places past the count hold the notes that had to give their voice up. The pool holds 16 notes at most, parked and
fading ones included, 20 at VOICES 16: so four wait at VOICES 16 and 12, eight at 8.

- **The limit in force:** VOICES 8 and 12 use the first core alone; 16 gives the second core six places. While the
  second core does not run (SECOND CORE off, not started yet in the first 2 s, stopped by a fallback), 16 counts as
  12, all on the first core, and it comes back when the second core runs again. The screens show the limit in force
  (STATUS' `VOICES 13/16`, or `/12`).
- **A free voice first, the first core's first.** A new note takes a voice no note sounds in: the free place that
  comes first. At VOICES 16 the second core plays a voice only past 10 notes at once. A note keeps its place while
  it sounds: it moves from one core to the other only by parking (below).
- **Else one is taken from a note**, the one least missed (counted in STATUS' STEALS):
  1. a release tail (a note let go, ringing out): the new note's own track's first, then the quietest;
  2. a held note of a song (the song's, the loops', a deck's), the quietest first; a muted track's counts as
     silent;
  3. a note held on the keys (MIDI's and the arpeggiator's too), the oldest first, however quiet: what is played
     live goes last, and only to another note of the keys. A song's note never takes it: with every voice a held
     note of the keys, the song's note starts parked (below) and comes in as a voice comes free (a
     TB-303's, which slides from note to note in its voice, is not played). So with more keys down than voices,
     every song note is parked, and then the newest keys' notes park the oldest.

  A note started in the same 1.45 ms block has no loudness to judge yet: it goes last among its kind. On DJ the
  deck lower in the mix gives way first ([dj.md](dj.md#voices)).
- **A held note taken is parked, not lost.** It moves to a place past the count, fades out over
  1.45 ms (no click) and goes on unheard: its envelopes, LFOs and oscillators run as if it sounded, at a small part
  of a voice's cost. As soon as a voice is free again, a parked note comes back into it, fading in over 5.8 ms,
  where its envelope would be by then: the keys' first, then the song's, each the one parked last first. A long
  quiet pad pushed out by a busy bar comes back once the bar is over.
  The sound it comes back with is very close to the note never taken (the same within a fraction of a dB), not
  the same sample for sample: its filters start from rest.
- **A parked note let go is gone:** its key let go, its track's next note, NOTE CUT, all sound off. A
  release tail taken, and a TB-303's note (one voice a track, sliding from note to note), fade out over 1.45 ms
  and go.
- **When the places past the count are full,** the note parked longest goes for good (unheard, nothing is lost
  to the ear), a song's before the keys'; if every one is still fading out, the note taken is cut at once (a
  song's note that would start parked is not played).
- **A smaller VOICES** parks the held notes above it (a TB-303's is let go), their tails ring out; a larger one
  brings the parked notes back. The limit falling from 16 to 12 (the second core stopping or switched off) does the
  same to the notes in the second core's places; a parked note comes back into the first place that frees, of
  either core.
- **While a note fades out of its place,** one voice more than VOICES sounds for 1.45 ms: the figures count
  the notes, not the fade.

## The MIDI page

The same kind of list: **SELECT** picks a line, **K1** changes it, **LFO** resets it to the default.

| Item | Values | Default |
|---|---|---|
| MIDI IN CHANNEL | the channel MIDI in listens to, USB and the jack: ALL or 1-16 | **ALL** |
| MIDI OUT | what goes out over USB: OFF; KEYS (the FM-1's keys as played); ALL (the keys, and everything that plays: track 0 on channel 1 … track 15 on channel 16) | **KEYS** |
| MIDI OUT CHANNEL | the keys' channel out: 1-16 | 1 |
| SYNC | INTERNAL (PT/FM's own tempo) or EXTERNAL (an outside clock, USB or the jack) | **INTERNAL** |
| CLOCK OUT | OFF or ON: the clock, START / STOP / CONTINUE and the song position out, with SYNC INTERNAL | **OFF** |
| MIDI CONTROLLER | no value: EDIT makes the FM-1 the PT/FM controller until it is switched off and on ([below](#midi-controller)) | |
| LEARN | how many mappings of the 64 (`3 OF 64`); EDIT learns one ([below](#learn)) | |

- **Then the mappings,** a line each in the order learned: the FM-1's control, the outside MIDI
  control, its kind. SELECT goes on through them; **LFO deletes the one picked**
  (`K1: CC 74 CH 1 DELETED`; SEL's REVERT brings it back, with the whole map as saved). With none:
  `NO MAPPINGS YET: EDIT ON LEARN`, dim.
- **The footer's right side:** an item's default; on MIDI CONTROLLER `EDIT: SWITCH`; on LEARN `EDIT: LEARN`; on
  a mapping `LFO: DELETE`.
- **The playground** has no MIDI in from an outside controller: LEARN's second step waits there until
  HOME ([../../playground.md](../../playground.md)).

### LEARN

**EDIT on LEARN** opens a box ([../dialogs.md](../dialogs.md#questions), drawn as a question's, without
answers):
1. `TOUCH AN FM-1 CONTROL`: a knob turned, MASTER moved, a button or a key pressed. It does nothing
   else; HOME cancels, so HOME itself is the one control that cannot be learned *(proposed)*.
2. `MOVE THE MIDI CONTROL`, under the FM-1 control's name: the outside knob turned, or pad pressed and
   let go. Its first message names it (the channel, CC or note, the number); what comes within a second
   tells its kind: a knob's values going up and down by steps, or jumping as an endless encoder's do;
   a pad's press and release, or one message a press ([../../connections.md](../../connections.md#midi-learn)).
   The box closes with the toast `K1: CC 74, CH 1`. HOME cancels.

With 64 mappings EDIT is refused (`64 MAPPINGS: DELETE ONE FIRST`).

## Controls

| Control | Does |
|---|---|
| SELECT | picks an item (on MIDI: then the mappings) |
| SEQ, ALGORITHM | turn the pages: the list, MIDI |
| K1 | changes it; on TOOLS, picks the tool |
| LFO | resets it to the default; on a mapping, deletes it |
| EDIT | runs the tool picked on TOOLS; opens ABOUT, and closes it; on LEARN, learns |
| SEL | the menu: REVERT, the page's own back as saved (the settings; on MIDI, the mappings) |
| everything else | as on every screen ([../controls.md](../controls.md)) |

## Layout

The mockups are on the T grid (40 × 20 characters of 6 × 12 px); the list's rows are 15 px, drawn
here a line each. The header and footer are the frame's ([../frame.md](../frame.md)).

### The list

```
SETTINGS                               S, the frame's header
[OUTPUT                          STEREO]   the selected item, boxed
 LINE GAIN                         0 DB
 USB GAIN                          0 DB
 VOICES                              16
 SECOND CORE                         ON
 THEME                          DEFAULT
 KEYS VOLUME                         60
 KEYS GAIN                         0 DB
 KEYS                        THROUGH FX
 KEYS OCTAVE                          0
 KNOB SPEED                         LOW
 HOLD DELAY                      500 MS
 COMBO GRACE                     100 MS
 PLAYBACK SCROLL                   ROWS
                                           (PRESETS LIST, COUNT-IN, QUANTIZE, ARP RHYTHM, ARP PATTERN, ARP
                                           LATCH, POWER-ON SCREEN, TUNING, TOOLS and ABOUT below: it scrolls)
01:00 EL PIANO DX7       DEFAULT STEREO    the frame's footer
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| item *i* = 0-13 of those shown | 4, 20 + 15 *i* | | T | its name; the first shown is `max(0, min(selected − 6, items − 14))` |
| – its value | right-aligned to x 236 | | T | |
| – the selected item's box | 0, item − 2 | 240 × 15 | | |

The values' forms: LINE GAIN, USB GAIN and KEYS GAIN signed, with ` DB` (`0 DB`, `+3 DB`, `-6 DB`); KEYS
VOLUME in hex (`60`); KEYS OCTAVE signed (`0`, `+1`, `-2`); HOLD DELAY and
COMBO GRACE with ` MS`; TUNING with ` HZ`; TOOLS the tool picked; ABOUT `VERSION, LICENCES`.

Colours: names cyan; a value light grey at its default, yellow when changed; THEME's value light
magenta; the selected item's name and value white, boxed white. The footer's `DEFAULT ...` dark grey,
`EDIT: RUN` light grey.

### The MIDI page

```
SETTINGS MIDI                  SEQ 2/2   S, the frame's header: MIDI white
[MIDI IN CHANNEL                    ALL]   the selected line, boxed
 MIDI OUT                          KEYS
 MIDI OUT CHANNEL                     1
 SYNC                          INTERNAL
 CLOCK OUT                          OFF
 MIDI CONTROLLER
 LEARN                          3 OF 64
 K1             CC 74 CH 1     RELATIVE    the mappings: the FM-1's control, the MIDI one, its kind
 PLAY           NOTE 36 CH 10    TOGGLE
 KEY F-3        NOTE 60 CH 2  MOMENTARY

01:00 EL PIANO DX7          DEFAULT ALL    the frame's footer
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| line *i* = 0-13 of those shown | 4, 20 + 15 *i* | | T | an item's name, or a mapping's FM-1 control (`K1`, `MASTER`, `PLAY`, `KEY F-3`); the first shown is `max(0, min(selected − 6, lines − 14))` |
| – a mapping's MIDI control | 94, same | | T | `CC 74 CH 1`, `NOTE 36 CH 10` |
| – the value, or the kind | right-aligned to x 236 | | T | an item's value; LEARN's `3 OF 64`; a mapping's `ABSOLUTE`, `RELATIVE`, `MOMENTARY`, `TOGGLE` |
| – the selected line's box | 0, line − 2 | 240 × 15 | | |
| the empty list | 4, after LEARN | | T | `NO MAPPINGS YET: EDIT ON LEARN` |

Colours: as the list's (names cyan, values light grey at the default, yellow changed); a mapping's FM-1
control yellow, its MIDI control light grey, its kind dark grey; the selected line white, boxed white;
the empty list's line dark grey.

### LEARN's box

```
 +-------------------------------------+
 |              MIDI LEARN             |   T, white
 |                  K1                 |   the FM-1 control, yellow (step 2)
 |        MOVE THE MIDI CONTROL        |
 |   A KNOB: TURN IT. A PAD: PRESS IT  |
 |                                     |
 |            HOME: CANCEL             |
 +-------------------------------------+
```

A question's panel ([../dialogs.md](../dialogs.md#questions)): 4 lines, *h* = 88, at 8, 78, 224 × 88;
the lines centred at 86 + 12 *i*, light grey; `HOME: CANCEL` at 150. Step 1's lines: `MIDI LEARN`, an
empty line, `TOUCH AN FM-1 CONTROL`, `A KNOB, A BUTTON OR A KEY`.

### ABOUT

```
SETTINGS
PT/FM 0.1-202610072229 DEV
POCKET TRACKER / FUSION MACHINE

GPL-3.0-ONLY, (C) 2026 HITOSU

FONTS: TERMINUS, SIL OFL 1.1
DX7 AFTER MSFA (APACHE-2.0)
JUNO-106 AFTER KR-106 (GPL-3.0)
TB-303 AFTER OPEN303 (MIT)

BUILT-IN SOUNDS: CC0, MIT, BSD, GPL-3.0,
CC BY-SA 4.0; CREDITS: THE INSTALL PAGE
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the page | 0, 17 | 240 × 210 | | the body, cleared |
| line *i* | 0, 19 + 12 *i* | | T | |

Colours: the first line white; the `FONTS` line cyan; the others light grey. EDIT closes it; nothing
else acts while it shows.

### A tool running

```
   +--------------------------------+
   |            CHECKING            |
   | [##############                ]|
   +--------------------------------+
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 20, 97 | 200 × 44 | | |
| the text | centred, 103 | | T | `CHECKING`, `MEASURING EVERY SOUND`, `RELOADING` |
| the bar | 30, 121 | 180 × 10 | | a box; filled inside (32, 123, up to 176 × 6) as the work goes |

Input waits while it shows ([../dialogs.md](../dialogs.md)). Colours: the panel's border light grey,
the text white, the bar's box light grey, its fill green.

### CLEAN UP's checklist

```
 +-------------------------------------+
 |        CLEAN UP: FREES 1.3K         |
 |[# UNUSED PATTERNS          1  1.3K ]|
 |  . UNUSED SAMPLES              NONE |
 |  . UNUSED INSTRUMENTS     99  5.5K  |
 |                                     |
 |         OCT-              OCT+      |
 |         KEEP            CLEAN UP    |   S
 |                                     |
 | SELECT: PICK  K1: TICK  HOME: CANCEL|
 +-------------------------------------+
```

A question with a checklist ([../dialogs.md](../dialogs.md#a-question-with-a-checklist)), 3 lines: a
panel at 8, 55, 224 × 133; CLEAN UP (OCT+) / KEEP (OCT-). With nothing unused anywhere it does not
open: the toast `NOTHING TO CLEAN UP`. REVERT ALL's question is the one SAVE + OCT- asks
([../saving.md](../saving.md)).

### The SEL menu

A menu ([../dialogs.md](../dialogs.md#menus)) at the top right of the body, titled `SETTINGS` on the list
and `MIDI` on the MIDI page, with one item:

```
              +----------------------+
              | SETTINGS             |
              |[REVERT              ]|
              +----------------------+
```

REVERT opens its question ([../dialogs.md](../dialogs.md)), one line, `REVERT THE SETTINGS TO SAVED?`
(`REVERT THE MIDI MAP TO SAVED?`), REVERT (OCT+) / KEEP (OCT-). Colours as every menu's.

### CHECK REFERENCES' list

```
 +--------------------------------------+
 | MISSING REFERENCES: 3                |
 |  PATTERN 0C ROW 12 TRACK 03: 0B:05   |
 |  SONG 02 POSITION 05: PATTERN 1A     |
 |  FUSION 10:03: PATTERN 1A            |
 |                                      |
 +--------------------------------------+
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 2, 19 | 236 × 206 | | a box |
| the title | 6, 22 | | T | `MISSING REFERENCES: ` and the count |
| line *i* | 8, 37 + 13 × (*i* - top) | | T | a missing reference: where it is, then what it names; 14 show |

SELECT scrolls; HOME closes it. Colours: the title cyan, the lines light grey, the box light grey. With
nothing missing there is no list, only the toast `NO MISSING REFERENCES`.

### MIDI CONTROLLER

On the MIDI page, its line after CLOCK OUT (EDIT; `EDIT: SWITCH` in the footer). The FM-1 as a USB-MIDI controller
for the playground, synths and DAWs, without installing anything: PT/FM ends and the controller of
[../../controller.md](../../controller.md#1-switching-and-the-start) runs, until the FM-1 is switched off and on. Two questions
come first, as every question ([../dialogs.md](../dialogs.md)):

```
 +-------------------------------------+      +-------------------------------------+
 |     SWITCH TO MIDI CONTROLLER?      |      |       SAVE BEFORE SWITCHING?        |
 |     PT/FM STOPS UNTIL THE FM-1      |      |         1 UNSAVED CHANGE            |
 |       IS SWITCHED OFF AND ON        |      |        DROP: THEY ARE LOST          |
 |   OCT-                  OCT+        |      |   OCT-                  OCT+        |
 |   BACK                 SWITCH       |      |   DROP                  SAVE        |
 |            HOME: CANCEL             |      |            HOME: CANCEL             |
 +-------------------------------------+      +-------------------------------------+
```

- **SWITCH (OCT+)** goes on; BACK (OCT-) and HOME leave everything as it was.
- **With unsaved changes** the second question follows: **SAVE (OCT+)** pauses what plays and saves (its bar),
  then switches once the save is written (a save that fails, or does not fit, switches nothing: its own toast or
  question says why); **DROP (OCT-)** switches, the changes lost; HOME leaves everything as it was. Without
  unsaved changes it switches at once. While a save is under way the tool waits: the toast `A SAVE IS UNDER WAY`.
- **The switch:** what plays pauses, the keys let go, and the sound fades out as [CUT](../controls.md#cut) does
  (12 ms); MIDI OUT ends its notes and the clock. 50 ms later PT/FM ends for good: no player, engines or effects,
  no store, MIDI or screens run any more (the audio interrupt gives silence at once), and the controller's start
  screen shows: `REC MIRROR CH 16`, `HOME GENERAL CH 1`, SELECT the channel, REC's and HOME's LEDs lit. From
  there it is the controller, as [../../controller.md](../../controller.md) says.

## Texts

- **The header:** `SETTINGS`.
- **The items:** `OUTPUT`, `LINE GAIN`, `USB GAIN`, `VOICES`, `SECOND CORE`, `THEME`, `KEYS VOLUME`, `KEYS GAIN`, `KEYS`, `KEYS OCTAVE`,
  `KNOB SPEED`, `HOLD DELAY`, `COMBO GRACE`, `PLAYBACK SCROLL`, `PRESETS LIST`, `COUNT-IN`, `QUANTIZE`, `ARP RHYTHM`, `ARP PATTERN`,
  `ARP LATCH`, `POWER-ON SCREEN`, `TUNING`, `TOOLS`, `ABOUT`.
- **VOICES' toast:** `16 VOICES: 12 WITHOUT CPU 2`, when 16 is picked while the second core does not run (the line
  itself shows the value picked; no toast otherwise).
- **The values:** `STEREO`, `MONO`, `SYNC PULSE L`, `SYNC PULSE R`; ` DB`; `8`, `12`, `16`; `ON`, `OFF`; the themes' names; `THROUGH FX`, `DRY`; `OFF`, `LOW`, `HIGH`; `ROWS`,
  `PAGES`; `OFF`, `1 BAR`, `2 BARS`, `KEY`; `2 ROWS`, `3 ROWS`, `4 ROWS`, `ROW`, `REAL TIME`; the rhythms (`#___`); `UP`, `DOWN`, `UP-DOWN`, `RANDOM`,
  `AS PLAYED`; `OFF`, `1 NOTE`, `2 NOTES` … `8 NOTES`; `STATUS`, `LAST` and the screens' names; ` MS`, ` HZ`; `CHECK REFERENCES`, `CHECK STORAGE`,
  `CLEAN UP`, `AUTO LEVEL ALL`, `REVERT ALL`, `FACTORY RESET`; `VERSION, LICENCES`.
- **SEL's menu:** `SETTINGS`, `MIDI`; `REVERT`; its question `REVERT THE SETTINGS TO SAVED?`
  (`REVERT THE MIDI MAP TO SAVED?`), REVERT / KEEP; the toasts `REVERTED`, `NOTHING TO REVERT`.
- **The footer's right side:** `DEFAULT ` and the default value; `EDIT: RUN`; on MIDI `EDIT: LEARN`,
  `LFO: DELETE`.
- **The MIDI page:** the header's `MIDI`; the items `MIDI IN CHANNEL`, `MIDI OUT`, `MIDI OUT CHANNEL`,
  `SYNC`, `CLOCK OUT`, `MIDI CONTROLLER`, `LEARN`; the values `ALL`, `1`-`16`, `OFF`, `KEYS`, `INTERNAL`,
  `EXTERNAL`, `ON`,
  `3 OF 64`; the mappings' `K1` (the knobs, `MASTER`, the buttons by name, `KEY F-3`), `CC 74 CH 1`,
  `NOTE 36 CH 10`, `ABSOLUTE`, `RELATIVE`, `MOMENTARY`, `TOGGLE`; `NO MAPPINGS YET: EDIT ON LEARN`.
- **LEARN:** `MIDI LEARN`, `TOUCH AN FM-1 CONTROL`, `A KNOB, A BUTTON OR A KEY`, `MOVE THE MIDI CONTROL`,
  `A KNOB: TURN IT. A PAD: PRESS IT`, `HOME: CANCEL`; the toasts `K1: CC 74, CH 1` (learned),
  `K1: CC 74 CH 1 DELETED` (LFO), `64 MAPPINGS: DELETE ONE FIRST`. The connections' own toast,
  `CLOCK LOST`: [../../connections.md](../../connections.md#clock).
- **ABOUT:** the lines above: the firmware's own, its built-in instruments' licences among them
  ([../../files/instruments.md](../../files/instruments.md#built-in-instruments)), each voice's credit on
  the install page; an uploaded disk's credits travel with the disk.
- **The tools:**

  | Text | When |
  |---|---|
  | `CHECKING` | CHECK REFERENCES runs (a progress bar) |
  | `MISSING REFERENCES: 3`; `PATTERN 0C ROW 12 TRACK 03: 0B:05`, `SONG 02 POSITION 05: PATTERN 1A`, `FUSION 10:03: PATTERN 1A` | its list |
  | `NO MISSING REFERENCES` | its toast, with nothing missing |
  | `CLEAN UP: FREES 1.3K`; `UNUSED PATTERNS`, `UNUSED SAMPLES`, `UNUSED INSTRUMENTS`, each with `1  1.3K` or `NONE`; CLEAN UP / KEEP; `SELECT: PICK  K1: TICK  HOME: CANCEL` | CLEAN UP's checklist |
  | `CLEANED UP: 4 DELETED`; `NOTHING TO CLEAN UP` | its toasts |
  | `AUTO LEVEL 486 SOUNDS?`, `EACH ONE'S LEVEL SET AGAIN`; LEVEL / KEEP | AUTO LEVEL ALL's question |
  | `MEASURING EVERY SOUND` | AUTO LEVEL ALL runs (a progress bar) |
  | `LEVELS SET` | its toast |
  | `CHECKING STORAGE` | CHECK STORAGE runs (a progress bar) |
  | `STORAGE PROBLEMS: 2`; `PATTERN 1A: AN OLDER VERSION`, `SAMPLE 0C:03: MISSING` | its list |
  | `STORAGE OK` | its toast, with nothing wrong |
  | `REVERT TO SAVED?`, `DROP 3 UNSAVED CHANGES`, `AND RELOAD FROM FLASH`; REVERT / KEEP | REVERT ALL's question ([../saving.md](../saving.md)) |
  | `RELOADING`, then `REVERTED TO SAVED` | it runs, then its toast |
  | `NOTHING TO REVERT` | REVERT ALL with nothing unsaved |
  | `FACTORY RESET?`, `ERASES ALL YOU SAVED OR UPLOADED`, `ONLY THE BUILT-IN INSTRUMENTS STAY`; RESET / KEEP | FACTORY RESET's first question |
  | `ERASE IT ALL NOW?`, `THERE IS NO WAY BACK`, and with changes not saved `3 UNSAVED CHANGES LOST TOO` (`1 UNSAVED CHANGE LOST TOO`); ERASE / KEEP | its second |
  | `ERASING` | it runs (a progress bar), then the restart |
  | `A SAVE IS UNDER WAY` | FACTORY RESET while a save runs |
  | `SWITCH TO MIDI CONTROLLER?`, `PT/FM STOPS UNTIL THE FM-1`, `IS SWITCHED OFF AND ON`; SWITCH / BACK | MIDI CONTROLLER's question |
  | `SAVE BEFORE SWITCHING?`, `3 UNSAVED CHANGES` (`1 UNSAVED CHANGE`), `DROP: THEY ARE LOST`; SAVE / DROP | its second, with unsaved changes |
  | `A SAVE IS UNDER WAY` | its toast while a save runs |

## Help

As the help shows it ([../help.md](../help.md)): `{...}` a key, `[...]` a screen. The help draws the
last line, `PRESS HOME TO GET BACK`, itself. The shared pages follow.

```
[SETTINGS]: MY OWN NERDY OPTIONS
{SELECT}: PICK A SETTING
{K1}: CHANGE IT, AT ONCE
{LFO} (LEAVE FIELD OPEN): BACK TO DEFAULT
{SAVE}: KEEP THEM AFTER POWER OFF
{SEQ} (SHOW ELSE, QUICK) OR {ALGORITHM}:
  THE MIDI PAGE, AND BACK
OUTPUT: STEREO, OR MONO FOR THE SPEAKER
  SYNC PULSE L / R: CLICKS ON THAT SIDE
LINE GAIN: SPEAKER AND JACK LEVEL
USB GAIN: USB AUDIO, MASTER IGNORED
VOICES: NOTES AT ONCE: 8, 12 OR 16
  16 NEEDS THE SECOND CORE
SECOND CORE: A 2ND PROCESSOR, FOR THE
  EFFECTS AND VOICES 11-16
```

```
SOUND, KEYS AND KNOBS
THEME: THE COLOURS
KEYS VOLUME: THE {KEYS}' NOTE VOLUME
KEYS GAIN: AS HEARD, NOT AS RECORDED
KEYS: THROUGH FX, OR DRY (NO EFFECTS)
KEYS OCTAVE: THE OCTAVE AT POWER-ON
KNOB SPEED: BIGGER STEPS IF TURNED FAST
HOLD DELAY: HOW LONG UNTIL A HOLD
COMBO GRACE: SLACK FOR TWO-KEY PRESSES
PLAYBACK SCROLL: [PLAYBACK] BY ROWS OR
  BY PAGES
PRESETS LIST: SHOWN AS {PRESETS} TURNS
COUNT-IN: BEFORE A TAKE IN [EDITOR]
  1 OR 2 BARS, OFF, OR KEY:
  THE FIRST NOTE STARTS THE TAKE
QUANTIZE: WHERE A TAKE'S NOTES LAND,
  HEARD AS WRITTEN
```

```
ARP, POWER-ON AND TOOLS
{ARP} (AGAIN, REPEAT, PLEASE): ARPEGGIO
ARP RHYTHM: # A NOTE, _ A REST
ARP PATTERN: THE ORDER IT PLAYS IN
ARP LATCH: NOTES KEPT AFTER THE {KEYS}
POWER-ON SCREEN: WHERE IT STARTS
TUNING: PITCH OF A4, IN HZ
TOOLS: {K1} PICKS ONE, {EDIT} RUNS IT
  CHECK REFERENCES: WHAT IS MISSING
  CHECK STORAGE: ARE THE SAVES WHOLE
  CLEAN UP: DELETE WHAT NOTHING USES
  AUTO LEVEL ALL: EVEN OUT YOUR SOUNDS
  REVERT ALL: DROP EVERY UNSAVED EDIT
  FACTORY RESET: ERASE ALL YOU SAVED
ABOUT, {EDIT}: VERSION AND LICENCES
{SEL}, REVERT: THE SETTINGS AS SAVED
```

```
THE MIDI PAGE: USB AND THE MIDI JACK
MIDI NOTES IN: PLAY AS THE {KEYS} DO
MIDI IN CHANNEL: ALL, OR ONE
MIDI OUT, USB ONLY: OFF, THE {KEYS},
  OR ALL THAT PLAYS:
  TRACKS 0-15 ON CHANNELS 1-16
MIDI OUT CHANNEL: THE {KEYS}' CHANNEL
SYNC: OWN TEMPO, OR AN OUTSIDE CLOCK
CLOCK OUT: SEND THE CLOCK AND START
MIDI CONTROLLER, {EDIT}: THE FM-1 TURNS
  INTO A MIDI CONTROLLER, UNTIL IT IS
  SWITCHED OFF AND ON (IT ASKS FIRST)
LEARN, {EDIT}: TOUCH AN FM-1 CONTROL,
  THEN MOVE THE MIDI ONE TO DRIVE IT
BELOW: WHAT IT LEARNED, {LFO} DELETES
{SEL}, REVERT: THE MAPPINGS AS SAVED
```

# SETTINGS

Status: **settled** (2026-10-09), revised 2026-10-24, 2026-11-12 and 2026-11-22. The records: [the UI
proposal](../../proposals/202610072229-ui.md), sections 6.9 and 13.9; QUANTIZE, [the loop station
proposal](../../proposals/202610240236-loops-station.md), section 5; VOICES, [the ghost notes
proposal](../../proposals/202611121830-ghost-notes.md); the list and the MIDI page in S, [menus and lists in 8 ×
16](../../proposals/202611131620-bigger-lists.md); KEYS VELOCITY, [the keys' velocity
proposal](../../proposals/202611221230-key-velocity.md).

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
| VOICES | 8 / 12 / 16: the notes heard at once, everywhere (DJ too); 16 with the second core (the two sharing them each block, at any count), 12 while it does not run ([VOICES](#voices)) | 16 |
| SECOND CORE | ON / OFF: the effects and the output stage worked out on the chip's second core, beside the voices on the first ([../../second-core.md](../../second-core.md)); OFF: one core does it all. Applies from the next audio half, both ways; STATUS shows each core's load and, on DEVICE, why the second stopped if it did | ON |
| CORE SUPPLIES | STOCK / LOWER: the chip's two core supplies at the levels the FM-1's stock firmware sets (1.60 V, 1.38 V), raised at every start before any sound; LOWER leaves them where the boot loader puts them (1.40 V, 1.26 V), at which some FM-1s misread their memory under load ([../../hardware.md](../../hardware.md#the-chip)). STOCK picked raises them at once; LOWER takes effect at the next power-on, as only a power-off lowers them (a toast says so). The playground has no supplies: the row only | STOCK |
| THEME | DEFAULT, EGA, PASTEL, NEON, SUNSET, ICE, CLEAR, GREEN, AMBER, WHITE, DAYLIGHT, PAPER, MANUAL, SILVER, LCD, COMMANDER ([../colours.md](../colours.md)) | DEFAULT |
| KEYS VELOCITY | how hard a key is pressed is its note's volume, as a MIDI note's velocity is ([../../hardware.md](../../hardware.md#the-keys-velocity)): SOFT, NORMAL, HARD, the curve from the lightest press (`01`) to the firmest (`7F`), SOFT reaching `7F` with the least force; CONSTANT, every key at KEYS VOLUME and the keys scanned as without it. The top seven keys (C#5-G5) are not read: they play at the mean of the last four keys read, at KEYS VOLUME before any. The note sounds once its velocity is known, about 1-2 ms after the key goes down; a press not read in time plays at KEYS VOLUME. A held key pressed harder makes its own notes louder, toward `7F`, and softer again as it eases (heard only: [../../hardware.md](../../hardware.md#the-keys-velocity)) | NORMAL |
| KEYS VOLUME | the volume of the keys' notes with KEYS VELOCITY CONSTANT, and of the top seven keys before any key was read: `01`-`7F` | `60` |
| KEYS GAIN | the level of the keys as heard while playing them, the FM-1's keys and MIDI's notes alike: −24 to +12 dB, in 1 dB steps. Only what is heard: a note recorded (the EDITOR's takes, LOOPS) keeps its volume (the key's velocity, a MIDI note's, or KEYS VOLUME) and plays back as written, without it; on LOOPS the keys sound through the selected loop's mix instead (on `+ NEW LOOP` NEW's), on the stage or played freely ([loops.md](loops.md#the-instrument-and-mono-or-poly)). FX's MIXER shows it too, as the KEYS column's LVL, beside the keys' PAN, DUCK amount and sends ([fx.md](fx.md#mixer)) | 0 dB |
| KEYS | what the effects do to the keys as heard (the FM-1's, MIDI's notes, the arpeggiator's, the previews): THROUGH FX, through the effects as everything else; DRY, past every effect, the LIMITER and the punch-ins too, joining the mix just before the output stage ([fx.md](fx.md#the-keys-dry)), so the effects work on what plays in the background alone. One setting for every song, the loops and DJ (where the keys play no notes); on LOOPS the keys go through the selected loop's mix (on `+ NEW LOOP` NEW's) and LOOPS' effects whatever it says; FX's GLO shows it too | THROUGH FX |
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
- **12 items show,** in S; the list scrolls, keeping the selected one in the middle while it can.
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
  - **CHECK REFERENCES** lists the missing ones (the loops' instruments among them, LOOPS' own and each set's), and then
    each old-form sampler once
    ([../../files/references.md](../../files/references.md#a-reference-to-nothing)).
  - **CHECK STORAGE** reads every record in the user area and checks its CRC (~0.5 s, a progress bar;
    [../../files/store.md](../../files/store.md#power-on)), and a sample's entry against its audio's head
    ([../../files/instruments.md](../../files/instruments.md#samples)), and lists, as CHECK
    REFERENCES does, what fails: a thing that fell back to an older version, or one now missing, each named by its kind
    and number (`SONG 02`, `PATTERN 1A`, `SAMPLE 23`, `SET 03` for a LOOPS set, `CARTRIDGE 03`; LOOPS, DJ, the settings
    and the MIDI mappings by their names). It writes nothing.
  - **CLEAN UP** asks what goes, as a checklist ([../dialogs.md](../dialogs.md#a-question-with-a-checklist)):
    - **UNUSED PATTERNS:** those no song position and no FUSION slot plays;
    - **UNUSED SAMPLES:** those no instrument names, in any sampler (old-form ones left alone);
    - **UNUSED INSTRUMENTS:** those no cell plays, no loop (LOOPS' own or a set's) and no favourite names.

    The built-in instruments, any built-in patterns, the EDITOR's own pattern and the FAVORITES cartridges
    ([instruments.md](instruments.md#favorites)) are never listed.

    Each line says how many and what they take; the first line, what the ticked ones free. Patterns
    and samples come ticked, instruments not: most of a bank is unused by any song, a library rather
    than leftovers.
  - **AUTO LEVEL ALL** measures every sound in the user area and sets its level (FAVORITES cartridges left out:
    their favourites are no sounds of their own); the built-in instruments keep theirs, changed or not (a cartridge's own menu measures one, [../saving.md](../saving.md#fws)). It asks first, since it changes every level at once
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
notes sound at once: 8, 12 or 16. The pool has **16 engines** and **40 voices**. An engine is the place a note sounds
in: the state of one kind of engine (DX7, JUNO-106, TB-303, the sampler or CHIP) and its scope. A voice is the control
of a note: whose note it is, its instrument and note, its level, gain, pan and pitch as last set, a Juno-106's patch
copy or the other kinds' parameter commands, its times. VOICES counts the engines notes take. No engine belongs to a
core: while the second core runs, each block splits the engines sounding between the two so both finish together, at
any VOICES ([../../second-core.md](../../second-core.md#the-split)). There is none past 16.

A note sounding is a voice with an engine; a voice without one is a **ghost**. A ghost is unheard and costs no
engine's work (no rendering, no envelopes run), only the walk over the ghosts each block
([../../open-questions.md](../../open-questions.md#the-firmware)). Every rule of the player still acts on it. Once an
engine is free it comes back, worked forward to where its note would be by then. Up to 24 ghosts wait beside the
notes sounding, at any count.

- **The limit in force:** 12 is the most the first core renders alone, so 16 needs the second core; at 8 and 12 the
  second core shares the work too while it runs. While the second core does not run (SECOND CORE off, not started yet in the first 2 s, stopped by a fallback), 16 counts as
  12, all on the first core, and it comes back when the second core runs again. The screens show the limit in force
  (STATUS' `VOICES 13/16`, or `/12`).
- **A free engine first.** A new note takes an engine of the count no note sounds in: the free one that comes first.
  A note keeps its engine while it sounds; which core renders it is each block's split's.
- **Else an engine is taken from a note**, the one least missed (counted in STATUS' STEALS):
  1. a release tail (a note let go, ringing out): the new note's own track's first, then the quietest;
  2. a held note of a song (the song's, the loops', a deck's), the quietest first; a note of a track muted now
     counts as silent, even in its mute's own block ([muted tracks](#muted-tracks): but DUCK's source's and the
     stage's column's, never muted);
  3. a note held on the keys (MIDI's and the arpeggiator's too), the oldest first, however quiet: what is played
     live goes last, and only to another note of the keys. A song's note never takes it, nor a key's note fading
     back in: with every engine a held note of the keys, or a key's held ghost waiting for one (its return under way
     among them), the song's note starts as a ghost (below) and comes back after the keys' (a TB-303's, which slides
     from note to note in its engine, is not played). On LOOPS the keys play through the stage's lanes, so their
     notes rank as the loops'.

  An engine a ghost's return holds while under way ([below](#coming-back)) is taken before any sounding note (of
  several, the highest-numbered): it is silent, so no STEAL is counted, and the ghost stays a ghost. A note started
  in the same 1.45 ms block, or fading back in, has no loudness to judge yet: it goes last among its rank. On DJ the
  deck lower in the mix gives way first ([dj.md](dj.md#voices)).
- **The taken note fades out in its own engine, in the same block.** Its last block is rendered there, its gain
  going down to 0 over the block (1.45 ms, no click), into its mix as it was (its track's or the keys', its pan,
  sends and Juno-106 chorus; its peak into its track's, DUCK and the meters); the engine is then cut, as a note let go
  leaves it, and the new note starts in it and sounds from that block, on time (what a cut leaves carries over, as at
  any note's start where another note was: the DX7's phases without OSC KEY SYNC, the Juno-106's LFO, moved on by the
  fade's block). As many fades a block as the engines the count leaves unused, 4 at the least: 8 at VOICES 8, 4 at
  12 and 16; past them a taken note is cut at once. A note never heard yet (started in the same block), or silent or
  muted at its last block's end, gives its engine up with no fade. A fade is its engine's block rendered once more
  on the first core: for 1.45 ms up to 4 notes more than VOICES sound (8 at VOICES 8). STATUS' VOICES count leaves
  the fades out; the times and blocks counted by kind of engine, and the debug build's mean of voices made from them,
  hold them.
- **A held note taken goes on as a ghost,** but two go instead: a TB-303's, which fades out, and one that has already
  ended by itself (a one-shot sample past its END, a CHIP note whose VOL table ran out), for which a full list drops no
  ghost. **A release tail taken** fades out and goes.
- **A note that may take no engine starts as a ghost:** a song's note while the keys hold every engine, or while a
  key's held ghost waits for one; a muted track's note ([muted tracks](#muted-tracks)). A TB-303's note that would
  start as a ghost is not played, nor a note of a DJ deck faded out that finds no engine of its own deck.

### Ghosts

- **A ghost is a note as any:** the player finds, holds, follows, lets go, legatos, bends, levels and cuts it by
  every rule, as a sounding note: by its track and note count, its lane and the stage's column mark, its key, a
  FUSION runner's layer; a note off while it is its track's latest (its volume slides and glide going on through
  it); LEG, 3xx, a lane's legato and glide; NOTE CUT, all sound off, CUT, a FUSION layer's ECx, DJ's cuts at the end
  of a BRAKE, BACKSPIN, ECHO OUT or REVERB OUT, K1's restart, the second replay's tracks moving; LOOPS' MOVE and
  REC's hand-over (a lane's ghosts with it, let go ones too, as its tails), a new loop's track; FILTER's envelope
  counts a held ghost ([fx.md](fx.md#filters-envelope)). What its engine would hold its voice keeps: its pitch (a
  track's bend, a lane's glide and legato, a FUSION layer's arpeggio), its level and gain (a FUSION layer's slid
  volume among them), a Juno-106's key (as a legato moves it), a sample's TUNE, its parameter commands (a Juno-106's
  turn its patch copy).
- **Only what needs a sound differs:** a ghost is not rendered (no peak, no scope, nothing into DUCK or the meters,
  no tile on VISUAL's VOICES); RECORD LIVE counts it not ([loops.md](loops.md#record-live)); its times are kept on a
  block clock (a count of the 1.45 ms blocks made): its start and its let-go.
- **Its end by itself,** a one-shot sample past its END or a CHIP note whose VOL table or release ends, is worked
  out when it becomes a ghost and again whenever something moves it (its let-go; END, LOOP, the loop's points or
  TUNE on a sampler ghost; its pitch: a bend, a slide, DJ's KEY or BRAKE), and it ends then, as its voice would have
  (FILTER's envelope released, its voice free).
- **A sampler ghost keeps its place** in its sample, moved on at each change of its pitch by the time it played at
  the pitch before (once a block however often its pitch moves): its end and the place it comes back at come from
  that one count. A sampler note started as a ghost has its place where its note starts as its commands put it:
  LOOP OFF, or loop points leaving no room, take it out of its loop at once, as at a sounding note's start. Its
  instrument is read as it is each time its place is worked out (a ghost keeps no copy of it): an edit while it waits
  counts from the next such time (its let-go, a command on it, a change of its pitch, its end coming due) or from its
  return, and found then with no loop to play (LOOP OFF, or a loop with no room) it leaves its loop for good.
- **A CHIP ghost's end** comes from two figures, its end while held (by its VOL table) and its release's length once
  let go, worked out when it becomes a ghost and again as its return's note starts; its commands meanwhile (RELEASE,
  the VOL table, FRAMES) act from its return ([../../chip.md](../../chip.md#a-note-without-its-engine)).
- **Ending:** what ends a note ends its ghost; a ghost let go goes on as its note's release would. **A ghost let go
  goes:**
  - 10 s after its let-go;
  - at its return, when it is under hearing ([below](#coming-back));
  - at its return, a Juno-106's whose RELEASE has made it silent by then, with no work at all. A table gives the
    ticks each RELEASE takes from its envelope's top to a VCA gain of 0, silent at any level (VCA GATE: a tick;
    RELEASE 127: some 26 s, so the 10 s come first);
  - at its return, a song's note whose track has taken another instrument meanwhile. A track gone to another
    instrument and back keeps it; the loops' lanes, the stage's column and the keys keep theirs.

  **A ghost whose instrument is gone** by its return (a cartridge deleted), or is now of another kind of engine, goes.
- **Ghosts made at once** (a mute, a smaller count, a fallback) are made in their engines' order, the lowest first.

### Coming back

**The order**, at each block's start, after the block's new notes have taken their engines:

1. the keys' held ghosts;
2. the songs' and loops' held ghosts;
3. the keys' ghosts let go (every arpeggiator note's tail among them);
4. the songs' and loops' ghosts let go.

Within each, the one made a ghost last comes first: far over the count, the notes made ghosts first can stay ghosts
while later ones come and go, heard again through their next notes. A muted track's ghost never comes back while
muted ([muted tracks](#muted-tracks)). **At most 4 come back a block** (a return that drops its ghost counts not), and
none in a block the load guard holds (below).

- **An engine to come back in:**
  - a free one of the count, the first first;
  - with none free, **a held ghost takes a release tail's engine**: the tail a new note of its own would take (its
    track's first, then the quietest; never a held note's, nor one fading; a song's ghost never a key's note fading
    back in; on DJ the deck rule, [dj.md](dj.md#voices)), and only while the block has a fade left. The tail fades
    out there over that block, a fade among the block's, and the return begins in that engine in the next block, its
    checks made again then. A tail never heard yet, or silent, is cut at once and the return begins at once;
  - **a ghost let go takes only a free engine.**

  So in a full pool held ghosts come back as soon as tails ring (in LOOPS, at every bar line). The ghosts that cannot
  begin in a block (no engine they may take, no fade left) wait, and the returns under way after them in the order go
  on. Once 4 have come back, or the budget is spent (below), the block's walk ends: the rest wait, the returns under
  way too.
- **Its checks first,** before an engine or a tail is taken, so a ghost they drop costs none: its instrument still
  there and still of its kind; a Juno-106 let go whose RELEASE is over; its parameter commands (below); a sampler's
  place walked to now and its end.
- **Its engine starts from zero** (its filters at rest, its LFOs from their start) and its note is started from its
  voice: its instrument as it is then (edited while it waited or not: a ghost keeps no copy of it; a Juno-106 keeps
  its patch copy, below) with its parameter commands, at the note it started at, then its pitch, level, key and tune
  as they stand. Then it is **worked forward** over its time since its start by its kind's own steps: the DX7's
  and the sampler's envelopes and the DX7's pitch envelope stage by stage (a stretch in one sum), the Juno-106's
  envelope over its firmware loop's ticks (its slow DECAY and RELEASE in runs of one sum, at most some 135 for the
  slowest), the CHIP's tables, envelopes, sweep and release, the LFOs and their delays from their start; a ghost let
  go has its note off at its let-go on the way. Its envelopes come back exactly where the note never taken's are; its
  oscillators' phases, its filters' state and its pitch's history (a bend, a slide, the LFO's and the pitch
  envelope's pull on the DX7's operators) are not kept, nor its LFOs' phases where the note never taken's engine
  carried them over from an earlier note (the Juno-106's always, the DX7's without its SYNC); a sample comes back at
  the place its voice kept.
- **Then it fades in** over 5.8 ms (4 blocks). Its sound is then the note never taken's given that note's phases, its
  envelopes' levels the same. With its own phases (a return keeps none) its level can be off by more than 0.5 dB in
  short windows: up to some 18 dB for a detuned DX7 patch whose operators beat, 12 dB for a Juno-106, 11 for a CHIP,
  never for the sampler. A song's older note (let go, its track on to another note) can differ also by the commands
  its track has at its return (below). While it fades in it counts as just started for the takes (its loudness
  unknown, the last of its rank to be taken), so its faded peak never makes it the quietest. It is the same voice it
  was (its note count, its start, its place among the keys' notes): taken again, it is worked forward from its real
  start.
- **Its parameter commands at its return:**
  - the keys' notes, the loops' lanes' and the stage's column's keep their own;
  - a Juno-106's note, a song's too, keeps its patch copy, which holds them (a song's let go goes if its track has
    taken another instrument meanwhile, as below);
  - a song's DX7, sampler or CHIP note takes its track's as they are at its return, while the track is still on its
    instrument. It takes none while that instrument is being edited, and none once the track has taken another (a
    ghost let go then goes). So a parameter its kind takes only at a note's start (most of the DX7's) that the
    track changed meanwhile comes back changed, and a song's older note (let go, its track on to another note) comes
    back with its track's commands of then, not those it had;
  - a song's FUSION layer keeps its own: its cell's as it started, and its track's that turned it while its note was
    the track's latest. A layer of an older note goes. While its FUSION note is its track's latest: with the track
    still on FUSION, its cell is read again, and it comes back if the cell still holds its note and instrument, else
    it goes; with the track on another instrument meanwhile (a cell with an instrument alone, which ends no held
    note), a held layer comes back as it is and one let go goes;
  - a sampler's place is walked to its return by the commands it had, then taken on by those it comes back with.
    It comes back out of its loop when its instrument as it comes back (as it is then, with those commands) has no
    loop to play: LOOP OFF, or a loop with no room (LOOP END, or END when it comes first, at or under LOOP START + 1),
    whether its instrument was edited so while it waited or its commands made it so. A loop turned on meanwhile never
    puts it into one: it plays on to END, as a note started with LOOP OFF does. One that has ended by them, or ends
    within the block it would come back in, goes.
- **Under hearing:** a ghost let go is dropped at its return when, after its forward work, its bound is under -60 dB
  (33 on the 16-bit scale, where a released note's peak frees it): its engine is cut, as the quiet rule leaves one.
  - The bound is the most its note still reaches (the DX7's carriers' envelopes, the Juno-106's VCA target, the
    sampler's envelope, the CHIP's volume) times the most gain its render takes: its own, and PLAYBACK's VOLUME or
    KEYS GAIN where over 0 dB.
  - Its track's LEVEL and DUCK, never over 0 dB, are left out, so a note that would be heard is never dropped.
  - A held ghost is never dropped for being quiet: it may rise again.
- **The budget,** a safety net counted in units of about 1 µs of the FM-1's time, never in time itself, so the FM-1,
  the emulator and the playground decide alike: 44 a block, 3 % of a block. Each kind of engine's note start and
  each step of its forward work weigh what it takes on the FM-1 (its mean, measured there):

  | Kind of engine | A note's start | A step |
  |---|---:|---:|
  | DX7 | 63 | 2 |
  | JUNO-106 | 32 | 6 |
  | sampler | 116, and 95 for its decoder's seek, in the block it comes back in | 3 |
  | CHIP | 56 | 48 |

  and each ghost's checks 15, a sampler's 109 (its record read twice, its place walked to now). The budget is spent on
  the returns in the order above, the returns under way among them; a return begins while some is left, and one not
  finished goes on in the next block, its engine held for it. Each block's budget is whole: the most a block's work
  takes is the budget and one ghost's overdraw (its checks, its start, its engine's last steps with their rounding:
  the DX7's up to 8 past its budget, a sampler's seek). So a block mostly begins one return, whose checks and start
  overdraw the budget (a DX7's 78 units, a Juno-106's 47, a CHIP's 71, a sampler's 225), its forward work going on in
  the blocks after: a DX7's usual return (some 40 steps) sounds from its third block or so, a sampler's from its
  second; a DX7's longest, or a Juno-106 deep in a slow DECAY and RELEASE, takes more, and more again when returns
  ahead of it spend the budget.
- **The load guard,** for a core near its time: while a half's work on the busier core, as counted, is over 4,200
  units (the budget's), the returns' walk waits for the next block: no return begins and none under way goes on (its
  engine held, silent). The count is made at each block's walk and never measured, so the FM-1, the emulator and the
  playground decide alike; it does not read the block's split, which follows the FM-1's timing, but splits the
  engines itself by the weights alone:
  - each engine with a note in it, sounding or held for a note's start or a return, for the half's 4 blocks at its
    kind's weight a block, its block and its mixing in (their means measured on the FM-1): DX7 78, JUNO-106 64, sampler
    52, TB-303 76, CHIP 40; while the second core runs, taken from the top engine down onto the second core while its
    share and a fixed allowance for its effects stay under the first core's (10 at most), as the block's split starts;
    without it, all on the first core;
  - on the first core's side: 115 for each note started in the half so far (its cell, its record, its engine's start),
    64 for each fade begun in it (a take's, or a tail's for a return), and 400 while DJ plays (its second replay, the
    decks' mix).

  The rest of a half's work on the first core (the controls, the replays' own, the effects handed over) comes to some
  1,000 to 1,400 µs more, so the guard holds near 90 % of the half's 5,804 µs: where a row starts many notes over a
  full pool, and held ghosts would take, in that same half, the tails those notes let go. They come back a half or so
  later. The demo disk's songs and loops, each played alone, never reach it at any VOICES; two of its songs mixed on
  DJ do, at their busiest rows.
- **A return under way** is still a ghost until its fade in begins: in its place in the list, its end and its drop as
  any ghost's; whatever acts on a voice acts on it, its pitch, level and key turning its engine once its note has
  started there. A new note takes its engine before any sounding note; a parameter command on it, its track muted, or
  a smaller count over its engine gives its engine up: the ghost stays a ghost and begins again later.

### The list full

The 24 ghosts in use, the one that goes for a new one:

1. ghosts let go, the one let go longest ago first;
2. muted ghosts, the one a ghost longest first;
3. the songs' and loops' held ghosts, the one a ghost longest first;
4. the keys' held ghosts, the one a ghost longest first.

A note that would become a ghost and ranks below every ghost in the list is not kept: taken, it fades out and goes;
starting, it is not played. In LOOPS a bar line lets many notes go at once, so ghosts let go are dropped often: they
are the least missed. A muted track's note makes room in a full list before its sample is looked for, so one that then
cannot sound (its sample gone) still costs a ghost.

### Muted tracks

- **The moment a note's track is muted** (its own mute, another track's solo, or the note moving into a muted or
  solo-silenced track: a lane sent by REC into a muted loop, a song's track as a loop's), its notes sound their last
  block with the mute's ramp (1.45 ms) and leave their engines at its end: the held ones become held ghosts, the
  tails ghosts let go (a tail whose last block peaked under hearing goes), and their engines are free from the next
  block. **A muted track's new note starts as a ghost:** a muted note never takes an engine and costs no engine.
  Unmuted, its ghosts come back as any (4 a block, each fading in over 5.8 ms; within about 10 ms with engines
  free). A solo that makes more ghosts than the list holds loses those past the 24 while it lasts; its end loses none.
- **Not so:**
  - **DUCK's source:** its muted notes keep their engines, so DUCK follows the source muted or not; they rank as heard
    for a take, and its ghosts (taken for other notes) rank as unmuted for returns and drops. A change of the source
    in force (SOURCE by the knob or an FX command, DUCK on or off, BYPASS, a song's setup at its start, a DJ deck's own
    DUCK and its LOAD) acts, from the next block, as a mute on the old source's muted notes and as an unmute on the new
    source's muted ghosts ([fx.md](fx.md#duck));
  - **a TB-303's note** keeps its engine muted, and a muted track's TB-303 note takes one;
  - **the stage's column** (the preview, the stage, the keys on it, the pattern recorder's single notes): never muted,
    never a ghost by a mute.
- **A muted note's take has no fade** (it is silent at its last block's end): its engine is cut at once.

### Another count

- **A smaller VOICES, the second core stopping** (16 counting as 12): the held notes in the engines above the count
  fade out there in that block (8 fades at most, past them cut at once) and become ghosts, in their engines' order; a
  TB-303's is let go and rings on where it is; a held note whose start waited for a fade there becomes a ghost at
  once, never heard (a TB-303's, or one let go already, goes); a return under way there is given up; the release tails
  above the count ring out where they are. A TB-303 track's next note (its line let go between its rows or not) takes
  its track's engine again only while that engine is within the count; above it, the note there rings out where it is
  and the new note takes an engine within the count, as any new note does.
- **A larger count:** its engines take ghosts back as any free engine does (4 a block at most, within the budget).
- **A fallback during a voice job** of the second core's:
  [../../second-core.md](../../second-core.md#the-voices-on-core-2).

## The MIDI page

The same kind of list: **SELECT** picks a line, **K1** changes it, **LFO** resets it to the default.

| Item | Values | Default |
|---|---|---|
| MIDI IN CHANNEL | the channel MIDI in listens to, USB and the jack: ALL or 1-16 | **ALL** |
| MIDI OUT | what goes out over USB: OFF; KEYS (the FM-1's keys as played); ALL (the keys, and everything that plays: track 0 on channel 1 … track 15 on channel 16) | **KEYS** |
| MIDI OUT CHANNEL | the keys' channel out: 1-16 | 1 |
| SYNC | INTERNAL (PT/FM's own tempo) or EXTERNAL (an outside clock, USB or the jack) | **INTERNAL** |
| CLOCK OUT | OFF or ON: the clock, START / STOP / CONTINUE and the song position out, with SYNC INTERNAL | **OFF** |
| CONTROLLER AFTERTOUCH | what MIDI CONTROLLER's general mode sends of the keys F3-C5 held and pressed harder ([../../controller.md](../../controller.md#3-general-mode)): CHANNEL, Channel Pressure (the hardest-pressed key's); POLY, each key's on its note; OFF, none. Nothing with KEYS VELOCITY CONSTANT (the keys are not read then), nothing in mirror mode, nothing in the tracker | **CHANNEL** |
| MIDI CONTROLLER | no value: EDIT makes the FM-1 the PT/FM controller until it is switched off and on ([below](#midi-controller)) | |
| LEARN | how many mappings of the 64 (`3 OF 64`); EDIT learns one ([below](#learn)) | |

- **Then the mappings,** two lines each in the order learned: the FM-1's control, then the outside MIDI
  control and its kind under it. SELECT goes on through them; **LFO deletes the one picked**
  (`K1: CC 74 CH 1 DELETED`; SEL's REVERT brings it back, with the whole map as saved). With none:
  `NO MAPPINGS: EDIT ON LEARN`, dim.
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

The list and the MIDI page are in S, in rows of 16 px ([../dialogs.md](../dialogs.md#rows-in-s)): in their mockups a
character is one of S's (8 px, 30 across) and a line a 16-px row. The header and footer are the frame's
([../frame.md](../frame.md)).

### The list

```
SETTINGS                          S, the frame's header
[OUTPUT                STEREO]    the selected item, boxed
 LINE GAIN               0 DB
 USB GAIN                0 DB
 VOICES                    16
 SECOND CORE               ON
 CORE SUPPLIES          STOCK
 THEME                DEFAULT
 KEYS VELOCITY         NORMAL
 KEYS VOLUME               60
 KEYS GAIN               0 DB
 KEYS              THROUGH FX
 KEYS OCTAVE                0
                                  (KNOB SPEED, HOLD DELAY, COMBO GRACE, PLAYBACK SCROLL, PRESETS LIST, COUNT-IN,
                                  QUANTIZE, ARP RHYTHM, ARP PATTERN, ARP LATCH, POWER-ON SCREEN, TUNING,
                                  TOOLS and ABOUT below: it scrolls)
01:00 EL PIANO DX7 DEFAULT STEREO the frame's footer, T
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| item *i* = 0-11 of those shown | 4, 19 + 16 *i* | | S | its name; the first shown is `max(0, min(selected − 5, items − 12))` |
| – its value | right-aligned to x 236, the same line (the next when name + 2 spaces + value pass 29 characters) | | S | |
| – the selected item's box | 0, item − 1 | 240 × 16 × its lines | | |

Every item fits one line: 29 characters a row, the widest `POWER-ON SCREEN  INSTRUMENTS` 28.

The values' forms: LINE GAIN, USB GAIN and KEYS GAIN signed, with ` DB` (`0 DB`, `+3 DB`, `-6 DB`); KEYS
VOLUME in hex (`60`); KEYS OCTAVE signed (`0`, `+1`, `-2`); HOLD DELAY and
COMBO GRACE with ` MS`; TUNING with ` HZ`; TOOLS the tool picked; ABOUT `VERSION, LICENCES`.

Colours: names cyan; a value light grey at its default, yellow when changed; THEME's value light
magenta; the selected item's name and value white, boxed white. The footer's `DEFAULT ...` dark grey,
`EDIT: RUN` light grey.

### The MIDI page

```
SETTINGS MIDI      SEQ 2/2        S, the frame's header: MIDI white
[MIDI IN CHANNEL          ALL]    the selected row, boxed
 MIDI OUT                KEYS
 MIDI OUT CHANNEL           1
 SYNC                INTERNAL
 CLOCK OUT                OFF
 CONTROLLER AFTERTOUCH
                      CHANNEL     its value on the next line (name + 2 + value pass 29 characters)
 MIDI CONTROLLER
 LEARN                3 OF 64
 K1                               the mappings: the FM-1's control,
         CC 74 CH 1  RELATIVE     the MIDI one and its kind under it
 PLAY
        NOTE 36 CH 10  TOGGLE
 KEY F-3                          the third mapping's first line; the rest below: it scrolls
01:00 EL PIANO DX7 DEFAULT ALL    the frame's footer, T
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| line *i* = 0-11 of those shown | 4, 19 + 16 *i* | | S | an item's name, or a mapping's FM-1 control (`K1`, `MASTER`, `PLAY`, `KEY F-3`), on its first line |
| – an item's value | right-aligned to x 236, the same line (the next when name + 2 spaces + value pass 29 characters: CONTROLLER AFTERTOUCH's) | | S | an item's value; LEARN's `3 OF 64` |
| – a mapping's MIDI control and kind | right-aligned to x 236, the mapping's second line | | S | `CC 74 CH 1  RELATIVE`, `NOTE 36 CH 10  MOMENTARY`: the control, two spaces, `ABSOLUTE`, `RELATIVE`, `MOMENTARY` or `TOGGLE` |
| – the selected row's box | 0, its first line − 1 | 240 × 16 × its lines | | |
| the empty list | 4, after LEARN | | S | `NO MAPPINGS: EDIT ON LEARN` (26 characters) |

12 lines show, a mapping two of them; the list scrolls by rows, a mapping selected always whole, kept 5 lines down
while it can ([../dialogs.md](../dialogs.md#rows-in-s)).

Colours: as the list's (names cyan, values light grey at the default, yellow changed); a mapping's FM-1
control yellow, its MIDI control light grey, its kind dark grey; the selected row white, boxed white;
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

A menu ([../dialogs.md](../dialogs.md#menus)) at the top of the body, its whole width, in S, titled `SETTINGS` on
the list and `MIDI` on the MIDI page, with one item (a character here one of S's):

```
+----------------------------+
| SETTINGS                   |
|[REVERT                    ]|
+----------------------------+
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
 |  FAVORITE FB:07: 0B:05               |
 +--------------------------------------+
```

A favourite whose source is gone is listed as FUSION's are: `FAVORITE FB:07: 0B:05`. A sample instrument naming a
missing sample: `SAMPLER 01:05: SAMPLE 23`. A loop whose instrument is missing comes after all those: LOOPS' own as
`LOOP 03: 0B:05`, then each LOOPS set's, in the sets' order, as `SET 02 LOOP 03: 0B:05`. After the references to
nothing, and not counted among them, each old-form sampler once
([../../files/instruments.md](../../files/instruments.md#the-old-form)): `CARTRIDGE 03: OLDER FORM`; with only those,
the title reads `MISSING REFERENCES: 0`.

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
- **The items:** `OUTPUT`, `LINE GAIN`, `USB GAIN`, `VOICES`, `SECOND CORE`, `CORE SUPPLIES`, `THEME`, `KEYS VELOCITY`, `KEYS VOLUME`, `KEYS GAIN`, `KEYS`, `KEYS OCTAVE`,
  `KNOB SPEED`, `HOLD DELAY`, `COMBO GRACE`, `PLAYBACK SCROLL`, `PRESETS LIST`, `COUNT-IN`, `QUANTIZE`, `ARP RHYTHM`, `ARP PATTERN`,
  `ARP LATCH`, `POWER-ON SCREEN`, `TUNING`, `TOOLS`, `ABOUT`.
- **VOICES' toast:** `16 VOICES: 12 WITHOUT CPU 2`, when 16 is picked while the second core does not run (the line
  itself shows the value picked; no toast otherwise).
- **CORE SUPPLIES' toast:** `LOWER FROM THE NEXT POWER-ON`, when LOWER is picked.
- **The values:** `STEREO`, `MONO`, `SYNC PULSE L`, `SYNC PULSE R`; ` DB`; `8`, `12`, `16`; `ON`, `OFF`; `STOCK`, `LOWER`; the themes' names; `CONSTANT`, `SOFT`, `NORMAL`, `HARD`; `THROUGH FX`, `DRY`; `OFF`, `LOW`, `HIGH`; `ROWS`,
  `PAGES`; `OFF`, `1 BAR`, `2 BARS`, `KEY`; `2 ROWS`, `3 ROWS`, `4 ROWS`, `ROW`, `REAL TIME`; the rhythms (`#___`); `UP`, `DOWN`, `UP-DOWN`, `RANDOM`,
  `AS PLAYED`; `OFF`, `1 NOTE`, `2 NOTES` … `8 NOTES`; `STATUS`, `LAST` and the screens' names; ` MS`, ` HZ`; `CHECK REFERENCES`, `CHECK STORAGE`,
  `CLEAN UP`, `AUTO LEVEL ALL`, `REVERT ALL`, `FACTORY RESET`; `VERSION, LICENCES`.
- **SEL's menu:** `SETTINGS`, `MIDI`; `REVERT`; its question `REVERT THE SETTINGS TO SAVED?`
  (`REVERT THE MIDI MAP TO SAVED?`), REVERT / KEEP; the toasts `REVERTED`, `NOTHING TO REVERT`.
- **The footer's right side:** `DEFAULT ` and the default value; `EDIT: RUN`; on MIDI `EDIT: LEARN`,
  `LFO: DELETE`.
- **The MIDI page:** the header's `MIDI`; the items `MIDI IN CHANNEL`, `MIDI OUT`, `MIDI OUT CHANNEL`,
  `SYNC`, `CLOCK OUT`, `CONTROLLER AFTERTOUCH`, `MIDI CONTROLLER`, `LEARN`; the values `ALL`, `1`-`16`, `OFF`,
  `KEYS`, `INTERNAL`, `EXTERNAL`, `ON`, `CHANNEL`, `POLY`,
  `3 OF 64`; the mappings' `K1` (the knobs, `MASTER`, the buttons by name, `KEY F-3`), `CC 74 CH 1`,
  `NOTE 36 CH 10`, `ABSOLUTE`, `RELATIVE`, `MOMENTARY`, `TOGGLE`; `NO MAPPINGS: EDIT ON LEARN`.
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
  | `MISSING REFERENCES: 3`; `PATTERN 0C ROW 12 TRACK 03: 0B:05`, `SONG 02 POSITION 05: PATTERN 1A`, `FUSION 10:03: PATTERN 1A`, `SAMPLER 01:05: SAMPLE 23`, `LOOP 03: 0B:05`, `SET 02 LOOP 03: 0B:05`, `CARTRIDGE 03: OLDER FORM` | its list |
  | `NO MISSING REFERENCES` | its toast, with nothing missing |
  | `CLEAN UP: FREES 1.3K`; `UNUSED PATTERNS`, `UNUSED SAMPLES`, `UNUSED INSTRUMENTS`, each with `1  1.3K` or `NONE`; CLEAN UP / KEEP; `SELECT: PICK  K1: TICK  HOME: CANCEL` | CLEAN UP's checklist |
  | `CLEANED UP: 4 DELETED`; `NOTHING TO CLEAN UP` | its toasts |
  | `AUTO LEVEL 486 SOUNDS?`, `EACH ONE'S LEVEL SET AGAIN`; LEVEL / KEEP | AUTO LEVEL ALL's question |
  | `MEASURING EVERY SOUND` | AUTO LEVEL ALL runs (a progress bar) |
  | `LEVELS SET` | its toast |
  | `CHECKING STORAGE` | CHECK STORAGE runs (a progress bar) |
  | `STORAGE PROBLEMS: 2`; `PATTERN 1A: AN OLDER VERSION`, `SAMPLE 23: MISSING`, `SET 03: DAMAGED`, `SAMPLE 23: ENTRY AND AUDIO DIFFER` | its list |
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
VOICES: 8, 12, OR 16 WITH THE 2ND CORE
SECOND CORE: A 2ND PROCESSOR, FOR THE
  EFFECTS AND A SHARE OF THE VOICES
CORE SUPPLIES: THE CHIP'S VOLTAGES
```

```
SOUND, KEYS AND KNOBS
THEME: THE COLOURS
KEYS VELOCITY: PLAY HARDER FOR LOUDER
KEYS VOLUME: HOW LOUD WITHOUT VELOCITY
KEYS GAIN: AS HEARD, NOT AS RECORDED
KEYS: THROUGH FX, OR DRY (NO EFFECTS)
KEYS OCTAVE: THE OCTAVE AT POWER-ON
KNOB SPEED: BIGGER STEPS IF TURNED FAST
HOLD DELAY: HOW LONG UNTIL A HOLD
COMBO GRACE: SLACK FOR TWO-KEY PRESSES
PLAYBACK SCROLL: BY ROWS OR BY PAGES
PRESETS LIST: SHOWN AS {PRESETS} TURNS
COUNT-IN: BEFORE A TAKE IN [EDITOR]
  1 OR 2 BARS, OFF, OR KEY:
  THE FIRST NOTE STARTS THE TAKE
QUANTIZE: WHERE A TAKE'S NOTES LAND,
  HEARD AS WRITTEN
```

```
ARP, POWER-ON AND TOOLS
{ARP} (AUTOMATIC REPEAT PROCEDURE):
  ARPEGGIO
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
  OR ALL: TRACKS 0-15 ON CHANNELS 1-16
MIDI OUT CHANNEL: THE {KEYS}' CHANNEL
SYNC: OWN TEMPO, OR AN OUTSIDE CLOCK
CLOCK OUT: SEND THE CLOCK AND START
MIDI CONTROLLER, {EDIT}: THE FM-1 TURNS
  INTO A MIDI CONTROLLER, UNTIL IT IS
  SWITCHED OFF AND ON (IT ASKS FIRST)
CONTROLLER AFTERTOUCH: GENERAL MODE
  SENDS KEY PRESSURE; CONSTANT: NONE
LEARN, {EDIT}: TOUCH AN FM-1 CONTROL,
  THEN MOVE THE MIDI ONE TO DRIVE IT
BELOW: WHAT IT LEARNED, {LFO} DELETES
{SEL}, REVERT: THE MAPPINGS AS SAVED
```

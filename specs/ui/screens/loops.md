# LOOPS

Status: **settled** (2026-10-24), with the stage since 2026-11-03 and its switch since 2026-11-04. The records: [the
RECORD LIVE proposal](../../proposals/202611042000-record-live-tap.md) (a song records what is heard); [the stage
switch proposal](../../proposals/202611041720-loops-stage-switch.md) (ARP's tap, ARP held + REC); [the stage
proposal](../../proposals/202611030330-loops-redesign.md), which changed LOOPS as [the loop station
proposal](../../proposals/202610240236-loops-station.md) made it; that one replaced the first LOOPS ([the UI
proposal](../../proposals/202610072229-ui.md), section 6.5).

LOOPS is for playing live, like a loop station: the loops go round, the keys play over them onto **the stage**, cycle
after cycle, each cycle fixing a part, and REC keeps the stage as a loop the moment it sounds right; the loops are
muted, soloed and mixed while playing over them, and then the whole performance can be recorded into a song. The
loops' record: [../../files/loops.md](../../files/loops.md). LOOPS has its own FX setup and metronome ([fx.md](fx.md)),
so it sounds the same whatever song is current, and SAVE keeps the loops like any change.

There is no precise editing in LOOPS: SAVE TO PATTERN stores the loops in a pattern to edit in the EDITOR, and LOAD
FROM PATTERN brings a pattern back as loops ([below](#storing-in-a-pattern-and-loading-one)).

**LOOPS is a screen of the round** (HOME + POLY, or HOME + SELECT between FX and DJ;
[../navigation.md](../navigation.md)), with a playback of its own:
- **what plays plays on into LOOPS** (a song, the EDITOR's pattern loop) until the loops start here: PLAY, or an armed
  key ([below](#rec-edit-and-the-arm)); it then pauses and keeps its place. What played on pauses as the loops start,
  a count-in begins or the pattern recorder opens ([below](#the-pattern-recorder-arp));
- **the loops play on outside LOOPS,** the stage with them, until a song or a pattern starts; there PLAY pauses and
  resumes them, as what played last, with loops or none ([../transport.md](../transport.md));
- **leaving LOOPS for any screen but FX** disarms an arm, ends a count-in, cancels a song armed and ends a song
  recording (what was heard written), cancels ADJUST, closes the pattern recorder, and the keys down stop writing; the
  loops and the stage play on, and the keys elsewhere never write onto the stage;
- HOME tap in LOOPS opens HOME's combination as on every screen ([../controls.md](../controls.md#homes-combination)),
  but while a song is armed or recorded: it cancels or ends the song ([below](#record-live));
- FX tap does not leave LOOPS: an arm or a song's recording goes on, the keys down stop writing (on FX the keys are
  FX's), FX works on LOOPS' own setup, and FX again goes back ([fx.md](fx.md)); going on elsewhere from that FX leaves
  LOOPS then.

## The loops

- **Up to 16 loops,** in the order they were made, the newest last; after them the placeholder `+ NEW LOOP` while
  fewer than 16 exist. Four are in view at a time, in slots, and all sixteen in the overview column beside them
  ([below](#layout)). A loop's number is its place, `00`-`0F`, renumbered when a loop before it goes; in CONTROL mode
  ([below](#control-mode-mutes-and-solos)) the slots show their white keys, `1`-`16`.
- **One length for all,** in whole bars, always set: 4 bars at first ([below](#the-length)).
- **A loop has one instrument and one colour of its own** (not its engine's: [below](#colours)), given when REC makes
  it from the stage. PRESETS and ENV pick the instrument the keys play, the stage's
  ([below](#the-instrument-and-mono-or-poly)), never a loop's; REC writes it into the loop it sends to, and SEL's APPLY
  INSTRUMENT gives the selected loop the current one. Selecting a loop leaves the current instrument as it is. A kit is
  just a loop's instrument, each note a drum.
- **A loop keeps its notes once, its base, plus a repeat count.** It plays its base from the loops' start as many
  times as its count says, then rests until the length's end; where the length ends inside a repeat, that repeat is
  cut there, drawn and played so. So DOUBLE, a pattern repeated and a length change never copy notes.
- **A loop's SHIFT** moves the whole loop round the length without moving a note: the rows it plays later, its
  repeats and rest turned with it ([ADJUST](#k1-k4-view-and-adjust)); 0 at first. A repeat the length cuts then ends
  at the loop's own row 0, where its first repeat begins again, in the first pass too.
- **POLY or MONO,** as the keys played when REC sent the stage ([below](#the-instrument-and-mono-or-poly)). A POLY
  loop holds up to 4 notes at once, its lanes; a MONO loop one.
- **What a loop holds is notes:** their volumes (KEYS VOLUME), note offs, and with QUANTIZE REAL TIME their note
  delays (`EDx`) and key offs (`Kxx`); a MONO loop's glides and legatos (a TB-303's `SLD`, elsewhere `3xx` or `LEG`).
  The loops' mix turned, FX changes and punch-ins are heard live and written only by RECORD LIVE.
- **A loop's mix** is a track's mix (LEVEL, PAN, DUCK, CHORUS, DELAY and REVERB SENDS), set by K1-K4
  ([below](#k1-k4-view-and-adjust)) and FX's MIXER, where the loops are the columns, with the KEYS column
  ([fx.md](fx.md#mixer)). **A new loop starts with the next loop's mix:** its LEVEL, PAN and DUCK LOOPS' own (K1-K4 on
  `+ NEW LOOP`; plain at first, as a new song's track), **its CHORUS, DELAY and REVERB SENDS the KEYS column's,** as
  they are when REC makes it. So the stage on `+ NEW LOOP` sounds with the KEYS column's sends (a change of them heard
  from the next block), REC writes them into the new loop, and K1-K4 with a send's job on `+ NEW LOOP` turn the KEYS
  column's send ([below](#k1-k4-view-and-adjust)). REC into a loop keeps that loop's own mix.
- **Voices:** 16 loops of 4 lanes can sound more notes than VOICES allows; parking handles them as any notes
  ([settings.md](settings.md#voices)).

## The length

- **Always set:** 4 bars at first (a bar the METRONOME's BEATS A BAR beats, each GRID's rows a beat: [fx.md](fx.md)),
  or the most whole bars that fit 128 rows where 4 do not: 4 bars fit only while GRID's rows a beat × BEATS A BAR is
  32 or less (at 1/16 up to 8 beats a bar, at 1/16T 5, at 1/32 4). 128 rows at most: 8 bars at 1/16, 5 at 1/16T, 4 at
  1/32 (4 beats a bar). GLO's LENGTH and TEMPO change it any time, with loops or none.
- **A TEMPO change is heard at once** while the loops play (GLO's, K4's, the RECORD LIVE panel's), from the tick
  playing, not at the length's end: the loops always play at their own TEMPO and TICKS A ROW (they hold no tempo
  command of their own).
- **PLAY plays the loops with none:** the length goes round (the METRONOME heard when LOOPS' FX has it on, the top
  line's beats lit), and the stage fills ([below](#the-stage)).
- **LENGTH** ([the properties](#the-properties-glo)), a bar a click: **longer** at once, silence added at the end;
  **shorter** only planned while GLO is open (its value yellow, `2 BARS (NOW 4 BARS)`), nothing cut, and cut as GLO
  closes (GLO or HOME), after one question when a note that plays would go (`CUT TO 2 BARS?` / `NOTES IN THE BARS CUT
  GO`, OCT+ `CUT`, OCT- `KEEP`: the length as it was). A shorter length with nothing to lose cuts without asking.
- **A cut** cuts each loop's base and repeats at the new end: a base past it cut there and played once (a note
  sounding over the cut stops at the wrap: a note off at row 0), else as many repeats as reach it; a loop left with no
  notes stays, empty. **A shifted loop** is first written out as it is heard over the old length (its repeats one
  after another, its SHIFT turned into its notes, SHIFT 0), so the bars kept play as they did; the same before a
  longer length. The question counts the notes as heard.
- **Before a longer length** a loop whose last repeat the old length cuts is written out over the old length first,
  as DOUBLE does (its repeats one after another, the last cut there), and repeats the old length never reached are
  dropped, so the new bars are silence: the note sounding at the old line ends there, as the loop goes to rest. A loop
  that only rests at the end needs nothing.
- **LENGTH steps whole bars:** from a part bar (BEATS A BAR changed under the length) to the next whole bar each way:
  5 1/3 bars go up to 6 and down to 5.
- **A part bar is shown in beats,** exact, as the length's rows are always whole beats: `21 BEATS` in the top line,
  GLO's LENGTH (`5 BARS (NOW 21 BEATS)` while a shorter length is planned), DOUBLE's toast and the pattern recorder's
  `REPEATS`; whole bars as ever, `4 BARS`, `1 BAR`. A length of no whole beats (a record written elsewhere) is shown in
  rows, `22 ROWS`.
- **GRID** only with no loops and an empty stage (`GRID: ONLY WITH NO LOOPS`; with notes on the stage `GRID: ONLY WITH
  THE STAGE EMPTY`); the length keeps its bars (as many as the new grid allows; a part bar its whole bars, rounded
  down, at least 1).
- **The length stays** through CLEAR LOOPS, DELETE LOOP of the only loop, and REC. LOAD FROM PATTERN sets it
  ([below](#storing-in-a-pattern-and-loading-one)).
- **Any change of the length** (LENGTH, DOUBLE, GRID, LOAD FROM PATTERN, REVERT) takes the stage with it (cut, a note
  sounding over the cut ending at the line, or longer with empty rows; DOUBLE plays it twice), and the keys down stop
  writing. The play position is taken round the new length (its row modulo the length's rows).

## The stage

- **One loop of the length, belonging to no loop.** It cycles with the loops and is heard in the keys' instrument and
  MONO / POLY (the current ones, [below](#the-instrument-and-mono-or-poly)), through the mix REC would put it in: the
  selected loop's, on `+ NEW LOOP` the next loop's (its sends the KEYS column's, [above](#the-loops)). **Mutes and solos
  never silence it:** what is played is heard. It
  sounds through LOOPS' effects, not KEYS GAIN or the keys' DRY ([fx.md](fx.md#the-keys-dry)).
- **While the loops play on LOOPS, the keys overdub it.** Each note lands as QUANTIZE says ([below](#quantize): a
  note played before its line waits for it; one just after the line sounds at once, as close to the line as it can,
  and is written on the line; REAL TIME: the row it falls in with a note delay `EDx`, its end a key off `Kxx` at its
  tick), and is heard exactly as it is written:
  - **OVERDUB keeps what the stage had:** a note is added, nothing is removed. **POLY:** the note takes a lane that is
    free where it lands (no note sounding there or starting there), of those the one free longest (its next note
    furthest), the other lanes untouched: a long pad played over stays whole. **No lane free:** it takes the lane whose
    note ends soonest and cuts that note where it lands (cut, not removed: its start stays). **A note starting on its
    lane while the key is held** has its start moved on with the key: put down where the key is let go, its end as it
    was; one the key covers whole (its end passed too, or the next note's start) is gone, and one reaching the line
    while the key is held is gone with the line. A note sounding on its lane is silenced from where the key lands while
    it is held; the other lanes play on. Notes played together share the lanes (a key a lane, never one another key
    down holds), keys landing on one row making a chord; a fifth key is neither heard nor written.
  - **MONO:** one lane, so never a free one: a note placed on it cuts the note sounding there, and a note starting
    while its key is held is moved on with it as in POLY; keys landing on one row compete for it,
    the one registered last written and heard (the keys keep their order of registration, also within one scan). A
    note played over a held one takes over as GLO's MONO LEGATO says: `GLIDE` (on a TB-303 its slide, `SLD` on the
    held note; elsewhere a short, clear glide to the note, `3xx`), `LEGATO` (the held note moves to the new one at
    once, with no new attack: its sound goes on, `LEG`, [../../files/commands.md](../../files/commands.md#legato); on a
    TB-303 its slide, on a FUSION instrument a plain new note) or `JUMP` (a plain new note: the held one ends, the new
    one starts with its attack; at first). A MONO overdub on a stage holding chords first makes it one lane for good (the later
    of overlapping notes).
  - **A note off** goes on the nearest row, at least a row after its note (REAL TIME: the key off at its tick).
  - **The span grows with the play line:** a held note moves notes on, and is drawn, only up to the line; ahead of it
    the stage keeps its notes where they are until the line gets there. **The rows not played keep their notes,** so
    each cycle fixes parts.
  - **OVERWRITE** ([below](#the-stage)) is plain: the line erases everything it passes, every lane, and a key held
    there writes over all four lanes, as the line does.
  - **A note is heard once as it is played,** then from the stage each time the line reaches its row.
- **The line** (the length's end, played through): there is no wrap of a note's end. A note held over the line ends
  there: it is written once, from where it started to the length's end (a note off at row 0 where its lane is free),
  its sound let go, its release ringing on into the start until it ends or its lane's next note cuts it. **Its key stops
  writing there:** still held, it writes nothing in the next cycle and its release does nothing (as a stage-off key
  whose lane a 17th key took), so the note plays again where it was written, as written, each cycle: a pad held from
  its row to the end keeps its start. POLY and MONO alike; a key whose note landed in the next cycle (quantized past the
  line) writes there as any.
- **OVERWRITE** instead of OVERDUB: **LFO's tap switches between them,** at its release, with no grace and however long
  it was held (`OVERWRITE`, `OVERDUB`); the stage's title always says which ([below](#the-screen)). OVERDUB at every
  power-on, never saved; REC's send, a clear and a move of the selection leave the mode as it is. That is all LFO does
  here: no layer, nothing while held, no other control closes it, and REC with LFO held is plain REC. HOME alone takes it:
  LFO held, then HOME, is the help, and no switch. While OVERWRITE is on and the loops play, the play line
  erases everything on the stage it passes, on every lane (notes starting there removed, a note running into it cut
  where the line came in, the stage's sounding notes there stopped), and what is played meanwhile is written: that
  stretch becomes what is played, silence included. **So in OVERWRITE a take goes as the line comes round,** unless
  REC has sent it; in OVERDUB it stays. **What EDIT or the pattern recorder put on the stage stays:** each leaves it in
  OVERDUB, switching OVERWRITE off if it was on. LFO does nothing in ADJUST, the pattern recorder and while the stage
  is off. OVERWRITE on rests there, and outside LOOPS (FX too: the stage plays on, its notes kept), and goes on from
  the line once back; stopped, it erases nothing (the line does not move).
- **The stage's clear:** **EDIT held 2 s,** whatever HOLD DELAY says, so a slow tap never reaches it (let go sooner it is
  EDIT's tap; held on, the tap does not happen), **EDIT's tap on `+ NEW LOOP`** (it copies that silence, as EDIT copies a loop) and **SEL's CLEAR STAGE** ([below](#the-sel-menu)) empty the
  stage (`STAGE CLEARED`, the selected loop heard again) and turn it on, as EDIT on a loop does. With the stage already
  empty: off, it just turns on (`STAGE ON`); on, the button blinks. Refused during a count-in (the button blinks), as
  EDIT is; nothing in ADJUST or the pattern recorder.
- **The selected loop is silent while the stage holds notes** (the stage is what REC would put in its place): drawn
  grey, its state `STAGED` in red. With an empty stage it sounds, and with the stage off; selecting a loop alone
  silences nothing. A loop falling silent lets its notes go at once.
- **Moving the selection keeps the stage:** the loop moved to falls silent, the one left is heard again; REC sends the
  stage wherever the selection is then, so EDIT, a move and REC copy a loop.
- **ARP tapped switches the stage off and on** (`STAGE OFF`, `STAGE ON`): a way to play the keys over the loops
  without keeping them. ARP forms a combination here (ARP held + REC, [the pattern
  recorder](#the-pattern-recorder-arp)), so its tap acts once the combination grace has passed with no REC
  ([../controls.md](../controls.md#buttons-tap-hold-and-layers)); held past the hold delay ARP shows its layer (`REC:
  RECORD PATTERN`) and, let go, does nothing. **The stage off:**
  - keeps its notes, drawn much fainter ([below](#the-screen)), and is not heard; the selected
    loop is heard in its place (not `STAGED`, not silent), outside LOOPS too;
  - **the keys, while the loops play, sound at once in the stage's sound** (the keys' instrument and MONO / POLY, the
    mix REC would put the stage in, LOOPS' effects; POLY every key a note of its own, up to 16 at once (a key takes the
    lowest free one of 16 lanes; with all 16 held, a 17th key takes the lanes in turn, round and round, letting that
    lane's note go to ring out its release, and the key that had it, still down, is forgotten: its release does
    nothing; the voice pool's own rule decides which voices sound when it holds fewer); MONO one note, as MONO LEGATO
    says, on the lane it holds, only its key's release ending it), ended by the key's release, with no wait for
    QUANTIZE's line, and are **written nowhere** but in a song recording, as heard; stopped, they play freely as ever;
  - **REC turns it on and records, in one tap:** the loops stopped, it arms at once as REC arms on an empty stage
    stopped, whatever the stage holds (`STAGE ON: ARMED`: REC blinks, the first key or PLAY starts the loops writing);
    the loops playing, the keys write from there, on being all it takes (`STAGE ON`); it sends nothing; a count-in's
    cancel and RECORD LIVE's start stay REC's, and in ADJUST it does nothing; LFO does nothing;
  - **REC's send leaves it off,** the stage empty, so that keeping more is a choice: the next REC or ARP turns it on;
  - **EDIT and the pattern recorder's REC fill it and turn it on:** each puts its notes onto the stage, replacing what
    it held, as ever, and the stage is on again (an explicit fill of the stage turns it on); a clear turns it on too;
  - switched off while the loops are armed, it disarms them (`CANCELLED` in place of `STAGE OFF`; the arm is the
    stage's: its first key would start the loops writing), a count-in with it;
  - the top of the stage's frame reads `STAGE OFF` and `REC: STAGE ON` ([below](#the-screen)), the hint line `STAGE OFF:
    THE KEYS ARE NOT KEPT`, ARP dim.

  Switched on, the stage is heard again from the next row, the selected loop silent again while it holds notes; the
  keys down while it was off sound on until they are let go (or LOOPS is left, or FX takes the keys: they end there).
  The keys down stop writing as it goes off. **On at every
  power-on, never saved;** SAVE, REVERT ALL, LOOPS' REVERT and CLEAR LOOPS leave the switch as it is.
- **A new instrument or mode** (PRESETS, ENV, the pipette, SEQ held + MONO / POLY, CONTROL mode's) keeps the stage's
  notes, played in the new sound; a POLY stage in MONO plays one note at a time (the later of overlapping notes takes
  over, an end counting only on the lane whose note sounds, as REC writes it: counted from the stage's first row, a jump
  too), and POLY again brings its chords back; switched to MONO while chords sound, the stage's other notes end at its
  next row. The keys down stop writing.
- **The keys down stop writing** (each note ends as when its key is let go, its span what the line passed) whenever
  the stage's rows or sound change under them: SELECT, a new instrument or mode, REC, EDIT, the pattern recorder's
  REC, a length change, going to FX, leaving LOOPS, a stop, a jump ([below](#the-play-position-play-and-jumps)).
- **What empties it:** REC (sent), the stage's clear, CLEAR LOOPS, a power cycle; nothing else (OVERWRITE or a
  shorter length may leave it with no note). EDIT on a loop and the pattern recorder's REC replace what it holds (and
  turn it on).
- **Stopped,** the keys play freely (every voice, no grid, through KEYS GAIN), unless REC has armed the loops
  ([below](#rec-edit-and-the-arm)); the stage keeps its notes.
- **Never saved, as an edit mode's copy is not** ([../controls.md](../controls.md#buttons-tap-hold-and-layers)): it is
  no unsaved change (SAVE's LED, the unsaved count and LOOPS' REVERT item take no notice of it). **SAVE and REVERT ALL
  leave it as it is,** and so do LOOPS' REVERT and LOAD FROM PATTERN (fitted to the length they bring), so a SAVE
  mid-jam loses no take. A power cycle drops it.
- **Outside LOOPS** it plays on with the loops, the selected loop staying silent (the stage off: it stays off, the
  selected loop heard); the keys elsewhere never write it.
- **While RECORD LIVE is armed or records, the stage works as ever:** heard, overdubbed, silencing the selected loop,
  switched by ARP's tap, and written into the song as it sounds ([below](#record-live)). Only while the song keeps the
  loops silent (PLAY) the stage is silent with them, its line standing: the keys then sound as with the stage off.
- **Its notes are kept in the edit memory** as a piece of a kind of its own, beside the edit modes' copies, encoded as
  a loop's stream, at most 3,800 B ([../../files/ram.md](../../files/ram.md#the-stage-in-the-edit-memory)); no fixed
  RAM. There is no undo copy of it: REVERT is the way back to what was saved. **A note the stage cannot take is
  neither heard nor written,** and the header says why:
  - the edit memory full: `MEMORY FOR EDITS IS FULL`;
  - its stream at its 3,800 B, a loop's most: `THE STAGE IS FULL`;
  - the store busy (a save being written, or seeding from the computer): `STAGE NOT WRITTEN: STORAGE BUSY`. The keys
    overdub only while the loops play, a save is written while nothing plays and every control waits
    ([../dialogs.md](../dialogs.md#progress)), and seeding silences the FM-1, so no overdub meets a busy store; the
    rule guards what else changes the stage (EDIT, REC, a length change) should one meet it.

## REC, EDIT and the arm

- **REC acts at its press.** With notes on the stage it **sends the stage where the selection is:**
  - **on `+ NEW LOOP`** a new loop: the keys' instrument and mode, the next colour, the next loop's mix, its base the
    length, once, no SHIFT; the selection on `+ NEW LOOP` again (`STAGE AS LOOP 06`);
  - **on a loop** its notes replaced by the stage's, the keys' instrument and mode written into it, its SHIFT 0, its
    place, colour, mix and mute kept (`STAGE INTO LOOP 03`);
  - the stage emptied; while the loops play, what of it sounds goes on as the loop's, without a break. What is written
    is what was heard: MONO one lane (a POLY stage merged as it played); no wrap for the notes played onto the stage
    (one still sounding at the length's end ends at the line); the notes EDIT brought as they are (one crossing the
    line keeps its end past it, as it was heard), and so the notes DOUBLE copied into the stage's second half; and a
    note off or key off that ends nothing (its lane silent there, counted round from the length's end) left out;
  - **refused, nothing changed,** when the loop it writes would pass a loop's 3,800 B (the ends at the line added to a
    full stage: `THE STAGE IS FULL`, REC blinking), or the edit memory cannot hold it (`MEMORY FOR EDITS IS FULL`);
  - **an arm ends with it.**
- **The stage empty:** while the loops play REC is refused (`THE STAGE IS EMPTY`, REC blinks); stopped, **REC arms:**
  REC blinks, the header's ● is red, the top line's `REC` reads `ARMED` in red, and the hint line says `ARMED: A NOTE OR
  PLAY STARTS AT 2.4.3` (the play position). Then the first key starts the loops from the play position at once, its
  note the stage's first (it lands as any overdub's; the keys of a chord pressed with it land with it); PLAY starts them
  after GLO's COUNT-IN (OFF at first: at once; 1 or 2 bars of clicks, [../transport.md](../transport.md#count-in)). REC
  again disarms (`CANCELLED`). **During the count** REC, PLAY and HOME cancel it and disarm (`CANCELLED`), REC sending
  nothing even with notes on the stage (EDIT can fill it while armed); the keys play freely then, nothing written.
  Switching the stage off disarms too (`CANCELLED`, [above](#the-stage)).
- **The stage off,** REC turns it on, sending nothing: the loops stopped, it arms at once as above, whatever the stage
  holds (`STAGE ON: ARMED`); playing, the keys write from there (`STAGE ON`); a count-in's cancel and RECORD LIVE's start
  stay as they are ([above](#the-stage)). **A send leaves the stage off:** what is played after it is kept once REC or
  ARP has turned it on, so after a send, stopped, one REC arms the next take.
- **ARP held + REC opens the pattern recorder** at REC's release ([below](#the-pattern-recorder-arp)): REC's press
  then does none of its own jobs (no send, arm, count-in's cancel or song start), and ARP's tap does not happen
  after it. ARP let go within the combination grace counts as held; REC pressed before ARP is plain REC.
- **The order REC's press is taken in:** ARP held, the pattern recorder (above), nothing at the press, however long
  REC is held; a count-in running, it cancels; RECORD LIVE armed, REC starts it ([below](#record-live)); then, the
  stage off, it turns it on (and, the loops stopped, arms); else the stage's, as above, a song recording or not. In
  ADJUST REC does nothing (it blinks), ARP held or not. LFO held changes none of this.
- **For 500 ms after REC does a job, REC does nothing,** no blink and no toast, so a double tap does one job: after
  it sends the stage (never arming after it), turns the stage on (never sending it after), arms or disarms, cancels a
  count-in, puts the pattern recorder's pattern onto the stage (never sending it after), arms RECORD LIVE from its panel
  or starts it (never sending the stage at once). A refused REC starts no wait, and REC with ARP held (the pattern recorder, a combination) is outside it.
- **EDIT copies the selected loop onto the stage,** as it is heard (its repeats written out, a note off where it goes
  to rest or a repeat is cut, its SHIFT applied), in place of what the stage held, turns the stage on if it was off and
  leaves it in OVERDUB, so the line keeps those notes ([above](#the-stage)); its instrument and MONO / POLY become the
  keys' (as ENV held does); the loop falls silent at once (`LOOP 03 ON THE STAGE`). The loop itself is
  unchanged until REC writes the stage back. On `+ NEW LOOP` EDIT's tap is the stage's clear, and held past the hold
  delay EDIT is that clear anywhere ([above](#the-stage)); refused during a count-in.
  A loop that, written out, would take the stage past its 3,800 B is refused, nothing changed (`THE STAGE IS FULL`,
  EDIT blinking).
- **No undo of REC:** REVERT brings back what was saved.

## The play position, PLAY and jumps

- **The play position** is where PLAY starts and where an armed key starts the loops. **ALGORITHM** moves it a row a
  click (SEL held: a bar), round the length: stopped, the header shows `PLAY FROM 2.4.3` (bar, beat, and the step when
  not on a beat); while the loops play they **jump** there at once (`JUMP +1 ROW`, `JUMP -16 ROWS`), from the next row.
- **A jump** is no line: the notes sounding ring on as at a tracker's jump (each until its lane's next cell), a loop
  landing in its rest falls silent there, a key held stops writing (its note ends as when it is let go, its span what
  the line passed), OVERWRITE goes on from where the loops land, and the stage plays what it holds from there (a jump
  back plays what was just written). A jump back keeps the loops' pass; one forward past the length's end counts the
  next pass. While a song keeps them silent (PLAY), a jump moves the place they come back on from.
- **PLAY** (at its press): stopped, the loops play from the play position, with loops or none; armed, after COUNT-IN;
  **playing, they stop and the play position goes back to the start** (the stage kept, the keys down no longer
  writing), PLAY's red then dim: at the start, as everywhere. **PLAY held:** stop, back to the start (`BACK TO THE
  START`). What played on into LOOPS pauses as the loops start or the count begins.
- **While a song is armed or recorded, PLAY is RECORD LIVE's** ([below](#record-live)): armed, when the loops did not
  play as it was armed and do not play now, PLAY starts the loops and the song after COUNT-IN (else PLAY is the loops'
  own, above); recording, PLAY acts as it is let go, a tap silencing the loops or bringing them back on the song's next
  row, held taking them to their start (playing from the top on the song's next row, or waiting there silent), the song
  going on.
- **Outside LOOPS** PLAY pauses and resumes the loops when they played last, as anything that played last, with loops
  or none ([../transport.md](../transport.md#play)); paused there they keep their place, and PLAY on LOOPS goes on from
  it (the play position).
- **SYNC EXTERNAL** ([../../connections.md](../../connections.md#clock)): the master's START plays the loops from bar 1
  (the play position 0), STOP pauses them where they are (CONTINUE goes on from there), the song position moves the
  play position (at once while they play: a jump); armed, START or CONTINUE starts them as PLAY does, without the
  count-in; with no loops they play all the same. RECORD LIVE as [below](#record-live).

## The instrument and MONO or POLY

- **The keys' setup is the current instrument and its MONO / POLY:** the stage plays in it, and REC writes both into
  the loop it sends to. The footer shows the instrument at the left, `MONO` or `POLY` at the right. POLY at first.
- **PRESETS** the instrument; **ENV** the cartridge list (`NEW LOOP: A CARTRIDGE`: its first instrument); **ENV held,
  the pipette:** the selected loop's instrument and MONO / POLY become the keys' (`KEYS: 01:04 BRASS, MONO`; on
  `+ NEW LOOP` nothing); SEL's **TAKE ITS INSTRUMENT** is its tap route.
- **MONO / POLY:** CONTROL mode's MONO and POLY keys, or **SEQ held + MONO or POLY** (`KEYS: MONO`, `KEYS: POLY`).
- **APPLY INSTRUMENT** (SEL) gives the selected loop the keys' instrument and MONO / POLY (`LOOP 03: 01:04 BRASS,
  MONO`).

## K1-K4, VIEW and ADJUST

- **K1-K4 each have a job,** set in GLO (`K1 TURNS` ... `K4 TURNS`), from: LEVEL, PAN, DUCK, CHORUS SEND, DELAY SEND,
  REVERB SEND (the selected loop's; on `+ NEW LOOP` the next loop's, whose sends are the KEYS column's: a send turned
  there turns FX MIXER's KEYS column, as its own column does, a setting, unsaved until SAVE, [fx.md](fx.md#mixer)),
  TEMPO (the loops', as GLO's), VIEW. At first K1 LEVEL, K2 PAN, K3 REVERB SEND, K4 TEMPO. SEL held: a mix value by 16,
  TEMPO by 10. The header shows the turn as everywhere (`LEVEL 7F 100%`, `REVERB SEND 7F 100%`, `TEMPO 78 (120 BPM)`,
  `VIEW 2 BARS`). Kept with LOOPS' setup.
- **VIEW:** 1, 2 or 4 bars a screen, 2 at first (GLO's `VIEW`: `2 BARS A SCREEN`; a knob's job too): what the strips
  show ([below](#the-strips)).
- **ADJUST** (SEL, on a loop) moves the whole loop round the length without moving a note: **its SHIFT,** the rows it
  plays later (`-3 ROWS` shown the nearer way round). **K1** a row, **K2** a bar; ALGORITHM still the play position;
  SELECT, K3 and K4 nothing meanwhile. The loops play on, the shift heard and drawn at once. **SEL keeps it** (`LOOP
  03: SHIFT +17 ROWS`), **HOME cancels** (`CANCELLED`: the SHIFT as it was). Open: the header's subpage `ADJUST`, the
  top line `LOOP 03: SHIFT +17 ROWS`, the hint line `K1 ROW  K2 BAR  SEL KEEP  HOME CANCEL`; REC, LFO, EDIT, GLO, ENV,
  SEQ and ARP do nothing. Leaving LOOPS cancels it. **On the loop the stage stands for** (silent while the stage holds
  notes) the shift is drawn at once and heard once the loop sounds again; REC writing the stage into it sets SHIFT 0.
- **SHIFT is honoured wherever a loop is read:** the player (a loop plays its row (row − SHIFT) round the length, its
  repeats and rest turned with it), the strips, EDIT, SAVE TO PATTERN, SEL's QUANTIZE (its lines
  as heard), LENGTH (written out as heard first). Each reads the loop as it is heard: written out over the length, then
  turned SHIFT rows. **SHIFT goes to 0** when REC writes the loop, when LENGTH writes it out, and on LOAD FROM PATTERN;
  DOUBLE keeps it (the loop plays the same).

## QUANTIZE

GLO's QUANTIZE and CONTROL mode's black keys OP1-OP5; `ROW` at first. The EDITOR's realtime takes have the same
values in SETTINGS' QUANTIZE ([settings.md](settings.md), [editor.md](editor.md#realtime-takes)).

| Value | What a note lands on |
|---|---|
| `2 ROWS`, `3 ROWS`, `4 ROWS` | the nearest line of that many rows, from the loops' start |
| `ROW` | the nearest row (the 1/32 rows too at GRID 1/32) |
| `REAL TIME` | the row it falls in, with a note delay `EDx` of the ticks since, and its end as a key off `Kxx` at its tick ([../../files/loops.md](../../files/loops.md#recording-on-the-grid)) |

**SEL's QUANTIZE** moves a loop's notes once they are kept ([the SEL menu](#the-sel-menu)).

## The pattern recorder (ARP)

**ARP held + REC** opens it, at REC's release ([above](#rec-edit-and-the-arm)), and so does SEL's **RECORD PATTERN**
(its tap route: [the SEL menu](#the-sel-menu)), over the loops, the loops playing on, so the pattern is heard in the
mix while it is built. Refused while armed and during a count-in (REC and ARP blink, SEL from the menu). A song armed or
recorded allows it: its preview is heard and so written, and inside the recorder REC is the recorder's, HOME its
close. With the stage off it opens, and its REC turns the stage on. Inside it, ARP is its own (the preview, below).

- **4 rows of up to 16 steps, a note a row** (with a kit, each row a drum); **each step a grid row** (16 steps at 1/16
  are a bar). The pattern repeats over the length from the loops' start, the last repeat cut at the length's end
  (`4 X 16 = 64 ROWS, 2 LANES`; 12 steps over 4 bars of 16 rows: `5 X 12 + 4 = 64 ROWS, 1 LANE`), so it starts again
  with the loops each pass. **SELECT** picks the row the white keys switch.
- **A white key tapped** switches its step on or off, at its release; a step switched on sounds its note while the
  preview is stopped. **Held past the hold delay** ([../controls.md](../controls.md)), the step becomes a tie, held
  from the step before, when that step is on or a tie; after a step off, or as the first step, it becomes a plain note
  (a held run starts with one). No tie is ever left alone: switching off a run's first note, or a tie in its middle,
  makes the next tie a plain note. A tie adds its steps to the note's length. Keys past LENGTH do nothing.
- **K1 NOTE LENGTH,** how long each step's note is held: the lengths GRID makes whole rows of (`1/32`, `1/16T`, `1/16`,
  `1/8T`, `1/8`, `1/4`, `1/2`, `1 BAR`) and `TO THE NEXT` (it rings until its row's next note); `1/16` at first (a
  row's length where GRID lacks it). **The row's next note always ends the one before;** notes on other rows overlap
  (a pad's chord rings). **K2 LENGTH:** `4`, `8`, `12` or `16 STEPS`; 16 at first. The line `REPEATS` `OVER 4 BARS`,
  dark grey, has no knob: it says what the pattern fills.
- **The selected row's note:** **K3 NOTE** a semitone a click, heard; or **SEL tapped, then a key:** that key, white or
  black, sets the note at the keyboard's octave, heard, and the keys go back to the steps (`KEYS: THE NOTE`, `KEYS: THE
  STEPS`; SEL again before a key: back to the steps); **SEL held + a key** does the same at once, a shortcut. OCT- /
  OCT+ move the keyboard's octave as on every screen, and the selected row's note with it, heard (`NOTE C-5`); of the
  other rows only those never set below it change (they take its note). The first row starts on C at the keyboard's
  octave when the recorder opens; a row never set starts on the note of the row above
  it. SEQ does nothing here.
- **ALGORITHM turns TEMPO** here, as GLO's TEMPO does (SEL held: by 10): the recorder has no play position.
- **The preview plays from the moment it opens,** in the stage's sound (opened on a loop, the loop's instrument) and
  through the stage's mix, on the stage's own column: no mute or solo silences it, as none silences the stage. A note
  heard on its own (K3, OCT, SEL + a key, a step switched on) sounds the same way: that instrument, through that mix, a
  step long (0.15 s at least).
  - **While the loops play,** the step playing is where the loops are: the place the pattern lands after REC (its
    first step on the loops' start), moving with them. ARP only switches the pattern's sound off and on.
  - **While they do not,** ARP starts and stops the preview, alone at LOOPS' tempo, from its first step; what plays on
    into LOOPS (a song, the EDITOR's pattern loop) pauses as the recorder opens, keeping its place.
  - **PLAY pausing the loops** leaves the preview going on its own from where it was; ARP then stops it.
  - **PLAY starting the loops** makes a preview that plays jump to the loops' place, in time with them from then on.
- **REC puts the pattern onto the stage** (`PATTERN ON THE STAGE`), replacing what it held, repeated over the length,
  turns the stage on if it was off, leaves it in OVERDUB as EDIT does ([above](#the-stage)), and closes; the preview's
  sounding notes go on as the stage's, without a break:
  with the loops playing, in their time; with the preview playing alone, the loops start from where it was; with
  nothing playing, nothing starts. The stage plays in the sound the preview had: opened on a loop, that loop's
  instrument becomes the keys'; POLY when the pattern fills more than one row. Each step on is a note at KEYS VOLUME;
  rows with no step on are left out. REC on the screen then sends the stage, as any ([above](#rec-edit-and-the-arm)).
  No step on: refused (`NO STEPS ON`, REC blinks). **HOME** closes it, nothing written.
- **It opens clean every time:** no steps or ties, the first row selected, the notes as above, NOTE LENGTH and LENGTH at
  their first values. Nothing is kept from the last time, after REC or HOME alike, and nothing is saved.

## CONTROL mode, mutes and solos

**SEQ tapped switches the keys between PLAY (notes) and CONTROL** (`KEYS: CONTROL`, `KEYS: PLAY`), at its release; in
CONTROL mode SEQ's place in the header reads `SEQ MUTE` or `SEQ SOLO` ([../frame.md](../frame.md#the-header)), the
slots show their keys, and the hint line says `WHITE KEYS: MUTE  HELD: AT THE BAR` (`SOLO`). **SEQ held + a key** acts
as CONTROL mode's key once, the keys staying in PLAY (SEQ's own tap then does not happen); while SEQ is held the slots
and the hint line show as in CONTROL mode.

- **White keys 1-16: loops 1-16** in their order. In MUTE, a tap mutes or unmutes the loop at once; held past the hold
  delay ([../controls.md](../controls.md)), it waits for the next bar line, the slot showing a yellow `M` meanwhile. In
  SOLO the same for solos, one loop at a time: a key solos its loop alone (the other solos off), and on the soloed loop
  takes its solo off; held, each solo that changes waits for the bar line (a yellow `S` meanwhile). While a loop is
  soloed only it sounds (and the stage, which no mute or solo silences); mutes are kept for when the solo clears.
  Stopped, both act at once. Mutes are saved with the loops; solos are not (none after a power-on,
  [../../files/loops.md](../../files/loops.md)). A key with no loop does nothing.
- **Black keys, by their print:** OP1, OP2, OP3 QUANTIZE `2 ROWS`, `3 ROWS`, `4 ROWS`; OP4 `ROW`; OP5 `REAL TIME`
  (`QUANTIZE: 2 ROWS`, ...); PIT all back on, every mute and solo cleared (`ALL LOOPS ON`); the key printed GLO MUTE
  ↔ SOLO (`SEQ: MUTE`, `SEQ: SOLO`); MONO and POLY the keys' mode (`KEYS: MONO`, `KEYS: POLY`; the footer shows it).
  OP6 and the unprinted key do nothing.
- **HOME's combination,** on every screen ([../controls.md](../controls.md#homes-combination)): white key *n* mutes or
  unmutes loop *n* at once, held solos it, after SEQ solos (key 1 the first loop; a solo as CONTROL's, one loop at a
  time).
- **A muted loop runs on silently,** in time; its slot is dark grey. So is a loop silenced by another's solo.
- **Mutes are kept with the loops** (SAVE keeps them); solos are not.

## RECORD LIVE

- **SEL → RECORD LIVE opens its panel** ([below](#the-record-live-panel)): the new song's **NAME** (`LOOPS 01`, or
  one past the highest `LOOPS ##` a song has; EDIT opens the text entry, [../dialogs.md](../dialogs.md#text-entry))
  and **K1 TEMPO**, LOOPS' own tempo, changed at once and heard (the song starts at it); the panel's ALGORITHM turns it
  too, as GLO's TEMPO does (the panel has no play position). **REC (ARM)** arms it and closes the panel; **HOME
  (CANCEL)** closes it, nothing armed. Armed: REC blinks, the top line's `REC` reads `SONG`, red, and SEL's item reads
  `CANCEL RECORD LIVE` (`CANCELLED`).
- **Armed, it starts with:** REC (nothing else starts: the loops go on as they were, playing or not); a note (the note
  its first); PLAY, when the loops did not play as it was armed and do not play now (the loops and the song start
  together, after COUNT-IN). Whether the loops play does not matter for arming.
- **A song records everything that sounds, whatever LOOPS does:** it is a layer before the output, a tap where notes
  enter the voices. Every note that sounds is written as it sounds, from any source:
  - the loops as they are heard: repeats, SHIFT and a change made meanwhile all as played; a muted or non-soloed loop,
    or the selected loop where the stage holds notes, is not heard and so not written, and a note sounding where a loop
    falls silent ends there;
  - the stage's notes each cycle, OVERWRITE's erasing included;
  - the keys as heard: with the stage on as they land on it, by QUANTIZE; with it off, or while the song keeps the loops
    silent, as played, each at its tick;
  - the pattern recorder's preview and its single notes;
  - the keys on another screen a song goes on through (FX), on tracks with the KEYS column's mix, at full level.

  A FUSION note is written as one note; its layers follow on playback.
- **Only notes that have a voice are written;** the voices are the VOICES count (8, 12 or 16,
  [settings.md](settings.md#voices)). A note given no voice is not written; a note losing its voice, taken by another or
  parked past the count, gets a note off where it lost it; a parked note coming back is not written again. A replay of
  the song may still park other notes, so what overlaps can differ there: their releases overlap.
- **LOOPS works as ever while a song is armed or recorded:** EDIT, the pattern recorder, APPLY INSTRUMENT, ADJUST,
  QUANTIZE, DELETE LOOP, DOUBLE, LENGTH, GRID, CLEAR LOOPS, LOAD FROM PATTERN and REVERT, the mutes, solos, FX, punch-ins,
  tempo and mixes; each is heard, and so written. REC keeps its jobs ([above](#rec-edit-and-the-arm)): it sends the
  stage as anywhere, and the song writes the loops as heard, the change included; with the stage off it turns it on as
  anywhere (the loops' replay runs on: no arm); armed, it still starts the song. What is otherwise a stop is not one here, as the song's time runs on:
  LOAD FROM PATTERN leaves the loops silent at their start (PLAY brings them on), and its list's PLAY previews nothing.
- **The song's time runs on from its start whatever the loops do:** while it records, the loops' replay never stops,
  and PLAY on LOOPS works any time without ending it ([above](#the-play-position-play-and-jumps)):
  - **PLAY, the loops not playing:** they play on the song's next row, from where they stopped;
  - **PLAY, the loops playing:** they fall silent from the song's next row, their place kept, the stage with them;
    silent rows hold only what the keys played;
  - **PLAY held:** the loops go to their start: playing, from the top on the song's next row; not playing, they wait at
    their start.
- **Its rows are its own,** fixed at its start with LOOPS' tempo, ticks a row, swing and FX setup then. Each note is
  placed by time, counted in the replay's ticks: its row, a note delay `EDx` for the ticks after the row's start (15 at
  most); its end a note off on a row's first tick, else a key off `Kxx` at its tick (a note shorter than a tick lasts
  one). So nothing LOOPS does later (GRID, LENGTH, DOUBLE, a loop changed or deleted) changes how it is written.
- **It writes a new song** at the first free number, under the panel's NAME: a pattern every 128 rows from its start,
  a position each, in order; no pattern is shared and none is aligned to the loops. FX turns, effects switched on and
  off, punch-ins, tempo changes and the mixes turned are commands at their rows, each in the first cell with room. A
  command goes at the row playing when the firmware sees the change (a pass of its main loop, tens of milliseconds
  apart), not at the turn itself: a turn and back between two passes writes nothing.
  Instruments' parameters (turned from a MIDI map) are not written. The row playing as it starts is its row 0: the
  notes that row has started are written, the tap running from the moment it is armed.
- **It plays once and stops at its end.**
- **HOME tapped ends it:** the row HOME was pressed in is the song's last (what was written after it, while the tap
  waited for HOME's release and the combination grace, cut away), the notes sounding end there, and the loops play on,
  LOOPS staying (`SONG 12 RECORDED: 1:23, 5 PATTERNS`: the time recorded and the patterns written). Ended before its
  first row: nothing written (`CANCELLED`). **Armed, HOME cancels it** (`CANCELLED`), as it cancels a count-in. HOME's
  tap opens no combination then; held it is HOME's combination as ever. Inside the pattern recorder or a dialog HOME is
  theirs (cancel, close), so HOME again ends the song.
- **16 tracks:** a note goes to a track whose mix is its source's (a loop's LEVEL, PAN and sends; the stage's, the
  stage-off keys' and the preview's the mix REC would use), on a track free at its row: the track its lane held first,
  then any with that mix, then one unused, then the one whose mix differs least, which mixer commands on the note bring
  to the source's. Instruments are per cell. A mix turned is mixer commands, at their row, on the tracks that mix holds.
  A track takes one note a row; what finds no track is heard live, left out, and counted (`SONG 12: 3 NOTES LEFT OUT`,
  at its end).
- **Leaving LOOPS for another screen ends it at once,** what was written kept; FX and back, ENV's cartridge list,
  PRESETS and the help are not leaving. PLAY on FX does end it, as do the master's START and STOP there: each takes
  the loops off. **So do SAVE NOW and REVERT ALL,** first: a save under way takes no more edits, so the song is written
  before it, and REVERT ALL drops the song with the other changes. SAVE itself waits for a pause, as ever.
- **It is an edit, kept in the edit memory:** the FM-1 cannot write the flash while sound plays, so the song is
  built in RAM and SAVE keeps it. It runs until the edit memory, the pattern numbers or the song's 256 positions run
  out, then stops and says why (`SONG 12 STOPPED: MEMORY FOR EDITS IS FULL`, `SONG 12 STOPPED: ALL 240 PATTERNS
  USED`, `SONG 12 STOPPED: 256 POSITIONS`): what waits for its pattern, up to 128 rows, goes, and with no pattern
  written yet no song is made. With no song number free it is refused at once (`NO SONG NUMBER FREE`). A
  pattern's notes wait in RAM until its rows are heard ([../../files/ram.md](../../files/ram.md#the-edit-memory-and-the-spare-zone)):
  about 2,000 notes and ends; past that the notes that come are left out and counted (a note off, a mix or a command
  that finds no room is dropped, not counted). Of the row playing when it starts, the FM-1 keeps the last 32 events
  (notes, ends, voices taken): a row with more loses its oldest, not counted.
- **SYNC EXTERNAL** ([../../connections.md](../../connections.md#clock)): armed, the master's START starts it as PLAY
  does, without the count-in. Recording, its STOP silences the loops and CONTINUE brings them on, START takes them to
  their start, the song going on; a song position does nothing.

## Storing in a pattern and loading one

- **SEL → SAVE TO PATTERN** opens the pattern list ([../dialogs.md](../dialogs.md#the-pattern-list)), titled `SAVE
  THE LOOPS TO`, with `NEW` first and picked (the first free number); SEL takes it, or another pattern, which asks
  first when it is not empty (`PATTERN 2A IS NOT EMPTY` / `REPLACE IT WITH THE LOOPS?`). It writes one pattern of the
  loops' length (`LOOPS STORED IN PATTERN 2A`): every loop written out as it is heard (its repeats, a note off where
  it goes to rest, the cut at the wrap, then turned SHIFT rows), so the pattern plays what LOOPS played, each note
  naming its instrument, lanes packed into the 16 tracks where they do not overlap (what does not fit left out and
  counted: `PATTERN 2A: 3 NOTES LEFT OUT`); each loop's mix as mixer commands (`=`) with its first note on a track, and
  again where a shared track turns to another loop. The stage is not written. The loops go on as they were. A
  built-in pattern (brown in the list; the firmware has none yet) takes them as any: a SAVE keeps the change in the
  user area ([../saving.md](../saving.md#fws)). The EDITOR's own pattern is never listed.
- **SEL → LOAD FROM PATTERN** opens the pattern list, titled `LOAD INTO LOOPS` (its PLAY previews each pattern at the
  timing LOAD brings), and replaces the loops after a question when there are loops (`REPLACE THE LOOPS` / `WITH
  PATTERN 2A?`). The loops stop, the play position and the selection at their start; the stage stays, fitted to the
  new length.
  - **The length** is the pattern's rows in whole bars (rounded up, at most 128 rows).
  - **The timing,** as the pattern plays in its song (the current song if it plays the pattern, else the first song
    that does): its TEMPO, TICKS A ROW, ROWS A BEAT and SWING. ROWS A BEAT that GRID lacks keeps the loops' GRID,
    the TEMPO scaled to the same BPM; a pattern in no song keeps the loops' own.
  - **A loop an instrument:** its notes from every track that plays it, up to 4 at once as its lanes; more at once
    makes another loop of the same instrument. Loops are made in the order their instruments first play. Each loop's
    repeat count is 1, its SHIFT 0, POLY, its mix from the `=` commands it finds with its notes (else plain), its
    glides (`SLD`, `3xx`), legatos (`LEG`) and its note delays and key offs kept.
  - **As much as fits:** 16 loops, 4 lanes each; the rest, and the commands a loop does not hold, left out and
    listed (`PATTERN 2A IN LOOPS: 3 LOOPS`; `PATTERN 2A IN LOOPS: 3 INSTRUMENTS LEFT OUT`, `... 12 COMMANDS LEFT
    OUT`).

## Controls

| Control | Does |
|---|---|
| SELECT | the loops in their order, then `+ NEW LOOP` (clockwise down, as in every list); the slots in view follow it; the stage stays ([above](#the-stage)) |
| ALGORITHM | the play position, a row a click, round the length: stopped, where PLAY starts (`PLAY FROM 2.4.3`); playing, a jump there at once (`JUMP +1 ROW`); SEL held: a bar ([above](#the-play-position-play-and-jumps)). In the pattern recorder and the RECORD LIVE panel: TEMPO |
| K1-K4 | each its job from GLO: the selected loop's LEVEL, PAN, DUCK, CHORUS SEND, DELAY SEND or REVERB SEND (on `+ NEW LOOP` the next loop's, a send there the KEYS column's: a setting), TEMPO or VIEW; at first LEVEL, PAN, REVERB SEND, TEMPO; SEL held: a mix value by 16, TEMPO by 10 ([above](#k1-k4-view-and-adjust)). ADJUST: K1 a row, K2 a bar |
| PLAY | at its press: stopped, the loops play from the play position, with loops or none; armed, after COUNT-IN; playing, they stop and go back to the start. A song armed or recorded: RECORD LIVE's ([above](#record-live)) |
| PLAY held | stop, back to the start ([../transport.md](../transport.md)); recording a song: the loops to their start |
| REC | at its press: notes on the stage, sent where the selection is (a new loop on `+ NEW LOOP`, into the loop on a loop); the stage empty: stopped, arms (again: disarms), playing, refused; a send leaves the stage off; the stage off: turns it on and, stopped, arms (`STAGE ON: ARMED`; playing `STAGE ON`). A count-in: cancels it. RECORD LIVE armed: starts it. A song recording changes none of this. Each job done, nothing for 500 ms ([above](#rec-edit-and-the-arm)) |
| LFO | at its release: OVERDUB and OVERWRITE switched (`OVERDUB`, `OVERWRITE`; ADJUST, the pattern recorder, the stage off: nothing); nothing else ([above](#the-stage)) |
| EDIT | the selected loop onto the stage, as heard, the stage on, in OVERDUB; on `+ NEW LOOP` the stage's clear ([above](#the-stage)) |
| EDIT held | 2 s, whatever HOLD DELAY says: the stage's clear (`STAGE CLEARED`), on a loop and on `+ NEW LOOP`; its tap then does not happen, let go sooner it is the tap |
| keys | PLAY mode: the keys' instrument; overdubbing the stage while the loops play (the stage off: heard at once in its sound, written nowhere); armed, the first starts them; stopped, played freely. CONTROL mode: [above](#control-mode-mutes-and-solos) |
| SEQ | the keys: PLAY ↔ CONTROL, at its release |
| SEQ held + a key | that key as in CONTROL mode, once: white keys mute or solo, OP1-OP5 QUANTIZE, PIT all on, GLO's key MUTE / SOLO, MONO or POLY the keys' mode |
| ARP | the stage off and on (`STAGE OFF`, `STAGE ON`; [above](#the-stage)), at its tap, once the combination grace has passed with no REC |
| ARP held | past the hold delay: its layer (`REC: RECORD PATTERN`); let go: nothing (no arpeggiator in LOOPS) |
| ARP held + REC | the pattern recorder, at REC's release, REC's press doing none of its own jobs ([above](#the-pattern-recorder-arp)); SEL's RECORD PATTERN the same |
| PRESETS | the keys' instrument: the stage's sound, which REC writes into a loop; never a loop's own |
| ENV | the cartridge list, titled `NEW LOOP: A CARTRIDGE`: the keys' instrument becomes the picked cartridge's first |
| ENV held | the pipette: the selected loop's instrument and MONO / POLY become the keys' (SEL's TAKE ITS INSTRUMENT the same) |
| OCT- / OCT+ | the keys' octave |
| FX | LOOPS' FX setup, FX again back; FX held + a white key: a punch-in (heard; written only by RECORD LIVE) |
| GLO | the properties ([below](#the-properties-glo)) |
| SEL | the menu ([below](#the-sel-menu)); in ADJUST: keep the SHIFT |
| HOME | HOME's combination, as on every screen ([above](#control-mode-mutes-and-solos)); a count-in: cancels it; ADJUST: cancels it; a song armed: cancels it; a song recording: tapped, ends it, its last row the row HOME was pressed in ([above](#record-live)) |

**The LEDs** ([../leds.md](../leds.md)):
- **REC** blinking while armed, a song armed or a count-in runs; full while a song records or the stage holds notes
  and is on (REC sends them); else dim.
- **LFO** full while OVERWRITE is on (a mode), else dim.
- **EDIT** dim on a loop; on `+ NEW LOOP` dim while the stage holds notes or is off (its tap clears it, on), else dark.
- **SEQ** full in CONTROL mode or held, else dim. **ARP** full while the stage is on, dim while it is off. **ENV**
  dim.
- **PLAY** as for the loops ([../transport.md](../transport.md#plays-two-leds)): stopped, its red never dark on LOOPS
  (the loops play with none); dim at the start, after a stop too. **While the loops play it is a visual metronome**, by
  LOOPS' METRONOME on or off (its TEMPO and BEATS A BAR): a quick 100 ms flash a beat, red on the bar's first, green on
  the others, dark between.
- **ADJUST open:** SEL full, HOME dim; REC, LFO, EDIT, GLO, ENV, SEQ, ARP dark.
- **The pattern recorder:** ARP full, REC dim, SEL full while it waits for a key (else dim); GLO, SEQ, EDIT, LFO, ENV
  dark; the keys as [its layout](#the-pattern-recorder) says.
- **The keys** in CONTROL mode or with SEQ held: white key *n* full while loop *n* sounds, dim muted or silenced,
  blinking while its mute or solo waits, dark with no loop; OP1-OP5 the QUANTIZE chosen full, the others dim; PIT dim;
  the GLO key full in SOLO, dim in MUTE; MONO or POLY full by the keys' mode, the other dim. In PLAY mode the keys
  light with their notes. HOME's combination open: white key *n* as in CONTROL mode.

## Layout

The body (y 17-226, [../frame.md](../frame.md)): the top line, four slots, the overview column at the right, the
stage, the length's bar, the hint line. **The header** shows `LOOPS` with no mark, and the subpage `ADJUST` while
ADJUST is open. Nothing moves or changes size when a hint, the arm, OVERWRITE or the stage's switch comes or goes.

### The screen

Two loops, loop 01 selected with notes on the stage, the play line in bar 1 of 4:

```
LOOPS                         > |##9     S: the name; the transport, CPU, voices, battery
120 BPM 4 BARS  Q ROW   REC     ## .. .. T: tempo, length, QUANTIZE, REC; the beats
----------------------------------------
   SYN PIANO P X2 SHIFT +3            #  slot 0: the number (S) in the gutter; sound, P / M,
00 ##.##..#|#.##..##.|..            7FO    repeats, SHIFT, state; its strip, its LEVEL; the overview at x 229
+--------------------------------------+
|  SYN PIANO M                 STAGED  |#  the selected slot boxed; silent while the stage
|01 ........|.........              7F|+    holds notes: grey, STAGED red, its LEVEL dark grey
+--------------------------------------+
   NEW LOOP 02          08:00 SYN PIANO    + NEW LOOP: the number it takes, the keys' instrument
 + :REC: A NEW LOOP...............: 7F     a dashed strip: where REC puts the stage; the next loop's LEVEL

-STAGE OVERDUB LFO --- REC: INTO LOOP 01   the stage: OVERDUB or OVERWRITE, LFO's tooltip, REC's target
|o  ####..##|##.....                   |   its strip, in the selected loop's colour
+--------------------------------------+
   1 ------ 2 ------ 3 ------ 4 ------     the length's bar: a segment a bar, the play tick
WHITE KEYS: MUTE  HELD: AT THE BAR         the hint line (only where a state changes the keys)
08:00 SYN PIANO JUNO                MONO   the footer: the keys' instrument; MONO / POLY
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the tempo | 2, 19 | | T | `120 BPM`: the loops' tempo as BPM |
| the length | 50, 19 | | T | `4 BARS`, `1 BAR`; a part bar in beats, `21 BEATS` (8 characters at most: `32 BEATS`) |
| QUANTIZE | 98, 19 | | T | `Q 2 ROWS`, `Q 3 ROWS`, `Q 4 ROWS`, `Q ROW`, `Q REAL` |
| REC | 146, 19 | | T | `REC`; `ARMED` (armed, counting in); `SONG` (a song armed) |
| the beats | `194 + k × w`, 21 | `(w − 2)` × 8, `w = floor(44 / BEATS A BAR)` | | the bar's beats; while the loops play, the playing one lit |
| ADJUST's top line | 2, 19 | | T | `LOOP 03: SHIFT +17 ROWS`, in place of the tempo, the length, QUANTIZE and REC |
| a song recording | 2, 19 ... | | T | [below](#a-song-recording) |
| the line | 0, 31 | 240 × 1 | | |
| slot *k* (0-3) | 0, `y = 33 + 32 k` | 228 × 28 | | the loop `top + k`, or `+ NEW LOOP` |
| its number | 5, y + 8 | | S | `00`-`0F`; with the keys shown (CONTROL mode, SEQ held) ` 1`-`16`, `10`-`16` at 2 |
| its sound | 28, y | | T | the instrument's name, 9 characters |
| its mode, repeats | 88, y; 100, y | | T | `P` POLY, `M` MONO; `X2`-`X9` while its base repeats, `X+` past 9; nothing when it plays once |
| its SHIFT | 118, y | | T | `SHIFT +3`, `SHIFT -3` (the nearer way round; none at 0) |
| its state | right-aligned to 226, y | | T | `STAGED` (the stage stands for it); `M` / `S` (waiting for the bar line); `S` (soloed); `M` (muted); else nothing |
| its strip | 28, y + 13 | 180 × 14 | | [below](#the-strips) |
| its LEVEL | right-aligned to 226, y + 14 | | T | its mix's LEVEL in hex, as K1 and the MIXER show it: `7F` (2 characters at 214-225, past the strip's end at 208) |
| the selected slot's box | 0, y − 1 | 228 × 29 | | an outline |
| `+ NEW LOOP` | 9, y + 8; 28, y; right-aligned to 226, y | | S; T; T | `+`; `NEW LOOP 02` (the number REC gives it); the keys' instrument (`08:00 SYN PIANO`, its name 10 characters) |
| its strip | 28, y + 13 | 180 × 14 | | dashed: a dot every 4 px top and bottom, solid ends; `REC: A NEW LOOP` (with notes on the stage and the stage on, selected: `REC: THE STAGE GOES HERE`) at 34, y + 14 |
| its LEVEL | right-aligned to 226, y + 14 | | T | the next loop's LEVEL in hex, `7F` |
| the overview column | 229, 33 | 11 × 128 | | [below](#the-overview-column) |
| the stage's frame | 0, 168 | 240 × 29 | | an outline, its top edge broken for its title and REC's target |
| its title | 6, 162 | | T | `STAGE OVERDUB` or `STAGE OVERWRITE`, then ` LFO`, the tooltip for the switch, where LFO acts (none in ADJUST); while the stage is off `STAGE OFF`. The footer shows the instrument and MONO / POLY |
| REC's target | right-aligned to 234, 162 | | T | `REC: INTO LOOP 01` on a loop, `REC: NEW LOOP 02` on `+ NEW LOOP`; while the stage is off `REC: STAGE ON` |
| its dot | centre 13, 185 | radius 4 | | while keys overdub; while the loops play; stopped, or the stage off |
| its strip | 28, 176 | 180 × 18 | | the stage as it plays (MONO: one note at a time); dimmed while it is off |
| `EMPTY` | 208, 179 | | T | while the stage holds no note (its last pixel at 236, two clear of the frame) |
| the length's bar | 28, 199 | 180 × 12 | | a segment a bar: its number (T) at the segment's x; a 3-px rule from x + 7 to the next segment's x − 2 at y + 5; the play position's tick, 1 × 11 at y + 1 |
| the hint line | 2, 214 | | T | [below](#the-hint-line) |
| the footer | the frame's | | | [below](#the-footer) |

### The strips

- **The window:** VIEW's bars (at most the length) from the page the play position is on, pages of VIEW's bars from
  the loops' start, the last page ending at the length's end. While the loops play the window turns a page as the play
  line leaves it; stopped, it shows the play position's page. Every strip, the stage's too, shows the same window.
- **A row a cell,** `x = 28 + floor((row − start) × 180 / rows shown)`, a pixel's gap after each cell 4 px or wider; a
  lane a band of the strip's height (`floor((h − (lanes − 1)) / lanes)` px each, a pixel apart; a MONO loop's one lane
  fills it).
- **Each cell:** empty, the ground (deep light grey on a beat's first row, deep dark grey otherwise); **a note's
  sustain** in the loop's colour; **its attack** (its first cell, when it is longer than one) and **its release** (the
  cells after its end, up to the lane's next note: 2 for a DX7 or JUNO-106 instrument, 1 for the others) in that
  colour's deep shade; a note one cell long full. A silenced loop's notes in dark grey and its deep shade. Repeats
  drawn as played, the rest empty; SHIFT applied.
- **Bar lines** light grey in the gap before each bar's first cell, a pixel over and under the strip; **the play line**
  white the same way, in every strip and the stage's.
- **The stage's strip:** a held key's note drawn only up to the play line, cell by cell as it moves; ahead of it the
  lane shows what the stage had, up to its next note there; a note landing ahead (quantized forward, or in the next
  cycle) drawn when the line reaches it. While the stage is off the strip is much fainter than a silenced loop's: on
  black, with no cells behind its notes, the sustain and the bar lines deep light grey, the attack and the release deep
  dark grey, the play line dark grey.

### The overview column

A cell a loop, 16 of them, 8 px apart from y 33: **a 6 × 6 square** at 231, `34 + 8 i` in the loop's colour (red while
the stage stands for it, dark grey muted or silenced), a yellow 8 × 8 frame round it while soloed, a yellow 2 × 2 dot in
it while a mute or solo waits; **`+ NEW LOOP`** a plus filling the same 6 × 6 box, 2-px strokes, dark grey; the cells
after it black. **The selection** a small circle in its cell: a white ring round black, its loop's colour a 2 × 2 dot
in the middle (red standing for the stage, dark grey silenced; light grey on `+ NEW LOOP`, the circle in the plus's
place). **The slots in view** in a white frame, 11 px wide, round their cells.

### The hint line

Only where a state changes what the keys, REC or HOME do; empty otherwise (its row kept, so nothing moves):

| State | Hint |
|---|---|
| ADJUST | `K1 ROW  K2 BAR  SEL KEEP  HOME CANCEL` (the buttons yellow) |
| a song recording (RECORD LIVE) | `RECORDING: HOME ENDS AND KEEPS IT` (`RECORDING:` red, `HOME` yellow): HOME's tap ends the song and keeps it ([above](#record-live)); in ADJUST HOME cancels ADJUST, so ADJUST's hint comes first |
| a song counting in (RECORD LIVE) | `COUNTING IN: HOME CANCELS` (`COUNTING IN:` red, `HOME` yellow) |
| a song armed (RECORD LIVE) | `SONG ARMED: A NOTE STARTS  HOME CANCELS` (`SONG ARMED:` red, `HOME` yellow); REC and PLAY start it too, as [above](#record-live) |
| armed, not playing | `ARMED: A NOTE OR PLAY STARTS AT 2.4.3` (`ARMED:` red, `PLAY` yellow) |
| CONTROL mode, SEQ held | `WHITE KEYS: MUTE  HELD: AT THE BAR` (`SOLO`) (`WHITE KEYS`, `HELD` yellow) |
| the stage off (not while a song is armed or recorded: the keys write the song then) | `STAGE OFF: THE KEYS ARE NOT KEPT` (`STAGE OFF` cyan) |

When several hold, the first in the table's order shows; its words not named yellow or red are light grey. OVERDUB and
OVERWRITE have no hint: the stage's title says which is on.

### Colours

- **Each loop's colour,** given when it is made and kept: light red, yellow, light green, light cyan, light blue,
  light magenta, brown, green, cyan, red, magenta, light grey, then round again (the next colour after the newest
  loop's; the first loop light red).
- **The stage's colour:** the selected loop's (on `+ NEW LOOP` the colour the new loop will get), coloured as the
  loops' strips.
- The header's `LOOPS` in the PLAY group's colour. The top line light grey; QUANTIZE cyan; `REC` dark grey, `ARMED` and
  `SONG` red; the beats deep green, the one playing yellow on the bar's first beat and green on the others; ADJUST's
  top line white.
- A slot: its number cyan (dark grey silent, muted or silenced; with the keys shown, white), its sound in its colour
  (dark grey silenced; red when `STAGED`), its mode, repeats and SHIFT light grey (dark grey silenced); `STAGED` red,
  a waiting `M` / `S` yellow, `S` soloed yellow, `M` muted dark grey; its LEVEL light grey (dark grey where its notes
  are: silent, muted or silenced). The selected slot's box white; the line dark grey.
- `+ NEW LOOP`: `+` and `NEW LOOP 02` white, the keys' instrument in its engine's colour, the next loop's LEVEL light
  grey; its dashed strip and its text dark grey.
- **The stage:** its frame dark grey; `STAGE` cyan, `OVERDUB` in the stage's colour, `OVERWRITE` red, ` LFO` dark grey;
  `REC` yellow, `: INTO LOOP 01` / `: NEW LOOP 02` light grey; its dot red while keys
  overdub, deep red while the loops play, deep grey stopped or off; `EMPTY` dark grey. **While it is off all of it is much
  fainter:** the frame deep light grey, `STAGE OFF`, `REC: STAGE ON` and `EMPTY` dark grey, its strip as
  [above](#the-strips).
- **The length's bar:** a bar's number white in view, dark grey out of it; its rule light grey in view, deep light
  grey out of it; the play position's tick white.

### The footer

- **Left:** the keys' instrument (`08:00 SYN PIANO JUNO`, as on every screen); in the pattern recorder, the one its
  preview plays.
- **Right:** `MONO` or `POLY`, the keys' mode, light grey; `REC: TO STAGE` in the pattern recorder; `MEMORY 81% FREE`
  while a song records (the edit memory left), red.
- **A knob turned shows in the header,** as on every screen ([../frame.md](../frame.md#the-header)): `TEMPO 78 (120
  BPM)`, `LEVEL 7F 100%`, `PAN 80 MIDDLE`, `DUCK 20 25%`, `CHORUS SEND 20 25%`, `DELAY SEND 20 25%`, `REVERB SEND 20
  25%`, `VIEW 2 BARS`, `PLAY FROM 2.4.3`, `JUMP +1 ROW`, `JUMP -16 ROWS`.

### No loops yet

```
LOOPS                           |###
120 BPM 4 BARS  Q ROW   REC     .. .. ..
----------------------------------------
+--------------------------------------+
|  NEW LOOP 00         11:03 CHIPIN    |+  + NEW LOOP, selected
|+ :REC: A NEW LOOP...............: 7F |
+--------------------------------------+
PLAY, THEN PLAY NOTES:                     at 2, 70
THEY GO ON THE STAGE                       at 2, 82
REC KEEPS THE STAGE AS A LOOP              at 2, 94
PRESETS, ENV: THE SOUND                    at 2, 112
GLO: LENGTH, TEMPO, VIEW                   at 2, 124

-STAGE OVERDUB LFO ---- REC: NEW LOOP 00
|o                                EMPTY |
+--------------------------------------+
   1 ------ 2 ------ 3 ------ 4 ------
                                         
11:03 CHIPIN FUSION                 POLY
```

Colours: the five lines light grey, `PRESETS`, `ENV` and `GLO` yellow. With the stage off the lines at 82 and 94 read
`THEY ARE NOT KEPT` and `REC: THE STAGE BACK ON` (the stage's title `STAGE OFF`, its right `REC: STAGE ON`).

### Armed, ADJUST, the stage off

- **Armed:** the top line's `ARMED` red; the header's ● red ([../frame.md](../frame.md)); the hint line `ARMED: A NOTE
  OR PLAY STARTS AT 2.4.3`; REC blinking. During a count-in, the count-in's box over the body
  ([../transport.md](../transport.md#the-count-ins-box)).
- **ADJUST:**

  ```
  LOOPS ADJUST                  > |##9
  LOOP 01: SHIFT +17 ROWS         ## .. ..
  ----------------------------------------
  ...
  K1 ROW  K2 BAR  SEL KEEP  HOME CANCEL
  08:00 SYN PIANO JUNO                MONO
  ```

  the subpage `ADJUST` white; the top line white; the slot's `SHIFT` and its strip turned at once.
- **The stage off** (ARP): the selected loop heard (no `STAGED`), the stage's title and its right changed, its notes
  dimmed, the hint line:

  ```
  ...
  |  SYN PIANO M                         |#  the selected loop: heard, not STAGED
  |01 ##.##..#|#.##..##.|..           7F|O
  ...
  -STAGE OFF ----------------- REC: STAGE ON   all dark grey
  |o  ####..##|##.....                   |   its notes dark grey, the dot deep grey
  +--------------------------------------+
     1 ------ 2 ------ 3 ------ 4 ------
  STAGE OFF: THE KEYS ARE NOT KEPT           the hint line (STAGE OFF cyan)
  08:00 SYN PIANO JUNO                MONO
  ```

### A song recording

The top line reads `SONG 0:42  5 PATTERNS  120 BPM`: `SONG` red at 2, the time recorded at 32 (`M:SS`; `MM:SS` from 10
minutes, at 26), its patterns so far at 68, the one being written counted (`1 PATTERN`, `5 PATTERNS`, in decimal), the
tempo at 146, light grey; the beats as ever; the loops' play line, while they are silent, at their place. The hint
line `RECORDING: HOME ENDS AND KEEPS IT` ([above](#the-hint-line)). The footer's right `MEMORY 81% FREE`, red.

### The RECORD LIVE panel

A panel over the loops, the top line in view, as RECORD PATTERN's ([../dialogs.md](../dialogs.md#record-pattern)):

```
LOOPS                           |###
120 BPM 4 BARS  Q ROW   REC     ## .. ..
 +-------------------------------------+
 |             RECORD LIVE             |   the title, T
 | NAME                       LOOPS 03 |
 | K1 TEMPO                    124 BPM |
 | EDIT: NAME                          |
 |                                     |
 |         HOME              REC       |   the buttons, T
 |        CANCEL             ARM       |   the answers, S, 16 px
 +-------------------------------------+
11:03 CHIPIN FUSION                 POLY
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the panel | 8, 68 | 224 × 108 | | a black box with a 1-px outline, centred in the body |
| the title | centred in 8-231, 74 | | T | `RECORD LIVE` |
| line *i* | 14, 92 + 12 × *i*; the value right-aligned to 226 | | T | `NAME` `LOOPS 03`, `K1 TEMPO` `124 BPM` |
| EDIT | 14, 120 | | T | `EDIT: NAME` |
| the buttons, the answers | as a question's, from 136 (no on the left, yes on the right) | | T, S | `HOME` `CANCEL`, `REC` `ARM` |

Colours: the outline light grey; the title white; `K1` and `EDIT` yellow, the lines' names light grey, their values
white; the buttons and answers as a question's. ALGORITHM turns the tempo too, as GLO's TEMPO does; the keys play as
on the screen; REC blinks while the panel is open.

### The pattern recorder

A box over the loops, the top line in view:

```
LOOPS                         > |##3
120 BPM 4 BARS  Q ROW   REC     ## .. ..
+--------------------------------------+
| RECORD PATTERN: TO STAGE     PREVIEW |   the title; PREVIEW green while it plays
|     1       2       3       4        |   the beats over the steps
| C-2 ## .. .. .. ## .. .. .. ## ..    |   4 rows: the note, LENGTH's steps in fours
|[D-2 .. .. .. .. ##=## .. .. .. ..   ]|   the selected row, boxed; a tie joined to its note
| F#2 .. .. ## .. .. .. ## .. .. ..    |
| D#2 .. .. .. .. .. .. .. .. .. ..    |   an empty row: its note dark grey
| K1 NOTE LENGTH                  1/16 |
| K2 LENGTH                   16 STEPS |
| K3 NOTE                          D-2 |
| REPEATS                  OVER 4 BARS |   no knob: what the pattern fills
| 4 X 16 = 64 ROWS, 3 LANES            |   what it makes
| KEY: A STEP  HELD: A TIE  SEL: NOTE  |
+--------------------------------------+
F4:00 KIT FUSION           REC: TO STAGE
```

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the box | 4, 36 | 232 × 186 | | black, a 1-px outline |
| the title | 10, 40 | | T | `RECORD PATTERN: TO STAGE` |
| PREVIEW | 188, 40 | | T | `PREVIEW` |
| the beats | `x(k) + 3`, 56, `k` = 0, 4, 8, 12 | | T | `1`-`4`, over each four steps of LENGTH |
| row `r`'s note | 8, `71 + 17 r` | | T | `C-2`, `F#2` |
| step `k` of row `r` | `x(k) = 30 + 12 k + 2 × floor(k / 4)`, `70 + 17 r` | 11 × 13 | | LENGTH's steps |
| a tie's bar | `x(k − 1) + 6`, `75 + 17 r` | `x(k) − x(k − 1) + 1` × 3 | | joining a tie to the step before, at its middle |
| the selected row's box | 6, `68 + 17 r` | 228 × 17 | | an outline |
| line `i` | 12, `140 + 12 i`; the value right-aligned to 228 | | T | `K1 NOTE LENGTH` `1/16`, `K2 LENGTH` `16 STEPS`, `K3 NOTE` `D-2`, `REPEATS` `OVER 4 BARS` |
| what it makes | 12, 192 | | T | the whole repeats × the pattern's rows (+ the cut one's rows) = the length, and its lanes: `4 X 16 = 64 ROWS, 2 LANES`, `5 X 12 + 4 = 64 ROWS, 1 LANE` |
| the keys' line | 12, 206 | | T | `KEY: A STEP  HELD: A TIE  SEL: NOTE` |

Colours: the outline light grey; the title cyan; `PREVIEW` green while the preview plays, dark grey otherwise; the
beats dark grey; a row's note in the stage's colour, white on the selected row, dark grey on an empty row; a step on
in that colour, the step playing white; a tie in the colour's deep shade (light grey on the step playing), its bar in
the colour; off deep grey, the step playing deep blue; the box white; the K lines' names cyan, their values light
grey, K3's white; `REPEATS` and its value dark grey; what it makes light grey; the keys' line dark grey. The footer:
the instrument the preview plays, and `REC: TO STAGE`.

**The keys' LEDs:** a step on full; a tie blinking; off dark, but steps 1, 5, 9 and 13 dim; past LENGTH dark; the step
playing turned over (on: dark, off: full). While a key sets the note (after SEL, or SEL held): the row's note's key
full when the keyboard's octave has it, the rest dark. ARP full; REC dim; SEL full while it waits for a key.

### The properties (GLO)

The properties dialog ([../dialogs.md](../dialogs.md#properties)), titled `LOOPS PROPERTIES`, twelve lines (192 px of
the body, the dialog's own layout):

| Item | Shows | K1 |
|---|---|---|
| TEMPO | `78 (120 BPM)`: as a song's, and its BPM | 3-600; SEL held: by 10 |
| SWING | `50 %` | 50-75 % |
| LENGTH | `4 BARS`, `1 BAR`; a part bar in beats, `21 BEATS`; a shorter one planned, yellow: `2 BARS (NOW 4 BARS)`, `5 BARS (NOW 21 BEATS)` | a bar more or less, 1 to the most (from a part bar the next whole bar each way, [above](#the-length)): longer at once; shorter cut as GLO closes, asking first when a note that plays would go (`CUT TO 2 BARS?` / `NOTES IN THE BARS CUT GO`, OCT+ `CUT`, OCT- `KEEP`) |
| GRID | `1/16`, `1/16T`, `1/32`: 4, 6 or 8 rows a beat; dark grey while it cannot change | only with no loops and the stage empty (`GRID: ONLY WITH NO LOOPS`, `GRID: ONLY WITH THE STAGE EMPTY`) |
| QUANTIZE | `2 ROWS`, `3 ROWS`, `4 ROWS`, `ROW`, `REAL TIME` | |
| MONO LEGATO | `GLIDE`, `LEGATO`, `JUMP`; `JUMP` in a new setup (one saved keeps its own) | |
| COUNT-IN | `OFF`, `1 BAR`, `2 BARS` ([../transport.md](../transport.md#count-in)): the bars counted when PLAY starts armed loops; an armed key always starts them at once | |
| VIEW | `1 BAR A SCREEN`, `2 BARS A SCREEN`, `4 BARS A SCREEN` | |
| K1 TURNS, K2 TURNS, K3 TURNS, K4 TURNS | `LEVEL`, `PAN`, `DUCK`, `CHORUS SEND`, `DELAY SEND`, `REVERB SEND`, `TEMPO`, `VIEW`; at first `LEVEL`, `PAN`, `REVERB SEND`, `TEMPO` | |

### The SEL menu

The menu ([../dialogs.md](../dialogs.md#menus)), titled `LOOP 03 SUB BASS` on a loop or `LOOPS` on `+ NEW LOOP`:

- **TAKE ITS INSTRUMENT** (on a loop): the pipette's tap route ([above](#the-instrument-and-mono-or-poly)): the loop's
  instrument and MONO / POLY become the keys' (`KEYS: 01:04 BRASS, MONO`).
- **APPLY INSTRUMENT** (on a loop): the selected loop takes the keys' instrument and MONO / POLY (`LOOP 03: 01:04
  BRASS, MONO`).
- **ADJUST** (on a loop): its SHIFT ([above](#k1-k4-view-and-adjust)).
- **QUANTIZE** (on a loop): a list, titled `QUANTIZE LOOP 03`: `2 ROWS`, `3 ROWS`, `4 ROWS`, `ROW`. The loop's notes go
  to the nearest line of that size, the lines counted as the loop is heard (from the loops' start, its SHIFT counted,
  its first repeat), back in its base; REAL TIME's note delays and key offs go; each note off moves with its note (its
  length kept, a row at least); two notes landing on one lane's row: the later wins; a note rounded to the base's end
  goes to its start (`LOOP 03 QUANTIZED: 4 ROWS`). REVERT is the way back.
- **DELETE LOOP** (on a loop): the loop goes at once, no question (`LOOP 03 DELETED`); the loops after it move up with
  their mixes and waiting mutes, the selection on the one moved into its place; the length stays and the row playing
  runs on; REVERT is the way back. What sounds in a place another loop now stands in (one of another instrument, colour or MONO) ends there; a
  place with none left ends too. The stage stays: with notes on it, the loop now selected falls silent, as any selected
  loop does.
- **DOUBLE,** listed only when it can be done (loops exist, twice the length fits 128 rows): the length ×2, every loop
  playing twice what it played, nothing copied (a loop filling the length doubles its repeat count; one that rests or
  is cut at the end is written out to the old length first, so the second half is the first again), the stage played
  twice (`DOUBLED: 8 BARS`). Refused when the edit memory cannot hold it (`MEMORY FOR EDITS IS FULL`), or the stage
  played twice would pass its 3,800 B (`THE STAGE IS FULL`, SEL blinking): nothing changed. Picked once twice the
  length no longer fits (the length changed under the open menu: REVERT ALL), it is refused (`DOUBLE: AT MOST 8 BARS`,
  SEL blinking).
- **RECORD PATTERN:** the pattern recorder ([above](#the-pattern-recorder-arp)), ARP held + REC's tap route, on a loop
  (its preview in the loop's sound) and on `+ NEW LOOP`; refused as that is (SEL blinking).
- **RECORD LIVE:** its panel, to arm the song's recording ([above](#record-live)); while armed, the item reads `CANCEL
  RECORD LIVE`; while a song records it is not listed.
- **SAVE TO PATTERN**, **LOAD FROM PATTERN** ([above](#storing-in-a-pattern-and-loading-one)).
- **CLEAR STAGE,** listed while the stage holds notes: the stage's clear ([above](#the-stage)), at once, no question
  (`STAGE CLEARED`); EDIT held's tap route.
- **CLEAR LOOPS:** every loop and the stage's notes, after a question (`CLEAR ALL 6 LOOPS?` (`1 LOOP`) / `THE STAGE
  TOO; THE LENGTH STAYS`, OCT+ `CLEAR`, OCT- `KEEP`; then `LOOPS CLEARED`); the length, the properties, the FX setup
  and the next loop's mix stay; the loops play on, empty.
- **REVERT** (while LOOPS has something unsaved): LOOPS back as saved, after a question (`REVERT LOOPS TO SAVED?`,
  OCT+ `REVERT`, OCT- `KEEP`; then `REVERTED`): LOOPS' unsaved pieces are dropped from the edit memory and what the
  flash holds plays again, from the next row; the stage kept ([above](#the-stage)). No copy is kept for it; it is not
  an undo.
- With no loops, RECORD LIVE, SAVE TO PATTERN and CLEAR LOOPS say `NO LOOPS YET`. During a count-in each that changes
  the loops blinks.

## Saving

**SAVE keeps the whole LOOPS setup,** as DJ's stand is kept: the loops (notes, instruments, colours, mixes, MONO /
POLY, SHIFTs, mutes; never a solo), the length, the properties (VIEW and K1-K4's jobs among them), the next loop's mix,
the keys' MONO / POLY, the FX setup, the selection. A power cycle brings it back, paused at the start. Not kept: what
plays, the play position, the stage (never saved: [above](#the-stage)) and its switch (on at every power-on), an arm
or a song armed, ADJUST, a planned LENGTH, OVERWRITE (OVERDUB at every power-on), CONTROL mode (PLAY at first), the
pattern recorder's steps.
LOOPS lives in the edit memory as
any edit: saved loops play from the flash, and only what changed since takes edit memory
([../../files/ram.md](../../files/ram.md)).

## Texts

- **The header:** `LOOPS`; the subpage `ADJUST`; SEQ's place `SEQ MUTE`, `SEQ SOLO` in CONTROL mode.
- **The top line:** `120 BPM`, `4 BARS`, `1 BAR`, `21 BEATS` (`1 BEAT`), `22 ROWS`, `Q 2 ROWS`, `Q 3 ROWS`, `Q 4 ROWS`,
  `Q ROW`, `Q REAL`, `REC`, `ARMED`, `SONG`; ADJUST's `LOOP 03: SHIFT +17 ROWS`; a song recording: `SONG`, `0:42`, `5
  PATTERNS` (`1 PATTERN`).
- **The slots:** the numbers, the sounds' names, `P`, `M`, `X2`-`X9`, `X+`, `SHIFT +3`, `STAGED`, `M`, `S`, the
  LEVELs (`7F`); `+`, `NEW LOOP 02`, `REC: A NEW LOOP`, `REC: THE STAGE GOES HERE`.
- **The stage:** `STAGE`, `OVERDUB`, `OVERWRITE`, ` LFO`, `OFF`, `REC: INTO LOOP 01`, `REC: NEW LOOP 02`, `ARP: STAGE
  ON`, `EMPTY`.
- **The hint line:** `K1 ROW  K2 BAR  SEL KEEP  HOME CANCEL`, `RECORDING: HOME ENDS AND KEEPS IT`, `COUNTING IN: HOME
  CANCELS`, `SONG ARMED: A NOTE STARTS  HOME CANCELS`, `ARMED: A NOTE OR PLAY STARTS AT 2.4.3`, `WHITE KEYS: MUTE  HELD: AT THE BAR` (`SOLO`), `STAGE OFF: THE KEYS ARE NOT KEPT`.
- **ARP's layer** ([../dialogs.md](../dialogs.md#hold-layers)): `REC: RECORD PATTERN`.
- **No loops yet:** `PLAY, THEN PLAY NOTES:`, `THEY GO ON THE STAGE`, `REC KEEPS THE STAGE AS A LOOP`, `PRESETS, ENV:
  THE SOUND`, `GLO: LENGTH, TEMPO, VIEW`; with the stage off the second and third lines read `THEY ARE NOT KEPT` and `REC:
  THE STAGE BACK ON`.
- **The footer's right:** `POLY`, `MONO`, `MEMORY 81% FREE`, `REC: TO STAGE`.
- **The header, a knob turned:** `TEMPO 78 (120 BPM)`, `LEVEL 7F 100%`, `PAN 80 MIDDLE`, `DUCK 20 25%`, `CHORUS SEND
  20 25%`, `DELAY SEND 20 25%`, `REVERB SEND 20 25%`, `VIEW 2 BARS`, `PLAY FROM 2.4.3`, `JUMP +1 ROW`, `JUMP -16 ROWS`.
- **Dialogs:** `LOOPS PROPERTIES` (TEMPO, SWING, LENGTH, GRID, QUANTIZE, MONO LEGATO, COUNT-IN, VIEW, K1 TURNS, K2
  TURNS, K3 TURNS, K4 TURNS; `21 BEATS`, `2 BARS (NOW 4 BARS)`, `5 BARS (NOW 21 BEATS)`, `2 ROWS`, `3 ROWS`, `4 ROWS`,
  `ROW`, `REAL TIME`, `GLIDE`,
  `LEGATO`, `JUMP`, `OFF`, `1 BAR`, `2 BARS`, `1 BAR A SCREEN`, `2 BARS A SCREEN`, `4 BARS A SCREEN`, `LEVEL`, `PAN`,
  `DUCK`, `CHORUS SEND`, `DELAY SEND`, `REVERB SEND`, `TEMPO`, `VIEW`); `CUT TO 2 BARS?` / `NOTES IN THE BARS CUT GO`,
  CUT, KEEP; the menus `LOOP 03 SUB BASS`, `LOOPS` (TAKE ITS INSTRUMENT, APPLY INSTRUMENT, ADJUST, QUANTIZE, DELETE
  LOOP, DOUBLE, RECORD PATTERN, RECORD LIVE, CANCEL RECORD LIVE, SAVE TO PATTERN, LOAD FROM PATTERN, CLEAR STAGE, CLEAR
  LOOPS, REVERT); `QUANTIZE
  LOOP 03` (`2 ROWS`, `3 ROWS`, `4 ROWS`, `ROW`); `CLEAR ALL 6 LOOPS?` (`1 LOOP`) / `THE STAGE TOO; THE LENGTH
  STAYS`, CLEAR, KEEP; `REVERT LOOPS TO SAVED?`, REVERT, KEEP; the count-in's box
  ([../transport.md](../transport.md#the-count-ins-box)); `SAVE THE LOOPS TO` with `NEW`, `LOAD INTO LOOPS`; `PATTERN
  2A IS NOT EMPTY` / `REPLACE IT WITH THE LOOPS?`, YES, NO; `REPLACE THE LOOPS` / `WITH PATTERN 2A?`, YES, NO; `NEW
  LOOP: A CARTRIDGE`; the RECORD LIVE panel's (`RECORD LIVE`, `NAME`, `LOOPS 01`, `K1 TEMPO`, `124 BPM`, `EDIT:
  NAME`, `HOME` `CANCEL`, `REC` `ARM`); the pattern recorder's (`RECORD PATTERN: TO STAGE`, `PREVIEW`, `K1 NOTE
  LENGTH`, `K2 LENGTH`, `K3 NOTE`, `REPEATS`, `1/32` ... `1/4`, `1/2`, `1 BAR`, `TO THE NEXT`, `16 STEPS`, `OVER 4
  BARS` (`OVER 21 BEATS`), what it makes, `KEY: A STEP  HELD: A TIE  SEL: NOTE`).
- **Toasts:**

  | Toast | When |
  |---|---|
  | `STAGE AS LOOP 06` | REC on `+ NEW LOOP` with notes on the stage: a new loop |
  | `STAGE INTO LOOP 03` | REC on a loop with notes on the stage |
  | `THE STAGE IS EMPTY` | REC with an empty stage while the loops play (refused) |
  | `LOOP 03 ON THE STAGE` | EDIT |
  | `STAGE CLEARED` | EDIT held; EDIT on `+ NEW LOOP`; SEL → CLEAR STAGE |
  | `STAGE OFF`, `STAGE ON` | ARP tapped: the stage's switch; `STAGE ON` too for REC with the stage off while the loops play, and for a clear with the stage empty and off |
  | `STAGE ON: ARMED` | REC with the stage off, the loops stopped: the stage on, the loops armed |
  | `OVERWRITE`, `OVERDUB` | LFO tapped |
  | `THE STAGE IS FULL` | a note past the stage's 3,800 B (neither heard nor written); EDIT or DOUBLE past it, or REC writing a loop past it: refused, nothing changed |
  | `STAGE NOT WRITTEN: STORAGE BUSY` | a change of the stage while the store is busy (refused) |
  | `CANCELLED` | REC while armed; REC, PLAY or HOME during the count-in; ARP switching the stage off while armed; HOME in ADJUST; HOME with a song armed; SEL → CANCEL RECORD LIVE; a song ended before its first row |
  | `LOOP 03: SHIFT +17 ROWS` | ADJUST kept (SEL) |
  | `KEYS: CONTROL`, `KEYS: PLAY` | SEQ |
  | `KEYS: MONO`, `KEYS: POLY` | MONO, POLY in CONTROL mode, or with SEQ held |
  | `KEYS: 01:04 BRASS, MONO` | ENV held; SEL → TAKE ITS INSTRUMENT |
  | `LOOP 03 MUTED`, `LOOP 03 ON`, `LOOP 03 SOLO`, `LOOP 03 SOLO OFF` | a white key in CONTROL mode or with SEQ held, HOME + white key |
  | `LOOP 03 MUTED AT THE BAR`, `LOOP 03 ON AT THE BAR`, `LOOP 03 SOLO AT THE BAR`, `LOOP 03 SOLO OFF AT THE BAR` | the same, held: waiting for the bar line |
  | `ALL LOOPS ON` | PIT in CONTROL mode or with SEQ held |
  | `QUANTIZE: 2 ROWS` (`3 ROWS`, `4 ROWS`, `ROW`, `REAL TIME`) | OP1-OP5 in CONTROL mode or with SEQ held |
  | `SEQ: MUTE`, `SEQ: SOLO` | GLO's black key in CONTROL mode or with SEQ held |
  | `NO LOOPS YET` | RECORD LIVE, SAVE TO PATTERN or CLEAR LOOPS with no loops |
  | `KEYS: THE NOTE`, `KEYS: THE STEPS`, `NOTE C-5` | the pattern recorder: SEL; OCT |
  | `NO STEPS ON` | the pattern recorder: REC with no step on (refused) |
  | `PATTERN ON THE STAGE` | the pattern recorder: REC |
  | `GRID: ONLY WITH NO LOOPS`, `GRID: ONLY WITH THE STAGE EMPTY` | GLO, GRID with loops; with notes on the stage |
  | `DOUBLED: 8 BARS` (a part bar: `DOUBLED: 42 BEATS`) | DOUBLE |
  | `DOUBLE: AT MOST 8 BARS` | DOUBLE picked once twice the length no longer fits (refused) |
  | `LOOP 03: 01:04 BRASS, MONO` | APPLY INSTRUMENT |
  | `LOOP 03 QUANTIZED: 4 ROWS` | SEL → QUANTIZE |
  | `LOOP 03 DELETED` | SEL → DELETE LOOP |
  | `SONG 12 RECORDED: 1:23, 5 PATTERNS` (`1 PATTERN`) | the song's recording ended (HOME, leaving LOOPS, PLAY on FX) |
  | `SONG 12: 3 NOTES LEFT OUT` (`1 NOTE`) | ... with notes that did not fit its 16 tracks |
  | `SONG 12 STOPPED: MEMORY FOR EDITS IS FULL`, `SONG 12 STOPPED: ALL 240 PATTERNS USED`, `SONG 12 STOPPED: 256 POSITIONS` | the song's recording stopped by itself |
  | `NO SONG NUMBER FREE` | RECORD LIVE with every song number taken |
  | `LOOPS STORED IN PATTERN 2A`, `PATTERN 2A: 3 NOTES LEFT OUT` | SAVE TO PATTERN |
  | `PATTERN 2A IN LOOPS: 3 LOOPS`; `PATTERN 2A IN LOOPS: 3 INSTRUMENTS LEFT OUT`, `PATTERN 2A IN LOOPS: 12 COMMANDS LEFT OUT` | LOAD FROM PATTERN |
  | `CARTRIDGE 0A IS EMPTY` | ENV picked an empty cartridge (an empty FAVORITES cartridge too, `CARTRIDGE FB IS EMPTY`; a FAVORITES one with favourites gives its first favourite's source, [instruments.md](instruments.md#favorites)) |
  | `LOOPS CLEARED` | SEL → CLEAR LOOPS, CLEAR |
  | `REVERTED` | SEL → REVERT, after its question |
  | `MEMORY FOR EDITS IS FULL` | a note the stage cannot take (neither heard nor written); EDIT, REC, the pattern recorder, DOUBLE or LOAD past the edit memory: refused, nothing written |

## Help

```
[LOOPS]: A LOOP STATION
{PLAY}: THE LOOPS GO ROUND THE LENGTH
  (4 BARS AT FIRST; {GLO}: LENGTH)
THE STAGE: ONE LOOP YOU PLAY OVER
  AND OVER; NOTES ADD UP, NONE REMOVED
  (A KEY HELD OVER A START MOVES IT ON)
{REC}: THE STAGE ADDED ON + NEW LOOP,
  OR INTO THE LOOP, THEN OFF; OFF: ON;
  STOPPED, EMPTY OR OFF: IT ARMS
{ARP} (AUTOMATIC REPEAT PROCEDURE):
  THE STAGE OFF / ON; OFF: KEYS NOT KEPT
{SELECT}: A LOOP, {EDIT}: IT ONTO THE STAGE
  ON + NEW LOOP OR HELD 2S: CLEARS IT
{LFO}: OVERDUB OR OVERWRITE, WHERE THE
  LINE ERASES ALL IT PASSES
```

```
PLAYING: THE LOOPS AND THE KEYS
{PLAY}: PLAY / STOP, HOLD: TO THE START
{ALGORITHM}: WHERE PLAY IS, A ROW
  (HOLD {SEL}: A BAR); PLAYING: A JUMP
{K1}-{K4}: LEVEL, PAN, REVERB, TEMPO
  ({GLO}: EACH KNOB'S JOB)
{PRESETS}: THE SOUND YOU PLAY
{ENV} (EXPLORE NAMED VAULTS): ITS
  CARTRIDGE
HOLD {ENV}: THE LOOP'S SOUND AS YOURS
THE KEYS SOUND AS KEPT: QUANTIZED,
  MONO ONE NOTE, POLY 4 AT MOST
THE STAGE: PLAYS IN THE SOUND YOU PLAY
{SEQ} (SHOW ELSE, QUICK): THE {KEYS}
  PLAY NOTES, OR CONTROL THE LOOPS
{HOME}, {WHITE KEY}: MUTE A LOOP
  HELD: SOLO
```

```
CONTROL: THE {KEYS}
{WHITE KEY}: LOOP 1-16, MUTE / SOLO
  TAP: NOW, HOLD: AT THE BAR LINE
{GLO} KEY: MUTE OR SOLO
{PIT}: ALL LOOPS ON
{OP1} {OP2} {OP3}: QUANTIZE 2, 3, 4 ROWS
{OP4}: A ROW, {OP5}: REAL TIME
{MONO}, {POLY}: HOW YOU PLAY AND KEEP
  MONO: ONE NOTE, AS MONO LEGATO SAYS
  POLY: UP TO 4 NOTES
HOLD {SEQ} + A KEY: AS IN CONTROL
RECORD LIVE ({SEL}): ALL THAT SOUNDS
  GOES IN A SONG; {HOME} ENDS IT
```

```
HOLD {ARP} + {REC}: RECORD PATTERN
  {SELECT}: ONE OF ITS 4 ROWS
  {WHITE KEY}: A STEP, HOLD: A TIE
  {K3} OR {SEL}, THEN A KEY: ITS NOTE
  {K1} NOTE LENGTH, {K2} STEPS: A ROW
  EACH; A ROW'S NEXT NOTE ENDS ITS LAST
  {ARP}: THE PREVIEW ON / OFF
  {REC}: ONTO THE STAGE, {HOME}: CLOSE
{GLO}: TEMPO, SWING, LENGTH, GRID,
  QUANTIZE, MONO LEGATO, COUNT-IN,
  VIEW, K1-K4'S JOBS
{SEL}: TAKE ITS INSTRUMENT, APPLY,
  ADJUST, QUANTIZE, DELETE, DOUBLE,
  RECORD PATTERN, RECORD LIVE,
  SAVE TO / LOAD FROM PATTERN,
  CLEAR STAGE, CLEAR LOOPS, REVERT
{FX}: THE LOOPS' EFFECTS
```

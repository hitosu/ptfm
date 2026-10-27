# DJ

Status: **settled** (2026-10-10), built in the playground (2026-10-11); the parts marked
*(proposed)* are to be tried ([../../open-questions.md](../../open-questions.md#the-ui-ui)). The record:
[the DJ proposal](../../proposals/202610101408-dj.md).

DJ is for playing live, like a DJ stand: two songs on two decks, in time with each other, mixed with a
crossfader, each deck with its own effects. It is only for performing: nothing done on DJ is written into
a pattern, a song or an instrument. DJ's own setup and its stand, the decks as SAVE keeps them:
[../../files/dj.md](../../files/dj.md).

**DJ is a screen of the round** (HOME + OP5, or HOME + SELECT between LOOPS and SETTINGS;
[../navigation.md](../navigation.md)), with a playback of its own, the decks. **DJ stays apart until it is
played:**
- **opening DJ changes nothing of what plays:** a song, the EDITOR's pattern loop or the loops play on, and the
  decks stay as the stand left them. While what plays was not started on DJ, DJ makes no sound and touches no
  effect: LOAD, the knobs, the filters, the pads' settings, FX and the properties set up the stand, heard once
  DJ plays. One can open DJ, set it up and leave with nothing heard;
- **DJ's first start** (a deck's PLAY key, CUE or JUMP on a paused deck, PLAY with nothing playing, the
  master's START with SYNC EXTERNAL) **stops what plays and cuts its sound:** it pauses, keeping its place, and
  every voice and the effects' tails go at once (as all sound off), so the decks start dry with DJ's FX setup;
- **PLAY on DJ while something else plays pauses it** (`PAUSED: PLAY AGAIN FOR DJ`); the next PLAY plays the
  decks;
- **once DJ plays, the decks play on outside DJ** until a song, a pattern or the loops start; there PLAY pauses
  and resumes both, as what played last ([../transport.md](../transport.md)), and coming back to DJ finds them
  as they are. The decks and the loops never play together: the right deck plays on the loops' tracks, 16-31;
- HOME tap opens HOME's combination as on every screen ([../controls.md](../controls.md#homes-combination));
- FX tap does not leave DJ: FX works on DJ's own setup (heard once DJ plays), and FX again goes back
  ([fx.md](fx.md)).

## The decks

### The main and SWAP

- **The left deck is always the main.** Its tempo is the set's tempo, and the right deck can follow it
  ([SYNC](#tempo-and-sync)): a DJ player's master deck, fixed to a side.
- **SWAP** (SEQ) exchanges the decks: their songs, places, cue points, tempos, filters, KEY, loops, mutes and
  voices. The crossfader is mirrored, SYNC stays with the side (a right deck synced before is synced after),
  and the new main keeps the BPM it played at as its own tempo, so nothing heard changes. A playing deck plays
  on through it, nothing restarted; what waits goes with its deck (a LOAD for the bar line, a synced start, a
  stop or a loop held), and a key or pad held through it lets go on its deck, on the other side by then.
- **The flow:** the main plays; the next song is loaded on the right, started in time and brought in; SWAP;
  the right deck is free for the next.

### Loading

- **One song list for both decks.** SELECT moves its highlight (at the first entry the current song); the list
  marks the songs on the decks (`L`, `R`, `LR`).
- **A deck's LOAD key** puts the highlighted song on that deck, at its start: its cue point the start, its
  tempo the song's own (the main's, while the right deck is synced: SYNC stays on), KEY 0, its filter OFF,
  its tracks' mutes off.
- **While a deck plays, its LOAD waits for the deck's next bar line** (`LEFT: CITY LIGHTS AT THE BAR`, the key
  blinking, the state line `LOADS AT THE BAR`): there the song playing stops and the new one plays on from its
  start; KEY goes to 0 and the mutes off, the filter stays as its knob and key left it, the cue point is the new
  song's start. **Its tempo:** LOAD tapped, the BPM the deck played, so the beat goes on (the change shows against
  the new song's own); LOAD held past the hold delay, the song's own tempo, 0 % (`LEFT: CITY LIGHTS AT ITS
  TEMPO`; at once, if the bar line came first). A synced right deck takes the main's either way. A second tap of LOAD before the bar line cancels it
  (`LEFT: LOAD CANCELLED`); the deck paused before it (its PLAY, HOLD, CUE, another song started) drops it too. A
  song that stops at its end has its last bar line there: the new song plays on from its start. A deck
  waiting for its synced start gives the start up and loads paused. A song
  with no positions is refused (`SONG 0A IS EMPTY`).
- **The same song can be on both decks,** to fade from one part of it to another: loaded on the right too, MOVE
  (or HOLD) puts the paused right deck at the part wanted, CUE sets its cue point there; SYNC; PLAY brings it in on the
  main's bar line, in phase with the part playing. Each deck reads the song's patterns for itself.

### Playing and pausing

- **The deck's PLAY key** plays or pauses that deck. Paused, its notes are let go and its place is kept. **Held**
  past the hold delay (the press has already played or paused it), the deck goes back to its song's start,
  paused (`LEFT: BACK TO THE START`); its cue point stays.
  - **In the firmware (the FM-1, the emulator), a start is made ready over the next two halves of the audio**
    (5.8 ms each; the playground starts at once): the song read and its first
    pattern found, then its replay set at the first row, then its first notes, so no half takes the whole
    start, and the deck sounds about 12 ms after the press. Both decks started together are made ready
    together and start in the same half. A deck that is to start on a bar line is made ready first and starts on
    the line exactly (pressed less than 12 ms before it, on the next one). A deck started again while it plays
    (its own arena still in use) does it all in one half.
  - **A synced right deck starts on the main's next bar line** *(proposed: the bar line, not the beat)*, its
    PLAY key blinking while it waits; a second tap cancels the start.
  - **The deck playing never moves for the one starting:** the main started while a synced right deck plays
    waits for the right deck's bar line in the same way.
  - **A bar** is the song's BEATS A BAR beats (its METRONOME's, [fx.md](fx.md)), each ROWS A BEAT rows.
- **The deck's CUE pad** (PLAY's pad 2, near the deck's PLAY key, [below](#the-pads)), as a DJ player's CUE key:
  - **paused:** sets the deck's cue point where it is, on the bar line at or before it (the deck goes there
    too), so that a synced start lines the phrases up (`CUE: POS 02 BAR 0`);
  - **playing, or stopped at the song's end:** takes the deck back to its cue point and plays from it at once
    while held; let go, the deck is back at the cue point, paused (a tap: a moment of the cue heard, then
    paused there);
  - **held while paused at the cue point:** plays from it at once while held, and goes back to it when let go;
  - **the deck's PLAY key while CUE is held playing** turns what letting go does, as on a DJ player (CUE held,
    then PLAY): pressed, letting go plays on from where the deck is; again, back to the cue. The footer says
    which (`LET GO CUE  OP1 PLAYS ON`, `LET GO PLAYS ON  OP1 CUE`).
- **The deck's SET CUE pad** (PLAY's pad 1, beside CUE) sets the cue point while the deck plays on, on the bar
  line nearest the tap ([below](#the-pads)).
- **The PLAY button plays and stops all the decks at once:** with any deck playing, every deck pauses; with none
  playing, every deck with a song (and not stopped at its end) starts, all at the same moment, from where each
  stands, so two decks set at their cue points start together; a synced right deck starts with the main, its
  beats pulled onto the main's. A deck's own PLAY key stays its own play / pause. **Held** past the hold delay,
  the PLAY button takes both back to their cue points,
  paused (`BACK TO THE CUES`); so does HOME + PLAY.
- **A song's end:** it stops or loops, as its AT THE END says. Stopped, the deck is paused at its end (`AT THE
  END`): its PLAY has nothing more to play (refused, `THE END: CUE GOES BACK`, its key dark), CUE takes it back
  to its cue point, JUMP plays from it, and MOVE, HOLD or a LOAD move it as from anywhere. Through the last 8 bars of
  a song that stops, the deck's time left and its overview's last bars are red, and its PLAY key blinks on the
  beat.

## Tempo and SYNC

- **A deck's tempo is a share of its song's own,** as a turntable's pitch fader is: shown as the BPM it plays
  (`124.0 BPM`) and the change (`+3.2%`). The song's tempo commands still act, scaled.
  - **Its range:** 3 to 600 BPM, whatever the song's own tempo, in steps of 0.1 BPM; a fast turn takes bigger
    steps ([../controls.md](../controls.md#knobs)); SEL held: 1 BPM. The limits are a song's TEMPO's
    ([../../files/songs.md](../../files/songs.md#the-record)): the player's longest tick (0.83 s, TEMPO 3) and its
    shortest (4.2 ms, TEMPO 600), the BPM they make at 6 ticks a row and 4 rows a beat; a song of other TICKS A
    ROW or ROWS A BEAT stops where its ticks reach them, if that comes first. Every tempo a song plays stays
    within them, its scaled tempo commands too.
  - **SEL** tapped after a turn sets it back to the song's own (`TEMPO 120.0 BPM: THE SONG'S`).
- **SYNC** (the right deck's key, on at first) locks the right deck to the main:
  - **its tempo** is the main's BPM, or its half or double, whichever is nearest the right song's own (a
    70 BPM song beside a 140 BPM one plays at 70); which of the three is chosen when SYNC goes on or a song is
    loaded, and kept, so a turn of the main's tempo never flips it. The right song's tempo commands are
    overridden while synced. It follows the main whether the main plays or not (an empty main: its own
    tempo);
  - **what it follows** is the main's set tempo and its place in time, never its speed of the moment: a stop
    on the main moves only the main ([below](#landing-in-time)); a HOLD or a ROLL on the main moves the main's
    place for good (a ROLL plays on from where its loop is), and the right follows it (not while the ROLL is
    held);
  - **its beats** fall on the main's while both play. Switched on while both play, the right deck's place moves
    at once by at most half a beat (a note or two may be skipped or repeated); started while the main plays,
    it waits for the main's bar line. Beats line up across songs with other ROWS A BEAT; the bar line is the
    main's;
  - **its tempo knob does nothing** while synced (`SYNC: TEMPO ON THE MAIN DECK`); the main's knob turns both.
- **SETTINGS' SYNC EXTERNAL** ([../../connections.md](../../connections.md#clock)): an outside clock leads the main
  deck. It plays at the clock's tempo, its half or its double, whichever is nearest its song's own, and keeps its
  place on the clock; its tempo knob does nothing (`SYNC EXTERNAL: THE CLOCK'S TEMPO`); a synced right deck
  follows the main as ever, one not synced keeps its own tempo. The master's START plays the main deck from its
  cue point, STOP pauses it, CONTINUE plays it on; a song position does nothing here.
- **Two decks at one BPM never drift apart:** the player counts ticks exactly.
- **Moving a deck against the other** is HOLD's ([below](#hold)): a hand on the record, the deck held back for a
  moment or moved by hand, playing on from there.

## The sound

### Voices

- **One pool, SETTINGS' VOICES (8, 12 or 16); when it is full, the deck lower in the mix gives way.** A note finding
  no free voice takes one from the deck the crossfader favours less (with the crossfader in the middle, the
  right deck), in the pool's order ([settings.md](settings.md#voices): release tails, then held notes, the
  quietest first; a held note taken is parked and comes back when a voice is free); only when that deck has
  none, its own deck's.
- **A deck faded out completely** (the crossfader at the other end) keeps time and takes free voices, but never
  one from the deck heard.
- **The CPU** stays what one song costs at that VOICES; a song alone on DJ has every voice it has on SONGS.
  DJ has no VOICES of its own: 16 as everywhere, the notes past 10 on the second core
  ([../../second-core.md](../../second-core.md#the-voices-on-core-2)), 12 while it does not run. The header's CPU
  bar and STATUS's LATE show the cost.
- **Samples** are all in the user area, read in place ([../../files/store.md](../../files/store.md#where-things-go)).

### Volume

**Each deck has a VOLUME,** a mixer's channel fader: 0 dB at first, up to +6 dB and down to −24 dB, a dB a click
(SEL held: 6), and below −24 OFF, silent. It scales the deck's sound and its sends before the crossfader, and
DJ's LIMITER keeps a boost from clipping.
- **K2 / K3 turn it** once ENV has switched them to VOLUME ([Controls](#controls)); the header shows it as it
  turns (`VOLUME L -3 DB`, `VOLUME R OFF`); SEL tapped after sets it back to 0 dB.
- **It stays with its deck:** through a LOAD, and in a SWAP to the other side.
- **Shown** at the end of the deck's song row, as the header's CPU bar is: a bar 4 × 12 at x0 + 110, 10 px at
  0 dB, 12 at +6, 1 at −24, none at OFF; green, yellow while boosted (above 0 dB), on dark grey's deep shade. The
  song's name gives way to it: 15 characters.

### Effects

- **Each deck plays its song as made, but for three effects:** its song's mixer (levels, pans, DUCK amounts,
  the sends) and `=` commands; its song's DUCK (its source, depth and release), GATE, FILTER (with its envelope,
  fired by the deck's own tracks or beat: [fx.md](fx.md#filters-envelope)), DRIVE and LO-FI,
  in its chain's order, and their `#` commands, on its own sound; then a **DJ FILTER** of its own (the deck's
  filter knob: low-pass one way, high-pass the other, OFF in the middle; its resonance DJ's), before the
  crossfader.
  - **The knob sets it, the deck's FILTER key switches it on and off** (OP3 the left's, GLO the right's), from
    the press (`FILTER L ON LP 2.4K`, `FILTER L OFF`), sliding there while the key is held, over 0.5 s at most;
    let go, it is there: a quick press switches at once, a held one slides. **It starts on, at OFF** (the knob in
    the middle), so K2 and K3 filter as soon as they turn. While it is on, the knob is heard as
    it turns; while it is off, the knob sets what the key brings in (`FILTER L LP 2.4K (OFF)`): set the filter
    unheard, then switch it in on the beat, and out again into the drop. LOAD sets it on at OFF; LOAD on a
    playing deck keeps it as it is.
- **CHORUS, DELAY and REVERB are one each, shared:** sends that both decks' tracks feed, with DJ's settings. A
  song in GLOBAL routing sends its whole mix to the ones it has on, at their MIX. A song's `#` commands for
  those three are skipped. **A deck's sends follow its crossfader gain,** so a deck faded out feeds no echo or
  reverb; what is already in them rings on.
- **The DJ FILTER filters the deck's sends too** (GLO's FILTER THE SENDS, ON at first): a deck's three send
  streams are summed on their own, go through copies of its filter, and are then added into the shared effects,
  so a sweep takes the deck's echoes and reverb with it. It costs about 3-4 % CPU (estimated) and a 3 KB block
  the two decks take in turn. OFF, the filter shapes the deck's dry sound only. A send whose effect is off (and
  not made to take it by ECHO OUT or REVERB OUT) is not filtered, as nothing hears it; its filter starts afresh
  when the effect comes on.
- **After the crossfader:** the punch-ins and DJ's LIMITER, so two songs together never clip.
- **DJ's FX setup** holds CHORUS, DELAY, REVERB, the LIMITER and the METRONOME (TEMPO FOLLOW: the main's
  tempo). FX from DJ shows LIVE and EDIT on them, the other effects not shown (they are the decks' songs');
  its ROUTING is SENDS; there is no MIXER page (the decks play their songs' mixers). At first: DELAY 1/8.,
  PING-PONG ON, FEEDBACK 40 %, MIX 30 %; REVERB SIZE 70 %, MIX 35 %; the LIMITER on; the rest as a new song's.
- **Punch-ins:** FX held + a white key runs them on the mix, as on every screen, from the buffer of what is
  heard. **A song's own punch-in commands** play while only its deck is heard (the crossfader at its end);
  while both decks are heard they are skipped.

### USB audio

- **USB audio carries the stereo mix,** at USB GAIN ([settings.md](settings.md)), as everywhere: the jack and
  USB play the same mix. DJ has no split cue: a second mix would cost a second set of DJ's audio work, which
  the FM-1 has no time for.

## Playing the decks

All from the sequencer and the engines, each on one deck; none needs recorded audio or RAM of its own.

### HOLD

**The deck's HOLD key** (OP2 the left's, PIT the right's) is a hand on the record, the way to move a deck against
the other by hand:
- **down:** the deck stops where it is (`HELD` on its state line), its notes let go;
- **its tempo knob turned meanwhile** (K1 the left, K4 the right) moves it by hand, forward or back: a row a
  click, a fast turn more *(proposed: one row a click, at 24 clicks a turn)*, at the hand's speed, the row's
  ticks spread over the time between clicks; turning back plays the previous rows as if they came next
  ([playback.md](playback.md)). Held still, the notes ring, as in a step;
- **its filter knob turned meanwhile** (K2 the left, K3 the right) moves it silently, a beat a click (a fast turn
  more), for the long way; what rang from the other knob is let go;
- **HOLD repositions, a loop with it, enabled through the hold** (its pad stays lit, the overview shows where it
  will be): REPEAT PATTERN switches to the pattern the head is in, REPEAT bars to the block of bars around it, a
  free LOOP starts at the head; the knobs move the head freely across the loop's ends meanwhile. Let go without
  moving the head, the loop is as it was;
- **let go:** a deck that was playing plays on from where the hand left it, at once (a synced deck too: SYNC
  then pulls its beats onto the main's, [above](#tempo-and-sync)); the time held is lost, so a tap holds the
  deck back by that much. A deck that was paused stays paused there: the way to a point for CUE.

### The pads

Each deck's 8 white keys, the left deck's 1-8, the right deck's 9-16, in one of three modes, for both decks:
**the blank key** turns PLAY ↔ LOOPS; **LFO** brings in the LFO pads, and again goes back to the mode before
(the blank key too).

- **PLAY:** where the deck plays, SET CUE, CUE, JUMP, MOVE | the stops, BRAKE, BACKSPIN, ECHO OUT, REVERB OUT.
  SET CUE, JUMP and MOVE never stop the deck.
  - **Pad 1, SET CUE:** sets the cue point without stopping the deck (`CUE: POS 02 BAR 4`): playing, on the bar
    line nearest the row heard, so a tap on the one, a little early or late, lands on it (the bar line after a
    position's last bar is the next position's start); paused, as CUE's tap; with HOLD down, on the bar line at
    or before the hand; with MOVE held, at MOVE's cursor ([below](#the-pads)). At the song's end it is refused
    (`THE END: CUE GOES BACK`), unless MOVE is held. **Held 250 ms or more** (as JUMP held goes on the beat), the
    cue point moves on to the start of the beat the press fell in, finer than the tap's bar line (`CUE: POS 02 BAR
    4 BEAT 2`, the beat counted from 0 as the bar; the state line `SET CUE ON THE BEAT`); paused, the deck goes
    there too. With MOVE held it stays at the cursor.
  - **Pad 2, CUE:** the deck's cue point, as a DJ player's CUE key ([above](#playing-and-pausing)), near the
    deck's PLAY key as there (the left deck's PLAY, OP1, sits over pads 1-2; the right's, OP6, over pads 8-9):
    paused, a tap sets it; playing, it goes back to it and plays from it while held, paused there when let go
    (the deck's PLAY key meanwhile: it plays on); held at the cue point, the deck plays from it while held.
  - **Pad 3, JUMP:** let go, the deck goes back to its cue point and plays on from it: after a tap at once (at
    the next row: the tap is the timing), after a hold of 250 ms or more on the next beat (`JUMP ON THE BEAT`,
    the pad blinking), so a hold let go just before a beat lands the cue on it. Paused or at the song's end, the
    deck goes to its cue point and plays (a synced right deck from the main's bar line, as its PLAY key).
  - **Pad 4, MOVE:** the deck moved by bars and patterns, playing on. Held, the deck's knobs move a cursor, as
    HOLD's do, the outer knob the finer: **K1 / K4 a bar a click, K2 / K3 a pattern a click** (the same bar of
    the other position's pattern, or its last). The cursor is a bar line: the press puts it on the bar the deck
    is in, shown at once, and **only the knobs move it**: the deck plays on under it and it stays
    put. It shows on the overview (cyan) and on the state line (`MOVE POS 03 BAR 2`), and never goes past the
    song's ends. **Let go, the deck moves on the next beat** (`MOVE ON THE BEAT`, the pad blinking) into the
    cursor's bar, on the beat of the bar it was playing, so the bars stay in phase, and plays on from there;
    paused, it is at the cursor's bar line at once. Let go without a turn, nothing moves. **SET CUE while MOVE
    is held** only sets the cue point at the cursor, its bar line (`CUE: POS 03 BAR 2`); letting MOVE go still
    moves the deck there.
  - **Pads 5-8, BRAKE, BACKSPIN, ECHO OUT, REVERB OUT:** the stops ([below](#the-stops)).
  - **A jump with a loop running** (JUMP, MOVE) takes the loop along, as HOLD does: REPEAT PATTERN repeats the
    position it lands in, REPEAT bars the block of bars around it, a free LOOP starts there.
- **LOOPS:** three kinds, a group of four each half: PATTERN, then REPEAT 4 BARS, 2 BARS, 1 BAR | free LOOPs of
  2 BARS, 1 BAR, 1/2 BAR, 1/4. Nothing shorter than a beat: the short ones are FX's punch-ins (LOOP 1/4 to 1/32,
  [fx.md](fx.md#punch-in-effects)), and loops shorter than 1/8 sounded bad and took more voices than the FM-1
  keeps up with; a free loop is never shorter than 0.15 s (above 400 BPM a longer note value).
  - **PATTERN** (pad 1), a tap on / off for its deck (`LEFT: REPEAT PATTERN ON`, `LEFT: REPEAT PATTERN OFF`):
    the deck's current pattern over and over: at the pattern's end the deck plays it again instead of the next,
    its breaks and jumps to other positions too. A jump (CUE, JUMP, MOVE, HOLD) takes it along:
    the position the deck goes to repeats. It is the longest of the loops, and one loop runs at a time: PATTERN
    switches a running loop off, another LOOPS pad switches PATTERN off. LOAD switches it off; SWAP takes it
    with its deck. It counts in the hands' counted place too ([Landing in time](#landing-in-time)).
  - **Pads 2-4, REPEAT the bars around:** the block of 4, 2 or 1 bars the deck is in, on the pattern's bar grid
    (4 BARS in bar 6: bars 5-8), round and round until the pad is tapped again; the deck then plays on through
    the block's end. Another of them re-aligns to its own block.
  - **Pads 5-8, a free LOOP:** from the last beat, the rows play to the loop's end and round again, until the pad
    is tapped again; the deck then plays on through the loop's end. Another free pad changes the length from the
    same start.
  - The rows themselves repeat, so it never clicks.
  - **Held, it is a ROLL:** the same while held; let go, the deck plays on from where the loop is, out through its
    end, as a LOOP tapped off does (not where it would have been: no landing in time). The loop
    starts as the pad goes down; let go within 250 ms it was a tap. The pads keep this limit of their own,
    shorter than the hold delay ([settings.md](settings.md)), so that a short stutter of a quarter second rolls.
  - **On a paused deck** the pads set the loop too, around where the deck stands (its pad lit, the overview
    green), and it goes round once the deck plays (a pad held there sets it as a tap: nothing plays to roll).
    **A loop or a repeat stays through a pause and a stop**, and plays again when the deck does: its PLAY key, the
    PLAY button, JUMP, and CUE's play, from the cue point, all go round it. Played from outside the loop, the
    deck takes it along as a jump does. A ROLL ends with a pause; LOAD clears them.
- **LFO:** the LFO pads ([below](#the-lfo-pads)), across both decks' keys: the crossfader's LFO, the decks'
  filters' and the FX LFO, and each deck's KEY.
- **A pad that cannot act** blinks fast: a stop on a deck not playing (or still waiting for its bar line, or
  already stopping), SET CUE at the song's end, MOVE let go when the song has ended under the cursor, a LOOPS
  pad at the song's end, a deck's LFO or KEY pad with no song on the deck.

### The stops

PLAY's pads 5-8, stopping the deck with an effect, one at a time: the record's, BRAKE and BACKSPIN | the tails',
ECHO OUT and REVERB OUT. **Each works while held:**
- **held, it stops:** the stop runs from the press; once the deck has stopped it stands silent there (the pad
  lit), its place counting on meanwhile ([Landing in time](#landing-in-time));
- **let go, it plays on** where it would have been, in time (after BRAKE or BACKSPIN spinning up in about 0.1 s;
  after ECHO OUT or REVERB OUT dry, the tail already sent ringing out under it). Let go before it has stopped,
  the deck comes back from there just the same: a tap is a dip;
- **the deck's PLAY key while the pad is held turns what letting go does:** a press, it stays stopped; another,
  it plays on again; the footer says which (`LET GO STAYS  OP1 PLAYS ON`). Let go staying, the deck is paused
  where the stop left it (a stop still running runs to its end first), and its PLAY plays on from there (after
  BRAKE or BACKSPIN spinning up).

The stops:
- **BRAKE:** the deck's rows slow and its notes' pitch falls with them, to a stop in 0.5 s: a turntable's STOP,
  one bend over the deck's voices.
- **BACKSPIN:** the record spun back by hand: from the row heard the rows run backwards, three times as fast and
  slowing, the notes' pitch high and falling with them (each note forwards *(proposed: what slides, arpeggios
  and retriggers do backwards)*), to a stop in 1 s,
  back from where it was. Inside a LOOP or REPEAT set the rows go backwards round it (from its start to its
  end), so a loop stays where it is.
- **ECHO OUT:** the deck's whole sound goes into the DELAY (DJ's settings, on for its echoes even when the DELAY
  is off); the deck stops on the next beat, and its echoes ring on for 4 s.
- **REVERB OUT:** the same into the REVERB (DJ's settings, on for it even when off): the deck washes out, stops
  on the next beat, the reverb ringing on for 6 s.
- **On the main, a synced right deck keeps its time:** SYNC follows the main's counted place through a held
  stop, and does not follow a kept stop's slowing rows.

### The LFO pads

**LFO** brings them in (`PADS: LFO`), and LFO again (or the blank key) goes back to the pads' mode before.
**LFO held** past the hold delay shows which pad does what: its layer
([../dialogs.md](../dialogs.md#hold-layers)) lists the 16 pads by key, a line a group of four, what is on white
(an LFO running, the reverse, a deck's KEY not 0), then how the on / off and KEY pads take knobs; it fits one
screen. Let go, the pads are the LFO pads, whatever they were before (already the LFO pads: they stay; the
mode before is kept for LFO's next tap).
They are LFOs: things that swing by themselves, each at its own speed, a note length of the tempo (`1/2` at
first). In groups of four across both decks' white keys, the crossfader's first, as it belongs to both decks:

| Pads | Do |
|---|---|
| 1-4 | the crossfader's LFO: 1 on / off, 2 shape, 3 range, 4 reverse |
| 5-8 | the left deck's DJ FILTER's LFO: 5 on / off, 6 shape, 7 range; 8 the left deck's KEY |
| 9-12 | the right deck's, the same: 9 on / off, 10 shape, 11 range; 12 its KEY |
| 13-16 | the FX LFO (REVERB MIX at first, [GLO](#the-properties-glo)): 13 on / off, 14 shape, 15 range; 16 every LFO off |

- **On / off** (pads 1, 5, 9, 13): the press switches it on (`FADER LFO: TRIANGLE 1/2`); it sets off on the
  next step (row) of the deck whose beats lead (the main, else the right; at once with no deck playing), the
  pad blinking till then, from where the thing is. A tap on a running one switches it off (`FADER LFO OFF`), its
  swing fading out over 0.2 s, back to where the thing is set. **Held, ALGORITHM sets its speed,** a note length
  of the leading deck's tempo a click: `32 BARS`, `16 BARS`, `8 BARS`, `4 BARS`, `2 BARS`, `1 BAR`, `1/2` (at
  first: 1 Hz at 120 BPM), `1/4` (a beat), `1/8`, faster clockwise, each one swing there and back (`FADER
  LFO 1/4`, the header's message following the turn); no `1/16`: faster is no longer an LFO; lengths faster than
  10 Hz are skipped (above 300 BPM, `1/8`). It follows the tempo (the leading deck's BPM, its BEATS A BAR for the bars; with no song, 120 BPM in
  4). SEL tapped after sets it back to `1/2`. Let go, a running one goes off only if ALGORITHM did not turn
  meanwhile, so its speed changes without stopping it. The LFOs run on when the pads go back to PLAY or LOOPS.
- **Shape** (pads 2, 6, 10, 14), **range** (3, 7, 11, 15) and **reverse** (4) change an LFO, running or not,
  each a tap to the next (`FADER: L TO R`, `FILTER L: SINE`):
  - **the crossfader:** `MIRROR` at first (between where it is and the same mirrored, through the middle:
    `L 50%` ↔ `R 50%`; from either end, end to end), then `L TO HERE` (between where ALGORITHM has it and the
    left end), `L TO R` (end to end), `SWING` (around where it is, out to either side as far as the end nearer
    it: at `L 50%`, from L to the middle), and round to `MIRROR` again;
    reversed `HERE TO R`, `R TO L` (end to end, setting off toward the right), `SWING` setting off toward the left
    (else the right), `MIRROR` as it is. **ALGORITHM alone still moves the crossfader:** in `L TO HERE`, `SWING`
    and `MIRROR` where the swing is around or comes from, in `L TO R` where the crossfader stays once the LFO
    stops. SWAP mirrors the swing with the crossfader. While it runs, the track is filled between where the
    crossfader is set and where the swing has it, as the filter row's slider is;
  - **a filter,** on the side its knob has it (LP or HP; at OFF, LP's): `OFF TO HERE` (at first: between OFF and
    the filter as heard, the filter coming in and out), `OFF TO FULL` (between OFF and the end of its side: LP's
    lowest, HP's highest), `HERE TO FULL` (between the filter as heard and that end), `SWING` (between the filter
    as heard and the same mirrored, through OFF: `LP 2.4K` ↔ its HP twin); never end to end. Its knob moves
    HERE. **It follows the deck's FILTER key:** heard only while the key has the
    filter on (`FILTER L LFO: ITS FILTER IS OFF` when switched on with it off). With the filter off it swings on
    unheard, around the setting the key brings in: **a preview** the deck's filter row shows, its slider moving in
    dark grey ([below](#the-filter-row)); the key brings the filter in where the swing is, in time, gliding over
    about 15 ms either way;
  - **the FX LFO:** the MIX of one of DJ's shared effects, the one DJ's properties pick (FX LFO: `REVERB MIX` at
    first, `DELAY MIX`, `CHORUS MIX`): `HERE TO 100 %` (between its MIX as set on FX and 100 %) or `0 TO 100 %`
    (end to end, setting off upward). It moves only the MIX, so a deck is heard swinging as much as its song
    sends to the effect. **It follows DJ's FX:** an effect switched off there stays off, and the LFO does
    nothing (`REVERB LFO: REVERB IS OFF` when switched on with it off). The footer's right shows the MIX as it
    swings, short (`REV 62%`, `DLY 40%`, `CHO 80%`, `REV OFF`), while it runs;
  - **the shapes,** moving as a hand on a knob would, each LFO's `TRIANGLE` at first, the pad turning to the
    next in this order: `SINE`; `SQUARE`, each flip taking a hand's 90 ms, eased in and out (at high
    frequencies at most a quarter of the cycle); `TRIANGLE`, slowing into each corner and starting back slowly (a
    sixth of each way eased at either end); `SAW UP`, `SAW DOWN`, their jumps back taking the same 90 ms;
    `RANDOM`, a new place each cycle, glided to. Each sets off from where the thing is toward the
    range's far end (`L` in `L TO HERE`, `OFF` in `OFF TO HERE`; in `OFF TO FULL` the end farther from it); a
    change of shape or range glides over about 15 ms.
- **KEY** (pads 8, 12), held: the deck's tempo knob (K1 the left, K4 the right) turns its KEY a semitone a
  click, −6 to +6 (`KEY L +1`): the whole deck down or up, its samples too, its tempo kept, for mixing in key.
  SEL tapped after sets it back to 0.
- **ALL OFF** (pad 16): every LFO off (`ALL LFOS OFF`).
- A LOAD on a paused deck switches its filter's LFO off, its filter on at OFF; a LOAD at the bar keeps both.

### Landing in time

While a held stop runs, the deck's place goes on counting at its tempo, through
the song's flow as playing would go (its jumps, breaks and tempo commands among the rows it passes, round a LOOP
or REPEAT set). Letting go jumps there, as a jump to a song position does. SYNC follows this counted place, so the right deck keeps time
with the main whatever the main's stops do. A ROLL counts no place: let go, the deck plays on from its loop.

### Mutes

HOME's combination mutes the decks' tracks, as on every screen ([../controls.md](../controls.md#homes-combination)):
white keys 1-8 the left deck's tracks `00`-`07`, keys 9-16 the right deck's. **OCT- / OCT+ belong to the
white keys there:** HOME + OCT+ takes them to `08`-`0F` (`MUTES: TRACKS 08-0F`; HOME + OCT- back to `00`-`07`),
where a deck's song has more than 8 tracks; with no tracks that way they do nothing, no message. Alone, OCT-
and OCT+ do nothing on DJ (dark). Solo is HOME, SEQ, a white key, within its deck; SEQ switches the white keys
between mute and solo as often as wanted. Mutes go with their deck in a SWAP, and are kept in the stand
([Saving the stand](#saving-the-stand)).

### Saving the stand

**SAVE keeps the whole DJ stand** with everything else ([../../files/dj.md](../../files/dj.md#the-stand)): both
decks as they stand (song, place, cue point, tempo, KEY, VOLUME, filter, loop or REPEAT, mutes and solo, the filter's
LFO) and the set (the crossfader, SYNC, the crossfader's and the FX LFO's setups). SAVE while the decks play
waits for a pause, as every SAVE does, so what is kept is where they paused; the decks standing otherwise than kept
count as a change (SAVE dim). **At power-on** the decks are back as kept, paused: open DJ (or have POWER-ON SCREEN on
DJ) and PLAY goes on from the same point with the same settings. Where an LFO is in its swing is not kept: an LFO
that was on sets off afresh as the decks play.

## Controls

The functions played live are on the note keys, easier to hit than the buttons. The holds are HOLD (a note key,
with a knob to move the deck), the CUE pad, MOVE (with a knob to move its cursor), the stops, the ROLL, an LFO's
on / off pad (with ALGORITHM, its frequency) and a KEY pad (with a knob), and the shortcuts every screen has. JUMP
held only times its jump.

**Knobs:**

| Knob | Does |
|---|---|
| SELECT | the song list |
| ALGORITHM | the crossfader, the left deck ← → the right, 32 clicks end to end *(proposed)*; a fast turn takes bigger steps. At first it is all the way to the left: the main heard alone. With an LFO's on / off pad held, that LFO's frequency ([the LFO pads](#the-lfo-pads)) |
| K1 | the left deck's TEMPO, the set's; with its HOLD key held, the deck by hand, a row a click, heard ([HOLD](#hold)); with its MOVE pad held, the cursor a bar a click ([the pads](#the-pads)); with its KEY pad held, its KEY |
| K2 | the left deck's DJ FILTER's setting, heard while its FILTER key has it on; SEL held: steps of 10. **ENV turns what K2 and K3 do,** a tap to the next: FILTER (at first), VOLUME (the deck's [volume](#volume); SEL held: 6 dB), then FILTER again. With its HOLD key held, the deck moved silently, a beat a click; with its MOVE pad held, the cursor a pattern a click |
| K3 | the right deck's, the same: its DJ FILTER's setting, or its VOLUME; with its HOLD key held, silently, a beat a click; with its MOVE pad held, a pattern a click |
| K4 | the right deck's TEMPO (nothing while synced); with its HOLD key held, the deck by hand, heard; with its MOVE pad held, a bar a click; with its KEY pad held, its KEY |
| PRESETS | nothing, SEL + PRESETS too: DJ has no keyboard functions (no key plays an instrument here) |

**Black keys:** a job a group, so each is found by touch: the left deck (the first three), the two LOADs (the
pair), the right deck (the second three, in the left's order), SYNC (the second pair's first; POLY beside it does
nothing), the pads' mode (the lone key at the end).

| Key | Group | Does |
|---|---|---|
| F#3 (OP1) | the left deck | PLAY / pause the left deck; held: back to its start |
| G#3 (OP2) | | HOLD the left deck: stopped while held; with K1, moved by hand |
| A#3 (OP3) | | FILTER: the left deck's DJ FILTER off and back on, sliding |
| C#4 (OP4) | the LOADs | LOAD the highlighted song on the left deck |
| D#4 (OP5) | | LOAD it on the right deck |
| F#4 (OP6) | the right deck | PLAY / pause the right deck; held: back to its start |
| G#4 (PIT) | | HOLD the right deck: stopped while held; with K4, moved by hand |
| A#4 (GLO) | | FILTER: the right deck's DJ FILTER off and back on, sliding |
| C#5 (MONO) | the set | SYNC on / off: the right deck to the main |
| D#5 (POLY) | | nothing (free for a later job) |
| F#5 (the blank key) | alone | the pads' mode: PLAY ↔ LOOPS (from the LFO pads: back to the mode before) |

The left deck's keys sit over its pads (white keys 1-8), the right deck's over its (9-16). CUE is a pad
(PLAY's 2, near the deck's PLAY key), the pattern's repeat too (LOOPS' 1, PATTERN).

**White keys:** the pads ([above](#the-pads)).

**Buttons:**

| Button | Does |
|---|---|
| SEQ | SWAP: the decks exchanged |
| OCT- / OCT+ | nothing alone; HOME + OCT: the tracks HOME's mutes reach, `00`-`07` or `08`-`0F` ([Mutes](#mutes)) |
| LFO | **tap:** the LFO pads ([the pads](#the-pads)); again, back to the pads' mode before (`PADS: LFO`, `PADS: PLAY`). **Held** past the hold delay: its layer lists the LFO pads by key ([the LFO pads](#the-lfo-pads)); let go, the pads are the LFO's (already: they stay) |
| GLO | the properties ([below](#the-properties-glo)) |
| PLAY | both decks: pause / resume; held: both back to their cue points |
| FX | the FX screen on DJ's setup; FX held + a white key: a punch-in on the mix |
| HOME | HOME's combination: the screens; the decks' tracks' mutes; PLAY both back to their cue points; REC the CUT; LFO the help |
| SAVE | saves |
| SEL | **tap:** resets the last knob turned (a tempo to the song's own, a filter to OFF, the crossfader to the middle), or the deck's KEY after a KEY pad, LFO's job on other screens; **held** + a knob: coarse steps |
| REC | nothing alone; **HOME + REC** is the [CUT](../controls.md#cut), as on every screen: every voice let go, everything heard fades out in about 12 ms, every effect emptied (the shared CHORUS, DELAY and REVERB, ECHO / REVERB OUT's, the DJ FILTERs, a punch-in); while REC is held no note starts and nothing is worked on, the decks keeping their time. Let go, it is silent unless a deck plays, which is heard again from its next notes, its effects starting dry |
| ENV | what K2 and K3 turn, a cycle: FILTER, VOLUME (`K2 K3: VOLUME`, `K2 K3: FILTER`); lit while off the first, the filters ([Knobs](#controls)) |
| EDIT, ARP | nothing |

- **While a deck key is held,** the other keys work as ever: a ROLL on one deck and a cue on the other go
  together.

## LEDs

Off, dim and full, and blinks, so they read the same through the controller (MIDI CONTROLLER)
([../../controller.md](../../controller.md)): dim where a key does something, full where something is on,
blinking where something waits, dark where nothing would happen ([../leds.md](../leds.md)).

**Black keys:**

| Key | Dark | Dim | Full | Blinking |
|---|---|---|---|---|
| PLAY | no song on the deck, or stopped at the song's end | loaded, paused | playing: a blink a beat, full on each of its beats (the beat's first row, ROWS A BEAT apart), dim between; two synced decks blink together | fast: a synced start waiting for the bar line; on the beat, full and dark: the last 8 bars |
| SYNC | no song | off | synced | |
| POLY | always | | | |
| HOLD | no song | ready | held | |
| FILTER | no song | off | on | |
| LOAD | | it can take the highlighted song | | fast: a load waiting for the deck's bar line, or refused |
| the blank key | | always | | |

Fast is 150 ms on, 150 ms off; slow 500 ms each.

**Pads:** lit for finding them by touch, as every group of white keys is ([../leds.md](../leds.md)): pads 1 and 5
of each deck (white keys 1, 5, 9, 13) dim, the others dark; over that, what is on full and what waits blinking:
- **PLAY:** pad 1, SET CUE: dim (the finding light), full for 0.3 s as it sets. Pad 2, CUE: full paused at the
  cue point, or held playing from it; blinking slowly paused away from it (a tap would set it there); else
  dark. Pad 3, JUMP: dark, blinking
  fast while a jump waits for the beat. Pad 4, MOVE: dark, full while held, blinking fast while the move waits for
  the beat. A stop (pads 5-8) full while held, and when kept until the deck
  stops (a tail rings on unlit).
- **LOOPS:** full while that loop or roll runs; PATTERN full while on.
- **LFO:** each group of four's first, its on / off pad (pads 1, 5, 9, 13), dim (the finding light), the others
  dark; what is on full: an on / off pad while its LFO runs (blinking fast while it waits for the step), the
  reverse while on, a KEY pad while held or while its deck's KEY is not 0, ALL OFF for 0.3 s as it acts. The
  shape and range pads stay dark (no on to show). A deck with no song: its group only the finding light.
- The left deck's pads 1-4 lie under its black keys (the first three), 5-8 under the LOADs; the right deck's
  9-12 under its three, 13-16 under SYNC, POLY and the pads' mode. The LFO pads keep their groups' places,
  the crossfader's under the left deck's keys.

**Buttons:** PLAY as for a song ([../transport.md](../transport.md#plays-two-leds)), green on the main's beats;
OCT- / OCT+ dark (in HOME's combination dim when there are tracks that way, else dark); SEQ dim while a deck has a song; LFO full
while the pads are the LFO's, else dim; ENV full while K2 / K3 turn the volumes (off the cycle's first), else
dim; GLO, FX, SEL, HOME, REC dim; SAVE by its own rule; EDIT, ARP dark.

**The layers are every screen's:** HOME's combination (the black keys the screens; the white keys the tracks:
1-8 the left deck's, 9-16 the right deck's), FX held (the punch-ins).

## Layout

The body is y 17-226; the frame's header and footer: [../frame.md](../frame.md). The decks side by side at the
top, the left one the main; the crossfader across the middle; the song list below.

### The decks and the list

```
DJ PLAY                       > |###     header: the name; the pads' mode
----------------------------------------
03 CITY LIGHTS      00 PUNCH IN          each deck's song (left: the main)
120.0 BPM    +0.0%  120.0 BPM     SYNC   its tempo; the change, or SYNC
POS 02/05 BAR 2/7   POS 00/05 BAR 0/7    its place: position, bar
._:##:.:#|#:._.:##: |.:##:._:##::._.:.:  its overview; | the playhead
# . . .      -00:58 # . . .      -01:36  the beats; the time left
                    AT THE CUE           what the deck does now
OFF     ---|---     LP 2.4K ##|---- LFO  the filter: its value, a slider (OFF in the middle)
MUTE 2              LOOP 1 BAR   KEY +1  what is set on it: the loop, the mutes, KEY
  FADER L =======|======= R ALG          the crossfader, its middle on the divider; ALG: the knob that moves it
----------------------------------------
   NR NAME             BPM LEN  TIME     the list: 5 show, scrolling
 R 00 PUNCH IN         124   6  01:33    L / R: on which deck
   01 SPICE ROAD        96   6  02:00
   02 MIDNIGHT RUN     130   6  01:29    the highlight, boxed
L  03 CITY LIGHTS      120   6  01:36
   04 ACID TEST        138   6  01:23
----------------------------------------
SET CUE JUMP MOVE|STOPS          REV 62% the footer: the pads' keys; the FX LFO (while it runs)
```

A deck's elements start at `x0`: 2 for the left deck, 123 for the right.

| Element | x, y | w × h | Font | Shows |
|---|---|---|---|---|
| the divider | 120, 17 | 1 × 112 | | between the decks |
| the song | x0, 19 | 18 characters | T | its number and name (15 characters); `NO SONG` |
| the volume | x0 + 110, 19 | 4 × 12 | | the deck's VOLUME, a bar ([Volume](#volume)) |
| the tempo | right-aligned to x0 + 40, 31 | | S | `120.0`, ` 96.0`; then `BPM` (T) at x0 + 44, 34; the change (`+3.2%`, `-12.5%`) or `SYNC` (T), right-aligned to x0 + 114, at 34 |
| the place | x0, 48 | | T | `POS 02/05 BAR 2/7`: the position of the last, the bar of the position's last (both indexes, hex from 0) |
| the overview | x0, 61 | 114 × 14 | | [below](#the-overview) |
| the beats | x0 + k × w, 79 | (w − 2) × 8, w = floor(60 / BEATS A BAR) | | the bar's beats; the one playing lit |
| the time left | right-aligned to x0 + 114, 77 | | T | `-00:58`, at the deck's tempo (tempo commands ahead not counted, as on SONGS) |
| the state line | x0, 91 | 19 characters | T | [below](#the-state-line) |
| the filter row | x0, 104 | 114 × 12 | T | [below](#the-filter-row): the value at x0; the slider x0 + 47 to x0 + 93; `LFO` right-aligned to x0 + 114 |
| the set line | x0, 117 | 19 characters | T | [below](#the-set-line): the loop at x0, the mutes after it; KEY right-aligned to x0 + 114 |
| the crossfader | the bar centred on the decks' divider: `FADER` 14, 131; `L` 50; the bar 60; `R` 184; `ALG` 196 | the bar 120 × 11 | T | the track 1 px at y 136; the mark 3 × 11 at 60 + round(position × 117 / 32) (the middle at 119-121, on the divider at 120); `ALG`: the knob that moves it. Its LFO on: the mark (white) where the swing has it, one dark grey where it comes back to (the set place), and the track filled between them, 5 px high, light grey |
| the FX LFO | the footer's right | | T | `REV 62%` while the FX LFO runs: its effect's MIX now, as it swings, the effect short (`REV`, `DLY`, `CHO`); `REV OFF` while DJ's FX has it off; `REV` alone while DJ does not play yet (no MIX to show) |
| the rule | 0, 144 | 240 × 1 | | |
| the column names | 0, 146 | | T | `NR NAME BPM LEN TIME`, over their columns (LEN: the positions, the song's length) |
| a song's row, *i* = 0-4 | 0, 158 + 13 *i* | 240 × 13 | T | |
| – the decks' marks | 2, row | 2 characters | T | `L `, ` R`, `LR` |
| – number | 18, row | 2 characters | T | hex |
| – name | 36, row | up to 16 characters | T | |
| – BPM | 138, row | 3, right-aligned | T | the song's own |
| – positions | 162, row | 3, right-aligned | T | |
| – length | 192, row | 5 | T | mm:ss, as on SONGS ([songs.md](songs.md)) |
| – the highlight's box | 0, row − 1 | 240 × 13 | | |

The list shows 5 songs and scrolls a song at a time as the highlight moves, keeping it in the middle while it
can, as the song list does ([../dialogs.md](../dialogs.md#the-song-list)). The header shows no page.

### The state line

What the deck does, yellow; **every key or pad held shows its word there while it is down**, so no hold is
silent. One thing at a time, the first that applies: `LOADS AT THE BAR` (`LOADS AT ITS TEMPO` once LOAD was held
long enough), `HELD`, `MOVE POS 03 BAR 2` (MOVE held: its cursor's place), the keys and pads held: `CUE` (`CUE
PLAY` while it plays from the cue), `SET CUE` (`SET CUE ON THE BEAT` held past 250 ms), `JUMP` (`JUMP ON THE BEAT` held past 250 ms), the deck's PLAY key
held past 250 ms `HOLD: TO THE START` (then `BACK TO THE START`), its FILTER key `FILTER ON LP 2.4K` or `FILTER
OFF`, its KEY pad `KEY`, its filter's LFO on / off pad `LFO 1/2` (its speed); then `ROLL 1/4` (while held), `BRAKE`,
`BACKSPIN`, `ECHO OUT`, `REVERB OUT`, `STARTS AT THE BAR`, `JUMP ON THE BEAT`, `MOVE ON THE BEAT`, `AT THE END`, `AT
THE CUE`; else nothing. The line has the deck's whole width: the filter and what is set on the deck (a loop, KEY)
have lines of their own.

### The filter row

The deck's DJ FILTER, under the state line, coloured by its side so LP and HP read at a glance, as a DJ mixer's
filter knob does:
- **the value** at x0, as FILTER's CUT ([fx.md](fx.md)): `LP 2.4K` light blue, `HP 400` brown, `OFF` dark grey;
  a setting its FILTER key has switched off dark grey (`HP 400` set, unheard);
- **a slider** from x0 + 47 to x0 + 93: a dark grey track (1 px, y 109), OFF in the middle (a light grey tick,
  1 × 7, at x0 + 70), filled 5 px high from the middle out to the value, LP's end at the left, HP's at the right
  (23 px each way), in the value's colour;
- **its LFO:** while it swings the filter, the value and the fill where the sound has it now, and where it comes
  back to a dark grey tick; `LFO` right-aligned to x0 + 114, white. **With the filter off, a preview:** the value
  and the fill where the swing would have it, dark grey and moving, the setting the key brings in a tick in its
  side's colour, `LFO` dark grey; the key brings the filter in there ([above](#the-lfo-pads)).

### The set line

What is set on the deck, for as long as it is set, under the filter row (the state line above says what happens
now); each only when set:
- **the loop going round,** green, at x0: `REPEAT 1 BAR`, `LOOP 1/2 BAR`, `REPEAT PATTERN` (a ROLL is a hold: the
  state line's, while held). The overview shows its rows green as well;
- **the mutes,** after the loop: `MUTE 2` (how many of the song's tracks are muted), `SOLO 05` (the track soloed);
  `MUTE` and `SOLO` cyan, the number light grey;
- **KEY,** right-aligned to x0 + 114, white: `KEY +1`, `KEY -3`.

The line holds 19 characters, a space between each. Short of room, the loop drops its first word (`PATTERN`,
`4 BARS`, `1/2 BAR`), and if that is still too long, the mutes give way (the loop then in full where it fits).

### No songs on the decks

```
DJ PLAY
----------------------------------------
NO SONG             NO SONG

OP4: LOAD THE       OP5: LOAD THE
HIGHLIGHTED SONG    HIGHLIGHTED SONG




  FADER L|============== R ALG           at first all the way to the left
----------------------------------------
   NR NAME             BPM LEN  TIME
   00 PUNCH IN         124   6  01:33
   01 SPICE ROAD        96   6  02:00
   02 MIDNIGHT RUN     130   6  01:29
   03 CITY LIGHTS      120   6  01:36
   04 ACID TEST        138   6  01:23
----------------------------------------
SET CUE JUMP MOVE|STOPS
```

A deck with no song: `NO SONG` on its song line, `OP4: LOAD THE` (the right deck: `OP5: LOAD THE`) at y 48 and
`HIGHLIGHTED SONG` at y 61; nothing else (no filter row, no set line). With no songs at all, the list says `NO
SONGS: MAKE ONE ON SONGS` at 2, 158.

### The overview

A tracker's stand-in for a DJ player's waveform: **each bar of the song is a column as tall as it has notes**
(notes started in the bar, every track; 2-14 px from the bottom, scaled to the song's busiest bar), read from
the song's patterns once at LOAD (about 120 B a deck).

- Bar *b*'s columns: from `x0 + floor(b × 114 / bars)` to the next bar's.
- **Over it:** the positions' boundaries (1 px, black), the playhead (1 px, white, y 60-76), the cue point (a
  mark 3 × 2 at y 59), MOVE's cursor while MOVE is held (1 px, cyan, y 60-76); the playhead and MOVE's cursor
  each between two 1-px black lines (y 60-76), so they show on any column in any theme (in a light one white is
  the darkest colour, as dark as the pattern playing's columns). **What goes round** (a loop's or a roll's rows as the song's flow plays them, or REPEAT
  PATTERN's pattern): green to the row, the part of each bar's column it covers (a 1/4 loop a quarter of its
  bar's), and a 2-px line under that part, so a bar with no notes shows too; the boundaries cut through the line
  as through the columns.
- **The pattern playing** light grey, the song's other bars dark grey, so the section the deck is in shows at a
  glance; the last 8 bars red while their warning runs.

### Colours

- **The header:** `DJ` in the PLAY group's colour ([../colours.md](../colours.md)); the pads' mode
  (`PLAY`, `LOOPS`, `LFO`) white; the page, where there is more than one, light grey.
- **A deck:** the song's number dark grey, its name white, green while the deck plays; `NO SONG` dark grey, the
  hint's `OP4` and `OP5` yellow, the rest light grey; the tempo white, `BPM` light grey, the change light grey
  (white when not 0), `SYNC` green; `POS` and `BAR` cyan, their numbers light grey; the beats deep green, the
  one playing yellow on the bar's first beat and green on the others; the time left light grey, red in the last
  8 bars; the state line yellow; the filter row by its side, LP light blue, HP brown, OFF dark grey
  ([above](#the-filter-row)); the set line's loop green, `MUTE` / `SOLO` cyan and their numbers light grey, `KEY
  +1` white ([above](#the-set-line)).
- **The overview:** as above; the cue point yellow, what goes round green.
- **The crossfader:** `FADER`, `L` and `R` cyan, `ALG` yellow (a control, as the help draws one), the track dark
  grey, the mark white; with its LFO on, where it comes back to dark grey, the fill between them light grey.
- **The FX LFO** in the footer's right: white.
- **The list:** the column names cyan; a deck's mark green while that deck plays the song, else light grey;
  numbers dark grey; names light grey, the highlighted one white in a white box; BPM, positions, length light
  grey; `NO SONGS: MAKE ONE ON SONGS` dark grey.
- **The divider and the rules** dark grey.
- **The footer's left:** no instrument here (the keys are the decks', PRESETS does nothing): the pads' keys by
  their mode, the keys yellow, the rest light grey ([Texts](#texts)).
- **The footer's right:** the FX LFO white; a knob's name and value white. The voices sounding are the header's
  number, as on every screen ([../frame.md](../frame.md#the-header)).

### The properties (GLO)

The properties dialog ([../dialogs.md](../dialogs.md#properties)), titled `DJ PROPERTIES`:

| Item | Shows | K1 |
|---|---|---|
| CROSSFADER | `SMOOTH`, `SHARP` | SMOOTH: equal power, each deck at −3 dB in the middle, as loud through the blend; SHARP: both full until the last 2 clicks at either end, for cuts *(proposed)* |
| FILTER RESONANCE | `30 %` | the DJ FILTERs' resonance, 0-100 % |
| FILTER THE SENDS | `ON`, `OFF` | ON: a deck's filter also filters what it sends to CHORUS, DELAY and REVERB ([above](#effects)); OFF: its dry sound only, saving ~3-4 % CPU |
| FX LFO | `REVERB MIX`, `DELAY MIX`, `CHORUS MIX` | what the FX LFO swings (the LFO pads 13-15, [above](#the-lfo-pads)): that effect's MIX |
| RESET DJ | `EDIT` (yellow: the key) | EDIT, after a question (`RESET DJ?`, `BOTH DECKS EMPTIED, ALL AS AT FIRST`; RESET with OCT+, KEEP with OCT-): the stand back to a new store's ([Saving the stand](#saving-the-stand)): both decks empty and paused, the crossfader all the way to the left, SYNC on, every LFO off at its first setup (`DJ RESET`). The properties above and DJ's FX setup stay. SAVE then keeps it, as any change of the stand; K1 does nothing here |

## Texts

- **The header:** `DJ`; `PLAY`, `LOOPS`, `LFO`.
- **The decks:** `NO SONG`, `OP4: LOAD THE`, `OP5: LOAD THE`, `HIGHLIGHTED SONG`; `BPM`, `SYNC`, `POS`, `BAR`;
  `LOADS AT THE BAR`, `LOADS AT ITS TEMPO`, `HELD`, `MOVE POS 03 BAR 2`, `CUE`, `CUE PLAY`, `SET CUE`, `SET CUE ON THE BEAT`, `JUMP`,
  `HOLD: TO THE START`, `BACK TO THE START`, `FILTER ON LP 2.4K`, `KEY`, `LFO 1/2`, `LFO 4 BARS`, `ROLL 1/4`, `BRAKE`, `BACKSPIN`, `ECHO OUT`, `REVERB OUT`, `STARTS AT THE BAR`, `JUMP ON THE BEAT`, `MOVE ON THE BEAT`, `AT THE END`,
  `AT THE CUE`; the filter row's `OFF`, `LP 2.4K`, `HP 400`, `LFO`; the set line's `REPEAT 1 BAR`, `LOOP 1/2 BAR`,
  `REPEAT PATTERN` (short: `PATTERN`, `4 BARS`), `MUTE 2`, `SOLO 05`, `KEY +1`, `KEY -3`.
- **The crossfader:** `FADER`, `L`, `R`, `ALG`.
- **The footer's left** (cut short before the right side's text): PLAY `SET CUE JUMP MOVE|STOPS` (the
  `|` dark grey); LOOPS `1 PATTERN 2-4 BARS 5-8 FREE`; LFO `1 FADER 5 FILTER 13 REVERB` (the FX LFO's effect); the
  keys yellow. While a key or pad is down, what it does instead, the first that applies in this order: a HOLD
  key held `K1 ROWS HEARD  K2 BEATS`; a stop's pad held `LET GO PLAYS ON  OP1 STAYS`, after the deck's PLAY key
  `LET GO STAYS  OP1 PLAYS ON` (`OP6` for the right deck); CUE held playing from the cue `LET GO CUE  OP1 PLAYS
  ON`, after the deck's PLAY key `LET GO PLAYS ON  OP1 CUE`; JUMP held `LET GO JUMPS TO THE CUE`, past 250 ms `LET
  GO CUE ON THE BEAT`; else CUE or SET CUE held the cue point `CUE POS 02 BAR 4` (`CUE POS 02 BAR 4 BEAT 2` on a
  beat); a MOVE pad held `K1 BARS  K2 PATTERNS` (`K4`, `K3` for the right deck); an LFO's on / off pad held `ALG
  SPEED  LET GO OFF` (`LET GO STAYS ON` when it was off, or ALG turned); a KEY pad held `K1 KEY, A SEMITONE A
  CLICK`. `LET GO`, `CUE` and the knobs and keys named yellow, the rest light grey.
- **The list:** `NR`, `NAME`, `BPM`, `POS`, `TIME`; `L`, `R`, `LR`; `NO SONGS: MAKE ONE ON SONGS`.
- **The footer's right:** the FX LFO's MIX while it runs (`REV 62%`; `REV`, `DLY`, `CHO`; `REV OFF` while DJ's FX
  has it off), else nothing; while a knob turns: `TEMPO L 124.0 BPM +3.2%`, `TEMPO R 118.0 BPM -1.7%`,
  `FILTER L LP 2.4K`, `FILTER R OFF`, `VOLUME L -3 DB`, `VOLUME R +2 DB`, `VOLUME L OFF`, `X-FADER MIDDLE`, `X-FADER L 50%`, `X-FADER R 100%` (the way from the middle to that end),
  while a HOLD key's knob turns: `POS 02 ROW 1F`; while MOVE's: `MOVE L POS 03 BAR 2` (its cursor); an LFO's
  speed `FADER LFO 1/4`, `FILTER L LFO 2 BARS`, `REVERB LFO 1/2` (SEL after); a KEY `KEY L +1`.
- **Dialogs:** `DJ PROPERTIES`: CROSSFADER, FILTER RESONANCE, FILTER THE SENDS, FX LFO, RESET DJ; `SMOOTH`,
  `SHARP`, `ON`, `OFF`, `REVERB MIX`, `DELAY MIX`, `CHORUS MIX`, `EDIT`. RESET DJ's question:
  `RESET DJ?`, `BOTH DECKS EMPTIED, ALL AS AT FIRST`; RESET (OCT+), KEEP (OCT-).
  HOME's combination adds `KEYS 1-8: LEFT DECK, 9-16: RIGHT` and says `PLAY: BACK TO THE CUES`
  ([../dialogs.md](../dialogs.md#homes-combination)).
- **Toasts:**

  | Toast | When |
  |---|---|
  | `LEFT: CITY LIGHTS`, `RIGHT: PUNCH IN` | LOAD |
  | `LEFT: CITY LIGHTS AT THE BAR` | LOAD on a playing deck: on its next bar line, its tempo kept |
  | `LEFT: CITY LIGHTS AT ITS TEMPO` | LOAD held on a playing deck: the song's own tempo |
  | `LEFT: REPEAT PATTERN ON`, `LEFT: REPEAT PATTERN OFF` | LOOPS' PATTERN pad |
  | `LEFT: BACK TO THE START` | a deck's PLAY key held |
  | `LEFT: LOAD CANCELLED` | LOAD again before that bar line |
  | `SONG 0A IS EMPTY` | LOAD of a song with no positions (refused) |
  | `NO SONG ON THE DECK` | PLAY, CUE, SYNC, HOLD or FILTER on an empty deck (refused) |
  | `FILTER L OFF`, `FILTER L ON LP 2.4K` | a deck's FILTER key |
  | `THE END: CUE GOES BACK` | a deck's PLAY, or PLAY, or SET CUE, at a song's end (refused) |
  | `DECKS SWAPPED` | SEQ |
  | `NO SONGS TO SWAP` | SEQ with both decks empty (refused) |
  | `SYNC ON`, `SYNC OFF` | SYNC |
  | `SYNC: TEMPO ON THE MAIN DECK` | K4 while synced |
  | `SYNC EXTERNAL: THE CLOCK'S TEMPO` | the main deck's tempo knob while an outside clock leads it |
  | `TEMPO 120.0 BPM: THE SONG'S` | SEL after a tempo |
  | `CUE: POS 02 BAR 0`, `CUE: POS 02 BAR 4 BEAT 2` | CUE or SET CUE sets the cue point (on a beat: SET CUE held) |
  | `PADS: PLAY`, `PADS: LOOPS`, `PADS: LFO` | the blank key; LFO |
  | `BACK TO THE CUES` | PLAY held; HOME + PLAY |
  | `PAUSED: PLAY AGAIN FOR DJ` | PLAY while something not started on DJ plays: it pauses |
  | `LEFT TRACK 02 MUTED`, `RIGHT TRACK 02 ON`, `LEFT TRACK 02 SOLO`, `SOLO OFF` | HOME + white key |
  | `KEY 0` | SEL after a KEY turned |
  | `K2 K3: VOLUME`, `K2 K3: FILTER` | ENV: what K2 and K3 turn |
  | `FADER LFO: TRIANGLE 1/2`, `FADER LFO: SINE 4 BARS`, `FADER LFO OFF`, `FILTER L LFO: ITS FILTER IS OFF`, `REVERB LFO: REVERB IS OFF`, `REVERB LFO OFF` | an LFO's on / off pad (ALG turning its speed shows as a knob: `FADER LFO 4 BARS`) |
  | `FADER: L TO HERE`, `FADER: L TO R`, `FADER: SWING`, `FADER: MIRROR`, `FADER: HERE TO R`, `FADER: R TO L`, `FILTER L: OFF TO HERE`, `FILTER L: OFF TO FULL`, `FILTER L: HERE TO FULL`, `FILTER L: SWING`, `REVERB: HERE TO 100 %`, `REVERB: 0 TO 100 %` | its range pad, the crossfader's reverse |
  | `FADER: SINE`, `SQUARE`, `TRIANGLE`, `SAW UP`, `SAW DOWN`, `RANDOM` | its shape pad |
  | `ALL LFOS OFF` | pad 16 on the LFO pads |
  | `MUTES: TRACKS 08-0F`, `MUTES: TRACKS 00-07` | HOME + OCT+, HOME + OCT-: HOME's mutes' tracks |
  | `DJ RESET` | GLO's RESET DJ, answered RESET |

## Help

HOME + LFO ([../help.md](../help.md)): five pages of its own, then the shared ones. The help draws the first
page's last line, `PRESS HOME TO GET BACK`, itself. The black key printed GLO is called `BLACK GLO`, apart from
the GLO button, and the blank key `BLANK KEY` *(proposed: these two names)*. The black keys' readings
(`OPEN PAGE 5` …) name screens, so DJ's help leaves them out: here the keys are the decks'.

```
[DJ]: TWO SONGS MIXED LIVE
DECKS: LEFT AND RIGHT, A SONG ON EACH
{SELECT}: PICK A SONG IN THE LIST
{OP4}, {OP5}: LOAD IT ON THE LEFT / RIGHT
{OP1}, {OP6}: PLAY / PAUSE THE LEFT / RIGHT
{PLAY}: PLAY / PAUSE BOTH AT ONCE
{ALGORITHM}: THE CROSSFADER, L TO R
{K1}: THE LEFT'S TEMPO, THE SET'S
{MONO}: SYNC THE RIGHT TO THE LEFT
  IT STARTS ON THE LEFT'S BAR LINE
{K4}: THE RIGHT'S TEMPO, NOT SYNCED
{K2}, {K3}: THE LEFT, RIGHT FILTER
{SEQ} (SHOW ELSE, QUICK): SWAP THE DECKS
WHAT PLAYED: GOES ON TILL A DECK PLAYS
  {PLAY} PAUSES IT, A DECK'S PLAY CUTS IT
```

```
THE PADS: THE {WHITE KEYS}
  1-8 THE LEFT DECK'S, 9-16 THE RIGHT'S
{BLANK KEY}: PLAY PADS OR LOOP PADS
PLAY PADS
1 SET CUE: CUE POINT HERE, PLAYING ON
2 CUE: PAUSED, SETS THE CUE POINT;
  PLAYING, PLAYS FROM IT WHILE HELD
3 JUMP: TO THE CUE POINT, PLAYING ON
4 MOVE: HOLD, {K1} BARS, {K2} PATTERNS
  LET GO: THERE, ON THE NEXT BEAT
5-8: BRAKE, BACKSPIN, ECHO, REVERB OUT
  STOP WHILE HELD; LET GO: BACK IN TIME
  {OP1} / {OP6} MEANWHILE: IT STAYS
LOOP PADS
1: THE PATTERN; 2-4: 4, 2 OR 1 BARS
5-8 FROM HERE: 2, 1, 1/2, 1/4 BAR
  TAP: LOOP ON / OFF; HOLD: A ROLL
```

```
THE DECKS BY HAND
HOLD {OP2}: THE LEFT STOPS, A HAND ON IT
  {K1}: ROWS, HEARD; {K2}: BEATS, SILENT
  LET GO: IT PLAYS ON FROM THERE
HOLD {PIT}: THE SAME, RIGHT ({K4}, {K3})
{OP3}, BLACK {GLO}: A DECK'S FILTER
  OFF AND BACK ON, SLIDING
{ENV} (EXPLORE NAMED VAULTS): {K2}, {K3}
  TURN VOLUMES, AGAIN: FILTERS
{SEL}: THE LAST KNOB TURNED, BACK:
  TEMPO, FILTER, FADER, VOLUME, KEY
HOLD {OP1} OR {OP6}: A DECK TO ITS START
HOLD {PLAY}: BOTH BACK TO THEIR CUE
HOLD CUE + {OP1} / {OP6}: LET GO, PLAYS ON
A PLAYING DECK: LOADS ON ITS NEXT BAR
THE LAST 8 BARS: ITS PLAY KEY BLINKS
```

```
THE LFO PADS: THINGS SWINGING ALONE
{LFO}: THESE PADS, AGAIN: BACK
  HELD: SHOWS WHICH PAD DOES WHAT
PAD 1: THE CROSSFADER'S LFO ON / OFF
  2 SHAPE: SINE, SQUARE, TRIANGLE,
    SAW UP, SAW DOWN, RANDOM
  3 RANGE: MIRROR, L TO HERE, L TO R,
    SWING; 4 REVERSE: HERE TO R
PAD 5: THE LEFT FILTER'S LFO, 6 SHAPE,
  7 RANGE: OFF TO HERE, OFF TO FULL,
  HERE TO FULL, SWING; 9-11 THE RIGHT'S
PAD 13: THE FX LFO, REVERB MIX ({GLO}),
  14 SHAPE, 15 RANGE; 16: ALL LFOS OFF
HOLD 1, 5, 9 OR 13: {ALGORITHM} THE SPEED,
  32 BARS TO 1/8; {SEL}: BACK TO 1/2
  LET GO, NO TURN: A RUNNING ONE STOPS
HOLD 8 OR 12: {K1} OR {K4} A DECK'S KEY
```

```
THE SET
{GLO}: CROSSFADER, FILTER, FX LFO,
  RESET DJ
{FX}: DJ'S OWN DELAY, REVERB, CHORUS
HOLD {FX} + {WHITE KEY}: A PUNCH-IN
{HOME}, {WHITE KEY}: MUTE A TRACK
  1-8 THE LEFT DECK'S, 9-16 THE RIGHT'S
{HOME}, {OCT+} / {OCT-}: MUTES 08-0F, 00-07
{HOME}, {PLAY}: BOTH BACK TO THEIR CUE
{HOME}, {REC}: SOUND OFF, THE DECKS PLAY ON
{SAVE}: KEEPS THE DECKS AS THEY STAND
LEAVING [DJ]: THE DECKS PLAY ON
USB AUDIO: ALWAYS THE MIX, IN STEREO
```

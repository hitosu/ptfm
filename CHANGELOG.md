# Changelog

What changes in PT/FM from one version to the next: the firmware, its pages (install, emulator, editor, manual,
site) and the demo disk. Between releases the top section, **Next version**, collects the changes made since the last
release; at a release it takes that version's number and date, and the first change after it starts a new Next version
above it. Earlier changes are in the
git history.

Each change is one entry, its kind first:

- **FEATURE:** something new;
- **CHANGE:** something that works differently;
- **BUG:** something fixed;
- **REMOVED:** something taken out.

## Next version

- FEATURE: The site's landing shows the third video, WHAT'S NEW IN PT/FM 0.27: a card under the second video's, the
  same kind.

## 0.27 (2026-11-08)

- FEATURE: Safe installs. An install cut short (a cable pulled, the power lost) no longer leaves the FM-1 dark: a
  start check at the very start of the firmware slot checks the firmware against its mark at every start, and with
  the firmware not whole it starts PT/FM's update loader, now kept in the flash between installs. The FM-1 then waits
  in UPDATE MODE, saying why, and the install page finishes the install. The loader writes the firmware's mark last,
  once the whole package checks out.
- FEATURE: The FM-1's screen during an install: INSTALLING, CHECKING or WRITING with the share done, a bar and DON'T
  SWITCH OFF, then DONE; it shows with any installer.
- FEATURE: Faster installs from PT/FM's pages: the update loader compares each block of the firmware with what the
  FM-1 holds and fetches only those that differ; installing the same firmware again writes nothing.
- CHANGE: The red PT/FM CRASH screen (and PT/FM STOPPED) stays until a button is pressed, so its numbers can be
  noted, instead of restarting after 4 seconds.
- BUG: FM-1s with the later flash chip (UC25HQ80) refused PT/FM's install, and could have saved nothing. Both of the
  FM-1's flash chips are now accepted, by the firmware and by its update loader.
- FEATURE: The red screens show the firmware's version and its BUILD code, 8 hex digits that tell exactly which build
  crashed, so a photo of the screen is a complete report.
- CHANGE: PT/FM CRASH names each of its numbers and shows more of what the crash found: the stack pointers, the
  protected area written into, the last branches, the time since the start, the late halves and the second core's
  state.
- CHANGE: USB RESCUE is now UPDATE MODE: OCT- held at power-on, or two failed starts in a row, start it as before,
  and so does a firmware that is not whole. With no update loader kept, or when it does not come up, the FM-1 falls
  to the chip's own USB mode (a dark screen).
- CHANGE: The debug build's figures and tests (the answers to the debug page, the layout's proofs) are a debug part
  of their own, written into the flash by the debug page as the last step of INSTALL DEBUG FIRMWARE (no key of its
  own), so the debug build's slot is the release's and the sound's probes. Its user area ends 32 KB lower.
  The watchdog test's second restart now ends in UPDATE MODE.

- FEATURE: FAVORITES: cartridges of favourite instruments from any cartridge, any engine, in one list. The built-in
  FAVORITES (`FB`) is there from the first start, empty; NEW makes more of your own. Each slot points at an instrument
  (`0B:05`), so what you play, record or write keeps that instrument's own number. On INSTRUMENTS a FAVORITES cartridge
  lists each favourite's source and shows it in detail below; PRESETS steps through the favourites everywhere; EDIT on
  one points it elsewhere (K1 the cartridge, PRESETS the instrument); SEL there offers MARK, COPY, PASTE, MOVE, SWAP and
  REMOVE. A favourite whose instrument is gone shows `MISSING` in grey and plays nothing. FAVORITES come first in every
  cartridge list, MIDI's bank select included; moving or swapping an instrument takes its favourites along, CLEAN UP
  counts them as uses, CHECK REFERENCES lists the missing ones.
- FEATURE: ADD TO FAVORITES in INSTRUMENTS' SEL menu, on an instrument (or every marked one): into the first empty slot
  of FAVORITES (with several FAVORITES cartridges, pick which), skipping those already there.
- FEATURE: SEL + GLO (SEL held first) on every screen but DJ: to the built-in FAVORITES at the favourite last used, and
  back to where you were; holding SEL lists it (`GLO: FAVORITES AND BACK`). GLO alone works as before.
- FEATURE: GLO held + PRESETS on every screen but DJ and INSTRUMENTS: a favourite just for now. Its sound becomes what
  the keys play while the current cartridge stays, the favourites' list showing while GLO is held; the next PRESETS
  click goes on in the cartridge from where it was. Holding GLO lists it (`PRESETS: A FAVORITE, ONCE`); GLO held that
  long and let go no longer opens the properties, as with every button that has a layer.
- FEATURE: NEW CARTRIDGE in INSTRUMENTS' slot menu: a new cartridge of any type, made and opened.
- BUG: The debug build's AUTOTEST no longer fails for nothing now and then: KEYS on `DELAY AND REVERB AT WORK`, which it
  could read halfway through an audio block, and CHECK REFERENCES or CHECK STORAGE on a toast it looked for too late.
- BUG: DJ: a LOAD on a playing deck, and JUMP or MOVE on the beat, now land on the bar line or the beat exactly: the
  new song starts where the old one's bar would have begun. Before, they came a little late, after the old song's bar
  had started, and on fast songs a bar line or a beat could pass unseen. A JUMP or MOVE meanwhile no longer lands a
  waiting LOAD at once (it waits for the next bar line played into), and what waits for a bar line or a beat waits
  through a held stop instead of cutting it short or getting lost.
- BUG: DJ: a deck stopped at its song's end stayed there only until DJ went live again, and then played the song from
  its start. It now stays at its end (AT THE END, its PLAY refused) and is shown there: the playhead at the end, no time
  left.
- BUG: DJ: a key or pad held through RESET DJ went on acting: with CUE held, the next song's PLAY key did nothing. A
  key or pad held through it now does nothing more when let go.
- BUG: DJ: a synced deck waiting for the main's bar line could start on a row BACKSPIN played back; it now waits for the
  bar line of the main's counted place. A deck let go after a held stop lands round a LOOP or REPEAT set, as its counted
  place goes.
- BUG: DJ's set line counts only the song's tracks in `MUTE 2`, and the mutes give way when the line is too long (they
  ran into KEY); a ROLL held shows only on the state line. The filter row's LFO marks where the knob has the filter dark
  grey (in its side's colour while previewing), and `LFO` sits right-aligned.
- BUG: DJ's texts: SEL after a volume shows `VOLUME L 0 DB` (or `R`), after a KEY the toast `KEY 0`; a ROLL's tap no
  longer toasts `LEFT: LOOP 1 BAR`; a tempo's change shows to the nearest 0.1 % (one click up from the song's own:
  `+0.1%`); a synced half tempo is rounded (62.8 BPM beside a main at 125.5, not 62.7); above 300 BPM an LFO's fastest
  speed is 1/4.
- BUG: DJ's LEDs: JUMP and MOVE blink while they wait for the beat, an LFO's on / off pad while it waits for its step;
  SET CUE and ALL OFF light full as they act; CUE blinks slowly paused away from the cue point; a stop kept by the
  deck's PLAY key stays lit until the deck stops; a deck's PLAY key goes dark at its song's end and blinks on the beat
  through the last 8 bars; LOAD is dark with no song to take (the playground too).
- BUG: DJ with no songs: once songs are made, the next entry highlights the current song, not the first.
- BUG: LOOPS under SYNC EXTERNAL: the master's START plays the loops from their start, armed loops and RECORD LIVE
  armed starting with it without a count-in (the master counts); STOP pauses them where they are; CONTINUE resumes the
  loops, not a song played meanwhile, from where they paused or from a song position given while they stood; a song
  position while they play jumps there.
- BUG: LOOPS' pattern recorder: a note heard on its own (K3, OCT, SEL + a key, a step switched on) sounds as the preview
  does, through the stage's mix (the selected loop's; on `+ NEW LOOP` the next loop's), not through the keys'.
- BUG: RECORD LIVE: SAVE NOW during a song could lose it with no word, the last message reading `SAVED`, and REVERT ALL
  could leave a stray song. The song now ends first.
- BUG: LOOPS: DELETE LOOP and CLEAR LOOPS no longer stretch the row playing, which put a short delay on every note a song
  recorded after them.
- CHANGE: LOOPS' own FX setup starts at ROUTING SENDS, so each loop's send knobs act from the start; a saved one keeps its
  routing.
- CHANGE: LOOPS: the stage switched off is drawn much fainter, all of it: its frame, its title and its notes.
- CHANGE: The cartridge lists (PRESETS', LOOPS' ENV, INSTRUMENTS'), DJ's overview and song list, and LOOPS' strips work
  out less for each frame, leaving the sound more time; nothing on the screen changes.
- CHANGE: LOOPS: a new loop takes FX MIXER's KEYS column's sends (chorus, delay, reverb). The stage on `+ NEW LOOP`
  already sounds with them, REC writes them into the new loop, and a send's knob (K1-K4) on `+ NEW LOOP` turns the KEYS
  column's, a setting SAVE keeps. Its level, pan and duck stay the next loop's own; REC into a loop keeps that loop's mix.
- CHANGE: LOOPS: a key held over the length's end stops writing there. Its note is kept once, from where it was played
  to the end, and plays again there each round; held on, the key writes nothing more and its release does nothing, so a
  pad held to the end keeps its start.
- CHANGE: LOOPS: MONO LEGATO is JUMP in a new LOOPS setup; a saved one keeps its own.
- FEATURE: LOOPS: each loop's slot shows its LEVEL at the right, in hex as K1 and the MIXER show it (`7F`); `+ NEW LOOP`
  the next loop's.
- CHANGE: LOOPS: REC with the stage off switches it on and records in one tap. With the loops stopped it arms them as
  well, whatever the stage holds (`STAGE ON: ARMED`: the first key or PLAY starts them); with them playing, the keys
  write from there (`STAGE ON`).
- CHANGE: LOOPS: RECORD PATTERN opens clean every time, after REC or HOME alike: no steps, the first row on C, 16 steps,
  NOTE LENGTH 1/16. It used to keep them until power-off. The EDITOR's RECORD PATTERN keeps its settings as before.
- CHANGE: LOOPS: during RECORD LIVE the line under the bars says what HOME does: `SONG ARMED: A NOTE STARTS  HOME
  CANCELS` while armed, `COUNTING IN: HOME CANCELS` during its count-in, `RECORDING: HOME ENDS AND KEEPS IT` while it
  records.
- FEATURE: LOOPS: PLAY's lights are a visual metronome while the loops play: a quick flash (100 ms) on each beat, red
  on the bar's first, green on the others, dark between. They follow the METRONOME of LOOPS' effects (its TEMPO and BEATS A BAR) whether it
  is on or off; off, it now counts on unheard while something plays, and switched on clicks on from there.
- CHANGE: LOOPS: OVERDUB keeps what the stage had. A note takes a lane that is free (a long pad played over stays
  whole); with all four in use it takes the lane whose note ends soonest and cuts that note where it lands, and a note
  starting on that lane while the key is held moves on with it, put down where the key is let go. A held key used to
  remove every note starting under it, on all four lanes. OVERWRITE is as before.
- BUG: LOOPS: a TEMPO change with K4 (or anywhere outside GLO's properties) was heard only when the loops came round to
  their start; it is now heard at once.
- CHANGE: PRESETS' instrument list shows in big letters (twice the size), six instruments at a time; SEL + PRESETS'
  cartridge list too, seven at a time, with each cartridge's type and no slots count.
- BUG: FX: turning DRIVE's knobs made the screen lag, as its HARMONICS picture was worked out anew at every click. The
  picture shows what it showed, at a fraction of the work.

- BUG: The EDITOR's WRITE refused a panel that fits the FM-1 when what the FM-1 already held (records that stay,
  blocks still taken by deleted records) left no room around it. It now writes the whole user area anew in that
  case, as the install page's REPLACE ALL does, and says so before writing; the FM-1 then holds exactly the panel.
- BUG: The EDITOR page kept a stereo sound (a dropped file, a take from a stereo input) stereo as the sample's
  original, drew its two channels in the SAMPLE tab and offered CHANNELS, though PT/FM's samples are one channel. A
  stereo sound is now mixed to one channel as it comes, so every sample on the TABLE goes to the FM-1 as it is; the
  SAMPLE tab draws the sample as it is, and CHANNELS is gone. Originals kept stereo before are made one channel at the
  next load, as their samples had them.
- BUG: The site's pixel fonts (PTFM Pixel, PTFM Pixel Prop) drew each letter as far left as the blank columns at its
  left: in the screen cut an I stood 2 pixels left, so words read "MI DI" or "TI ME", and the landing's logo and
  figures sat a pixel or two off. Each letter now sits in its cell as Terminus draws it.
- FEATURE: The site's landing shows the second video, PT/FM: UNDER THE HOOD: a small card under the first video, its
  thumbnail, length and what it shows; pressed, it plays there at the first video's size.
- REMOVED: The debug build's bench (the AUTOTEST's CHIP scenario: the chip's and each engine's and effect's own
  costs) and its tests of the second core (RESTARTS' CORE 2 FAULT, CORE 2 MISREAD and CORE 2 STUCK, and the ONE
  CORE autotests). Their questions are answered; the debug build reports what the FM-1 does as it plays, and has
  room again for the features to come.
- BUG: After DJ's decks had played and stopped, FX opened outside DJ edited the current song's effects, while the
  keys played through DJ's. It now edits DJ's (its properties titled DJ), as it does for the loops, until something
  else plays or UNLOAD SONG.
- BUG: STATUS PERFORMANCE's EFFECTS always read 0.0 %. It now shows what the effects take, measured while the
  page is shown, on whichever core runs them (the emulator, whose clock stands still inside a half, reads 0.0 %).
- CHANGE: The CPU shares STATUS PERFORMANCE gives each engine's voice (DX7 3.9 %, JUNO 3.4 %, TB-303 3.3 %, SAMPLE
  1.1 %) and the FX screen gives each effect (GATE 1.1 % ... REVERB 8.3 %) are now the FM-1's own measurements; the
  playground's CPU figures follow them.
- CHANGE: The FX screen's footer measures what the picked effect costs as it works (the last quarter second, while
  the screen is open; `0.0%` while it is off) instead of showing a fixed figure; the effects are no longer timed
  while nobody looks. The emulator and the playground keep the FM-1's measured figures.
- FEATURE: The EDITOR page records: ● RECORD on the TABLE takes sound from any input the computer has (a microphone,
  a line in, the FM-1 over USB), with a level meter and the take's waveform drawn as it comes in, and saves each take
  as a sample (TAKE00, TAKE01 ...) at 16 bits, 44.1 kHz, closing once it is saved. A part of the take can be selected
  by a drag, or by TRIM SILENCE (its silence at both ends left out); only the part is played and saved, and WHOLE TAKE
  takes all of it again. A take, or a sound too big for the preview, plays as it is through the footer's player; over
  the take a line follows where it plays.
- CHANGE: The EDITOR page keeps each sound's original apart in the browser, written once, so a long recording is not
  written again at every change.
- FEATURE: The EDITOR page's TABLE keeps a sample longer than the FM-1 holds (about 23.8 s at 44.1 kHz) whole, its
  size `TOO LONG`: a long take, a long sound dropped (refused before), or a long piece cut from one. It plays through
  the footer's player and is cut in its tab into pieces that fit; it goes to the FM-1 once TRIM, REMOVE, a lower RATE
  or OPTIMIZE makes it fit, a move refusing it till then (`TOO LONG FOR THE FM-1: 31.2 S, 23.8 S AT MOST`).
- FEATURE: A sample's tab cuts: drag to select, then PLAY THE SELECTION, TRIM or SAVE AS A NEW SAMPLE; DETECT SLICES
  finds the hits in a recording (SENSITIVITY 1-10, markers moved, added and deleted by hand), and REMOVE SILENCE,
  REMOVE, SAVE AS SAMPLES and MAKE A KIT (a sampler and a FUSION cartridge playing a slice a key) work on them.
- FEATURE: The EDITOR page's cartridge tabs work on many instruments at once: every slot listed, Shift- and
  Ctrl-click picking, COPY, CUT, PASTE (into empty slots), REPLACE and DELETE (Ctrl+C, X, V, Shift+V, Delete);
  instruments dragged to another slot, MOVE TO and Alt+↑ / ↓, with every
  cell, loop, favourite and the current instrument following at SAVE.
- CHANGE: The manual's About this manual names the emulator among PT/FM's pages: PT/FM's own firmware in the
  browser, with sound, to try every screen before installing, upload or download a disk, and record what it plays.
- CHANGE: The firmware takes 31 KB less of its slot, room for the features to come; the sound and the screens are
  unchanged. The device's own screens draw with the app's fonts, the fixed texts are packed tighter, tables and code
  nothing uses are left out, and the seldom-run code is built for size. LOOPS' stage (below) has taken about 17 KB of
  that room, 8 KB of which smaller code has given back since: 23.0 KB are left, 20.2 KB in the debug build (after
  independent samples, below).

- FEATURE: LOOPS' stage. While the loops go round, the keys play onto the stage, one loop of the length that belongs to
  no loop, cycle after cycle: a note replaces what the stage had for as long as its key is held, so each pass fixes a
  part and the rest stays (OVERDUB). A note held over the length's end ends at the line, and the key still held plays
  on from the next round's first row. The stage is heard exactly as it is written, in the keys' sound, through the
  selected loop's mix (no mute or solo silences it), the selected loop staying silent meanwhile. It is never saved and
  counts as no change: SAVE and REVERT ALL leave it as it is, and a power-off loses it.
- FEATURE: OVERWRITE on LOOPS' stage: LFO's tap switches to it and back, as it is let go (`OVERWRITE`, `OVERDUB`): the play
  line erases what it passes and writes what is played, silence included. OVERDUB at every start; EDIT on a loop and the
  pattern recorder leave it in OVERDUB, so what they put on the stage stays.
- FEATURE: The stage's clear on LOOPS: EDIT held 2 s, EDIT on `+ NEW LOOP`, or SEL's CLEAR STAGE (`STAGE CLEARED`), each
  leaving the stage on.
- CHANGE: LOOPS' stage title always shows the mode, `STAGE OVERDUB` or `STAGE OVERWRITE` in red, with a dim `LFO` for
  what switches it; the instrument and MONO / POLY are the footer's. LFO is lit while OVERWRITE is on.
- CHANGE: REC on LOOPS leaves the stage off when it keeps it, and with the stage off REC switches it on, as ARP does:
  one press to keep a take, one to start the next.
- FEATURE: REC keeps LOOPS' stage the moment it sounds right: on `+ NEW LOOP` as a new loop (`STAGE AS LOOP 06`), on a
  loop into it, in place of its notes (`STAGE INTO LOOP 03`), in the keys' instrument and MONO / POLY; what sounds goes
  on as the loop's without a break. EDIT brings the selected loop back onto the stage as it is heard, to play over a
  bar of it and send it back with REC. Stopped with the stage empty, REC arms: the first note starts the loops (a chord
  played with it lands whole), PLAY after COUNT-IN. For half a second after REC does something it does nothing more,
  so a double tap does one thing.
- FEATURE: LOOPS' length is set from the start: 4 bars, changed any time in GLO, with loops or none (LENGTH longer at
  once; shorter planned while GLO is open and cut as it closes, asking first when notes would go). CLEAR LOOPS and
  DELETE LOOP keep it; a length that is not whole bars shows in beats (`21 BEATS`).
- FEATURE: ADJUST in LOOPS' SEL menu moves a whole loop round the length without moving a note (its SHIFT: K1 a row,
  K2 a bar, heard at once; SEL keeps it, HOME cancels). The strips, EDIT, RECORD LIVE, SAVE TO PATTERN, QUANTIZE and
  LENGTH all take a loop as it is heard.
- FEATURE: QUANTIZE (`2 ROWS`, `3 ROWS`, `4 ROWS`, `ROW`), DELETE LOOP and TAKE ITS INSTRUMENT in LOOPS' SEL menu:
  a loop's notes moved to the lines once it is kept, a loop taken out at once (REVERT the way back), and the selected
  loop's instrument and MONO / POLY made the keys', as ENV held does.
- FEATURE: LOOPS' K1-K4 each turn what GLO sets (`K1 TURNS` ... `K4 TURNS`: LEVEL, PAN, DUCK, CHORUS, DELAY or REVERB
  SEND, TEMPO or VIEW; at first LEVEL, PAN, REVERB SEND, TEMPO), and VIEW shows 1, 2 or 4 bars a screen.
- FEATURE: LOOPS' pattern recorder: NOTE LENGTH on K1 (`1/32` up to `1 BAR`, or `TO THE NEXT`) and ties (a step's key
  held: the note before goes on through it). Its REC puts the pattern onto the stage (`PATTERN ON THE STAGE`), the
  preview's notes going on as the stage's, and REC on the screen keeps it as a loop.
- FEATURE: LOOPS' screen shows four loops at a time, big enough to read, all sixteen in a column beside them, the
  stage below with where REC sends it, the length's bar, and a line saying what the keys and buttons do just then.
- CHANGE: PLAY on LOOPS plays the loops from the play position, with loops or none (the length goes round and the
  stage fills); pressed while they play, it stops them and goes back to the start.
- CHANGE: ALGORITHM on LOOPS moves the play position, a row a click (SEL held: a bar): stopped, where PLAY starts
  (`PLAY FROM 2.4.3`); playing, a jump there (`JUMP +1 ROW`). TEMPO is K4's at first and GLO's; the pattern recorder
  and RECORD LIVE's panel keep it on ALGORITHM.
- CHANGE: MONO / POLY on LOOPS is the keys' (SEQ held + MONO or POLY, or CONTROL mode's keys: `KEYS: MONO`), heard on
  the stage at once and written into the loop by REC; ENV held takes a loop's instrument and MONO / POLY for the keys,
  APPLY INSTRUMENT gives a loop both.
- CHANGE: LOOPS' help explains the stage, names ENV with its reading as every other screen's does, and gives ARP's
  reading once (again only where ARP holds something on).
- CHANGE: The state the FM-1 sends a page that asks for it (the controller's mirror, the debug page) ends with LOOPS'
  bytes: the notes on the stage, the selected loop's SHIFT, ADJUST open, OVERWRITE at work and on.
- CHANGE: The debug page's LOOPS autotests play the stage: notes onto it and REC, EDIT and back, DELETE LOOP, the
  pattern recorder onto the stage.
- FEATURE: The EDITOR page's LOOPS tab shows the loops' length and edits each loop's SHIFT, signed (`+3` plays 3 rows
  later, `-3` earlier); WRITE to an FM-1 that reads the older loops writes each loop out as it is heard, SHIFT
  included, and names what that FM-1 cannot keep (`NOT KEPT: VIEW, K1-K4'S JOBS, THE LENGTH`).
- CHANGE: The manual's LOOPS chapter follows the stage (overdub and OVERWRITE, REC and EDIT, the length, the keys'
  sound, ADJUST, the pattern recorder's ties and NOTE LENGTH), with pictures of a loop on the stage, OVERWRITE, ADJUST
  and a tie, and appendix B's LOOPS messages and questions anew; the EDITOR's RECORD PATTERN explains its own keys,
  and the EDITOR page's chapter the LOOPS tab's length and SHIFT.
- REMOVED: LOOPS' takes (armed for the loops' next start, one pass that ended by itself) and the first loop setting the
  length a bar at a time: the stage and the length set from the start do their work.
- REMOVED: LOOPS' re-record (EDIT) and peel (LFO): EDIT now brings a loop onto the stage, and DELETE LOOP takes one
  out.
- REMOVED: The pattern recorder's TIMES, STEP SIZE and `RING TO THE NEXT` / `A STEP LONG` (NOTE LENGTH and ties do
  their work), and its writing straight into a loop.
- REMOVED: LOOPS' `BETA` mark.

- FEATURE: Independent samples. A sample is now a thing of its own, numbered `00`-`FF` apart from the cartridges (256 on
  an FM-1), and a sampler cartridge holds only its instruments (up to 64), each naming any sample by its number: one
  sample can serve instruments in several samplers, and a sampler can be cloned, moved or deleted without touching
  any audio. The sound is unchanged: the demo disk converted plays bit for bit as before. An instrument naming a
  number with no sample (a partial upload, a save cut short) plays nothing and its page says `THE SAMPLE IS MISSING`.
- FEATURE: REPLACE SAMPLE in a sampler's SEL menu, on an instrument (browsing or editing): THE SAMPLES list opens on its
  sample, and the one picked is played by the instrument as by a new one, every value back to a new instrument's.
- FEATURE: CREATE on an empty sampler slot, in place of INIT, and EDIT there: pick a sample from THE SAMPLES list and a
  new instrument plays it whole, named after it. With no sample on the FM-1 these say `NO SAMPLES ON THE FM-1`.
- CHANGE: The samples list shows every sample on the FM-1, by number, whichever sampler you came from (`SAMPLES (23)`,
  the highlighted row's waveform): K1 on SAMPLE turns through all of them, ENV on it lists them, a line `ALL SAMPLES` ends
  the cartridge list to look, listen and delete without an instrument, and the SAMPLE page shows `SAMPLE 23`, its number
  and name.
- CHANGE: DELETE of a sample, from the list, warns when instruments of any sampler use it (`DELETE SAMPLE? 3
  INSTRUMENTS USE IT AND ARE DELETED TOO`) and takes them with it; it waits for SAVE like any change (the row reads
  `(DELETED)`), and REVERT brings it back. When storage is full the save writes the deletions first.
- CHANGE: INIT on a sampler instrument keeps its sample and sets the rest back to a new instrument's, named after the
  sample; over marked slots it leaves the empty ones empty.
- CHANGE: CLONE and MOVE of a sampler cartridge copy and move only its instruments: nothing is written to the flash
  until SAVE, as for every cartridge, and `CLONE FAILED: FLASH ERROR`, `SAVE IT FIRST` and `NO ROOM IN THE USER AREA`
  for samplers are gone.
- CHANGE: DELETE of a sampler cartridge takes its instruments and leaves its samples; CLEAN UP's UNUSED SAMPLES lists the
  samples no instrument of any sampler plays.
- CHANGE: CHECK REFERENCES lists a sampler instrument whose sample is gone (`SAMPLER 01:05: SAMPLE 23`) and each sampler
  still in the older form once (`CARTRIDGE 03: OLDER FORM`, not counted); CHECK STORAGE lists a sample whose description
  and audio disagree (`SAMPLE 23: ENTRY AND AUDIO DIFFER`), read as missing; STATUS counts the samples, and the older
  form's audio apart; the SAMPLE SOUNDS help page follows.
- CHANGE: The conversion. A sampler of an older PT/FM is converted to the new form (same numbers, same slots; the same
  sample used by two samplers becomes one, the same audio under two names stays two; a sampler whose samples would pass
  256 stays old as `OLDER FORM`, deleting it or other samples frees numbers). This PT/FM leaves an old sampler alone and
  plays nothing from it; the install page, the emulator and the EDITOR page convert it (below). **Going back to an older
  PT/FM needs a copy first:** it cannot read the new samples (it would leave the longer ones out as damaged), so READ,
  then DOWNLOAD DISK on the EDITOR page, install the older PT/FM, then WRITE, which converts down (`NOT KEPT` names what
  the older form cannot hold).
- FEATURE: INSTALL FIRMWARE converts an older PT/FM's samplers on the FM-1 by itself after the install: whole samplers a
  save, in number order, into free room only, everything else kept; the status line says each step (`Converting the
  samplers to the new form: 2 of 5. Keep the FM-1 connected.`) and Done waits for the end. One cut short goes on at the
  next INSTALL FIRMWARE, nothing lost; a sampler too big for the free room stops it, saying how much room it needs and
  two ways out. CLEAN INSTALL skips it.
- CHANGE: UPLOAD CUSTOM DISK (install page, emulator) converts a disk an older PT/FM made as it loads, and every upload
  refuses what would not fit in the FM-1's list of records (560 at most). An upload names a sample whose description
  and audio disagree (the FM-1 plays nothing from it); the EDITOR page shows one dim, `SAMPLE · ENTRY AND AUDIO
  DIFFER`, its instruments naming nothing. The conversion's last line warns of low room only when the run itself
  leaves the FM-1 short of it, and when the FM-1 would have too little left even to save a delete it names only the
  EDITOR page's READ, then WRITE.
- FEATURE: The emulator converts the flash it keeps at its first load after this version, saying so in its status line.
- BUG: The FM-1 counted what a deleted sample leaves in its freed blocks as damaged records once a save wrote into part
  of them (`DATA RECEIVED, 2 BAD RECORDS` after an upload or an EDITOR WRITE, the install page's "damaged records" line,
  the EDITOR's `N BAD RECORDS: WRITE AGAIN`), though CHECK STORAGE named none and nothing was lost. It now counts only what
  CHECK STORAGE names.
- BUG: A sample's DELETE in edit mode was undone by HOME along with the mode's other changes. DELETE now leaves edit mode
  keeping the changes first, as EDIT does, then deletes.
- FEATURE: The EDITOR page knows samples as items of their own on both panels (the FM-1 panel numbers them), with the
  tabs to match: a sampler's tab points an instrument at any sample (SAMPLE), REPLACE SAMPLE, CREATE, DELETE (the
  instrument goes, its sample stays), THE SAMPLES IT USES with NEW INSTRUMENT FOR IT, PASTE bringing an instrument's
  sample along (at a free number; `NO FREE SAMPLE NUMBER` when none is); a sample's tab lists PLAYED BY across samplers and
  its instruments follow a cut on SAVE; sounds dropped on a sampler cartridge come with an instrument each.
- CHANGE: The EDITOR page converts the old form as it loads anything (READ, a disk, a backup, what the TABLE keeps), marks
  what the conversion cannot take `OLDER FORM`, says `NOT ENOUGH SAMPLE NUMBERS: 70 NEEDED, 12 FREE` for a move that
  needs more numbers than are free, and its first WRITE after a conversion rewrites every sample as a record of its
  own, the whole user area anew only when they do not fit beside the old records, with its BACKUP of the old records
  first. Tracker modules come in as samples of their own and samplers of 64 instruments (samples past the 256th
  are left out, the page saying how many); MAKE A KIT and SAVE AS SAMPLES take free sample numbers.
- CHANGE: The manual follows independent samples: the basics, the sampler chapter with REPLACE SAMPLE, CREATE and THE
  SAMPLES list (with a picture), the install page's conversion, going back to an older PT/FM, the EDITOR page's chapter,
  the glossary, the specifications, appendices B and C.
- CHANGE: While LOOPS' loops are what plays (or what played last), PLAYBACK shows only that they play (`THE LOOPS PLAY:
  LOOPS SHOWS THEM`, paused: `THE LOOPS ARE PAUSED: PLAY GOES ON`), as if nothing were loaded; its knobs, LFO and EDIT no
  longer change the song nobody hears, and GLO has no UNLOAD SONG for the loops (SONGS' menu still has it).
- FEATURE: The EDITOR page's TABLE saves what is picked there as a disk: DOWNLOAD AS DISK in its selection bar, the
  items with everything they use, numbered as they would land on an empty FM-1, the built-ins left out; the file named
  after the item (`punch-in.ptfmdsk`), or `ptfm-selection-` and the day for several. What would not fit an FM-1 is
  refused (`NO DISK SAVED`).
- FEATURE: A built-in cartridge moved from the EDITOR page's FM-1 panel to the TABLE (the arrow or a drag) becomes a copy
  of its own there, its changes included, to edit as any cartridge; moved back, it takes a free number beside the
  built-in. A song moved to the TABLE keeps playing the built-ins it uses, as before.
- CHANGE: ARP's reading in every screen's help and in the manual is AUTOMATIC REPEAT PROCEDURE (it was AGAIN, REPEAT,
  PLEASE).
- FEATURE: LOOPS' stage switched off and on with ARP's tap (`STAGE OFF`, `STAGE ON`), to play the keys over the loops
  without keeping them: off, the stage keeps its notes, drawn dimmed and not heard, the selected loop is heard in its
  place, and the keys sound at once in the stage's sound (POLY every key up to 16 at once, the 17th taking a lane in turn, MONO one note as MONO LEGATO says), written
  nowhere; REC turns it on, sending and arming nothing, LFO does nothing, EDIT and the pattern
  recorder's REC fill the stage and switch it on; the stage's title reads `STAGE OFF`, the hint line `STAGE OFF: THE
  KEYS ARE NOT KEPT`, ARP dim (lit with the stage on). On at every power-on.
- CHANGE: LOOPS' pattern recorder opens with ARP held + REC (at REC's release; ARP held alone shows `REC: RECORD
  PATTERN`) or the SEL menu's new RECORD PATTERN; ARP's tap now waits COMBO GRACE before it acts.
- CHANGE: RECORD LIVE records everything that sounds: the loops as heard, the stage, the keys, the pattern recorder's
  preview. LOOPS works as usual during a song (EDIT, the pattern recorder, ADJUST, QUANTIZE, DOUBLE, LENGTH, GRID and the
  rest, no more `RECORDING A SONG`), REC keeps its jobs, and HOME ends the song, at the row it was pressed in. Only notes
  that have a voice are written.
- FEATURE: The EDITOR page's FM-1 panel can rearrange cartridges: SET NUMBER… (F4, or the right-click menu) gives one
  another number, `00`-`EF`. A free number moves it, a taken one swaps the two, and everything that names it follows.
  One step of UNDO, reaching the FM-1 with WRITE.
- CHANGE: The demo disk's numbers start at `A0` in every kind (cartridges `A0`-`A5`, songs `A0`-`B1`, samples `A0`-`C0`,
  patterns `A0`-`DF`), leaving `00` and up for what you make or add, and `E0`-`EF` free. The music is the same. An FM-1
  holding the old disk goes on working; UPLOAD DEMO DISK puts the new one in place of everything in the user area.
- CHANGE: Everything made takes the smallest free number: NEW and CLONE of a song, which no longer shift the others, and
  NEW and CLONE of a cartridge, which no longer go after the last. DELETE of a song frees its number, the other songs
  keep theirs, and it stops only that song if it plays.
- BUG: SONGS' list showed songs that REVERT ALL had just taken away, until you left the screen.

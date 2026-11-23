# Changelog

What changes in PT/FM from one version to the next: the firmware, its pages (install, emulator, editor, manual,
site) and the demo disk. Between releases the top section, **Next version**, collects the changes made since the last
release; at a release it takes that version's number and date, and the first change after it starts a new Next version
above it. A version is the firmware's: a change that leaves the firmware as it is (a page, the site, the manual) goes
into the current version's section while no new firmware is on its way, and into Next version with it once one is.
Earlier changes are in the git history.

Each change is one entry, its kind first:

- **FEATURE:** something new;
- **CHANGE:** something that works differently;
- **BUG:** something fixed;
- **REMOVED:** something taken out.

## 1.0 (2026-11-23)

- FEATURE: The keys play as hard as you press them. The FM-1 reads how deep each key from F3 to C5 sits as it lands
  (the carbon pill under it conducts more the harder it is pressed) and plays the note at that velocity, as a MIDI
  note's: the instruments, the EDITOR's takes, LOOPS, the arpeggiator, MIDI OUT and MIDI CONTROLLER all take it. SETTINGS'
  KEYS VELOCITY picks the curve, SOFT, NORMAL (at first) or HARD, or CONSTANT: every key at KEYS VOLUME, as before. The
  top seven keys, C#5 to G5, cannot be read and play at the mean of the last four that were; a note sounds a
  millisecond or two after its key goes down, as its press is read. The virtual FM-1's keys play harder the lower they
  are pressed. A held key pressed harder makes its own notes louder, toward the loudest, and softer as it eases (on
  LOOPS through the stage's lanes too); an EDITOR take and LOOPS' stage record it as volumes on the rows it changes on
  (on the stage a note as loud as it was heard by its own row, a light landing pressed on at once included; in MONO the
  key whose note sounds), and play it back smoothly (a take with volume slides, the stage gliding, a MONO stage and the
  loop REC makes of it too). The debug build shows each press's readings on STATUS DEVICE and in its figures (each key at rest, the
  last presses, so a RUN played soft and hard measures an FM-1's own keys), and its AUTOTEST's KEYS: VELOCITY feeds
  presses to the FM-1's own velocity (KEYS FEED), checking each note's volume and MIDI OUT's, the keys at rest and the
  scan's cost.
- FEATURE: MIDI CONTROLLER's GENERAL mode sends aftertouch: a held key (F3 to C5) pressed harder than it rests sends
  Channel Pressure, or each note's Polyphonic Key Pressure (SETTINGS' MIDI page, CONTROLLER AFTERTOUCH: CHANNEL at first,
  POLY, OFF); none with KEYS VELOCITY CONSTANT. On the virtual FM-1, a held key dragged down.
- FEATURE: AUTOMATIONS (HOME + OP6, last in HOME's round): 16 slots that record what you do on the FM-1, every key,
  button and knob turn at its time (MIDI's learned controls as the controls they stand for; never MASTER), and play it
  back. REC records from the screen you came from, starting at your first press; HOME + OP6 ends it, neither recorded.
  PLAY goes to the recorded screen and plays it there once, in whatever state the FM-1 is in, a fast knob turn in the
  steps it took when recorded; any key, button or knob of yours stops it, and MASTER stays yours. SELECT, ALGORITHM
  or a white key picks a slot; SEL: RECORD, PLAY, COMPACT, CLEAR, REVERT. COMPACT shortens the slot's pauses while
  nothing of it is held to 1, 0.5 or 0.3 s, so work on the screens plays back as fast as it can while held keys and
  knob turns keep their time. From any screen HOME, ARP, then a white key plays that key's slot; with REC before
  the white key it records into it there, and HOME, ARP, REC ends a recording where you are. While the FM-1 takes
  no input (a save's progress) a playback's time stands still. SAVE keeps them, in a compact file of their own (about
  1 KB a minute of busy playing); REVERT ALL, or a page writing to the FM-1, ends a recording or a playback. The
  playground does the same, taking the controls a pass at a time as the firmware does.
- FEATURE: The EDITOR page reads, edits and writes AUTOMATIONS: a system item, one on the FM-1 panel (DEFAULT while
  the FM-1 holds none, and again once every slot is emptied) and any number on the TABLE (READ, WRITE, + NEW, the disks
  and BACKUP carry it). Its tab lists the 16 slots and shows the picked one's events as a table in words (`KEY C-4
  DOWN`, `SEL UP`, `K1 +2`): each event's time, gap, control and clicks edited, rows inserted and deleted, the
  slot's screen changed, the rows the FM-1 skips or cancels in one pass grey, saying why; marked slots copied and pasted
  into another file's AUTOMATIONS, asking before replacing slots that hold one.

- FEATURE: LOOPS sets: SEL's STORE AS A SET, first of the menu's saves and loads, keeps all of LOOPS (every loop with
  its notes, sound, mix, mute and solo, the length, the properties and LOOPS' own effects) under a number and a name of
  its own, and LOAD A SET brings a set back, the loops stopped at their start: many jams on one FM-1. In the sets'
  list EDIT renames a set and LFO deletes it; SAVE keeps them as any change. Moving, swapping or deleting an instrument
  or a cartridge now counts the loops that play it, LOOPS' and the sets', among its uses, and YES makes them follow;
  CLEAN UP keeps an instrument only a loop plays. The EDITOR page reads, writes, moves (with the cartridges their loops
  play), downloads and plays sets, in a group of their own, LOOPS SETS. The playground does the same.
- FEATURE: LOOPS can cut a loop: SEL's CUT HERE (or SEL held + EDIT) gives the selected loop a window from the loops'
  start to the current position, and it wraps there while the length stays, running on across the length's end and
  drifting against the other loops (loops of different lengths, phasing). SHIFT turns the loop's notes under the
  window; REMOVE CUT (or SEL held + EDIT held) brings the whole loop back. The stage over a cut loop wraps there too.
  The strip shows the whole loop with the notes past the mark dimmed, paged by where the cut loop plays so that place
  is always on screen (the stage's over it too); SAVE TO PATTERN writes the first pass. An older
  PT/FM plays a cut loop uncut. The EDITOR page and the playground do the same.
- FEATURE: LOOPS: putting notes in and erasing by hand while stopped. Stopped with the stage on, the keys write at the
  play position as they would were the loops playing, the loops and the play position standing still: a chord while
  any key of it is held, each note as long as its key is held at TEMPO, drawn as it grows, OVERDUB and OVERWRITE as
  ever; POLY four keys, MONO the first; the next insert at that row replaces it. PLAY with its keys held plays from
  there, their notes growing on as long as the keys are held, each heard to its end in the first pass too; ALGORITHM
  turned with them held ends their notes where they stand. LFO held + ALGORITHM erases the notes starting on each row
  the play position moves across (ERASING). LFO used so switches nothing.
- FEATURE: LOOPS: MOVE (SEL's menu) moves the selected loop up and down the list, SELECT a place a click, SEL keeping
  it, HOME putting it back; SEL held + SELECT does the same a click at a time. A loop moves with its notes, sound,
  colour, mix, mutes and solo. SEL's layer on LOOPS names both SEL + SELECT and SEL + ALGORITHM.
- CHANGE: The DX7's voices answer velocity in their sound: a note's volume at its start (a cell's, a key's, a MIDI
  note's) is its velocity for the operators that modulate, so harder is brighter, as each operator's KEY VEL SENS sets;
  the operators heard stay at velocity 100, so the loudness is the volume's, as before. At volume 100 (`64`), or with
  no KEY VEL SENS, a voice sounds as before.
- CHANGE: LOOPS: the keys on the stage sound the moment they are pressed, with every QUANTIZE, and end when let go;
  the stage still keeps each note on QUANTIZE's line and plays it there from the next pass. A key played early used to
  wait for its line. POLY still plays 4 keys at most, MONO one, its MONO LEGATO now heard at the press. What the stage
  keeps and plays is as before, and a song recording writes the keys as the stage keeps them.
- CHANGE: The two cores share the voices by the work each block: the second core renders as many of the notes
  sounding as evens out the two, at any VOICES while it runs (it rendered only notes past the tenth at VOICES 16, none
  at 8 and 12). The same sound, with a full passage's work no longer mostly the first core's.
- CHANGE: The EDITOR's realtime takes: a key sounds the moment it is pressed, with every QUANTIZE, and ends when let
  go; its note is written on QUANTIZE's grid as before and plays there from the next pass. A note played early used to
  wait for its line.
- CHANGE: LOOPS: SEL held + ALGORITHM turns the selected loop's SHIFT a row a click (as ADJUST's K1), no longer the
  play position a bar.
- CHANGE: LOOPS: a new loop starts at LEVEL 40 with every send full, from defaults in GLO's properties (NEW LOOP LEVEL,
  NEW LOOP CHORUS, DELAY and REVERB, saved with LOOPS; a properties list too long for the screen scrolls). FX
  MIXER's last column on LOOPS is NEW in place of KEYS: the next loop's own mix, turned there or by K1-K4 on + NEW LOOP
  for that one loop (not saved), the defaults again after REC makes it; the KEYS column's sends no longer go to a new
  loop. The keys on LOOPS sound through the selected loop's mix (on + NEW LOOP NEW's), played freely while stopped too,
  never through KEYS GAIN or DRY.
- CHANGE: A SAVE that does not fit at once is saved in parts: as many whole things as fit, each part a save of its own,
  the next once compaction has freed what the part replaced. A SAVE of changes alone (AUTO LEVEL ALL, an instrument's
  edits) now asks NOT ENOUGH STORAGE TO SAVE only when one thing is bigger than all the free room, and its figure is
  what that thing lacks. A deletion something new could be made from (a MOVE's) waits until the new thing is saved; a
  power cut between parts leaves the parts before it saved. One progress bar across the parts.
- CHANGE: LOAD FROM PATTERN (and LOAD A SET) checks the edit memory before the loops stop: refused, LOOPS stays as it
  was and plays on. A REVERT that undoes a load leaves no solos.
- BUG: LOOPS: a key's note no longer sounds on forever when ALGORITHM jumps over the row where it ends (a key let go or
  held as the loops jump); it ends where they land.
- BUG: LOOPS: the note keys lit only while stopped. They now light with their notes as on every screen, the stage on or
  off: at the press, then by the note's loudness; in MONO a glide or a legato moves the light to the new key. A FUSION
  key lights with its layers (it lit the keys of the notes its layers play).
- BUG: LOOPS: a FUSION instrument on a loop of several lanes started its later layers, and slid their volumes, at the
  volume of whichever of the loop's notes came last; each note now keeps its own lane's volume.
- BUG: LOOPS: a MONO stage played a note held over the line on past it in the next pass (its end at row 0 was not
  counted); it ends there now, as it was heard and as REC writes it. The playground does the same.
- BUG: DX7: a released note on an algorithm where an operator adds into a modulators' bus (7-18, 20, 26, 27) counted
  that operator as a carrier, so its voice stayed busy, silent, until that modulator's envelope ended; it ends with its
  carriers now. The demo songs sound the same, bit for bit, at VOICES 8, 12 and 16.
- BUG: DX7: a stored patch whose pitch envelope holds a rate or level past 99 (an imported dump's, say) read past the
  engine's tables; it reads them at 99 now. Other values past 99 still play as stored.
- BUG: LOOPS: a strip of 2, 3 or 4 lanes left up to 3 empty pixels under its last lane, like a fifth row. Its lanes now
  sit in the middle, a pixel apart, as high as they fit, and the strips grew for them: a loop's from 14 to 17 px (its
  notes 8, 5 or 3 px high at 2, 3 or 4 lanes, were 6, 4 and 2), the stage's from 18 to 19 px (9, 5 and 4, were 8, 5
  and 3). The playground the same.
- BUG: VISUAL LEVELS showed nothing while LOOPS played. Its meters are now the loops, a loop's number dark grey while it
  is muted or silenced by a solo; the stage's notes show in the meter of the loop they sound as.
- FEATURE: The EDITOR page's LOOPS tab edits more of LOOPS: each loop's mute and mix (LEVEL, PAN, DUCK and the three
  sends), its place in the list and its deletion, a new loop's defaults, and the setup (QUANTIZE, MONO LEGATO, COUNT-IN,
  the keys' POLY or MONO, VIEW and K1-K4's jobs beside TEMPO, TICKS A ROW and SWING). A LOOPS set now opens in the same
  tab, with each loop's solo. The notes, the length and a cut stay the FM-1's.
- CHANGE: The EDITOR page plays LOOPS and a LOOPS set on round their length until STOP, as LOOPS does, with no break
  between passes. It used to rest a second after each pass and start the loops again.
- CHANGE: The EDITOR page's LOOPS tab, while its loops play: every change is heard at once with the loops playing on, as
  an edit on LOOPS (MUTE and SOLO, the mixer, TEMPO and the setup, the FX setup, an instrument, SHIFT, a loop moved or
  deleted); each used to start them again. A LOOPS set's preview hears its solos. THE NOTES runs across, the rows left
  to right and each lane three lines (notes, volumes, commands), where a loop of 128 rows was a tall list.
- CHANGE: The EDITOR page is out of beta: its `BETA` mark is gone, from the page and the landing. What a page kept in
  the browser stays.

- FEATURE: Ghost notes: a note pushed out of the voices is no longer lost. A held note taken for a new one, past a
  smaller VOICES, or when the second core stops, goes on unheard as a ghost, up to 24 at once (4 or 8 waited before),
  still following everything played on it (its note off, slides, legato, bends, cuts, FILTER's envelope), and comes
  back where it would be by then as soon as a voice frees, fading in; let go meanwhile, it comes back in its release.
  A held ghost takes a ringing tail's voice to come back, so on LOOPS a pad pushed out in a busy bar is back at the
  next bar line. VOICES caps the notes heard; up to 40 notes are kept (16 sounding, 24 ghosts). A muted loop or track
  takes no voices: its notes keep time as ghosts and come back when it is heard again, so the voices go to what is
  heard (DUCK's source's and a TB-303's muted notes keep theirs: DUCK still ducks while its source is muted). The
  playground plays the same: there too a muted track's notes wait unheard, and a track's last note keeps following its
  slides and its volume after its note off.
- CHANGE: Ghost notes wait to come back while the first core is near its time (the load guard): in a half whose
  counted work (the first core's sounding voices, the notes started and fades begun, DJ) is over the guard's cap, no
  return begins or goes on, the next block tries again. Counted, never measured, so the FM-1, the emulator and the
  playground decide alike. No demo song or DJ loop reaches it; DJ HEAVY's busiest halves, where a row starts many notes
  over a full pool and the ghosts made then took the tails just let go, are back near 0.27's. The debug build's
  ghosts' figures count the walks it held.
- CHANGE: VISUAL: a muted track's notes have no VOICES tile and its LEVELS meter stays empty, as they take no voice
  (DUCK's source's and a TB-303's sound on muted, their levels grey).
- CHANGE: A save is made in silence: at SAVE all sound stops as CUT stops it (ringing notes, the effects' tails), and
  comes back once the save is over. Nothing of the sound or the second core then runs between the save's flash steps.
- BUG: A TUNE command on a sample instrument with a TUNE of its own (the demo disk's toms and congas have one) added
  the two; it now sets the note's TUNE, as every parameter command sets its value. The playground already set it, and
  now takes TUNE in the FM-1's steps of 1/64 semitone.
- BUG: A held sample note looping FORWARD whose LOOP END a command turned under LOOP START ran its place out of the
  sample and ended (on the FM-1; undefined on a computer). Its loop now stops there, as with LOOP OFF, and it plays on
  to END. A note started on a loop with no room does not loop in the playground either, as on the FM-1.
- BUG: A MIDI note played again at once was lost: a held note's off and its next on coming together (a sequencer's
  repeated notes, back to back), or an on again without its off, left the key held, so the repeat played nothing and
  was written nowhere (the EDITOR's REC, LOOPS' stage). The key is now let go, then pressed again a moment later; the
  same for the learned controls and for the panel from a page (MIRROR), whose quick double taps were lost too.

- CHANGE: The red PT/FM CRASH screen names who made a bad access: the debug unit's culprit registers in two rows
  (`WR` the write limits hit by core 1, core 2 and the DMAs, `DMA` the one that hit one; `AXI W` / `R` the bus master
  of an invalid write or read; `MMU W` / `R`), `CORE 2 RUNNING AT` where the second core was when the crash
  paused it, and the second core's own fault: `CORE 2 FAULT AT` its PC, R its return address (it had shown where the
  stopped core stood).
- FEATURE: CORE 2 FAULTED at a start: a fault of the second core that no screen showed (it survives restarts and
  installs until the power goes off) is shown once, its words and when it came.
- CHANGE: The second core's fills and copies write only into the RAM: one aimed outside it (the crashes' invalid
  write) is refused, the second core stops and the first goes on alone, STATUS' DEVICE naming the destination and the
  caller, CORE 2 FAULTED after a restart too.
- FEATURE: PT/FM FROZE. After a freeze (PT/FM stuck until the FM-1's watchdog restarts it), the next start shows where
  it stood: the main loop (the store, drawing, a flash erase and its address ...) and when its last pass came; the
  audio interrupt when it never finished (waiting for the second core, a flash read and its address, or where in
  making the sound: the notes, the ghosts coming back, a voice and its engine, the part of a Juno-106 voice's render,
  the effects, the output); whether the sound and the input went on after it; the second core's work handed over and
  done, and its stop earlier in that run (why, when, where it stood); a crash that began in that run but never reached
  its red screen; the core supplies as the run left them. A button goes on. An install's own restarts never show it.
- FEATURE: The chip's core supplies are raised at every start, once the settings are read, to the levels the FM-1's
  stock firmware sets (1.60 V, 1.38 V), from the boot loader's (1.40 V, 1.26 V), at which some FM-1s misread their
  memory under load. SETTINGS' CORE SUPPLIES chooses: STOCK (the default) or LOWER, which leaves them at the boot
  loader's levels from the next power-on.

- CHANGE: Bigger letters where lists are read: menus, properties (GLO), SETTINGS and its MIDI page, the song list and
  the cartridge list are drawn in the header's 8 × 16 letters instead of the small 6 × 12, rows of 16 pixels. Menus
  span the screen's width, 11 items in view; properties show 11 lines, SETTINGS 12; the song and cartridge lists 10,
  their names big and their figures small beside them on the same baseline (a song's length past 99:59 in hours and
  minutes, `1H41`). A row too long for one line takes two, its value under its name (FX's KEYS, EVERYWHERE; a MIDI
  mapping, its MIDI control and kind under the FM-1's control), and the row selected always shows whole; the MIDI page
  with nothing learned says `NO MAPPINGS: EDIT ON LEARN`. A held button's layer is big when every line fits (SAVE's,
  GLO's, LFO's help line, SEL's on most screens, LOOPS' ARP); the wide tables stay small (the pattern list, HOME's and
  FX's layers, CHECK REFERENCES, CLEAN UP). The playground draws the same.
- BUG: A held button's layer cut its keys to 10 characters: SEL held on the EDITOR's grid and on INSTRUMENTS while
  browsing read `SELECT, AL: MARK ...`, and in a name being typed `SELECT IN : NEXT GROUP`. Every key reads in full
  (SELECT, ALGORITHM; SELECT IN A NAME).
- BUG: SEL held past its layer, then PRESETS: SEL's layer stayed over the cartridge list it opened. It now gives way
  while the list shows, as GLO's does over its favourites.
- CHANGE: STATUS PERFORMANCE: the CPU graph's last 30 seconds fill the whole box, 4 pixels a sample (a 2-pixel bar),
  where they filled its right half.
- CHANGE: One FAVORITES on the FM-1, the built-in FB. NEW CARTRIDGE and the cartridge list's NEW make no FAVORITES
  cartridge and no FAVORITES cartridge has CLONE; ADD TO FAVORITES always adds into FB (no longer a choice of
  cartridges), and on a sound FB holds already (with slots marked: every one of them) it reads REMOVE FROM FAVORITES and
  takes it out of every slot of FB naming it (ALREADY IN FB:07 is gone); SEL + GLO and GLO + PRESETS use FB alone;
  SEL + PRESETS (and MIDI's bank select) in a favourite's edit does nothing. The EDITOR's TABLE keeps any number of
  FAVORITES: one moved onto the FM-1 panel replaces FB's favourites (asking first while FB holds some), of several moved
  at once the bottom-most, and DOWNLOAD AS DISK puts one on the disk as FB, as that move does; UPLOAD DISK puts a disk
  back as it is. The playground does the same.
- FEATURE: DELETE CARTRIDGE in INSTRUMENTS' slot menu: the cartridge you are in deleted, as the cartridge list's DELETE
  deletes it (asking about its uses when songs use it); not on a built-in cartridge.
- BUG: A SAVE that took the FM-1 past the 560 records it lists (each song, sample and cartridge one, a pattern or a
  LOOPS set one a part) lost the newest things at once (a new set gone from the list right after SAVE), and a later
  power-on could drop others. Such a save is now refused before anything is written, the changes kept:
  `TOO MANY RECORDS TO SAVE`, how many more, `DELETE SOMETHING, THEN SAVE`, OCT+ running CLEAN UP; a deletion
  makes room once it is saved. The EDITOR page's WRITE, refusing a panel past the list, gives the panel's own count.
  The playground does the same.
- BUG: A MOVE, SWAP, a cartridge's MOVE or a DELETE answered YES with the edit memory nearly full could make part of
  the change (the slots moved, some patterns or favourites following, the loops not) and save it. The room for the
  whole change is now made first: refused, nothing changes and nothing is saved (`MEMORY FOR EDITS IS FULL`). The
  playground already refused it whole.
- BUG: CHECK REFERENCES never looked at the loops: it now lists LOOPS' loops and each LOOPS set's whose instrument is
  gone (`LOOP 03: 0B:05`, `SET 02 LOOP 03: 0B:05`), after the other missing references and counted with them. The
  playground does the same.
- BUG: SETTINGS' TOOLS: AUTO LEVEL ALL measured a built-in cartridge once it had been changed and saved, which could
  take more of the edit memory than SAVE had room for; it measures the user area's sounds alone now (a built-in
  cartridge, changed or not, is measured from its own cartridge's menu), and its question counts them alone.

- FEATURE: The debug build: the ghosts' figures (made, back, ended and dropped by kind, each engine's forward work
  and its cost, the budget's use, the waits, the oldest note brought back; the GHOSTS stage under VOICES) and the
  AUTOTEST's GHOST NOTES on LOOPS (20 held notes at VOICES 8 through mutes, a solo, VOICES changed, the keys over a
  song and a long note).
- BUG: The debug build's AUTOTEST: a LOOPS scenario run right after a DJ one (A BEAT without THE LOOP STATION before
  it) no longer fails: it read LOOPS' tempo from the FM-1 while DJ, played last, still held the keys' tempo. LOOPS'
  scenarios now let DJ go first (UNLOAD SONG).
- FEATURE: The debug build's figures say what a stack read as used whole holds at its bottom (the bytes written there,
  then the paint above them, and so how deep it went above them), on a line of their own: a word written at a
  stack's very bottom read as the whole stack used, and a stack not painted at the start the same.
- FEATURE: The debug build's AUTOTEST runs DJ HEAVY at VOICES 12 too (DJ HEAVY AT VOICES 12), beside VOICES 16: what
  the notes ghost notes bring back cost at each count.
- BUG: The debug build's AUTOTEST lost notes on LOOPS' stage now and then (KEYS: TB-303's 303 loops, GHOST NOTES' TB-303
  line): it took the loops' pass to start when it first saw the playhead back at the start, which can be most of a row
  late while the FM-1's main loop is slow, so every other chord landed on the row before it, replacing that row's. It
  now takes the earliest start its looks over half a pass allow, and taps on the rows with passes of 50 ms too.
- CHANGE: The debug build holds no help: every screen's help (HOME + LFO) is one page, `HELP IS STRIPPED IN THE DEBUG
  BUILD`. The help's pages, about 10 KB, are the room the debug build's figures need in the firmware slot, where the
  release keeps the help whole. The code that counts the ghosts' figures moved into the debug part, out of the slot,
  too.
- CHANGE: The firmware's texts are Huffman-coded on top of their tokens, about 1 KB less of the firmware slot, every
  screen the same; a text takes a few microseconds more to unpack.

- FEATURE: The EDITOR reads Juno-106 and Juno-60 banks as Juno-106 cartridges: `.syx` files of the Juno-106's patch
  messages, the Juno-106 Librarian's `.106` libraries, `.pat` patch lists, and recordings of both machines' cassette
  dumps, dropped as sounds (a sound that looks like one asks: CARTRIDGE or SAMPLE) or taken by the recorder, which
  reads a take as it comes and offers SAVE AS CARTRIDGE. A Juno-106's patches come in exactly; a Juno-60's are
  converted to the Juno-106 patches whose sound comes closest. Patches without names are PATCH00, PATCH01 ...
- CHANGE: The EDITOR page: WRITE no longer names an item as removed when it only loses records (a pattern's part, an
  AUTOMATIONS slot), and BACKUP takes an object whole as the FM-1 holds it, never old parts beside new ones.
- BUG: The EDITOR page: a cartridge tab's empty slots line up with the others in its list again.
- CHANGE: The EDITOR page's WRITE to an FM-1 whose PT/FM reads only the older LOOPS writes a cut loop as its first
  pass and names it under NOT KEPT (`A LOOP'S CUT (PLAYED AS ITS FIRST PASS)`).
- BUG: The playground's PLAYBACK: K4 TEMPO and K3 VOLUME moved a step a click however fast they were turned; a fast
  turn takes KNOB SPEED's bigger steps now, as on the FM-1.
- CHANGE: The manual follows 1.0: the AUTOMATIONS chapter (18), LOOPS' sets, CUT, MOVE and putting notes in and
  erasing by hand, ghost notes, PT/FM FROZE and CORE 2 FAULTED in troubleshooting, the bigger lists, one FAVORITES,
  CORE SUPPLIES, the 560 records, with pictures of a cut loop, MOVE, erasing by hand and VISUAL's LEVELS on LOOPS; the
  demo disk's numbers in the data appendix and in MIDI's bank select are put right.

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
- FEATURE: The site's landing shows the third video, WHAT'S NEW IN PT/FM 0.27: a card under the second video's, the
  same kind.

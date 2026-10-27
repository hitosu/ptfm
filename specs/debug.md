# The debug build

Status: **settled** (2026-10-12), with proposed details marked *(proposed)*.

The firmware as released, plus figures of what its time goes to, read over USB by a page of its own. It is
for measuring on the FM-1: what the sound costs by stage, what the screen costs it, how close the stacks
come to their ends. Nothing a user does differs from the release. The page also runs
[autotests](#autotests) (settled 2026-10-13: [proposals/202610131302-autotest.md](proposals/202610131302-autotest.md)),
on any PT/FM firmware.

## Made and installed

- **`fw/build.sh --debug`** makes `dist/debug/ptfm-debug.fwsc` and `dist/debug/install.html`
  ([layout.md](layout.md#the-built-files-dist-committed)). Its version is `fw/VERSION`'s number, its own
  build time (month, day, hour, minute) and ` DEBUG`, so each debug build is told apart.
- **Package identity `FM-1_7102`**, so the FM-1 says which one it runs; on USB it is "FM-1 PT/FM" as
  the release. Its update loader, rescue, store and seeding are the release's: the release's page installs
  over it and back, the library staying; the debug page's own install is clean at first ([the
  page](#the-page)) and leaves an empty user area (UPLOAD DEMO DISK then writes the demo disk).
- **What it holds more:** probes around each stage of the sound (two reads of the microsecond clock each,
  summed in RAM), the main loop's and the screen's times, the stacks painted at power-on to find how deep
  they went, the answer to the page, [the bench](#the-bench) and [the USB test tone](#the-usb-test-tone).
  Built with `PTFM_DEBUG` defined; the
  release compiles none of it. Its RAM: about a kilobyte more; it keeps 7 KB spare, not 8.

## The figures

The device keeps **a window**: what happened since the page last asked (about a second). Each answer
carries the window and starts a new one, so nothing grows on the device; the page gathers the windows.

**The stages of the sound,** in the order an audio half runs them (each summed over the window, and its
longest in one half):

| # | Stage | What |
|---|---|---|
| 0 | START | the half's start: DJ's mix taken, the main loop's requests, a chase's share |
| 1 | CONTROL | each block's control: the mix's buffers cleared, the keys' arpeggio, DJ's control |
| 2 | DJ DECKS | DJ's part of it for the decks: their stops, KEY, armed starts, SYNC (inside CONTROL) |
| 3 | DJ LFOS | DJ's LFOs: the crossfader's, the DJ FILTERs' and the FX LFO, the filters as heard (inside CONTROL) |
| 4 | REPLAY | the replays' ticks: rows, commands, the tracks' levels |
| 5 | CELLS | the replays reading each row's cells and their instruments (inside REPLAY) |
| 6 | NOTES | the rows' notes started: the instrument, a voice, the engine's note-on (inside REPLAY) |
| 7 | VOICES | every voice rendered and mixed into its track, the engines included |
| 8 | DX7 | the DX7's voices alone (inside VOICES) |
| 9 | JUNO | the Juno-106's |
| 10 | TB-303 | the TB-303's |
| 11 | SAMPLE | the sampler's |
| 12 | MIX | each voice mixed in after it is made: its gain, mute, pan, sends, scope and peaks (inside VOICES) |
| 13 | J-CHORUS | the Juno-106's chorus buses (inside VOICES) |
| 14 | EFFECTS | the effects: the song's chain, or DJ's mix of both decks |
| 15 | REVERB | the REVERB alone (inside EFFECTS) |
| 16 | DELAY | the DELAY alone |
| 17 | CHORUS | the CHORUS alone |
| 18 | DJ FILTERS | DJ's two DJ FILTERs, on each deck's sound and its sends (inside EFFECTS) |
| 19 | DJ MIX | DJ's own mix of the decks, but for its filters and the decks' own effects: each deck's sound taken apart, its strip set up, ECHO / REVERB OUT fed, VOLUME x the crossfader on its sound and sends, the decks summed (inside EFFECTS) |
| 20 | INSERTS | GATE, FILTER, DRIVE and LO-FI: the song's, or each deck's (inside EFFECTS) |
| 21 | LIMITER | the LIMITER (inside EFFECTS) |
| 22 | PUNCH-INS | the punch-ins and their buffer, which records the output always (inside EFFECTS) |
| 23 | OUTPUT | the soft ceiling, the output to the codec, VISUAL's ring, USB audio |
| 24 | USB AUDIO | USB audio's stream while a computer records: the mix to 16 bits at USB GAIN and resampled to the computer's rate ([connections.md](connections.md#usb-audio)); nothing while none does (inside OUTPUT) |
| 25 | CLOCK | the clocks: SYNC's pace, CLOCK OUT's, each replay's |
| 26 | BLOCKS | the half's blocks handed to the effects: the settings and the clock each block's effects work with kept with it, put back for its effects block by block, the changes it made to the effects replayed ([the blocks of a half](#a-halfs-blocks)) |
| 27 | MIDI | MIDI's share of the half: CLOCK OUT, notes sent ended |
| 28 | END | the snapshot the screens read |

What a stage's parts leave of it is the rest of its work (in REPLAY: the replays' own row and tick work, the
rows' commands, the tracks' levels; in EFFECTS: the shared effects' own passes, the sends' sums, the METRONOME).

**Each stage is measured wherever it runs:** while core 2 takes the blocks ([second-core.md](second-core.md)), EFFECTS
and its parts (but DJ MIX, DJ FILTERS and the decks' INSERTS, core 1's), OUTPUT and USB AUDIO are core 2's, counted
by core 2 for the half they were handed over in. At the next half's start, once core 2 has finished, core 1 adds
them to that half's own counts and clears them, so a stage's sum and its longest in one half, and the worst half,
take both cores' work for the half (CPU time, not the time the half took).

The audio interrupt's whole time is measured too, core 1's alone. The page's "(interrupt)" row is what the stages
leave of both cores' time for the half (the interrupt's, and core 2's from 37, the stages counting both): the
interrupt's own work (silencing the half first, waiting for core 2 to finish the half before, the calls between the
stages, the rest) and core 2's between its stages.

### A half's blocks

A half is four blocks of 64 frames. The player makes every block up to its effects first (its control, the
replays, the voices, the clocks), keeping with each the settings its effects work with and the changes it made
to them; then the effects take the blocks: all four together, each effect's code once, when every block has
sound and the same chain of effects (each block with its own knobs), else block by block, each block's settings and clock put back first and its
changes replayed. A silent block with the effects at rest takes the rest path, a block at a time. BLOCKS is
that handing over; the effects' own work is EFFECTS'.

**The worst half,** besides the stages' sums: the one half of the window whose stages (the top ones, not
their parts) summed most, with its own stages, its voices, the notes started in it and its voice blocks by
engine, so a late half shows what it was made of, not only how long it took.

**Besides:** the halves in the window and the late ones; how many 64-frame voice blocks each engine
rendered (so a voice's cost: its time over its blocks); the voices now and the most in the window, the
notes taken; flash reads in the audio context, their count and time; the main loop's passes and its
longest; the screen's frames drawn, their total and longest time; each stack's deepest point since power-on
(the main loop's, the interrupts'); the time since power-on; what plays (the song, or DJ's two, its tempo,
VOICES, the effects switched on), the screen, and the firmware's version; USB audio's state and its packets
(sent, short, missed), so a recording's clicks show whether the FM-1 was late or the USB buffer ran dry.

## Asking: SysEx

PT/FM's messages, `F0 7D 50 54 cmd ... F7` ([files/blob.md](files/blob.md#seeding)), seven commands more:

| cmd | Direction | Contents |
|---|---|---|
| 30 FIGURES? | page → device | one byte, 7 bits: bit 0 the worst figures start afresh (only the page keeps them: it drops its own), bit 1 FREEZE THE SCREEN on, bit 2 off |
| 31 FIGURES | device → page | the window, 7-bit packed as seeding's (7 bytes in 8): the bytes below |
| 37 FIGURES 2 | device → page | core 2's window, sent right after each 31, packed as 31: [its bytes](#core-2s-bytes) |
| 38 SECOND CORE | page → device | one byte: 0 core 2 off (as SETTINGS' SECOND CORE OFF, the setting itself untouched and nothing saved) until a 38 with 1 or any restart; 1 back to what SETTINGS says |
| 32 BENCH | page → device | one byte: 1 run the bench, 0 stop it ([the bench](#the-bench)) |
| 33 BENCH RECORD | device → page | a piece of the bench done, packed as 31: [its bytes](#a-records-bytes) |
| 34 USB TONE | page → device | one byte: 1 the USB test tone on, 0 off ([below](#the-usb-test-tone)) |
| 35 PROVE | page → device | one byte: 0 the records, 1 the watchdog test, 2 the main loop's stack's overflow, 3 the interrupts', 4-6 core 2's fault, misread, stuck ([below](#proving-the-layout)) |
| 36 RECORDS | device → page | what was kept across the last restart, packed as 31: [its bytes](#proving-the-layout) |

Only the debug build answers 30, 32, 34, 35 and 38; the release ignores them, as every unknown command. The state
(40, 41) and MIRROR (43) are every build's ([connections.md](connections.md#mirror-the-panel-from-a-page)).

**FIGURES' bytes** (version 11; little-endian; `u16` and `u32` unsigned; *n* stages, 29; *p* = 18 + 6*n*,
192):

| Offset | Size | What |
|---|---|---|
| 0 | u8 | the version: 11 |
| 1 | u8 | bit 0 the screen frozen through the window (from its start); bit 1 frozen from now on |
| 2 | u16 | the window's length, ms (at most 65,535) |
| 4 | u16 | audio halves in the window |
| 6 | u16 | late halves in the window, either core's (37 tells them apart) |
| 8 | u32 | the audio interrupt's time (core 1's), summed, µs |
| 12 | u16 | its longest half, µs |
| 14 | u16 | a half's length, µs (5,804) |
| 16 | u8 | *n*, the stages that follow |
| 17 | u8 | the window's number, modulo 256 (37 FIGURES 2 carries the same) |
| 18 | *n* × 6 | each stage, in the table's order: u32 its time summed, u16 its longest in one half, µs |
| *p* | 4 × u16 | voice blocks rendered: DX7, JUNO, TB-303, SAMPLE |
| *p* + 8 | u8, u8, u16 | voices now; the most at once in the window; notes taken in the window |
| *p* + 12 | u16, u16 | flash reads in the audio context: how many; their time, µs |
| *p* + 16 | u16, u16 | the main loop's passes; the screen's frames drawn |
| *p* + 20 | u32 | the main loop's longest pass, µs |
| *p* + 24 | u32, u32 | the frames' time summed; the longest frame, µs |
| *p* + 32 | u16, u16 | the deepest each stack went, bytes: the main loop's, the interrupts' |
| *p* + 36 | u16, u16 | each stack's size, bytes |
| *p* + 40 | u32 | time since power-on, ms |
| *p* + 44 | u8 | what plays: 0 nothing, 1 a song, 2 a pattern, 3 LOOPS, 4 DJ (the player's `what`) |
| *p* + 45 | u8 | 1 playing |
| *p* + 46 | u8, u8, u8 | the song; DJ's left deck's, right deck's (`FF` none) |
| *p* + 49 | u8 | VOICES, the limit in force: 8, 12 or 16 (SETTINGS' 16 is 12 while core 2 does not run) |
| *p* + 50 | u16 | the tempo now, BPM × 10 |
| *p* + 52 | u16 | the effects on and working now, a bit each by their numbers ([ui/screens/fx.md](ui/screens/fx.md)) |
| *p* + 54 | 10 | the screen's name, ASCII, NUL-padded |
| *p* + 64 | 24 | the firmware's version, ASCII, NUL-padded |
| *p* + 88 | 8 + 2*n* | [the worst half](#a-worst-halfs-bytes) |
| *p* + 96 + 2*n* | 185 | [DJ](#djs-bytes) |
| *p* + 281 + 2*n* | 16 | [USB audio](#usb-audios-bytes) |
| *p* + 297 + 2*n* | | the end (547 B; 632 on the wire, of the 640 a message may take: the build checks) |

Version 1 had 16 stages (no MIX, J-CHORUS, DJ FILTERS, INSERTS, LIMITER, PUNCH-INS) and no count at 16;
version 2 no worst half; version 3 22 stages (no CELLS, NOTES); version 4 no DJ; version 5 24 stages (no DJ
MIX); version 6 25 (no USB AUDIO) and no USB audio's bytes; version 7 26 (no DJ DECKS, DJ LFOS); version 8 27 (one DJ
CONTROL for both); version 9 28 (no BLOCKS) and USB audio's byte 1 spare; version 10 one core, byte 17 spare, no 37;
a page reads only its own version.

### Core 2's bytes

**37 FIGURES 2** (version 2; little-endian), after every 31, for the same window:

| Offset | Size | What |
|---|---|---|
| 0 | u8 | the version: 2 |
| 1 | u8 | the window's number, as 31's byte 17 |
| 2 | u8 | core 2's state: 0 not started yet (`STARTING`), 1 running (`ON`), 2 off (`OFF`), 3 stopped |
| 3 | u8 | why it stopped: 0 not stopped, 1 no answer, 2 a misread, 3 stuck, 4 a fault, 5 after a crash |
| 4 | u8 | bit 0 SETTINGS' SECOND CORE on; bit 1 off by 38; bit 2 its start needed the clock-divider bit (always 0 now: the start has one try, without it); bit 3 the blocks went to core 2 for the whole window |
| 5 | u8 | spare |
| 6 | u16 | its answer at the start, in 10 ns (41: 0.41 µs; 0 not started) |
| 8 | u32 | its blocks' time summed, µs |
| 12 | u16 | its longest half (the blocks of one half summed), µs |
| 14 | u16 | the blocks handed over |
| 16 | u16 | the misreads (entries it refused) since its start |
| 18 | u32 | core 1's waits for it at the halves' starts, summed, µs |
| 22 | u16 | the longest of them, µs |
| 24 | u16 | late halves core 1 caused: its interrupt ran past the half |
| 26 | u16 | late halves core 2 caused: its last block not done when the next half began |
| 28 | u16, u16 | its jobs' stack: the deepest it went since its start, its size, bytes |
| 32 | u32, u32 | its fault record if it faulted: the vector and the PC (0 none) |
| 40 | u32 | its voice jobs' time summed, µs (part of byte 8's; [second-core.md](second-core.md#the-voices-on-core-2)) |
| 44 | u16 | the voice jobs handed over (blocks with a voice of core 2's places, 11-16) |
| 46 | u8, u8 | the most of core 2's places sounding at once; spare |
| 48 | u32 | core 1's waits for the voice jobs, summed, µs |
| 52 | u16 | the longest of them, µs |
| 54 | u8, u8 | SysEx messages the FM-1 lost on the way in this window (255 at most): begun while others waited for its main loop (it keeps one frame and one short message, under 15 bytes, at a time), cut short by another message's status (not core 2's, but the window's: FIGURES 2 carried it, the bytes being spare; 0 from an older firmware) |
| 56 | | the end |

Core 2's voice jobs count in the stages as core 1's voices do (VOICES and the engines' stages, their blocks; their
mixing in MIX, as core 1's adding of core 2's mixes): the stages sum both cores' time. A voice job's time (byte 40)
is its rendering and its mixing.

### USB audio's bytes

In every FIGURES; the report gives a line of them for a section a computer recorded in, or says nothing
recorded. Counted since the last window, but the state and byte 1:

| Offset | Size | What |
|---|---|---|
| 0 | u8 | bit 0 the stream is fed now (a computer reads it); bit 1 at 48 kHz (else 44.1) |
| 1 | u8 | the FIGURES answers the FM-1 could not send whole since power-on, modulo 256: USB cut one short (the computer stopped taking it for 200 ms) or could not start it; that window is lost. A window missing while this stands still was lost on the computer's side. The report says how many a section lost so |
| 2 | u16 | packets sent |
| 4 | u16 | packets short: the buffer ran dry, the last frame repeated (a click in the recording) |
| 6 | u16 | missed: the computer asked and found no packet ready (a gap in the recording) |
| 8 | u16 | stalls: the computer stopped reading for more than 20 ms |
| 10 | u16 | restarts: the stream primed again (a start, a rate change, after a stall) |
| 12 | u16, u16 | the buffer's fill at the last half's start, frames; the fill it is steered to (128 at 44.1 kHz, 139 at 48) |

### DJ's bytes

In every FIGURES, whatever plays (the page uses them when DJ played):

| Offset | Size | What |
|---|---|---|
| 0 | u8 | bit 0 the decks are in the player (DJ on); bits 1 and 2 the left, the right deck playing |
| 1 | 24 | the left deck as it plays: u8 bit 0 playing, 1 armed (to start on the other's bar line), 2 at its song's end, 3 a stop standing, 4 a ROLL; u8 the position, u8 the row; u16 its tempo now, BPM × 10; u8 a stop held (0 none, 1 BRAKE, 2 BACKSPIN, 3 ECHO OUT, 4 REVERB OUT); u8 its loop (0 none, 1 the pattern, 2 rows); u8 the loop's rows (at most 255); 16 its song's name, ASCII, NUL-padded |
| 25 | 24 | the right deck, the same |
| 49 | 136 | DJ's record as it stands now ([files/dj.md](files/dj.md#the-record)): the setup, DJ's FX setup, the crossfader's and the FX LFO, each deck's song, tempo, KEY, VOLUME, FILTER, loop, mutes, solo, filter LFO |

### A worst half's bytes

In FIGURES and in a bench step's record:

| Offset | Size | What |
|---|---|---|
| 0 | u16 | its stages summed (the top ones), µs |
| 2 | u8, u8 | its voices; the notes started in it |
| 4 | 4 × u8 | its voice blocks: DX7, JUNO, TB-303, SAMPLE |
| 8 | *n* × u16 | each stage in it, µs, in the stages' order |

## FREEZE THE SCREEN

While frozen, the screen is not drawn (its last frame stays, with `FROZEN FOR MEASURING` in its header);
keys, knobs, LEDs and the sound go on. Comparing the sound's figures with and without it shows what the
screen's drawing costs the sound, through the code cache the two share. A power cycle ends it. The freeze
starts at once; the thaw after the answer to the request that asks for it, so a window is frozen or not
from its start to its end (the first window of a freeze counts as not frozen).

## The bench

On the page's 32 BENCH the FM-1 measures by itself for well under a minute, a piece at a time in the main
loop, and sends each piece's result as a record (33). It measures only what needs the firmware's insides:
what the chip itself costs, and what each engine and effect costs alone and whether the code cache slows
it. The sound running (songs, the keys' voices, effects, DJ) is the [autotests](#autotests)' to measure,
through the panel; the bench's steps that did it are gone (since 2026-10-13). The page runs it as the
autotests' CHIP scenario.

**Before:** what plays stops, the keys and every voice are let go, every effect is off (so the effects'
state is the bench's alone). It refuses while ARP is on, while the flash is written, or while DJ is on,
saying so in its END. **After** (done, or STOP): the sound off, the effects as they were; what played does
not start again. The screen shows each piece in its header; nothing must be touched meanwhile.

**The pieces, in order:**

1. **THE CORE:** the platform's own, each run 1 + 8 times with the interrupts off, timed by the 24 MHz
   timer (on the FM-1: an empty loop; eight adds in a loop; a 32-bit multiply; the engines' 64-bit multiply
   and shift; a 64-bit multiply-accumulate with its code in the flash, the same code in RAM, and with its
   code's cache lines dropped first; a 32-bit divide; RAM loads and stores; memset and memcpy, and memcpy of 48
   bytes with its two ends aligned alike (a word at a time) and not (a byte at a time: MEMCPY 48 B, ODD); flash words
   through the code cache warm, after their lines were dropped, and 32 and 64 bytes apart (a cache line
   each); USB audio's resampler: an output frame as the stream makes it and as a loop of four
   multiply-accumulates a tap, the whole 64-frame block at 44.1 kHz (warm, and with its code's cache lines
   dropped) and at 48 kHz, set up for the measurement and the stream put back as it was; the
   [USB test tone](#the-usb-test-tone)'s 64 frames; the whole cache emptied).
2. **ALONE:** a voice of each engine, outside the pool (three DX7 voices of different algorithms, a
   Juno-106 patch for each of its render's paths, the TB-303, the first looped instrument of any sampler
   (else the one with the longest sample) at its base note and an octave up), the Juno-106's noise generator
   (one block for all its voices: JUNO NOISE), then each effect (REVERB, DELAY, CHORUS, FILTER, DRIVE, LO-FI, GATE,
   LIMITER, the punch-ins' buffer, a DJ FILTER on two channels and on one), and the settings a block keeps for
   its effects, taken and put back as [a half's blocks](#a-halfs-blocks) have them (SETTINGS, GET + PUT), a 64-frame block at a time
   with the interrupts off: three blocks not timed (a note's attack, a filter's first glide), one with the
   code cache emptied first (**cold**), then eight (**warm**).

### A record's bytes

Version 5 (version 1: no worst half; version 2: 22 stages; version 3: 24; version 4: 25); little-endian. Every
record starts so:

| Offset | Size | What |
|---|---|---|
| 0 | u8 | the version: 5 |
| 1 | u8 | its kind: 0 START, 1 CORE, 2 ALONE, 4 END (3 STEP and 5 NEXT were the steps', gone) |
| 2 | u16 | the piece's number, from 1 (START 0; END the number of pieces) |
| 4 | u16 | the pieces in all |
| 6 | 20 | its name, ASCII, NUL-padded |

| Kind | Offset | What |
|---|---|---|
| START | 26 | the firmware's version, 24 B; 50: u8 VOICES before; 52: u32 time since power-on, ms (56 B) |
| CORE, ALONE | 26 | u8 what an op is: 0 a turn or an op (cycles), 1 a byte, 2 a call, 3 a 64-frame block; 27: u8 the runs after the first; 28: u32 ops a run; 32: u32 the first run, ticks (ALONE: the cold one); 36, 40, 44: u32 the runs after it, least, most, summed (48 B; ticks of the 24 MHz timer). An ALONE with no runs had no sound (its audio not there) |
| END | 26 | u8 why: 0 done, 1 stopped, 2 refused: ARP on, 3 refused: the flash busy, 4 refused: DJ on; 28: u32 its time, ms (32 B) |

## The USB test tone

**34 USB TONE** with 1: USB audio carries a test tone instead of the mix, a sine of 997 Hz on the left and
1,499 Hz on the right at −12 dBFS whatever USB GAIN is, through the same resampler and stream
([connections.md](connections.md#usb-audio)); with 0, the mix again. A recording of it shows every lost,
repeated or late packet exactly, and the resampler's quality, apart from the music. The speaker and the jack
play on as ever. It goes off at a USB bus reset and at power-off, so it never stays on unseen; the state
says when it is on ([connections.md](connections.md#the-state)). The tone costs ~76 µs a half of its own (two
sine lookups a frame), so with it on USB AUDIO reads that much over the stream's own cost.

## Proving the layout

The RAM's layout ([files/ram.md](files/ram.md#the-layout)) is proved on the FM-1 by the debug build before it is
trusted: its guards are tried on purpose, and what each restart kept is read back. **35 PROVE** asks, the page's
**RESTARTS** keys (one press: its key counts down 3 s, `RESTART IN 3: PRESS TO CALL OFF` or `STOP CORE 2 IN 3`, then
the test goes; a press during it calls it off: each restarts the FM-1 or stops core 2):

| Key | PROVE | What it does |
|---|---|---|
| RECORDS | 0 | answers 36 RECORDS (the page asks by itself at every CONNECT, so after a restart CONNECT is enough) |
| WATCHDOG | 1 | the main loop stops feeding the watchdog. First a crash record of the test's own is written and the boot guard set as for a start not yet healthy, so each restart counts as a failed start. About 8 s later the watchdog restarts the FM-1; that start shows what it found (the crash record kept or lost, the boot guard's count, 1 expected, the watchdog as the reason, the band) for 5 s, then stops feeding again: the second restart must end in **USB RESCUE**. Install again from there (or switch off and on); RECORDS then lists the first restart's findings |
| MAIN STACK | 2 | the main loop calls deeper and deeper until its stack runs past its bottom: the crash screen must say `STACK: MAIN LOOP`; after the restart, RECORDS shows the crash record saying so |
| INTERRUPTS' STACK | 3 | the same in the audio interrupt: `STACK: INTERRUPTS` |
| CORE 2 FAULT | 4 | core 2's next block faults on purpose (its own stack limit tripped: a fault of core 2's own EMU bank, which the chip takes on core 2, as the FM-1 showed; a bus error would be flagged in the debug unit both cores share and taken on core 1): core 2 keeps its fault record and parks, core 1 falls back at the next half (`STOPPED: FAULT`), the sound goes on; no restart. 37's last bytes show the record |
| CORE 2 MISREAD | 5 | the next block's entry is handed over with a wrong check word: core 2 refuses it, core 1 falls back (`STOPPED: MISREAD`); no restart |
| CORE 2 STUCK | 6 | core 2's next block never ends (it spins): core 1 falls back at the next half, 1 ms into its wait (`STOPPED: STUCK`); no restart |

PROVE 4-6 take the next job handed over: with more than 10 notes sounding at VOICES 16 that is a block's voice job,
handed over before the block's effects, so held keys past 10 try the fallback during a voice job (that block's
core-2 voices unheard, then parked, or let go if core 2 had started on them, the sound going on:
[second-core.md](second-core.md#the-voices-on-core-2)).

PROVE 4-6 need core 2 running and the next half handing it blocks (else they do nothing, and the page says so: at
once when 37 shows core 2 not running, or when a CUT, SECOND CORE off or a 38 kept the half on core 1); after one,
core 2 stays off until the next
start ([second-core.md](second-core.md#start-switch-fallback)). **PROVE 2-3 with core 2 running** end in a crash
that core 2 was running at, so the starts after it keep core 2 off (`STOPPED: AFTER A CRASH`) until the power goes
off, by design: run PROVE 4-6 first, or switch off and on after 2-3. PROVE 1's watchdog restarts are the test's own
and leave core 2 free to start.

- **The band:** the 256 B at 0x01C07F00 hold the effects' lines, so they cannot be patterned while PT/FM runs.
  Before every restart the debug build makes itself (a crash's, the watchdog test's), with the sound stopped, it
  patterns them; the next start checks them before the pool is set up and keeps what changed.
- **Its record** is kept with the crash record and the boot guard (40 B more there), fresh at every power-on.
- **What to see in a full AUTOTEST:** no crash, no false alarm of the stack check; RECORDS' stack windows "one for
  each stack".

**36 RECORDS' bytes** (version 3; device → page, packed as 31; little-endian):

| Offset | Size | What |
|---|---|---|
| 0 | u8 | the version: 3 |
| 1 | u8 | the stack windows armed: 2 one for each stack, 1 one over both (the pointers were not where the start put them) |
| 2 | u8 | PROVE's test last under way: 0 none, 1 the watchdog's first restart, 2 its second, 3 the main loop's stack, 4 the interrupts' |
| 3 | u8 | 1: the band was checked at this start (patterned before the restart) |
| 4 | u16 | its bytes found changed |
| 6 | u8 | this start's reasons (the chip's: bit 0 power-on, 2 the watchdog, 6 a software reset) |
| 7 | u8, u8 | the boot guard now: failed starts counted, pending |
| 9 | u8 | 1: the watchdog test's first restart was seen; then: |
| 10 | u8, u8 | the boot guard it found: failed (1 expected), pending |
| 12 | u8, u8 | the watchdog the reason; the test's crash record kept |
| 14 | u16 | the band's bytes changed by that restart (`FFFF` not checked) |
| 16 | u32 | this start's other reset reasons (the chip's RST_SRC) |
| 20 | 72 | the crash record as kept: 18 words (its mark, the crashes counted, the vector, PC, RETS, EMU, DBG, SP, PSR, ICFG, the time up in ms, four branches traced, the crashes early in a row, the stack overflowed: 0 none, 1 the main loop's, 2 the interrupts'; core 2 running at the crash: 1, else 0) |
| 92 | 4 × u32 | the stacks' ends: the main loop's bottom and top, the interrupts' |
| 108 | 3 × u32 | the stack pointers the start's check read when it armed the windows (byte 1): `usp`, `ssp`, and `sp` (the one in use then); RECORDS says which of the two `sp` was |
| 120 | | the end |

Version 1 had a crash record of 17 words (no core 2) and ended at 104; version 2 ended at 108.

## The page

`dist/debug/install.html`: the release's install page made the debug build's own, **on one screen, in the
FM-1 screen's look**. It is the design the other pages take: the release's install page
(`dist/firmware/install.html`) has the same look on one plain column (no sidebar, header or footer: a simple
install), in the browser's own fonts (nothing embedded or fetched: it works offline from its file), and on a
phone its keys stack. Its how-to (before you start, what the keys do, what can go wrong, the credits) is what
HELP shows here.

- **The look:** black, and the soft EGA palette's roles ([ui/colours.md](ui/colours.md)): labels cyan, text
  light grey, values white, buttons as keys with yellow names, on and passed green, stops, failures and
  warnings red, prompts yellow, the scenario running on the playing row's blue band. Square corners and
  1-px dark grey lines. **One font, Iosevka Aile, everywhere on the page** (Iosevka's quasi-proportional cut,
  regular and bold, in the page, cut to the characters it writes, SIL OFL 1.1, credited under HELP). The pages
  never use the FM-1's own Terminus bitmaps.
- **The frame:**
  - **a sidebar** on the left with the buttons of FIRMWARE, FM-1, LISTEN, RUNS and RESTARTS ([proving the
    layout](#proving-the-layout): its records shown below its keys) (AUTOTEST's are in its column); on top
    the PT/FM logo in the start screen's colours (P light red, T light cyan, / yellow, F light green, M light
    magenta);
  - **the header:** the screen's header line (`DEBUG FIGURES`, then `(ONCE A SECOND)` small and grey, HELP on
    its right), the live figures
    (CPU 1 and CPU 2 now, core 2's state, the worst half, late halves while recording, voices) and the graph of the
    last 60 s, core 1's as now and core 2's a line of its own;
  - **the body,** three columns, each scrolling on its own: the stages, the scenarios, the reports;
  - **the footer:** what is under way (the install's words, the autotest's step) and the states (MIDI,
    LISTEN, AUTOTEST), **always one line:** what does not fit is cut, whole in its tooltip, so the page never
    jumps (on a phone it may wrap). The install page's line before anything happened ("Ready: connect the FM-1
    by USB ...") is left out: its keys say it.

  It fits the window from 1366 × 768 up. Narrower, the body takes two columns; on a phone, everything comes
  one after another and the page scrolls. HELP opens the install page's how-to and credits over the body,
  and the demo disk's plan opens there too.

Its parts:

1. **FIRMWARE:** the release's page (INSTALL FIRMWARE, UPLOAD DEMO DISK) with the debug package in it, its CLEAN
   INSTALL switch on at first ([files/blob.md](files/blob.md#seeding): everything the FM-1 holds
   erased, an empty user area; the demo disk UPLOAD DEMO DISK's). Under them the page's own **CLEAR USER DATA**
   (`everything the FM-1 holds erased, PT/FM kept`): CLEAN INSTALL's erasing without the install, with PT/FM
   running, through the same seeding (every block of the user area written empty: no record left, the settings
   and damaged records with them; the FM-1 mounts the empty library and goes on, as after any seeding). A press
   counts down 3 s on the key (`CLEARING IN 3`, the key blinking as a question; its line says a press calls it
   off), a press in them calls it off, as does pressing another key or switching CLEAN INSTALL; an autotest's run
   holds it. With edits not saved the FM-1 refuses (REVERT them or SAVE first) and nothing is erased; a SAVE on
   the FM-1 between its layout and the erasing plans again. **The key pressed tells what it does:** while it
   works its name becomes `INSTALLING 42%` (or `UPLOADING`, `CLEARING`), the share as a green fill, and its second
   line the install's words, the last of them staying until another key is pressed or CLEAN INSTALL switched: red
   when it failed; green when done, and then gone after 10 s (the key's own line back, the footer's line and bar
   away). The second line is always two lines high, so the sidebar never moves: longer words are cut, whole
   in the footer and the key's tooltip.
2. **RUNS,** the figures recorded by hand:
   - **CONNECT** asks for the figures once a second while the FM-1 runs the debug build (not while an install
     or an upload runs); the window's figures show live: the CPU now, each stage's share and µs, a graph of
     the last 60 s. The first answer after CONNECT covers everything since a page last asked, so it only
     starts the clock: it is neither shown nor kept.
   - **RECORD and STOP:** the user sets up what is to be measured, types the run's name (`PASS 1`), presses
     RECORD, and STOP when done; only the windows between them are kept, as that run. While it records the
     key says how long so far (`REC 00:22`, a second at a time), the name field is disabled and shows the run's
     name; STOP leaves the name there to be edited (`PASS 2`).
     Between a STOP and the next RECORD the figures show live and nothing is kept, so changing things is
     never measured. RECORD waits while an autotest runs.
   - **WITH AUDIO** (a tick under them, off at first): RECORD also records the FM-1's USB audio for the run (the
     input taken first, the browser's prompt if it asks, then the run starts), STOP ends both. The run's line
     says how much (`audio 01:42 19.0 MB`) and its **ZIP** saves its WAV beside the run's report. The FM-1 then
     sends USB audio, about 8 % of its CPU, which the run's figures include (its report's USB audio line says so).
     LISTEN's own recording can run beside it.
   - **The runs,** a list: each with its name, length, CPU 1 and CPU 2 (mean, worst) and late halves at a glance, its
     **COPY** (its report), **ZIP** (a file to keep: its report, `report.txt`, and its WAV when recorded WITH
     AUDIO, `ptfm-run-pass-1-YYYYMMDD-HHMMSS.zip`; waits while the run records) and **DELETE**. The part's
     **COPY** copies every run's report, one after another; **DELETE ALL** drops them.
   - **The page gathers every recorded answer** in memory (one a second, a few hours at most) and computes,
     for each run: the worst, the mean, the median and the 95th percentile, and the CPU's share for each
     stage, **from the windows the FM-1 played in** (the stopped ones left out, and counted), unless it
     never played; for each core apart (CPU 1: the interrupt's time; CPU 2: its blocks' time, from 37), with
     core 2's state, core 1's waits for it and the late halves each core caused in the report.
   - **FREEZE SCREEN** switches the freeze on and off.
3. **LISTEN,** a part of its own:
   - **USB AUDIO HERE** (off at first) plays the FM-1's USB audio input on the computer's speakers, with a
     **VOLUME** slider (remembered by the page, 50 % at first): to hear what USB audio carries. The browser asks
     once for the input, with the browser's voice processing (echo cancelling, noise suppression, level control)
     off. The page plays whatever input the browser opened: the one picked in the browser's prompt (Windows
     names the FM-1's "Line (FM-1 PT/FM)"), or later in its microphone setting. It shows that input's name, and
     says when the permission was refused. Its sound starts in the click on the switch itself (a browser lets a
     page's sound start only from one, and the prompt comes between); when the browser holds it back anyway, it
     says so. **Once allowed, the input stays open while the page is open:** a browser keeps no permission for
     a page opened from a file, so asking again would bring its prompt again; off only pauses the sound, and on
     goes on without a prompt. The prompt comes again after a reload, or after the input went away (the FM-1
     unplugged). **An autotest run holds LISTEN** ([below](#autotests)): the switch locked, off and the input
     let go but for the scenario that takes it, as it was before RUN after the run. Its latency is the
     browser's (some tens of ms): for listening, not for playing along.
   - **USB DELAY,** under the slider, live, tells which side a delay is on, in two lines. **The FM-1's
     part** from the latest FIGURES: its buffer's fill at a half's start in ms (the frames at the computer's
     rate) plus the resampler's 0.2 ms, with line out's own buffer (a half, 5.8 ms) beside it; "not
     sending" while nothing reads its USB audio. **The computer's part** while USB AUDIO HERE plays, as the
     browser reports it: the input track's latency, then the sound's base and output latency (`?` when the
     browser gives none).
   - **RECORD and STOP** record what the input carries, whether it is heard or not (its level as it comes,
     not the VOLUME's), into **one recording:** a new RECORD takes its place. While it records the key says how
     long so far (`REC 00:22`) and the input stays open; the line under the keys says its length, rate, size
     and time (`00:42, 48 kHz STEREO, 8.1 MB, 15:43`). **SAVE WAV** saves it as a file
     (`ptfm-listen-YYYYMMDD-HHMMSS.wav`, 16-bit stereo at the browser's sound rate; a mono input on both
     sides). RECORD waits while an autotest runs, and RUN stops a recording under way (it is kept).
4. **AUTOTEST** ([below](#autotests)): its column holds its keys above the scenarios, **RUN** and **STOP**,
   **RECORD AUDIO** under them, with the warning under it (unsaved changes dropped, hands off the FM-1); its
   runs' reports, each with **COPY**, **ZIP** and **DELETE**, are in the reports' column. While a run goes on, the scenario running is on the band, the
   list scrolls to each one as it starts, and each finished one shows PASSED, FAILED and why, or SKIPPED; the
   tick boxes wait. A prompt shows between the keys and the list, with its **CONTINUE** blinking.

**A run's report** is a compact text: the firmware, the time, then the run: what played, its length, CPU
(mean, median, 95 %, worst), late halves (and how many a minute, so runs of other lengths compare: `late 12
(24/min)`), each stage's mean µs a half and share, the worst half (its time,
voices, notes started, its stages and voice blocks), **the moments** (each window with late halves or a START of
300 µs or more, at its time into the run, `12 s late 2 START 643 worst 104%`, so a spike or a dropout can be laid
beside what the run did then; at most 12, those with the most late halves, then the longest START), a voice's cost
by engine, voices (mean, most: the mean from the voice blocks made, every block of every voice, so voices
that sound half a row, as a TB-303's on a track, count as they sound), the
screen's frames, the stacks' depth, USB audio, and the FIGURES windows the FM-1 could not send whole when there
were any. A run DJ played in also lists **DJ's settings** as at its end
(the crossfader and its place, FILTER RESONANCE, FILTER THE SENDS, SYNC and its multiple, the crossfader's and
the FX LFO, DJ's CHORUS, DELAY, REVERB and LIMITER with their knobs' steps, the METRONOME;
each deck's song, tempo, KEY, VOLUME, FILTER, REPEAT PATTERN, loop, mutes, solo, filter LFO, cue), what each
deck was doing at its end (playing or not, where, its tempo, a loop or a stop), and every setting that
changed during it with its values in order. Its size stays the same however long it ran. Plain ASCII,
lines under 100 characters, so it pastes anywhere.

## Autotests

Autotests play the FM-1 as a hand on its panel would: the page presses its buttons and keys and turns its
knobs through [MIRROR](connections.md#mirror-the-panel-from-a-page), waits, checks where the FM-1 is through
[the state](connections.md#the-state), and measures through FIGURES. They run on the real device and its
real sound, scenario after scenario, unattended. These are **stress tests**; UI tests
(every screen opened, patterns edited) are to come the same way.

- **On any PT/FM firmware with MIRROR:** CONNECT is all they need. On a firmware other than the debug build
  they run the same, checking the state at every step; FIGURES are left out of the report, and the
  scenarios that need the debug build (CHIP, FILES' USB TONE) are skipped and say so, and AUDIO OUT's
  debug part (the tone, the check of USB audio in the figures) is left out. A firmware that does not
  answer the state gets no autotest (the page says so).
- **MIRROR is the page's while it runs:** the hello at RUN, and again whenever the state says MIRROR is off
  (USB was reset). **At its end, done, failed or STOPPED,** the FM-1 is left as a scenario starts
  ([below](#a-scenario)): every control let go, the test tone off, what plays stopped (a song, DJ, LOOPS), all
  sound off, the changes dropped (REVERT), no question open; then MIRROR off. These steps run on after STOP
  (they are what STOP does; the run shows RUNNING until they are done); when they cannot be done the report
  says so in its second line, after all sound off and a question closed by HOME (its BACK). Done, the FM-1 is
  then seen **at rest**: 3 s for the effects' tails to fall asleep, then 2.5 s of FIGURES and the state, and the
  report's line `THE END: AT REST, NO VOICE SOUNDING, THE EFFECTS RESTING (CPU 5.0 %)`, or `THE END: NOT AT
  REST (CPU 8.1 %): 2 VOICES SOUNDING` with the effects found at work on a line under it (`EFFECTS AT WORK:
  REVERB, DELAY, LIMITER`): an effect at work shows its own stage in the FIGURES, the effects at rest are all
  skipped and none does. (The emulator's clock stands still inside a half, so its stages read 0 there: only the
  voices are seen.)
- **A cartridge picked by SEL + PRESETS** that the cursor does not reach gets a second go (a SEL press not taken
  leaves PRESETS turning the slots); then a built-in (F0-FF, always there) fails its scenario with the why (`COULD
  NOT REACH THE CARTRIDGE F8 (AT F0)`), a user one (a demo disk's) is skipped as missing, the why in brackets.
- **A state left unanswered** (four questions, ~4 s; up to twelve, ~12 s, while the FM-1's FIGURES still come)
  fails its scenario with whose silence it was: `THE FM-1 DOES NOT ANSWER THE STATE (ITS FIGURES STILL COME, THE
  LAST 0.6 S AGO; ASKED FOR 12 S; ITS MAIN LOOP'S LONGEST PASS 63.2 MS, SYSEX LOST 2 + 0)`, the FM-1 and the USB
  working but that answer missing, or `(NOTHING FROM THE FM-1 FOR 4.3 S)`, the link, the computer or the FM-1 itself
  stalled. A state answered only after two questions or more goes on, noted: `THE STATE ANSWERED ONLY AFTER 4.8 S:
  THE FM-1 SENT 6 ANSWERS THE COMPUTER NEVER GOT, 0 CAME LATE; MEANWHILE (5 FIGURES WINDOWS) ITS MAIN LOOP'S LONGEST
  PASS 63.2 MS, SYSEX LOST ON THE WAY IN: 2 BEGUN WHILE OTHERS WAITED, 0 CUT SHORT` (by the answers' numbers, [the
  state](connections.md#the-state)'s byte 25: a computer busy drops the FM-1's messages now and then, FIGURES too:
  `USB LOST THE REST`; the main loop's pass and the SysEx lost from the FIGURES windows that came meanwhile, [core 2's
  bytes](#core-2s-bytes)' 54 and 55: a main loop held up, or the questions lost on their way in).
- **Unsaved changes are dropped before every scenario** with REVERT, when the state says there are any, so
  every scenario starts from what is saved and a run never piles up changes. By its shortcut, SAVE + OCT-,
  then OCT+ to the question. SAVE is held past HOLD DELAY first, its layer open, and seen held (a state
  asked after it) before OCT- comes: a SAVE let go after its layer, or after a combination, is no tap, so it
  never saves, whatever happens (a SAVE and an OCT- seen in one main loop pass would be OCT- first, then a
  SAVE alone). Under RUN, small and dim: `AUTOTESTS DROP UNSAVED CHANGES: SAVE YOUR WORK FIRST`, and
  `HANDS OFF THE FM-1 WHILE IT RUNS` (its own panel stays live); no question.
- **They never save:** SAVE never tapped, SAVE + OCT+ never pressed, a question's yes only to REVERT's. Nor do
  they run what writes the flash outside the edits (a sampler's CLONE or MOVE, seeding), which REVERT
  could not take back. Everything else
  is theirs, as a hand's: screens, the transport, the keys, FX knobs, DJ's stand, the tools that only change
  edits.

**The AUTOTEST part:**

- **The scenarios** in their groups, each with a tick box; a group's box ticks its scenarios. **Ticked at
  first: all but FILES,** the scenarios whose point is a file (a recording this page makes of the FM-1's USB
  audio, a WAV in the run's ZIP; screenshots later): a run by default makes only its text report. No scenario
  needs a hand. What a FILES scenario checks besides its file is in a scenario of its own, ticked (USB's AUDIO
  OUT). **DEFAULT** ticks those again,
  **ALL OFF** unticks every one (to tick a few): two small keys (12 px), only ticks; both wait while a run goes.
  **RUN** runs the ticked ones in
  order, those taking the FM-1's input first (AUDIO OUT, FILES'): the browser's prompt for the input comes at
  once, while the hand that pressed RUN is still at the page (a browser keeps a once-only answer only minutes
  while the page is out of sight and not using the input), **STOP** stops at once: every wait looks at it at least every 0.2 s, a measured stretch, a prompt
  and the bench included.
- **A run holds LISTEN,** its switch locked from RUN to the run's end: off and the FM-1's input let go at
  RUN and at every scenario's start (the FM-1 then sends no USB audio, which costs it, while they are
  measured), on for the scenarios that take the input (AUDIO OUT, FILES': heard on the computer while they
  run). After the run LISTEN is as it was before RUN (the browser may ask for the input again).
- **A FILES scenario records itself:** it takes the input as AUDIO OUT does, and the page records it from its
  start to its end into a WAV of its own, in the run's ZIP (as below); the report says how long it got and what
  the recording holds (the test tone's 997 Hz left, 1,499 Hz right). With RECORD AUDIO off, only these are
  recorded.
- **RECORD AUDIO** (off at first, not remembered): the run records each scenario's sound, from its start to
  its end, into a WAV of its own (16-bit stereo, about 11 MB a minute, kept in the page until saved). The page
  takes the FM-1's input at RUN (the browser's prompt first, if it asks) and keeps it all run, so the FM-1
  sends USB audio all run, about 8 % of its CPU, which the figures then include: the report says so, and that
  nothing was recorded when the browser was not allowed the input. The run's line in the reports' column says
  what was kept (`audio: 2 WAVs, 8.3 MB`).
- **A run's ZIP** (once it is over) saves the run as one file (`ptfm-autotest-YYYYMMDD-HHMMSS.zip`, stored, not
  compressed): its report (`report.txt`) and the WAVs recorded, numbered in the run's order
  (`01-songs-idle.wav`); a run without audio gives the report alone.
- **While it runs:** the scenario and its step, the checks so far, and two counters. On the running
  scenario, its test: `SETTING UP` while the FM-1 is set up for it, then from its first measured stretch (or
  the bench) `00:42/02:00`, the test's time so far and what it takes. In the AUTOTEST column's head the run's,
  whole scenarios (what is left counted as it is expected to take). What a scenario or its test takes is its
  time in the last run on this browser it passed in, else its own estimate; a kept time goes when the
  scenario's own estimate has changed since (a scenario made shorter or longer). **A prompt** (a scenario asking
  for a hand: none does now) holds it with its text and a **CONTINUE** button.
- **Its timing holds with the page out of sight:** a browser slows a hidden or covered page's own timers to
  about one a second (later one a minute), which turned a 60 ms tap into a hold and stretched what was
  measured. The page's waits, its time-outs and FIGURES' question run on a worker's timers, which keep their
  time; a press's release is sent by the MIDI port at its own time; a measured stretch ends by the run's clock.
- **The run's clock waits for the user:** while the browser's prompt for the FM-1's input is up, or a prompt
  waits for CONTINUE, both counters stand still, and the times in the report and those kept for the estimates
  leave the wait out (the line under way says `THE RUN'S CLOCK WAITS`).
- **Each run of autotests makes its own report,** listed with **COPY** and **DELETE**.

### A scenario

A list of steps, with checks between them.

- **Every scenario starts from a known place,** whatever the last one left: HOME tapped until the state shows
  no overlay, what plays stopped (DJ on DJ, LOOPS on LOOPS, a song on SONGS), ARP off, all sound off (HOME +
  REC), REVERT; then the screen of its own choosing.
- **A song is started from silence:** all sound off right before its PLAY, so what played before (released
  notes, the effects' tails) never rings into it, nor into what is measured (the twelve loops one after
  another, the DJ stand then MIDNIGHT RUN). The bench starts from silence too.
- **Input:** a tap (held 60 ms), a hold (for a time, or past the state's HOLD DELAY), a combination (one
  control held while others go: the held one first and 60 ms between each press and release, as a hand's,
  since what arrives within one main loop pass is seen at once), keys held and let go, knob clicks (50 ms
  apart), MASTER. After a button is let go the next input waits out COMBINATION GRACE (from the state), or
  it would combine with that button (a key just after FX is FX + the key). Clicks that come
  within one main loop pass count as a fast turn, as a hand's do, and some knobs then take bigger steps: an
  exact value is reached by seeking.
- **Seeking:** a knob turned a click at a time, 120 ms apart (slower than the knobs' acceleration, which can
  count a click twice under 90 ms), until the state shows what is wanted (a song by its name, a cartridge, a
  deck's filter, the crossfader), at most a set number of clicks: the FM-1 is never assumed to start where
  the page thinks.
- **Waiting:** a time, or until the state shows something, at most a set time.
- **Setting what it depends on:** every setting a scenario needs (VOICES 16 for sixteen keys, SYNC INTERNAL,
  DJ's effects) is set as an edit and checked; the next REVERT takes it back. REVERT brings back the saved DJ
  stand, whatever it is, so every DJ scenario first sets DJ's properties as a new store has them (CROSSFADER
  SMOOTH, FILTER RESONANCE 30 %, FILTER THE SENDS ON, FX LFO REVERB MIX) and sets, or switches off, every DJ
  FILTER, LFO and effect it does not measure.
- **Checks:** the state after a step; on the debug build also FIGURES. A check that fails ends the scenario
  (`FAILED: EXPECTED SONGS, FOUND INSTRUMENTS`), all sound off, and the run goes on with the next scenario.
- **Measured stretches:** named parts whose FIGURES (the debug build) go into the report, each laid out as a
  RUNS report is. While one is measured the page sends FIGURES' question once a second and asks the state
  every 2 s (no answer while a progress bar holds the main loop: no look then). A question no scenario asks
  for (`MEMORY FOR EDITS IS FULL`) ends it and fails the scenario with the question's words; so does the
  edit memory ([connections.md](connections.md#the-state)) filling past three quarters while it runs, with
  its figures. Input going on meanwhile (knobs swept, keys held, DJ BUSY's steps) ends with its scenario,
  failed or stopped, before the controls are let go.
- **After each scenario** (passed or failed) a note gives the edit memory it left: filled, of its size, and
  what the changes take. When the figures show the FM-1 sending USB audio in a scenario that does not take
  it (something else on the computer holds the FM-1's input), a note says in how many windows: its cost is
  in those figures. A stretch of 5 s or more that got fewer than 0.8 FIGURES windows a second (USB lost the
  rest) gets a note too: its figures cover only those, and of the lost how many the FM-1 could not send
  ([USB audio's byte 1](#usb-audios-bytes), between the stretch's first window and its last; the others were
  lost on the computer's side).
- **Prompts,** for what the page cannot do (recording on the computer), and **notes** for the report.

### The scenarios

Times are what each measures; those ticked at first take about 20 minutes.

| Group | Scenario | What |
|---|---|---|
| SONGS | IDLE | from KEYS' start (below): nothing plays, 10 s |
| | IDLE, THE SCREEN STILL | the same with FREEZE THE SCREEN on (nothing drawn between the halves; the debug build's only), 10 s, every window checked frozen: what the screen's drawing costs the sound (its code taking the code cache from the sound's, fetched again at the next half) |
| | IDLE AFTER A SONG | PUNCH IN from position 02 for 10 s, paused, 5 s on, then 10 s measured: its DELAY and REVERB at rest ([ui/screens/fx.md](ui/screens/fx.md#effects-at-rest)), so the CPU is IDLE's |
| | each demo song | PUNCH IN, SPICE ROAD, MIDNIGHT RUN, CITY LIGHTS, ACID TEST, ADVENTURE, each from its heaviest stretch (*(proposed)* for now position 02 for PUNCH IN, 03 for the others), 20 s (a pattern change: 8 bars take 13-20 s at their tempos) |
| | each DJ loop | the twelve, a scenario each: from its last position (ALGORITHM turned up till it moves no more), usually a loop's heaviest, 20 s (a pass of an 8-bar pattern at 96 BPM or more) |
| KEYS | each engine | one start for every engine, whatever the scenario before left: WORLD LOOP played a moment from its start, paused and all sound off, so the keys sound through its effects, DELAY and REVERB (checked at work with the first keys down), as the KEYS runs measured before; VOICES 16; 1, 4, 8, 12 and 16 keys held (past 10 on core 2) on a DX7, a Juno-106 resonant and one plain, and a sampler whose instrument loops (03:08, so it sounds all the time it is held; the user area's, skipped if it is not there); the TB-303 one key on the keys (they play it as one voice, legato: more keys add nothing), then 4, 8 and 12 TB-303 voices as loops on LOOPS (a voice a lane): the loops cleared (SEL's CLEAR LOOPS), then POLY loops of 4 lanes recorded on `+ NEW LOOP`, one for each four voices, chords of four tapped on every row of a bar (a 303 line in sixteenths, QUANTIZE `ROW`), the first take starting the loops (then LOOPS' DELAY and REVERB switched on, as the keys had them, and checked at work back on LOOPS: not at first, noted with what played, the screen and the effects at work, and how long they took; not within a second, the scenario fails with the same), the others armed while the loops play; on a lane a 303's gate closes half a row after its note, so each voice sounds about half of every row and a row's start has them all; 5 s each |
| EFFECTS | a song with effects | a heavy song with CHORUS, DELAY and REVERB on, their knobs swept on FX LIVE (three times), 30 s |
| | the punch-ins | punch-ins held one after another over it |
| | METRONOME and COUNT-IN | a song with the METRONOME on, started through a COUNT-IN |
| DJ | the stand | TECHNO LOOP and ELECTRO LOOP synced at 126.0 BPM; the crossfader SMOOTH at 6 of 32 with its LFO (TRIANGLE, MIRROR, `1/2`); the DJ FILTERs at 30 % resonance, FILTER THE SENDS on, the left LP 29 with its LFO (TRIANGLE, HERE TO FULL, `1/2`), the right HP 25 with its (TRIANGLE, SWING, `1/2`); the FX LFO on the REVERB's MIX (RANDOM, 0 TO 100 %, `1/2`); DJ's DELAY and REVERB on, its CHORUS off; 30 s (steady: the LFOs turn each second, the loops come round twice) |
| | DJ busy | each action in a stretch of its own, done while it is measured, so each has its own figures: a LOAD at the bar (8 s), SWAP (4 s), BRAKE, BACKSPIN, ECHO OUT, REVERB OUT (each held 2 s, 5 s), the crossfader swept (4 s), MOVE (4 s), JUMP (6 s); 46 s |
| | DJ effects and punch-ins | the stand with DJ's CHORUS on too (DELAY and REVERB already), each white key's punch-in held 2 s over it, 32 s |
| | DJ two tempos | TRANCE LOOP and DISCO LOOP, SYNC off, the crossfader in the middle, no DJ FILTER and no LFO, DJ's DELAY and REVERB on (its CHORUS off), each deck's tempo turned on its own (K1 the left's, K4 the right's, up and down by turns), so the decks are never in step, 30 s; both still playing at the end, each at its own tempo |
| | DJ heavy | two demo songs mixed, the heaviest pair: PUNCH IN on the left, MIDNIGHT RUN on the right, SYNC on, the crossfader in the middle, no DJ FILTER and no LFO, DJ's DELAY and REVERB on (its CHORUS off), VOICES 16 (the notes past 10 on core 2), 30 s; both still playing at the end |
| LOOPS | the loop station | VOICES 12; the loops cleared (SEL's CLEAR LOOPS), then twelve loops recorded from the keys: the first armed by REC on `+ NEW LOOP`, started by a note and closed by REC on its bar line (a bar), each next one REC on `+ NEW LOOP` while the loops play (armed, its take from the loops' start), a note held over its pass; JUNO-106 sounds for the first six, DX7's for the rest (each the next with PRESETS); all twelve playing, 20 s |
| | a beat | VOICES 12; the loops cleared, then a beat as a hand builds one, every step checked through LOOPS' cursor in the state: the first loop from the pattern recorder (ARP; a kit, a drum a row: KICK, SNARE, HAT, HAT OPEN; TIMES ×1; REC writes it, a bar, the loops starting); a TB-303 bass line taken on `+ NEW LOOP` (MONO, REC armed while the loops play); a chord of four on EL PIANO (POLY, 4 lanes); 20 s measured; a re-record (EDIT on the bass's loop: its notes gone at once, a new line taken); a peel (LFO on `+ NEW LOOP`: the last loop, the pad's, goes); CONTROL mode (SEQ): white key 1 tapped (loop 00 muted at once), white key 2 held (loop 01's mute waiting for the bar line, then muted), GLO's black key to SOLO, white key 1 (loop 00 soloed), PIT (all back on); 10 s measured, the loops playing on |
| ONE CORE | IDLE, ONE CORE | IDLE with core 2 off (38 SECOND CORE 0 before, 1 after; the debug build's only, as 38 is), 10 s: the two figures to compare; every window's 37 must say core 2 off by 38, no block handed over |
| | DJ HEAVY, ONE CORE | DJ heavy the same way, 30 s |
| | KEYS AT 12, ONE CORE | the KEYS engines' 12-key steps (DX7, Juno-106 resonant and plain), core 2 off, 5 s each |
| CHIP | the chip | [the bench](#the-bench): THE CORE and ALONE (on core 1, no block handed to core 2 meanwhile) |
| TOOLS | AUTO LEVEL ALL | SETTINGS' tool over every instrument (its question answered LEVEL), measured while it runs (the main loop's stack) and ended by its end (the state answered again, its progress bar gone; at most 10 min), which is checked |
| | CHECK REFERENCES | SETTINGS' tool (it writes nothing), measured till its progress bar is gone: `NO MISSING REFERENCES`, or the list of what is missing (noted, not a failure: a user's own songs may point at things deleted), closed by HOME |
| | CHECK STORAGE | SETTINGS' tool (it writes nothing), measured till its progress bar is gone: `STORAGE OK` is checked; a list of problems fails the scenario and is closed by HOME |
| | RECORD PATTERN | in LOOPS with no loops, ARP's pattern recorder: four steps on the white keys, its preview measured (ARP), REC writes them as the first loop (`PATTERN: LOOP 00, 1 BAR`) |
| UI | THE EDITOR'S OWN PATTERN | the EDITOR, SEL's NEW PATTERN (the cursor's pattern `FE`, empty); REC on, four keys tapped (it holds notes), REC off; PLAY 4 s and again; ENV, another pattern picked: the warning (`EDITOR CHANGES WILL BE LOST`) answered NO (still `FE`, its notes kept), then again YES (on the one picked, `FE` empty); SEL's LOAD A PATTERN, PUNCH IN's first pattern (`FE` holds notes, `PATTERN xx LOADED`); SEL's STORE IN A PATTERN, `+ NEW PATTERN` (a new number, `STORED IN PATTERN xx`); each step checked through the EDITOR's cursor in the state; REVERT ALL drops it all after |
| | NEW SONG | SONGS, SEL's NEW: the list `NEW SONG STARTS WITH`, `+ NEW PATTERN`: a new song is current and there are unsaved changes; REVERT ALL drops them (no unsaved change left, the song gone) |
| | THE KEYS ON LOOPS | LOOPS with no loops (SEL's CLEAR LOOPS), LOOPS' DELAY switched on (an edit), nothing playing: a key held, the state shows the DELAY at work and LOOPS' tempo; on SONGS, a key held: LOOPS' DELAY not at work |
| | LOOPS TO A PATTERN AND BACK | LOOPS with three loops (the pattern recorder's drums, a MONO bass, a POLY pad); SEL's SAVE TO PATTERN, `NEW` (`LOOPS STORED IN PATTERN xx`); SEL's CLEAR LOOPS, answered CLEAR (`LOOPS CLEARED`); SEL's LOAD FROM PATTERN, that pattern (`PATTERN xx IN LOOPS: 3 LOOPS`): the same length, three loops and the notes all the loops start as before, checked through LOOPS' cursor in the state; REVERT ALL drops it all after |
| | RECORD LIVE | LOOPS with those three loops playing; SEL's RECORD LIVE (its panel), REC (ARM), REC: it starts at once; EDIT refused (`RECORDING A SONG`); loop 00 muted (CONTROL mode), a punch-in held (FX + a white key); PLAY: the loops silent, the song going on; PLAY: on again; REC: it ends at once (`SONG xx RECORDED: 0:05, 1 PATTERN`); the new song checked on SONGS (its one position, LOOPS' tempo, named `LOOPS ##`); REVERT ALL drops it after |
| USB | AUDIO OUT | the page takes the FM-1's USB audio input itself (the FM-1 then sends it) and LISTEN plays it; the browser asks for the input then (it keeps no answer for a page opened from a file), its prompt waited for 2 minutes, then the scenario is skipped (a run left alone goes on); the stand 30 s and a heavy song 20 s measured, the test tone 5 s; the debug build: [the USB test tone](#the-usb-test-tone) switched on and off, and the figures must show USB audio in at least half the windows (else: pick the FM-1's input in the browser); the input let go after; no file (skipped when the browser is not allowed the input) |
| FILES (unticked at first) | USB RECORDING | the page takes the FM-1's input (LISTEN plays it) and records the stand 60 s and a heavy song 30 s into a WAV in the run's ZIP; the recording must not be empty (skipped when the browser is not allowed the input) |
| | USB TONE | [the USB test tone](#the-usb-test-tone) recorded so, 30 s |
### An autotest run's report

Plain ASCII, lines under 100 characters (a longer one goes on in the next, indented: nothing is cut): the
firmware and its build, the time, the scenarios ticked, RECORD
AUDIO when it was on (or refused the input); then each scenario: PASSED, FAILED and why, or SKIPPED and why, its
time (`00:19 setting up + 02:00` once its test started), its checks, its notes, its measured
stretches laid out as RUNS' reports, and CHIP's tables (THE CORE in cycles at 360 MHz, or MB/s, or µs; ALONE
in µs a block, warm best, mean, worst, cold, and its share of the block's 1,451 µs).

### Tested first on the computer

Every scenario runs against the firmware compiled for the browser with the debug build's figures, through
MIRROR and the state, every check passing, before it reaches the FM-1 (`fw/tests/run_autotest.sh`). The
scenarios that turn, pick and hold by hand run once more with every main-loop pass 100 ms long, as slow as the
FM-1's while a list or DJ redraws: a step that leans on a fast pass fails there first. A knob's turn ends with a
look at the state, which the FM-1 answers after taking its input, so a button pressed next never shares a pass
with the clicks (that would make it the button held + the knob: a combination, no tap).

### Later

Screenshots (the debug build pauses its screen, draws it once more and sends it band by band as palette
numbers, compressed; the page makes a PNG); the page recording the USB audio input itself, a WAV a
scenario with a quick look at it; debug switches for USB to carry exactly the jack's sound, or the FM-1's
own samples unresampled for comparing with a computer's render; reports saved as HTML with their
screenshots, recordings beside them.

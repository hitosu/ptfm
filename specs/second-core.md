# The second core

Status: **settled** (2026-10-22). The record: [the second core's proposal](proposals/202610220009-second-core.md),
and for the voices on core 2 [its own](proposals/202610222227-voices-on-core-2.md).

The AC791N has two cores at the same speed. **Core 1** runs everything it always has: the interrupts, the main loop,
every flash access and every peripheral, the replays and up to 12 voices (10 at VOICES 16). **Core 2** runs each
audio block's effects and its output stage, handed over block by block, and at VOICES 16 the voices past 10: up to
six, so 16 notes are heard at once ([the voices on core 2](#the-voices-on-core-2)). The screens call them CPU 1 and CPU 2 ([ui/frame.md](ui/frame.md#the-header),
[ui/screens/status.md](ui/screens/status.md)); the chip numbers them 0 and 1 (its `cnum`), and code that touches the
chip says which numbering it uses.

## What runs where

**Core 2, for each block of 64 frames:**

- **the effects:** the block's events (the changes to the effects' own state the block made: a click, a punch-in,
  all sound off, CUT's emptying), its settings and its music clock put in place, then the effects' work on the
  block's mix and sends, or the rest path when the block is silent and the effects at rest
  ([ui/screens/fx.md](ui/screens/fx.md#effects-at-rest)). On DJ: DJ's shared effects (CHORUS, DELAY, REVERB,
  LIMITER, the METRONOME) and the punch-ins;
- **the output stage:** the keys heard dry joined to the mix, CUT's fade, USB audio's stream (the mix at USB GAIN,
  resampled to the computer's clock), the soft ceiling, SYNC PULSE, VISUAL's ring, and the block converted into the
  codec's half (LINE GAIN, MASTER, mono);
- **the voice job,** when any of the pool's places 11-16 sounds at VOICES 16: those voices rendered one after another
  into a block buffer and mixed into mixes of core 2's own ([the voices on core 2](#the-voices-on-core-2)).

**Core 1, everything else:** the half's start (the requests, a chase's share, a deck's start made ready), each
block's control, the replays, the voices in places 1-10 (1-12 below VOICES 16 or without core 2), adding core 2's
mixes to its own, DJ's decks' mix (each deck's GATE,
FILTER, DRIVE and LO-FI, the DJ FILTERs, VOLUME and the crossfader), the music clocks, each block's copy of the
effects' settings, MIDI; the main loop, every interrupt, every flash access, every peripheral.

**Core 2 and the flash.** The effects' and the output stage's code, their constant tables and core 2's stacks are in
RAM ([RAM](#ram)); the voice job runs the engines' code in the image and reads the voices' data (patches, samples)
in place from the flash, as core 1 does. Core 2 must not touch the flash, code or data, while core 1's erase,
program or direct read has the code window off: that wedges the flash path, both cores stopping until the watchdog
(measured: [hardware.md](hardware.md#the-flash)). So **every flash operation of core 1's that switches the code
window off waits until core 2 is idle,** with the interrupts off so that no job is handed over meanwhile: the store's
erases, programs and direct reads, the seeding's, the update's own (its staging), the debug build's bench and PROVE,
and the audio context's direct-read path (which no voice reaches: the user area and the built-ins read in place). A
wait lasts at most what core 2 was handed, a fraction of a millisecond. Two short paths of core 2's run from the
image besides the voice job: its entry at a start and its fault handler's first steps (the one vector table);
neither runs while the flash is busy (core 1 starts it, and every flash operation waits for it to be idle).

## When

A half is four blocks. Core 1 makes block 0 up to its effects (its control, the replays, the voices, the clocks,
its copy of the settings) and hands it to core 2; while core 2 works out block 0's effects and output, core 1 makes
block 1, and so on. After the last block core 1 returns from the interrupt at once: the half being made plays only
after the one playing now has ended, 5.8 ms after the interrupt, so core 2 finishes the last block long before the
codec reaches it.

```
interrupt                                                    the codec reaches this half (5,804 µs)
|                                                                                      |
core 1  [start][block 0][block 1][block 2][block 3] (returns: the main loop runs)
core 2           [fx+out 0][fx+out 1][fx+out 2][fx+out 3]
```

- **First thing in the audio interrupt, core 1 makes sure core 2 has finished every block handed over before** (it
  has, but in an overload). Only then does core 1 change anything core 2 works on ([who owns what](#who-owns-what)):
  the half to write into, the output's gains, USB audio's steering, the requests.
- **The same sound, sample for sample:** each block's effects run in order with the settings, the events and the
  clock that block had, as they do on one core block by block. The latency is the same.
- **On core 2 the effects take a block at a time.** On one core they take a half's four blocks together, each
  effect's code once ([files/ram.md](files/ram.md#where-the-ram-goes)), because code fetched from the flash costs;
  core 2's code is in RAM, where that saves nothing, and a block at a time is what lets core 2 start before the
  half's voices are all made.
- **CUT's halves keep the effects on core 1:** while CUT fades the sound out, each block's fade decides what the next
  block's voices feed the effects, so the blocks go one after another; faded out, the effects only keep time. A
  block's voice job still goes to core 2 (core 1 waits for it, mixes, then runs the block's effects and fade):
  16 voices and the effects on core 1 alone would overrun those halves.
- **A late half** is one the codec reached before it was done: core 1's interrupt ran past it, or core 2 had not
  finished its last block when the next half began. The debug build counts them apart ([debug.md](debug.md#the-figures)).

## The voices on core 2

**The pool has 20 places:** at VOICES 16 places 1-10 are core 1's and 11-16 core 2's; at 8 and 12 every place
sounding is core 1's. The places past the count hold the parked notes, four at VOICES 16 and 12, eight at 8
([ui/screens/settings.md](ui/screens/settings.md#voices)): below VOICES 16 the pool holds 16 notes at most. Measured
on the FM-1: with eight places past 12, DJ HEAVY on core 1 alone kept more notes sounding (parked ones coming back:
11.3 on average against 10.5) and ran late more often (117 halves in 30 s against 68). While core 2 does not run
(SECOND CORE off, before its start, after a fallback) the limit in force is 12, all on core 1. A new note takes a free
place of core 1's first, core 2's only once all 10 of core 1's sound.

**Each block, core 1:**

1. makes the block's control (the requests' share, the replays' ticks, the notes on and off, the levels and pitches,
   the parked notes back, the Juno-106's noise for the block);
2. when any of places 11-16 sounds, works out how each of those voices goes into the mix this block (its gain ramp,
   pan, sends and Juno-106 chorus bus, as it would mixing it) and hands core 2 the block's **voice job**: those
   voices, rendered one after another into core 2's block buffer and each mixed, as core 1 mixes, into a mix of core
   2's own (a replay's stereo mix, its sends and chorus buses, or the keys' heard dry), writing each voice's scope and
   peak;
3. renders its own voices and mixes them into the block's mix, sends and buses, place by place;
4. waits for the voice job (core 2 runs the block before's effects first, then the voices), then adds core 2's mixes
   into its own and does the rest of each core-2 voice in their places' order (its peak into DUCK and the meters, its
   end);
5. hands the block's effects and output to core 2, as before.

```
core 1  [control 0][post V0][voices 1-10][add V0][post FX0][control 1][post V1][voices 1-10][add V1] ...
core 2             [V0: voices 11-16, mixed]          [FX0 + out 0][V1: voices 11-16, mixed]
```

- **The same sound, sample for sample,** as one core rendering and mixing the same 16 voices: the mixes are whole-number
  sums, so adding core 2's mix to core 1's gives what mixing every voice into one gives, in any order. Core 1 decides
  everything that goes into the mixing (the gains, pans, sends, the mutes, the buses) and keeps every decision drawn
  from it (DUCK, the meters, the end of a voice). A voice of places 11-16 whose mute fades this block is not handed
  over: core 1 renders and mixes it in its place, as its own, after the job. With 10 notes or fewer nothing changes at
  all.
- **Why core 2 mixes too:** at 16 voices core 1 mixing all of them was what made DJ HEAVY run late more often than at
  12 (measured on the FM-1: MIX 528 against 442 us a half, 8 late halves in 30 s against 4); core 2 mixing its own
  takes their mixing off core 1 for the cost of adding core 2's mixes. Measured on the FM-1 with 12 + 4: with 16 keys
  held, core 2's four voices cost core 1 about 2.7 % of a half instead of about 5 %; in DJ HEAVY almost nothing, as
  core 2's voices there go into some four of the two decks' sends, chorus buses and mixes each block, and adding each
  back costs core 1 nearly what mixing those voices did.
- **Why 10 + 6:** in DJ HEAVY at VOICES 16 (12 + 4) core 1 ran at 67 % of a half on average, 109 % at worst, core 2 at
  28 %, 37 % at worst: the late halves were core 1's (a half's last block reaches core 2 too late when core 1 runs
  past ~95 %). Each voice moved to core 2 is about 4 % of a half off core 1 there.
- **A note keeps its place while it sounds;** it moves from one core to the other only by parking: a note parked from
  core 2's places comes back into the first place that frees, of either core. When core 2 stops running, the notes in
  places 11 and 12 sound on, on core 1.
- **Without core 2** (SECOND CORE off, before its start, after a fallback) a note in places 13-16 is parked, fading
  out on core 1, which renders it for that fade; 16 comes back when core 2 runs again.
- **A fallback during a voice job** (stuck, a misread, a fault): core 1 holds core 2, that block's voices of core 2's
  are left out of the mix (one block, 1.45 ms, unheard). A job core 2 never started: its voices are parked, their
  state as it was. A job core 2 started: its voices are let go (gone, as a note cut), as core 2 may have left their
  state half done. Core 1 never renders a block core 2 was handed: it would carry 16 voices and the effects in that
  block, from that same state.
- **The time core 1 waits** for the voice job counts in core 1's share; the job's own time in core 2's.

## Who owns what

Every datum the sound uses belongs to one core at a time:

- **Handed over with a block** (its slot): the block's mix and sends, the keys' dry mix, the copy of the effects'
  settings, the music clock, the events, the block's flags (silent, sends, DJ's or the song's effects, SYNC PULSE's
  frame, whether its buffers still hold zeros from a rest), and where in the codec's half it goes. Core 1 writes them
  before the hand-over and does not touch that slot again until core 2 has finished it.
- **The effects' settings, live and working.** What the requests, the commands and the screens change (the effects
  on, their knobs, the chain, the routing, the METRONOME's, BYPASS, the forced DELAY / REVERB) is core 1's, the live
  settings; what the effects' work reads is a working copy, put in place from the block's copy at the start of each
  block's job. Core 1 never writes the working copy; core 2 never reads the live settings.
- **The effects' own state** (their lines, filters, LFOs, envelopes, the punch-in buffer, the METRONOME's click, the
  LIMITER's gain) is core 2's while it runs. Core 1 changes it only through a block's events, or at a half's start
  once core 2 has finished; a change that cannot wait for a block (the events' queue full) waits for core 2 first.
  The queue holds 64 events, far more than a half makes (a punch-in, a click, all sound off, a setup's end each
  take one), so in practice it is never full; a change made at once past it would land after the blocks core 2
  already took, the one place one core and two could sound different.
- **A voice handed over in a voice job** (places 11-16) is core 2's from the hand-over until core 1 has seen the job
  done: core 2 runs its engine (its state, its scope) in its block buffer and writes core 2's mixes, and core 1
  touches none of them meanwhile. Core 1 makes every change to it (a note on, off or cut, a level, a pitch, a
  parameter command, a park, how it goes into the mix) in the block's control, before the hand-over, and reads its
  peak, its end and core 2's mixes after the job.
- **The engines' shared state:** the DX7's operators' buses are one scratch per core (the engine takes the scratch of
  the core it runs on); the Juno-106's noise generator runs on core 1 for the block before the voice job is handed
  over, and core 2's voices only read it; the engines' tables (the DX7's built at power-on in RAM, the others in the
  image) are read only; the counters the voices keep for the figures (each engine's time and blocks) are the voice
  job's own, added by core 1 as it mixes.
- **DJ's decks' own effects** (each deck's GATE, FILTER, DRIVE and LO-FI) run on core 1 with the deck's own knobs
  and state handed to the effects' code, never through the working settings or the mix's own state; what they
  measure and show (their time, the effects at work, FILTER's picture) is counted apart from core 2's. LO-FI's dust
  on a deck draws from a noise generator of its own, so core 2's METRONOME click (white noise) and the decks' dust
  never share one: the one difference from the single core of before, heard only on DJ with a deck's DUST and the
  METRONOME's CLICK sounding together, and in the noise after such a DJ (the same kind of noise, not the same
  samples).
- **The output's state** (CUT's gain over the blocks, SYNC PULSE's count, VISUAL's place, USB audio's resampler and
  stream) is core 2's. USB audio's stream is a ring with one writer, core 2, and one reader, USB's interrupt on core
  1: the place moves only after the data are written, both published with the chip's sync.
- **What the main loop reads of core 2's** (the effects at work, the LIMITER's gain, GATE's step, FILTER's envelope,
  VISUAL's ring, the effects' times) it reads as it is, a word at a time: for the screens only, never to decide
  anything the sound depends on.
- **The main loop's locks** hold off the audio interrupt, and wait for core 2 to finish what it was handed: the
  store's and the edits' sections change what the voices read (their patches and records), which a voice job may be
  reading. The effects' changes go through the requests, which the audio interrupt takes at a half's start. The
  debug build's bench runs its pieces on core 1 with no block handed over meanwhile. **The figures core 2 counts** (its stages,
  its time) are its own, per half: core 1 reads the half before's at the next half's start, once core 2 has
  finished it (so never half-counted), adds them to that half's own and clears them.

## The hand-over

A queue of a half's blocks in RAM: each entry its block and its number, and a check word made of both. Core 1
writes an entry, then the count of entries handed over; core 2 runs the next entry only when its number follows the
last one it finished and its check word matches, and writes the count it finished. Each core's words sit on a cache
line of their own. An entry whose check fails is a misread: core 2 counts it and leaves it. Core 2 spins on the
queue in its loop in RAM when it has nothing to do; it takes no interrupts.

## Start, switch, fallback

- **The start:** 2 s into every start of the firmware (after a power-on, an update's restart or a crash's restart),
  with SECOND CORE on ([ui/screens/settings.md](ui/screens/settings.md)) and no crash with core 2 running recorded
  (below); from the main loop, interrupts off around it (a few tens of microseconds: its stacks marked, its queue
  cleared, then the start itself, which answers in under a microsecond). Core 2 starts through the chip's ROM,
  so the PC limits are opened for the start and armed again once it has answered from its loop in RAM; its entry
  word is written into the chip's top for the start and put back after. One try, without the clock-divider bit the
  vendor's start sets (never needed on the FM-1; a second try would be a start after a hold). Core 2 then sets its
  own stack limit.
- **Held at three moments only:** first thing at every start; before every reset (the crash screen's, an update's,
  the chip's USB update mode, the debug build's watchdog test); and by a fallback (below). Always with the safe hold:
  the bus traps off around it, the hold's own bus flag cleared. Never for flash work or anything else: a hold is final
  until the next start (a start after a hold faults in the ROM; a restart of the chip clears that).
- **SECOND CORE works at once both ways.** OFF: core 1 stops handing blocks over from the next half and runs the
  effects itself, the limit in force 12; core 2 waits in its loop. ON: blocks are handed over again from the next
  half, and VOICES 16 is 16 again; if core 2 has not started yet in this start (SECOND CORE was off at the start), it
  starts then.
- **Fallbacks,** each ending this start's use of core 2:
  - **no answer** within 20 ms at the start;
  - **a misread:** an entry core 2 refused;
  - **stuck:** at a half's start, the blocks handed over in the half before not finished, and still not after 1 ms
    more;
  - **a fault:** core 2's exception keeps a record in its own RAM and parks core 2 in a loop of its own in RAM (the
    park); core 1 sees it at the next half.

  A fallback holds core 2, and core 1 runs the effects from that half on: their state is all in RAM, so the sound
  goes on, at worst the blocks of one half lost (they play as silence); the limit in force falls to 12: the notes
  of a voice job core 2 had started are let go, the others in places 13-16 parked ([the voices on core
  2](#the-voices-on-core-2)). The toast `SECOND CORE STOPPED` once, on
  any screen; STATUS' DEVICE page says why ([ui/screens/status.md](ui/screens/status.md#device)).
- **A restart with core 2 running that was not ours:** a word kept across restarts says core 2 runs; it is set
  before the start's first try and cleared by every fallback, a failed start and every restart PT/FM makes itself
  (an update, the chip's USB update mode, the debug build's watchdog test). A start that finds it still set, after a
  restart that was not a power-on (a crash, the STOPPED screen, the watchdog, a crash while core 2 was starting),
  leaves core 2 off until the power goes off (`STOPPED: AFTER A CRASH`): the word lives in the RAM kept across
  restarts, which a power-off clears, so a fault core 2 caused cannot loop. The boot guard is unchanged: two failed
  starts in a row end in the USB rescue; a crash core 2 caused counts once, and the next start, on one core, is
  healthy.
- **The supplies stay at the boot loader's levels.**

## RAM

**Core 2's room** is a part of its own at the top of the RAM's data region, under the main loop's stack's guard
band, 40 KB, of which today's code leaves about 2.5 KB ([files/ram.md](files/ram.md#the-layout)):

- its code: the effects, the output stage, USB audio's stream writer, its loop, its fault handler's park, its own
  `memset` / `memcpy` and the timer read; and their constant tables, about 30 KB together, all copied from the
  image at power-on, as the flash driver's code is;
- its stacks: the jobs' (6 KB) and the exceptions' (1 KB), filled with a mark at each start (their depth); the voice
  job's deepest and the effects job's never nest, and the build works out both;
- its queue, its counters and its fault record (164 B).

**The voice job lies in the image,** with the engines it runs: core 2 fetches it from the flash through the shared
code cache, as core 1 does (the same engine code on both cores costs nothing more). Its block buffer (256 B, each
voice in turn), its three mixes (a replay's stereo mix, sends and chorus buses each: 5,376 B) and the four more
places of the pool (2.6 KB) are in the data region ([files/ram.md](files/ram.md)).

**One copy of the code, reached through pointers across the gap:** RAM lies out of a direct call's reach from the
image and the image from RAM, so the code in core 2's room calls only code in its room (its own `memset` / `memcpy`
under names of their own, never the compiler's implicit ones, and its own timer read), and core 1 reaches the
block's job and the effects' entry points in RAM through pointers, as it reaches the flash driver's code. When core 1
runs the effects itself (SECOND CORE off, CUT, a fallback, before the start) it runs that same code in RAM: the same
sound on either core by construction. What core 1 alone calls (the effects' commands, setups, the live settings, the
screens' questions) stays in the image.

Placed by what the chip does:

- its loop, its queue and its park lie at or above 0x01C72000, where neither a restart nor the update loader writes,
  so a core 2 a reset did not hold would only spin there;
- the ROM's start stack (0x01C6F800-0x01C70000) lies inside its jobs' stack, which holds nothing at a start;
- core 1's write limit (its fourth window) covers its code once copied;
- the PC limits' RAM window, the second of the two, reaches from the flash driver's code at 0x01C00000 up to the end
  of core 2's code (the data region between them inside it): the two cores share the windows, and core 2's code
  must run inside one.

**The build checks** core 2's room: its code calls nothing outside it and names no flash address (code or data), the
voice job being the one job in the image, it fits, its loop and queue lie above 0x01C72000, the ROM's start stack lies inside its jobs' stack, and the spare zone
keeps its floor. **Over the whole image** it refuses an instruction pair whose two halves write one register (the
device's compiler was seen pairing a dead load with a live write of the same register, which write wins unknown;
the fix where it fires is the source reshaped so the dead load goes). The stacks' worst case counts core 2's jobs'
stack apart from the interrupts'.

## The chip

What the start, the hold and the fault use (the chip's own numbering: core 2 is its core 1):

- **Start:** core 2 held; its interrupt bank (0x1EEF300, 32 words; core 1's at 0x1EEF100) all off; its EMU flags
  cleared (0x1EEF2D4 written all ones) and its stack limit off (0x1EEF2D0 bit 3 cleared: the ROM runs first, on its
  own stack, outside any window); its entry's address written into vector 126's word (0x01C7FFF8), the word the chip
  starts it from, the top's write limit let go around that write; the PC limits opened wide (the ROM is outside
  them); then C1_CON (0x1EEE004) bit 3 set and bit 1 cleared. It answers from its loop within 20 ms (0.41 µs
  measured), or the start has failed (no second try). After: vector 126 back to its fatal stub, the top's limit and
  the PC limits armed again.
- **Its entry** (in the image): interrupts off, no interrupt configuration, its stacks set (user: the jobs';
  supervisor: the exceptions'), then into its loop in RAM by a return from exception. **Its loop** first arms its own
  stack limit (its EMU bank, core 1's + 0x200: 0x1EEF2D8-0x1EEF2E4 the window over both its stacks less their lowest
  256 B, 0x1EEF2D0 bit 3 on, bits 2 and 16-20 off), then reports in.
- **The safe hold:** the bus traps (DBG_EN 0x1EEE340, bits 4, 5 and 16-21) off; C1_CON bit 1 set, then bit 3
  cleared; some 256 turns of a loop for the bus flag the hold raises; DBG_MSG_CLR (0x1EEE248) those bits; the traps
  as they were. Registers only, no calls: safe when core 2 never ran, and first thing at a start. **A hold before
  any start in this run does not stop the next start** (every start holds it first, and the starts after a restart
  succeeded so); a start after a start and a hold does.
- **Publishing:** the chip's `csync` after writing what the other core reads and before reading what it wrote; no
  cache upkeep for RAM.
- **Faults:** one vector table serves both cores; the fault handler asks which core it runs on (`cnum`): on core 2 it
  keeps the record in core 2's room (the vector, PC, RETS, its EMU flags, DBG, SP, the time) and jumps to the park in
  RAM through a register (RAM is out of a direct call's reach), never the crash screen or a reset. That holds for
  core 2's own EMU bank's faults (its stack limit: taken on core 2, as the FM-1 showed). **A bus error of core 2's**
  (a wild pointer) is flagged in the debug unit both cores share, whose traps core 1 keeps armed, and is likely taken
  on core 1: its crash screen, a restart, then core 2 off until the power goes off (core 2's record still says it
  runs). Contained, and it cannot loop; telling such a fault apart on core 1 and falling back without a restart is
  left for later.

## The emulator and the playground

One core, the same sound. The emulator runs the same code: each block's job, the voice job too, runs at once where
it is handed over, in the device's order, and its time counts as core 2's. The playground estimates core 2's share
from the effects' and the voices' costs as it estimates the rest. Both have the pool of 20 places with core 1's
first, VOICES 16 counted as 12 while their second core is off, and show the two columns, the two CPU figures, SECOND
CORE in SETTINGS (OFF moves core 2's share back into core 1's) and DEVICE's line (`STARTING`, then `ON` or `OFF`;
never `STOPPED`).

## Measured on the FM-1

At the supplies the boot loader leaves (1.40 V, 1.26 V):

- core 2 started late (the sound, the timers and USB running, interrupts off around the start) answers in 0.41 µs,
  without the clock-divider bit;
- a hand-over's round trip: 0.45 µs at least, 0.54 on average, 4.5 at most;
- core 2 is as fast as core 1, from RAM or from the flash (a Juno-106 voice's block 46.7 / 46.9 µs, a DX7 voice's
  22.5 / 22.7, the REVERB's block 121.8 / 120.5); the code cache is shared;
- no contention: core 1's pieces within ±1.3 % whatever core 2 runs;
- core 2 runs on from RAM through core 1's direct flash reads (9,442 in 2 s, no error);
- no misreads in 5 minutes of checking (5.1 million rounds), a song playing;
- a start after a hold faults in the ROM, every time, contained; a software restart and an update's restart clear
  it; the ROM writes its start stack near 0x01C6FC70-0x01C6FF04.

With the effects on core 2 (the full AUTOTEST, SECOND CORE on and, for its ONE CORE scenarios, off):

- core 2 ran in every window, no misread; its start answered in 0.41 µs on the first try; core 1 waits for it
  4-5 µs a half on average, 47 at most; its jobs' stack 4,764 of 6,144 B at most (the punch-ins);
- core 2 at 14-17 % of a half with a song's effects, ~20 % with USB audio recording, ~1 % at rest;
- DJ HEAVY (two decks, ~10.7 voices): core 1 66.0 % a half on average, 4 late halves in 30 s (one of them core 2's
  last block finishing past the half while core 1 was near 100 %), against 80.6 % and 68 late on one core; DJ
  BUSY 42.5 %, no late half; IDLE 3.4 % (one core 4.2); the voices are core 1's main cost (55 % in DJ HEAVY);
- the fallbacks: a fault of core 2's own (its stack limit) taken on core 2 (vector 1, its pc in its room), `STOPPED:
  FAULT`; a wrong check word, `STOPPED: MISREAD`; a block never ending, `STOPPED: STUCK`; each with core 1 taking
  over and the sound going on; a crash of core 1 with core 2 running keeps core 2 off at the next start (`STOPPED:
  AFTER A CRASH`); an update's restart reads as a power-on (core 2 starts).

Core 2 with the flash and with voices (measured on the FM-1, 2026-10-22; the details in [hardware.md](hardware.md)):

- **core 2 touching the flash while core 1's flash operation has the window off wedges both cores** (core 1 hangs
  at its next fetch once the window is back on; only the watchdog ends it), at the first operation of every kind;
  core 2 on RAM only runs on through all of them;
- **with core 2 idle in RAM around each of core 1's flash operations,** its work from the flash between them is right:
  engine voices, sample voices reading their audio in place, code and data, a 2-minute run with ~24,000 erases,
  programs, direct reads and line drops between, no wrong result and no stale line;
- **the engines on core 2 from the flash** run as fast as on core 1 (within 0-6 %), their output equal sample for
  sample; rendered on both cores at once they stay equal, but for a DX7 patch whose operators feed each other through
  the engine's buses: the DX7 engine keeps those buses in one scratch for every voice, so two cores spoil each other's
  (every block);
- **12 + 4 voices fit a half:** 12 on core 1 at 48 % at most (all 6-operator DX7s), 4 on core 2 with an effects
  load near a song's at 30 %; a hand-over 0.75 µs; core 2's jobs' stack 524 B for 4 voices;
- **the code cache is shared:** the same code on both cores costs nothing; different code past 32 KB in all slows
  both several times;
- **the cache RAM** (28 KB of data RAM made of the data cache's ways) works at RAM's speed and through flash work;
  no code runs from it.

**Not measured yet:** PT/FM's own 16 voices (AUTOTEST's KEYS at 16, DJ HEAVY and a dense song at 16, SECOND CORE on
and off, and saving while 16 voices play); a long run with two full DJ decks; a bus error of core 2's (no PROVE action
makes one).

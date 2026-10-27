# Editing, saving and RAM

Status: **settled** (2026-10-09), revised 2026-10-15 and 2026-10-24. The records: [the files
proposal](../proposals/202610080050-fs.md), section 11; [the edits proposal](../proposals/202610151809-edits.md);
[the loop station proposal](../proposals/202610240236-loops-station.md), section 12 (LOOPS in the edit memory).

How edits wait in RAM until SAVE, where the FM-1's 512 KB of RAM go, and what writing to the flash
costs.

## Editing and saving

- **The flash holds whole records; the edit memory holds only what differs from them, as pieces, each piece
  once.** A piece changed again is rewritten where it is: nothing piles up, and nothing waits to be tidied.

  | Record | Its pieces |
  |---|---|
  | a pattern, and the loops | each track's stream, and each loop's stream; the pattern's head (tracks, rows, EDIT STEP) |
  | a cartridge | each slot; the cartridge's head (its name, its slots' count); a sampler's samples' table |
  | a song, the settings, DJ's setup, the MIDI map, LOOPS' head (the setup and the loop table) | the record whole (they are small) |
  | sample audio | not edited in RAM: what writes it writes the flash ([store.md](store.md)) |

- **Reading never builds a record.** A pattern is read as each track's stream, from wherever it lies: the
  flash's tracks in place, the changed ones from the edit memory. A cartridge's slot is read the same way: the
  changed slot if there is one, else the flash's. The player reads them as it reads the flash.
- **A piece that becomes what the flash holds goes:** nothing is unsaved by it.
- **The cost of a change is its piece, once:** a cell changed costs its track's stream (about 100 B on the demo
  disk's songs) however many times it is changed again; a DX7 parameter costs its slot (112 B). The edit memory's
  67 KB hold about 650 changed tracks.
- **Every edit is heard at once,** from the next row (a pattern) or the next note (an instrument), whatever plays
  it: a song, the EDITOR's loop, LOOPS, either DJ deck. The edit itself, with the sound held off for those few
  microseconds, rewrites the piece and sets the player's place on that track: it walks the new stream from its
  start to the row the player is on and puts the player there. A piece that outgrows its room moves in the same
  step, its old room free at once, so no copy is ever kept for the player's sake.
  - **The player's steps cost nothing more:** each row is read from where the player stands on each track. The
    walk is the edit's, and only for a track the player is reading then. No index of the rows is kept: a cell
    whose length changes moves every row after it, so an index would be rewritten at every edit (and take up to
    4 KB: 128 rows, 16 tracks, 2 B).
  - **Room:** pieces of many sizes come and go, so free room breaks up; it is joined again by moving pieces down
    one at a time, each a short step with the player told as above, never the whole memory at once.
- **The list of pieces** lives in the edit memory itself, growing down from its top as the pieces grow up from
  its bottom, 8 B a piece: it takes only what the pieces in it need, no table of a fixed size.
- **Full:** when the pieces and their list meet, `MEMORY FOR EDITS IS FULL` ([../ui/saving.md](../ui/saving.md)).
  Only real changes fill it.
- **SAVE** waits for a pause, then builds each changed record from its flash bytes and its pieces, one record at
  a time in the store's 4 KB scratch, and writes them as one save ([store.md](store.md#saving)), compacting first
  when free blocks run short ([store.md](store.md#garbage-and-compaction)); then the edit memory is empty.
- **REVERT** takes the current object (a pattern, a slot or a cartridge, a song, the effects of what plays, all
  of LOOPS, the settings, the MIDI map) back to the flash's: its pieces dropped. **REVERT ALL** drops the whole
  edit memory and reads everything from the flash again. Both ask first and write nothing
  ([../ui/saving.md](../ui/saving.md)). There is no UNDO, no step back: nothing keeps an older copy.
- **Smaller still, only if needed:** a piece nobody is reading (not the playing pattern's, not the EDITOR's own)
  may be kept as its difference from the flash's bytes when that is smaller (a cell's change: about 6-10 B
  against a track's 100), and made readable again before it is read (the EDITOR opening the pattern, or the
  player about to reach it, prepared ahead by the main loop). Built only if the edit memory proves short in use.

## The edit memory and the spare zone

Two zones, neither borrowing from the other: the edit memory, 67 KB (68,608 B), sure of its room; the spare zone,
the rest of the data region, lent to what borrows from it.

- **The edit memory is all the edits':** the pieces and their list ([above](#editing-and-saving)), LOOPS
  included, which lives there as any edit, with no RAM of its own ([loops.md](loops.md#the-record)). Nothing
  borrows from it, and the build refuses a size that leaves the spare zone under its minimum.
- **Its offsets are 16-bit and count 4-byte units:** every piece starts on a multiple of 4 (its room rounded up to
  4), so the list keeps each offset ÷ 4 in 16 bits, which reaches 256 KB with no RAM more. The state's figures of
  the edit memory count 4-byte units too ([../connections.md](../connections.md#the-state)).
- **The spare zone is the data region's last part,** from the end of the variables up to the main loop's stack's
  guard band: whatever the region leaves, so no RAM lies idle. About 32 KB with today's code (33,440 B; 32,624 B in the
  debug build), 672 B over its minimum (880 B in the debug build), since core 2's room took 40 KB of the
  region and its voices 9 KB more (the pool's 20 places, their block buffers and core 2's mixes:
  [the second core](../second-core.md#ram)). The build refuses a zone under its own part, 24 KB (24,576 B: DJ's
  borrow with the 3,840 B more planned, and USB audio's buffers in reserve), and the spare (8 KB; 7 KB in the debug
  build), and prints its size. It holds no records: nothing in the
  flash needs a copy in RAM to be read ([store.md](store.md#where-things-go)). What else it is for is left to a
  later review of the RAM.
- **What is borrowed comes from its top,** one borrow at a time; a borrow never refuses while it fits the zone.
  Given back, the room is free again.
- **DJ borrows from its top while it plays** ([../ui/screens/dj.md](../ui/screens/dj.md)), 13,116 B: its second
  deck's song arena (6,400 B) and its tracks, chorus, sends, mix and state (6,716 B); each deck's own mix and
  sends over the half will take 3,840 B more.
  - **A deck's start is made ready in its own arena,** over the halves after the press
    ([../ui/screens/dj.md](../ui/screens/dj.md#playing-and-pausing)): no third arena.
  - **Leaving DJ,** what plays next starts on the pool's own arena, and the borrow goes back once the second deck
    has let go of it.
- **USB audio's buffers** may be borrowed from the spare zone the same way while a computer takes the FM-1's
  audio (6.5 KB: [in reserve](#in-reserve)).

## Where the RAM goes

Of the 512 KB, 439 KB hold data, in one region; the rest is code that runs while the flash is busy, core 2's room,
the stacks, the records kept across restarts and the chip's own. The sizes are those of the same engines, effects
and screens as built for the FM-1.

**The data region** (449,536 B):

| What | Bytes |
|---|---:|
| the spare zone, the region's last part: what the rest leaves (at least 32,768: its own 24 KB and the spare the build keeps, 7,168 B of it in the debug build); DJ's borrow and USB audio's buffers taken from its top | 33,440 (32,624 in the debug build) |
| the edit memory: the edits' pieces and their list, LOOPS' included | 68,608 |
| the DELAY line: 1 s at 29.4 kHz, so a 1/4 note down to 60 BPM (PING-PONG: 0.5 s) | 58,804 |
| the screen's strip: half the screen at 16 bits a pixel | 57,600 |
| the REVERB: its lines at 29.4 kHz (44,516) and PRE-DELAY's, up to 100 ms (5,884) | 50,400 |
| the punch-in buffer: the last 0.5 s as its middle at 29.4 kHz (29,400) and its sides at 7.35 kHz (7,350) | 36,750 |
| the DX7's tables: sine, exp2 and tanh of 8 KB each, the frequency table of 4 KB, built at power-on | 28,676 |
| the screens' state (with the texts' 12 line buffers and the panels' kept texts) | 21,919 |
| USB, the audio out, MIDI: USB audio's resampler table (4 KB, read for every sample sent) and ring of 512 frames (2 KB), the audio's double buffer (4 KB), SysEx and MIDI queues | 15,418 |
| 20 voices' places, their scopes, core 2's voice buffer, the Juno-106's chorus bus, VISUAL's ring | 15,984 |
| the keys heard dry ([../ui/screens/fx.md](../ui/screens/fx.md#the-keys-dry)): their mix a block's slot each (2,048), their own Juno-106 chorus lines and buses (2,592) | 4,640 |
| the player's and the effects' state (a block's tracks, sends and mix, the requests); LOOPS' lanes, their requests and the preview's take ~2 KB more ([loops.md](loops.md)) | 11,167 |
| the store: the map of the flash ([store.md](store.md#power-on)), its 4 KB scratch, its variables | 9,917 |
| the playing song's arena: the song built as the module the player runs | 6,400 |
| the effects over a whole half: the mix and the sends of a half's four blocks | 4,480 |
| room for the screens' next additions (their variables' cap: 28 KB) | 4,096 |
| the screens' shared scratch: an editor's copy of a record, the help's text, AUTO LEVEL's voices | 4,096 |
| the CHORUS's lines, 1 KB a side | 4,096 |
| the screen's line buffer, lent to the update path, the USB rescue and incoming SysEx | 3,840 |

- **Every large buffer of the sound is used in every audio block,** so none can share its room with another.
  The CPU is the limit, not the RAM.
- **Where a note value does not fit** (a DELAY TIME, a punch-in loop), the longest one that does is used.
- **The effects run over a whole half on one core,** not block by block: the voices of the half's four blocks first,
  then each effect over the four, so its code and state are fetched once a half. On core 2 they run block by block
  from RAM, each block handed over as its voices are made ([the second core](../second-core.md#when)).

**The REVERB, the DELAY and the punch-in buffer run at 2/3 of the output's rate, 29.4 kHz,** resampled in and
out; what they give back keeps everything up to ~13 kHz. At half the rate the loss above 11 kHz is plain to
hear; 29.4 kHz is close to the full rate by ear.
- **The REVERB** is a plate (Dattorro's, [../ui/screens/fx.md](../ui/screens/fx.md)): its delay lines
  take 44.5 KB at 16 bits (67 KB at the full rate) and PRE-DELAY's (up to 100 ms) 5.9 KB.
- **The DELAY** is one line, 59 KB (88 KB at the full rate), fed one signal: the sends are mono, and
  in GLOBAL routing the mix is summed into it. PING-PONG reads it at the time and at twice the time
  (the repeats two lines would give), so its time is at most half the line.
- **The punch-in buffer** ([../ui/screens/fx.md](../ui/screens/fx.md#punch-in-effects)) keeps the mix as its
  middle, (L + R) / 2, at 29.4 kHz, and its sides, (L − R) / 2, at 7.35 kHz (1/6 of the output's rate), so a
  replayed slice keeps everything up to ~13 kHz and the song's stereo up to ~3 kHz: the pans of drums, bass,
  chords and pads stay where they were, and only the width of the top comes to the middle. Played back, left =
  middle + sides and right = middle − sides, so a mono output sums to the middle exactly. The length is 0.5 s,
  a beat at 120 BPM. While anything plays, the middle goes through a 3:2 converter and the sides through a 6:1
  one; while a punch-in plays, both are read back through their converters.

**The voice pool** is the same everywhere: 20 places, 16 heard at most (10 on core 1, 6 on core 2: 12 on core 1
while core 2 does not run), the rest holding parked notes ([../ui/screens/settings.md](../ui/screens/settings.md#voices),
[../second-core.md](../second-core.md#the-voices-on-core-2)); core 2's voices render one after another into one
block buffer (256 B).

## The layout

| Address | Bytes | What |
|---|---:|---|
| 0x01C00000 | 4,096 | code in RAM: the flash driver (3,028 B), which runs while the flash cannot serve code; the link fails beyond |
| 0x01C01000 | 449,536 | **the data region**: the pool, then the variables; everything set up at power-on |
| 0x01C6EC00 | 40,960 | **core 2's room** ([the second core](../second-core.md#ram)): its jobs' stack (6,144 B) at the bottom, over the ROM's start stack (0x01C6F800-0x01C70000); its exceptions' stack (1,024 B); its code and tables, copied at power-on (30,384 B; 31,092 B in the debug build); its loop and park, queue, counters and fault record at or above 0x01C72000 (under 0.6 KB, its code's end rounded to 256 B); 2,908 B left (2,140 B in the debug build). The build checks it fits and the rest; the PC limits' RAM window reaches from 0x01C00000 to its code's end |
| 0x01C78C00 | 256 | guard band |
| 0x01C78D00 | 16,128 | the main loop's stack |
| 0x01C7CC00 | 256 | guard band |
| 0x01C7CD00 | 12,032 | the interrupts' stack |
| 0x01C7FC00 | 256 | records kept across restarts (the crash record, the boot guard and core 2's record, 96 B; 136 B in the debug build), where a restart changes nothing |
| 0x01C7FD00 | 76 | unused, write-protected with the chip's own |
| 0x01C7FD4C | 692 | the chip's own: boot info, the boot-state mailbox, the update record, the vectors |

- **The data region is one block,** placed by the linker: no buffer is placed by hand.
  - **The pool comes first, the effects' at its very bottom:** the linker takes the effects' object's pool
    first, the DELAY's line, the REVERB's and the punch-in buffer, about 150 KB that only the CPU reads and
    writes. So the chip's scratch while it restarts (the region's bottom 26 KB) and the update loader an
    update's restart unpacks (the next 80 KB) lie over those, and a stray write there could only be heard.
  - **Then the variables.** Every buffer a DMA uses for USB, the screen's line buffer and MIDI IN's ring
    then lie above 0x01C08000, as they always have: the sound's and the screen's DMA were measured to work
    below it, USB's was never tried there.
  - **The spare zone comes last,** up to the region's top ([above](#the-edit-memory-and-the-spare-zone)): what
    the code does not take is the zone's, and a borrow from its top takes the highest addresses.
  - **The build checks** all three, reads the region's bounds and its parts from the linker's symbols (no number
    of its own), and refuses a spare zone under its own part (24 KB) and the spare.
- **Before every reset, no DMA writes RAM and core 2 is held:** a core reset may leave the peripherals running while
  the next boot writes the update loader into the data region, so the sound's DMA and MIDI IN's stop first (USB is
  detached already) and core 2 is held ([the second core](../second-core.md#start-switch-fallback)) on an update, on
  a crash and on the way to the chip's USB update mode. The USB rescue starts neither.
- **The records kept across restarts** sit at the top, where no restart changes anything: the crash record (72 B:
  with the stack found overflowed, if any, and whether core 2 was running), the boot guard (12 B: two failed starts
  in a row end in the USB rescue) and core 2's record (12 B: whether it runs, and whether it is kept off until the
  power goes off: [the second core](../second-core.md#start-switch-fallback)), 96 of their 256 B; the debug build
  keeps its proving's record there too (40 B more: [../debug.md](../debug.md#proving-the-layout)).
- **The write protection of the chip's own area starts at 0x01C7FD00,** covering the boot info from its first
  byte (0x01C7FD4C), the mailbox, the update record and the vectors.

## The stacks

- **The main loop's stack is 16,128 B:** 6,808 B seen at most on the FM-1 (AUTO LEVEL ALL; its measurement takes
  about 5 KB while it runs, a chorus line and the loudest window's sums, the voices it measures borrowing the
  screens' shared scratch), 5,036 B after a power-on seeding. It is cut only after the paths no autotest takes
  are measured too (a seeding, CHECK STORAGE, a compacting SAVE).
- **The interrupts' stack is 12,032 B:** 6,068 B seen at most on the FM-1, only on the paths the autotests took;
  the sound's own frames are large (the effects' block 2.7 KB, the DELAY's 1.4 KB, the REVERB's 1.4 KB, the
  player's 1 KB).
  - **Its size comes from the worst case the build works out from the device's code:** each function's frame
    and its deepest call, for the audio interrupt, TIMER5's on top of it and the fault handler on top of both;
    the build prints it with the stacks, so a change that deepens it shows, and fails when it is more than the
    stack. Every frame is counted whole wherever its function calls, and a tail call as a call: an upper bound.
    5,888 B (5,916 B in the debug build), the effects now in core 2's job (6,632 B before the effects went over
    the half; 6,068 B seen). Core 2's jobs' stack, worked out the same way: 4,744 B of its 5,888 usable (4,764 B in
    the debug build).
  - **The code cannot show where a call through a pointer goes,** so the build keeps a table of every hook the
    interrupts call and its possible targets (MIDI's note hook, USB audio's tap, the output's, ...), counts each call at its deepest target, and refuses a call through a
    pointer it does not list, and any recursion: else the worst case would only be a lower bound, as the deepest
    seen is. A function deep on purpose (the debug build's stack overflow) is listed apart, outside the count.
- **Core 2's stacks** lie in its room: its jobs' 6 KB (the effects' block and the output stage, worked out by the
  build from the device's code as the interrupts' is, and refused when more than the stack) and its exceptions'
  1 KB; core 2 sets its own stack-pointer window over both, and each start fills them with a mark so their depth can
  be read ([the second core](../second-core.md#ram)).
- **Each stack has a window of its own for the stack pointer:** the interrupts' stack pointer within the
  interrupts' stack, the main loop's within the main loop's, so a stack running past its end is caught whatever
  its frame's size. The 256 B guard bands, write-protected, are a second net. The two windows are armed only when
  both pointers stand where the start put them (the main loop's within its stack, the interrupts' at its top);
  else one window over both, which the debug build's records say. Should the chip refuse two windows, the
  interrupts' stack goes below the main loop's, so one window over both catches the tighter one at its bottom.
- **Each stack's bottom and top are named in the linker script,** and everything that needs a stack's ends (the
  stack-pointer windows, the guard bands' write limits, the figures' stack depths, [../debug.md](../debug.md))
  uses those names, never a guard band's: either order of the stacks works unchanged.
- **The fault handler** saves its registers and draws the crash screen on the stack in use, the stack check and
  the write limits switched off first, so what it pushes on an overflowed stack, or into a guard band, faults no
  more. The crash screen names the stack found past its bottom (`STACK: MAIN LOOP`, `STACK: INTERRUPTS`) and the
  crash record keeps which. Should an overflow's report not come (the screen, or the crash record at the next
  start), it switches first to a crash stack of its own, 1 KB below the records, taken from the data region's
  top (the spare zone's).

## Measured on the FM-1

- **The RAM outside the data region as it was** (0x01C01000-0x01C07F00 and 0x01C7C100-0x01C7FD40, patterned and
  checked once a second): untouched through a 21-minute AUTOTEST (songs, DJ, USB audio, saving) and 236 checks
  after a restart. Nothing else writes there while PT/FM runs.
- **One speed:** a load takes 10.1 cycles there, 10.0 in the old data region.
- **DMA reaches it:** sound and screen ran from buffers there the whole run.
- **A restart** (a software reset, as a crash's) changed 23,532 B, all below 0x01C07634: the chip's scratch while
  it boots (0x01C00000 +16, 0x01C00A00 +1,536, 0x01C02000 +22,068, and the startup's own 20 B at 0x01C07F20,
  which go). Nothing from 0x01C08000 up changed.
- **An update's restart** (a core reset) unpacks the update loader at 0x01C0A800 (80 KB) and runs it on stacks
  below 0x01C72000; the loader keeps only the update record.

**Proved on the FM-1 before the layout is used,** by actions of the debug build ([../debug.md](../debug.md#proving-the-layout)):
- **A watchdog's restart keeps the records:** the main loop stops feeding the watchdog; twice in a row must end
  in the USB rescue (the boot guard counted both restarts), the crash record readable after the first.
- **Each stack's overflow is caught and reported:** a deliberate overflow of each ends in the crash screen naming
  the stack check, and the next start finds a crash record that says so; no false alarm in a full AUTOTEST.
- **The 256 B at 0x01C07F00 stay untouched by a restart,** left out of the pattern above: they now hold the
  effects' lines, so they cannot be patterned through an AUTOTEST; nothing but the firmware's own code runs
  while PT/FM runs, so what the restart does is the question. The debug build patterns them before every
  restart it makes (a crash's, the watchdog's) and checks them at the next start, before the pool is set up.
- **The punch-in buffer's converters:** the CPU the recording adds is measured, and the buffer heard in the
  playground against the full-rate stereo one, before it is kept.

## In reserve

Not done, for when RAM runs short:
- **the cache RAM: 28 KB more** (7 of the data cache's ways as RAM at 0x01F28000, the SDK's own setting; the data
  cache serves nothing on the FM-1): measured as fast as RAM on both cores and kept through every kind of flash work,
  core 2 able to work on it while the flash is busy; data only, no code ([../hardware.md](../hardware.md#the-cache-ram));
  set once at a start, before core 2 starts;
- the screen's strip as 8-bit colours (it draws from 32 colours only): 28,800 B back, the screens unchanged;
- USB audio's buffers borrowed from the spare zone while a computer takes them: 6.5 KB;
- the DX7's frequency table read from the flash: 4,100 B;
- the scopes in 8 bits: 1,024 B.

## What writing costs

From the flash's figures ([store.md](store.md#the-flash)); an erase only when a block opens or
compaction runs.

| Action | Written | Programs | Erases | Time |
|---|---|---|---|---|
| a cell changed, SAVE | 1 pattern, ~0.7 KB | 3-4 | 0, or 1 when a block opens (~1 save in 6) | ~8 ms |
| a song's order, or the settings | ~150 B | 1 | 0-1 | ~2 ms |
| a DX7 voice changed | its cartridge, 3.6 KB | 14 | 0-1 | ~27-45 ms |
| the loops: a jam, 8 loops over 4 bars | ~5-8 KB | ~20-32 | 1-2 | ~60-100 ms |
| the loops at the most: 16 loops of 4 lanes, 128 rows full | ~35 KB, in 10 parts | ~140 | ~9 | ~0.4 s |
| a busy session: 20 patterns, a cartridge, a song | ~18 KB | ~72 | ~5 | ~0.2 s |
| compacting a block | its live records, at most 4 KB | at most 16 | 1 | at most 45 ms |
| a reference update: an instrument in 40 patterns | ~27 KB | ~110 | ~7 | ~0.3 s |
| the most: a cartridge used in all 256 patterns | ~175 KB | ~700 | ~43 | ~2 s |

- **A save writes whole records,** each built from the flash's bytes and the edit memory's pieces: a cell changed
  writes its whole pattern, a DX7 parameter its whole cartridge.
- **Only four things write several blocks:** reference updates (asked for, and rare), compaction, CLONE or
  MOVE of a sampler cartridge with samples ([store.md](store.md#a-record)), and seeding from the PC
  ([blob.md](blob.md#seeding)).
- **Wear:** a normal SAVE costs ~0.2 erases, shared by the blocks that hold no audio: the head walks
  every free block in turn ([store.md](store.md#the-head-erases-and-wear)). With audio filling the user
  area down to the reserve, 16 blocks are left turning; a thousand saves a day then give each ~12
  erases a day, and its ~100,000 last over 20 years. Less audio in the user area, more blocks, longer.

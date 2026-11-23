# The FM-1's hardware

Status: **a reference**, kept current. What the M-VAVE FM-1 does, as far as PT/FM depends on it: each fact measured
on the FM-1 (said so, with the figures) or read in JieLi's open AC79 SDK. It is written for anyone making firmware for
the FM-1. The specs that build on it: [second-core.md](second-core.md), [files/ram.md](files/ram.md),
[files/store.md](files/store.md), [connections.md](connections.md).

The two cores are **core 1** (the one every firmware runs on) and **core 2**; the chip numbers them 0 and 1 (its
`cnum`), and its registers say "CPU0" and "CPU1".

## The chip

- **JieLi AC791N** (the WL82 family), two pi32v2 cores, bare metal (no OS, no libc, no heap needed).
- **Clocks:** the cores at 360 MHz (a PLL of 540 MHz × 2/3), the high-speed bus at 180 MHz, the peripheral bus at
  60 MHz (SPI1 runs at 60 / (BAUD + 1) MHz). That is above the SDK's own table already: no headroom to raise them.
- **Arithmetic:** a 32 × 32 → 64-bit multiply and multiply-add in one instruction, 64-bit shifts, a 32-bit divider.
  A single-precision FPU without square root, whose divide-by-zero trap fires on a float divide; the compiler runs a
  divide ahead of the test that guards it, so a guarded float divide by zero still traps. PT/FM builds without float.
- **Time:** TIMER4 runs free from the 24 MHz crystal (wrapping every 178.9 s): the time base. TIMER5 is free for a
  periodic interrupt (PT/FM's 10 kHz scan).
- **Supplies:** the boot loader leaves VDC14 at 1.40 V and SYSVDD (the cores and the SRAM) at 1.26 V (measured); every
  figure here was measured at those levels. At 360 MHz that is under the chip maker's own table (1.45 V even at 320
  MHz), and at those levels some FM-1s misread their RAM under load. With SETTINGS' CORE SUPPLIES at STOCK (the
  default; [ui/screens/settings.md](ui/screens/settings.md)) PT/FM raises both at every start, once the settings are
  read and before any sound or core 2, to the levels the FM-1's stock firmware sets: VDC14 1.60 V (level 7: 1.25 V +
  50 mV a level), SYSVDD 1.38 V (level 15: 0.93 V + 30 mV a level), each range's top; up only, a level every 100 µs,
  VDC14 first (SYSVDD is regulated from it), interrupts off (`fw/hal/fm1_supply.h`). A P33 transfer that times out
  stops the raise where it stands. STOCK picked later raises them at once; LOWER leaves them as they are: nothing
  lowers raised supplies but a power-off (a restart keeps them), so LOWER takes effect at the next power-on.
- **Costs measured:** a taken jump ~6 cycles; a RAM load ~10 cycles; a code-cache line missed ~465 cycles (~1.3 µs);
  64-bit fixed point is cheap, a 64-bit divide (a library call) and float are not.

## RAM

512 KiB at 0x01C00000-0x01C80000, the same speed everywhere, **not cached** (data the two cores share needs only the
chip's `csync`, no cache upkeep).

- **The chip's own:** 0x01C7FD4C-0x01C80000 (boot info, the boot-state mailbox at 0x01C7FD80, the update record at
  0x01C7FD88, the 128 vectors from 0x01C7FE00; vector 126's word, 0x01C7FFF8, is where core 2 starts).
- **What a restart writes** (measured): a software restart (a crash's) changes 23,532 B, all below 0x01C07634 (the
  chip's scratch while it boots: 0x01C00000 +16, 0x01C00A00 +1,536, 0x01C02000 +22,068, 20 B at 0x01C07F20);
  nothing from 0x01C08000 up changes. An update's restart (a core reset) unpacks the update loader at 0x01C0A800
  (80 KB) and runs it on stacks below 0x01C72000. **From 0x01C72000 up nothing writes** at any restart: a core 2
  a reset did not stop can only run on there, and records kept across restarts stay there and at the top.
- **The watchdog's restart keeps RAM** as a software restart does (measured: a record at the top read after it).
- **DMA:** the sound's and the screen's measured to work below 0x01C08000; USB's never tried there (PT/FM keeps
  every DMA buffer above it).
- **The cache RAM:** 28 KB more, made of the data cache's ways ([below](#the-cache-ram)).

## The caches

One controller serves both cores (control at 0x01EEE008: bit 8 the code cache on, bit 9 the data cache on, bit 14 it
is idle; the data cache's ways at 0x01EEE00C, three 8-bit fields; the code cache's at 0x01EEE010, two 8-bit fields).

- **The code cache serves the flash,** code and data read in place alike: 32 KB, 8 ways × 128 sets × 32-B lines (the
  SDK's tag layout), **shared by the two cores**. Lines 4 KB apart compete for one set's 8 places. Measured:
  - the same code run on both cores costs nothing (the cores share its lines; often a little faster);
  - different code on each, more than 32 KB in all, slows both: 16 KB each took core 1 from 24 to 121 µs a pass;
    32 KB each took both from ~210 to ~2,620 µs (12×); 64 KB each, past the cache already alone, 3.6×;
  - 10 lines of one set read over and over (5 by each core; the set has 8 ways): 54× slower than 5 alone;
  - code in RAM does not use the cache.
- **The data cache serves only external RAM** (SDRAM or PSRAM, which the FM-1 does not have). The boot leaves it on
  (control 0x302, data ways 0x00FFFFFF, measured); turning it off changed no engine's time beyond ~2 %.
- **Dropping lines** (after the flash changed under them): `flushinv` one 32-B line at a time, waiting for the cache
  to be idle around each, from RAM with interrupts off. ~0.7 µs a line; ~98 µs a line while core 2 runs code from
  the flash (its misses keep the cache busy), measured. A line dropped is dropped for both cores.

### The cache RAM

The AC79 SDK's own setting without external RAM: 7 of the data cache's 8 ways as RAM, **28 KB at 0x01F28000-
0x01F2F000.** Set once at a start, from RAM, interrupts off, core 2 held, before anything else uses the caches:

1. wait for the cache to be idle; clear control bits 8 and 9;
2. data ways = (data ways & 0xFF000000) | 0x00808080 (way 7 kept for both cores and DMA), code ways = (code ways &
   0xFFFF0000) | 0xFFFF;
3. clear the tag RAMs: 0x01F00000-0x01F04000, 0x01F08000-0x01F0A200, 0x01F0B000-0x01F0B200;
4. set control bit 8 (the code cache on; the data cache stays off); read the registers back.

Measured: ready in 1.1 ms (with an address-and-complement pattern test over all 28 KB); **as fast as RAM** on both
cores (128 KB read in 911 µs, written in 820 µs on core 1; read 911 / written 1,002 on core 2; both cores at once
as with RAM); **kept through flash work**: core 2 checked a pattern there 7,650 times, none wrong, while core 1
wrote it again, erased, programmed, read the flash directly and dropped lines; core 2 works on it while the flash
is busy, as on RAM; an engine voice's state there renders the same, at the same speed. **Code cannot run from it:**
fetches there do not see what was written (a 30-byte routine copied in ran on, on either core, 4 KB to the PC
limit's end, and faulted). **Data only.** PT/FM keeps it in reserve ([files/ram.md](files/ram.md#in-reserve)).

## The flash

1 MiB NOR, read in place at 0x02000000 + (offset − 0x4000) through the code cache. **FM-1s carry one of two chips,**
told apart by their JEDEC ID (command 9Fh):

| JEDEC ID | Chip | In |
|---|---|---|
| 0x856014 | Puya P25Q80 | the FM-1s made first; every figure below was measured on it |
| 0xB36014 | UCUN UC25HQ80 | FM-1s made later |

Both have the same layout and commands (4 KB erase blocks, 256 B pages, a program of part of a page leaving its other
bytes as they were); the UC25HQ80's datasheet gives a 4 KB erase 7 ms and a page program 1.5 ms. **PT/FM writes to
the flash only when it is one of the two** (`fw/hal/fm1_flash_id.h`, read at every start): with any other chip the
firmware saves nothing and takes no install, and PT/FM's update loader writes nothing (the firmware there starts
again), so a firmware is never written where it could not be updated again. Nothing has been tried on a UC25HQ80 yet.

| Offsets | What |
|---|---|
| 0x000000-0x004000 | the boot code: never written |
| 0x004000-0x093000 | the firmware slot (app.bin from 0x4120, at 0x02000120 in place; 581,564 B), written only by installs: PT/FM's start check from 0x4120 (its first block the same in every build), the firmware from 0x5000, the mark in its last block ([Updates and rescue](#updates-and-rescue)) |
| 0x093000-0x0E0000 | free for data (PT/FM's user area, part 1) |
| 0x0E0000-0x0E5000 | the update's staging: PT/FM's update loader kept there |
| 0x0E5000-0x0FF000 | free for data (PT/FM's user area, part 2; 0x0FC000 is where other firmwares keep their settings) |

- **The data area reads in place** once a window set at the start maps it (`fl_plain_window_init`); a header read
  that way costs 1.3 µs cold, against 16.1 µs as a direct read.
- **An erase, a program and a direct read each switch the code window off** (the flash controller to the SPI port):
  nothing runs or reads from the flash meanwhile, on either core. The driver runs from RAM with interrupts off and
  drops the written block's lines from the cache before it ends. Measured: a 4 KB erase 17.09 ms; a program
  1.92 ms whatever its size up to a page (1.90-2.02; a full 256-B page 1.68-1.70); a direct read of 256 B 0.18 ms
  (6 µs + 0.7 µs a byte).
- **Erasing stops code fetches,** so the sound stops for it: silence the audio around it.
- **A page can be programmed again in pieces** before the next erase (no ECC): records appended into one page work.
- **Power cuts** (measured): a program under way finishes; an erase can be left short (a block almost all 0xFF, a
  few bytes not): check an erased block before writing it.
- **Wear** is no concern for installs (2-3 erases of the staging a time against ~100k rated).
- **Core 2 must not touch the flash while it is busy** (measured): code or data read from the flash by core 2 while
  core 1's erase, program or direct read has the window off **wedges the flash path**. Core 1 finishes its
  operation and turns the window back on, then hangs at its next fetch from the flash; core 2 gets no further;
  only the watchdog ends it. Every kind of operation, the firmware slot and the data area alike, at the first one.
  Core 2 working from RAM (or the cache RAM) runs on through any of them (18,500 checks, none wrong). So **every
  flash operation of core 1 waits until core 2 is idle in RAM**, and then core 2's work from the flash between
  operations is right: a 2-minute run of code walks, data reads and engine voices from the flash on core 2, with
  ~24,000 erases, programs, direct reads and line drops between, saw no wrong result, and no stale line after an
  erase or a program.

## The second core

The sequences (start, hold, faults) are [second-core.md](second-core.md#the-chip)'s. Measured:

- **It starts once in each start of the firmware:** a start after a start and a hold faults in the chip's ROM, every
  time; a restart of the chip (a software restart, or an update's) clears that. Started late (the sound, the timers
  and USB running, interrupts off around it) it answers in 0.41-0.95 µs, without the clock-divider bit the SDK's
  start sets. The ROM writes its own start stack for it near 0x01C6FC70-0x01C6FF04.
- **As fast as core 1, from RAM or from the flash:** the same engines' voices within 0-6 % (a 64-frame block, warm:
  a 6-operator DX7 53.45 / 53.33 µs, a Juno-106 53.2 / 51.0, a TB-303 44.2 / 44.0, a sample voice at +12 68.2 /
  66.5), their output equal sample for sample.
- **A hand-over** (a job posted, run and seen done, through a queue in RAM) round trip: 0.54-0.75 µs on average,
  4.5 µs at most.
- **Idle, polling its queue in RAM,** it costs core 1 nothing measurable; no contention on RAM (core 1's pieces within
  ±1.3 % whatever core 2 does); both cores reading RAM at once ~10 % slower each.
- **No misreads** in 5 minutes of checking (5.1 million rounds), a song playing.
- **Its faults:** a fault of its own (its stack limit, its PC limit) is taken on core 2 (one vector table for both: the
  handler asks `cnum`); a bus error is flagged in the debug unit both cores share and taken on both: twice an invalid
  AXI write of core 2's (the culprit registers named it) faulted core 2, which kept its record and parked, and core 1,
  which showed its red screen.
- **12 + 4 voices fit a half** (5,804 µs; interrupts off, no other work): 12 voices on core 1 took 30-48 % of a half
  on average (all 6-operator DX7s the most: 2,546 µs), 4 voices and 222 µs of effects a block on core 2 24-30 %;
  core 1 waited ~1.3 µs a half for core 2; no half late. A plain firmware's interrupts (a 10 kHz panel scan, USB
  polled at 2 kHz) took 6.3 % of core 1.

## Sound

- **I2S (ALNK0) to an external codec,** 44.1 kHz nominal, **~44,145 Hz measured** (+0.1 %, ~1.8 cents sharp, against
  a USB clock); 24-bit samples in 32-bit words; a double buffer of 256-frame halves (5,804 µs), an interrupt a half.
- **The internal speaker plays one channel only:** send mono for it. **A cable in the output mutes the speaker;** no
  pin tells it is there.
- **PC6 is the I2S data line,** not a mute (PC0 MCLK, PC1 SCLK, PC2 LRCLK; the sound goes out on ALNK0's channel 3).
  Whether the pin follows its port level while the I2S unit drives it is not known.
- **At power-on PC6 stays low until 150 ms of silence have gone through the codec,** then goes up
  (`fm1_audio_init_muted`, then `fm1_audio_unmute`, in `fw/hal/fm1_audio.h`; the usual bring-up raises it before
  the I2S clock runs). On the FM-1 the power-on click became softer, not gone
  ([open-questions.md](open-questions.md#the-firmware)).
- **A late half** (the DMA reaching it before its sound is made) plays what was made, then silence, never noise:
  the half is cleared first, then each block goes into it as it is made (`out_block` in `fw/device/main.c`). A
  half left as it was clicks loudly (seen on the FM-1).

## Screen

- **ST7789-class, 240 × 240 RGB565 on SPI1,** polled DMA (the clock does not disturb the audio's interrupts, only
  how long a frame takes). 12 MHz (BAUD 4) everywhere; 60 MHz (BAUD 0) works, at the controller's 62.5 MHz write
  limit (~44 MHz effective). A full frame: ~77-90 ms at 12 MHz, ~30 ms at 60 MHz.
- **No tearing-effect line reaches any pin** (ports A, B, C and H watched); the panel refreshes every ~17.1 ms, so
  tearing can only be made shorter (faster transfers, smaller windows).
- **Never toggle PA2** (the backlight, active low): the same line enables the buttons' and keys' matrix. Dimming it
  by PWM froze an FM-1; switching it off left the panel deaf to every key and knob until a power-cycle. To blank the
  screen, the controller's DISPOFF and SLPIN (waking takes 120 ms).

## Controls

- **An 11 × 6 matrix,** scanned at 10 kHz in a TIMER5 interrupt (above the audio's priority, so slow frames never
  delay notes). The input functions that take edges disable interrupts briefly: never call them from an interrupt.
- **Note keys:** bit 0 F3 ... bit 26 G5.
- **Buttons** by matrix id: OCT− 0, OCT+ 1, FX 2, SEL 3, ENV 4, LFO 5, EDIT 6, GLO 7, HOME 8, SAVE 9, ARP 10, SEQ 11,
  PLAY 12, REC 13 (button 3 is printed SEL).
- **Knobs** (endless, 24 clicks a turn, no push): SELECT 0, ALGORITHM 1, K1-K4 2-5, PRESETS 6. **A click is one full
  quadrature cycle** (4 transitions), the knob resting in the state it had at power-on. **A decoder must take every
  scan's state:** one that took a state only after two scans agreed (~2.2 ms) lost clicks on fast turns (12 clicks at
  150 a second counted ~8, at 200+ none, sometimes one backwards); taking every scan, with a two-state jump from the
  rest position continuing a click of the last ~22 ms, counts them all.
- **LEDs** on every button and key; PLAY has a red (id 12) and a green one (the matrix's column 8, bit 1).
- **MASTER** is an analog pot on ADC channel 4 (0-1023); **the battery** on channel 3 (PT/FM reads 501-621 as 0-100 %
  and lights the bars from 10 / 34 / 67 %, i.e. 513 / 542 / 582; the voltage behind them unknown).

### The keys' velocity

The keys carry no velocity sensor: each is a rubber dome over one contact of the diode matrix. But the contact is a
carbon pill whose resistance falls with the force on it, and two of the matrix's rows, PA7 and PA8, are inputs of the
chip's converter (channels 0 and 1): they hold 20 of the 27 note keys, F3 to C5. The top seven, C#5 to G5 on PA6, have
no converter. SETTINGS' KEYS VELOCITY ([ui/screens/settings.md](ui/screens/settings.md)) reads them; CONSTANT, the scan, the
converter and MASTER work as without it. The record: [the keys' velocity proposal](proposals/202611221230-key-velocity.md).

- **One conversion a scan tick** (`fw/hal/fm1_input.h`): once the tick has latched column n, it starts a conversion of
  that column's PA7 key (even frames) or PA8 key (odd frames) and takes it first thing at the next tick, 100 µs later,
  before the rows are read (a conversion takes about 60 µs at the converter's clock divider 7 and start delay 2: faster
  settings read rails, a longer delay runs past the tick). Each of the 20 keys is read every 2.2 ms. The scan's rate
  and its debounce stay: the digital scan is the truth for pressed and let go; the readings only add the velocity.
- **Driven readings.** Pulled up, a seated key reads about 185 of 1023 whatever the force (the matrix diode's drop),
  and a harder press moves it 10-20. So once a key has been touched, every other reading of it is taken with its row
  driven high at the pin's second-weakest drive (HD0 alone; the level the figures here were measured at): that driver
  cannot hold the line against a seated pill, and the reading less the diode's 185 is the pill's resistance. Soft and
  firm presses then differ by 100-150, firm ones repeat within about 5; a hard press lands at about 390-450, a gentle one
  at 515-584. The row is an input with its pull-up again at the next tick, before the rows are read.
- **A key's readings:** open; touched (pulled up under 900: the pill has started to conduct); seated (under 400); open
  again after two readings over 600 (a landed key can read open once: a bounce), or when the digital scan lets it go.
  A touch that goes before it seats is a graze: it plays nothing (the digital scan never saw a press).
- **The velocity** is how deep the key sits, not how fast it went down: the dome snaps, every press landing within a
  reading or two whatever its speed. The first driven reading after the touch is the landing (WAIT 1; the debug build
  can take the lower of the first two, WAIT 2, about 4.4 ms later: [debug.md](debug.md#the-keys-bytes)). On a log scale
  of the pill's resistance, as conductive rubber's falls as a power of the force: t = log(r / r_firm) / log(r_light /
  r_firm), r the landing less 185, r_firm from 530 (velocity 127), r_light from 896 (velocity 1); velocity = 1 + 126
  (1 − t)^c, c by KEYS VELOCITY: SOFT 0.25, NORMAL 0.5, HARD 1.0 (softer playing reaches 127 at SOFT). In integers
  (`fw/device/keyvel.h`: a log2 table and the curves' tables), mapped where the press is taken, never in the tick. FM-1s
  differ by about 120 at the firm end; a key pressed off its centre reads softer.
- **The note waits for it:** a key's press reaches the app (`plat_input`'s notes) once its velocity is known, at most
  one reading (2.2 ms) after the touch, so a note comes about 1-2 ms later than without it; a press whose velocity is not
  known 10 ms after the digital scan saw it plays at KEYS VOLUME.
- **The top row** (C#5-G5) plays at the mean of the last four velocities read (fewer, theirs), at KEYS VOLUME before any.
- **The LEDs.** A lit LED on the column being read lifts the reading by about 25: a held key's column is dark on the
  tick that reads it (1 tick in 22 for that column).
- **MASTER and the battery** come from the scan while it reads the keys (the converter is its own then): MASTER in
  OCT-'s slot (odd frames, column 0), every 2.2 ms; the battery in a short pause of the readings, every 2 s as without
  them (the tick takes the conversion it started and starts none, the main loop converts as before, then the readings go
  on).
- **The rows' digital inputs stay on.** With them off, every key on PA7 and PA8 reads pressed, OCT− and OCT+ among
  them (OCT− at power-on is UPDATE MODE); a row left driven reads its keys as let go. A crash's screen, which scans the
  panel itself, releases both rows first. The scan's host test checks the rows' direction at every read
  (`fw/tests/scan_test.c`).
- **The pressure:** a held key's driven readings go on every 4.4 ms; how much harder than it rests the key is pressed
  is worked out in the main loop every 5 ms (`fw/core/press.h`: the rest follows the reading down for 250 ms after the
  landing, then creeps after it, and comes up at once when the key eases; log2(rest / reading), nothing under 0.04
  octave, the most at 0.46 more, smoothed over ~30 ms). MIDI CONTROLLER's general mode sends it as aftertouch
  ([controller.md](controller.md#3-general-mode)); in the tracker it makes **the key's own sounding notes louder,
  live**: the note's volume rises from its velocity's toward 127 at the most and falls back as the key eases (a note at
  127 already gains nothing). Loudness only: a DX7's modulators keep the press's velocity. The voices it reaches are
  the key's own as the keys start them (any engine, a FUSION's layers, the TB-303 the keys glide) and, on LOOPS, the
  lane a key plays through (the stage's keys, those held into PLAY, the keys with the stage off); not the
  arpeggiator's; none on the top row or at CONSTANT. Each change reaches the player as a gain on the key's notes
  (`player_key_press`) or a lane's volume, ramped in the voices' own gain over a block. **Recorded** by an EDITOR take
  and LOOPS' stage as volumes alone, a change of 4 or more at a time and the return to the key's velocity, nothing
  while the key is held steady, and played back smoothly: a take's track slides to each (`Axy` written on the row
  before it, [ui/screens/editor.md](ui/screens/editor.md#realtime-takes)), a stage's lane glides to each over its row
  ([ui/screens/loops.md](ui/screens/loops.md#the-stage)); the notes written and MIDI OUT keep the press's velocity.
- **The cost:** the tick's own work grows by one conversion's registers, a key's state and, on a driven reading, a few
  pin writes (no loop, no divide): 1.25 us a tick in the debug build (10.2 against 8.95 at CONSTANT, measured on the
  FM-1); RAM about 260 B with the pressure; in the audio interrupt nothing but a pressure's change taken on (a key's
  voices' gain set). Since then the tick does less: a key at rest reading open is left at once (24 instructions in the
  debug build, 31 before; 17 in the release), the rows' drive strength is set once as the readings start, not at each
  driven reading (two read-modify-writes fewer), and a conversion's start writes its last word whole (a read fewer).

## USB and MIDI

- **USB full speed,** a USB-MIDI device (its SysEx also carries the update protocol), polled at 2 kHz from TIMER5.
  USB-MIDI's realtime messages (code index 0xF: clock, start, stop) arrive only if the driver passes them on.
- **The SysEx a host does not read for 200 ms** with part of a message queued is given up.
- **TRS MIDI IN:** PH8 into UART1's receiver at 31,250 baud, by DMA into a ring, polled; keyboards work through a
  TRS-to-DIN adapter of either type (A or B). No MIDI out jack.

## The radio (Bluetooth, Wi-Fi)

- **The chip has a Wi-Fi and Bluetooth radio, and the FM-1 its antenna:** the FM-1's own firmware offers MIDI over
  Bluetooth LE.
- **The radio is reached only through JieLi's closed libraries** (the Bluetooth controller and stack, `btctrler.a`,
  `btstack.a`, partly LLVM bitcode, a BTstack-like API), built for JieLi's own system (its multi-core RTOS and
  drivers); the radio library alone calls ~130 of that system's functions. Running them on bare metal takes a shim
  of that system; one has been done elsewhere.
- **A GPL-3.0 firmware that links them cannot be published** (closed objects), so PT/FM never enables the radio. Not
  tried on the FM-1 by PT/FM.
- **On the computer's side,** Windows has no built-in Bluetooth LE MIDI yet (a vendor's driver or a bridge app).

## Updates and rescue

- **An install** goes over USB-MIDI SysEx from a web page: the running firmware answers the installer's handshake
  with its identity (`FM-1_<digits>`: the installers compare the part before `_` with the model and refuse letters
  after it; found by "FM-1" in the USB product name), stages the package's update loader at 0x0E0000 (step 1),
  writes the update record in RAM and restarts; the boot code runs the loader, which writes the firmware slot (step
  2). The boot code looks for the update record in RAM only: a power-off ends it, so on its own an install cut short
  never resumes.
- **The start check** (`fw/stub`) is PT/FM's own: the slot's first code, run at every start before the firmware, in
  the slot's first block, whose bytes are the same in every build (pinned; app.bin's CRC16 made constant by the
  mark's last 2 bytes, so the directory there is constant too). In about 75 ms (48 ms of key scans, 26.5 ms for the
  firmware's CRC-32 in place) it reads:
  - **OCT−** held alone (PA2 driven low first: the key matrix reads only then; so OCT− is no boot key for anything
    else);
  - **the firmware's request** (its boot guard: two failed starts in a row), in a box of 16 B at the top of the RAM
    records, kept across restarts and set anew at every power-on, told by the chip's reset reasons (RAM can outlast a
    short power-off: seen on the FM-1, the box counting on through a switch-off);
  - **the mark**, the slot's last 32 B: the firmware's length and CRC-32, its own CRC-32;
  - **the firmware's CRC-32**, read in place, against the mark's.

  With none of the first two and both checks good it starts the firmware (its CRC-32 of the firmware takes 26.5 ms on
  the FM-1); else **update mode**: the update loader kept (the first whole head at 0x0FC000, 0x0FA000 or 0x0E0000:
  the flash's end, ready for the staging's move there, then today's staging), once; with none kept, or when it started once already without the firmware starting between
  (it did not come up), the chip's own USB mode (a dark screen; it takes a ROM-mode writer).
- **PT/FM's update loader** ("ota-FM-1_901", 9.6 KB staged) keeps what other firmwares check before staging it (the
  16-byte mark at its start, its load address, at most 0x14000 B) and the M-UPGRADE protocol. It writes the slot
  only. It fetches the mark's block first and, when it is another build's, erases it before anything else; it
  writes the mark's block last, once the whole package's CRC checks out. So from the first erase to the last block
  there is no mark that fits the firmware: a power cut there leaves update mode at the next start, and the install
  page finishes the install. A host that answers hash reads (PT/FM's pages: F0 22 24 35 7C F7 starts it) is asked
  each block's CRC-32 first, and only the blocks that differ are fetched. Its screen: INSTALLING, CHECKING or WRITING
  with the share and a bar of the slot's 143 blocks, DON'T SWITCH OFF; in update mode UPDATE MODE and why.
- **The loader stays staged:** PT/FM's firmware keeps a loader of its own in the staging at every start (it erases
  anything else there). An install from another firmware stages the package's loader as before.
- **The one write with nothing to fall back on** is the slot's first block: the first install of a firmware with
  the start check, and any later change of the start check (about 44 ms: an erase and 16 pages). Every other install
  leaves that block as it is.
- **The watchdog** (in the P33 power domain, reset mode, 1-32 s) and **a software restart** (P33's power register)
  both restart the chip; P33 says why (power-on, watchdog, software).
- **A crash** (an exception on core 1, or PT/FM stopping itself) shows a red screen, `PT/FM CRASH` (or `PT/FM
  STOPPED` and why), then the version and `BUILD` with the build's code, and `PRESS A BUTTON TO RESTART`: it stays,
  the watchdog fed and the panel scanned in place, until a button or a key is pressed (one held at the crash let go
  first), so its numbers can be noted; then a software restart. `PT/FM CRASH` names what the fault found
  (`fw/device/crash_screen.c`):
  - two words a line: `VEC` (the vector), `PC`, `EMU` (the stack-pointer windows' flags), `DBG` (the debug unit's:
    bit 13 a write limit, bit 12 or 10 the PC limits, bits 4, 5, 16-21 the bus), `RETS`, `SP`;
  - the debug unit's culprit registers, which name the bus master of a bad access (the chip keeps who and which
    window, not the address), in the smaller font, a row each 12 px:
    - `WR` and the write limits hit by core 1, core 2 and the DMAs, a bit a window, core 1's named when it is one
      (`MAIN LOOP BAND`, `INTERRUPT BAND`: the 256-byte bands under the two stacks; `TOP (VECTORS)`, `CORE 2'S
      CODE`), then `DMA`, the DMA that hit one;
    - `AXI W` and `R`, the bus masters of an invalid AXI write and read (`0100` core 1), and `MMU W` and `R`, the DMAs
      of an MMU write and read error; four hex digits each, the window masks two;
  - `ETM`, the last four branches traced; `USP` and `SSP`; `PSR` and `ICFG` (the interrupt level it ran at);
  - `UP`, the seconds since the start, and `LATE`, the halves the sound was late;
  - core 2 as the crash found it: `RUNNING AT` where it was (paused at the fault, its PC read, before the hold), `OFF`,
    `OFF: AFTER A CRASH`, or `STOPPED:` and why. **A fault of its own** (one since its start, in its record: a fault on
    core 2 records itself and parks it, so where the pause finds it says nothing) reads `CORE 2 FAULT AT` its PC and
    `R` its RETS, whether core 1 had held it yet or not. A bus-invalid trap is imprecise on core 2 as on core 1: the
    write came a few instructions before that PC;
  - `STACK: INTERRUPTS` or `STACK: MAIN LOOP` when the stack check found one past its end.

  Only the record's words are kept for the next start (debug's RECORDS); the culprit registers and core 2's PC are
  the screen's alone.
- **A freeze** (the main loop stuck, so the watchdog restarts the chip after ~8 s) leaves a record in RAM the restart
  keeps: the main loop's step now (each part of its pass marks itself: the store, the UI, drawing, USB, an update, and
  every flash step with its address, the wait for core 2 before it apart), the time of its last pass, the audio
  halves and input ticks begun since and whether either interrupt was inside at the end. The next start, P33 naming
  the watchdog, shows a blue screen until a button or a key is pressed (one held let go first), then goes on:
  `PT/FM FROZE`, `THE WATCHDOG RESTARTED IT`, `THE MAIN LOOP STOOD AT` and the step (`A FLASH ERASE`, `ADDRESS
  000A3000`), `ITS LAST PASS AT 717.123 S`, `AUDIO: 1378 HALVES AFTER, OUT` (the audio went on: the main loop alone
  stuck, or starved), `INPUT: ... TICKS AFTER` (none: the interrupts were off), core 2's queue (`CORE 2 POSTED ...
  DONE ...`, from its memory the restart keeps: a job handed over and never done; shown when core 2 was marked as
  running at the freeze, read before the start works out its ban), core 2 stopped by a fallback earlier in that run
  (the first time: why, when, and where it stood, paused and read as the hold came; the record keeps them: `CORE 2
  STUCK AT 1234 S PC 01C78472`, `MISREAD`, `NO ANSWER` with no PC, `FAULT`), or `CORE 2 OFF`; with the audio interrupt inside
  at the end, where it stood: `IN THE AUDIO: ` and `ITS START`, `CORE 2'S LAST HALF` (the wait for core 2's blocks of
  the half before), `MAKING THE SOUND`, `CORE 2'S QUEUE FULL`, `WAITING FOR CORE 2`, `A FLASH READ` and its address
  (a direct read in the audio context, interrupts off), or `ITS END`; making the sound, the core's own mark names
  where in it (a byte the platform keeps through the restart, plat.h's `plat_sound_at`): `ITS START`, `THE NOTES`,
  `THE GHOSTS` (the ghosts coming back: their checks, starts and forward work), `THE VOICES`, `A JUNO-106 VOICE`
  (`DX7`, `SAMPLER`, `TB-303`, `CHIP`), `THE EFFECTS` or `THE OUTPUT`; in a Juno-106 voice, the engine's own marks
  (`juno_mark`, kept the same way) on the next row: the part of its render (`START`, `OSCILLATORS`, `FILTER 1/4` to
  `FILTER 4/4`, `VCA`, `END`), the voice and its patch as the render began (`FILTER 2/4 V 01C3A0B4 P 01C3A0A2`; the
  marks are whichever core's rendered last: core 1's when core 2 was off); and when a fault began in that run but its
  red screen never came (the crash record's count grew), `A CRASH BEGAN: VEC 01 PC 0200B086`; last, the supplies as
  the frozen run left them (read at this start, before anything raises them: a restart keeps them),
  `SUPPLIES 1.60 V, 1.38 V` (`: NOT RAISED` when they are not both at the stock levels: CORE SUPPLIES LOWER, or a
  raise stopped short). The small rows run from y 98, 12 px each, the last at 170: with every row in use the
  supplies' is left out. Then the version,
  `BUILD` and `PRESS A BUTTON TO GO ON`. Every restart of PT/FM's own (an update's, which then restarts through the
  loader, the chip's USB update mode, FACTORY RESET's, the debug build's watchdog test) disarms the record first:
  those can read as the watchdog's at the next start, and none is a freeze.
- **Core 2's own fault record** (in core 2's room: kept through restarts and installs, lost at a power-off) is shown
  once. A crash's red screen that names it (`CORE 2 FAULT AT`) marks it shown; one no screen has shown (core 1 held
  core 2 and ran on, or an older build's crash, whose screen read where core 2 was paused) is shown at the next start,
  after PT/FM FROZE's check, on a red screen until a button or a key is pressed: `CORE 2 FAULTED`, its `VEC`, `PC`,
  `EMU`, `DBG`, `RETS` and `SP`, `UP 181 S  FAULT 1` (the seconds that run had been up; the faults since the power came
  on), `FROM AN EARLIER RUN, SHOWN ONCE`, then the version, `BUILD` and `PRESS A BUTTON TO GO ON`.
- **Core 2's fills and copies write only into the RAM** (0x01C00000-0x01C7FFFF): its memset and memcpy check the
  destination and its end first, a compare and a branch a call. One that leaves the RAM (an invalid write over the
  bus: a crash on both cores) is written nowhere: core 2's fault record says so (its kind, `pc` the caller, the
  destination and length, the time up) and core 2 parks as at a fault of its own, so core 1 holds it at its next half
  (`STOPPED: FAULT`) and goes on alone, VOICES 16 counting as 12; on core 1 (core 2 off, its jobs run there) the
  record alone and the block's work goes on. STATUS' DEVICE shows it at once (`BAD FILL TO ... +n`, `ASKED FROM ...`);
  after a restart CORE 2 FAULTED shows it once: `A BAD FILL STOPPED IT` (`A BAD COPY`), `TO` and `LEN`, `FROM` (the
  caller) and `DBG`, the time up and the count, `NOT WRITTEN; CORE 1 WENT ON ALONE` (`ON CORE 1 (CORE 2 OFF); NOT
  WRITTEN`).
- **The build's code** names the very build a crash report comes from, as a release's version is its number alone: 8
  hex digits holding the build's time to the minute (UTC) and its kind (release, debug, dev, bring-up), scrambled so
  they read as no date, with a check that catches most misread digits. `fw/tools/buildcode.py CODE` reads one back;
  `fw/tools/build.py` makes it (with `SOURCE_DATE_EPOCH` set, that time: the same sources then build the same image).

## The toolchain

JieLi's clang 4 for pi32v2 (the SDK's), as found building PT/FM:

- **An int32 multiplier that is constant in a loop** is hoisted as a sign-extended 64-bit value, and
  `((int64_t)a * b) >> 24` becomes a 64 × 64 multiply (three multiplies): read such coefficients from memory the
  loop's stores may alias, a load per use and one multiply.
- **A call in a hot loop** spills most registers around it every time: inline it. Splitting a loop in two passes to
  ease register pressure made it worse.
- **Dependent 64-bit multiply chains** are the cost, and independent chains are emitted one after another, not
  interleaved.
- **A weak function's body** in the unit that calls it is used for register allocation at its calls, so a strong
  override linked in its place is miscalled: keep weak defaults in a unit of their own.
- **It can pair a dead load with a live write of the same register** in one instruction pair, which write wins
  unknown: PT/FM's build refuses any such pair in the image.
- **`memset` / `memcpy` must work by words** (byte by byte cost a busy song ~15 %), and a copy whose two ends differ
  mod 4 still goes byte by byte: align what is copied often.

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
- **Supplies:** as the boot loader leaves them, 1.40 V and 1.26 V (measured); every figure here is at those levels.
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

1 MiB Puya NOR (JEDEC 0x856014), read in place at 0x02000000 + (offset − 0x4000) through the code cache.

| Offsets | What |
|---|---|
| 0x000000-0x004000 | the boot code: never written |
| 0x004000-0x093000 | the firmware slot (the app from 0x4120, at 0x02000120 in place; 581,564 B), written only by installs; its last block holds the image's tail |
| 0x093000-0x0E0000 | free for data (PT/FM's user area, part 1) |
| 0x0E0000-0x0E5000 | the update's staging: keep free |
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
  handler asks `cnum`); a bus error is flagged in the debug unit both cores share and likely taken on core 1.
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
  after it; found by "FM-1" in the USB product name), stages the update loader ("ota-FM-1_900") at 0x0E0000,
  writes the update record and restarts; the loader writes the whole firmware slot, skipping sectors already equal.
  **An install cut short does not resume after a power-off** (it does while the FM-1 stays in update mode).
- **The USB rescue:** OCT− held alone at power-on (so OCT− is no boot key for anything else), or two failed starts in
  a row (a boot guard kept across restarts), start a mode that only takes an install.
- **The watchdog** (in the P33 power domain, reset mode, 1-32 s) and **a software restart** (P33's power register)
  both restart the chip; P33 says why (power-on, watchdog, software).

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

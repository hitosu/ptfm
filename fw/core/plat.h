/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* What the app (fw/core) needs from the hardware. fw/device/ gives it on the FM-1, emulator/ in a
 * browser, fw/tests/ on the PC. Nothing in fw/core touches a register or a fw/hal header: only this. */
#ifndef PTFM_PLAT_H
#define PTFM_PLAT_H
#include <stdint.h>

#define PLAT_RATE   44100   /* output frames a second */
#define PLAT_HALF   256     /* frames an audio half (5.8 ms): app_audio's block */
#define PLAT_W      240     /* the screen */
#define PLAT_H      240

/* ---- time ---- */
uint32_t plat_ms(void);                 /* milliseconds since power-on */
uint32_t plat_us(void);                 /* microseconds, wrapping: for CPU figures */

/* ---- input ----
 * Buttons by the FM-1's ids, notes by bit (bit 0 = F3 ... bit 26 = G5), the knobs' clicks counted since
 * the last plat_input(). Safe to call from the main loop; app_audio may read plat_notes() for timing. */
enum { B_OCTM, B_OCTP, B_FX, B_SEL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY,
       B_REC, B_COUNT };
enum { K_SELECT, K_ALG, K_K1, K_K2, K_K3, K_K4, K_PRESETS, K_COUNT };
typedef struct {
    uint32_t buttons;                   /* bit B_* held */
    uint32_t notes;                     /* bit n held */
    int16_t knob[K_COUNT];              /* clicks since the last call, + clockwise */
    int16_t knob_pre[K_COUNT];          /* of those, the clicks that came before the first new press (a button or
                                         * key down since the last call): taken before the presses, the rest after.
                                         * The caller zeroes the struct: 0 where a platform cannot tell */
    uint16_t master;                    /* MASTER, 0..1023 */
} plat_input_t;
void plat_input(plat_input_t *in);
uint32_t plat_notes(void);              /* the note keys held now (any context) */

/* ---- LEDs ----
 * id: a button 0-13 (B_*), a note key 14 + n (n 0-26), PLAY's green 41. level 0 off .. 255 full; 64 is
 * the UI's "dim" (a quarter). */
#define LED_NOTE(n)   (14 + (n))
#define LED_PLAY_G    41
#define LED_COUNT     42
void plat_led(int id, uint8_t level);

/* ---- screen ----
 * RGB565 pixels, native byte order, full-width rows y .. y + h - 1. Returns once the rows are taken (the
 * caller may reuse px). */
void plat_lcd_rows(int y, int h, const uint16_t *px);
/* 1 once after the platform drew a screen of its own over the app's (an update's), else 0: the app
 * then draws everything again. */
int plat_screen_lost(void);

/* ---- audio ----
 * The platform calls app_audio() for every half from its audio context (the FM-1: an interrupt) and
 * applies MASTER, the speaker's mono and the codec's scale itself. Its figures for STATUS: */
typedef struct {
    uint32_t last_us, max_us;           /* app_audio's time, the last half and the most since reset */
    uint32_t late;                      /* halves that came too late (a dropout) since reset */
    uint32_t half_us;                   /* a half's length: 5804 */
    uint32_t sum_us, halves;            /* app_audio's time summed, and the halves, since power-on (wrapping; never
                                         * reset): a mean over any stretch from two readings (OVER 9000) */
    uint32_t c2_last_us, c2_max_us, c2_sum_us;   /* core 2's jobs' time (the second core, below): the last half's,
                                         * the most since reset, summed since power-on over the same halves */
    uint8_t c2_state, c2_why;           /* core 2: PLAT_C2_STARTING .. _STOPPED; stopped, why (PLAT_C2_WHY_*) */
} plat_cpu_t;
void plat_cpu(plat_cpu_t *c, int reset);
/* SETTINGS' OUTPUT and LINE GAIN (specs/ui/screens/settings.md): the platform applies them to what
 * app_audio gives, with MASTER, then the soft ceiling. Stereo and 0 dB until the app sets them. */
void plat_output(int mono, int line_gain_db);
/* SETTINGS' USB GAIN (-24 .. +12 dB): USB audio's level, in MASTER's place (specs/connections.md#usb-audio); a
 * platform without USB audio ignores it. 0 dB until the app sets it. */
void plat_usb_gain(int db);

/* ---- the second core (specs/second-core.md) ----
 * Core 1 runs the app; core 2 may take each block's effects and output stage, handed over block by block as core 1
 * makes them (block j of the half: job(j)), and the block's voice job (core 2's places: the one job that runs from
 * the image and reads the flash). A platform with one core never takes a half (plat_weak.c's defaults).
 * plat_c2_half: at a half's start, every half (audio context, after the platform made sure core 2 finished the half
 * before): 1 this half's jobs go to core 2, each by plat_c2_post as it is made (CUT's halves hand over only their
 * voice jobs); 0 core 1 runs them itself, and VOICES 16 counts as 12. The answer holds for the whole half; a job not
 * taken (core 2 stopped meanwhile) runs at once on core 1. plat_c2_wait: core 1 waits for every job handed over. */
int plat_c2_half(void);
void plat_c2_post(void (*job)(uint32_t), uint32_t j);
#define PLAT_C2_VOICE_JOB 0x100u        /* a voice job's j (no block's number): the platform may tell them apart */
void plat_c2_enable(int on);            /* SETTINGS' SECOND CORE (main loop): from the next half */
void plat_c2_force_off(int off);        /* the debug build's 38: off whatever SETTINGS says (1), or back (0) */
void plat_c2_wait(void);                /* core 1 (audio context): until core 2 has finished what it was handed */
enum { PLAT_C2_STARTING, PLAT_C2_ON, PLAT_C2_OFF, PLAT_C2_STOPPED };      /* plat_cpu_t's c2_state */
enum { PLAT_C2_WHY_NONE, PLAT_C2_WHY_NO_ANSWER, PLAT_C2_WHY_MISREAD, PLAT_C2_WHY_STUCK, PLAT_C2_WHY_FAULT,
       PLAT_C2_WHY_CRASH };             /* its c2_why: what stopped core 2 (STATUS' DEVICE) */
/* Core 2's room (specs/second-core.md#ram): the code that may run on core 2 (a block's job and everything it reaches)
 * is marked PLAT_C2, its constant tables PLAT_C2_RO. On the FM-1 (its build defines both) they lie in RAM, out of a
 * direct call's reach from the image and the image from them: that code calls only code marked so, with memset and
 * memcpy of its own (c2_memset, c2_memcpy; no copy the compiler may turn into a memcpy call: build.py's closure
 * check catches one) and a timer read of its own (plat_c2_us), and core 1 calls into it through a pointer
 * (PLAT_FAR). Its inline helpers are marked too: the device's compiler inlines no function of another section but an
 * always_inline one. Elsewhere the marks are empty, PLAT_FAR a plain call and the rest the usual functions. */
#ifdef PLAT_C2
#ifndef PLAT_C2_RO
#define PLAT_C2_RO
#endif
void *c2_memset(void *d, int c, uint32_t n) PLAT_C2;
void *c2_memcpy(void *d, const void *s, uint32_t n) PLAT_C2;
uint32_t plat_c2_us(void) PLAT_C2;      /* plat_us, read by core 2's code */
#define PLAT_FAR(fn) (*(__typeof__(&fn) volatile *)&(__typeof__(&fn)){fn})   /* (a pointer the compiler cannot fold
                                                                            * back into a direct call) */
#else
#define PLAT_C2
#define PLAT_C2_RO
#define PLAT_FAR(fn) fn
#define c2_memset memset
#define c2_memcpy memcpy
#define plat_c2_us plat_us
#endif

/* ---- flash ----
 * Addresses are offsets into the 1 MiB flash. plat_flash_ptr() gives a pointer for reading in place
 * where that works (the user area), NULL where it does not (ROM's free part: direct reads only).
 * plat_flash_read() works everywhere (direct reads, interrupts off a 256-B window at a time).
 * Erase (4 KB, aligned) and program (1-256 B within one 256-B page) silence the audio around them
 * themselves; both return 0 when done, else -1 (outside the regions the platform allows). */
const uint8_t *plat_flash_ptr(uint32_t addr);
int plat_flash_read(uint32_t addr, void *dst, uint32_t n);
int plat_flash_erase(uint32_t addr);
int plat_flash_prog(uint32_t addr, const void *src, uint32_t n);
uint32_t plat_fw_end(void);             /* where the firmware's image ends (ROM's free part starts after) */

/* ---- USB-MIDI SysEx ----
 * PT/FM's messages (F0 7D 50 54 ... F7) reach app_sysex(); the platform keeps the update protocol's.
 * plat_sysex_send() returns 0 once the message is queued, -1 if busy (try again next pass). */
int plat_sysex_send(const uint8_t *msg, uint32_t n);

/* ---- MIDI: USB and the MIDI IN jack (specs/connections.md) ----
 * plat_midi_rx: the next message in, oldest first, from either port: a channel message (m[0] 0x80-0xEF,
 * its data bytes after it, 0 where it has none), the song position (F2, its two data bytes), or a realtime
 * byte alone (F8 clock, FA START, FB CONTINUE, FC STOP). SysEx never comes here (app_sysex). ms: when it
 * came (a realtime message: as it arrived, for the clock's tempo; others: about now). 1 a message, 0 none.
 * Main loop only.
 * plat_midi_tx: a message out over USB-MIDI, as plat_midi_rx gives them: 0 queued, -1 not (USB down, the
 * queue full). From the main loop and the audio context alike (the audio context's: as plat_midi_tx_at's frame 0).
 * plat_midi_tx_at: from the audio context, a message sent when frame `frame` (0-255) of the half being made is
 * heard (the clock out, the tracks' notes: specs/connections.md#clock); elsewhere, or a platform that does not
 * time them (the emulator), at once.
 * plat_master_set: MASTER from a MIDI knob learned onto it, 0..1023: the sound follows it until MASTER
 * itself moves. */
enum { PLAT_MIDI_USB, PLAT_MIDI_JACK };
typedef struct {
    uint8_t m[3];
    uint8_t src;                        /* PLAT_MIDI_USB or PLAT_MIDI_JACK */
    uint32_t ms;
} plat_midi_t;
int plat_midi_rx(plat_midi_t *e);
int plat_midi_tx(const uint8_t m[3]);
int plat_midi_tx_at(const uint8_t m[3], int frame);
void plat_master_set(uint16_t v);
/* USB bus resets since power-on (unplugged, the computer restarting): MIRROR and the debug build's test tone
 * end at one (specs/connections.md#mirror-the-panel-from-a-page). A platform without USB: 0 (plat_weak.c's
 * default). Main loop only. */
uint32_t plat_usb_resets(void);
/* USB configured by a host now (1), or not (0: unplugged, or no host yet); a platform that cannot tell: 1 */
int plat_usb_up(void);
/* USB power as the screens show it (the header's battery, STATUS, specs/ui/frame.md): configured and the host's frames
 * still coming (an unplug resets nothing on the bus, so plat_usb_up stays 1 till the next host; a sleeping host stops
 * them too); a platform that cannot tell: 1 */
int plat_usb_power(void);
/* plat_fast: fn from now on every millisecond in the platform's timer interrupt, with the input as it is then (the
 * knobs' clicks since the last call: plat_input no longer gets them) and the time in ms; NULL: no more. 1 the
 * platform does (the FM-1), 0 it cannot (fn never runs: the caller ticks from the main loop). For the controller
 * inside PT/FM (ctlmode.c), whose keys must not wait for the main loop. What fn may call: plat_midi_tx (the
 * controller alone sends then), plat_led; nothing else of plat.h. Main loop only. */
int plat_fast(void (*fn)(const plat_input_t *in, uint32_t ms));

/* ---- memory ----
 * A big buffer (more than a few KB) is marked PLAT_POOL: on the FM-1 it goes into the pool (the RAM after
 * the variables; the two share 432 KB), elsewhere it is an ordinary variable. */
#ifndef PLAT_POOL
#define PLAT_POOL
#endif
/* The sound's files are built for speed (-O2: build.py's HOT); a function of theirs that runs seldom (a request, a
 * song's start, a tick's or a row's work, AUTO LEVEL's setup, LOOPS' editing, the main loop's) is marked PLAT_COLD:
 * on the FM-1 it is built for size (the image's room is the slot's), never one a block or a sample runs.
 * Elsewhere empty. */
#ifndef PLAT_COLD
#define PLAT_COLD
#endif
/* The spare zone's memory (specs/files/ram.md#the-edit-memory-and-the-spare-zone): spare.c's own 24 KB, marked
 * PLAT_SPARE, and the room the platform has right after it, plat_spare_room() bytes, the zone's too. On the FM-1
 * the zone is the data region's last object, its room running up to the region's top (what the code leaves: at
 * least the spare the build keeps); elsewhere the array holds 8 KB more itself (PLAT_SPARE_EXTRA), so the host
 * tests and the emulator run with the least zone an FM-1 build may have. */
#ifndef PLAT_SPARE
#define PLAT_SPARE
#ifndef PLAT_SPARE_EXTRA
#define PLAT_SPARE_EXTRA 8192u
#endif
#endif
#ifndef PLAT_SPARE_EXTRA
#define PLAT_SPARE_EXTRA 0u
#endif
uint32_t plat_spare_room(void);
/* The RAM's parts the platform knows, for STATUS' RAM MAP (specs/ui/screens/status.md): the whole RAM; the code it
 * runs from RAM; the variables (.data and .bss, the big buffers apart); its own screen buffers (the FM-1's LCD
 * transfers). The FM-1 from its link; a platform with no such RAM (the emulator) gives the FM-1's figures. */
typedef struct { uint32_t total, ram_text, variables, screen; } plat_ram_t;
void plat_ram(plat_ram_t *r);

/* ---- long work and shared state ----
 * plat_idle: the main loop's long work (a walk over the flash, compaction) says it is alive: on the FM-1 it
 * feeds the watchdog. Main loop only. plat_lock / plat_unlock: the audio context held off while the main
 * loop changes what it reads (the store's map, the edits' list): briefly; nestable (unlock with what lock
 * gave). A platform without either has plat_weak.c's no-op defaults (weak: alone in that unit, see there). */
void plat_idle(void);
uint32_t plat_lock(void);
void plat_unlock(uint32_t was);

/* ---- the rest ---- */
int plat_battery(void);                 /* 0..100 %, or -1 not known */
void plat_fault(const char *why);       /* a dev build's assertion: the crash screen; never returns */

#endif

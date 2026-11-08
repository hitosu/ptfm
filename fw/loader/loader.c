/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Vasily Kazantsev (the include paths: PT/FM's fw/device; the screen, update mode, the hashes) */
/* The update loader (device side). Runs from RAM, started by the SPL from
 * an update record, with interrupts off: USB is polled from the main loop.
 * It is a USB-MIDI device (1209:0002, its USB names usb.c's FW_LOADER strings)
 * that answers the M-UPGRADE update protocol as "ota-FM-1_901" (hosts look for
 * "ota-"; PT/FM's pages also ask it for hashes: F0 22 24 35 7C F7), and writes
 * the app area only (ldr_core.c). The UBOOT soft key works here too.
 * Started two ways: by a firmware's install (step 1 staged it: INSTALLING), or
 * by the start check (fw/stub) for update mode, its box saying so (UPDATE MODE
 * and why, until a host starts an install). Its screen: a title, a line, a bar
 * of the slot's blocks, DON'T SWITCH OFF; the panel set up here (after a cold
 * start nothing else has). Single compilation unit. */
#include <stdint.h>
#define FW_LOADER 1
#define FW_OTA 1
#define FW_OTA_DRYRUN 0
#define FW_USB_PID 0x0002
#define FW_ID "ota-FM-1_901"
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "fm1_time.h"
#include "fm1_sys.h"
/* the loader may write the app area, and erase the staging's last block (an older firmware's flash copy of the
 * update record: ldr_core.c); never the head, never the user area */
#define FL_RANGE_OK(off, n) (FL_IN(off, n, 0x4000u, 0x93000u) || ((off) == 0xE4000u && (n) == 0x1000u))
#include "fm1_flash.h"
#include "fm1_lcd_hw.h"
#include "../device/libc.c"
#include "../device/usb.c"
#include "../stub/stub_core.h"
#include "ldr_font.h"                               /* (generated: tools/gen_fonts.py --loader) */
#include "../device/lcd.c"                          /* (at 12 MHz, its default: the safest) */

static uint32_t ldr_ms(void)   /* monotonic ms; survives the TIMER4 wrap (178.9 s) */
{
    static uint32_t last, acc, ms;
    uint32_t now = fm1_ticks();
    acc += now - last;
    last = now;
    ms += acc / (1000u * FM1_TICKS_PER_US);
    acc %= 1000u * FM1_TICKS_PER_US;
    return ms;
}

static void ldr_poll(void)                          /* USB at ~2 kHz, watchdog */
{
    static uint32_t last;
    uint32_t now = fm1_ticks();
    fm1_wdt_feed();
    if (now - last >= 500u * FM1_TICKS_PER_US) {
        last = now;
        usb_poll();
    }
}

#include "ldr_screen.c"                             /* the screen */

/* ---- ota.c hooks (the SysEx frame hooks are in usb.c) ---- */
static uint32_t ota_now_ms(void) { return ldr_ms(); }
static void ota_idle(void) { ldr_poll(); }
/* step-1 hooks of ota.c: not used by the loader */
static int ota_erase(uint32_t off) { (void)off; return -1; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { return fl_read_ram(off, p, n); }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }
#include "../device/ota.c"

/* ---- ldr_core.c hooks: interrupts are off for the whole loader ---- */
static uint32_t ldr_with_hashes;
static int ldr_fread(uint32_t off, void *p, uint32_t n) { return fl_read_ram(off, p, n); }
static int ldr_erase(uint32_t off)
{
    uint32_t took;
    return fl_erase4k_ram(off, &took);
}
static int ldr_prog(uint32_t off, const void *p, uint32_t n)
{
    uint32_t took;
    return fl_prog_ram(off, p, n, &took);
}
static void ldr_record_clear(void)
{
    fm1_updata_parm_clear();                        /* CRC 0: the SPL ignores it on a warm reset */
}
static int ldr_flash_known(void) { return fl_jedec_ok(fl_jedec_ram()); }   /* as the app (device/main.c) */
static int ldr_hashed(void) { return (int)ldr_with_hashes; }
static void ldr_progress(uint32_t phase, uint32_t done, uint32_t total)
{
    ldr_poll();
    ldr_shown(phase, done, total);
}
#include "ldr_core.c"

extern uint32_t _bss_start[], _bss_end[];

void ldr_main(void)
{
    volatile stub_box_t *box = fm1_stub_box();
    fm1_wdt_arm(0x0D);                              /* ~8 s, fed by ldr_poll */
    fm1_time_init();
    lcd_init();
    if (box->magic == STUB_BOX_MAGIC && box->last == STUB_LOADER) {
        ldr_update_mode(box->why);                  /* (the start check's: update mode) */
    } else {
        ldr_connecting();                           /* (a firmware's install: the page goes on in a moment) */
    }
    usb_start();
    for (;;) {
        ldr_poll();
        usb_retry(ldr_ms());
        if (usb.uboot_req) {                        /* soft key: mask-ROM UBOOT */
            usb_detach();
            fm1_delay_ms(30);
            fm1_enter_uboot();
        }
        ota_service();                              /* handshake */
        if (usb.ota_req) {
            int rc;
            ldr_with_hashes = usb.ota_req == 2u;
            usb.ota_req = 0;
            ldr_installing();
            rc = ldr_session();
            ldr_ended(rc);
            if (rc == 0 || rc == LDR_EFLASH) {      /* done, or another flash chip: the old firmware again */
                fm1_delay_ms(200);                  /* the "success" reply leaves */
                usb_detach();
                fm1_delay_ms(30);
                fm1_core_reset();                   /* the SPL boots the new app */
            }
        }
    }
}

void ldr_cstart(void)
{
    uint32_t *p;
    ldr_record_clear();                             /* first thing: a crash below cannot loop through a warm reset;
                                                     * a power-off during the write: the start check starts this
                                                     * loader again (it is kept in the staging) */
    for (p = _bss_start; p < _bss_end; p++)
        *p = 0;
    ldr_main();
}

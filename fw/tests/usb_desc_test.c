/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The USB device's descriptors as PT/FM's firmware builds them (fw/device/usb.c with FW_UAC, main.c's
 * settings): every descriptor's length adding up, the audio control header listing the audio streaming
 * interface before the MIDI one, the stream's two rates, 16-bit stereo, EP4 big enough for 48 kHz's packets
 * (specs/connections.md). Nothing of the SIE runs: only the tables are read.
 *   usb_desc_test */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define FW_OTA 1                    /* (as main.c: the firmware's usb.c) */
#define FW_UAC 1
#define USB_BCD_DEVICE 0x7102u
#define RING_PUBLISH() ((void)0)
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "../device/usb.c"
static uint32_t ota_now_ms(void) { return 0; }
static void ota_idle(void) {}

static int fails;
static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

int main(void)
{
    const uint8_t *d;
    uint16_t len;
    check(get_desc(0x0100, &d, &len) && len == 18 && (d[12] | d[13] << 8) == 0x7102, "the device: bcdDevice 71.02 "
          "(the audio input: hosts read the descriptors again)");
    {
        static const uint8_t serial[] = {10, 3, 'P', 0, 'T', 0, 'F', 0, 'M', 0};
        const uint8_t *s, *pr;
        uint16_t sl, pl;
        check(get_desc(0x0100, &d, &len) && d[16] == 3 && get_desc(0x0303, &s, &sl) && sl == sizeof serial &&
              !memcmp(s, serial, sizeof serial), "the serial number: string 3, \"PTFM\" (Windows names the FM-1 "
              "under PT/FM by its own strings)");
        check(get_desc(0x0302, &pr, &pl) && pl == 22 && pr[0] == 22 && pr[2] == 'F' && pr[20] == 'M',
              "the product: \"FM-1 PT/FM\"");
        check(!get_desc(0x0304, &s, &sl), "no string 4");
    }
    check(get_desc(0x0200, &d, &len) && len == sizeof CFG_DESC && (d[2] | d[3] << 8) == len && d[4] == 3,
          "the configuration: its total length its bytes, 3 interfaces");
    uint32_t p = 0, n = 0, ok = 1, ac_at = 0, ep4_at = 0, fmt_at = 0, as_alts = 0, midi = 0;
    while (p < len && ok) {                              /* every descriptor: its length, its type */
        uint32_t l = d[p], t = d[p + 1];
        if (l < 2 || p + l > len) { ok = 0; break; }
        if (t == 4 && d[p + 2] == 2) as_alts++;          /* the audio streaming interface's settings */
        if (t == 4 && d[p + 2] == 1 && d[p + 5] == 1 && d[p + 6] == 3) midi = 1;
        if (t == 0x24 && d[p + 2] == 1 && !ac_at) ac_at = p;
        if (t == 0x24 && d[p + 2] == 2 && l == 14) fmt_at = p;
        if (t == 5 && d[p + 2] == 0x84) ep4_at = p;
        p += l;
        n++;
    }
    check(ok && p == len, "every descriptor's length adds up to the total");
    check(ac_at && d[ac_at] == 10 && d[ac_at + 7] == 2 && d[ac_at + 8] == 2 && d[ac_at + 9] == 1,
          "the audio control header: the audio streaming interface (2) first, then the MIDI one (1)");
    {
        uint32_t cs = 0, q = ac_at;                      /* its wTotalLength: itself and the terminals after it */
        while (q < len && d[q + 1] == 0x24) { cs += d[q]; q += d[q]; }
        check(ac_at && (d[ac_at + 5] | d[ac_at + 6] << 8) == cs && cs == 31, "the audio control header's total: 31");
    }
    check(midi, "the MIDI streaming interface stays interface 1");
    check(as_alts == 2, "the audio streaming interface: alt 0 (no bandwidth) and alt 1 (the stream)");
    check(fmt_at && d[fmt_at + 4] == 2 && d[fmt_at + 5] == 2 && d[fmt_at + 6] == 16 && d[fmt_at + 7] == 2 &&
          (d[fmt_at + 8] | d[fmt_at + 9] << 8 | d[fmt_at + 10] << 16) == 44100 &&
          (d[fmt_at + 11] | d[fmt_at + 12] << 8 | d[fmt_at + 13] << 16) == 48000,
          "the format: PCM type I, 2 channels, 16 bits, 44100 and 48000 Hz");
    check(ep4_at && (d[ep4_at + 3] & 3) == 1 && (d[ep4_at + 4] | d[ep4_at + 5] << 8) >= 49 * 4 && d[ep4_at + 6] == 1,
          "EP4 IN: isochronous, room for 49 frames, every 1 ms");
    printf(fails ? "usb_desc_test: %d FAILED\n" : "usb_desc_test: all passed\n", fails);
    return fails ? 1 : 0;
}

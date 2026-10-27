/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The firmware's SysEx out (fw/device/usb.c ota_wire_send) when the host stops reading: a message longer than the
 * ring (64 packets; PT/FM's state answer is ~127) is cut short after 200 ms with part of it queued; its end, a lone
 * F7, is owed, sent once the host reads again (usb_sx_poll, or before the next message), so the host never reads
 * the cut message run into the next one; notes wait meanwhile (sx_busy). A fake clock (a millisecond each wait)
 * and a fake host taking packets from the ring; nothing of the SIE runs.
 *   usb_sx_test */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define FW_OTA 1                    /* (as main.c: the firmware's usb.c) */
#define FW_UAC 1
#define USB_BCD_DEVICE 0x7102u
#define RING_PUBLISH() ((void)0)
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "../device/usb.c"

static uint32_t clock_ms, host_reads, host_stops_at = 0xFFFFFFFFu;
static uint32_t got[4096], ngot;    /* the packets the host took, in order */
static void host_take(void)
{
    while (so_r != so_w && ngot < 4096u) {
        if (ngot >= host_stops_at) return;
        got[ngot++] = sx_out_q[so_r % SXQ];
        so_r++;
    }
}
static uint32_t ota_now_ms(void) { return clock_ms; }
static void ota_idle(void) { clock_ms++; if (host_reads) host_take(); }

static int fails;
static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

/* a message F0 .. F7 of n bytes, its bytes from seed */
static uint8_t msg[640];
static uint32_t make(uint32_t n, uint8_t seed)
{
    msg[0] = 0xF0;
    for (uint32_t i = 1; i + 1 < n; i++) msg[i] = (uint8_t)((seed + i) & 0x7F);
    msg[n - 1] = 0xF7;
    return n;
}
/* the host's packets from k on as the bytes of whole messages: each F0 .. F7, never one run into the next */
static int whole(uint32_t from, uint32_t to, uint32_t *messages)
{
    int in = 0;
    *messages = 0;
    for (uint32_t k = from; k < to; k++) {
        uint32_t cin = got[k] & 15u, b0 = got[k] >> 8 & 0xFF;
        if (cin == 4u && b0 == 0xF0) { if (in) return 0; in = 1; }   /* (a start inside a message: run together) */
        if (cin >= 5u && cin <= 7u) { if (!in && !(cin == 5u && b0 == 0xF7)) return 0; in = 0; (*messages)++; }
    }
    return !in;
}
static void reset_ring(void) { so_r = so_w = 0; sx_cut = 0; sx_busy = 0; ngot = 0; host_stops_at = 0xFFFFFFFFu; }

int main(void)
{
    usb.config = 1;
    uint32_t n = make(380, 1), m;              /* ~127 packets: a state answer's size */

    reset_ring(); host_reads = 1;
    check(ota_wire_send(msg, n) == 0 && !sx_busy, "the host reading: a message twice the ring's size goes out");
    host_take();
    check(whole(0, ngot, &m) && m == 1, "... whole: F0 .. F7");

    reset_ring(); host_reads = 1; host_stops_at = 30;
    uint32_t t0 = clock_ms;
    int r = ota_wire_send(msg, n);
    check(r == -1 && clock_ms - t0 >= 200u && clock_ms - t0 < 260u, "the host stops reading: given up after 200 ms");
    check(sx_cut && sx_busy, "... part of it queued: its end owed, the notes held");
    usb_sx_poll();
    check(sx_cut && sx_busy, "... the ring still full: still owed");
    host_stops_at = 0xFFFFFFFFu;               /* the host reads again */
    host_take();
    usb_sx_poll();
    host_take();
    check(!sx_cut && !sx_busy, "the host reads again: the end goes out, the notes go on");
    check(ngot && (got[ngot - 1] & 15u) == 5u && (got[ngot - 1] >> 8 & 0xFF) == 0xF7,
          "... the cut message ends with a lone F7 (CIN 5)");
    check(whole(0, ngot, &m) && m == 1, "... so the host reads it as one (short) message");

    reset_ring(); host_reads = 1; host_stops_at = 30;
    ota_wire_send(msg, n);                     /* cut short, its end owed */
    host_stops_at = 0xFFFFFFFFu;
    uint32_t n2 = make(100, 50);
    check(ota_wire_send(msg, n2) == 0 && !sx_cut && !sx_busy, "a message while an end is owed: the end first, then it");
    host_take();
    check(whole(0, ngot, &m) && m == 2, "... the host reads two messages, neither run into the other");

    reset_ring(); host_reads = 1; host_stops_at = 30;
    usb.config = 0;                            /* (unplugged: the bus reset empties the ring) */
    r = ota_wire_send(msg, n);
    check(r == -1 && !sx_cut && !sx_busy, "USB gone mid-message: nothing owed (the ring goes with the host)");
    usb.config = 1;

    reset_ring(); host_reads = 0;
    uint32_t n3 = make(30, 9);                 /* 10 packets: it fits */
    check(ota_wire_send(msg, n3) == 0 && !sx_cut, "a short message, the host not reading yet: it fits, nothing owed");

    /* SysEx in: one frame waits for the main loop, a short message begun meanwhile kept apart and served after it;
     * what still finds no room, or is cut short by another status, counted (usb_sx_lost) */
    static const uint8_t FIG[7] = {0xF0, 0x7D, 0x50, 0x54, 0x30, 0x00, 0xF7}, ASK[6] = {0xF0, 0x7D, 0x50, 0x54, 0x40, 0xF7};
    const uint8_t *p;
    uint32_t pn;
    uint16_t l0 = usb_sx_lost[0], l1 = usb_sx_lost[1];
    for (uint32_t i = 0; i < sizeof FIG; i++) sysex_byte(FIG[i]);
    for (uint32_t i = 0; i < sizeof ASK; i++) sysex_byte(ASK[i]);
    check(ota_frame_get(&p, &pn) && pn == 5 && p[3] == 0x30, "two questions before the main loop looks: the first first");
    ota_frame_done();
    check(ota_frame_get(&p, &pn) && pn == 4 && p[3] == 0x40, "... then the second, kept apart meanwhile");
    ota_frame_done();
    check(!ota_frame_get(&p, &pn) && usb_sx_lost[0] == l0, "... then none; nothing lost");
    for (uint32_t i = 0; i < sizeof FIG; i++) sysex_byte(FIG[i]);
    for (uint32_t i = 0; i < sizeof ASK; i++) sysex_byte(ASK[i]);
    for (uint32_t i = 0; i < sizeof ASK; i++) sysex_byte(ASK[i]);
    check(usb_sx_lost[0] == l0 + 1u, "a third while both wait: lost, counted");
    ota_frame_done();
    for (uint32_t i = 0; i < sizeof FIG; i++) sysex_byte(FIG[i]);
    check(usb_sx_lost[0] == l0 + 2u, "a frame begun while the short one waits (the main frame free): lost, counted "
          "(the order kept)");
    ota_frame_get(&p, &pn);
    ota_frame_done();
    check(!ota_frame_get(&p, &pn), "... the short one served, then none");
    for (uint32_t i = 0; i < sizeof FIG; i++) sysex_byte(FIG[i]);
    n3 = make(30, 9);
    for (uint32_t i = 0; i < n3; i++) sysex_byte(msg[i]);
    check(usb_sx_lost[0] == l0 + 3u, "a long one (30 B) while a frame waits: lost, counted");
    ota_frame_get(&p, &pn);
    ota_frame_done();
    sysex_byte(0xF0); sysex_byte(0x7D); sysex_byte(0xB0); sysex_byte(0x10); sysex_byte(0x01);
    check(usb_sx_lost[1] == l1 + 1u && !ota_frame_get(&p, &pn), "a message cut short by another status: counted, "
          "nothing kept");
    sysex_byte(0xF0); sysex_byte(0x7D); sysex_byte(0xF8); sysex_byte(0x50); sysex_byte(0x54); sysex_byte(0x40);
    sysex_byte(0xF7);
    check(usb_sx_lost[1] == l1 + 1u && ota_frame_get(&p, &pn) && pn == 4, "... realtime inside one does not cut it");
    ota_frame_done();

    printf(fails ? "usb_sx_test: %d FAILED\n" : "usb_sx_test: all passed\n", fails);
    return fails ? 1 : 0;
}

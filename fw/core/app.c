/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The app: the store, the player, MIDI and the UI, tied to the platform (app.h, plat.h). */
#include <string.h>
#include "app.h"
#include "plat.h"
#include "store/store.h"
#include "store/seed.h"
#include "play/player.h"
#include "midi/midi.h"
#include "ui/ui.h"
#include "play/dj.h"
#include "play/ahead.h"
#include "remote.h"
#include "ctlmode.h"
#include "dbg.h"                        /* the debug build's figures (empty in the release) */

/* SETTINGS' MIDI CONTROLLER (ctlmode.h): the tracker ends for good, till the FM-1 is switched off and on. Asked
 * (ui_controller_wanted), the player pauses, lets the keys go and fades out (CUT), and MIDI OUT ends its notes and
 * the clock; CTL_FADE_MS later nothing of the tracker runs any more (no store, player, effects, MIDI or UI; the
 * audio interrupt gives silence at once) and the controller's start screen comes */
#define CTL_FADE_MS 50u
static volatile uint8_t app_ctl;        /* the controller: the tracker ended */
static uint8_t app_ctl_asked;
static uint32_t app_ctl_t;

void app_init(void)
{
    store_init();                       /* mount the store: the scan, the map */
    player_init();                      /* engines, effects, the voices */
    ui_init();                          /* the first screen */
    midi_init();                        /* MIDI in and out (after the settings: ui_init read them) */
}

void app_poll(void)
{
    if (app_ctl) {
        ctlmode_poll();
        return;
    }
    if (app_ctl_asked && plat_ms() - app_ctl_t >= CTL_FADE_MS) {
        app_ctl = 1;                    /* (from here the tracker is never called again) */
        ctlmode_start();
        ctlmode_poll();
        return;
    }
    seed_poll();                        /* seeding's time-out (10 s without a message: mount) */
    if (!seed_active()) {
        store_poll();                   /* a SAVE under way, compaction */
        ahead_poll();                   /* where the patterns about to play have their tracks */
    }
    midi_poll();                        /* what came in over MIDI (read even while seeding), MIDI OUT's state */
    dj_poll();                          /* DJ's second deck's memory back to the spare zone once DJ has ended */
    ui_poll();                          /* input, the screens, LEDs; a seeding screen while seeding */
    remote_poll();                      /* the state a page asked for */
#ifdef PTFM_DEBUG
    dbg_poll();                         /* the figures the debug page asked for */
#endif
    if (!app_ctl_asked && ui_controller_wanted()) {
        player_pause();                 /* (MIDI OUT's notes end and the clock stops: midi_poll sees it) */
        player_keys_off();
        player_hush(1);                 /* the sound fades out (12 ms), nothing new starts */
        app_ctl_asked = 1;
        app_ctl_t = plat_ms();
    }
}

void app_audio(int16_t *lr, int frames)
{
    plat_c2_wait();                     /* (the half before: core 2 finished it before anything here reads or changes
                                         * what it worked on, as the FM-1's interrupt makes sure first thing; on every
                                         * way through, the early return's too) */
#ifdef PTFM_DEBUG
    static uint8_t made;                /* (a half made: its figures go into the window at the next one's start) */
    if (made) {                         /* the half before into the window, its blocks' jobs with it: core 2 has
                                         * finished them (the platform made sure before it called) */
        made = 0;
        dbg_half_end(player_voices_sounding());
    }
#endif
    if (app_ctl || seed_active()) {     /* the controller: no sound; the flash being written: no reading the store */
        if (lr)                         /* (no lr: the platform's buffer is silent already) */
            memset(lr, 0, (uint32_t)frames * 2 * sizeof *lr);
        return;
    }
    player_audio(lr, frames);           /* the replay, the voices, the effects, the mix */
    DBG_T(t);
    midi_audio(frames);                 /* CLOCK OUT's clocks, the tracks' notes sent ended */
    DBG_ADD(DS_MIDI, t);
#ifdef PTFM_DEBUG
    made = 1;
#endif
}

void app_audio_out(void (*out)(const int32_t *lr, int at, int n)) { player_out = out; }
void app_audio_usb(void (*usb)(const int32_t *lr, const int16_t *click, int n)) { player_usb = usb; }

void app_sysex(const uint8_t *msg, uint32_t n)
{
    if (app_ctl) {                      /* the controller: the page's question only (the updater's are the platform's) */
        ctlmode_sysex(msg, n);
        return;
    }
#ifdef PTFM_DEBUG
    if (dbg_sysex(msg, n)) return;      /* 30 FIGURES?, 32 BENCH, 34 USB TONE, 38 SECOND CORE (specs/debug.md) */
#endif
    if (remote_sysex(msg, n)) return;   /* 40 STATE? (specs/connections.md#the-state) */
    if (midi_sysex(msg, n)) return;     /* 43: the PT/FM controller's hello, MIRROR on or off */
    seed_sysex(msg, n);                 /* seeding's commands (specs/files/blob.md); others ignored */
}

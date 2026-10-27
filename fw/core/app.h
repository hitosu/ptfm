/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The app's entry points: what a platform (fw/device, emulator, fw/tests) calls. */
#ifndef APP_H
#define APP_H
#include <stdint.h>

void app_init(void);                            /* once, after the platform is up */
void app_poll(void);                            /* a pass of the main loop: never waits long */
void app_audio(int16_t *lr, int frames);        /* the audio context: frames stereo pairs to fill */
/* or, lr 0: each block handed out as it is made (16-bit values in int32, n frames from frame at; lr 0 for a silent
 * block), for a platform that writes them into its DMA buffer itself: a late half then plays what was made, else
 * silence */
void app_audio_out(void (*out)(const int32_t *lr, int at, int n));
/* USB audio's tap: each block's mix after the effects (16-bit values in int32, n frames), and the clicks in it to
 * leave out (0: none) */
void app_audio_usb(void (*usb)(const int32_t *lr, const int16_t *click, int n));
void app_sysex(const uint8_t *msg, uint32_t n); /* a PT/FM SysEx, F0 7D 50 54 ... F7, main-loop context */

#endif

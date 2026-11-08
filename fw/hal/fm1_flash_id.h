/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The FM-1's flash chips by their JEDEC ID (command 9Fh). FM-1s carry one of two 1 MiB NOR parts with the same
 * layout and commands (4 KB erase blocks, 256 B pages, a page programmed again in pieces before its erase): the app
 * (device/main.c) and the update loader (loader/loader.c) write to the flash only when it is one of them, and refuse
 * any other (../../specs/hardware.md, The flash). Host tests include this alone (tests/ldr_test.c). */
#pragma once
#include <stdint.h>

#define FL_JEDEC_PUYA 0x856014u                 /* Puya P25Q80, the FM-1s made first */
#define FL_JEDEC_UCUN 0xB36014u                 /* UCUN UC25HQ80, in FM-1s made later */

static inline int fl_jedec_ok(uint32_t id) { return id == FL_JEDEC_PUYA || id == FL_JEDEC_UCUN; }

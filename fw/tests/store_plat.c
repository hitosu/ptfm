/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The fake FM-1 of store_plat.h. The user area reads in place, the rest only with direct reads, as on the
 * device. Writes are allowed in the user area only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "store_plat.h"
#include "../core/plat.h"
#include "../core/store/store.h"

uint8_t fake_flash[1u << 20];
uint32_t fake_fw_end = 0x060000;
uint32_t fake_ms;
long fake_ops, fake_erases;
long cut_at = -1;
int cut_torn;
jmp_buf cut_env;
void (*fake_sysex)(const uint8_t *msg, uint32_t n);

void fake_reset(void)
{
    memset(fake_flash, 0xFF, sizeof fake_flash);
    fake_ops = fake_erases = 0;
    cut_at = -1;
    cut_torn = 0;
}

static int in_user(uint32_t a, uint32_t n)
{
    return (a >= STORE_U1_START && a + n <= STORE_U1_END) || (a >= STORE_U2_START && a + n <= STORE_U2_END);
}
static int writable(uint32_t a, uint32_t n) { return in_user(a, n); }

uint32_t plat_ms(void) { return fake_ms; }
uint32_t plat_us(void) { return fake_ms * 1000u; }
uint32_t plat_fw_end(void) { return fake_fw_end; }

long fake_reads, fake_read_bytes, fake_low_reads;

const uint8_t *plat_flash_ptr(uint32_t addr)
{
    if (addr < STORE_U1_START) fake_low_reads++;
    return in_user(addr, 1) ? &fake_flash[addr] : 0;
}

int plat_flash_read(uint32_t addr, void *dst, uint32_t n)
{
    if (addr + n > sizeof fake_flash || addr + n < addr) return -1;
    if (addr < STORE_U1_START) fake_low_reads++;
    fake_reads++;
    fake_read_bytes += n;
    memcpy(dst, &fake_flash[addr], n);
    return 0;
}

int plat_flash_erase(uint32_t addr)
{
    if (addr & 0xFFF || !writable(addr, 4096)) { fprintf(stderr, "erase refused at %06X\n", addr); return -1; }
    if (cut_at >= 0 && fake_ops >= cut_at) {
        if (cut_torn) {                                 /* an erase cut short: almost all 0xFF */
            memset(&fake_flash[addr], 0xFF, 4096);
            fake_flash[addr + 100] = 0x12; fake_flash[addr + 2000] = 0x00; fake_flash[addr + 4000] = 0x7F;
        }
        longjmp(cut_env, 1);
    }
    fake_ops++; fake_erases++;
    memset(&fake_flash[addr], 0xFF, 4096);
    return 0;
}

int plat_flash_prog(uint32_t addr, const void *src, uint32_t n)
{
    const uint8_t *s = (const uint8_t *)src;
    if (!n || n > 256 || (addr & ~255u) != ((addr + n - 1) & ~255u) || !writable(addr, n)) {
        fprintf(stderr, "program refused at %06X (%u B)\n", addr, n);
        return -1;
    }
    if (cut_at >= 0 && fake_ops >= cut_at) {
        if (cut_torn) for (uint32_t i = 0; i < n / 2; i++) fake_flash[addr + i] &= s[i];
        longjmp(cut_env, 1);
    }
    fake_ops++;
    for (uint32_t i = 0; i < n; i++) fake_flash[addr + i] &= s[i];
    return 0;
}

int fake_sysex_refused;
int plat_sysex_send(const uint8_t *msg, uint32_t n)
{
    if (fake_sysex_refused) return -1;
    if (fake_sysex) fake_sysex(msg, n);
    return 0;
}

void plat_fault(const char *why) { fprintf(stderr, "fault: %s\n", why); abort(); }
int plat_battery(void) { return -1; }
long fake_idles, fake_locks, fake_restarts;
int fake_lock_depth;
void plat_restart(void) { fake_restarts++; }
void plat_idle(void) { fake_idles++; }
uint32_t plat_lock(void) { fake_locks++; return (uint32_t)fake_lock_depth++; }
void plat_unlock(uint32_t was) { fake_lock_depth = (int)was; }
uint32_t plat_spare_room(void) { return 0; } /* (the spare zone's array alone: its own 24 KB, 8 KB PLAT_SPARE_EXTRA) */

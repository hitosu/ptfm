/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The emulator's <string.h>: WebAssembly is built without a C library (-ffreestanding -nostdlib), so
 * what fw/core may use of it is here (libc.c): the same functions as the FM-1's build gives
 * (fw/device/include/string.h), no more, so what links here links there. */
#ifndef PTFM_WEB_STRING_H
#define PTFM_WEB_STRING_H
#include <stddef.h>

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *d, const char *s);
char *strncpy(char *d, const char *s, size_t n);
char *strcat(char *d, const char *s);
char *strncat(char *d, const char *s, size_t n);
char *strchr(const char *s, int c);
char *strstr(const char *h, const char *n);

#endif

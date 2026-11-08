/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Vasily Kazantsev (PT/FM's mark) */
/* The update loader's entry. The SPL decompresses the loader to its load
 * address (0x01C0A800) and calls its first byte. Interrupts off,
 * our own stacks, clear .bss, then C. Never returns. */
	.section .text.crt0,"ax",@progbits
	.globl _start
_start:
	goto 1f
	.ascii "FELUCCA-LOADER-1"	/* 16 bytes: the marker firmwares recognise this loader by in a package */
	.ascii "PTFM-LD1"		/* 8 more: PT/FM's own, which PT/FM's firmware keeps in the staging (ota.c) */
	.balign 2
1:	cli
	sp = _stack_top
	ssp = _sstack_top
	call ldr_cstart
	goto .

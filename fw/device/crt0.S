/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Vasily Kazantsev (no notes of the boot's registers: nothing read them) */
/* App entry. The SPL maps app.bin at XIP 0x02000120 and jumps to its first
 * byte with r0 = boot-parameter block (0x01C7FE08, inside the vector table).
 * The SPL expects `goto +4; rts` as the first instruction. Interrupts off,
 * then our stacks (app.ld names their tops). */
	.section .text.crt0,"ax",@progbits
	.globl _start
_start:
	goto 1f
	rts
1:	cli
	sp = _ustack_top
	ssp = _sstack_top
	call fm1_cstart
	goto .

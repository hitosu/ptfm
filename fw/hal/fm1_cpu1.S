/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Charles Vestal (the second core's entry this follows)
 * Copyright (C) 2026 Kerem Kilic (the entry as first run on the FM-1)
 * Copyright (C) 2026 Vasily Kazantsev
 * After the AC79 SDK's startup (JieLi, Apache-2.0). */
/* Core 2's entry (the chip's numbering: its core 1), in the image: fm1_cpu1_start writes this address into the word
 * the chip starts core 2 from (vector 126's, 0x01C7FFF8). Interrupts off, no interrupt configuration, its own
 * stacks (app.ld's core 2's room: the jobs' for its user stack, the exceptions' for its supervisor stack), then out of
 * the start into fm1_c1_loop (in RAM, fm1_cpu1.h) by a return from exception: it runs on usp, the jobs' stack, and
 * never returns. */
	.section .text.cpu1,"ax",@progbits
	.balign 4
	.globl fm1_c1_entry
fm1_c1_entry:
	cli
	icfg = 0
	usp = _c2_ustack_top
	sp = _c2_sstack_top
	ssp = _c2_sstack_top
	reti = fm1_c1_loop
	rti

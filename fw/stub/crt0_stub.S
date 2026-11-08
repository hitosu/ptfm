/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The start check's entry: app.bin's first byte, where the chip's boot jumps (it expects `goto +4; rts` first, as
 * the firmware's own crt0). Interrupts off, the stacks at the main loop's and the interrupts' tops (stub.ld), then
 * C. Never returns. */
	.section .text.crt0,"ax",@progbits
	.globl _start
_start:
	goto 1f
	rts
1:	cli
	sp = _stub_stack
	ssp = _stub_sstack
	call stub_main
	goto .

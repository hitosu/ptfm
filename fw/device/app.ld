/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Vasily Kazantsev (the RAM laid out anew: specs/files/ram.md#the-layout) */
/* PT/FM's memory map (specs/files/ram.md, The layout). Code and constants execute in place from XIP (app.bin
 * offset 0 = 0x02000120). In RAM:
 *   0x01C00000   4,096  RAMTEXT: .ram_text, the code that must run while the flash is busy (the flash driver);
 *                       the link fails beyond
 *   0x01C01000 449,536  DATA, one region, set up at power-on: the pool first, the effects' object's at its very
 *                       bottom (its delay lines and punch-in buffer: read and written by the CPU only, where the
 *                       chip's scratch of a restart and the update loader of an update's restart land), then the
 *                       variables (.data, .bss), every DMA buffer among them above 0x01C08000 (build.py checks),
 *                       then the spare zone (.spare: spare.c's own 24 KB, and the room above it up to the region's
 *                       top, plat_spare_room: what the code leaves is the zone's; build.py refuses under 24 KB and
 *                       the spare)
 *   0x01C6EC00  40,960  C2RAM: core 2's room (specs/second-core.md#ram), from its bottom: its stacks (.c2_stack:
 *                       the jobs' 6 KB over the chip's ROM's start stack, then the exceptions' 1 KB), the code a
 *                       block's job reaches (.c2_text, PLAT_C2) and its tables (.c2_rodata, PLAT_C2_RO), then at or
 *                       above 0x01C72000 its loop and park (.c2_top), its queue (.c2_queue) and its fault record
 *                       (.c2_keep, never cleared); the code and tables copied from the image at power-on as
 *                       .ram_text is; core 1 calls into it through pointers (PLAT_FAR), out of a call's reach
 *   0x01C78C00     256  guard band (write-protected)
 *   0x01C78D00  16,128  the main loop's stack (_ustack_lo .. _ustack_top)
 *   0x01C7CC00     256  guard band (write-protected)
 *   0x01C7CD00  12,032  the interrupts' stack (_sstack_lo .. _sstack_top)
 *   0x01C7FC00     256  NOINIT: the records kept across restarts (the crash record, the boot guard)
 *   0x01C7FD00      76  unused, write-protected with the chip's own (from _records_top)
 *   0x01C7FD4C     692  the chip's own: boot info, the boot-state mailbox, the update record, the vectors
 * Each stack's bottom and top are named here; everything that needs them (the stack-pointer windows, the guard
 * bands, the debug build's stack depths) takes these names, never a neighbour's, so the stacks' order may change
 * here alone. build.py reads the region's bounds and its parts from these symbols. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x8DFBC
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x1000
    DATA   (rwx) : ORIGIN = 0x01C01000, LENGTH = 0x6DC00
    C2RAM  (rwx) : ORIGIN = 0x01C6EC00, LENGTH = 0xA000
    NOINIT (rw)  : ORIGIN = 0x01C7FC00, LENGTH = 0x100
}
SECTIONS
{
    .text : { _text_start = .; KEEP(*(.text.crt0)) *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > XIP
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) . = ALIGN(4); _rt_end = .; } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    /* core 2's room, placed by what the chip does (specs/second-core.md#ram):
     * - its stacks at the bottom: the jobs' (its usp) over the ROM's start stack 0x01C6F800-0x01C70000, which the
     *   chip's ROM writes at every start of core 2 (nothing else may lie there), then the exceptions' (its ssp);
     * - its code, its tables, then its loop and park, one run in RAM and in the image (fm1_cstart copies _c2_start ..
     *   _c2_end from _c2_load); core 1's write limit covers that run once copied, the PC limits' RAM window reaches
     *   from .ram_text to its end (fm1_guard.h); the write limit's ends on 256 B, as the guard bands' are;
     * - its loop and park, its queue and its fault record at or above 0x01C72000, where neither a restart nor the
     *   update loader writes, so a core 2 a reset did not hold only spins there (fm1_cpu1.h) */
    .c2_stack (NOLOAD) : {
        _c2_ustack_lo = .; . += 0x1800; _c2_ustack_top = .;   /* the jobs' stack, 6 KB */
        _c2_sstack_lo = .; . += 0x400; _c2_sstack_top = .;    /* the exceptions', 1 KB (core 2 takes no interrupts) */
    } > C2RAM
    .c2_text : { _c2_start = .; KEEP(*(.c2_text*)) . = ALIGN(4); _c2_text_end = .; } > C2RAM AT > XIP
    .c2_rodata : { KEEP(*(.c2_rodata*)) . = ALIGN(4); _c2_ro_end = .; } > C2RAM AT > XIP
    _c2_top_pad = _c2_ro_end < 0x01C72000 ? 0x01C72000 - _c2_ro_end : 0;   /* (should the code and tables shrink) */
    .c2_top : { . += _c2_top_pad; _c2_top_start = .; KEEP(*(.c2_top*)) . = ALIGN(256); _c2_end = .; } > C2RAM AT > XIP
    _c2_load = LOADADDR(.c2_text);
    .c2_queue (NOLOAD) : { . = ALIGN(64); _c2_queue = .; KEEP(*(.c2_queue*)) . = ALIGN(4); } > C2RAM
    .c2_keep (NOLOAD) : { _c2_keep = .; KEEP(*(.c2_keep*)) . = ALIGN(4); _c2_used = .; } > C2RAM
    .pool (NOLOAD) : {
        . = ALIGN(16); _pool_start = .;
        *core_fx_fx.o(.pool .pool.*)      /* the effects' lines and the punch-in buffer at the very bottom */
        _pool_fx_end = .;
        *(.pool .pool.*)
        . = ALIGN(4); _pool_end = .;
    } > DATA
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > DATA AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > DATA
    /* the spare zone last: its own 24 KB, then the room up to the region's top is the zone's too (plat_spare_room) */
    .spare (NOLOAD) : { . = ALIGN(16); _spare_lo = .; KEEP(*(.spare)) . = ALIGN(4); _spare_end = .; } > DATA
    .noinit (NOLOAD) : { _records_lo = .; *(.noinit*) _records_end = .; } > NOINIT
    _data_lo = ORIGIN(DATA); _data_top = ORIGIN(DATA) + LENGTH(DATA);
    _c2_lo = ORIGIN(C2RAM); _c2_top = ORIGIN(C2RAM) + LENGTH(C2RAM);
    _guard0 = 0x01C78C00; _ustack_lo = 0x01C78D00; _ustack_top = 0x01C7CC00;
    _guard1 = 0x01C7CC00; _sstack_lo = 0x01C7CD00; _sstack_top = 0x01C7FC00;
    _records_top = ORIGIN(NOINIT) + LENGTH(NOINIT);
    _restart_band = 0x01C07F00;           /* the 256 B the debug build patterns before a restart it makes and checks
                                           * at the next start (in the effects' lines: specs/debug.md) */
    ASSERT(_data_top <= _guard0, "the data region runs into the main loop's stack's guard band")
    ASSERT(_data_top <= _c2_lo && _c2_top <= _guard0, "core 2's room overlaps the data region or the guard band")
    ASSERT(ADDR(.c2_rodata) == _c2_text_end && ADDR(.c2_top) == _c2_ro_end
           && LOADADDR(.c2_rodata) == _c2_load + (ADDR(.c2_rodata) - _c2_start)
           && LOADADDR(.c2_top) == _c2_load + (ADDR(.c2_top) - _c2_start),
           "core 2's code, tables, loop and park are not one run in RAM and in the image (fm1_cstart copies one)")
    ASSERT(_c2_ustack_lo == _c2_lo && _c2_ustack_lo <= 0x01C6F800 && _c2_ustack_top >= 0x01C70000 + 0x400,
           "core 2's jobs' stack is not at its room's bottom over the ROM's start stack (0x01C6F800-0x01C70000, below its top 1 KB: fm1_cpu1_start)")
    ASSERT(_c2_ustack_top - _c2_ustack_lo == 0x1800 && _c2_sstack_lo == _c2_ustack_top
           && _c2_sstack_top - _c2_sstack_lo == 0x400 && _c2_start >= _c2_sstack_top,
           "core 2's stacks are not the jobs' 6 KB, then the exceptions' 1 KB, under its code")
    ASSERT(_c2_top_start >= 0x01C72000 && _c2_queue >= 0x01C72000 && _c2_keep >= 0x01C72000,
           "core 2's loop, queue or fault record lies below 0x01C72000 (a restart or the update loader writes there)")
    ASSERT(_c2_start % 256 == 0 && _c2_end % 256 == 0, "core 2's code is not on 256 B for core 1's write limit")
    ASSERT(_c2_used <= _c2_top, "core 2's room overflows")
    ASSERT(_ustack_lo == _guard0 + 0x100 && _sstack_lo == _guard1 + 0x100, "a guard band is not 256 B")
    ASSERT(_sstack_top <= ORIGIN(NOINIT), "the interrupts' stack runs into the records")
    ASSERT(_records_top <= 0x01C7FD4C, "the records run into the chip's own")
    /DISCARD/ : { *(.comment) *(.note*) }
}

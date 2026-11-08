/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The start check (stub.c): app.bin's first 0xEE0 bytes, in place at 0x02000120 (flash 0x4120 .. 0x5000), the
 * firmware from 0x02001000. In RAM, for the few milliseconds it runs: its read routine at RAMTEXT (the firmware's
 * own .ram_text later), its variables at the bottom of the main loop's stack and its stack at that stack's top (the
 * firmware's app.ld: 0x01C78D00 .. 0x01C7CC00), the box at 0x01C7FCF0 (stub_core.h). No initialised variables. */
MEMORY
{
    STUB   (rx)  : ORIGIN = 0x02000120, LENGTH = 0xEE0
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x1000
    SRAM   (rw)  : ORIGIN = 0x01C78D00, LENGTH = 0x800
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*) . = ALIGN(4); } > STUB
    .ram_text : { _srt = .; KEEP(*(.ram_text*)) . = ALIGN(4); _ert = .; } > RAMTEXT AT > STUB
    _lrt = LOADADDR(.ram_text);
    .data : { *(.data*) *(.sdata*) } > SRAM AT > STUB
    .bss (NOLOAD) : { _sbss = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _ebss = .; } > SRAM
    _stub_stack = 0x01C7CC00;
    _stub_sstack = 0x01C7FC00;
    ASSERT(SIZEOF(.data) == 0, "the start check has initialised variables: make them constants")
    /DISCARD/ : { *(.comment) *(.note*) *(.noinit*) }
}

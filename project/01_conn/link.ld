/*
 * Linker script for nRF54L15 application core (Cortex-M33), bare metal.
 *
 * Memory map (from Zephyr SoC DTS, cpuapp, secure):
 *   RRAM (non-volatile, code+rodata) : 0x00000000, 1524 KB
 *   SRAM (RAM)                       : 0x20000000,  256 KB
 *
 * This is a STANDALONE image: the vector table sits at 0x00000000, so the
 * core boots directly into it (no MCUboot). Flash it to RRAM offset 0.
 */

ENTRY(Reset_Handler)

MEMORY
{
    RRAM (rx)  : ORIGIN = 0x00000000, LENGTH = 1524K
    RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}

/* Top of stack = end of RAM (full descending stack). */
_estack = ORIGIN(RAM) + LENGTH(RAM);

/* Minimum heap/stack you want guaranteed to exist (link fails otherwise). */
_Min_Heap_Size  = 0x400;   /* 1 KB */
_Min_Stack_Size = 0x1000;  /* 4 KB */

SECTIONS
{
    .isr_vector :
    {
        KEEP(*(.isr_vector))
    } > RRAM

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7) *(.glue_7t)
        *(.eh_frame)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
    } > RRAM

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > RRAM

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > RRAM
    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > RRAM

    /* C++ static constructors / init arrays (unused now, kept for later). */
    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > RRAM
    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > RRAM

    . = ALIGN(4);
    _sidata = LOADADDR(.data);   /* .data load address in RRAM */

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > RRAM

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } > RAM

    ._user_heap_stack (NOLOAD) :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } > RAM

    /DISCARD/ : { *(.ARM.attributes) }
}

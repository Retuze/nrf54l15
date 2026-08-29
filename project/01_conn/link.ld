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

/* 栈保护：RAM 顶端 _estack 之下预留 4 KB 不入堆（__heap_end 用它定位） */
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

    __tdata_source = LOADADDR(.tdata);
    .tdata :
    {
        . = ALIGN(4);
        __tdata_start = .;
        *(.tdata)
        *(.tdata*)
        . = ALIGN(8);
        __tdata_end = .;
    } > RAM AT > RRAM

    __tdata_source_end = __tdata_source + SIZEOF(.tdata);
    __tdata_size       = __tdata_end - __tdata_start;

    /* TLS（picolibc local-exec 静态 TLS 块；startup.c 里 _set_tls(&__tls_base)） */
    __tls_base       = ADDR(.tdata);
    __tls_align      = MAX(ALIGNOF(.tdata), ALIGNOF(.tbss));
    __arm32_tls_tcb_offset = MAX(8, __tls_align);

    .tbss (NOLOAD) :
    {
        . = ALIGN(4);
        __tbss_start = .;
        *(.tbss)
        *(.tbss*)
        . = ALIGN(4);
        __tbss_end = .;
    } > RAM

    __tls_size = __tbss_end - __tls_base;

    /* .bss 清零区：__bss_start = ADDR(.tbss)，startup 从 __bss_start
     * 清零到 _ebss 一条龙覆盖 tbss+bss */
    __bss_start = ADDR(.tbss);
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

    /* 堆：bss 之后向上长（picolibc sbrk 用 __heap_start/__heap_end）；
     * 栈：RAM 顶端（_estack）向下，之间留 _Min_Stack_Size 保护 */
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    __heap_start = .;
    __heap_end   = ORIGIN(RAM) + LENGTH(RAM) - _Min_Stack_Size;

    /DISCARD/ : { *(.ARM.attributes) }
}

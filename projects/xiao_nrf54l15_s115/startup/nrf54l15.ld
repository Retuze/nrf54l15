/*
 * nRF54L15 linker script — static library linking (no hex merge).
 *
 * Flash (RRAM):  0x00000000 .. 0x00180000  (1.5 MB, app + MPSL + SDC linked together)
 * RAM:            0x20000080 .. 0x20040000  (~256 KB, below 0x80 = CRACEN/KMU push area)
 *
 * Library .data/.bss from libmpsl.a and libsoftdevice_controller_multirole.a
 * are merged into the output sections by the linker — no separate SoftDevice
 * RAM partition needed.
 *
 * picolibc crt0 symbols:
 *   _start expects: __data_start, __data_end, __data_source,
 *                   __bss_start, __bss_end,
 *                   __tls_* (TLS support),
 *                   __heap_start, __heap_end, __stack
 */

MEMORY
{
    RRAM (rx)  : ORIGIN = 0x00000000, LENGTH = 0x00180000
    RAM  (rwx) : ORIGIN = 0x20000080, LENGTH = 0x0003FF80
}

__app_ram_start = ORIGIN(RAM);
__app_ram_end   = ORIGIN(RAM) + LENGTH(RAM);

__heap_min   = 0xC000;   /* 48 KB heap — Opus encoder scratch allocs */
__stack_size = 0x2000;   /*  8 KB main stack */

ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector : {
        KEEP(*(.isr_vector))
    } > RRAM

    .text : ALIGN(4) {
        *(.text .text.*)
        *(.init .fini)
        *(.glue_7 .glue_7t)
        *(.rodata .rodata.*)
        . = ALIGN(4);
        __text_end = .;
        PROVIDE(__etext = __text_end);
        PROVIDE(_etext  = __text_end);
        PROVIDE(etext   = __text_end);
    } > RRAM

    .preinit_array : ALIGN(4) {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > RRAM

    .init_array : ALIGN(4) {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > RRAM

    .fini_array : ALIGN(4) {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > RRAM

    .data : ALIGN(4) {
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(4);
        __data_end   = .;
    } > RAM AT > RRAM
    __data_source = LOADADDR(.data);
    __data_size   = __data_end - __data_start;

    PROVIDE(_sdata  = __data_start);
    PROVIDE(_edata  = __data_end);
    PROVIDE(_sidata = __data_source);

    .tdata : ALIGN(4) {
        __tls_base    = .;
        __tls_first   = .;
        __tdata_start = .;
        *(.tdata .tdata.*)
        __tdata_end   = .;
    } > RAM AT > RRAM
    __tdata_source = LOADADDR(.tdata);
    __tdata_size   = __tdata_end - __tdata_start;

    .tbss (NOLOAD) : ALIGN(4) {
        __tbss_start = .;
        *(.tbss .tbss.*)
        *(.tcommon)
        __tbss_end = .;
        __tls_end  = .;
    } > RAM
    __tls_start = SIZEOF(.tdata) ? ADDR(.tdata) : ADDR(.tbss);
    __tls_align = MAX(ALIGNOF(.tdata), ALIGNOF(.tbss));
    __tls_head_start = __tls_start;
    __tls_head_end = __tls_start;
    __tls_head_size = 0;
    __tbss_size = __tbss_end - __tbss_start;
    __tbss_offset = ADDR(.tbss) - __tls_start;
    __tls_space_size = __tls_end - __tls_start;
    __tls_space = __tls_start;
    __tls_space_pad = ALIGN(__tls_space_size, __tls_align) - __tls_space_size;
    __tls_tail_start = __tls_end;
    __tls_tail_end = __tls_end;
    __tls_tail_size = __tls_space_pad;
    __tls_size  = __tls_space_size + __tls_tail_size;
    __arm32_tls_tcb_offset = MAX(8, __tls_align);

    .bss (NOLOAD) : ALIGN(4) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end   = .;
    } > RAM
    __bss_size = __bss_end - __bss_start;

    PROVIDE(_sbss         = __bss_start);
    PROVIDE(_ebss         = __bss_end);
    PROVIDE(__bss_start__ = _sbss);

    ._user_heap_stack (NOLOAD) : ALIGN(8) {
        . = ALIGN(8);
        PROVIDE(end  = .);
        PROVIDE(_end = .);
        __heap_start     = .;
        . = . + __heap_min;
        . = . + __stack_size;
        . = ALIGN(8);
        __stack = .;
    } > RAM

    __stack_limit  = __stack - __stack_size;
    __heap_end     = __stack_limit;
    PROVIDE(__heap_size = __heap_end - __heap_start);
    PROVIDE(__StackTop   = __stack);
    PROVIDE(_estack      = __stack);
    PROVIDE(__StackLimit = __stack_limit);

    /DISCARD/ : {
        *(.ARM.exidx .ARM.exidx.*)
        *(.ARM.extab .ARM.extab.*)
        *(.eh_frame  .eh_frame.*)
    }

    ASSERT(_end + __heap_min + __stack_size <= ORIGIN(RAM) + LENGTH(RAM),
           "data+bss+heap+stack exceeds RAM — reduce .bss, __heap_min, or __stack_size")
    ASSERT(__heap_end >= __heap_start,
           "heap upper bound invalid")
    ASSERT(__heap_end >= __heap_start + __heap_min,
           "heap below __heap_min — reduce __stack_size or enlarge RAM")
}

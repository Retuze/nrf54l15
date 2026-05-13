/*
 * nrf52840.ld - Linker script for nRF52840 + S140 + picolibc.
 *
 * S140 v7.3.0 hex occupies 0x0000-0x26498. App at 0x27000 (next page).
 * Symbol naming follows picolibc crt0 expectations (__ prefix).
 */
MEMORY
{
    RRAM (rx) : ORIGIN = 0x00027000, LENGTH = 0x000D9000
    RAM  (rwx): ORIGIN = 0x20002800, LENGTH = 0x0003D800
}

__softdevice_start = 0x00027000;
__sd_ram_start     = 0x20000000;
__sd_ram_size      = 0x00002800;
__app_ram_start    = 0x20002800;

ENTRY(Reset_Handler)

SECTIONS
{
    .text : {
        KEEP(*(.vectors))
        *(.text .text.* .gnu.linkonce.t.*)
        *(.glue_7t) *(.glue_7)
        *(.rodata .rodata.* .gnu.linkonce.r.*)
        PROVIDE(__text_end = .);
        PROVIDE(__etext = __text_end);
    } > RRAM

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > RRAM
    .ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > RRAM

    .preinit_array : {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > RRAM

    .init_array : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > RRAM

    .fini_array : {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > RRAM

    __data_start = .;
    .data : {
        *(.data .data.* .gnu.linkonce.d.*)
        SORT(CONSTRUCTORS)
        __data_end = .;
    } > RAM AT > RRAM
    __data_source = LOADADDR(.data);
    __data_size   = __data_end - __data_start;

    .tdata : {
        __tls_base    = .;
        __tdata_start = .;
        *(.tdata .tdata.* .gnu.linkonce.td.*)
        __tdata_end   = .;
    } > RAM AT > RRAM
    __tdata_source = LOADADDR(.tdata);
    __tdata_size   = __tdata_end - __tdata_start;

    .tbss (NOLOAD) : {
        __tbss_start = .;
        *(.tbss .tbss.* .gnu.linkonce.tb.*) *(.tcommon)
        __tbss_end = .;
        __tls_end  = .;
    } > RAM
    __tls_start = SIZEOF(.tdata) ? ADDR(.tdata) : ADDR(.tbss);
    __tls_align = MAX(ALIGNOF(.tdata), ALIGNOF(.tbss));
    __tbss_size = __tbss_end - __tbss_start;
    __tbss_offset = ADDR(.tbss) - __tls_start;
    __tls_space_size = __tls_end - __tls_start;
    __tls_space = __tls_start;
    __tls_size  = __tls_space_size;
    __arm32_tls_tcb_offset = MAX(8, __tls_align);

    __bss_start = .;
    .bss (NOLOAD) : {
        *(.dynbss) *(.bss .bss.* .gnu.linkonce.b.*) *(COMMON)
        __bss_end = .;
    } > RAM
    __bss_size = __bss_end - __bss_start;

    .heap (NOLOAD) : {
        PROVIDE(end  = .);
        PROVIDE(_end = .);
        __heap_start = .;
        . = . + __heap_min;
        __heap_end = .;
    } > RAM

    .stack (NOLOAD) : {
        . = ALIGN(8);
        __stack_limit = .;
        . = . + __stack_size;
        __stack = .;
    } > RAM

    PROVIDE(__StackTop = __stack);
}

__heap_min   = 0x4000;
__stack_size = 0x2000;

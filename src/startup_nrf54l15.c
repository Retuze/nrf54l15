/*
 * Minimal startup for nRF54L15 application core (Cortex-M33 / ARMv8-M).
 *
 * Responsibilities:
 *   1. Provide the vector table (initial SP + Reset + core exceptions + IRQs).
 *   2. On reset: enable the FPU, copy .data from RRAM to RAM, zero .bss,
 *      then jump to main().
 *
 * No libc, no SystemInit magic: the SoC boots on its internal oscillator,
 * which is enough for GPIO. Clock/HFXO setup is done later, per-experiment.
 */

#include <stdint.h>
#include "uart.h"

/* Symbols provided by the linker script (nrf54l15_cpuapp.ld). */
extern uint32_t _sidata;   /* .data load address (in RRAM)            */
extern uint32_t _sdata;    /* .data start (in RAM)                    */
extern uint32_t _edata;    /* .data end   (in RAM)                    */
extern uint32_t _sbss;     /* .bss start                              */
extern uint32_t _ebss;     /* .bss end                                */
extern uint32_t _estack;   /* top of stack (end of RAM)               */

int  main(void);
void Reset_Handler(void);
void Default_Handler(void);

/* Core exception handlers: weakly aliased to Default_Handler so a real
 * project can override any of them by just defining a function of the
 * same name. */
#define WEAK_ALIAS __attribute__((weak, alias("Default_Handler")))

void HardFault_Handler(void);  /* defined below (records fault, no wfi) */

void NMI_Handler(void)        WEAK_ALIAS;
void MemManage_Handler(void)  WEAK_ALIAS;
void BusFault_Handler(void)   WEAK_ALIAS;
void UsageFault_Handler(void) WEAK_ALIAS;
void SecureFault_Handler(void)WEAK_ALIAS;
void SVC_Handler(void)        WEAK_ALIAS;
void DebugMon_Handler(void)   WEAK_ALIAS;
void PendSV_Handler(void)     WEAK_ALIAS;
void SysTick_Handler(void)    WEAK_ALIAS;

/* All peripheral IRQs default to Default_Handler for now. Named handlers
 * (e.g. RADIO_0_IRQHandler) can be added later and will override these. */
void Default_IRQHandler(void) WEAK_ALIAS;

typedef void (*vector_t)(void);

/* 16 core vectors + IRQ slots. 128 IRQ slots comfortably covers the
 * nRF54L15; unused slots simply point at Default_Handler. */
__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[16 + 128] = {
    (vector_t)(&_estack),   /* 0x00 Initial Stack Pointer */
    Reset_Handler,          /* 0x04 Reset                 */
    NMI_Handler,            /* 0x08 NMI                   */
    HardFault_Handler,      /* 0x0C HardFault             */
    MemManage_Handler,      /* 0x10 MemManage             */
    BusFault_Handler,       /* 0x14 BusFault              */
    UsageFault_Handler,     /* 0x18 UsageFault            */
    SecureFault_Handler,    /* 0x1C SecureFault           */
    0, 0, 0,                /* 0x20-0x28 Reserved         */
    SVC_Handler,            /* 0x2C SVCall                */
    DebugMon_Handler,       /* 0x30 DebugMonitor          */
    0,                      /* 0x34 Reserved              */
    PendSV_Handler,         /* 0x38 PendSV                */
    SysTick_Handler,        /* 0x3C SysTick               */

    /* IRQ0..IRQ127 -> Default_IRQHandler */
    [16 ... 16 + 128 - 1] = Default_IRQHandler,
};

void Reset_Handler(void)
{
    /* Enable the FPU (CP10 & CP11 full access) before any FP instruction,
     * required because we compile with -mfloat-abi=hard. */
    volatile uint32_t *CPACR = (volatile uint32_t *)0xE000ED88u;
    *CPACR |= (0xFu << 20);
    __asm volatile ("dsb");
    __asm volatile ("isb");

    /* Copy initialized data from RRAM (LMA) to RAM (VMA). */
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    /* Zero the .bss section. */
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0u;
    }

    (void)main();

    /* main() should not return; if it does, trap here. */
    for (;;) {
        __asm volatile ("wfi");
    }
}

/* Fault diagnostics, readable via pyOCD:
 *   g_fault[0] = magic 0xFA017000 once a HardFault happened
 *   g_fault[1] = CFSR, [2] = HFSR, [3] = stacked PC, [4] = stacked LR
 * Spin WITHOUT wfi so the debug AP stays powered and we can always re-attach. */
volatile uint32_t g_fault[8];

__attribute__((used)) void hardfault_c(uint32_t *frame)
{
    g_fault[0] = 0xFA017000u;
    g_fault[1] = *(volatile uint32_t *)0xE000ED28u; /* CFSR */
    g_fault[2] = *(volatile uint32_t *)0xE000ED2Cu; /* HFSR */
    g_fault[3] = frame[6];                          /* stacked PC */
    g_fault[4] = frame[5];                          /* stacked LR */
    uprintf("\n!!! HARDFAULT  CFSR=0x%08x HFSR=0x%08x PC=0x%08x LR=0x%08x\n",
            g_fault[1], g_fault[2], g_fault[3], g_fault[4]);
    for (;;) {
    }
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4        \n"
        "ite eq            \n"
        "mrseq r0, msp     \n"
        "mrsne r0, psp     \n"
        "b hardfault_c     \n");
}

void Default_Handler(void)
{
    for (;;) {
        /* spin, no wfi */
    }
}

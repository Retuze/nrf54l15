/*
 * startup_nrf54l15.c
 *
 * Vector table + Reset_Handler: MDK SystemInit() enables FPU/clocks,
 * then picolibc crt0 (_start) zeros .bss, copies .data, inits TLS,
 * runs C++ static constructors, and calls main().
 *
 * IRQn values: MDK nrf.h → nrf54l15_application.h (285 entries,
 * Cortex-M33 exceptions + 270 device IRQs).
 *
 * MPSL/SDC IRQ handlers are provided by the pre-compiled static
 * libraries (libmpsl.a, libsoftdevice_controller_multirole.a).
 * When linked, the libraries' strong ISR symbols fill the corresponding
 * vector table slots automatically — no forwarding wrappers needed.
 */

#include "nrf.h"
#include <stdint.h>

extern uint32_t __stack;
extern void     _start(void) __attribute__((noreturn));

void Default_Handler(void) { while (1) { __asm volatile("nop"); } }

/* Board / driver ISRs (must appear in g_pfnVectors — linker does not fill the table) */
extern void CLOCK_POWER_IRQHandler(void);
extern void TIMER20_IRQHandler(void);
extern void nrfx_i2s_20_irq_handler(void);
extern void SERIAL20_IRQHandler(void);
/* ---- System / fault handlers ------------------------------------------ */

void Reset_Handler(void) __attribute__((noreturn));

/* MDK SystemInit() enables FPU, sets up APPROTECT, trims, and PLL.
 * No need for manual fpu_enable() here — SystemInit() handles it. */
extern void SystemInit(void);

void NMI_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void SecureFault_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)      __attribute__((weak, alias("Default_Handler")));

/* SVC used by SDC (libsoftdevice_controller_multirole.a provides the handler) */
void SVC_Handler(void)          __attribute__((weak, alias("Default_Handler")));

typedef void (*irq_handler_t)(void);

/* ---- Vector table (285 entries) --------------------------------------- */

__attribute__((section(".isr_vector"), used))
const irq_handler_t g_pfnVectors[285] = {
    /* System exceptions (ARMv8-M) */
    [0]  = (irq_handler_t)(uintptr_t)&__stack,
    [1]  = Reset_Handler,
    [2]  = NMI_Handler,
    [3]  = HardFault_Handler,
    [4]  = MemManage_Handler,
    [5]  = BusFault_Handler,
    [6]  = UsageFault_Handler,
    [7]  = SecureFault_Handler,
    [11] = SVC_Handler,
    [12] = DebugMon_Handler,
    [14] = PendSV_Handler,
    [15] = SysTick_Handler,

    /* Device IRQs used in Phase 1 (unset slots stay 0 — do not enable IRQ without an entry) */
    [16 + TIMER20_IRQn]      = TIMER20_IRQHandler,      /* P2.00 软件 PWM（无 GPIOTE） */
    [16 + CLOCK_POWER_IRQn]  = CLOCK_POWER_IRQHandler,  /* nrfx clock / power */
    [16 + I2S20_IRQn]        = nrfx_i2s_20_irq_handler, /* I2S20 正弦波输出 */
    [16 + UARTE20_IRQn]      = SERIAL20_IRQHandler,      /* UARTE20 DMA TX */

    /* MPSL/SDC (Phase 2): add [16 + TIMER10_IRQn], [16 + GRTC_3_IRQn], etc. when linked */
};

__attribute__((noreturn))
void Reset_Handler(void)
{
    SystemInit();
    _start();
    for (;;) { }
}

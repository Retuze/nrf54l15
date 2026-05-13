/*
 * startup_nrf52840.c - Vector table and Reset_Handler for nRF52840 (Cortex-M4F).
 */

#include <stdint.h>

/* ---- External symbols ------------------------------------------------- */

extern uint32_t __stack;
extern void     _start(void);
extern void     SystemInit(void);

/* ---- Forward declarations --------------------------------------------- */

void Reset_Handler(void);
extern void GPIOTE_IRQHandler(void);

/* ---- Forward IRQ handlers for SoftDevice forwarding -------------------- */

extern void POWER_CLOCK_IRQHandler(void);
extern void RADIO_IRQHandler(void);
extern void RTC0_IRQHandler(void);
extern void TIMER0_IRQHandler(void);
extern void RNG_IRQHandler(void);
extern void ECB_IRQHandler(void);
extern void CCM_AAR_IRQHandler(void);
extern void TEMP_IRQHandler(void);
extern void SWI0_EGU0_IRQHandler(void);
extern void SWI1_EGU1_IRQHandler(void);
extern void SWI2_EGU2_IRQHandler(void);
extern void SWI3_EGU3_IRQHandler(void);
extern void SWI4_EGU4_IRQHandler(void);
extern void SWI5_EGU5_IRQHandler(void);

/* ---- Default handlers (weak — strong overrides win at link time) ------ */

__attribute__((weak)) void Default_Handler(void)   { for (;;) { } }
__attribute__((weak)) void HardFault_Handler(void) { for (;;) { } }
__attribute__((weak)) void SVC_Handler(void)       { for (;;) { } }
__attribute__((weak)) void PendSV_Handler(void)    { for (;;) { } }
__attribute__((weak)) void SysTick_Handler(void)   { for (;;) { } }

/* ---- Vector table (Cortex-M4F: 16 system + 48 IRQ = 64 entries) ------- */

__attribute__((section(".vectors"), used))
void (*const g_pfnVectors[])(void) = {
    (void (*)(void))&__stack,           /*  0: Initial SP */
    Reset_Handler,                      /*  1: Reset */
    /* ... ARM system exceptions (indices 2-15) */
    [2]  = (void (*)(void))0x00000000, /* NMI */
    [3]  = HardFault_Handler,
    [4]  = (void (*)(void))0x00000000, /* MemManage */
    [5]  = (void (*)(void))0x00000000, /* BusFault */
    [6]  = (void (*)(void))0x00000000, /* UsageFault */
    [11] = SVC_Handler,
    [12] = (void (*)(void))0x00000000, /* DebugMon */
    [14] = PendSV_Handler,
    [15] = SysTick_Handler,

    /* External interrupts (offset 16+) */
    [16 + 0]  = POWER_CLOCK_IRQHandler,    /* POWER_CLOCK */
    [16 + 1]  = RADIO_IRQHandler,          /* RADIO */
    [16 + 2]  = Default_Handler,           /* UARTE0_UART0 */
    [16 + 3]  = Default_Handler,           /* SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0 */
    [16 + 4]  = Default_Handler,           /* SPIM1_SPIS1_TWIM1_TWIS1_SPI1_TWI1 */
    [16 + 6]  = GPIOTE_IRQHandler,         /* GPIOTE */
    [16 + 7]  = Default_Handler,           /* SAADC */
    [16 + 8]  = TIMER0_IRQHandler,         /* TIMER0 */
    [16 + 9]  = Default_Handler,           /* TIMER1 */
    [16 + 10] = Default_Handler,           /* TIMER2 */
    [16 + 11] = RTC0_IRQHandler,           /* RTC0 */
    [16 + 12] = TEMP_IRQHandler,           /* TEMP */
    [16 + 13] = RNG_IRQHandler,            /* RNG */
    [16 + 14] = ECB_IRQHandler,            /* ECB */
    [16 + 15] = CCM_AAR_IRQHandler,        /* CCM_AAR */
    [16 + 16] = Default_Handler,           /* WDT */
    [16 + 17] = Default_Handler,           /* RTC1 */
    [16 + 18] = Default_Handler,           /* QDEC */
    [16 + 20] = Default_Handler,           /* RTC2 */
    [16 + 21] = SWI0_EGU0_IRQHandler,      /* SWI0 */
    [16 + 22] = SWI1_EGU1_IRQHandler,      /* SWI1 */
    [16 + 23] = SWI2_EGU2_IRQHandler,      /* SWI2 */
    [16 + 24] = SWI3_EGU3_IRQHandler,      /* SWI3 */
    [16 + 25] = SWI4_EGU4_IRQHandler,      /* SWI4 */
    [16 + 26] = SWI5_EGU5_IRQHandler,      /* SWI5 */
    [16 + 27] = Default_Handler,           /* TIMER3 */
    [16 + 28] = Default_Handler,           /* TIMER4 */
    [16 + 29] = Default_Handler,           /* PWM0 */
    [16 + 30] = Default_Handler,           /* PDM */
    [16 + 32] = Default_Handler,           /* MWU */
    [16 + 33] = Default_Handler,           /* PWM1 */
    [16 + 34] = Default_Handler,           /* PWM2 */
    [16 + 35] = Default_Handler,           /* SPIM2_SPIS2_SPI2 */
    [16 + 36] = Default_Handler,           /* RTC0 (safe) */
    [16 + 37] = Default_Handler,           /* FPU */
    [16 + 42] = Default_Handler,           /* CRYPTOCELL */
    [16 + 47] = Default_Handler,           /* SPIM3 */
};

/* ---- Reset handler ---------------------------------------------------- */

void Reset_Handler(void)
{
    /* Enable FPU (Cortex-M4F) */
    uint32_t *cpacr = (uint32_t *)0xE000ED88u;
    *cpacr |= (0xFu << 20);
    __asm volatile ("dsb; isb");

    SystemInit();
    _start();
}

#include "grtc.h"
#include "nrf.h"

/*
 * Which per-core SYSCOUNTER view to read. From the MDK (nrf54l15_interim.h),
 * GRTC_IRQ_GROUP for the application core in secure mode is 2.
 * (FLPR = 0, app non-secure = 1, app secure = 2.)
 */
#define GRTC_DOMAIN 2u

#define GRTC_52BIT_MASK 0x000FFFFFFFFFFFFFULL   /* low 32 | high 20 */

void grtc_init(void)
{
    /* Low-frequency source: LFLPRC (internal low-power RC) — always
     * available, no crystal or start-up needed. */
    NRF_GRTC_S->CLKCFG =
        (NRF_GRTC_S->CLKCFG & ~GRTC_CLKCFG_CLKSEL_Msk) |
        (GRTC_CLKCFG_CLKSEL_LFLPRC << GRTC_CLKCFG_CLKSEL_Pos);

    /* Keep our domain's SYSCOUNTER view active (do not let it go idle). */
    NRF_GRTC_S->SYSCOUNTER[GRTC_DOMAIN].ACTIVE = GRTC_SYSCOUNTER_ACTIVE_ACTIVE_Active;

    /* Enable and start the global SYSCOUNTER. */
    NRF_GRTC_S->MODE = GRTC_MODE_SYSCOUNTEREN_Msk;
    NRF_GRTC_S->TASKS_START = 1;
}

uint64_t grtc_now(void)
{
    /*
     * On Cortex-M33, a single 64-bit read of SYSCOUNTERL compiles to one LDRD,
     * so the low 32 and high 20 bits are captured atomically — no busy-poll or
     * capture handshake needed (this is what nrfx does).
     */
    volatile uint64_t *p =
        (volatile uint64_t *)&NRF_GRTC_S->SYSCOUNTER[GRTC_DOMAIN].SYSCOUNTERL;
    return (*p) & GRTC_52BIT_MASK;
}

void grtc_delay_us(uint32_t us)
{
    uint64_t start = grtc_now();
    while ((grtc_now() - start) < (uint64_t)us) {
    }
}

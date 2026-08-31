#include "clock.h"
#include "nrf.h"

/* 忙等上限：正常起振 < 1ms；超时说明硬件问题（比静默挂死可观测） */
#define HFXO_START_TIMEOUT_TICKS 2000000u   /* 空转计数 ≈ 2s @128MHz */

int clock_hfxo_start(void)
{
    NRF_CLOCK_S->EVENTS_XOSTARTED = 0;
    NRF_CLOCK_S->TASKS_XOSTART = 1;
    for (volatile uint32_t t = 0; NRF_CLOCK_S->EVENTS_XOSTARTED == 0; t++) {
        if (t > HFXO_START_TIMEOUT_TICKS) {
            return -1;
        }
    }
    return 0;
}

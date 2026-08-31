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

    /* XOTUNE：HFXO 自动调谐（负载电容微调到标称频率）。XOSTARTED 只表示
     * 起振,Zephyr 明确"HFCLK is stable after XOTUNED"——不调谐的载波偏几十
     * ppm,RF 上表现为手机(窄 AFC)收不到广播、宽容 dongle 能收但 RSSI 低
     * (2026-08-31 实板追出:安卓集体扫不到的根因之一)。 */
    return clock_hfxo_retune();
}

int clock_hfxo_retune(void)
{
    NRF_CLOCK_S->EVENTS_XOTUNED = 0;
    NRF_CLOCK_S->EVENTS_XOTUNEFAILED = 0;
    NRF_CLOCK_S->TASKS_XOTUNE = 1;
    for (volatile uint32_t t = 0;
         NRF_CLOCK_S->EVENTS_XOTUNED == 0 && NRF_CLOCK_S->EVENTS_XOTUNEFAILED == 0;
         t++) {
        if (t > HFXO_START_TIMEOUT_TICKS) {
            return -2;
        }
    }
    return NRF_CLOCK_S->EVENTS_XOTUNED ? 0 : -3;   /* -3 = 调谐失败 */
}

int clock_hfxo_tune_error(void)
{
    if (NRF_CLOCK_S->EVENTS_XOTUNEERROR) {
        NRF_CLOCK_S->EVENTS_XOTUNEERROR = 0;
        return 1;
    }
    return 0;
}

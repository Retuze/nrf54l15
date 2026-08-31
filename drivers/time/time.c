#include "time.h"
#include "nrf.h"
#include "nvic.h"   /* ISER 直写（vendor/cmsis 无 core_cm33.h） */

/*
 * Which per-core SYSCOUNTER view to read. From the MDK (nrf54l15_interim.h),
 * GRTC_IRQ_GROUP for the application core in secure mode is 2.
 * (FLPR = 0, app non-secure = 1, app secure = 2.)
 */
#define GRTC_DOMAIN 2u

#define GRTC_52BIT_MASK 0x000FFFFFFFFFFFFFULL   /* low 32 | high 20 */

void time_init(void)
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

uint64_t time_now_us(void)
{
    /*
     * LDRD 的 64 位读是两次总线事务，对 1 MHz 递增不原子（跨 2^32 µs 边界
     * 时会撕出偏差 2^32 µs 的时间戳）。硬件为此提供了专门协议：先读 L、
     * 再读 H，若 H 的 OVERFLOW 位（bit31，"读数期间 L 溢出过"）置位则重读
     * （nrfx_grtc 同款双读协议）。VALUE 只占低 20 位，OVERFLOW 在掩码外。 */
    volatile uint32_t *pl = (volatile uint32_t *)&NRF_GRTC_S->SYSCOUNTER[GRTC_DOMAIN].SYSCOUNTERL;
    volatile uint32_t *ph = (volatile uint32_t *)&NRF_GRTC_S->SYSCOUNTER[GRTC_DOMAIN].SYSCOUNTERH;
    uint32_t lo, hi;
    do {
        lo = *pl;
        hi = *ph;
    } while (hi & GRTC_SYSCOUNTER_SYSCOUNTERH_OVERFLOW_Msk);
    return ((uint64_t)(hi & GRTC_SYSCOUNTER_SYSCOUNTERH_VALUE_Msk) << 32) | lo;
}

void time_delay_us(uint32_t us)
{
    uint64_t start = time_now_us();
    while ((time_now_us() - start) < (uint64_t)us) {
    }
}

/* ===================================================== 闹钟（CC + IRQ） -- */
/*
 * GRTC 比较通道：CC[ch]（CCL 低 32 位 + CCH 高 32 位，用低 20 位；CCEN 使能）
 * 与域 2 的 SYSCOUNTER 视图比较（与 time_now_us 同源），命中置 EVENTS_COMPARE[ch]。
 * 中断线按域选：app secure = GRTC_IRQ_GROUP 2 → INTENSET2/INTENCLR2/INTPEND2，
 * NVIC 线 GRTC_2_IRQn = 228（nrf54l15_application.h）。vendor/cmsis 无
 * core_cm33.h，NVIC 使能直写 ISER。
 * 写序：INTENCLR2 关位 → 清事件 → CCEN 关 → 写 CCL 再写 CCH → CCEN 开 →
 * INTENSET2 开位（若烧板不触发，按风险清单先换 CCH/CCL 写序排查）。
 */
#define GRTC_IRQ_GRP     2u
#define GRTC_IRQ_NUM     228u
#define TIME_ALARM_CHANNELS 2u
#define CC_MASK(ch)      (GRTC_INTEN2_COMPARE0_Msk << (ch))

static time_alarm_cb_t s_alarm_cb[TIME_ALARM_CHANNELS];

void time_alarm_set(uint32_t ch, uint64_t t_us, time_alarm_cb_t cb)
{
    if (ch >= TIME_ALARM_CHANNELS) {
        return;
    }

    /* 目标时刻已过期：清掉本通道的遗留装配（旧闹钟的 CCEN/INTEN/事件），
     * 再立即同步回调（不进 IRQ） */
    if (t_us <= time_now_us()) {
        NRF_GRTC_S->INTENCLR2 = CC_MASK(ch);
        NRF_GRTC_S->EVENTS_COMPARE[ch] = 0;
        NRF_GRTC_S->CC[ch].CCEN = GRTC_CC_CCEN_ACTIVE_Disable;
        s_alarm_cb[ch] = 0;
        if (cb) {
            cb(ch);
        }
        return;
    }

    NRF_GRTC_S->INTENCLR2 = CC_MASK(ch);
    NRF_GRTC_S->EVENTS_COMPARE[ch] = 0;
    NRF_GRTC_S->CC[ch].CCEN = GRTC_CC_CCEN_ACTIVE_Disable;
    s_alarm_cb[ch] = cb;
    NRF_GRTC_S->CC[ch].CCL = (uint32_t)(t_us & 0xFFFFFFFFu);
    NRF_GRTC_S->CC[ch].CCH = (uint32_t)((t_us >> 32) & 0x000FFFFFu);
    NRF_GRTC_S->CC[ch].CCEN = GRTC_CC_CCEN_ACTIVE_Enable;
    NRF_GRTC_S->INTENSET2 = CC_MASK(ch);

    nvic_enable(GRTC_IRQ_NUM);
}

void time_alarm_cancel(uint32_t ch)
{
    if (ch >= TIME_ALARM_CHANNELS) {
        return;
    }
    NRF_GRTC_S->INTENCLR2 = CC_MASK(ch);
    NRF_GRTC_S->CC[ch].CCEN = GRTC_CC_CCEN_ACTIVE_Disable;
    NRF_GRTC_S->EVENTS_COMPARE[ch] = 0;
    s_alarm_cb[ch] = 0;
}

/* 向量表在 drivers/core/startup.c：[16 + GRTC_2_IRQn] = GRTC_2_IRQHandler。 */
void GRTC_2_IRQHandler(void)
{
    uint32_t pending = NRF_GRTC_S->INTPEND2;
    for (uint32_t ch = 0; ch < TIME_ALARM_CHANNELS; ch++) {
        uint32_t mask = CC_MASK(ch);
        if ((pending & mask) == 0 || NRF_GRTC_S->EVENTS_COMPARE[ch] == 0) {
            continue;
        }
        NRF_GRTC_S->EVENTS_COMPARE[ch] = 0;      /* 清事件（INTPEND 随之清） */
        NRF_GRTC_S->INTENCLR2 = mask;            /* 一次性闹钟 */
        NRF_GRTC_S->CC[ch].CCEN = GRTC_CC_CCEN_ACTIVE_Disable;
        time_alarm_cb_t cb = s_alarm_cb[ch];
        s_alarm_cb[ch] = 0;                      /* 先摘回调再调用（防重入 set） */
        if (cb) {
            cb(ch);
        }
    }
}

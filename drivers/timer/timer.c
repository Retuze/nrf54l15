#include "timer.h"
#include "nrf.h"
#include "nvic.h"

#define TIMER_IRQ_NUM 85u      /* TIMER00_IRQn */
#define TIMER_CC_MAX  8u

/* CC 通道静态槽位（不透明句柄）：8 个通道即 8 个实例上限 */
struct timer_hw {
    uint8_t  cc;               /* 占用的 CC 通道（0..7） */
    uint8_t  inited;
    uint8_t  oneshot;          /* 1 = 触发后解除通道（ISR 内） */
    uint32_t period_ticks;     /* periodic：ISR 里 CC += 周期（不清计数器） */
    timer_cb_t cb;
    void *cb_arg;
};

static timer_hw_t s_inst[TIMER_CC_MAX];

timer_hw_t *timer_init(timer_port_t port, const timer_cfg_t *cfg)
{
    (void)port;                              /* 应用核只有 TIMER00 */
    if (cfg == 0 || cfg->freq_hz != 1000000u) {
        return 0;                            /* 目前只有 1MHz（µs 节拍） */
    }
    timer_hw_t *t = 0;
    for (uint8_t ch = 0; ch < TIMER_CC_MAX; ch++) {
        if (!s_inst[ch].inited) {
            t = &s_inst[ch];
            break;
        }
    }
    if (t == 0) {
        return 0;                            /* 无空闲通道（上限 8 实例） */
    }
    t->cc = (uint8_t)(t - s_inst);
    t->cb = 0;
    t->cb_arg = 0;
    t->oneshot = 0;
    t->inited = 1;

    NRF_TIMER00_S->MODE     = TIMER_MODE_MODE_Timer;
    NRF_TIMER00_S->BITMODE  = TIMER_BITMODE_BITMODE_32Bit;
    /* 实板标定（2026-08-30）：PRESCALER=7 实测 500kHz（1s 周期回调实际 2s）
     * → TIMER00 输入时钟 64MHz。PRESCALER=6 → 2^6 分频 → 1MHz 节拍。 */
    NRF_TIMER00_S->PRESCALER = 6u;
    NRF_TIMER00_S->EVENTS_COMPARE[t->cc] = 0;

    nvic_set_prio(TIMER_IRQ_NUM, cfg->irq_prio);
    nvic_enable(TIMER_IRQ_NUM);
    NRF_TIMER00_S->TASKS_START = 1;
    return t;
}

void timer_deinit(timer_hw_t *t)
{
    if (t == 0 || !t->inited) {
        return;
    }
    timer_stop(t);
    t->cb = 0;
    t->cb_arg = 0;
    t->inited = 0;
}

void timer_set_callback(timer_hw_t *t, timer_cb_t cb, void *arg)
{
    if (t == 0 || !t->inited) {
        return;
    }
    t->cb = cb;
    t->cb_arg = arg;
}

/* 启动。相对计时：TASKS_CAPTURE 硬件快照当前计数 + us——不清共享计数器
 * （8 实例共存的关键：TASKS_CLEAR 会推迟别人已挂起的 COMPARE）。
 * PERIODIC 在 ISR 里 CC += 周期推进（不用 SHORTS COMPARE_CLEAR：CLEAR 会
 * 把共享计数器归零，其他通道的绝对 CC 永远追不上）；oneshot 不用
 * ONESHOTEN：它会停掉整块 TIMER（冻结共存实例的周期通道），单次语义
 * 改为 ISR 里"触发即解除本通道"。 */
int timer_start(timer_hw_t *t, uint32_t us, timer_mode_t mode)
{
    if (t == 0 || !t->inited || us == 0) {
        return -1;
    }
    NRF_TIMER00_S->TASKS_CAPTURE[t->cc] = 1; /* CC[cc] ← 当前计数（硬件快照） */
    NRF_TIMER00_S->CC[t->cc] += us;          /* 相对当前时刻；1MHz：µs 即节拍 */
    NRF_TIMER00_S->EVENTS_COMPARE[t->cc] = 0;
    NRF_TIMER00_S->INTENSET = TIMER_INTEN_COMPARE0_Msk << t->cc;
    /* periodic 不用 SHORTS COMPARE_CLEAR：CLEAR 会把共享计数器归零，其他
     * 通道的绝对 CC（oneshot/更长周期）永远追不上。改为 ISR 里 CC += 周期
     * 推进——计数器绝对递增，8 实例任意周期/单次共存。 */
    t->oneshot = (mode == TIMER_ONESHOT) ? 1 : 0;
    t->period_ticks = us;
    return 0;
}

void timer_stop(timer_hw_t *t)
{
    if (t == 0 || !t->inited) {
        return;
    }
    NRF_TIMER00_S->INTENCLR = TIMER_INTEN_COMPARE0_Msk << t->cc;
    NRF_TIMER00_S->EVENTS_COMPARE[t->cc] = 0;
    t->oneshot = 0;
    /* 回调保留：再次 start 不用重新注册（timer_deinit 才清） */
}

/* 向量表槽在 drivers/core/startup.c（[16 + TIMER00_IRQn]），弱别名占位、
 * 此处强定义接管。 */
void TIMER00_IRQHandler(void)
{
    for (uint8_t ch = 0; ch < TIMER_CC_MAX; ch++) {
        if (NRF_TIMER00_S->EVENTS_COMPARE[ch] == 0) {
            continue;
        }
        NRF_TIMER00_S->EVENTS_COMPARE[ch] = 0;
        timer_hw_t *t = &s_inst[ch];
        if (!t->inited) {
            NRF_TIMER00_S->INTENCLR = TIMER_INTEN_COMPARE0_Msk << ch;
            continue;                        /* 无主通道：静默关闭 */
        }
        timer_cb_t cb = t->cb;
        void *arg = t->cb_arg;
        if (t->oneshot) {
            NRF_TIMER00_S->INTENCLR = TIMER_INTEN_COMPARE0_Msk << ch;
            /* 回调保留：单次触发后再 start 不用重新注册 */
        } else {
            NRF_TIMER00_S->CC[ch] += t->period_ticks;  /* 推进下一周期（不清计数器） */
        }
        if (cb) {
            cb(t, arg);                      /* IRQ 上下文：必须短小 */
        }
    }
}

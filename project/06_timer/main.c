/*
 * 06_timer — 普通定时器实验（TIMER00 多实例：周期 + 单次，主循环自由）。
 *
 * 与 GRTC 的分工：GRTC/time 是 BLE 专用时基；本实验演示应用通用定时——
 *   实例 A：1s 周期回调（LED 翻转 + 计数）
 *   实例 B：3s 单次回调（打一条日志后自解除）
 *   主循环：自由打印心跳——证明定时器不占 CPU（无忙等），
 *           且单次/周期实例在同一块 TIMER00 上共存互不冻结。
 *
 * 验证点（烧板后）：
 *   - 串口每 1s 出现 "[main] heartbeat N"；每 2s LED 状态翻转
 *     （周期回调实际间隔与日志一致 = 1MHz 节拍假设成立）
 *   - 启动 3s 后出现一条 "[timer] oneshot fired"，且只出现一次
 */

#include <stdint.h>
#include <stdlib.h>
#include "xiao_nrf54l15.h"
#include "gpio.h"
#include "time.h"
#include "timer.h"
#include "console.h"
#include "log.h"

#define LED GPIO_PIN(BOARD_LED_PORT, BOARD_LED_PIN)

/* 周期实例：1s 回调，LED 翻转 */
static timer_hw_t *t_periodic;
static volatile uint32_t tick_count;
static void periodic_cb(timer_hw_t *t, void *arg)
{
    (void)t; (void)arg;
    tick_count++;
    gpio_write(LED, (tick_count & 1u) ? BOARD_LED_ACTIVE_LEVEL
                                      : !BOARD_LED_ACTIVE_LEVEL);
}

/* 单次实例：3s 后触发一次（回调里 log 是入队操作，微秒级，可接受） */
static timer_hw_t *t_oneshot;
static volatile uint32_t oneshot_hit;
static void oneshot_cb(timer_hw_t *t, void *arg)
{
    (void)t; (void)arg;
    oneshot_hit = 1;
    log_puts("[timer] oneshot fired\n");
}

int main(void)
{
    gpio_mode(LED, GPIO_OUTPUT);
    gpio_write(LED, !BOARD_LED_ACTIVE_LEVEL);

    /* 控制台与日志（打印策略：printf 仅 HardFault） */
        static const uart_cfg_t console_cfg = {
                .tx_pin = GPIO_PIN(BOARD_CONSOLE_TX_PORT, BOARD_CONSOLE_TX_PIN),
        .rx_pin = UART_PIN_NONE,
        .baud = UART_BAUD_115200,
        .fmt = UART_8N1,
    .irq_prio = 0,    };
    console_init(UARTE20, &console_cfg);
    console_log_init();
    time_init();

    log_puts("\n=== 06_timer: TIMER00 multi-instance demo ===\n");

    /* 两个实例共占同一块 TIMER00（不同 CC 通道） */
    static const timer_cfg_t cfg1m = { .freq_hz = 1000000, .irq_prio = 3 };
    t_periodic = timer_init(TIMER00, &cfg1m);
    t_oneshot = timer_init(TIMER00, &cfg1m);
    timer_set_callback(t_periodic, periodic_cb, 0);
    timer_set_callback(t_oneshot, oneshot_cb, 0);
    timer_start(t_periodic, 1000000u, TIMER_PERIODIC);
    timer_start(t_oneshot, 3000000u, TIMER_ONESHOT);
    log_puts("[timer] periodic 1s + oneshot 3s armed\n");

    uint32_t last_tick = 0;
    for (;;) {
        /* 主循环完全自由：心跳打印 + 观察回调计数 */
        if (tick_count != last_tick) {
            last_tick = tick_count;
            log_printf("[main] heartbeat %u\n", last_tick);
        }
        if (oneshot_hit == 1) {
            log_printf("[main] oneshot observed at heartbeat %u\n", last_tick);
            oneshot_hit = 2;              /* 只报一次（0=未触发 1=刚触发 2=已报） */
        }
        time_delay_us(50000u);
    }
}

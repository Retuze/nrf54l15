/*
 * 03_conn_log — 连接态实时日志实验（方案 A：异步日志）。
 *
 * 与 01_conn 完全相同的链路，加一条非阻塞日志通道（common/log）：
 *   - ll 的 on_conn_event 钩子在每个事件 reply 之后调用（~interval 松弛的
 *     安全插桩点），log_printf 只做"格式化 + 入队"（微秒级）
 *   - uart_tx 入队即返（驱动内 512B ring），TX END 中断后台排空到串口
 *   - 打印策略（全工程）：printf 只归 HardFault，日常日志统一走 log
 *
 * 观察：连接中串口逐事件打出 "evt=N ok/miss ch=X"，log_dropped 显示因
 * 打太猛而丢弃的字节数。
 */

#include <stdint.h>
#include <stdlib.h>
#include "xiao_nrf54l15.h"
#include "gpio.h"
#include "time.h"
#include "console.h"
#include "clock.h"
#include "radio.h"
#include "ll.h"
#include "gatt.h"
#include "log.h"

/* ---------------------------------------------------------------- LED -- */
#define LED GPIO_PIN(BOARD_LED_PORT, BOARD_LED_PIN)

static void led_set(int on)
{
    gpio_write(LED, on ? BOARD_LED_ACTIVE_LEVEL : !BOARD_LED_ACTIVE_LEVEL);
}

/* --------------------------------------------------------------- misc -- */
static uint32_t rng_state;
static uint32_t rng_next(void)
{
    uint32_t x = rng_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng_state = x; return x;
}

static const uint8_t OUR_ADDR[6] = { 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0 };

/* ---------------------------------------- 连接事件观察（安全插桩点） -- */
static void on_conn_event(uint32_t counter, int crc_ok, uint32_t ch)
{
    log_printf("evt=%u %s ch=%u\n", counter, crc_ok ? "ok" : "miss", ch);
}

static const ll_ops_t OPS = {
    .now_us   = time_now_us,
    .delay_us = time_delay_us,
    .radio_set_aa      = radio_set_aa,
    .radio_set_channel = radio_set_channel,
    .radio_disable     = radio_disable,
    .radio_tx          = radio_tx,
    .radio_rx          = radio_rx,
    .radio_reply_at    = radio_reply_at,
    .att_handle = gatt_handle_att,
    .on_dle     = gatt_set_tx_octets,
    .on_connect = gatt_on_connect,
    .mtu_get    = gatt_dbg_mtu,
    .txoct_get  = gatt_dbg_txoct,
    .led        = led_set,
    .on_conn_event = on_conn_event,
};

/* ---------------------------------------------------------------- main -- */
static const uart_cfg_t console_cfg = {
        .tx_pin = GPIO_PIN(BOARD_CONSOLE_TX_PORT, BOARD_CONSOLE_TX_PIN),
    .rx_pin = UART_PIN_NONE,            /* 板载 SAMD11 桥只接了 TX */
    .baud = UART_BAUD_115200,
    .fmt = UART_8N1,
    .irq_prio = 0,};

/* 闹钟自检：一次性 CC 闹钟 +5ms，回调在 IRQ 上下文只置 flag */
static volatile int alarm_hit;
static void alarm_self_cb(uint32_t ch)
{
    (void)ch;
    alarm_hit = 1;
}

int main(void)
{
    gpio_mode(LED, GPIO_OUTPUT);
    led_set(0);
    console_init(UARTE20, &console_cfg);
    console_log_init();   /* 打印策略：日志统一走 log（printf 仅 HardFault） */
    clock_hfxo_start();
    time_init();
    radio_init();
    gatt_init();
    ll_init(OUR_ADDR);

    void *smoke = malloc(64);
    log_printf("picolibc ready: grtc=%llu us, malloc=%p\n",
               (unsigned long long)time_now_us(), smoke);
    free(smoke);

    rng_state = (uint32_t)time_now_us() | 1u;
    log_puts("\n=== 54L-GATT connectable + scannable (live log) ===\n");

    /* 闹钟自检 */
    time_alarm_set(0, time_now_us() + 5000u, alarm_self_cb);
    while (!alarm_hit) { }
    log_puts("alarm self-test ok\n");

    ll_conn_t conn;
    ll_stats_t st;
    ll_adv_stats_t ast = { 0 };
    uint32_t adv_events = 0;

    for (;;) {
        uint64_t t_ci_end = 0;
        if (ll_adv_sweep(&OPS, &conn, &t_ci_end, &ast)) {
            ll_conn_run(&OPS, &conn, t_ci_end, &st);    /* blocks until link lost */
            log_printf("\n[conn] AA=0x%08x int=%uus hop=%u -> events=%u hits=%u tx=%u\n",
                    conn.aa, conn.interval_us, conn.hop, st.events, st.hits, st.tx_done);
            log_printf("[conn] mtu=%u txoct=%u maxrsp=%u tx_timeouts=%u\n",
                    st.mtu, st.tx_octets, st.maxrsp, st.tx_timeouts);
            log_printf("[conn] rx_end->tx_end gap=%uus (expect ~230)\n", st.gap_us);
            log_printf("[conn] log_dropped=%u bytes\n", log_dropped());
            uint32_t cnt = st.rxpdu_n < 6u ? st.rxpdu_n : 6u;
            uint32_t base = st.rxpdu_n - cnt;
            for (uint32_t j = 0; j < cnt; j++) {
                uint32_t slot = (base + j) % 6u;
                uint32_t llid = st.rxpdu[slot][0] & 0x3u;
                uint32_t len = st.rxpdu[slot][1];
                log_printf("  rx[-%u] LLID=%u len=%u:", cnt - j, llid, len);
                for (uint32_t k = 0; k < len + 2u && k < 32u; k++)
                    log_printf(" %02x", st.rxpdu[slot][k]);
                log_puts("\n");
            }
            continue;
        }

        if ((++adv_events & 127u) == 0)
            log_printf("[adv] events=%u rx_ok=%u rx_err=%u\n", adv_events, ast.rx_ok, ast.rx_err);

        time_delay_us(20000u + (rng_next() % 10000u));
    }
}

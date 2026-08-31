/*
 * 04_async_ll — 事件化链路层（方案 B）：连接态完全由 IRQ 驱动，主循环解放。
 *
 * 与 01_conn 的区别只有一个：CONNECT_IND 之后不再调阻塞的 ll_conn_run，
 * 而是 ll_async_start——连接事件由 GRTC 闹钟（锚点/窗口超时）与 RADIO IRQ
 * （收包/发完）推进，全部在中断上下文；主循环同时打 1s 心跳并累计空转
 * 次数，串口上"连接期间心跳照走"就是解放的直接证据。
 *
 * 接线（较 01_conn 新增）：
 *   ops.alarm_set/alarm_cancel  = drivers/time 的 GRTC CC 闹钟（LL 占通道 0/1）
 *   ops.radio_rx_arm/reply_arm  = drivers/radio 异步 API
 *   radio_irq_init(trampoline)  → ll_async_on_radio_rx / _tx
 *   断链回调（IRQ 上下文）只置 flag，统计打印回主循环做。
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

/* ------------------------------------------ 异步引擎的 IRQ 桥接 -- */
static volatile int g_connected;

static void radio_evt_trampoline(int is_rx, uint64_t t_addr, uint64_t t_end,
                                 int crc_ok)
{
    if (is_rx) {
        ll_async_on_radio_rx(t_addr, t_end, crc_ok);
    } else {
        ll_async_on_radio_tx();
    }
}

static void on_disconnect_cb(void)
{
    g_connected = 0;                /* IRQ 上下文：只置 flag，打印回主循环 */
}

/* -------------------------------------------- 驱动 → LL 的 ops 接线 -- */
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
    /* 异步能力（04 的主角） */
    .alarm_set      = time_alarm_set,
    .alarm_cancel   = time_alarm_cancel,
    .radio_rx_arm   = radio_rx_arm,
    .radio_reply_arm = radio_reply_arm,
};

/* ---------------------------------------------------------------- main -- */
static const uart_cfg_t console_cfg = {
    .tx_pin = GPIO_PIN(BOARD_CONSOLE_TX_PORT, BOARD_CONSOLE_TX_PIN),
    .rx_pin = UART_PIN_NONE,            /* 板载 SAMD11 桥只接了 TX */
    .baud = UART_BAUD_115200,
    .fmt = UART_8N1,
    .irq_prio = 0,
};

int main(void)
{
    gpio_mode(LED, GPIO_OUTPUT);
    led_set(0);
    console_init(UARTE20, &console_cfg);
    console_log_init();
    clock_hfxo_start();
    time_init();
    radio_init();
    radio_irq_init(radio_evt_trampoline);
    gatt_init();
    ll_init(OUR_ADDR);

    rng_state = (uint32_t)time_now_us() | 1u;
    log_puts("\n=== 04_async_ll: IRQ-driven connection, free main loop ===\n");

    ll_conn_t conn;
    ll_stats_t st;
    ll_adv_stats_t ast = { 0 };
    uint32_t adv_events = 0;

    for (;;) {
        /* ---- 广播（同步路径，主循环节奏） ---- */
        uint64_t t_ci_end = 0;
        if (ll_adv_sweep(&OPS, &conn, &t_ci_end, &ast)) {
            g_connected = 1;
            ll_async_start(&OPS, &conn, t_ci_end, &st, on_disconnect_cb);
            log_puts("[conn] async engine started\n");

            /* ---- 主循环解放的证据：连接全程心跳照走 ---- */
            uint32_t spins = 0;
            uint64_t next_beat = time_now_us() + 1000000u;
            uint32_t beat = 0;
            while (g_connected) {
                spins++;
                if (time_now_us() >= next_beat) {
                    next_beat += 1000000u;
                    beat++;
                    log_printf("[main] heartbeat %u: spins=%uk evt=%u hit=%u\n",
                               beat, spins / 1000u, st.events, st.hits);
                    spins = 0;
                }
            }

            log_printf("\n[conn] AA=0x%08x int=%uus hop=%u -> events=%u hits=%u tx=%u\n",
                       conn.aa, conn.interval_us, conn.hop,
                       st.events, st.hits, st.tx_done);
            log_printf("[conn] mtu=%u txoct=%u maxrsp=%u tx_timeouts=%u\n",
                       st.mtu, st.tx_octets, st.maxrsp, st.tx_timeouts);
            {
                uint32_t lmin, lmax, rmin, rmax;
                radio_dbg_tifs(&lmin, &lmax, &rmin, &rmax);
                log_printf("[conn] tifs_late=%u..%u ramp=%u..%u us\n",
                           lmin, lmax, rmin, rmax);
            }
            {
                uint32_t cnt = st.rxpdu_n < 6u ? st.rxpdu_n : 6u;
                uint32_t base = st.rxpdu_n - cnt;
                for (uint32_t j = 0; j < cnt; j++) {
                    uint32_t slot = (base + j) % 6u;
                    uint32_t llid = st.rxpdu[slot][0] & 0x3u;
                    uint32_t len = st.rxpdu[slot][1];
                    log_printf("  rx[-%u] evt=%u LLID=%u len=%u:", cnt - j,
                               st.rxevt[slot], llid, len);
                    for (uint32_t k = 0; k < len + 2u && k < 32u; k++)
                        log_printf(" %02x", st.rxpdu[slot][k]);
                    log_printf(" | tx=%02x\n", st.txhdr[slot]);
                }
            }
            continue;
        }

        if ((++adv_events & 127u) == 0)
            log_printf("[adv] events=%u rx_ok=%u rx_err=%u sreq=%u srsp=%u\n",
                       adv_events, ast.rx_ok, ast.rx_err, ast.scan_req, ast.scan_rsp);

        time_delay_us(20000u + (rng_next() % 10000u));
    }
}

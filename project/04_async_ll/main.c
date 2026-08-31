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
#include "proto.h"
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

/* F2:E0:D0:C0:B0:A0（与 05_proto 一致）：曾用旧地址被 iOS 系统级
 * 自动回连无限占线（占线期不广播=谁都扫不到）,换地址甩掉。 */
static const uint8_t OUR_ADDR[6] = { 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF2 };

/* ---------------------------------- 应用协议（与 05_proto 同款接线；
 * 区别：这里所有 proto 回调都运行在 RADIO IRQ 上下文——构建超过回复
 * 预算时 radio_reply_arm 放弃本事件、pend 队列下事件秒回） -- */
#define NOTIFY_SLOTS 4u
#define NOTIFY_FRAME_MAX 248u
static uint8_t  nq_buf[NOTIFY_SLOTS][NOTIFY_FRAME_MAX];
static uint16_t nq_len[NOTIFY_SLOTS];
static uint8_t  nq_head, nq_tail, nq_count;
static uint32_t nq_dropped;

static void nq_push(const uint8_t *frag, uint32_t len)
{
    if (len > NOTIFY_FRAME_MAX || nq_count == NOTIFY_SLOTS) {
        nq_dropped++;
        return;
    }
    for (uint32_t i = 0; i < len; i++) nq_buf[nq_tail][i] = frag[i];
    nq_len[nq_tail] = (uint16_t)len;
    nq_tail = (uint8_t)((nq_tail + 1u) % NOTIFY_SLOTS);
    nq_count++;
}

/* ATT Handle Value Notification 包装（0x1B + 柄 + proto 帧）；订阅前不发 */
static uint32_t att_notify_pull(uint8_t *out, uint32_t max)
{
    uint32_t creq = gatt_client_pull(out, max);
    if (creq) {
        return creq;                 /* 客户端 PDU（主动 MTU_REQ）优先于通知 */
    }
    if (nq_count == 0u || !gatt_notify_enabled()) {
        return 0;
    }
    uint32_t n = nq_len[nq_head];
    if (3u + n > max) {
        return 0;
    }
    out[0] = 0x1Bu;
    out[1] = (uint8_t)GATT_FFF1_VAL_HANDLE;
    out[2] = (uint8_t)(GATT_FFF1_VAL_HANDLE >> 8);
    for (uint32_t i = 0; i < n; i++) out[3u + i] = nq_buf[nq_head][i];
    nq_head = (uint8_t)((nq_head + 1u) % NOTIFY_SLOTS);
    nq_count--;
    return 3u + n;
}

static void proto_send_cb(const uint8_t *frag, uint32_t len, void *arg)
{
    (void)arg;
    nq_push(frag, len);
}
/* proto 传输帧上限 = 实时 ATT MTU − 3（HVN 头占 3）。曾写死 244：
 * 手机不请求 MTU（默认 23）时组出的大帧超 MTU,通知被对端栈静默丢弃
 * ——server 无法主动发起 MTU 交换（规范限 client）,正解是任何 MTU 下
 * 自动分片（proto 的 SEQ/MORE）。 */
static uint32_t proto_mtu_cb(void) { return gatt_dbg_mtu() - 3u; }

static uint8_t  bat_val[4];
static uint8_t  time_val[6];
static const uint8_t dev_val[] = { 0, 1, 0, 1, '5', '4', 'L' };
static uint8_t  up_val[4];
static uint8_t  stat_val[4];
static int64_t  epoch_offset_us;

static uint8_t fw_msg_id;
static uint8_t pending_report_id = 0xFFu;
static uint64_t report_deadline_us;
static uint32_t report_retries;
static uint8_t last_set_id = 0xFFu;

static void put_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static void fill_fields(void)
{
    bat_val[0] = 85u;
    put_be16(bat_val + 1, 3600u);
    bat_val[3] = 0u;
    uint64_t epoch_us = (uint64_t)time_now_us() + (uint64_t)epoch_offset_us;
    put_be32(time_val, (uint32_t)(epoch_us / 1000000u));
    put_be16(time_val + 4, 480u);
    put_be32(up_val, (uint32_t)(time_now_us() / 1000000u));
    put_be32(stat_val, 1u);
}

static void proto_send_full_report(uint8_t msg_id)
{
    fill_fields();
    proto_tlv_t tlvs[5] = {
        { PROTO_T_BATTERY,  4, bat_val },
        { PROTO_T_TIME,     6, time_val },
        { PROTO_T_DEV_INFO, (uint8_t)sizeof(dev_val), dev_val },
        { PROTO_T_UPTIME,   4, up_val },
        { PROTO_T_STATUS,   4, stat_val },
    };
    proto_report(tlvs, 5, msg_id, 1);
    pending_report_id = msg_id;
    report_deadline_us = time_now_us() + 500000u;
}

static void on_get_cb(const uint8_t *types, uint32_t n, uint8_t msg_id)
{
    (void)types; (void)n;
    report_retries = 0;
    proto_send_full_report(fw_msg_id++);
    log_printf("[proto] GET id=%u -> full report\n", msg_id);
}

static void on_set_cb(const proto_tlv_t *tlvs, uint32_t n, uint8_t msg_id)
{
    if (msg_id == last_set_id) {
        proto_ack(0u, msg_id);
        log_printf("[proto] SET id=%u dup, re-ack\n", msg_id);
        return;
    }
    last_set_id = msg_id;
    uint8_t err = 1u;
    for (uint32_t i = 0; i < n; i++) {
        if (tlvs[i].t == PROTO_T_TIME && tlvs[i].l == 6u) {
            uint32_t epoch = ((uint32_t)tlvs[i].v[0] << 24) |
                             ((uint32_t)tlvs[i].v[1] << 16) |
                             ((uint32_t)tlvs[i].v[2] << 8)  | tlvs[i].v[3];
            epoch_offset_us = (int64_t)epoch * 1000000LL -
                              (int64_t)time_now_us();
            err = 0u;
        }
    }
    proto_ack(err, msg_id);
    log_printf("[proto] SET n=%u err=%u\n", n, err);
}

static void on_ack_cb(uint8_t err, uint8_t msg_id)
{
    if (msg_id == pending_report_id) {
        pending_report_id = 0xFFu;
        report_retries = 0;
        log_printf("[proto] report %u acked err=%u\n", msg_id, err);
    }
}

static proto_ops_t proto_ops_ = {
    .send = proto_send_cb,
    .mtu_get = proto_mtu_cb,
    .arg = 0,
    .on_get = on_get_cb,
    .on_set = on_set_cb,
    .on_report = 0,
    .on_ack = on_ack_cb,
};

static uint32_t att_write_cb(uint16_t handle, const uint8_t *val, uint32_t len,
                             void *arg)
{
    (void)arg;
    if (handle == GATT_FFF1_VAL_HANDLE) {
        proto_feed(val, len);
    }
    return 0;
}

/* 连接事件钩子（IRQ）：ACK_REQ REPORT 超时重发，3 次未确认上抛 */
static void on_conn_event_cb(uint32_t counter, int crc_ok, uint32_t ch)
{
    (void)counter; (void)crc_ok; (void)ch;
    if (pending_report_id != 0xFFu && time_now_us() > report_deadline_us) {
        if (report_retries >= 3u) {
            log_printf("[proto] report %u unacked after %u retries\n",
                       pending_report_id, report_retries);
            pending_report_id = 0xFFu;
            return;
        }
        proto_send_full_report(pending_report_id);
        report_retries++;
        log_puts("[proto] report retry\n");
    }
}

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
    .on_conn_event   = on_conn_event_cb,
    .att_notify_pull = att_notify_pull,
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
    gatt_set_write_cb(att_write_cb, 0);
    proto_init(&proto_ops_);
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

        if ((++adv_events & 127u) == 0) {
            uint32_t lmin, lmax, rmin, rmax;
            radio_dbg_tifs(&lmin, &lmax, &rmin, &rmax);
            log_printf("[adv] events=%u rx_ok=%u rx_err=%u sreq=%u srsp=%u"
                       " late=%u..%u ramp=%u..%u scanner=%02x%02x%02x%02x%02x%02x\n",
                       adv_events, ast.rx_ok, ast.rx_err, ast.scan_req, ast.scan_rsp,
                       lmin, lmax, rmin, rmax,
                       ast.scan_addr[5], ast.scan_addr[4], ast.scan_addr[3],
                       ast.scan_addr[2], ast.scan_addr[1], ast.scan_addr[0]);
        }

        time_delay_us(20000u + (rng_next() % 10000u));
    }
}

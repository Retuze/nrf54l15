/*
 * 07_adv_scan — LE 双角色第一步：广播 + 被动扫描交替。
 *
 * 一个 radio 两件事：ll_sched（时间片调度器雏形）在时间轴上排布
 *   - adv 槽（prio 1）：每 ~60ms 一次 3 通道 ADV_IND sweep（保持可连接
 *     可扫描——手机侧与 05 无差别）
 *   - scan 槽（prio 2）：每 100ms 开一个 30ms 被动扫描窗（37/38/39 轮转），
 *     收环境广播进设备表
 * 窗口重叠时低优先级让步（yields 计数）——碰撞行为可观测，正是本实验
 * 要验证的调度器雏形语义。CONNECT_IND 到来则切 04 的异步连接引擎，
 * 连接期间调度暂停（主循环打心跳），断链自动恢复双角色。
 *
 * 串口输出：~2s 一行 [duo] 调度统计 + 环境设备表增量。
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
#include "sched.h"
#include "scan.h"
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

/* ------------------------------------------ 异步连接引擎桥接（同 04） -- */
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
    g_connected = 0;
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
    .on_conn_event   = on_conn_event_cb,
    .att_notify_pull = att_notify_pull,
    .alarm_set       = time_alarm_set,
    .alarm_cancel    = time_alarm_cancel,
    .radio_rx_arm    = radio_rx_arm,
    .radio_reply_arm = radio_reply_arm,
};

/* ---------------------------------------------------------------- main -- */
static const uart_cfg_t console_cfg = {
    .tx_pin = GPIO_PIN(BOARD_CONSOLE_TX_PORT, BOARD_CONSOLE_TX_PIN),
    .rx_pin = UART_PIN_NONE,
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
    int hfxo_rc = clock_hfxo_start();
    time_init();
    radio_init();
    radio_irq_init(radio_evt_trampoline);
    gatt_init();
    gatt_set_write_cb(att_write_cb, 0);
    proto_init(&proto_ops_);
    ll_init(OUR_ADDR);

    rng_state = (uint32_t)time_now_us() | 1u;
    log_printf("\n=== 07_adv_scan: dual-role adv + passive scan (hfxo rc=%d) ===\n",
               hfxo_rc);

    ll_conn_t conn;
    ll_stats_t st;
    ll_adv_stats_t ast = { 0 };
    ll_scan_stats_t sst;
    for (uint32_t i = 0; i < sizeof(sst); i++) ((uint8_t *)&sst)[i] = 0;

    /* 调度参数两种玩法（BUILD 时选）：
     * 碰撞观测（默认关）：adv 60ms / scan 100ms——非谐波周期,相位滑移,
     *   碰撞率 ≈ (30+4)/60 ≈ 57%,让步计数持续增长,验证让步机制。
     * 谐波打包（默认开）：scan 周期取 adv 的整数倍(120ms)且相位错开——
     *   adv 占 [0,4ms)、scan 占 [10,40ms),构造性零碰撞,占空比无损。
     *   同设备内相位归我们管,不需要概率避碰;互质/抖动留给管不着相位的
     *   对端(advDelay 随机抖动即为此)。 */
    ll_sched_init();
    uint64_t t0 = time_now_us();
#if defined(DUO_COLLIDE_DEMO)
    int slot_adv  = ll_sched_add(1, t0 + 1000u, 4000u, 60000u);
    int slot_scan = ll_sched_add(2, t0 + 5000u, 30000u, 100000u);
#else
    int slot_adv  = ll_sched_add(1, t0 + 1000u, 4000u, 60000u);
    int slot_scan = ll_sched_add(2, t0 + 11000u, 30000u, 120000u);
#endif

    uint32_t scan_ch = 0;
    uint32_t last_ndev = 0;
    uint64_t next_report = time_now_us() + 2000000u;

    for (;;) {
        /* ---- 连接期：调度暂停，主循环只打心跳（引擎全在 IRQ） ---- */
        if (g_connected) {
            static uint64_t next_beat;
            uint64_t now = time_now_us();
            if (next_beat == 0) next_beat = now + 1000000u;
            if (now >= next_beat) {
                next_beat += 1000000u;
                log_printf("[main] connected: evt=%u hit=%u\n", st.events, st.hits);
            }
            if (!g_connected) next_beat = 0;
            continue;
        }

        int s = ll_sched_pick(time_now_us());
        if (s < 0) {
            continue;
        }
        const ll_sched_slot_t *sl = ll_sched_slot(s);
        while (time_now_us() < sl->t_start) {
        }

        if (s == slot_adv) {
            uint64_t t_ci_end = 0;
            if (ll_adv_sweep(&OPS, &conn, &t_ci_end, &ast)) {
                g_connected = 1;
                ll_async_start(&OPS, &conn, t_ci_end, &st, on_disconnect_cb);
                log_puts("[conn] async engine started (dual-role paused)\n");
                ll_sched_done(s, time_now_us());
                continue;
            }
        } else if (s == slot_scan) {
            ll_scan_window(&OPS, scan_ch, 30000u, &sst);
            scan_ch = (scan_ch + 1u) % 3u;
        }
        ll_sched_done(s, time_now_us());

        /* ---- ~2s 统计报告 + 新设备增量 ---- */
        uint64_t now = time_now_us();
        if (now >= next_report) {
            next_report = now + 2000000u;
            const ll_sched_slot_t *a = ll_sched_slot(slot_adv);
            const ll_sched_slot_t *c = ll_sched_slot(slot_scan);
            log_printf("[duo] adv %u/%uy sreq=%u | scan %u/%uy win=%u ok=%u err=%u devs=%u\n",
                       a->runs, a->yields, ast.scan_req,
                       c->runs, c->yields, sst.windows, sst.rx_ok, sst.rx_err,
                       sst.n_dev);
            for (uint32_t i = last_ndev; i < sst.n_dev; i++) {
                ll_scan_dev_t *d = &sst.dev[i];
                log_printf("  dev %02x:%02x:%02x:%02x:%02x:%02x %c t%u n=%u '%s'\n",
                           d->addr[5], d->addr[4], d->addr[3],
                           d->addr[2], d->addr[1], d->addr[0],
                           d->txadd ? 'R' : 'P', d->type, d->count,
                           d->name_len ? d->name : "");
            }
            last_ndev = sst.n_dev;

            /* HFXO 周期重调谐（温漂对抗,见 README RF 三大必修课） */
            if (clock_hfxo_retune() != 0) {
                log_puts("[hfxo] retune failed\n");
            }
        }

        time_delay_us(200u + (rng_next() % 800u));   /* 广播相位抖动 */
    }
}

/*
 * 05_proto — 应用协议固件接线（common/proto over BLE GATT 0xFFF1）。
 *
 * 数据通路（规格 docs/proto.md）：
 *   上行 app→fw：WRITE_CMD/WRITE_REQ → gatt 写回调 → proto_feed
 *               （一个 write = 一个 proto 传输帧，SET 分片由 proto 重组）
 *   下行 fw→app：proto 传输帧 → 通知帧队列（4 槽）→ ll att_notify_pull
 *               （每个连接事件顶替空回复发一条，DLE 完成后才发大帧）
 *
 * 首次同步：app GET_REQ{} → fw REPORT{5 字段} ACK_REQ（状态类必须送达）
 *   → app ACK{0, id} → fw 清重发状态；超时 500ms 同 id 重发（on_conn_event
 *   里查——连接期主循环被 ll_conn_run 占用，事件钩子是唯一周期入口）。
 *
 * SET{TIME}：epoch 落 epoch_offset_us（日志可打绝对时间）；回 ACK 带 err。
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

/* F2:E0:D0:C0:B0:A0——2026-08-31 换地址甩掉按旧地址自动回连的占线设备
 * （占线期间不广播,表现为"扫不到"）。 */
static const uint8_t OUR_ADDR[6] = { 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF2 };

/* -------------------------------------- 通知帧队列（proto send 落点） -- */
#define NOTIFY_SLOTS 4u
#define NOTIFY_FRAME_MAX 248u   /* ATT 载荷上限 244（mtu 247-3），留余量 */
static uint8_t  nq_buf[NOTIFY_SLOTS][NOTIFY_FRAME_MAX];
static uint16_t nq_len[NOTIFY_SLOTS];
static uint8_t  nq_head, nq_tail, nq_count;
static uint32_t nq_dropped;

static void nq_push(const uint8_t *frag, uint32_t len)
{
    if (len > NOTIFY_FRAME_MAX || nq_count == NOTIFY_SLOTS) {
        nq_dropped++;                        /* 满/超长：丢新（mtu 244 下不该发生） */
        return;
    }
    for (uint32_t i = 0; i < len; i++) {
        nq_buf[nq_tail][i] = frag[i];
    }
    nq_len[nq_tail] = (uint16_t)len;
    nq_tail = (uint8_t)((nq_tail + 1u) % NOTIFY_SLOTS);
    nq_count++;
}

/* ll 的 att_notify_pull：每个连接事件取一条，包装成 ATT Handle Value
 * Notification（0x1B + 柄 LE + proto 传输帧）。订阅（CCCD）之前不发。
 * 放不下时不截断（截断的帧是坏帧）：留队等下一个事件。 */
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
    out[0] = 0x1Bu;                                  /* HANDLE_VALUE_NTF */
    out[1] = (uint8_t)GATT_FFF1_VAL_HANDLE;
    out[2] = (uint8_t)(GATT_FFF1_VAL_HANDLE >> 8);
    for (uint32_t i = 0; i < n; i++) {
        out[3u + i] = nq_buf[nq_head][i];
    }
    nq_head = (uint8_t)((nq_head + 1u) % NOTIFY_SLOTS);
    nq_count--;
    return 3u + n;
}

/* ------------------------------------------------ proto 装配 -- */
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

/* REPORT 字段静态存储（重发需要稳定引用） */
static uint8_t  bat_val[4];                 /* level, voltage_mv u16, flags */
static uint8_t  time_val[6];                /* epoch_s u32, tz_min i16 */
static const uint8_t dev_val[] = { 0, 1, 0, 1, '5', '4', 'L' };  /* 0.1.0, hw1, "54L" */
static uint8_t  up_val[4];
static uint8_t  stat_val[4];
static int64_t  epoch_offset_us;            /* 墙钟 = time_now_us() + offset */

static uint8_t fw_msg_id;
static uint8_t pending_report_id = 0xFFu;   /* 0xFF = 无待确认 REPORT */
static uint64_t report_deadline_us;
static uint32_t report_retries;

/* SET 幂等去重：同 msg_id 重发只回 ACK 不重复执行（协议层承诺，见
 * docs/proto.md）；0xFF = 无历史 */
static uint8_t last_set_id = 0xFFu;

static void put_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static void fill_fields(void)
{
    bat_val[0] = 85u;                        /* 演示值；VBAT ADC 校准留后 */
    put_be16(bat_val + 1, 3600u);
    bat_val[3] = 0u;
    uint64_t epoch_us = (uint64_t)time_now_us() + (uint64_t)epoch_offset_us;
    put_be32(time_val, (uint32_t)(epoch_us / 1000000u));
    put_be16(time_val + 4, 480u);            /* UTC+8（SET 可改） */
    put_be32(up_val, (uint32_t)(time_now_us() / 1000000u));
    put_be32(stat_val, 1u);                  /* bit0 = BLE connected */
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
    proto_report(tlvs, 5, msg_id, 1);        /* ACK_REQ：状态类必须送达 */
    pending_report_id = msg_id;
    report_deadline_us = time_now_us() + 500000u;
}

/* fw 侧协议回调 */
static void on_get_cb(const uint8_t *types, uint32_t n, uint8_t msg_id)
{
    (void)types; (void)n;                    /* 简化：一律全量上报 */
    report_retries = 0;
    proto_send_full_report(fw_msg_id++);
    log_printf("[proto] GET id=%u -> full report\n", msg_id);
}

static void on_set_cb(const proto_tlv_t *tlvs, uint32_t n, uint8_t msg_id)
{
    /* 幂等去重：同 id 重发（app 超时重传）只重发 ACK，不重复执行 */
    if (msg_id == last_set_id) {
        proto_ack(0u, msg_id);
        log_printf("[proto] SET id=%u dup, re-ack\n", msg_id);
        return;
    }
    last_set_id = msg_id;

    uint8_t err = 1u;                        /* 1 = 无已知字段 */
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
        pending_report_id = 0xFFu;           /* 状态送达，清重发 */
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

/* gatt 写回调：0xFFF1 一个 write = 一个 proto 传输帧 */
static uint32_t att_write_cb(uint16_t handle, const uint8_t *val, uint32_t len,
                             void *arg)
{
    (void)arg;
    if (handle == GATT_FFF1_VAL_HANDLE) {
        proto_feed(val, len);
    }
    return 0;
}

/* 连接事件钩子：ACK_REQ REPORT 超时重发（连接期唯一周期入口），
 * 3 次未确认上抛（多半已断链） */
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
        proto_send_full_report(pending_report_id);   /* 同 id 同载荷 */
        report_retries++;
        log_puts("[proto] report retry\n");
    }
}

/* 连接建立提示（连接期日志走异步 log，不阻塞时序） */
static void on_connect_log(void)
{
    gatt_on_connect();
    log_puts("[conn] connected\n");
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
    .on_connect = on_connect_log,
    .mtu_get    = gatt_dbg_mtu,
    .txoct_get  = gatt_dbg_txoct,
    .led        = led_set,
    .on_conn_event  = on_conn_event_cb,
    .att_notify_pull = att_notify_pull,
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
    int hfxo_rc = clock_hfxo_start();
    log_printf("hfxo start+tune rc=%d\n", hfxo_rc);   /* 0=已调谐 */
    time_init();
    radio_init();
    gatt_init();
    gatt_set_write_cb(att_write_cb, 0);
    proto_init(&proto_ops_);
    ll_init(OUR_ADDR);

    void *smoke = malloc(64);
    log_printf("picolibc ready: grtc=%llu us, malloc=%p\n",
               (unsigned long long)time_now_us(), smoke);
    free(smoke);

    {   /* GRTC 读取耗时标定（TIFS busy-wait 的粒度 = 单次读耗时） */
        uint64_t a = time_now_us();
        for (volatile int i = 0; i < 1000; i++) { (void)time_now_us(); }
        log_printf("grtc read x1000: %u us\n", (uint32_t)(time_now_us() - a));
    }


    rng_state = (uint32_t)time_now_us() | 1u;
    log_puts("\n=== 54L-GATT + app proto (GET/REPORT/SET/ACK) ===\n");

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
            log_printf("[conn] notify_dropped=%u\n", nq_dropped);
            {
                uint32_t lmin, lmax, rmin, rmax;
                radio_dbg_tifs(&lmin, &lmax, &rmin, &rmax);
                log_printf("[conn] tifs_late=%u..%u ramp=%u..%u us\n",
                           lmin, lmax, rmin, rmax);
            }
            {   /* 逐事件踪迹：最近 64 事件的 counter/rx头/tx头/信道 */
                uint32_t last = st.events ? (st.events - 1u) : 0u;
                uint32_t n = st.events < 64u ? st.events : 64u;
                for (uint32_t j = 0; j < n; j += 8u) {
                    log_printf("  tr");
                    for (uint32_t k = j; k < j + 8u && k < n; k++) {
                        uint32_t c = (last - (n - 1u) + k) % 64u;
                        log_printf(" %02x:%02x/%02x@%02u", st.evtrace[c][0],
                                   st.evtrace[c][1], st.evtrace[c][2], st.evtrace[c][3]);
                    }
                    log_puts("\n");
                    console_flush();                /* 逐行排空,防日志环溢出 */
                }
            }
            {   /* 断链后验尸：最后 6 个有内容 PDU（LL 控制/非空数据） */
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
            log_printf("[adv] events=%u rx_ok=%u rx_err=%u sreq=%u srsp=%u"
                       " scanner=%02x%02x%02x%02x%02x%02x\n",
                       adv_events, ast.rx_ok, ast.rx_err, ast.scan_req, ast.scan_rsp,
                       ast.scan_addr[5], ast.scan_addr[4], ast.scan_addr[3],
                       ast.scan_addr[2], ast.scan_addr[1], ast.scan_addr[0]);

        /* HFXO 周期重调谐：温度漂移会把载波拖出手机接收容差（长 PDU 先
         * 失联）。每 ~14s 一次,XOTUNEERROR 报告时立即。广播间隙做,不占
         * 连接时序。 */
        if ((adv_events & 511u) == 0 || clock_hfxo_tune_error()) {
            int rc = clock_hfxo_retune();
            if (rc != 0) log_printf("[hfxo] retune rc=%d\n", rc);
        }


        time_delay_us(20000u + (rng_next() % 10000u));
    }
}

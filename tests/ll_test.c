/*
 * ll_test.c — LL 链路层注入测试（宿主侧）。
 *
 * fake time：fake_now 变量，fake_delay_us 推进它（LL 所有等待的唯一入口）。
 * fake radio：脚本化报文队列——每个连接事件的"对端 PDU"预先写进脚本，
 * fake_rx 把 fake_now 推到脚本时刻并回包；t_off 是相对监听窗口起点的偏移。
 * fake_reply_at/fake_tx 记录发出的包（tx_log），供断言。
 *
 * 覆盖：CONNECT_IND 解析、SN/NESN ack 规则、全部 LL control 应答、
 * channel-map/conn-update 在 Instant 生效、DLE 自发起、ATT 透传、
 * supervision 超时、SCAN_RSP、led 回调。
 */
#include <string.h>
#include "tf.h"
#include "ll.h"

static const uint8_t our_addr[6] = { 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0 };
static const uint8_t full_chm[5] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x1F };  /* 37 信道全用 */

/* ------------------------------------------------ fake time -- */
static uint64_t fake_now;
static uint64_t fake_now_us(void) { return fake_now; }
static void fake_delay_us(uint32_t us) { fake_now += us; }

/* ----------------------------------------------- fake radio -- */
typedef struct {
    uint32_t t_off;        /* 相对窗口起点的到达时刻 */
    const uint8_t *pkt;    /* NULL = 本事件无包（窗口超时） */
    uint32_t len;
    int crc_ok;
} rx_event_t;

static rx_event_t ev[64];
static uint32_t nev, script_idx;

static uint8_t tx_log[64][260];
static uint32_t tx_len[64];
static uint32_t n_tx;

static uint32_t chan_rec[256];
static uint32_t n_chan;
static uint32_t last_aa, last_crc;
static uint32_t n_disable;

static void record_tx(const uint8_t *pkt, uint32_t len)
{
    if (n_tx < 64u) {
        for (uint32_t i = 0; i < len; i++) tx_log[n_tx][i] = pkt[i];
        tx_len[n_tx] = len;
    }
    n_tx++;
}

static int fake_tx(const uint8_t *pkt, uint32_t len, uint32_t timeout_us)
{
    (void)timeout_us;
    record_tx(pkt, len);
    fake_now += (uint64_t)(len + 11u) * 8u;   /* 前导+AA ≈ 11 字节空中时间 */
    return 1;
}

static int fake_rx(uint8_t *pkt, uint32_t maxlen, uint32_t window_us,
                   uint64_t *t_addr, uint64_t *t_end, int *crc_ok)
{
    (void)maxlen;
    if (script_idx >= nev) {
        TF_FAIL("script exhausted (rx)");
        return 0;
    }
    const rx_event_t *e = &ev[script_idx++];
    if (e->pkt == NULL) {                     /* 无包：推进整个窗口后超时 */
        fake_now += window_us;
        return 0;
    }
    if (e->t_off > window_us) {
        TF_FAIL("script t_off=%u outside window %u", e->t_off, window_us);
    }
    fake_now += e->t_off;
    for (uint32_t i = 0; i < e->len; i++) pkt[i] = e->pkt[i];
    if (t_addr) *t_addr = fake_now;
    if (t_end) *t_end = fake_now + (uint64_t)e->len * 8u;
    if (crc_ok) *crc_ok = e->crc_ok;
    return 1;
}

static int fake_reply_at(const uint8_t *pkt, uint32_t len, uint64_t rx_end_us)
{
    record_tx(pkt, len);
    fake_now = rx_end_us + 230u;              /* T_IFS 150 + ~80us 发送 */
    return 1;
}

static void fake_set_aa(uint32_t aa, uint32_t crcinit) { last_aa = aa; last_crc = crcinit; }
static void fake_set_channel(uint32_t freq_off, uint32_t white_ch)
{
    (void)freq_off;
    if (n_chan < 256u) chan_rec[n_chan++] = white_ch;
}
static void fake_disable(void) { n_disable++; }

/* ------------------------------------------ fake 上层（gatt 侧） -- */
static uint32_t n_att, n_dle, n_connect, dle_val;
static uint8_t led_states[8];
static uint32_t n_led;

static uint32_t fake_att(const uint8_t *req, uint32_t len, uint8_t *out)
{
    (void)req; (void)len;
    n_att++;
    out[0] = 0x0B; out[1] = 0x11; out[2] = 0x12; out[3] = 0x13; out[4] = 0x14;
    return 5;
}
static void fake_on_dle(uint32_t v) { dle_val = v; n_dle++; }
static void fake_on_connect(void) { n_connect++; }
static uint32_t fake_mtu(void) { return 77u; }
static uint32_t fake_txoct(void) { return 99u; }
static void fake_led(int on) { if (n_led < 8u) led_states[n_led++] = (uint8_t)on; }

static ll_ops_t ops = {
    .now_us = fake_now_us, .delay_us = fake_delay_us,
    .radio_set_aa = fake_set_aa, .radio_set_channel = fake_set_channel,
    .radio_disable = fake_disable, .radio_tx = fake_tx, .radio_rx = fake_rx,
    .radio_reply_at = fake_reply_at,
    .att_handle = fake_att, .on_dle = fake_on_dle, .on_connect = fake_on_connect,
    .mtu_get = fake_mtu, .txoct_get = fake_txoct,
    .led = fake_led,
};

static void fake_reset(void)
{
    fake_now = 1000;
    nev = 0; script_idx = 0;
    n_tx = 0; n_chan = 0; n_disable = 0;
    last_aa = last_crc = 0;
    n_att = n_dle = n_connect = 0; dle_val = 0;
    n_led = 0;
}

/* ------------------------------------------------ 剧本构建 -- */
static uint8_t pkts[64][260];

static void ev_miss(uint32_t t_off)
{
    ev[nev].t_off = t_off; ev[nev].pkt = NULL; ev[nev].len = 0; ev[nev].crc_ok = 0;
    nev++;
}
static void ev_pkt(uint32_t t_off, const uint8_t *p, uint32_t len, int crc_ok)
{
    ev[nev].t_off = t_off; ev[nev].pkt = p; ev[nev].len = len; ev[nev].crc_ok = crc_ok;
    nev++;
}

/* 空数据 PDU：LLID=1 */
static uint8_t *mk_empty(int slot, int sn, int nesn)
{
    pkts[slot][0] = (uint8_t)(0x01u | (nesn << 2) | (sn << 3));
    pkts[slot][1] = 0;
    return pkts[slot];
}

/* LL control PDU：LLID=3，payload 在 op 之后 */
static uint8_t *mk_ctrl(int slot, int sn, int nesn, uint8_t op,
                        const uint8_t *pl, uint32_t plen)
{
    pkts[slot][0] = (uint8_t)(0x03u | (nesn << 2) | (sn << 3));
    pkts[slot][1] = (uint8_t)(1u + plen);
    pkts[slot][2] = op;
    for (uint32_t i = 0; i < plen; i++) pkts[slot][3 + i] = pl[i];
    return pkts[slot];
}

/* L2CAP/ATT 帧：LLID=2 */
static uint8_t *mk_att(int slot, int sn, int nesn, const uint8_t *areq, uint32_t alen)
{
    pkts[slot][0] = (uint8_t)(0x02u | (nesn << 2) | (sn << 3));
    pkts[slot][1] = (uint8_t)(4u + alen);
    pkts[slot][2] = (uint8_t)alen; pkts[slot][3] = 0;
    pkts[slot][4] = 0x04; pkts[slot][5] = 0x00;      /* CID = ATT */
    for (uint32_t i = 0; i < alen; i++) pkts[slot][6 + i] = areq[i];
    return pkts[slot];
}

/* CONNECT_IND：2 头 + InitA 6 + AdvA 6 + LLData 22 */
static uint8_t *mk_connind(int slot, uint32_t aa, uint32_t crcinit,
                           uint8_t winsize, uint16_t winoff, uint16_t interval,
                           uint16_t timeout, const uint8_t chm[5], uint8_t hop)
{
    uint8_t *p = pkts[slot];
    p[0] = 0x45; p[1] = 34;
    for (int k = 0; k < 6; k++) p[2 + k] = 0xAA;      /* InitA（任意） */
    for (int k = 0; k < 6; k++) p[8 + k] = our_addr[k];
    p[14] = (uint8_t)aa; p[15] = (uint8_t)(aa >> 8);
    p[16] = (uint8_t)(aa >> 16); p[17] = (uint8_t)(aa >> 24);
    p[18] = (uint8_t)crcinit; p[19] = (uint8_t)(crcinit >> 8); p[20] = (uint8_t)(crcinit >> 16);
    p[21] = winsize;
    p[22] = (uint8_t)winoff; p[23] = (uint8_t)(winoff >> 8);
    p[24] = (uint8_t)interval; p[25] = (uint8_t)(interval >> 8);
    p[26] = 0; p[27] = 0;                              /* Latency 0 */
    p[28] = (uint8_t)timeout; p[29] = (uint8_t)(timeout >> 8);
    for (int k = 0; k < 5; k++) p[30 + k] = chm[k];
    p[35] = hop & 0x1Fu;
    return p;
}

static ll_conn_t mk_conn(void)
{
    ll_conn_t c = {0};
    c.aa = 0x11223344u; c.crcinit = 0x123456u;
    c.interval_us = 30000u; c.winsize_us = 6250u; c.winoffset_us = 0u;
    c.timeout_us = 1000000u;
    c.hop = 5u;
    for (int i = 0; i < 5; i++) c.chmap[i] = full_chm[i];
    for (uint32_t i = 0; i < 37; i++) c.used[i] = (uint8_t)i;
    c.num_used = 37;
    return c;
}

/* 空数据事件若干 + TERMINATE 收尾（SN/NESN 全程 0，LL 首事件会翻转 NESN） */
static void script_tail(int base_slot, uint32_t n_empty)
{
    for (uint32_t i = 0; i < n_empty; i++) {
        ev_pkt(100, mk_empty(base_slot + (int)i, 0, 0), 2, 1);
    }
    static const uint8_t term_pl[] = { 0x00 };
    ev_pkt(100, mk_ctrl(base_slot + (int)n_empty, 0, 0, 0x02u, term_pl, 1), 3, 1);
}

/* ------------------------------------------------ 用例 -- */
static void test_adv_connect(void)
{
    ll_conn_t conn; ll_adv_stats_t ast = { 0 };
    uint64_t t_ci_end = 0;
    fake_reset();
    ev_miss(200); ev_miss(200);
    ev_pkt(100, mk_connind(0, 0x11223344u, 0x123456u, 5, 0, 24, 100, full_chm, 5), 36, 1);

    CHECK_EQ(ll_adv_sweep(&ops, &conn, &t_ci_end, &ast), 1);

    /* CONNECT_IND 逐字段 */
    CHECK_EQ(conn.aa, 0x11223344u);
    CHECK_EQ(conn.crcinit, 0x123456u);
    CHECK_EQ(conn.winsize_us, 6250u);        /* 5 * 1250 */
    CHECK_EQ(conn.winoffset_us, 0u);
    CHECK_EQ(conn.interval_us, 30000u);      /* 24 * 1250 */
    CHECK_EQ(conn.timeout_us, 1000000u);     /* 100 * 10000 */
    CHECK_EQ(conn.hop, 5u);
    CHECK_EQ(conn.num_used, 37u);
    CHECK_EQ(conn.used[36], 36u);
    CHECK(t_ci_end > 0);
    CHECK_EQ(ast.rx_ok, 1u);
    CHECK_EQ(ast.rx_err, 0u);
    CHECK_EQ(last_aa, 0x8E89BED6u);
    CHECK_EQ(last_crc, 0x555555u);

    /* 3 个信道各一次 ADV_IND TX */
    CHECK_EQ(n_tx, 3u);
    CHECK_EQ(tx_log[0][0] & 0xFu, 0x0u);     /* ADV_IND */
    CHECK_EQ(tx_log[0][1], 19u);             /* 2+6+3+10 - 2 = 19 */
    CHECK(tx_log[0][2] == 0xA0 && tx_log[0][7] == 0xF0);   /* AdvA = ours */
    CHECK_EQ(chan_rec[0], 37u);
    CHECK_EQ(chan_rec[1], 38u);
    CHECK_EQ(chan_rec[2], 39u);
}

static void test_scan_rsp(void)
{
    ll_conn_t conn; ll_adv_stats_t ast = { 0 };
    uint64_t t_ci_end = 0;
    fake_reset();
    ev_miss(200);
    /* SCAN_REQ：type 3，AdvA = ours */
    pkts[1][0] = 0x43; pkts[1][1] = 12;
    for (int k = 0; k < 6; k++) pkts[1][8 + k] = our_addr[k];
    ev_pkt(150, pkts[1], 14, 1);
    ev_miss(200);

    CHECK_EQ(ll_adv_sweep(&ops, &conn, &t_ci_end, &ast), 0);
    CHECK_EQ(ast.rx_ok, 1u);
    CHECK_EQ(n_tx, 4u);                      /* ch0 ADV, ch1 ADV+SCAN_RSP, ch2 ADV */
    CHECK_EQ(tx_log[2][0] & 0xFu, 0x4u);     /* SCAN_RSP（ch2 的 ADV 在其后） */
    CHECK_EQ(tx_log[2][2], 0xA0u);           /* AdvA */
}

static void test_sn_nesn(void)
{
    ll_conn_t conn = mk_conn();
    ll_stats_t st;
    fake_reset();
    ev_pkt(100, mk_empty(0, 0, 0), 2, 1);    /* e0：新数据 → NESN 翻转 */
    ev_pkt(100, mk_empty(1, 0, 0), 2, 1);    /* e1：重传（SN 未变）→ 不动 */
    ev_pkt(100, mk_empty(2, 1, 1), 2, 1);    /* e2：ack 我们 + 新数据 → SN/NESN 都翻转 */
    static const uint8_t term_pl[] = { 0x00 };
    ev_pkt(100, mk_ctrl(3, 1, 1, 0x02u, term_pl, 1), 3, 1);

    ll_conn_run(&ops, &conn, 3000, &st);

    CHECK_EQ(tx_log[0][0], 0x05u);           /* LLID1 | NESN1<<2 | SN0<<3 */
    CHECK_EQ(tx_log[1][0], 0x05u);           /* 重传：NESN 不再翻 */
    CHECK_EQ(tx_log[2][0], 0x09u);           /* LLID1 | NESN0 | SN1 */
    CHECK_EQ(tx_log[3][0], 0x09u);           /* terminate ack */
    CHECK_EQ(st.events, 4u);
    CHECK_EQ(st.hits, 4u);
    CHECK_EQ(st.tx_done, 4u);
    CHECK_EQ(st.mtu, 77u);                   /* 注入 getter */
    CHECK_EQ(st.tx_octets, 99u);
    CHECK_EQ(last_aa, 0x11223344u);          /* ll_conn_run 先切 AA */
}

static void test_ll_ctrl(void)
{
    ll_conn_t conn = mk_conn();
    ll_stats_t st;
    fake_reset();
    static const uint8_t feat_pl[8] = { 0 };
    static const uint8_t len_pl[8] = { 251, 0, 0x48, 0x08, 251, 0, 0x48, 0x08 };
    static const uint8_t unk_pl[1] = { 0 };
    ev_pkt(100, mk_ctrl(0, 0, 0, 0x08u, feat_pl, 8), 11, 1);   /* FEATURE_REQ */
    ev_pkt(100, mk_ctrl(1, 0, 0, 0x0Cu, feat_pl, 4), 7, 1);    /* VERSION_IND */
    ev_pkt(100, mk_ctrl(2, 0, 0, 0x40u, unk_pl, 1), 4, 1);     /* unknown */
    ev_pkt(100, mk_ctrl(3, 0, 0, 0x14u, len_pl, 8), 11, 1);    /* LENGTH_REQ */
    ev_pkt(100, mk_ctrl(4, 0, 0, 0x15u, len_pl, 8), 11, 1);    /* LENGTH_RSP */
    static const uint8_t term_pl[] = { 0x00 };
    ev_pkt(100, mk_ctrl(5, 0, 0, 0x02u, term_pl, 1), 3, 1);    /* TERMINATE */

    ll_conn_run(&ops, &conn, 3000, &st);

    /* FEATURE_REQ → FEATURE_RSP + 8 字节零 feature */
    CHECK_EQ(tx_log[0][0] & 3u, 3u);
    CHECK_EQ(tx_log[0][1], 9u);
    CHECK_EQ(tx_log[0][2], 0x09u);
    CHECK_EQ(tx_log[0][3], 0u);
    CHECK_EQ(tx_log[0][10], 0u);
    /* VERSION_IND → 回 VERSION_IND */
    CHECK_EQ(tx_log[1][2], 0x0Cu);
    CHECK_EQ(tx_log[1][1], 6u);
    CHECK_EQ(tx_log[1][3], 0x0Cu);
    CHECK_EQ(tx_log[1][4], 0xFFu);
    CHECK_EQ(tx_log[1][5], 0xFFu);
    CHECK_EQ(tx_log[1][6], 0x01u);
    CHECK_EQ(tx_log[1][7], 0x00u);
    /* unknown → UNKNOWN_RSP(op) */
    CHECK_EQ(tx_log[2][2], 0x07u);
    CHECK_EQ(tx_log[2][3], 0x40u);
    CHECK_EQ(tx_log[2][1], 2u);
    /* LENGTH_REQ → RSP + on_dle(251) */
    CHECK_EQ(tx_log[3][2], 0x15u);
    CHECK_EQ(tx_log[3][1], 9u);
    CHECK_EQ(tx_log[3][3], 251u);
    CHECK_EQ(tx_log[3][5], 0x48u);
    /* LENGTH_RSP → 只 ack（LLID1） */
    CHECK_EQ(tx_log[4][0] & 3u, 1u);
    /* on_dle：LENGTH_REQ 与 LENGTH_RSP 各调一次（run 完成后统计） */
    CHECK_EQ(n_dle, 2u);
    CHECK_EQ(dle_val, 251u);
    /* 全程 6 事件 6 hit */
    CHECK_EQ(st.events, 6u);
    CHECK_EQ(st.hits, 6u);
}

static void test_chm_instant(void)
{
    ll_conn_t conn = mk_conn();
    ll_stats_t st;
    fake_reset();
    static const uint8_t new_chm[5] = { 0x01, 0, 0, 0, 0 };    /* 只剩信道 0 */
    static const uint8_t chm_pl[7] = { 1, 0, 0, 0, 0, 3, 0 };  /* ChM(5) Instant=3 */
    ev_pkt(100, mk_empty(0, 0, 0), 2, 1);
    ev_pkt(100, mk_ctrl(1, 0, 0, 0x01u, chm_pl, 7), 10, 1);    /* CHANNEL_MAP_IND */
    ev_pkt(100, mk_empty(2, 0, 0), 2, 1);
    ev_pkt(100, mk_empty(3, 0, 0), 2, 1);
    ev_pkt(100, mk_empty(4, 0, 0), 2, 1);
    ev_pkt(100, mk_empty(5, 0, 0), 2, 1);
    static const uint8_t term_pl[] = { 0x00 };
    ev_pkt(100, mk_ctrl(6, 0, 0, 0x02u, term_pl, 1), 3, 1);

    ll_conn_run(&ops, &conn, 3000, &st);

    /* hop=5 全图：5,10,15；instant=3 起只剩信道 0 → 每次重映射到 0 */
    CHECK_EQ(chan_rec[0], 5u);
    CHECK_EQ(chan_rec[1], 10u);
    CHECK_EQ(chan_rec[2], 15u);
    CHECK_EQ(chan_rec[3], 0u);
    CHECK_EQ(chan_rec[4], 0u);
    CHECK_EQ(chan_rec[5], 0u);
    CHECK_EQ(chan_rec[6], 0u);
    /* conn 结构已更新 */
    CHECK_EQ(conn.chmap[0], new_chm[0]);
    CHECK_EQ(conn.num_used, 1u);
    CHECK_EQ(conn.used[0], 0u);
    (void)st;
}

static void test_conn_update(void)
{
    ll_conn_t conn = mk_conn();
    ll_stats_t st;
    fake_reset();
    /* WinSize=8 WinOffset=0 Interval=40 Timeout=200 Instant=2 */
    static const uint8_t upd_pl[11] = { 8, 0, 0, 40, 0, 0, 0, 200, 0, 2, 0 };
    ev_pkt(100, mk_empty(0, 0, 0), 2, 1);
    ev_pkt(100, mk_ctrl(1, 0, 0, 0x00u, upd_pl, 11), 14, 1);   /* CONNECTION_UPDATE_IND */
    ev_pkt(100, mk_empty(2, 0, 0), 2, 1);
    ev_pkt(100, mk_empty(3, 0, 0), 2, 1);
    ev_pkt(100, mk_empty(4, 0, 0), 2, 1);
    static const uint8_t term_pl[] = { 0x00 };
    ev_pkt(100, mk_ctrl(5, 0, 0, 0x02u, term_pl, 1), 3, 1);

    ll_conn_run(&ops, &conn, 3000, &st);

    CHECK_EQ(conn.interval_us, 50000u);      /* 40 * 1250 */
    CHECK_EQ(conn.winsize_us, 10000u);       /* 8 * 1250 */
    CHECK_EQ(conn.timeout_us, 2000000u);     /* 200 * 10000 */
    CHECK_EQ(st.events, 6u);
    CHECK_EQ(st.hits, 6u);
    /* 信道序列不受 update 影响 */
    CHECK_EQ(chan_rec[0], 5u);
    CHECK_EQ(chan_rec[2], 15u);
}

static void test_dle_init(void)
{
    ll_conn_t conn = mk_conn();
    ll_stats_t st;
    fake_reset();
    script_tail(0, 10);                      /* 10 空事件 + TERMINATE */

    ll_conn_run(&ops, &conn, 3000, &st);

    /* 前 9 个事件普通空 ack；第 10 个（evt 9 > 8）自发 LENGTH_REQ */
    CHECK_EQ(tx_log[0][0] & 3u, 1u);
    CHECK_EQ(tx_log[8][0] & 3u, 1u);
    CHECK_EQ(tx_log[9][0] & 3u, 3u);
    CHECK_EQ(tx_log[9][2], 0x14u);
    CHECK_EQ(tx_log[9][1], 9u);
    CHECK_EQ(tx_log[9][3], 251u);
    CHECK_EQ(st.events, 11u);
}

static void test_att_passthrough(void)
{
    ll_conn_t conn = mk_conn();
    ll_stats_t st;
    fake_reset();
    static const uint8_t att_req[] = { 0x0A, 0x03, 0x00 };     /* READ name handle */
    ev_pkt(100, mk_att(0, 0, 0, att_req, 3), 9, 1);
    static const uint8_t term_pl[] = { 0x00 };
    ev_pkt(100, mk_ctrl(1, 0, 0, 0x02u, term_pl, 1), 3, 1);

    ll_conn_run(&ops, &conn, 3000, &st);

    CHECK_EQ(n_att, 1u);
    CHECK_EQ(tx_log[0][0] & 3u, 2u);         /* L2CAP start */
    CHECK_EQ(tx_log[0][1], 9u);              /* 4 + 5 */
    CHECK_EQ(tx_log[0][2], 5u);              /* L2CAP len = att rsp len */
    CHECK_EQ(tx_log[0][4], 0x04u);           /* CID = ATT */
    CHECK_EQ(tx_log[0][6], 0x0Bu);           /* att rsp opcode */
    CHECK_EQ(tx_log[0][7], 0x11u);
    CHECK_EQ(st.maxrsp, 9u);
}

static void test_supervision(void)
{
    ll_conn_t conn = mk_conn();
    ll_stats_t st;
    fake_reset();
    conn.timeout_us = 6u * 30000u;           /* max_misses 恰为 6 */
    for (uint32_t i = 0; i < 6; i++) {
        ev_miss(100);
    }

    ll_conn_run(&ops, &conn, 3000, &st);

    CHECK_EQ(st.events, 6u);
    CHECK_EQ(st.hits, 0u);
    CHECK_EQ(st.tx_done, 0u);
    CHECK(n_disable > 0);
    /* 从未 hit 不亮灯，但断链出口无条件灭灯一次 */
    CHECK_EQ(n_led, 1u);
    CHECK_EQ(led_states[0], 0u);
}

/* ------------------------------------------------ att_notify_pull -- */
static uint8_t notif[260];
static uint32_t notif_len, n_notif_pull;
static uint32_t fake_notify_pull(uint8_t *out, uint32_t max)
{
    n_notif_pull++;
    if (notif_len == 0) return 0;
    uint32_t n = notif_len < max ? notif_len : max;
    for (uint32_t i = 0; i < n; i++) out[i] = notif[i];
    notif_len = 0;                          /* 单槽：取走即清 */
    return n;
}

static void test_notify_pull(void)
{
    /* DLE 完成后，空 reply 事件应顶替发送通知；队列取空后回到空包 */
    ll_conn_t conn = mk_conn();
    ll_stats_t st;
    fake_reset();
    static const uint8_t len_pl[8] = { 251, 0, 0x48, 0x08, 251, 0, 0x48, 0x08 };
    ev_pkt(100, mk_ctrl(0, 0, 0, 0x15u, len_pl, 8), 11, 1);   /* LENGTH_RSP → dle 完成 */
    ev_pkt(100, mk_empty(1, 0, 0), 2, 1);                     /* 空 → 发通知 */
    ev_pkt(100, mk_empty(2, 0, 0), 2, 1);                     /* 空 → 队列空，回空包 */
    static const uint8_t term_pl[] = { 0x00 };
    ev_pkt(100, mk_ctrl(3, 0, 0, 0x02u, term_pl, 1), 3, 1);

    notif[0] = 0x42; notif[1] = 0x43; notif[2] = 0x44;
    notif_len = 3;
    n_notif_pull = 0;
    ops.att_notify_pull = fake_notify_pull;
    ll_conn_run(&ops, &conn, 3000, &st);
    ops.att_notify_pull = 0;

    /* evt0：LENGTH_RSP 完成 DLE，其纯空 ack 被通知顶替（合法） */
    CHECK_EQ(tx_log[0][0] & 3u, 2u);
    CHECK_EQ(tx_log[0][1], 7u);              /* 4 + 3 */
    CHECK_EQ(tx_log[0][2], 3u);              /* L2CAP len */
    CHECK_EQ(tx_log[0][4], 0x04u);           /* CID = ATT */
    CHECK_EQ(tx_log[0][6], 0x42u);
    CHECK_EQ(tx_log[0][7], 0x43u);
    CHECK_EQ(tx_log[0][8], 0x44u);
    /* evt1：队列已空 → 回空包 */
    CHECK_EQ(tx_log[1][0] & 3u, 1u);
    CHECK_EQ(tx_log[1][1], 0u);
    /* 空 reply 的事件都会拉一次队列：evt0 取到 + evt1/2/3 空拉 */
    CHECK_EQ(n_notif_pull, 4u);
}

static void test_notify_gated_pre_dle(void)
{
    /* DLE 未完成：通知不发（留队），reply 保持空包（早期事件不触发
     * DLE 自发起——它要 evt>8） */
    ll_conn_t conn = mk_conn();
    ll_stats_t st;
    fake_reset();
    ev_pkt(100, mk_empty(0, 0, 0), 2, 1);
    ev_pkt(100, mk_empty(1, 0, 0), 2, 1);
    static const uint8_t term_pl[] = { 0x00 };
    ev_pkt(100, mk_ctrl(2, 0, 0, 0x02u, term_pl, 1), 3, 1);

    notif[0] = 0x11;
    notif_len = 1;
    ops.att_notify_pull = fake_notify_pull;
    ll_conn_run(&ops, &conn, 3000, &st);
    ops.att_notify_pull = 0;

    CHECK_EQ(tx_log[0][0] & 3u, 1u);         /* 空包，非通知 */
    CHECK_EQ(tx_log[0][1], 0u);
    CHECK_EQ(notif_len, 1u);                 /* 通知仍在队 */
}

/* ------------------------------------------------ on_conn_event 钩子 -- */
static uint32_t hook_n, hook_counter[16], hook_ch[16];
static int hook_ok[16];
static void fake_on_conn_event(uint32_t counter, int crc_ok, uint32_t ch)
{
    if (hook_n < 16u) {
        hook_counter[hook_n] = counter;
        hook_ok[hook_n] = crc_ok;
        hook_ch[hook_n] = ch;
    }
    hook_n++;
}

static void test_on_conn_event_hook(void)
{
    ll_conn_t conn = mk_conn();
    ll_stats_t st;
    fake_reset();
    hook_n = 0;
    static const uint8_t term_pl[] = { 0x00 };
    ev_pkt(100, mk_empty(0, 0, 0), 2, 1);
    ev_pkt(100, mk_empty(1, 0, 0), 2, 1);
    ev_pkt(100, mk_empty(2, 0, 0), 2, 1);
    ev_pkt(100, mk_ctrl(3, 0, 0, 0x02u, term_pl, 1), 3, 1);

    ops.on_conn_event = fake_on_conn_event;
    ll_conn_run(&ops, &conn, 3000, &st);
    ops.on_conn_event = 0;

    CHECK_EQ(hook_n, 4u);                       /* 每事件一次，含 TERMINATE */
    CHECK_EQ(hook_counter[0], 0u);
    CHECK_EQ(hook_ok[0], 1);
    CHECK_EQ(hook_ch[0], 5u);                   /* hop=5 全图：5,10,15,20 */
    CHECK_EQ(hook_counter[1], 1u);
    CHECK_EQ(hook_ch[1], 10u);
    CHECK_EQ(hook_counter[3], 3u);
    CHECK_EQ(hook_ch[3], 20u);
}

static void test_led_calls(void)
{
    ll_conn_t conn = mk_conn();
    ll_stats_t st;
    fake_reset();
    static const uint8_t term_pl[] = { 0x00 };
    ev_pkt(100, mk_empty(0, 0, 0), 2, 1);
    ev_pkt(100, mk_ctrl(1, 0, 0, 0x02u, term_pl, 1), 3, 1);

    ll_conn_run(&ops, &conn, 3000, &st);

    CHECK_EQ(n_connect, 1u);
    CHECK_EQ(n_led, 2u);
    CHECK_EQ(led_states[0], 1u);             /* 首个 hit 亮 */
    CHECK_EQ(led_states[1], 0u);             /* 断链灭 */
}

/* ------------------------------------------------ 真实主端 SN/NESN 序列 -- */
/* 主端按 ack 规则演化（预推演的 6 事件剧本）：对端 NESN 确认我们 → SN 翻；
 * 对端 SN 是新数据 → NESN 翻。预期从端回复序列 05/09/05/09/05/09。 */
static void test_real_master_sequence(void)
{
    ll_conn_t conn = mk_conn();
    ll_stats_t st;
    fake_reset();
    static const uint8_t term_pl[] = { 0x00 };
    ev_pkt(100, mk_empty(0, 0, 0), 2, 1);       /* 主端 sn0/nesn0 */
    ev_pkt(100, mk_empty(1, 1, 1), 2, 1);       /* 主端 sn1/nesn1 */
    ev_pkt(100, mk_empty(2, 0, 0), 2, 1);
    ev_pkt(100, mk_empty(3, 1, 1), 2, 1);
    ev_pkt(100, mk_empty(4, 0, 0), 2, 1);
    ev_pkt(100, mk_ctrl(5, 1, 1, 0x02u, term_pl, 1), 3, 1);

    ll_conn_run(&ops, &conn, 3000, &st);

    /* 从端回复头：05(sn0,nesn1) / 09(sn1,nesn0) 交替 */
    for (uint32_t i = 0; i < 6; i++) {
        CHECK_EQ(tx_log[i][0], (i & 1u) ? 0x09u : 0x05u);
    }
    CHECK_EQ(st.events, 6u);
    CHECK_EQ(st.hits, 6u);
}

/* ------------------------------------------------ miss→hit 恢复 -- */
/* 连续 miss 后命中：窗口加宽/重同步路径的行为验证（fake_rx 的 t_off ≤
 * window_us 检查本身就是窗口计算的断言——算错会直接 FAIL） */
static void test_miss_recovery(void)
{
    ll_conn_t conn = mk_conn();
    ll_stats_t st;
    fake_reset();
    ev_miss(100);
    ev_miss(100);
    ev_pkt(100, mk_empty(0, 0, 0), 2, 1);       /* 恢复命中 */
    static const uint8_t term_pl[] = { 0x00 };
    ev_pkt(100, mk_ctrl(1, 0, 0, 0x02u, term_pl, 1), 3, 1);

    ll_conn_run(&ops, &conn, 3000, &st);

    CHECK_EQ(st.events, 4u);
    CHECK_EQ(st.hits, 2u);                       /* 2 miss + 2 hit */
    CHECK_EQ(st.tx_done, 2u);
}

/* ------------------------------------------------ winoffset≠0 的 update -- */
static void test_conn_update_winoffset(void)
{
    ll_conn_t conn = mk_conn();
    ll_stats_t st;
    fake_reset();
    /* WinSize=8 WinOffset=4(→5000us) Interval=40 Timeout=200 Instant=2 */
    static const uint8_t upd_pl[11] = { 8, 4, 0, 40, 0, 0, 0, 200, 0, 2, 0 };
    ev_pkt(100, mk_empty(0, 0, 0), 2, 1);
    ev_pkt(100, mk_ctrl(1, 0, 0, 0x00u, upd_pl, 11), 14, 1);
    ev_pkt(100, mk_empty(2, 0, 0), 2, 1);
    ev_pkt(100, mk_empty(3, 0, 0), 2, 1);
    static const uint8_t term_pl[] = { 0x00 };
    ev_pkt(100, mk_ctrl(4, 0, 0, 0x02u, term_pl, 1), 3, 1);

    ll_conn_run(&ops, &conn, 3000, &st);

    /* winoffset 只进 anchor 偏移（不写回 conn 结构——与 interval/winsize
     * 的持久化语义不同）；update 后连接继续命中即证明偏移应用正确 */
    CHECK_EQ(conn.interval_us, 50000u);
    CHECK_EQ(st.events, 5u);
    CHECK_EQ(st.hits, 5u);
}

/* ------------------------------------------------ CRC 坏包不回复 -- */
static void test_crc_bad_no_reply(void)
{
    ll_conn_t conn = mk_conn();
    ll_stats_t st;
    fake_reset();
    uint32_t tx_before;
    ev_pkt(100, mk_empty(0, 0, 0), 2, 0);       /* CRC 坏 */
    ev_pkt(100, mk_empty(1, 0, 0), 2, 1);       /* 正常命中 */
    static const uint8_t term_pl[] = { 0x00 };
    ev_pkt(100, mk_ctrl(2, 0, 0, 0x02u, term_pl, 1), 3, 1);

    tx_before = n_tx;
    ll_conn_run(&ops, &conn, 3000, &st);
    /* CRC 坏包的事件没有任何回复：3 事件只有 2 条 TX */
    CHECK_EQ(n_tx - tx_before, 2u);
    CHECK_EQ(st.hits, 2u);                       /* 坏包计 miss */
    CHECK_EQ(st.events, 3u);
}

/* ------------------------------------------------ 短 LL control → UNKNOWN_RSP -- */
static void test_short_ctrl_unknown_rsp(void)
{
    ll_conn_t conn = mk_conn();
    ll_stats_t st;
    fake_reset();
    static const uint8_t short_pl[2] = { 0xAA, 0xBB };
    ev_pkt(100, mk_ctrl(0, 0, 0, 0x08u, short_pl, 2), 5, 1);  /* FEATURE_REQ 缺 6 字节 */
    static const uint8_t term_pl[] = { 0x00 };
    ev_pkt(100, mk_ctrl(1, 0, 0, 0x02u, term_pl, 1), 3, 1);

    ll_conn_run(&ops, &conn, 3000, &st);

    CHECK_EQ(tx_log[0][0] & 3u, 3u);            /* LL control */
    CHECK_EQ(tx_log[0][1], 2u);                 /* UNKNOWN_RSP: op 回显 */
    CHECK_EQ(tx_log[0][2], 0x07u);
    CHECK_EQ(tx_log[0][3], 0x08u);
}

/* ------------------------------------------------ 0 信道 ChM 回滚 -- */
static void test_chm_zero_rollback(void)
{
    ll_conn_t conn = mk_conn();
    ll_stats_t st;
    fake_reset();
    static const uint8_t zero_chm[7] = { 0, 0, 0, 0, 0, 2, 0 };  /* 全零 ChM, instant=2 */
    ev_pkt(100, mk_empty(0, 0, 0), 2, 1);
    ev_pkt(100, mk_ctrl(1, 0, 0, 0x01u, zero_chm, 7), 10, 1);
    ev_pkt(100, mk_empty(2, 0, 0), 2, 1);       /* instant 应用点 */
    ev_pkt(100, mk_empty(3, 0, 0), 2, 1);
    static const uint8_t term_pl[] = { 0x00 };
    ev_pkt(100, mk_ctrl(4, 0, 0, 0x02u, term_pl, 1), 3, 1);

    ll_conn_run(&ops, &conn, 3000, &st);

    /* 非法 ChM 被回滚：信道序列维持原图 hop5 → 5,10,15,20,25 */
    CHECK_EQ(conn.num_used, 37u);
    CHECK_EQ(chan_rec[0], 5u);
    CHECK_EQ(chan_rec[1], 10u);
    CHECK_EQ(chan_rec[2], 15u);
    CHECK_EQ(chan_rec[3], 20u);
    CHECK_EQ(chan_rec[4], 25u);
}

/* ------------------------------------------------ 非法 CONNECT_IND 拒绝 -- */
static void test_bad_connind_rejected(void)
{
    ll_conn_t conn;
    ll_adv_stats_t ast = { 0 };
    uint64_t t_ci_end = 0;
    fake_reset();
    ev_miss(200); ev_miss(200);
    /* interval=0（非法，< 7.5ms 规范下限） */
    ev_pkt(100, mk_connind(0, 0x11223344u, 0x123456u, 5, 0, 0, 100, full_chm, 5), 36, 1);

    CHECK_EQ(ll_adv_sweep(&ops, &conn, &t_ci_end, &ast), 0);
    CHECK_EQ(ast.rx_ok, 1u);                    /* 收到了但拒绝 */
}

int main(void)
{
    ll_init(our_addr);
    test_adv_connect();
    test_scan_rsp();
    test_sn_nesn();
    test_ll_ctrl();
    test_chm_instant();
    test_conn_update();
    test_dle_init();
    test_att_passthrough();
    test_supervision();
    test_led_calls();
    test_on_conn_event_hook();
    test_notify_pull();
    test_notify_gated_pre_dle();
    test_real_master_sequence();
    test_miss_recovery();
    test_conn_update_winoffset();
    test_crc_bad_no_reply();
    test_short_ctrl_unknown_rsp();
    test_chm_zero_rollback();
    test_bad_connind_rejected();
    TF_END();
}

/*
 * LL 链路层（peripheral）：广播/连接状态机 + LL control 协议。
 * 纯协议实现，行为与 01_conn 原 main.c 逐行等价（迁移时逐段核对过），
 * 寄存器操作全部换成 ops 注入。见 ll.h 的约束说明。
 */
#include "ll.h"

/* ------------------------------------------------------- 常量 -- */
/* PDU 类型 */
#define PDU_TYPE_ADV_IND      0x0u
#define PDU_TYPE_SCAN_REQ     0x3u
#define PDU_TYPE_SCAN_RSP     0x4u
#define PDU_TYPE_CONNECT_IND  0x5u
#define HDR_TXADD_RANDOM      (1u << 6)

/* SCAN 应答监听窗口 */
#define RX_WINDOW_US 300u

/* LL control opcodes */
#define LL_CONNECTION_UPDATE_IND 0x00u
#define LL_CHANNEL_MAP_IND       0x01u
#define LL_TERMINATE_IND 0x02u
#define LL_UNKNOWN_RSP   0x07u
#define LL_FEATURE_REQ   0x08u
#define LL_FEATURE_RSP   0x09u
#define LL_VERSION_IND   0x0Cu
#define LL_LENGTH_REQ    0x14u
#define LL_LENGTH_RSP    0x15u

/* advertising-channel index -> RF frequency offset */
static const uint8_t adv_freq[3] = { 2u, 26u, 80u };
static const uint8_t adv_idx[3]  = { 37u, 38u, 39u };

/* Combined clock accuracy (ppm) used for Window Widening; generous to
 * cover the internal RC keep-alive source. ww = since_sync * ppm / 1e6,
 * rewritten as a 32-bit divide (no 64-bit __aeabi_uldivmod). */
#define WW_PPM 2000u
#define WW_DIV (1000000u / WW_PPM)   /* = 500 */

/* ---------------------------------------------------- 静态状态 -- */
static uint8_t our_addr[6];
static uint8_t adv_pdu[40];
static uint8_t scan_rsp_pdu[40];
static uint8_t rx_buf[260];        /* holds up to a 251-octet DLE data PDU */
static uint8_t tx_buf[260];
static uint8_t conn_sn, conn_nesn;
static uint8_t csa1_last;
static int g_terminate;

/* Pending parameter/channel-map updates, applied at their Instant (the
 * connEventCounter value at which master and slave switch together). */
static int g_upd_pending;
static uint16_t g_upd_instant;
static uint32_t g_upd_interval_us, g_upd_winsize_us, g_upd_winoffset_us, g_upd_timeout_us;
static int g_chm_pending;
static uint16_t g_chm_instant;
static uint8_t  g_chm_new[5];

/* Peripheral-initiated Data Length Extension: some centrals never start it, so
 * we do, to raise the data-PDU size from 27 to 251 octets. */
static int      g_dle_state;      /* 0 = not negotiated, 2 = done */
static uint32_t g_dle_sent_evt;
static uint32_t g_conn_evt;       /* current event index (mirror of counter) */

/* ------------------------------------------------ 小工具 -- */
static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd24(const uint8_t *p) { return rd16(p) | ((uint32_t)p[2] << 16); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | ((uint32_t)rd16(p + 2) << 16); }

/* data channel (0..36) -> RF frequency offset from 2400 MHz */
static uint32_t data_freq(uint32_t ch)
{
    return (ch <= 10u) ? (2u * ch + 4u) : (2u * ch + 6u);
}

/* 等待到绝对时刻 t。只走 ops->delay_us（宿主 fake 由此推进时间）。 */
static void wait_until(const ll_ops_t *ops, uint64_t t)
{
    for (;;) {
        uint64_t now = ops->now_us();
        if (t <= now) {
            return;
        }
        uint64_t d = t - now;
        if (d > 0xFFFFFFFEull) {
            d = 0xFFFFFFFEull;     /* delay_us 参数是 uint32 */
        }
        ops->delay_us((uint32_t)d);
    }
}

/* ------------------------------------------------- 广播 PDU -- */
void ll_init(const uint8_t *adv_addr)
{
    static const uint8_t name[] = "54L-GATT";
    for (uint32_t k = 0; k < 6; k++) {
        our_addr[k] = adv_addr[k];
    }

    /* ADV_IND: AdvA + Flags + Complete Local Name */
    uint32_t i = 2;
    for (uint32_t k = 0; k < 6; k++) adv_pdu[i++] = our_addr[k];
    adv_pdu[i++] = 2; adv_pdu[i++] = 0x01; adv_pdu[i++] = 0x06;
    adv_pdu[i++] = 1 + (sizeof(name) - 1); adv_pdu[i++] = 0x09;
    for (uint32_t k = 0; k < sizeof(name) - 1; k++) adv_pdu[i++] = name[k];
    adv_pdu[0] = PDU_TYPE_ADV_IND | HDR_TXADD_RANDOM;
    adv_pdu[1] = (uint8_t)(i - 2);

    /* SCAN_RSP: AdvA + Complete Local Name, so active scanners (Android is
     * picky) that SCAN_REQ our scannable ADV_IND get a proper response. */
    i = 2;
    for (uint32_t k = 0; k < 6; k++) scan_rsp_pdu[i++] = our_addr[k];
    scan_rsp_pdu[i++] = 1 + (sizeof(name) - 1);
    scan_rsp_pdu[i++] = 0x09;
    for (uint32_t k = 0; k < sizeof(name) - 1; k++) scan_rsp_pdu[i++] = name[k];
    scan_rsp_pdu[0] = PDU_TYPE_SCAN_RSP | HDR_TXADD_RANDOM;
    scan_rsp_pdu[1] = (uint8_t)(i - 2);
}

/* ------------------------------------------------- 连接解析 -- */
static void conn_parse(ll_conn_t *conn, const uint8_t *lld)
{
    conn->aa          = rd32(lld);
    conn->crcinit     = rd24(lld + 4);
    conn->winsize_us  = (uint32_t)lld[7] * 1250u;
    conn->winoffset_us= rd16(lld + 8) * 1250u;
    conn->interval_us = rd16(lld + 10) * 1250u;
    conn->timeout_us  = rd16(lld + 14) * 10000u;
    conn->hop         = lld[21] & 0x1F;
    for (uint32_t i = 0; i < 5; i++) conn->chmap[i] = lld[16 + i];
    conn->num_used = 0;
    for (uint32_t c = 0; c < 37; c++) {
        if (conn->chmap[c >> 3] & (1u << (c & 7))) conn->used[conn->num_used++] = (uint8_t)c;
    }
}

/* Channel Selection Algorithm #1 */
static uint8_t csa1_next(const ll_conn_t *conn)
{
    csa1_last = (uint8_t)((csa1_last + conn->hop) % 37u);
    if (conn->chmap[csa1_last >> 3] & (1u << (csa1_last & 7))) return csa1_last;
    return conn->used[csa1_last % conn->num_used];       /* remap unused channel */
}

/* Octets we may transmit given the peer's MaxRxOctets and MaxRxTime.
 * Time budget in octets = (MaxRxTime - 80us overhead) / 8us per octet. */
static uint32_t dle_effective(uint32_t max_rx_octets, uint32_t max_rx_time)
{
    uint32_t by_time = (max_rx_time > 80u) ? ((max_rx_time - 80u) / 8u) : 27u;
    return (max_rx_octets < by_time) ? max_rx_octets : by_time;
}

/* 待发响应队列：新数据到达时可能正处于“上一条数据 PDU 未确认”的窗口，
 * 响应先入队、等确认后发。ATT 同时只有一个未决请求 + LL 控制过程偶发
 * 并行，2 槽即覆盖；满时丢弃在正常流量下不可达。 */
#define PEND_SLOTS 2u
static uint8_t pend_llid_q[PEND_SLOTS];
static uint8_t pend_body_q[PEND_SLOTS][256];
static uint8_t pend_blen_q[PEND_SLOTS];
static uint8_t pend_head, pend_count;
static uint8_t tx_unacked;          /* 上一条非空数据 PDU 尚未被对端确认 */

static void pend_push(uint8_t llid, const uint8_t *b, uint32_t blen)
{
    if (pend_count == PEND_SLOTS) {
        return;
    }
    uint8_t slot = (uint8_t)((pend_head + pend_count) % PEND_SLOTS);
    pend_llid_q[slot] = llid;
    pend_blen_q[slot] = (uint8_t)blen;
    for (uint32_t i = 0; i < blen; i++) pend_body_q[slot][i] = b[i];
    pend_count++;
}

/*
 * Process one NEW data PDU (only called when its SN matched conn_nesn --
 * a retransmitted PDU is acknowledged by the header bits but must not be
 * re-processed, or a lost reply would re-execute ATT writes / LL procedures).
 * Responses go through the pend queue; the TX side drains it once our
 * previous data PDU is acknowledged.
 */
static void conn_process_rx(const ll_ops_t *ops)
{
    uint8_t llid = 0x03u;           /* ctrl 分支的响应 LLID；ATT 分支改成 2 */
    static uint8_t body[256];
    static uint8_t att[250];        /* ATT 载荷暂存 */
    uint32_t blen = 0;
    uint8_t rxllid = rx_buf[0] & 3u;
    uint8_t rxlen  = rx_buf[1];

    if (rxllid == 3u && rxlen >= 1u) {
        /* --- LL control PDU --- */
        uint8_t op = rx_buf[2];
        /* 各控制 PDU 的最小长度（opcode 1 字节 + 参数字节）：短 PDU 若直接
         * 按字段读会拿到上次残留的脏数据，一律回 UNKNOWN_RSP。
         * 用 goto 跳出 case（do-while 的 break 只跳宏体，会继续执行 case）。 */
#define CTRL_NEED(n) \
        if (rxlen < (n)) { body[0] = LL_UNKNOWN_RSP; body[1] = op; blen = 2; goto ctrl_done; }
        switch (op) {
        case LL_FEATURE_REQ:
            CTRL_NEED(9u);
            body[0] = LL_FEATURE_RSP;
            for (int i = 0; i < 8; i++) body[1 + i] = 0u;
            blen = 9;
            break;
        case LL_VERSION_IND:
            CTRL_NEED(5u);
            body[0] = LL_VERSION_IND;
            body[1] = 0x0Cu;                 /* VersNr: BT 5.3 */
            body[2] = 0xFFu; body[3] = 0xFFu;/* CompId (test)  */
            body[4] = 0x01u; body[5] = 0x00u;/* SubVersNr      */
            blen = 6;
            break;
        case LL_LENGTH_REQ:
            CTRL_NEED(9u); {
            /* Effective TX size is bounded by BOTH the peer's MaxRxOctets and
             * its MaxRxTime (octets that fit in that time). */
            if (ops->on_dle) ops->on_dle(dle_effective(rd16(&rx_buf[3]), rd16(&rx_buf[5])));
            g_dle_state = 2;
            body[0] = LL_LENGTH_RSP;
            body[1] = 251u;  body[2] = 0u;   /* MaxRxOctets = 251 */
            body[3] = 0x48u; body[4] = 0x08u;/* MaxRxTime = 2120 us */
            body[5] = 251u;  body[6] = 0u;   /* MaxTxOctets = 251 */
            body[7] = 0x48u; body[8] = 0x08u;/* MaxTxTime = 2120 us */
            blen = 9;
            break;
        }
        case LL_LENGTH_RSP:
            CTRL_NEED(9u); {
            /* Reply to our own LL_LENGTH_REQ: adopt the negotiated size. */
            if (ops->on_dle) ops->on_dle(dle_effective(rd16(&rx_buf[3]), rd16(&rx_buf[5])));
            g_dle_state = 2;
            break;                           /* 无响应载荷，只确认 */
        }
        case LL_CONNECTION_UPDATE_IND:
            CTRL_NEED(12u);
            /* WinSize(1) WinOffset(2) Interval(2) Latency(2) Timeout(2) Instant(2) */
            g_upd_winsize_us   = (uint32_t)rx_buf[3] * 1250u;
            g_upd_winoffset_us = rd16(&rx_buf[4]) * 1250u;
            g_upd_interval_us  = rd16(&rx_buf[6]) * 1250u;
            g_upd_timeout_us   = rd16(&rx_buf[10]) * 10000u;
            g_upd_instant      = (uint16_t)rd16(&rx_buf[12]);
            g_upd_pending      = 1;
            break;                           /* IND：只确认，无响应 */
        case LL_CHANNEL_MAP_IND:
            CTRL_NEED(8u);
            /* ChM(5) Instant(2) */
            for (int i = 0; i < 5; i++) g_chm_new[i] = rx_buf[3 + i];
            g_chm_instant = (uint16_t)rd16(&rx_buf[8]);
            g_chm_pending = 1;
            break;
        case LL_TERMINATE_IND:
            CTRL_NEED(2u);
            g_terminate = 1;
            break;
        default:
            CTRL_NEED(2u);
            body[0] = LL_UNKNOWN_RSP;
            body[1] = op;
            blen = 2;
            break;
        }
#undef CTRL_NEED
        ctrl_done: ;
    } else if ((rxllid == 2u || rxllid == 1u) && rxlen >= 4u) {
        /* --- L2CAP frame: header = length(2) + CID(2) --- */
        uint16_t cid = (uint16_t)(rx_buf[4] | (rx_buf[5] << 8));
        if (cid == 0x0004u && ops->att_handle) {    /* ATT channel */
            uint32_t alen = ops->att_handle(&rx_buf[6], (uint32_t)rxlen - 4u, att);
            if (alen > 0u) {
                llid = 0x02u;                    /* L2CAP start */
                body[0] = (uint8_t)alen; body[1] = (uint8_t)(alen >> 8);
                body[2] = 0x04u;         body[3] = 0x00u;   /* CID = ATT */
                for (uint32_t k = 0; k < alen; k++) body[4 + k] = att[k];
                blen = 4u + alen;
            }
        }
    }

    if (blen > 0u) {
        pend_push(llid, body, blen);
    }
}

/*
 * Build our reply, updating SN/NESN from the received header. New data is
 * processed exactly once (SN gate); our own data PDUs are retransmitted
 * unchanged until the peer's NESN acknowledges them.
 */
static void conn_reply(const ll_ops_t *ops, ll_stats_t *st, int crc_ok)
{
    uint8_t rx_hdr = rx_buf[0];
    int newdat = 0;
    if (crc_ok) {
        uint8_t sn_r  = (rx_hdr >> 3) & 1u;
        uint8_t nesn_r= (rx_hdr >> 2) & 1u;
        if (sn_r == conn_nesn) { conn_nesn ^= 1u; newdat = 1; } /* new data -> ack */
        if (nesn_r != conn_sn) { conn_sn ^= 1u; tx_unacked = 0; } /* acked -> advance */
    }

    if (newdat) {
        conn_process_rx(ops);
    }

    if (tx_unacked) {
        /* 上一条数据 PDU 未被确认：载荷原样重发。SN 未推进（仍等于
         * conn_sn），只需按当前状态刷新头字节的 NESN 位。 */
        tx_buf[0] = (uint8_t)((tx_buf[0] & 0x03u) | (conn_nesn << 2) | (conn_sn << 3));
        return;
    }

    /* 组新 PDU：待发响应 > DLE 自发起 > 应用通知 > 空 PDU。
     * 通知顶替空包是合法的——SN/NESN 语义由通知承载。每事件一条。 */
    uint8_t llid = 0x01u;
    uint32_t blen = 0;

    if (pend_count > 0u) {
        llid = pend_llid_q[pend_head];
        blen = pend_blen_q[pend_head];
        for (uint32_t i = 0; i < blen; i++) tx_buf[2 + i] = pend_body_q[pend_head][i];
        pend_head = (uint8_t)((pend_head + 1u) % PEND_SLOTS);
        pend_count--;
        if (blen > st->maxrsp) st->maxrsp = blen;
    } else if (g_dle_state == 0 && g_conn_evt > 8u &&
               (g_conn_evt - g_dle_sent_evt) > 8u) {
        /* If we'd otherwise send an empty PDU and DLE hasn't happened, start
         * it ourselves (retrying every 8 events until the peer answers). */
        llid = 0x03u;
        tx_buf[2] = LL_LENGTH_REQ;
        tx_buf[3] = 251u;   tx_buf[4] = 0u;    /* MaxRxOctets = 251 */
        tx_buf[5] = 0x48u;  tx_buf[6] = 0x08u; /* MaxRxTime = 2120 us */
        tx_buf[7] = 251u;   tx_buf[8] = 0u;    /* MaxTxOctets = 251 */
        tx_buf[9] = 0x48u;  tx_buf[10] = 0x08u;/* MaxTxTime = 2120 us */
        blen = 9;
        g_dle_sent_evt = g_conn_evt;
    } else if (g_dle_state == 2u && ops->att_notify_pull) {
        /* 下行通知：DLE 完成后才发大帧（未完成时通知留队）。 */
        static uint8_t att[250];
        uint32_t alen = ops->att_notify_pull(att, sizeof(att));
        if (alen > 0u) {
            llid = 0x02u;
            tx_buf[2] = (uint8_t)alen; tx_buf[3] = (uint8_t)(alen >> 8);
            tx_buf[4] = 0x04u;         tx_buf[5] = 0x00u;   /* CID = ATT */
            for (uint32_t k = 0; k < alen; k++) tx_buf[6 + k] = att[k];
            blen = 4u + alen;
        }
    }

    tx_buf[0] = (uint8_t)(llid | (conn_nesn << 2) | (conn_sn << 3));
    tx_buf[1] = (uint8_t)blen;
    tx_unacked = (blen > 0u);
}

/*
 * One connection event: listen on `ch`, and on a valid packet reply T_IFS
 * later (hardware turnaround, timing owned by the radio driver).
 * Returns crc_ok and *anchor_out (start-of-packet time) on success, 0 on miss.
 */
static int conn_event(const ll_ops_t *ops, uint32_t ch, uint32_t window_us,
                      uint64_t *anchor_out, ll_stats_t *st)
{
    ops->radio_set_channel(data_freq(ch), ch);

    int crc_ok = 0;
    uint64_t t_addr = 0, t_end = 0;
    int got = ops->radio_rx(rx_buf, sizeof(rx_buf), window_us, &t_addr, &t_end, &crc_ok);
    if (!got) {
        return 0;
    }
    if (!crc_ok) {
        /* CRC 错的包按规范丢弃、本事件关闭——不回任何 PDU（回应干扰包
         * 会凭空占用信道并污染对端接收）。返回 0 计一次 miss。 */
        return 0;
    }

    /* Build the reply and send it T_IFS after the RX end (driver-timed). */
    conn_reply(ops, st, crc_ok);
    uint32_t blen = (uint32_t)tx_buf[1] + 2u;
    if (ops->radio_reply_at(tx_buf, blen, t_end)) {
        st->tx_done++;
        if (st->gap_us == 0) {
            st->gap_us = (uint32_t)(ops->now_us() - t_end);
        }
    } else {
        st->tx_timeouts++;
    }

    /* Ring-buffer the last few interesting PDUs (LL control or non-empty), so
     * the post-mortem shows what happened right before a disconnect. */
    if (crc_ok && ((rx_buf[0] & 3u) == 3u || rx_buf[1] != 0u)) {
        uint32_t slot = st->rxpdu_n % 6u;
        uint32_t n = 2u + rx_buf[1];
        if (n > 32u) n = 32u;
        for (uint32_t i = 0; i < n; i++) st->rxpdu[slot][i] = rx_buf[i];
        st->txhdr[slot] = tx_buf[0];             /* 我们同事件的回复头 */
        st->rxevt[slot] = (uint16_t)g_conn_evt;
        st->rxpdu_n++;
    }

    /* Anchor = start of the central's packet ~= ADDRESS time - (preamble+AA). */
    *anchor_out = t_addr - 40u;
    return crc_ok;
}

/* ================================================== ADVERTISING ======= */
/* TX ADV_IND on adv channel n, listen for a reply. Returns 1 if a CONNECT_IND
 * for us was captured (and stores its end time in *t_ci_end). */
static int adv_and_listen(const ll_ops_t *ops, uint32_t n, ll_conn_t *conn,
                          uint64_t *t_ci_end, ll_adv_stats_t *ast)
{
    ops->radio_set_channel(adv_freq[n], adv_idx[n]);
    ops->radio_tx(adv_pdu, (uint32_t)adv_pdu[1] + 2u, 0);   /* 无超时：PHYEND_DISABLE 保证结束 */

    uint64_t t_addr = 0, t_end = 0;
    int crc_ok = 0;
    int got = ops->radio_rx(rx_buf, sizeof(rx_buf), RX_WINDOW_US, &t_addr, &t_end, &crc_ok);
    (void)t_addr;

    int is_conn = 0;
    if (got && crc_ok) {
        ast->rx_ok++;
        uint32_t type = rx_buf[0] & 0xF;
        int directed = (type == PDU_TYPE_CONNECT_IND || type == PDU_TYPE_SCAN_REQ);
        for (uint32_t k = 0; directed && k < 6; k++) {
            if (rx_buf[8 + k] != our_addr[k]) directed = 0;
        }

        if (directed && type == PDU_TYPE_CONNECT_IND) {
            *t_ci_end = ops->now_us();          /* capture NOW, print later */
            conn_parse(conn, &rx_buf[14]);
            /* 参数合法性：interval 最小 6（7.5ms，BT 规范）；winsize/timeout
             * 非零且 timeout ≥ interval——否则后续会除零/死等，拒绝本次连接 */
            if (conn->interval_us < 7500u || conn->winsize_us == 0u ||
                conn->timeout_us < conn->interval_us) {
                is_conn = 0;
            } else {
                is_conn = 1;
            }
        } else if (directed && type == PDU_TYPE_SCAN_REQ) {
            /* Answer the scan request with a SCAN_RSP, T_IFS later (software
             * timed — nRF54L has no hardware TIFS turnaround). */
            ast->scan_req++;
            if (ops->radio_reply_at(scan_rsp_pdu, (uint32_t)scan_rsp_pdu[1] + 2u, t_end)) {
                ast->scan_rsp++;
            }
        }
    } else if (got) {
        ast->rx_err++;
    }
    ops->radio_disable();
    return is_conn;
}

int ll_adv_sweep(const ll_ops_t *ops, ll_conn_t *conn,
                 uint64_t *t_ci_end, ll_adv_stats_t *ast)
{
    ops->radio_set_aa(0x8E89BED6u, 0x555555u);   /* advertising AA/CRCInit */
    for (uint32_t n = 0; n < 3; n++) {
        if (adv_and_listen(ops, n, conn, t_ci_end, ast)) {
            return 1;
        }
    }
    return 0;
}

/* ================================================== CONNECTION ======= */
/* Returns after the connection ends (supervision-style timeout on misses). */
void ll_conn_run(const ll_ops_t *ops, ll_conn_t *conn,
                 uint64_t t_ci_end, ll_stats_t *st)
{
    ops->radio_set_aa(conn->aa, conn->crcinit);
    conn_sn = 0; conn_nesn = 0;
    tx_unacked = 0; pend_head = 0; pend_count = 0;
    csa1_last = 0;
    g_terminate = 0;
    g_upd_pending = 0; g_chm_pending = 0;
    g_dle_state = 0; g_dle_sent_evt = 0; g_conn_evt = 0;
    if (ops->on_connect) {
        ops->on_connect();          /* reset ATT MTU / data-length state */
    }

    *st = (ll_stats_t){0};

    uint64_t anchor = t_ci_end + 1250u + conn->winoffset_us;
    uint32_t misses = 0, events = 0, hits = 0;
    uint16_t counter = 0;           /* connEventCounter */
    int at_instant = 0;             /* this event uses a fresh transmit window */

    /* Give up only when we've missed the peer for the whole supervision
     * timeout, not after a handful of packets (which may just be RF loss). */
    uint32_t max_misses = conn->timeout_us / conn->interval_us;
    if (max_misses < 6u) max_misses = 6u;

    for (;;) {
        /* Apply a pending update at its Instant (before this event). 用 16 位
         * 回绕安全的 "已到达/已越过" 判定而非 ==：链路恶化时 IND 可能重传
         * 多次才被收到，instant 已过——严格相等会永远不切换（central 已按
         * 新参数跳频，我们从此全 miss 直到监督超时,实板 Android 复现）。
         * 晚应用只损失中间几个事件,随后重新同步。 */
        at_instant = 0;
        if (g_chm_pending && (uint16_t)(counter - g_chm_instant) < 32768u) {
            /* 非法 ChM（0 个信道会让 CSA#1 重映射模零）：回滚保留旧表 */
            uint8_t old_chm[5], old_num = conn->num_used, old_used[37];
            for (int i = 0; i < 5; i++) old_chm[i] = conn->chmap[i];
            for (uint32_t i = 0; i < old_num; i++) old_used[i] = conn->used[i];
            for (int i = 0; i < 5; i++) conn->chmap[i] = g_chm_new[i];
            conn->num_used = 0;
            for (uint32_t c = 0; c < 37; c++)
                if (conn->chmap[c >> 3] & (1u << (c & 7))) conn->used[conn->num_used++] = (uint8_t)c;
            if (conn->num_used == 0) {
                for (int i = 0; i < 5; i++) conn->chmap[i] = old_chm[i];
                conn->num_used = old_num;
                for (uint32_t i = 0; i < old_num; i++) conn->used[i] = old_used[i];
            }
            g_chm_pending = 0;
        }
        if (g_upd_pending && (uint16_t)(counter - g_upd_instant) < 32768u) {
            /* 晚 late 个事件应用：instant 点之后 central 已按新 interval 走了
             * late 个周期,而我们的 anchor 是按旧 interval 推的——补上差值。 */
            uint16_t late = (uint16_t)(counter - g_upd_instant);
            int64_t drift = (int64_t)g_upd_interval_us - (int64_t)conn->interval_us;
            anchor = (uint64_t)((int64_t)anchor + (int64_t)g_upd_winoffset_us +
                                (int64_t)late * drift);
            conn->interval_us = g_upd_interval_us;
            conn->winsize_us  = g_upd_winsize_us;
            conn->timeout_us  = g_upd_timeout_us;
            max_misses = conn->timeout_us / conn->interval_us;
            if (max_misses < 6u) max_misses = 6u;
            g_upd_pending = 0;
            at_instant = 1;
            misses = 0;                            /* fresh sync window */
        }

        uint32_t ch = csa1_next(conn);

        /* Window widening: the longer since our last sync, the wider we must
         * listen. WW = (elapsed since sync) * combined_ppm. */
        uint32_t since_sync = (misses + 1u) * conn->interval_us;
        uint32_t ww = since_sync / WW_DIV;
        uint32_t pre = 400u + ww;
        uint32_t first_extra = (events == 0u || at_instant) ? conn->winsize_us : 0u;
        uint32_t win = pre + ww + first_extra + 1500u;

        uint32_t cap = conn->interval_us / 2u;          /* don't overrun next event */
        if (pre > cap) pre = cap;
        if (win > conn->interval_us - 200u) win = conn->interval_us - 200u;

        g_conn_evt = counter;         /* expose to conn_reply for DLE timing */
        wait_until(ops, anchor - pre);
        uint64_t got_anchor;
        int ok = conn_event(ops, ch, win, &got_anchor, st);
        if (ops->on_conn_event) {
            /* reply 已发完，距下个 anchor 有 ~interval 松弛——安全插桩点。
             * 回调里只做微秒级操作（如入队日志），阻塞会毁时序。 */
            ops->on_conn_event(counter, ok, ch);
        }
        events++;
        counter++;                    /* connEventCounter advances every event */

        if (ok) {
            if (hits == 0 && ops->led) ops->led(1);
            hits++;
            misses = 0;
            anchor = got_anchor + conn->interval_us;    /* re-sync to peer */
            if (g_terminate) {                          /* peer closed the link */
                if (ops->led) ops->led(0);
                ops->radio_disable();
                st->events = events;
                st->hits = hits;
                st->mtu = ops->mtu_get ? ops->mtu_get() : 0u;
                st->tx_octets = ops->txoct_get ? ops->txoct_get() : 0u;
                return;
            }
        } else {
            misses++;
            anchor += conn->interval_us;                /* keep cadence */
            if (misses >= max_misses) {
                if (ops->led) ops->led(0);
                ops->radio_disable();
                st->events = events;
                st->hits = hits;
                st->mtu = ops->mtu_get ? ops->mtu_get() : 0u;
                st->tx_octets = ops->txoct_get ? ops->txoct_get() : 0u;
                return;
            }
        }
    }
}

/*
 * 03b_connection — bare-metal BLE peripheral that actually ENTERS a connection.
 *
 * Flow:
 *   1. Advertise ADV_IND on 37/38/39, listening for CONNECT_IND (as in 03a).
 *   2. On CONNECT_IND: adopt its AA / CRCInit / hop / interval / channel map,
 *      timestamp the packet end, and switch to CONNECTION state.
 *   3. Connection: at each anchor (re-synced to the peer every event to cancel
 *      our RC-clock drift), hop to the next data channel (CSA #1), receive the
 *      central's data PDU, and T_IFS (150 us, hardware) later reply with an
 *      empty data PDU carrying the correct SN/NESN. Enough to hold the link so
 *      the phone/PC shows "Connected".
 *
 * All state is logged over UART (COM23) — read scripts/serial.log.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "config.h"
#include "nrf.h"
#include "grtc.h"
#include "uart.h"
#include "gatt.h"

/* ---------------------------------------------------------------- LED -- */
/* 板头（boards/xiao_nrf54l15.h）只给端口号；LED 在 P2，映射到 54L 的
 * 独立 GPIO2 寄存器组。换板 = 改板头 + 这里的一行映射。 */
#define LED_REG    NRF_P2_S   /* BOARD_LED_PORT == 2 */
#define LED_MASK   (1u << BOARD_LED_PIN)

static void led_init(void)
{
    LED_REG->PIN_CNF[BOARD_LED_PIN] =
        (GPIO_PIN_CNF_DIR_Output       << GPIO_PIN_CNF_DIR_Pos) |
        (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos);
    LED_REG->DIRSET = LED_MASK;
    LED_REG->OUTSET = LED_MASK;
}
static void led_on(void)
{
    if (BOARD_LED_ACTIVE_LEVEL) {
        LED_REG->OUTSET = LED_MASK;
    } else {
        LED_REG->OUTCLR = LED_MASK;
    }
}
static void led_off(void)
{
    if (BOARD_LED_ACTIVE_LEVEL) {
        LED_REG->OUTCLR = LED_MASK;
    } else {
        LED_REG->OUTSET = LED_MASK;
    }
}

/* --------------------------------------------------------------- misc -- */
static uint32_t rng_state;
static uint32_t rng_next(void)
{
    uint32_t x = rng_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng_state = x; return x;
}
static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd24(const uint8_t *p) { return rd16(p) | ((uint32_t)p[2] << 16); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | ((uint32_t)rd16(p + 2) << 16); }

/* Random static address F0:E0:D0:C0:B0:A0 (top two bits of MSB = 11). */
static const uint8_t OUR_ADDR[6] = { 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0 };

/* ------------------------------------------------------------ BLE PDU -- */
#define PDU_TYPE_ADV_IND      0x0u
#define PDU_TYPE_SCAN_REQ     0x3u
#define PDU_TYPE_CONNECT_IND  0x5u
#define HDR_TXADD_RANDOM      (1u << 6)

static uint8_t adv_pdu[40];
static uint8_t scan_rsp_pdu[40];
static uint8_t rx_buf[260];        /* holds up to a 251-octet DLE data PDU */
static uint8_t tx_buf[260];
static volatile int g_terminate;

#define PDU_TYPE_SCAN_RSP 0x4u

static void build_adv_pdu(void)
{
    static const uint8_t name[] = "54L-GATT";
    uint32_t i = 2;
    for (uint32_t k = 0; k < 6; k++) adv_pdu[i++] = OUR_ADDR[k];
    adv_pdu[i++] = 2; adv_pdu[i++] = 0x01; adv_pdu[i++] = 0x06;
    adv_pdu[i++] = 1 + (sizeof(name) - 1); adv_pdu[i++] = 0x09;
    for (uint32_t k = 0; k < sizeof(name) - 1; k++) adv_pdu[i++] = name[k];
    adv_pdu[0] = PDU_TYPE_ADV_IND | HDR_TXADD_RANDOM;
    adv_pdu[1] = (uint8_t)(i - 2);
}

/* SCAN_RSP: AdvA + Complete Local Name, so active scanners (Android is picky)
 * that SCAN_REQ our scannable ADV_IND get a proper response. */
static void build_scan_rsp(void)
{
    static const uint8_t name[] = "54L-GATT";
    uint32_t i = 2;
    for (uint32_t k = 0; k < 6; k++) scan_rsp_pdu[i++] = OUR_ADDR[k];
    scan_rsp_pdu[i++] = 1 + (sizeof(name) - 1);
    scan_rsp_pdu[i++] = 0x09;
    for (uint32_t k = 0; k < sizeof(name) - 1; k++) scan_rsp_pdu[i++] = name[k];
    scan_rsp_pdu[0] = PDU_TYPE_SCAN_RSP | HDR_TXADD_RANDOM;
    scan_rsp_pdu[1] = (uint8_t)(i - 2);
}

/* -------------------------------------------------------------- RADIO -- */
static void hfxo_start(void)
{
    NRF_CLOCK_S->EVENTS_XOSTARTED = 0;
    NRF_CLOCK_S->TASKS_XOSTART = 1;
    while (NRF_CLOCK_S->EVENTS_XOSTARTED == 0) { }
}

static void radio_common(void)
{
    NRF_RADIO_S->MODE = RADIO_MODE_MODE_Ble_1Mbit << RADIO_MODE_MODE_Pos;
    NRF_RADIO_S->TXPOWER = RADIO_TXPOWER_TXPOWER_Pos8dBm << RADIO_TXPOWER_TXPOWER_Pos;
    NRF_RADIO_S->PCNF0 =
        (8u << RADIO_PCNF0_LFLEN_Pos) |
        (1u << RADIO_PCNF0_S0LEN_Pos) |
        (RADIO_PCNF0_PLEN_8bit << RADIO_PCNF0_PLEN_Pos);
    NRF_RADIO_S->PCNF1 =
        (251u << RADIO_PCNF1_MAXLEN_Pos) |           /* allow DLE-sized PDUs */
        (3u   << RADIO_PCNF1_BALEN_Pos)  |
        (RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos) |
        (1u   << RADIO_PCNF1_WHITEEN_Pos);
    NRF_RADIO_S->CRCCNF =
        (RADIO_CRCCNF_LEN_Three     << RADIO_CRCCNF_LEN_Pos) |
        (RADIO_CRCCNF_SKIPADDR_Skip << RADIO_CRCCNF_SKIPADDR_Pos);
    NRF_RADIO_S->CRCPOLY = 0x00065Bu;
    NRF_RADIO_S->TXADDRESS   = 0u;
    NRF_RADIO_S->RXADDRESSES = 1u;
}

/* Access address & CRC init: advertising vs connection. */
static void radio_aa(uint32_t aa, uint32_t crcinit)
{
    NRF_RADIO_S->BASE0   = aa << 8;
    NRF_RADIO_S->PREFIX0 = (aa >> 24) & 0xFF;
    NRF_RADIO_S->CRCINIT = crcinit & 0xFFFFFF;
}

static void radio_channel(uint32_t freq_off, uint32_t white_ch)
{
    NRF_RADIO_S->FREQUENCY = freq_off;
    NRF_RADIO_S->DATAWHITE =
        (NRF_RADIO_S->DATAWHITE & RADIO_DATAWHITE_POLY_Msk) | (0x40u | white_ch);
}

static void radio_disable(void)
{
    NRF_RADIO_S->SHORTS = 0;
    NRF_RADIO_S->EVENTS_DISABLED = 0;
    NRF_RADIO_S->TASKS_DISABLE = 1;
    while (NRF_RADIO_S->EVENTS_DISABLED == 0) { }
}

/* advertising-channel index -> RF frequency offset */
static const uint8_t adv_freq[3] = { 2u, 26u, 80u };
static const uint8_t adv_idx[3]  = { 37u, 38u, 39u };

/* data channel (0..36) -> RF frequency offset from 2400 MHz */
static uint32_t data_freq(uint32_t ch)
{
    return (ch <= 10u) ? (2u * ch + 4u) : (2u * ch + 6u);
}

/* ================================================ CONNECTION STATE ==== */
typedef struct {
    uint32_t aa, crcinit;
    uint32_t interval_us;     /* connInterval in microseconds */
    uint32_t winsize_us, winoffset_us;
    uint32_t timeout_us;      /* supervision timeout */
    uint8_t  hop;
    uint8_t  chmap[5];
    uint8_t  used[37];
    uint8_t  num_used;
} conn_t;

static conn_t conn;
static uint8_t conn_sn, conn_nesn;
static uint32_t g_conn_events, g_conn_hits;

/* Pending parameter/channel-map updates, applied at their Instant (the
 * connEventCounter value at which master and slave switch together). */
static volatile int g_upd_pending;
static uint16_t g_upd_instant;
static uint32_t g_upd_interval_us, g_upd_winsize_us, g_upd_winoffset_us, g_upd_timeout_us;
static volatile int g_chm_pending;
static uint16_t g_chm_instant;
static uint8_t  g_chm_new[5];

/* Peripheral-initiated Data Length Extension: some centrals never start it, so
 * we do, to raise the data-PDU size from 27 to 251 octets. */
static int      g_dle_state;      /* 0 = not negotiated, 2 = done */
static uint32_t g_dle_sent_evt;
static uint32_t g_conn_evt;       /* current event index (mirror of counter) */

/* Capture the first few received data PDUs for post-mortem logging. */
static uint8_t  g_rxpdu[6][32];
static uint32_t g_rxpdu_n;
static uint32_t g_tx_done;
static uint32_t g_dbg_gap;
static uint32_t g_tx_timeouts;
static uint32_t g_dbg_maxrsp;

static void conn_parse(const uint8_t *lld)
{
    conn.aa          = rd32(lld);
    conn.crcinit     = rd24(lld + 4);
    conn.winsize_us  = (uint32_t)lld[7] * 1250u;
    conn.winoffset_us= rd16(lld + 8) * 1250u;
    conn.interval_us = rd16(lld + 10) * 1250u;
    conn.timeout_us  = rd16(lld + 14) * 10000u;
    conn.hop         = lld[21] & 0x1F;
    for (int i = 0; i < 5; i++) conn.chmap[i] = lld[16 + i];
    conn.num_used = 0;
    for (uint32_t c = 0; c < 37; c++) {
        if (conn.chmap[c >> 3] & (1u << (c & 7))) conn.used[conn.num_used++] = (uint8_t)c;
    }
}

/* Channel Selection Algorithm #1 */
static uint8_t csa1_last;
static uint32_t csa1_next(void)
{
    csa1_last = (uint8_t)((csa1_last + conn.hop) % 37u);
    if (conn.chmap[csa1_last >> 3] & (1u << (csa1_last & 7))) return csa1_last;
    return conn.used[csa1_last % conn.num_used];       /* remap unused channel */
}

/* LL control opcodes */
#define LL_CONNECTION_UPDATE_IND 0x00u
#define LL_CHANNEL_MAP_IND       0x01u
#define LL_TERMINATE_IND 0x02u
#define LL_FEATURE_REQ   0x08u
#define LL_FEATURE_RSP   0x09u
#define LL_VERSION_IND   0x0Cu
#define LL_UNKNOWN_RSP   0x07u
#define LL_LENGTH_REQ    0x14u
#define LL_LENGTH_RSP    0x15u

/* Octets we may transmit given the peer's MaxRxOctets and MaxRxTime.
 * Time budget in octets = (MaxRxTime - 80us overhead) / 8us per octet. */
static uint32_t dle_effective(uint32_t max_rx_octets, uint32_t max_rx_time)
{
    uint32_t by_time = (max_rx_time > 80u) ? ((max_rx_time - 80u) / 8u) : 27u;
    return (max_rx_octets < by_time) ? max_rx_octets : by_time;
}

/*
 * Build our reply, updating SN/NESN from the received header. If the peer sent
 * an LL control PDU (LLID=3), answer it in this same T_IFS turnaround so the
 * central completes its setup procedures instead of terminating on timeout.
 */
static void conn_reply(int crc_ok)
{
    uint8_t rx_hdr = rx_buf[0];
    if (crc_ok) {
        uint8_t sn_r  = (rx_hdr >> 3) & 1u;
        uint8_t nesn_r= (rx_hdr >> 2) & 1u;
        if (sn_r == conn_nesn)  conn_nesn ^= 1u;   /* got new data -> ack it   */
        if (nesn_r != conn_sn)  conn_sn   ^= 1u;   /* peer acked us -> advance  */
    }

    uint8_t llid = 0x01u;   /* default: empty data PDU */
    static uint8_t body[256];
    uint32_t blen = 0;
    uint8_t rxllid = rx_hdr & 3u;
    uint8_t rxlen  = rx_buf[1];

    if (crc_ok && rxllid == 3u && rxlen >= 1u) {
        /* --- LL control PDU --- */
        uint8_t op = rx_buf[2];
        llid = 0x03u;
        switch (op) {
        case LL_FEATURE_REQ:
            body[0] = LL_FEATURE_RSP;
            for (int i = 0; i < 8; i++) body[1 + i] = 0u;
            blen = 9;
            break;
        case LL_VERSION_IND:
            body[0] = LL_VERSION_IND;
            body[1] = 0x0Cu;                 /* VersNr: BT 5.3 */
            body[2] = 0xFFu; body[3] = 0xFFu;/* CompId (test)  */
            body[4] = 0x01u; body[5] = 0x00u;/* SubVersNr      */
            blen = 6;
            break;
        case LL_LENGTH_REQ: {
            /* Effective TX size is bounded by BOTH the peer's MaxRxOctets and
             * its MaxRxTime (octets that fit in that time). */
            gatt_set_tx_octets(dle_effective(rd16(&rx_buf[3]), rd16(&rx_buf[5])));
            g_dle_state = 2;
            body[0] = LL_LENGTH_RSP;
            body[1] = 251u;  body[2] = 0u;   /* MaxRxOctets = 251 */
            body[3] = 0x48u; body[4] = 0x08u;/* MaxRxTime = 2120 us */
            body[5] = 251u;  body[6] = 0u;   /* MaxTxOctets = 251 */
            body[7] = 0x48u; body[8] = 0x08u;/* MaxTxTime = 2120 us */
            blen = 9;
            break;
        }
        case LL_LENGTH_RSP: {
            /* Reply to our own LL_LENGTH_REQ: adopt the negotiated size. */
            gatt_set_tx_octets(dle_effective(rd16(&rx_buf[3]), rd16(&rx_buf[5])));
            g_dle_state = 2;
            llid = 0x01u;                    /* just ack */
            break;
        }
        case LL_CONNECTION_UPDATE_IND:
            /* WinSize(1) WinOffset(2) Interval(2) Latency(2) Timeout(2) Instant(2) */
            g_upd_winsize_us   = (uint32_t)rx_buf[3] * 1250u;
            g_upd_winoffset_us = rd16(&rx_buf[4]) * 1250u;
            g_upd_interval_us  = rd16(&rx_buf[6]) * 1250u;
            g_upd_timeout_us   = rd16(&rx_buf[10]) * 10000u;
            g_upd_instant      = (uint16_t)rd16(&rx_buf[12]);
            g_upd_pending      = 1;
            llid = 0x01u;                    /* IND: just ack, no response */
            break;
        case LL_CHANNEL_MAP_IND:
            /* ChM(5) Instant(2) */
            for (int i = 0; i < 5; i++) g_chm_new[i] = rx_buf[3 + i];
            g_chm_instant = (uint16_t)rd16(&rx_buf[8]);
            g_chm_pending = 1;
            llid = 0x01u;
            break;
        case LL_TERMINATE_IND:
            g_terminate = 1;
            llid = 0x01u;
            break;
        default:
            body[0] = LL_UNKNOWN_RSP;
            body[1] = op;
            blen = 2;
            break;
        }
    } else if (crc_ok && (rxllid == 2u || rxllid == 1u) && rxlen >= 4u) {
        /* --- L2CAP frame: header = length(2) + CID(2) --- */
        uint16_t cid = (uint16_t)(rx_buf[4] | (rx_buf[5] << 8));
        if (cid == 0x0004u) {                    /* ATT channel */
            static uint8_t att[250];
            uint32_t alen = gatt_handle_att(&rx_buf[6], (uint32_t)rxlen - 4u, att);
            if (alen > 0u) {
                llid = 0x02u;                    /* L2CAP start */
                body[0] = (uint8_t)alen; body[1] = (uint8_t)(alen >> 8);
                body[2] = 0x04u;         body[3] = 0x00u;   /* CID = ATT */
                for (uint32_t k = 0; k < alen; k++) body[4 + k] = att[k];
                blen = 4u + alen;
                if (blen > g_dbg_maxrsp) g_dbg_maxrsp = blen;
            }
        }
    }

    /* If we'd otherwise send an empty PDU and DLE hasn't happened, start it
     * ourselves (retransmitting every 8 events until the peer answers). */
    if (llid == 0x01u && blen == 0u && g_dle_state == 0 &&
        g_conn_evt > 8u && (g_conn_evt - g_dle_sent_evt) > 8u) {
        llid = 0x03u;
        body[0] = LL_LENGTH_REQ;
        body[1] = 251u;  body[2] = 0u;
        body[3] = 0x48u; body[4] = 0x08u;
        body[5] = 251u;  body[6] = 0u;
        body[7] = 0x48u; body[8] = 0x08u;
        blen = 9;
        g_dle_sent_evt = g_conn_evt;
    }

    tx_buf[0] = llid | (conn_nesn << 2) | (conn_sn << 3);
    tx_buf[1] = (uint8_t)blen;
    for (uint32_t i = 0; i < blen; i++) tx_buf[2 + i] = body[i];
}

/*
 * One connection event: listen on `ch`, and on a valid packet reply T_IFS
 * later (hardware turnaround). Returns 1 and *anchor_out (start-of-packet time)
 * on success, 0 on miss.
 */
/* nRF54L has no hardware TIFS turnaround, so we time the RX->TX switch in
 * software: after the received packet ends, wait until (T_IFS - TXEN ramp) and
 * fire TASKS_TXEN so the reply starts on air exactly T_IFS after RX. LEAD is
 * calibrated so the measured rx_end->tx_end gap is ~230 us (150 + 80). */
#define TURNAROUND_LEAD_US 98u

static int conn_event(uint32_t ch, uint32_t window_us, uint64_t *anchor_out)
{
    radio_channel(data_freq(ch), ch);
    NRF_RADIO_S->PACKETPTR = (uint32_t)rx_buf;
    NRF_RADIO_S->SHORTS = RADIO_SHORTS_READY_START_Msk |
                          RADIO_SHORTS_PHYEND_DISABLE_Msk;
    NRF_RADIO_S->EVENTS_DISABLED = 0;
    NRF_RADIO_S->EVENTS_CRCOK    = 0;
    NRF_RADIO_S->EVENTS_CRCERROR = 0;
    NRF_RADIO_S->EVENTS_ADDRESS  = 0;
    NRF_RADIO_S->EVENTS_PHYEND   = 0;

    uint64_t t0 = grtc_now();
    NRF_RADIO_S->TASKS_RXEN = 1;

    /* Wait for a packet to start (ADDRESS) or the listen window to expire. */
    while (NRF_RADIO_S->EVENTS_ADDRESS == 0) {
        if ((grtc_now() - t0) > window_us) {
            radio_disable();
            return 0;
        }
    }
    uint64_t addr_ts = grtc_now();

    /* Wait for the received packet to end (PHYEND). */
    while (NRF_RADIO_S->EVENTS_PHYEND == 0 && NRF_RADIO_S->EVENTS_DISABLED == 0) { }
    uint64_t t_phyend = grtc_now();

    /* CRC result lands right after PHYEND. */
    while (NRF_RADIO_S->EVENTS_CRCOK == 0 && NRF_RADIO_S->EVENTS_CRCERROR == 0) {
        if ((grtc_now() - t_phyend) > 20u) break;
    }
    int crc_ok = NRF_RADIO_S->EVENTS_CRCOK ? 1 : 0;

    /* Build the reply and arm TX (radio auto-disabled via PHYEND_DISABLE). */
    conn_reply(crc_ok);
    NRF_RADIO_S->PACKETPTR = (uint32_t)tx_buf;
    while (NRF_RADIO_S->EVENTS_DISABLED == 0) { }   /* RX now disabled */
    NRF_RADIO_S->EVENTS_DISABLED = 0;
    NRF_RADIO_S->EVENTS_PHYEND   = 0;

    /* Software TIFS: fire TXEN so the reply starts T_IFS after the RX end. */
    while ((int64_t)((t_phyend + TURNAROUND_LEAD_US) - grtc_now()) > 0) { }
    NRF_RADIO_S->TASKS_TXEN = 1;

    /* Large DLE packets take longer on air (a 251-octet PDU is ~2.1 ms), so
     * allow enough time or radio_disable() would abort our own TX. */
    uint64_t tw = grtc_now();
    while (NRF_RADIO_S->EVENTS_DISABLED == 0) {
        if ((grtc_now() - tw) > 3000u) { g_tx_timeouts++; break; }
    }
    if (NRF_RADIO_S->EVENTS_DISABLED) {
        g_tx_done++;
        if (g_dbg_gap == 0) g_dbg_gap = (uint32_t)(grtc_now() - t_phyend);
    }
    radio_disable();

    /* Ring-buffer the last few interesting PDUs (LL control or non-empty), so
     * the post-mortem shows what happened right before a disconnect. */
    if (crc_ok && ((rx_buf[0] & 3u) == 3u || rx_buf[1] != 0u)) {
        uint32_t slot = g_rxpdu_n % 6u;
        uint32_t n = 2u + rx_buf[1];
        if (n > 32u) n = 32u;
        for (uint32_t i = 0; i < n; i++) g_rxpdu[slot][i] = rx_buf[i];
        g_rxpdu_n++;
    }

    /* Anchor = start of the central's packet ~= ADDRESS time - (preamble+AA). */
    *anchor_out = addr_ts - 40u;
    return crc_ok;
}

static void wait_until(uint64_t t)
{
    while ((int64_t)(t - grtc_now()) > 0) { }
}

/* Returns after the connection ends (supervision-style timeout on misses). */
static void run_connection(uint64_t t_ci_end)
{
    radio_aa(conn.aa, conn.crcinit);
    conn_sn = 0; conn_nesn = 0;
    csa1_last = 0;
    g_terminate = 0;
    g_upd_pending = 0; g_chm_pending = 0;
    g_dle_state = 0; g_dle_sent_evt = 0; g_conn_evt = 0;
    gatt_on_connect();          /* reset ATT MTU / data-length state */

    uint64_t anchor = t_ci_end + 1250u + conn.winoffset_us;
    uint32_t misses = 0, events = 0, hits = 0;
    uint16_t counter = 0;           /* connEventCounter */
    int at_instant = 0;             /* this event uses a fresh transmit window */

    /* Give up only when we've missed the peer for the whole supervision
     * timeout, not after a handful of packets (which may just be RF loss). */
    uint32_t max_misses = conn.timeout_us / conn.interval_us;
    if (max_misses < 6u) max_misses = 6u;

    /* Combined clock accuracy (ppm) used for Window Widening; generous to
     * cover the internal RC keep-alive source. ww = since_sync * ppm / 1e6,
     * rewritten as a 32-bit divide (no 64-bit __aeabi_uldivmod). */
    #define WW_PPM 2000u
    #define WW_DIV (1000000u / WW_PPM)   /* = 500 */

    /* NO prints inside this loop: at 115200 baud a single line costs several
     * ms and would blow the sub-ms connection timing. Summary is printed once
     * after the link is lost, back in the advertising slack. */
    for (;;) {
        /* Apply a pending update exactly at its Instant (before this event). */
        at_instant = 0;
        if (g_chm_pending && counter == g_chm_instant) {
            for (int i = 0; i < 5; i++) conn.chmap[i] = g_chm_new[i];
            conn.num_used = 0;
            for (uint32_t c = 0; c < 37; c++)
                if (conn.chmap[c >> 3] & (1u << (c & 7))) conn.used[conn.num_used++] = (uint8_t)c;
            g_chm_pending = 0;
        }
        if (g_upd_pending && counter == g_upd_instant) {
            anchor += g_upd_winoffset_us;          /* new transmit-window offset */
            conn.interval_us = g_upd_interval_us;
            conn.winsize_us  = g_upd_winsize_us;
            conn.timeout_us  = g_upd_timeout_us;
            max_misses = conn.timeout_us / conn.interval_us;
            if (max_misses < 6u) max_misses = 6u;
            g_upd_pending = 0;
            at_instant = 1;
            misses = 0;                            /* fresh sync window */
        }

        uint32_t ch = csa1_next();

        /* Window widening: the longer since our last sync, the wider we must
         * listen. WW = (elapsed since sync) * combined_ppm. */
        uint32_t since_sync = (misses + 1u) * conn.interval_us;
        uint32_t ww = since_sync / WW_DIV;
        uint32_t pre = 400u + ww;
        uint32_t first_extra = (events == 0u || at_instant) ? conn.winsize_us : 0u;
        uint32_t win = pre + ww + first_extra + 1500u;

        uint32_t cap = conn.interval_us / 2u;          /* don't overrun next event */
        if (pre > cap) pre = cap;
        if (win > conn.interval_us - 200u) win = conn.interval_us - 200u;

        g_conn_evt = counter;         /* expose to conn_reply for DLE timing */
        wait_until(anchor - pre);
        uint64_t got_anchor;
        int ok = conn_event(ch, win, &got_anchor);
        events++;
        counter++;                    /* connEventCounter advances every event */

        if (ok) {
            if (hits == 0) led_on();
            hits++;
            misses = 0;
            anchor = got_anchor + conn.interval_us;    /* re-sync to peer */
            if (g_terminate) {                         /* peer closed the link */
                led_off();
                radio_disable();
                g_conn_events = events;
                g_conn_hits = hits;
                return;
            }
        } else {
            misses++;
            anchor += conn.interval_us;                /* keep cadence */
            if (misses >= max_misses) {
                led_off();
                radio_disable();
                g_conn_events = events;
                g_conn_hits = hits;
                return;
            }
        }
    }
}

/* ================================================== ADVERTISING ======= */
#define RX_WINDOW_US 300u
static uint32_t g_rx_ok, g_rx_err;

/* TX ADV_IND on adv channel n, listen for a reply. Returns 1 if a CONNECT_IND
 * for us was captured (and stores its end time in *t_ci_end). */
static int adv_and_listen(uint32_t n, uint64_t *t_ci_end)
{
    radio_channel(adv_freq[n], adv_idx[n]);

    NRF_RADIO_S->PACKETPTR = (uint32_t)adv_pdu;
    NRF_RADIO_S->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_PHYEND_DISABLE_Msk;
    NRF_RADIO_S->EVENTS_DISABLED = 0;
    NRF_RADIO_S->TASKS_TXEN = 1;
    while (NRF_RADIO_S->EVENTS_DISABLED == 0) { }

    NRF_RADIO_S->PACKETPTR = (uint32_t)rx_buf;
    NRF_RADIO_S->EVENTS_DISABLED = 0;
    NRF_RADIO_S->EVENTS_CRCOK    = 0;
    NRF_RADIO_S->EVENTS_CRCERROR = 0;
    NRF_RADIO_S->EVENTS_ADDRESS  = 0;
    NRF_RADIO_S->EVENTS_PHYEND   = 0;
    NRF_RADIO_S->TASKS_RXEN = 1;

    /* Wait for an incoming packet to start, or give up. */
    uint64_t t0 = grtc_now();
    while (NRF_RADIO_S->EVENTS_ADDRESS == 0) {
        if ((grtc_now() - t0) > RX_WINDOW_US) { radio_disable(); return 0; }
    }
    while (NRF_RADIO_S->EVENTS_PHYEND == 0 && NRF_RADIO_S->EVENTS_DISABLED == 0) { }
    uint64_t t_phyend = grtc_now();
    while (NRF_RADIO_S->EVENTS_CRCOK == 0 && NRF_RADIO_S->EVENTS_CRCERROR == 0) {
        if ((grtc_now() - t_phyend) > 20u) break;
    }

    int is_conn = 0;
    if (NRF_RADIO_S->EVENTS_CRCOK) {
        g_rx_ok++;
        uint32_t type = rx_buf[0] & 0xF;
        int directed = (type == PDU_TYPE_CONNECT_IND || type == PDU_TYPE_SCAN_REQ);
        for (uint32_t k = 0; directed && k < 6; k++)
            if (rx_buf[8 + k] != OUR_ADDR[k]) directed = 0;

        if (directed && type == PDU_TYPE_CONNECT_IND) {
            *t_ci_end = grtc_now();          /* capture NOW, print later */
            conn_parse(&rx_buf[14]);
            is_conn = 1;
        } else if (directed && type == PDU_TYPE_SCAN_REQ) {
            /* Answer the scan request with a SCAN_RSP, T_IFS later (software
             * timed — nRF54L has no hardware TIFS turnaround). */
            NRF_RADIO_S->PACKETPTR = (uint32_t)scan_rsp_pdu;
            while (NRF_RADIO_S->EVENTS_DISABLED == 0) { }   /* RX disabled */
            NRF_RADIO_S->EVENTS_DISABLED = 0;
            NRF_RADIO_S->EVENTS_PHYEND   = 0;
            while ((int64_t)((t_phyend + TURNAROUND_LEAD_US) - grtc_now()) > 0) { }
            NRF_RADIO_S->TASKS_TXEN = 1;
            uint64_t tw = grtc_now();
            while (NRF_RADIO_S->EVENTS_DISABLED == 0) {
                if ((grtc_now() - tw) > 2000u) break;
            }
        }
    } else {
        g_rx_err++;
    }
    radio_disable();
    return is_conn;
}

int main(void)
{
    led_init();
    uart_init();
    hfxo_start();
    grtc_init();
    build_adv_pdu();
    build_scan_rsp();
    gatt_init();
    radio_common();

    /* picolibc 冒烟：验证 stdio 落点（uart） + malloc 堆（sbrk） + TLS errno
     * （%llu 需要 picolibc 构建时的 -Dio-long-long=true） */
    void *smoke = malloc(64);
    printf("picolibc ready: grtc=%llu us, malloc=%p\n",
           (unsigned long long)grtc_now(), smoke);
    free(smoke);

    rng_state = (uint32_t)grtc_now() | 1u;
    printf("\n=== 54L-GATT connectable + scannable ===\n");

    uint32_t adv_events = 0;
    for (;;) {
        radio_aa(0x8E89BED6u, 0x555555u);      /* advertising AA/CRCInit */
        uint64_t t_ci_end = 0;
        int connreq = 0;
        for (uint32_t n = 0; n < 3; n++) {
            if (adv_and_listen(n, &t_ci_end)) { connreq = 1; break; }
        }

        if (connreq) {
            g_rxpdu_n = 0; g_tx_done = 0; g_dbg_gap = 0;
            g_tx_timeouts = 0; g_dbg_maxrsp = 0;
            run_connection(t_ci_end);          /* blocks until link lost */
            printf("\n[conn] AA=0x%08x int=%uus hop=%u -> events=%u hits=%u tx=%u\n",
                    conn.aa, conn.interval_us, conn.hop, g_conn_events, g_conn_hits, g_tx_done);
            printf("[conn] mtu=%u txoct=%u maxrsp=%u tx_timeouts=%u\n",
                    gatt_dbg_mtu(), gatt_dbg_txoct(), g_dbg_maxrsp, g_tx_timeouts);
            printf("[conn] rx_end->tx_end gap=%uus (expect ~230)\n", g_dbg_gap);
            uint32_t cnt = g_rxpdu_n < 6u ? g_rxpdu_n : 6u;
            uint32_t base = g_rxpdu_n - cnt;    /* oldest still-kept index */
            for (uint32_t j = 0; j < cnt; j++) {
                uint32_t slot = (base + j) % 6u;
                uint32_t llid = g_rxpdu[slot][0] & 0x3u;
                uint32_t len = g_rxpdu[slot][1];
                printf("  rx[-%u] LLID=%u len=%u:", cnt - j, llid, len);
                for (uint32_t k = 0; k < len + 2u && k < 32u; k++)
                    printf(" %02x", g_rxpdu[slot][k]);
                printf("\n");
            }
            continue;
        }

        if ((++adv_events & 127u) == 0)
            printf("[adv] events=%u rx_ok=%u rx_err=%u\n", adv_events, g_rx_ok, g_rx_err);

        uint32_t delay = 20000u + (rng_next() % 10000u);
        uint64_t s = grtc_now();
        while ((grtc_now() - s) < (uint64_t)delay) { }
    }
}

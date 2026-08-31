#include "radio.h"
#include "time.h"
#include "nrf.h"

/*
 * nRF54L 无硬件 TIFS 回转，RX->TX 切换是软件定时：收到包结束后，等到
 * (T_IFS - TXEN ramp) 时刻发 TASKS_TXEN，回复恰好落在 RX 包尾后 T_IFS。
 * LEAD 按实测校准：rx_end->tx_end 间隙 ~230us（150 + 80）。见 01_conn
 * 原 conn_event 注释与 [conn] gap 统计。
 */
/* 2026-08-31 按实测标定：TXEN→READY ramp=43..45us，busy-wait 退出滞后
 * 2..6us，t_end 读取滞后 ~2us → LEAD=100 时空口 T_IFS ≈ 150us。
 * 手机（窗口严格）对早/晚 ~10us 都会拒收；PC 适配器宽容得多。 */
#define TURNAROUND_LEAD_US 98u

/* 迟到容忍：构建超时宁可放弃本事件也不晚发——晚发的包手机收不到，
 * 且响应已进 LL 的 pend 队列，对端重传时下个事件零构建立即回复。 */
#define TURNAROUND_SLACK_US 5u

/* 发送等 DISABLED 的上限：251 字节 DLE 空中约 2.1ms */
#define TX_DONE_TIMEOUT_US 3000u

/* CRC 判定最多等这么久（PHYEND 之后硬件很快置位） */
#define CRC_WAIT_US 20u

/* TIFS 软件定时抖动统计（radio_dbg_tifs 读取并复位） */
static uint32_t g_tifs_late_min = 0xFFFFFFFFu;
static uint32_t g_tifs_late_max;
static uint32_t g_ramp_min = 0xFFFFFFFFu;    /* TXEN→READY 实测 ramp（µs） */
static uint32_t g_ramp_max;

void radio_dbg_tifs(uint32_t *late_min, uint32_t *late_max,
                    uint32_t *ramp_min, uint32_t *ramp_max)
{
    *late_min = g_tifs_late_min;
    *late_max = g_tifs_late_max;
    *ramp_min = g_ramp_min;
    *ramp_max = g_ramp_max;
    g_tifs_late_min = 0xFFFFFFFFu;
    g_tifs_late_max = 0u;
    g_ramp_min = 0xFFFFFFFFu;
    g_ramp_max = 0u;
}

void radio_init(void)
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

void radio_set_aa(uint32_t aa, uint32_t crcinit)
{
    NRF_RADIO_S->BASE0   = aa << 8;
    NRF_RADIO_S->PREFIX0 = (aa >> 24) & 0xFF;
    NRF_RADIO_S->CRCINIT = crcinit & 0xFFFFFF;
}

void radio_set_channel(uint32_t freq_off, uint32_t white_ch)
{
    NRF_RADIO_S->FREQUENCY = freq_off;
    NRF_RADIO_S->DATAWHITE =
        (NRF_RADIO_S->DATAWHITE & RADIO_DATAWHITE_POLY_Msk) | (0x40u | white_ch);
}

void radio_disable(void)
{
    NRF_RADIO_S->SHORTS = 0;
    NRF_RADIO_S->EVENTS_DISABLED = 0;
    NRF_RADIO_S->TASKS_DISABLE = 1;
    while (NRF_RADIO_S->EVENTS_DISABLED == 0) {
    }
}

int radio_tx(const uint8_t *pkt, uint32_t len, uint32_t timeout_us)
{
    (void)len;   /* PLEN 由包内长度字节决定，此处只装指针 */
    NRF_RADIO_S->PACKETPTR = (uint32_t)pkt;
    NRF_RADIO_S->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_PHYEND_DISABLE_Msk;
    NRF_RADIO_S->EVENTS_DISABLED = 0;
    NRF_RADIO_S->TASKS_TXEN = 1;
    if (timeout_us == 0u) {
        while (NRF_RADIO_S->EVENTS_DISABLED == 0) {
        }
        return 1;
    }
    uint64_t tw = time_now_us();
    while (NRF_RADIO_S->EVENTS_DISABLED == 0) {
        if ((time_now_us() - tw) > timeout_us) {
            radio_disable();
            return 0;
        }
    }
    return 1;
}

int radio_rx(uint8_t *pkt, uint32_t maxlen, uint32_t window_us,
             uint64_t *t_addr_us, uint64_t *t_end_us, int *crc_ok)
{
    (void)maxlen;
    NRF_RADIO_S->PACKETPTR = (uint32_t)pkt;
    NRF_RADIO_S->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_PHYEND_DISABLE_Msk;
    NRF_RADIO_S->EVENTS_DISABLED = 0;
    NRF_RADIO_S->EVENTS_CRCOK    = 0;
    NRF_RADIO_S->EVENTS_CRCERROR = 0;
    NRF_RADIO_S->EVENTS_ADDRESS  = 0;
    NRF_RADIO_S->EVENTS_PHYEND   = 0;

    uint64_t t0 = time_now_us();
    NRF_RADIO_S->TASKS_RXEN = 1;

    /* 等包开始（ADDRESS）或窗口过期。 */
    while (NRF_RADIO_S->EVENTS_ADDRESS == 0) {
        if ((time_now_us() - t0) > window_us) {
            radio_disable();
            return 0;
        }
    }
    uint64_t addr_ts = time_now_us();

    /* 等包结束（PHYEND）。已知限制：无超时（沿袭重构前行为）。 */
    while (NRF_RADIO_S->EVENTS_PHYEND == 0 && NRF_RADIO_S->EVENTS_DISABLED == 0) {
    }
    uint64_t t_phyend = time_now_us();

    /* CRC 结果落在 PHYEND 之后。 */
    while (NRF_RADIO_S->EVENTS_CRCOK == 0 && NRF_RADIO_S->EVENTS_CRCERROR == 0) {
        if ((time_now_us() - t_phyend) > CRC_WAIT_US) {
            break;
        }
    }
    *crc_ok = NRF_RADIO_S->EVENTS_CRCOK ? 1 : 0;
    *t_addr_us = addr_ts;
    *t_end_us = t_phyend;
    return 1;
}

int radio_reply_at(const uint8_t *pkt, uint32_t len, uint64_t rx_end_us)
{
    (void)len;
    /* RX 通道先停（PHYEND_DISABLE 短接保证收到包后必然自动停）。 */
    while (NRF_RADIO_S->EVENTS_DISABLED == 0) {
    }
    NRF_RADIO_S->EVENTS_DISABLED = 0;
    NRF_RADIO_S->EVENTS_PHYEND   = 0;

    NRF_RADIO_S->PACKETPTR = (uint32_t)pkt;
    /* 已过 T_IFS 触发点：放弃本次回复返回 0（宁缺毋晚），LL 计 miss，
     * 响应留在 pend 队列等对端重传时立即发出 */
    if ((int64_t)((rx_end_us + TURNAROUND_LEAD_US + TURNAROUND_SLACK_US) - time_now_us()) < 0) {
        radio_disable();
        return 0;
    }
    NRF_RADIO_S->EVENTS_READY = 0;
    while ((int64_t)((rx_end_us + TURNAROUND_LEAD_US) - time_now_us()) > 0) {
    }
    NRF_RADIO_S->TASKS_TXEN = 1;
    /* 时间戳在 TXEN 之后取：busy-wait 与 TXEN 之间不能插入任何代码
     * （一次 GRTC 读 ≈2-3us 就足以把 SCAN_RSP 推出安卓的接收窗口）。 */
    uint64_t t_txen = time_now_us();

    /* TIFS 抖动 + ramp 实测（TXEN 之后统计，不影响时序）：
     * late = busy-wait 退出相对目标时刻的迟到量；
     * ramp = TXEN→EVENTS_READY（发射机启动真值，LEAD 该配成 150-ramp）。 */
    {
        uint32_t late = (uint32_t)(t_txen - (rx_end_us + TURNAROUND_LEAD_US));
        if (late < g_tifs_late_min) g_tifs_late_min = late;
        if (late > g_tifs_late_max) g_tifs_late_max = late;
        while (NRF_RADIO_S->EVENTS_READY == 0) {
        }
        uint32_t ramp = (uint32_t)(time_now_us() - t_txen);
        if (ramp < g_ramp_min) g_ramp_min = ramp;
        if (ramp > g_ramp_max) g_ramp_max = ramp;
    }

    uint64_t tw = time_now_us();
    while (NRF_RADIO_S->EVENTS_DISABLED == 0) {
        if ((time_now_us() - tw) > TX_DONE_TIMEOUT_US) {
            radio_disable();
            return 0;
        }
    }
    return 1;
}

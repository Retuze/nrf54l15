#include "radio.h"
#include "time.h"
#include "nrf.h"

/*
 * nRF54L 无硬件 TIFS 回转，RX->TX 切换是软件定时：收到包结束后，等到
 * (T_IFS - TXEN ramp) 时刻发 TASKS_TXEN，回复恰好落在 RX 包尾后 T_IFS。
 * LEAD 按实测校准：rx_end->tx_end 间隙 ~230us（150 + 80）。见 01_conn
 * 原 conn_event 注释与 [conn] gap 统计。
 */
#define TURNAROUND_LEAD_US 98u

/* 发送等 DISABLED 的上限：251 字节 DLE 空中约 2.1ms */
#define TX_DONE_TIMEOUT_US 3000u

/* CRC 判定最多等这么久（PHYEND 之后硬件很快置位） */
#define CRC_WAIT_US 20u

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
    /* 若被 ISR 拖过 T_IFS 窗口（容忍 50us），回复已迟到——对端不认，
     * 放弃本次回复返回 0，让 LL 计 miss 走重传 */
    if ((int64_t)((rx_end_us + TURNAROUND_LEAD_US + 50u) - time_now_us()) < 0) {
        radio_disable();
        return 0;
    }
    while ((int64_t)((rx_end_us + TURNAROUND_LEAD_US) - time_now_us()) > 0) {
    }
    NRF_RADIO_S->TASKS_TXEN = 1;

    uint64_t tw = time_now_us();
    while (NRF_RADIO_S->EVENTS_DISABLED == 0) {
        if ((time_now_us() - tw) > TX_DONE_TIMEOUT_US) {
            radio_disable();
            return 0;
        }
    }
    return 1;
}

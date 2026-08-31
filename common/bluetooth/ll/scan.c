#include "scan.h"

/* 广播信道：RF 频率偏移（2400+n MHz）与白化信道号 */
static const uint8_t scan_freq[3] = { 2u, 26u, 80u };    /* 37/38/39 */
static const uint8_t scan_widx[3] = { 37u, 38u, 39u };

static uint8_t scan_rx_buf[260];

/* 从 AD 结构序列提取名字（0x09 完整名优先，0x08 短名替补）。 */
static void ad_extract_name(ll_scan_dev_t *d, const uint8_t *ad, uint32_t len)
{
    uint32_t i = 0;
    while (i + 2u <= len) {
        uint32_t l = ad[i];
        if (l == 0u || i + 1u + l > len) {
            return;                       /* 结构损坏/结束 */
        }
        uint8_t t = ad[i + 1u];
        if (t == 0x09u || (t == 0x08u && d->name_len == 0u)) {
            uint32_t n = l - 1u;
            if (n > LL_SCAN_NAME_MAX) n = LL_SCAN_NAME_MAX;
            for (uint32_t k = 0; k < n; k++) d->name[k] = (char)ad[i + 2u + k];
            d->name[n] = '\0';
            d->name_len = (uint8_t)n;
            if (t == 0x09u) return;       /* 完整名到手即止 */
        }
        i += 1u + l;
    }
}

int ll_scan_feed(ll_scan_stats_t *st, const uint8_t *pdu, uint32_t len)
{
    if (len < 8u) {
        return 0;                          /* 头 2 + AdvA 6 起步 */
    }
    uint8_t type = pdu[0] & 0x0Fu;
    uint8_t plen = pdu[1];
    if (2u + plen > len || plen < 6u) {
        return 0;
    }
    if (type > 6u) {
        return 0;                          /* 仅 legacy 广播 PDU */
    }
    const uint8_t *adva = &pdu[2];         /* 各 legacy 类型 AdvA 均在首 6 字节 */

    /* 设备表查找/登记（AdvA 去重） */
    ll_scan_dev_t *d = 0;
    for (uint32_t i = 0; i < st->n_dev; i++) {
        int same = 1;
        for (uint32_t k = 0; k < 6u; k++) {
            if (st->dev[i].addr[k] != adva[k]) { same = 0; break; }
        }
        if (same) { d = &st->dev[i]; break; }
    }
    if (!d) {
        if (st->n_dev >= LL_SCAN_DEVS) {
            st->dropped++;
            return 0;
        }
        d = &st->dev[st->n_dev++];
        for (uint32_t k = 0; k < 6u; k++) d->addr[k] = adva[k];
        d->name_len = 0;
        d->name[0] = '\0';
        d->count = 0;
    }
    d->count++;
    d->type = type;
    d->txadd = (pdu[0] >> 6) & 1u;

    /* 带 AD 载荷的类型（ADV_IND/NONCONN/SCAN_RSP/SCAN_IND）提取名字 */
    if ((type == 0u || type == 2u || type == 4u || type == 6u) && plen > 6u) {
        ad_extract_name(d, &pdu[8], (uint32_t)plen - 6u);
    }
    return 1;
}

void ll_scan_window(const ll_ops_t *ops, uint32_t ch_idx, uint32_t window_us,
                    ll_scan_stats_t *st)
{
    if (ch_idx > 2u) {
        return;
    }
    ops->radio_set_aa(0x8E89BED6u, 0x555555u);
    ops->radio_set_channel(scan_freq[ch_idx], scan_widx[ch_idx]);
    st->windows++;

    uint64_t t_end = ops->now_us() + window_us;
    for (;;) {
        uint64_t now = ops->now_us();
        if (now >= t_end) {
            break;
        }
        uint64_t t_addr = 0, t_pkt_end = 0;
        int crc_ok = 0;
        int got = ops->radio_rx(scan_rx_buf, sizeof(scan_rx_buf),
                                (uint32_t)(t_end - now), &t_addr, &t_pkt_end,
                                &crc_ok);
        if (!got) {
            break;                         /* 窗口耗尽（radio 已停） */
        }
        if (crc_ok) {
            st->rx_ok++;
            ll_scan_feed(st, scan_rx_buf, sizeof(scan_rx_buf));
        } else {
            st->rx_err++;
        }
    }
    ops->radio_disable();
}

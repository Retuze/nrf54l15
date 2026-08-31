/*
 * ll_scan — 被动扫描状态机（07_adv_scan 起用）。
 *
 * 纯协议（common 规则）：radio/时间经 ll_ops_t 注入；PDU 解析拆成
 * ll_scan_feed()（纯函数），宿主可单测（tests/sched_test.c 里一并测）。
 *
 * 被动扫描 = 只听不发（不发 SCAN_REQ）：开窗收 legacy 广播 PDU，按
 * AdvA 去重进设备表，从 AD 结构提取名字。RSSI 暂缺（radio 驱动尚未
 * 出样，README 路线图预留）。
 */
#ifndef LL_SCAN_H
#define LL_SCAN_H

#include <stdint.h>
#include "ll.h"

#define LL_SCAN_DEVS 16u
#define LL_SCAN_NAME_MAX 15u

typedef struct {
    uint8_t  addr[6];
    uint8_t  txadd;                /* 1 = 随机地址 */
    uint8_t  type;                 /* 最近一次的 PDU type（0..6） */
    uint8_t  name_len;             /* 0 = 未见到名字 AD */
    char     name[LL_SCAN_NAME_MAX + 1];
    uint32_t count;                /* 收到的包数 */
} ll_scan_dev_t;

typedef struct {
    uint32_t windows;              /* 扫描窗计数 */
    uint32_t rx_ok, rx_err;        /* CRC 好/坏包 */
    uint32_t dropped;              /* 设备表满丢弃的新地址 */
    uint32_t n_dev;
    ll_scan_dev_t dev[LL_SCAN_DEVS];
} ll_scan_stats_t;

/* 解析一条 CRC-OK 的广播信道 PDU 进设备表（纯函数，host 可测）。
 * 返回 1 = 记入（新设备或计数更新），0 = 非法/不关心的 PDU。 */
int ll_scan_feed(ll_scan_stats_t *st, const uint8_t *pdu, uint32_t len);

/* 开一个被动扫描窗：切到广播信道 ch_idx（37/38/39），持续收包直到
 * window_us 耗尽。radio AA/CRC 由本函数设置（广播信道参数），返回前
 * radio_disable——调用方（广播路径）每次 sweep 自会重设自己的参数。 */
void ll_scan_window(const ll_ops_t *ops, uint32_t ch_idx, uint32_t window_us,
                    ll_scan_stats_t *st);

#endif /* LL_SCAN_H */

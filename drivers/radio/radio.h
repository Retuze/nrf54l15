/*
 * RADIO driver for nRF54L15, bare metal, BLE 1Mbit.
 *
 * 物理时序封装在本驱动：nRF54L 没有硬件 TIFS 回转，T_IFS 软件时序
 * （TURNAROUND_LEAD_US 校准值）在 radio.c 内，协议层（common/ll）不出现
 * 任何时序常量。窗口超时与回复定时依赖 time 驱动。
 *
 * 无包缓冲：PACKETPTR 直接指调用方缓冲（必须在 RAM）。
 * 已知限制（沿袭自 01_conn 原实现）：PHYEND 等待无超时——本应收到包却
 * 一直等（如对端中途断电）会忙等，与重构前行为一致。
 */
#ifndef RADIO_H
#define RADIO_H

#include <stdint.h>

/* MODE/TXPOWER/PCNF/CRC/地址配置。hfXo 启动之后、任何收发之前调用。 */
void radio_init(void);

/* Access address & CRC init（advertising 与 connection 两套值由调用方切换）。 */
void radio_set_aa(uint32_t aa, uint32_t crcinit);

/* 信道：RF 频率偏移 + 白化信道号（DATAWHITE 的低位）。 */
void radio_set_channel(uint32_t freq_off, uint32_t white_ch);

/* 发送一包并等 DISABLED。timeout_us == 0 = 无超时（PHYEND_DISABLE 短接
 * 保证正常路径必然结束）；超时则内部 disable 并返回 0。 */
int radio_tx(const uint8_t *pkt, uint32_t len, uint32_t timeout_us);

/* 开窗接收。返回 1 = 收到包（含 CRC 错，*crc_ok 指示）；0 = 窗口超时
 * （内部已 disable）。时间戳来自 time 驱动：t_addr = ADDRESS 事件时刻，
 * t_end = PHYEND 时刻（RX 包尾）。CRC 判定最多等 20us。 */
int radio_rx(uint8_t *pkt, uint32_t maxlen, uint32_t window_us,
             uint64_t *t_addr_us, uint64_t *t_end_us, int *crc_ok);

/* 软件 T_IFS 回复：等 RX 通道 DISABLED → 装包 → 忙等到 (rx_end + LEAD) →
 * TXEN → 等 DISABLED（3ms 上限，容 251 字节 DLE ≈ 2.1ms）。
 * 返回 1 = 发送完成（radio 已停），0 = 超时（内部已 disable）。 */
int radio_reply_at(const uint8_t *pkt, uint32_t len, uint64_t rx_end_us);

/* 关 radio（清 SHORTS → TASKS_DISABLE → 等 DISABLED）。 */
void radio_disable(void);

/* TIFS 软件定时诊断：late = busy-wait 退出迟到量 min/max（µs），
 * ramp = TXEN→EVENTS_READY 实测 min/max（µs）。读取后复位。 */
void radio_dbg_tifs(uint32_t *late_min, uint32_t *late_max,
                    uint32_t *ramp_min, uint32_t *ramp_max);

#endif /* RADIO_H */

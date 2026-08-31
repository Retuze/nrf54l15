/*
 * RADIO driver for nRF54L15, bare metal, BLE 1Mbit.
 *
 * 物理时序封装在本驱动：nRF54L 没有硬件 TIFS 自动回转，本驱动用
 * DPPI+TIMER10 搭了一套（PHYEND→清零重计,COMPARE→TXEN,±62.5ns 抖动;
 * 触发点常量 TIFS_CC_US 在 radio.c,推导见其注释与 README"RF 三大必修
 * 课"）。协议层（common/ll）不出现任何时序常量。
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

/* 硬件 T_IFS 回复：等 RX 通道 DISABLED → 装包 → 装 DPPI 订阅（TXEN 由
 * TIMER10 比较在精确时刻触发）→ 等 DISABLED（3ms 上限，容 251 字节 DLE
 * ≈ 2.1ms）。构建超过触发点则放弃返回 0（宁缺毋晚）。
 * 返回 1 = 发送完成（radio 已停），0 = 放弃/超时（内部已 disable）。 */
int radio_reply_at(const uint8_t *pkt, uint32_t len, uint64_t rx_end_us);

/* 关 radio（清 SHORTS → TASKS_DISABLE → 等 DISABLED）。 */
void radio_disable(void);

/* 硬件 T_IFS 触发点调整（µs,PHYEND→TXEN;空口 = 该值 + TXEN ramp ~41us）。
 * 标定/扫掠用。 */
void radio_tifs_set_cc(uint32_t us);

/* TIFS 软件定时诊断：late = 装订阅时刻 min/max（µs,构建耗时观测），
 * ramp = rx_end→tx_end 全程 min/max（µs）。读取后复位。 */
void radio_dbg_tifs(uint32_t *late_min, uint32_t *late_max,
                    uint32_t *ramp_min, uint32_t *ramp_max);

/* ==== 异步（IRQ 驱动）API —— 04_async_ll 用 ====
 * is_rx=1：RX 结束（t_addr/t_end/crc_ok 有效）；is_rx=0：TX 发送完成。
 * 回调在 RADIO IRQ 上下文。radio_irq_init 只注册回调 + NVIC 使能（中断源
 * 由 radio_rx_arm 打开、radio_disable/TX 完成关闭——与同步轮询 API 互斥
 * 使用：同一时刻只允许一种风格在收发）。 */
typedef void (*radio_evt_cb_t)(int is_rx, uint64_t t_addr_us, uint64_t t_end_us,
                               int crc_ok);
void radio_irq_init(radio_evt_cb_t cb);

/* 装 RX 并立即返回：RXEN + ADDRESS/PHYEND/DISABLED 中断。包尾（PHYEND→
 * DISABLED 短接）触发回调 cb(is_rx=1, ...)。窗口超时由调用方闹钟负责，
 * 超时路径调 radio_disable() 撤收。 */
void radio_rx_arm(uint8_t *pkt, uint32_t maxlen);

/* 硬件 T_IFS 定时回复（非阻塞版 radio_reply_at）：装包 + 装 DPPI 订阅后
 * 立即返回 1（TXEN 由硬件到点触发）；发送完成经 IRQ 回调 cb(is_rx=0)。
 * 构建已超触发点则返回 0（radio 已撤收，不发迟到包）。须在 RX 回调
 * 上下文内调用。 */
int radio_reply_arm(const uint8_t *pkt, uint32_t len, uint64_t rx_end_us);

#endif /* RADIO_H */

/*
 * Clock driver for nRF54L15, bare metal.
 *
 * 目前只有 HFXO 启动（BLE 射频需要 64 MHz 晶振基准）；
 * LFRC/LFCLK 选择由 time 驱动（GRTC）自理，不经过这里。
 */
#ifndef CLOCK_H
#define CLOCK_H

/* Start the HFXO and busy-wait until XOSTARTED（2s 超时），随后 XOTUNE
 * 调谐到标称频率。Call once, before radio。
 * 0 = 起振+调谐成功；-1 = 起振超时；-2 = 调谐超时；-3 = 调谐失败。 */
int clock_hfxo_start(void);

/* HFXO 重调谐：XOTUNE 是一次性校准,芯片温度漂移后载波会偏移
 * （实板表现:长 PDU 相位漂出接收机容差,安卓收不到 SCAN_RSP/通知,
 * 短包反而能收）。空闲期（如广播循环）周期性调用;检测到
 * XOTUNEERROR 事件时也应调用。返回值同 clock_hfxo_start 的调谐段。 */
int clock_hfxo_retune(void);

/* XOTUNEERROR 事件检查（读取并清除）：1 = 硬件报告需要重新调谐 */
int clock_hfxo_tune_error(void);

#endif /* CLOCK_H */

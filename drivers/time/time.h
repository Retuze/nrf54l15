/*
 * Platform time abstraction for nRF54L15, bare metal.
 *
 * 底层实现是 GRTC（Global Real-time Counter）：其 SYSCOUNTER 是 52 位
 * 递增计数器，nRF54L15 上 1 MHz（1 count == 1 us，实测验证，见
 * scripts/grtc_rate.py）。读取用 L→H→OVERFLOW 重读协议（跨 2^32 边界
 * 防撕裂，见 time.c）；按域选择 SYSCOUNTER[2] 视图（app secure）。
 *
 * 接口层不出现 GRTC 外设名：调用方只关心"单调微秒时间"。
 */

#ifndef TIME_H
#define TIME_H

#include <stdint.h>

/* Start the SYSCOUNTER. Call once after reset (HFXO not required). */
void time_init(void);

/* Current 52-bit microsecond timestamp. Monotonic, wraps after ~142 years. */
uint64_t time_now_us(void);

/* Busy-wait for the given number of microseconds. */
void time_delay_us(uint32_t us);

/* ---- 闹钟（GRTC CC 通道 + IRQ）----
 * 一次性：触发即自动取消（回调里可以重新 set）。
 * 回调在 GRTC_2 IRQ 上下文执行：必须短小、禁 printf/长忙等、不重入本驱动。
 * 目标时刻已过期 → 立即同步回调（不在 IRQ 上下文）。ch ∈ {0, 1}。 */
typedef void (*time_alarm_cb_t)(uint32_t ch);

void time_alarm_set(uint32_t ch, uint64_t t_us, time_alarm_cb_t cb);
void time_alarm_cancel(uint32_t ch);

#endif /* TIME_H */

/*
 * console.h — 控制台：picolibc stdio 的落点 + 默认 log sink（独立模块）。
 *
 * 职责边界（2026-08-30 起）：
 *   - uart 模块 = 纯芯片驱动（字节进出）；本模块 = stdio/日志的装配层
 *   - console_init(port, cfg)：自开一个 uart 实例并接管 stdout 落点
 *     （强 write = uart_tx 入队 + uart_tx_wait 排空——printf 的阻塞语义
 *     在控制台层组合出来，驱动层只有非阻塞原语）
 *   - console_tx_abort()：HardFault 现场打印前归零 TX（STOP + 丢 ring）
 *   - console_log_init()：log 默认 sink 装配到控制台（uart_tx 即 sink，
 *     驱动内 ring 兜底——无需再挂外部 ring）
 *
 * 打印策略：printf 只归 HardFault 现场打印，日常日志统一走 common/log。
 * console_init 之前用 printf：uart_tx 的 inited 守卫静默丢弃，不会挂起。
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include "uart.h"   /* uart_port_t / uart_cfg_t */

void console_init(uart_port_t port, const uart_cfg_t *cfg);
void console_flush(void);        /* 排空 TX（= uart_tx_wait） */
void console_tx_abort(void);     /* HardFault 路径：STOP 在途 + 丢 ring */
void console_log_init(void);     /* log sink 装配（console_init 之后调一次） */

#endif /* CONSOLE_H */

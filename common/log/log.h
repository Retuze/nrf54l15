/*
 * log.h — 实时日志（平台无关，common 规则；底层传输经 log_sink 注入）。
 *
 * 背景：连接态是硬实时忙等状态机（T_IFS 150us / 事件窗口），printf 的
 * flush 是阻塞 uart_write（115200 = 87us/字符），连接循环里不能碰。
 * 本模块只做"格式化 + 注入"，微秒级返回：
 *   - 目标机：sink = uart_tx 适配器（驱动内 ring + TX END 中断后台排空）
 *   - 宿主测试：sink = fake 记录器（tests/log_test.c）
 *   - 缓冲策略归 sink 侧（本模块不持 ring），满则 sink 拒收、这里计丢弃
 *
 * 打印策略（2026-08-30 起）：printf 只归 HardFault 现场打印，日常日志
 * 统一走本模块。目标机装配用 uart_console_log_init()（uart.h）。
 * 插桩点仍要放在事件收尾（reply 之后），vsnprintf 格式化本身也要时间。
 */
#ifndef LOG_H
#define LOG_H

#include <stdint.h>
#include <stddef.h>

/* 非阻塞字节管道：送入底层传输立即返回，返回实际接受的字节数（0..len）。
 * 不足部分由 log 计为丢弃（log_dropped 可观测）。 */
typedef struct log_sink {
    size_t (*put)(const uint8_t *buf, size_t len, void *arg);
    void *arg;
} log_sink_t;

void log_init(const log_sink_t *sink);   /* NULL = 静默丢弃（未接线/测试） */

void log_puts(const char *s);            /* 入管道即返 */
void log_printf(const char *fmt, ...);   /* 栈缓冲格式化后入管道，即返 */

uint32_t log_dropped(void);              /* 累计被 sink 拒收的字节数 */

#endif /* LOG_H */

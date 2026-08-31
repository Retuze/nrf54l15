#include <stddef.h>
#include <sys/types.h>   /* ssize_t */
#include "console.h"
#include "log.h"

static uart_hw_t *s_console;

/* picolibc posix-console：库内弱 stdout（512B 行缓冲）换行 flush →
 * write(1) → 本强实现。阻塞语义 = 入队 + 主动排空（uart_tx_wait 自己踢
 * DMA，不依赖 ISR——HardFault 上下文里同样能走完）。 */
ssize_t write(int fd, const void *buf, size_t len)
{
    (void)fd;
    if (s_console != 0) {
        uart_tx(s_console, (const uint8_t *)buf, len);
        uart_tx_wait(s_console);
    }
    return (ssize_t)len;
}

void console_init(uart_port_t port, const uart_cfg_t *cfg)
{
    s_console = uart_init(port, cfg);
}

void console_flush(void)
{
    if (s_console != 0) {
        uart_tx_wait(s_console);
    }
}

void console_tx_abort(void)
{
    if (s_console != 0) {
        uart_tx_abort(s_console);
    }
}

/* ---- 默认 log sink：uart_tx 即 sink（驱动内 ring 兜底），
 * 返回实际接受字节数，不足部分由 log 计丢弃。 ---- */
static size_t console_log_put(const uint8_t *buf, size_t len, void *arg)
{
    (void)arg;
    if (s_console == 0) {
        return 0;
    }
    return uart_tx(s_console, buf, len);
}
/* 文件级静态 sink：函数内复合字面量随栈帧销毁（tests/log_test 踩过） */
static const log_sink_t console_log_sink = { .put = console_log_put, .arg = 0 };

void console_log_init(void)
{
    log_init(&console_log_sink);
}

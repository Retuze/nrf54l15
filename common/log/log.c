#include <stdarg.h>
#include <stdio.h>    /* vsnprintf：只做格式化（目标机 picolibc / 宿主 libc） */
#include <string.h>
#include "log.h"

#define LOG_FMT_BUF 128u   /* 单条日志的格式化缓冲（超出截断） */

static const log_sink_t *log_sink;
static uint32_t log_dropped_bytes;

void log_init(const log_sink_t *sink)
{
    log_sink = sink;
    log_dropped_bytes = 0;
}

static void log_push(const uint8_t *buf, size_t len)
{
    if (log_sink == 0 || log_sink->put == 0 || len == 0) {
        return;
    }
    size_t accepted = log_sink->put(buf, len, log_sink->arg);
    if (accepted < len) {
        log_dropped_bytes += (uint32_t)(len - accepted);
    }
}

void log_puts(const char *s)
{
    log_push((const uint8_t *)s, strlen(s));
}

void log_printf(const char *fmt, ...)
{
    char buf[LOG_FMT_BUF];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    if ((size_t)n > sizeof(buf) - 1u) {
        n = (int)sizeof(buf) - 1;
    }
    log_push((const uint8_t *)buf, (size_t)n);
}

uint32_t log_dropped(void)
{
    return log_dropped_bytes;
}

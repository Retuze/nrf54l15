/*
 * log_test.c — 实时日志模块宿主测试（fake sink 注入）。
 *
 * 覆盖：puts/printf 字节正确性、格式化（%u/%s/%x）、sink 拒收的丢弃计数、
 * 未接线（NULL sink）静默、超长格式化截断。
 */
#include <string.h>
#include "tf.h"
#include "log.h"

static uint8_t sink_buf[512];
static uint32_t sink_len;
static size_t   sink_cap;          /* 本次测试的接受上限（模拟 ring 满） */
static log_sink_t sink;            /* 静态存活期（复合字面量在 fake_reset
                                      返回时就死了，会悬垂） */

static size_t fake_put(const uint8_t *buf, size_t len, void *arg)
{
    (void)arg;
    size_t take = len;
    if (take > sink_cap) take = sink_cap;
    for (size_t i = 0; i < take; i++) {
        sink_buf[sink_len + i] = buf[i];
    }
    sink_len += (uint32_t)take;
    return take;
}

static void fake_reset(size_t cap)
{
    sink_len = 0;
    sink_cap = cap;
    memset(sink_buf, 0, sizeof(sink_buf));
    sink.put = fake_put;
    sink.arg = 0;
    log_init(&sink);
}

static void test_puts(void)
{
    fake_reset(512);
    log_puts("hello ");
    log_puts("conn");
    CHECK_EQ(sink_len, 10u);
    CHECK(memcmp(sink_buf, "hello conn", 10) == 0);
    CHECK_EQ(log_dropped(), 0u);
}

static void test_printf_fmt(void)
{
    fake_reset(512);
    log_printf("evt=%u ok=%d ch=%u %s\n", 42u, 1, 7u, "on");
    CHECK(memcmp(sink_buf, "evt=42 ok=1 ch=7 on\n", 19) == 0);
}

static void test_drop_count(void)
{
    /* sink 每次只收 8 字节：40 字节被拒 32 */
    fake_reset(8);
    log_puts("0123456789abcdefghijklmnopqrstuvwxyzABCD");
    CHECK_EQ(sink_len, 8u);
    CHECK_EQ(log_dropped(), 32u);
}

static void test_no_sink(void)
{
    log_init(0);
    log_puts("silent");
    log_printf("x=%u", 1u);
    CHECK_EQ(log_dropped(), 0u);     /* 未接线：静默，不计丢弃 */
}

static void test_truncate_fmt(void)
{
    /* 格式化缓冲 128：长串截断到 127，不越界 */
    fake_reset(512);
    log_printf("%s", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                     "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                     "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    CHECK_EQ(sink_len, 127u);
    CHECK_EQ(log_dropped(), 0u);
}

int main(void)
{
    test_puts();
    test_printf_fmt();
    test_drop_count();
    test_no_sink();
    test_truncate_fmt();
    TF_END();
}

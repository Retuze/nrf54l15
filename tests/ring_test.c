/*
 * ring_test.c — SPSC 字节环形缓冲宿主测试。
 *
 * 覆盖：FIFO 顺序、满/空边界、回绕（跨 2^32 模拟）、容量 1 退化、
 * SPSC 交错（生产消费交替）、count/free。
 */
#include "tf.h"
#include "ring.h"

static uint8_t buf[64];
static ring_t r;

static void test_fifo_order(void)
{
    ring_init(&r, buf, 64);
    CHECK_EQ(ring_count(&r), 0u);
    CHECK_EQ(ring_free(&r), 64u);
    for (int i = 0; i < 40; i++) CHECK_EQ(ring_put(&r, (uint8_t)i), 0);
    CHECK_EQ(ring_count(&r), 40u);
    CHECK_EQ(ring_free(&r), 24u);
    for (int i = 0; i < 40; i++) CHECK_EQ(ring_get(&r), i);
    CHECK_EQ(ring_count(&r), 0u);
    CHECK_EQ(ring_get(&r), -1);              /* 空 */
}

static void test_full_guard(void)
{
    ring_init(&r, buf, 8);
    for (int i = 0; i < 8; i++) CHECK_EQ(ring_put(&r, (uint8_t)i), 0);
    CHECK_EQ(ring_put(&r, 99), -1);          /* 满 → 拒绝 */
    CHECK_EQ(ring_count(&r), 8u);
    CHECK_EQ(ring_free(&r), 0u);
    for (int i = 0; i < 8; i++) CHECK_EQ(ring_get(&r), i);
}

static void test_wrap_index(void)
{
    /* 容量 4：写入/读取绕 mask 一圈以上 */
    ring_init(&r, buf, 4);
    for (int i = 0; i < 10; i++) {           /* 2 轮半 */
        CHECK_EQ(ring_put(&r, (uint8_t)i), 0);
        CHECK_EQ(ring_get(&r), i);
    }
    CHECK_EQ(ring_count(&r), 0u);
}

static void test_size1(void)
{
    uint8_t b1;
    ring_init(&r, &b1, 1);
    CHECK_EQ(ring_put(&r, 0xAA), 0);
    CHECK_EQ(ring_put(&r, 0xBB), -1);        /* 满 */
    CHECK_EQ(ring_count(&r), 1u);
    CHECK_EQ(ring_get(&r), 0xAA);
    CHECK_EQ(ring_get(&r), -1);              /* 空 */
    CHECK_EQ(ring_put(&r, 0xCC), 0);
    CHECK_EQ(ring_get(&r), 0xCC);
}

static void test_wrap_headtail(void)
{
    /* head/tail 从接近 2^32 处回绕 */
    ring_init(&r, buf, 8);
    r.head = 0xFFFFFFFCu;                    /* 距回绕 4 */
    r.tail = 0xFFFFFFFCu;
    for (int i = 0; i < 12; i++) {
        CHECK_EQ(ring_put(&r, (uint8_t)i), 0);
        CHECK_EQ(ring_get(&r), i);
    }
    CHECK_EQ(ring_count(&r), 0u);
}

static void test_spsc_interleave(void)
{
    /* 生产者 3 个、消费者 2 个交替（模拟 ISR/主循环） */
    ring_init(&r, buf, 8);
    CHECK_EQ(ring_put(&r, 1), 0);
    CHECK_EQ(ring_put(&r, 2), 0);
    CHECK_EQ(ring_put(&r, 3), 0);
    CHECK_EQ(ring_get(&r), 1);
    CHECK_EQ(ring_get(&r), 2);
    CHECK_EQ(ring_put(&r, 4), 0);
    CHECK_EQ(ring_put(&r, 5), 0);
    CHECK_EQ(ring_get(&r), 3);
    CHECK_EQ(ring_get(&r), 4);
    CHECK_EQ(ring_get(&r), 5);
    CHECK_EQ(ring_get(&r), -1);
}

int main(void)
{
    test_fifo_order();
    test_full_guard();
    test_wrap_index();
    test_size1();
    test_wrap_headtail();
    test_spsc_interleave();
    TF_END();
}

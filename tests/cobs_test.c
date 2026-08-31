/*
 * cobs_test.c — COBS 编解码宿主测试。
 *
 * 覆盖：标准向量、编码往返（含 0x00/全非零/254 边界/长帧）、逐字节增量
 * 解码 == 整包解码、噪声前缀后重同步、无定界不完成、cap 溢出。
 */
#include <string.h>
#include "tf.h"
#include "cobs.h"

static uint8_t enc[1200];
static uint8_t dec[1200];
static cobs_dec_t d;

static void check_vector(const uint8_t *src, uint32_t len, const uint8_t *want, uint32_t want_len)
{
    uint32_t n = cobs_encode(src, len, enc);
    CHECK_EQ(n, want_len);
    for (uint32_t i = 0; i < want_len; i++) {
        if (enc[i] != want[i]) {
            TF_FAIL("vec enc[%u]=0x%02x want 0x%02x", i, enc[i], want[i]);
            break;
        }
    }
}

static void check_roundtrip(const uint8_t *src, uint32_t len)
{
    uint32_t n = cobs_encode(src, len, enc);
    /* 逐字节增量解码 */
    cobs_dec_reset(&d);
    int rc = 0;
    uint32_t got = 0;
    for (uint32_t i = 0; i < n; i++) {
        rc = cobs_dec_feed(&d, enc[i], dec, sizeof(dec));
        if (rc > 0) { got = (uint32_t)rc; break; }
        if (rc < 0) break;
    }
    CHECK_EQ(rc > 0 ? (int)got : rc, (int)len);
    if ((uint32_t)rc == len) {
        for (uint32_t i = 0; i < len; i++) {
            if (dec[i] != src[i]) {
                TF_FAIL("rt[%u]=0x%02x want 0x%02x", i, dec[i], src[i]);
                break;
            }
        }
    }
}

static void test_vectors(void)
{
    /* 经典 COBS 例子 */
    { const uint8_t s[] = { 0x00 };
      const uint8_t w[] = { 0x01, 0x01, 0x00 };
      check_vector(s, 1, w, 3); }
    { const uint8_t s[] = { 0x11, 0x22, 0x00, 0x33 };
      const uint8_t w[] = { 0x03, 0x11, 0x22, 0x02, 0x33, 0x00 };
      check_vector(s, 4, w, 6); }
    { const uint8_t s[] = { 0x11, 0x22, 0x33, 0x44 };
      const uint8_t w[] = { 0x05, 0x11, 0x22, 0x33, 0x44, 0x00 };
      check_vector(s, 4, w, 6); }
    /* 254 个非零：code=0xFF 无隐零，块后还需收尾 code=0x01 + 定界 */
    {
        uint8_t s[254], w[257];
        for (int i = 0; i < 254; i++) s[i] = 0x11;
        w[0] = 0xFF;
        for (int i = 0; i < 254; i++) w[1 + i] = 0x11;
        w[255] = 0x01;
        w[256] = 0x00;
        check_vector(s, 254, w, 257);
    }
    /* 255 个非零：两个 code 字节（254 + 尾 code） */
    {
        uint8_t s[255];
        for (int i = 0; i < 255; i++) s[i] = 0x11;
        uint32_t n = cobs_encode(s, 255, enc);
        CHECK_EQ(n, 258u);
        CHECK_EQ(enc[0], 0xFFu);
        CHECK_EQ(enc[255], 0x02u);   /* 254 数据后 code=2：1 数据 + 隐零 */
        CHECK_EQ(enc[256], 0x11u);
        CHECK_EQ(enc[257], 0x00u);
    }
}

static void test_roundtrips(void)
{
    uint8_t s[1000];

    /* 全零 / 全非零 / 交替 / 随机 */
    for (int i = 0; i < 300; i++) s[i] = 0x00;
    check_roundtrip(s, 300);
    for (int i = 0; i < 300; i++) s[i] = 0x42;
    check_roundtrip(s, 300);
    for (int i = 0; i < 300; i++) s[i] = (i % 2) ? 0x00 : (uint8_t)i;
    check_roundtrip(s, 300);
    uint32_t x = 0x12345678u;
    for (int i = 0; i < 1000; i++) {
        x = x * 1664525u + 1013904223u;
        s[i] = (uint8_t)(x >> 24);
    }
    check_roundtrip(s, 1000);
    /* 空输入 */
    { const uint8_t w[] = { 0x01, 0x00 };
      check_vector(s, 0, w, 2); }
    /* 254 边界往返（253/254/255 长度） */
    for (int i = 0; i < 255; i++) s[i] = 0x33;
    check_roundtrip(s, 253);
    check_roundtrip(s, 254);
    check_roundtrip(s, 255);
}

static void test_noise_resync(void)
{
    /* COBS 只有帧尾定界：噪声后先发一个 0x00（空帧）清空对端状态，
     * 再发有效帧——解码器应完整恢复 */
    uint8_t s[8] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    uint32_t n = cobs_encode(s, 8, enc);
    cobs_dec_reset(&d);
    int rc = 0;
    /* 先喂垃圾（无定界，会污染当前帧） */
    for (int i = 0; i < 5; i++) {
        rc = cobs_dec_feed(&d, (uint8_t)(0x20 + i), dec, sizeof(dec));
        CHECK_EQ(rc, 0);
    }
    /* 定界符清状态（噪声攒成的残帧会作为一帧被完成，忽略其内容即可；
     * 发送侧约定：噪声后先发空帧 0x00 清空对端） */
    (void)cobs_dec_feed(&d, 0, dec, sizeof(dec));
    /* 再喂有效帧 */
    for (uint32_t i = 0; i < n; i++) {
        rc = cobs_dec_feed(&d, enc[i], dec, sizeof(dec));
        if (rc > 0) break;
    }
    CHECK_EQ(rc, 8);
    CHECK(memcmp(dec, s, 8) == 0);
}

static void test_no_delim_no_complete(void)
{
    uint8_t s[4] = { 0x01, 0x02, 0x03, 0x04 };
    uint32_t n = cobs_encode(s, 4, enc);   /* 编码后 6 字节含尾 0x00 */
    cobs_dec_reset(&d);
    int rc = 0;
    for (uint32_t i = 0; i < n - 1; i++) { /* 不喂尾定界 */
        rc = cobs_dec_feed(&d, enc[i], dec, sizeof(dec));
        CHECK_EQ(rc, 0);
    }
}

static void test_cap_overflow(void)
{
    uint8_t s[32];
    for (int i = 0; i < 32; i++) s[i] = 0x42;
    uint32_t n = cobs_encode(s, 32, enc);
    cobs_dec_reset(&d);
    int rc = 0;
    for (uint32_t i = 0; i < n; i++) {
        rc = cobs_dec_feed(&d, enc[i], dec, 8);   /* cap 只有 8 */
        if (rc != 0) break;
    }
    CHECK_EQ(rc, -1);   /* 溢出 */
}

int main(void)
{
    test_vectors();
    test_roundtrips();
    test_noise_resync();
    test_no_delim_no_complete();
    test_cap_overflow();
    TF_END();
}

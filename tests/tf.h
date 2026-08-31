/*
 * tf.h — 极简宿主测试框架（零依赖，~60 行）。
 *
 * 每个测试文件一个 main()：用 CHECK/CHECK_EQ 断言，最后 TF_END() 汇总。
 * 失败计数非 0 → 返回 1（scripts/run_tests.sh 据此判失败）。
 * 约定：一个 .c 一个测试二进制；tf.h 的计数是每文件 static。
 */
#ifndef TF_H
#define TF_H

#include <stdio.h>

static int tf_n_checks;
static int tf_n_fails;

#define TF_FAIL(fmt, ...) \
    do { printf("  FAIL %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
         tf_n_fails++; } while (0)

#define CHECK(cond) \
    do { tf_n_checks++; if (!(cond)) TF_FAIL("%s", #cond); } while (0)

#define CHECK_EQ(a, b) \
    do { \
        tf_n_checks++; \
        long long _a_ = (long long)(a), _b_ = (long long)(b); \
        if (_a_ != _b_) TF_FAIL("%s == %s  (%lld != %lld)", #a, #b, _a_, _b_); \
    } while (0)

#define TF_END() \
    do { \
        printf("%d checks, %d failed\n", tf_n_checks, tf_n_fails); \
        return tf_n_fails ? 1 : 0; \
    } while (0)

#endif /* TF_H */

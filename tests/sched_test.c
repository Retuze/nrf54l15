/*
 * sched_test.c — 调度器雏形 + 被动扫描解析的宿主单测。
 *
 * sched：纯逻辑（时刻全传入），直接驱动断言选择顺序/让步计数/相位保持。
 * scan：ll_scan_feed 纯函数，喂手工构造的广播 PDU 验证去重/名字提取。
 */
#include <string.h>
#include "tf.h"
#include "sched.h"
#include "scan.h"

/* ------------------------------------------------ sched -- */
static void test_sched_order(void)
{
    ll_sched_init();
    /* adv：prio1，t=1000 起，每 60ms，占 3ms；scan：prio2，t=2000 起，
     * 每 100ms，占 30ms */
    int adv = ll_sched_add(1, 1000, 3000, 60000);
    int scan = ll_sched_add(2, 2000, 30000, 100000);
    CHECK(adv >= 0 && scan >= 0);

    /* t=1000 的 adv 窗 [1000,4000) 与 scan 窗 [2000,32000) 重叠 →
     * scan 让步（+100ms → 102000），先选 adv */
    int s = ll_sched_pick(0);
    CHECK_EQ(s, adv);
    CHECK_EQ(ll_sched_slot(scan)->yields, 1u);
    CHECK_EQ((uint32_t)ll_sched_slot(scan)->t_start, 102000u);
    ll_sched_done(adv, 4000);
    CHECK_EQ((uint32_t)ll_sched_slot(adv)->t_start, 61000u);
    CHECK_EQ(ll_sched_slot(adv)->runs, 1u);

    /* 下一轮：adv@61000 vs scan@102000 → adv 不重叠 → adv */
    s = ll_sched_pick(4000);
    CHECK_EQ(s, adv);
    ll_sched_done(adv, 64000);

    /* adv@121000 窗 [121000,124000) 与 scan@102000 窗 [102000,132000)
     * 重叠 → adv 优先级高不让,scan 先到期先选？——选择规则是"最早开始
     * 者优先,低优先级让步":最早者是 scan(102000),与更高优 adv 重叠 →
     * scan 让步(+100ms→202000) → 重选 adv */
    s = ll_sched_pick(64000);
    CHECK_EQ(s, adv);
    CHECK_EQ(ll_sched_slot(scan)->yields, 2u);
    ll_sched_done(adv, 124000);

    /* adv@181000 vs scan@202000：不重叠（181k+3k < 202k）→ adv 先，
     * 完成后 scan 得到干净窗口 */
    s = ll_sched_pick(124000);
    CHECK_EQ(s, adv);
    ll_sched_done(adv, 184000);
    s = ll_sched_pick(184000);
    CHECK_EQ(s, scan);
    ll_sched_done(scan, 232000);
    CHECK_EQ(ll_sched_slot(scan)->runs, 1u);
    CHECK_EQ((uint32_t)ll_sched_slot(scan)->t_start, 302000u);
}

static void test_sched_phase_catchup(void)
{
    ll_sched_init();
    int a = ll_sched_add(1, 1000, 1000, 10000);
    /* done 时已落后 3 个周期：t_start 应按周期追到 now 之后，保持相位 */
    ll_sched_done(a, 35000);
    CHECK_EQ((uint32_t)ll_sched_slot(a)->t_start, 41000u);
    CHECK_EQ(ll_sched_slot(a)->runs, 1u);
}

static void test_sched_empty_and_full(void)
{
    ll_sched_init();
    CHECK_EQ(ll_sched_pick(0), -1);
    for (uint32_t i = 0; i < LL_SCHED_SLOTS; i++) {
        CHECK(ll_sched_add(1, 0, 100, 1000) >= 0);
    }
    CHECK_EQ(ll_sched_add(1, 0, 100, 1000), -1);
}

/* ------------------------------------------------ scan feed -- */
static uint32_t mk_adv(uint8_t *p, uint8_t type, uint8_t txadd,
                       const uint8_t addr[6], const char *name, uint8_t name_ad)
{
    uint32_t i = 2;
    for (uint32_t k = 0; k < 6; k++) p[i++] = addr[k];
    if (name) {
        uint32_t n = (uint32_t)strlen(name);
        p[i++] = (uint8_t)(1 + n);
        p[i++] = name_ad;                  /* 0x09 完整名 / 0x08 短名 */
        for (uint32_t k = 0; k < n; k++) p[i++] = (uint8_t)name[k];
    }
    p[0] = (uint8_t)(type | (txadd ? 0x40u : 0u));
    p[1] = (uint8_t)(i - 2);
    return i;
}

static void test_scan_feed(void)
{
    ll_scan_stats_t st;
    memset(&st, 0, sizeof st);
    uint8_t pdu[64];
    static const uint8_t a1[6] = { 1, 2, 3, 4, 5, 6 };
    static const uint8_t a2[6] = { 9, 9, 9, 9, 9, 9 };

    /* ADV_IND 带完整名 */
    uint32_t n = mk_adv(pdu, 0, 1, a1, "ABC", 0x09);
    CHECK_EQ(ll_scan_feed(&st, pdu, n), 1);
    CHECK_EQ(st.n_dev, 1u);
    CHECK_EQ(st.dev[0].count, 1u);
    CHECK_EQ(st.dev[0].txadd, 1u);
    CHECK(strcmp(st.dev[0].name, "ABC") == 0);

    /* 同地址再来：去重，计数++ */
    CHECK_EQ(ll_scan_feed(&st, pdu, n), 1);
    CHECK_EQ(st.n_dev, 1u);
    CHECK_EQ(st.dev[0].count, 2u);

    /* 另一设备，无名字（NONCONN） */
    n = mk_adv(pdu, 2, 0, a2, 0, 0);
    CHECK_EQ(ll_scan_feed(&st, pdu, n), 1);
    CHECK_EQ(st.n_dev, 2u);
    CHECK_EQ(st.dev[1].name_len, 0u);

    /* 短名不覆盖已有完整名；反向可补 */
    n = mk_adv(pdu, 4, 1, a1, "XY", 0x08);   /* SCAN_RSP 短名 */
    ll_scan_feed(&st, pdu, n);
    CHECK(strcmp(st.dev[0].name, "ABC") == 0);
    n = mk_adv(pdu, 4, 0, a2, "Q", 0x08);
    ll_scan_feed(&st, pdu, n);
    CHECK(strcmp(st.dev[1].name, "Q") == 0);

    /* 非法：太短 / 长度字段越界 / 非 legacy type */
    CHECK_EQ(ll_scan_feed(&st, pdu, 5), 0);
    pdu[1] = 200;
    CHECK_EQ(ll_scan_feed(&st, pdu, 20), 0);
    n = mk_adv(pdu, 7, 0, a2, 0, 0);
    CHECK_EQ(ll_scan_feed(&st, pdu, n), 0);

    /* 名字截断到 LL_SCAN_NAME_MAX */
    static const uint8_t a3[6] = { 7, 7, 7, 7, 7, 7 };
    n = mk_adv(pdu, 0, 1, a3, "0123456789ABCDEFGH", 0x09);
    ll_scan_feed(&st, pdu, n);
    CHECK_EQ(st.dev[2].name_len, (uint8_t)LL_SCAN_NAME_MAX);
}

static void test_scan_table_full(void)
{
    ll_scan_stats_t st;
    memset(&st, 0, sizeof st);
    uint8_t pdu[16];
    uint8_t a[6] = { 0, 0, 0, 0, 0, 0 };
    for (uint32_t i = 0; i < LL_SCAN_DEVS + 3u; i++) {
        a[0] = (uint8_t)i;
        uint32_t n = mk_adv(pdu, 2, 0, a, 0, 0);
        ll_scan_feed(&st, pdu, n);
    }
    CHECK_EQ(st.n_dev, (uint32_t)LL_SCAN_DEVS);
    CHECK_EQ(st.dropped, 3u);
}

int main(void)
{
    test_sched_order();
    test_sched_phase_catchup();
    test_sched_empty_and_full();
    test_scan_feed();
    test_scan_table_full();
    TF_END();
}

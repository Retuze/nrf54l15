/*
 * ll_sched — 单 radio 时间片调度器（雏形，07_adv_scan 起用）。
 *
 * 纯逻辑（common 规则：无寄存器、无时间源——时刻全由调用方传入），
 * 宿主可单测（tests/sched_test.c）。
 *
 * 模型：固定槽位数组，每槽一个周期性 radio 活动（广播 sweep / 扫描窗 /
 * 将来的连接事件），带 [t_start, t_start+duration) 的占用窗口和优先级。
 * 调用方循环：ll_sched_pick() 选中下一个可运行槽 → 等到 t_start → 干活
 * → ll_sched_done()。窗口重叠时低优先级**让步**（顺延一个自身周期并计数
 * ——观测双角色碰撞行为正是 07 实验的目的）。
 */
#ifndef LL_SCHED_H
#define LL_SCHED_H

#include <stdint.h>

#define LL_SCHED_SLOTS 4u

typedef struct {
    uint8_t  active;
    uint8_t  prio;                 /* 数小者优先（连接 0 > 广播 1 > 扫描 2） */
    uint64_t t_start;              /* 下次开始时刻（µs，调用方时基） */
    uint32_t duration_us;          /* 预计占用（碰撞判定用） */
    uint32_t period_us;            /* 完成/让步后 t_start += period */
    uint32_t runs, yields;         /* 统计 */
} ll_sched_slot_t;

/* 清空全部槽位。 */
void ll_sched_init(void);

/* 注册周期活动。返回槽号，满则 -1。首次开始时刻 = t_first。 */
int ll_sched_add(uint8_t prio, uint64_t t_first,
                 uint32_t duration_us, uint32_t period_us);

/* 选下一个可运行槽：取 t_start 最早者；若它与某更高优先级槽的窗口重叠，
 * 则它让步（t_start += period，yields++）后重选。返回槽号（其 t_start
 * 即计划开始时刻，可能在 now 之前=已到期），无活动槽返回 -1。 */
int ll_sched_pick(uint64_t now);

/* 槽 run 完成：runs++，t_start += period（追上 now：落后多个周期时按
 * 周期步进直到 > now_end，保持相位）。 */
void ll_sched_done(int slot, uint64_t now);

/* 只读访问（打印统计用）。 */
const ll_sched_slot_t *ll_sched_slot(int slot);

#endif /* LL_SCHED_H */

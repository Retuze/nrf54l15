#include "sched.h"

static ll_sched_slot_t s_slot[LL_SCHED_SLOTS];

void ll_sched_init(void)
{
    for (uint32_t i = 0; i < LL_SCHED_SLOTS; i++) {
        s_slot[i] = (ll_sched_slot_t){0};
    }
}

int ll_sched_add(uint8_t prio, uint64_t t_first,
                 uint32_t duration_us, uint32_t period_us)
{
    for (uint32_t i = 0; i < LL_SCHED_SLOTS; i++) {
        if (!s_slot[i].active) {
            s_slot[i].active = 1;
            s_slot[i].prio = prio;
            s_slot[i].t_start = t_first;
            s_slot[i].duration_us = duration_us;
            s_slot[i].period_us = period_us;
            s_slot[i].runs = 0;
            s_slot[i].yields = 0;
            return (int)i;
        }
    }
    return -1;
}

/* [a, a+da) 与 [b, b+db) 是否重叠 */
static int overlap(uint64_t a, uint32_t da, uint64_t b, uint32_t db)
{
    return a < b + db && b < a + da;
}

int ll_sched_pick(uint64_t now)
{
    (void)now;
    for (;;) {
        int best = -1;
        for (uint32_t i = 0; i < LL_SCHED_SLOTS; i++) {
            if (!s_slot[i].active) continue;
            if (best < 0 || s_slot[i].t_start < s_slot[best].t_start) {
                best = (int)i;
            }
        }
        if (best < 0) {
            return -1;
        }
        /* 碰撞处理：被选中者与更高优先级槽重叠 → 自己让步重选；
         * 被选中者优先级更高 → 把重叠的低优先级槽顺延（也计让步，
         * 碰撞对观测者始终可见）。同优先级不干涉（先到先得）。 */
        int yielded = 0;
        for (uint32_t i = 0; i < LL_SCHED_SLOTS; i++) {
            if (!s_slot[i].active || (int)i == best) continue;
            if (!overlap(s_slot[best].t_start, s_slot[best].duration_us,
                         s_slot[i].t_start, s_slot[i].duration_us)) continue;
            if (s_slot[i].prio < s_slot[best].prio) {
                s_slot[best].t_start += s_slot[best].period_us;
                s_slot[best].yields++;
                yielded = 1;
                break;
            }
            if (s_slot[i].prio > s_slot[best].prio) {
                s_slot[i].t_start += s_slot[i].period_us;
                s_slot[i].yields++;
            }
        }
        if (!yielded) {
            return best;
        }
    }
}

void ll_sched_done(int slot, uint64_t now)
{
    if (slot < 0 || slot >= (int)LL_SCHED_SLOTS || !s_slot[slot].active) {
        return;
    }
    s_slot[slot].runs++;
    do {
        s_slot[slot].t_start += s_slot[slot].period_us;
    } while (s_slot[slot].t_start <= now);   /* 落后时按周期追上，保持相位 */
}

const ll_sched_slot_t *ll_sched_slot(int slot)
{
    if (slot < 0 || slot >= (int)LL_SCHED_SLOTS) {
        return 0;
    }
    return &s_slot[slot];
}

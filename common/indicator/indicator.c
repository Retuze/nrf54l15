/*
 * indicator.c - 多实例 LED 状态机：运行时注册 + 优先级抢占 + recover。
 *
 * FSM: IDLE → ON → OFF → (重复或结束)
 * Custom: 完全绕过 FSM，由回调驱动。
 */
#include "indicator.h"
#include <stddef.h>
#include <string.h>

enum { FSM_IDLE = 0, FSM_ON, FSM_OFF };

/* ---- 硬件抽象 -------------------------------------------------------- */

static uint32_t now_ms(const led_indicator_t *h)
{
    return h->cfg.tick_ms ? h->cfg.tick_ms(h->cfg.ctx) : 0u;
}

static void phy_set(const led_indicator_t *h, bool on)
{
    if (h->cfg.set_on != NULL) h->cfg.set_on(h->cfg.ctx, on);
}

/* ---- 选举：找优先级最高的 eligible pattern --------------------------- */

/*
 * 遍历注册表，找优先级值最小且 recover() 返回 true 的 pattern。
 * 查找范围不包括当前活跃 pattern。
 */
static led_pattern_id_t find_best(const led_indicator_t *h)
{
    led_pattern_id_t best    = LED_PATTERN_NONE;
    uint32_t         best_pr = UINT32_MAX;

    for (uint8_t i = 0; i < h->p_count; ++i) {
        if (i == h->active) continue;

        bool eligible = (h->p[i].recover != NULL)
                        && h->p[i].recover();
        if (!eligible) continue;

        if (h->p[i].priority < best_pr) {
            best_pr = h->p[i].priority;
            best    = i;
        }
    }
    return best;
}

/* ---- 激活 / 停用 ----------------------------------------------------- */

static void activate(led_indicator_t *h, led_pattern_id_t id)
{
    if (id >= h->p_count) return;

    const led_pattern_cfg_t *p = &h->p[id];

    h->active  = id;
    h->cnt     = p->rep;
    h->tick    = now_ms(h);
    h->cus_due = 0u;

    if (p->custom != NULL) {
        h->fsm = FSM_IDLE;
    } else {
        h->fsm = FSM_ON;
        if (p->on_ms > 0u) phy_set(h, true);
    }
}

static void deactivate(led_indicator_t *h)
{
    h->active = LED_PATTERN_NONE;
    h->fsm    = FSM_IDLE;
    h->cnt    = 0u;
    phy_set(h, false);
}

/* ---- FSM 步进 -------------------------------------------------------- */

static void fsm_tick(led_indicator_t *h, const led_pattern_cfg_t *p)
{
    const uint32_t now     = now_ms(h);
    const uint32_t elapsed = (uint32_t)(now - h->tick);

    switch (h->fsm) {
    case FSM_ON:
        if (elapsed < p->on_ms) return;
        /* off_ms=0 + FOREVER = 常亮不灭。 */
        if (p->off_ms == 0u && p->rep == LED_INDICATOR_REP_FOREVER) {
            h->tick = now;
            return;
        }
        h->tick = now;
        h->fsm  = FSM_OFF;
        if (p->off_ms > 0u) phy_set(h, false);
        break;

    case FSM_OFF:
        if (elapsed < p->off_ms) return;
        h->tick = now;

        /* 递减 rep（FOREVER 不减）。 */
        if (p->rep != LED_INDICATOR_REP_FOREVER && h->cnt > 0u) --h->cnt;
        if (h->cnt == 0u) {
            deactivate(h);
            led_pattern_id_t nxt = find_best(h);
            if (nxt != LED_PATTERN_NONE) activate(h, nxt);
            return;
        }

        /* FOREVER：检查 recover 是否仍想运行。 */
        if (p->rep == LED_INDICATOR_REP_FOREVER) {
            if (p->recover != NULL && !p->recover()) {
                deactivate(h);
                led_pattern_id_t nxt = find_best(h);
                if (nxt != LED_PATTERN_NONE) activate(h, nxt);
                return;
            }
        }

        /* 下一个 ON 周期。 */
        h->fsm = FSM_ON;
        if (p->on_ms > 0u) phy_set(h, true);
        break;

    default:
        break;
    }
}

/* ---- 公开 API -------------------------------------------------------- */

void led_indicator_init(led_indicator_t *h, const led_indicator_cfg_t *cfg)
{
    if (h == NULL || cfg == NULL) return;
    memset(h, 0, sizeof(*h));
    h->cfg = *cfg;
}

led_pattern_id_t led_indicator_register(led_indicator_t *h,
                                        const led_pattern_cfg_t *pcfg)
{
    if (h == NULL || pcfg == NULL) return LED_PATTERN_NONE;
    if (h->p_count >= LED_INDICATOR_MAX_PATTERNS) return LED_PATTERN_NONE;

    led_pattern_id_t id = h->p_count;
    h->p[id] = *pcfg;
    ++h->p_count;
    return id;
}

void led_indicator_start(led_indicator_t *h, led_pattern_id_t id)
{
    if (h == NULL || id >= h->p_count) return;

    const led_pattern_cfg_t *np = &h->p[id];

    /* 当前活跃灯语优先级更高则不允许抢占。 */
    if (h->active != LED_PATTERN_NONE) {
        if (np->priority > h->p[h->active].priority) return;
    }

    /* 同一 custom pattern 已活跃时不重复启动。 */
    if (h->active == id && np->custom != NULL) return;

    if (h->active != LED_PATTERN_NONE && h->active != id) {
        deactivate(h);
    }
    activate(h, id);
}

void led_indicator_stop(led_indicator_t *h, led_pattern_id_t id)
{
    if (h == NULL || id >= h->p_count) return;

    if (h->active == id) {
        deactivate(h);
        led_pattern_id_t nxt = find_best(h);
        if (nxt != LED_PATTERN_NONE) activate(h, nxt);
    }
}

void led_indicator_stop_all(led_indicator_t *h)
{
    if (h == NULL) return;
    deactivate(h);
}

void led_indicator_set_recover(led_indicator_t *h, led_pattern_id_t id,
                               led_indicator_recover_fn fn)
{
    if (h == NULL || id >= h->p_count) return;
    h->p[id].recover = fn;
}

void led_indicator_raw_set(led_indicator_t *h, bool on)
{
    if (h == NULL) return;
    phy_set(h, on);
}

bool led_indicator_is_idle(const led_indicator_t *h)
{
    return (h == NULL) || (h->active == LED_PATTERN_NONE);
}

led_pattern_id_t led_indicator_active(const led_indicator_t *h)
{
    if (h == NULL) return LED_PATTERN_NONE;
    return h->active;
}

/* ---- poll ------------------------------------------------------------ */

void led_indicator_poll(led_indicator_t *h)
{
    if (h == NULL) return;

    /* 空闲：选举 eligible pattern。 */
    if (h->active == LED_PATTERN_NONE) {
        led_pattern_id_t nxt = find_best(h);
        if (nxt != LED_PATTERN_NONE) activate(h, nxt);
        return;
    }

    /* 已耗尽（防御）。 */
    if (h->cnt == 0u && h->p[h->active].rep != LED_INDICATOR_REP_FOREVER) {
        deactivate(h);
        led_pattern_id_t nxt = find_best(h);
        if (nxt != LED_PATTERN_NONE) activate(h, nxt);
        return;
    }

    const led_pattern_cfg_t *p = &h->p[h->active];

    /* Custom 路径。 */
    if (p->custom != NULL) {
        const uint32_t now = now_ms(h);
        if (h->cus_due != 0u && (int32_t)(now - h->cus_due) < 0) return;

        uint32_t d = p->custom(h, &h->p[h->active].cus_state);
        if (d != 0u) {
            h->cus_due = now + d;
            return;
        }

        /* 回调返回 0：本轮结束。 */
        if (p->rep != LED_INDICATOR_REP_FOREVER && h->cnt > 0u) --h->cnt;
        if (h->cnt == 0u) {
            deactivate(h);
            led_pattern_id_t nxt = find_best(h);
            if (nxt != LED_PATTERN_NONE) activate(h, nxt);
            return;
        }

        /* FOREVER：检查 recover，false 则退出。 */
        if (p->rep == LED_INDICATOR_REP_FOREVER) {
            if (p->recover != NULL && !p->recover()) {
                deactivate(h);
                led_pattern_id_t nxt = find_best(h);
                if (nxt != LED_PATTERN_NONE) activate(h, nxt);
                return;
            }
        }
        h->cus_due = 0u;
        return;
    }

    /* FSM 路径。 */
    fsm_tick(h, p);

    /* 检查是否有更高优先级 pattern 想抢占。 */
    if (h->active != LED_PATTERN_NONE) {
        led_pattern_id_t nxt = find_best(h);
        if (nxt != LED_PATTERN_NONE
            && h->p[nxt].priority < h->p[h->active].priority) {
            deactivate(h);
            activate(h, nxt);
        }
    }
}

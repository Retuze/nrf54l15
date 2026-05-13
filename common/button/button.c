/*
 * button.c - 多实例按键状态机（轮询驱动，由 tick_ms 推进，无独立定时器）。
 *
 * 检测以下事件并通过 button_callbacks_t 回调上报：
 *   - 按下 / 释放
 *   - 短按（单击）
 *   - 多击（双击、三击等，在 MULTI_CLICK_INTERVAL_MS 窗口内累计）
 *   - 多击超时（窗口到期，上报最终连击次数）
 *   - 长按（超过 LONG_PRESS_MS）
 *   - 长按连发（按住期间每隔 LONG_REPEAT_MS 重复触发）
 *   - 按下时长（每隔 DURATION_CB_INTERVAL_MS 上报当前按住时长）
 *
 * 不内置单例 — 板级在 bsp/board.c 中创建并绑定实例。
 */
#include "button.h"
#include <stddef.h>

#if BUTTON_LOG
#include <stdio.h>
#define BLOG(fmt, ...) printf("[btn] " fmt, ##__VA_ARGS__)
#else
#define BLOG(...) ((void)0)
#endif

/* ---- 硬件抽象 -------------------------------------------------------- */

static uint32_t now_ms(const button_t *b)
{
    return b->cfg.tick_ms ? b->cfg.tick_ms(b->cfg.ctx) : 0u;
}

static bool read_pressed(const button_t *b)
{
    return b->cfg.read_pressed ? b->cfg.read_pressed(b->cfg.ctx) : false;
}

/* ---- 状态复位 -------------------------------------------------------- */

static void reset_state(button_t *b)
{
    b->down                = false;
    b->t_press_start       = 0u;
    b->t_last_multi_click  = 0u;
    b->click_count         = 0u;
    b->long_reported       = false;
    b->multi_deadline_ms   = 0u;
    b->long_next_repeat_ms = 0u;
    b->last_duration_cb_ms = 0u;
}

/* ---- 多击检测 -------------------------------------------------------- */

/*
 * 每次释放（短按判定通过）时调用。
 * 若距上次多击在间隔窗口内则累加计数，否则从 1 开始。
 * 同时重置多击超时截止时间。
 */
static void fire_click_event(button_t *b, uint32_t now)
{
    if (b->cbs.on_click) {
        b->cbs.on_click();
    }
    if (b->t_last_multi_click != 0u
        && (uint32_t)(now - b->t_last_multi_click) < BUTTON_MULTI_CLICK_INTERVAL_MS) {
        ++b->click_count;
    } else {
        b->click_count = 1u;
    }
    if (b->cbs.on_multi_click) {
        b->cbs.on_multi_click(b->click_count);
    }
    BLOG("click seq=%u\n", (unsigned int)b->click_count);
    b->t_last_multi_click = now;
    b->multi_deadline_ms  = now + BUTTON_MULTI_CLICK_INTERVAL_MS;
}

/*
 * 每轮 poll 检查多击窗口是否到期。
 * 仍按住时暂不结算（可能正在打第二/三下），等抬起后再处理。
 */
static void handle_multi_timeout(button_t *b, uint32_t now)
{
    if (b->click_count == 0u || b->multi_deadline_ms == 0u) return;
    if (b->down) return;
    if ((int32_t)(now - b->multi_deadline_ms) < 0) return;

    if (b->cbs.on_click_timeout) {
        b->cbs.on_click_timeout(b->click_count);
    }
    BLOG("multi timeout n=%u\n", (unsigned int)b->click_count);
    b->click_count       = 0u;
    b->multi_deadline_ms = 0u;
}

/* ---- 公开 API -------------------------------------------------------- */

void button_init(button_t *b, const button_cfg_t *cfg)
{
    if (b == NULL || cfg == NULL) return;
    b->cfg.read_pressed = cfg->read_pressed;
    b->cfg.tick_ms      = cfg->tick_ms;
    b->cfg.ctx          = cfg->ctx;
    b->cbs = (button_callbacks_t){0};
    reset_state(b);
    BLOG("init\n");
}

void button_set_callbacks(button_t *b, const button_callbacks_t *cbs)
{
    if (b == NULL) return;
    b->cbs = cbs ? *cbs : (button_callbacks_t){0};
}

bool button_is_pressed(const button_t *b)
{
    return (b != NULL) && b->down;
}

void button_reset_state(button_t *b)
{
    if (b == NULL) return;
    reset_state(b);
    /* 复位后同步当前脚电平：如果正在按下，补发一次 on_press。 */
    if (read_pressed(b) && b->cbs.on_press) {
        b->down          = true;
        b->t_press_start = now_ms(b);
        b->cbs.on_press();
    }
}

void button_poll(button_t *b)
{
    if (b == NULL) return;

    const uint32_t now      = now_ms(b);
    const bool     physical = read_pressed(b);

    /* 检查多击窗口超时（抬起状态下才结算）。 */
    handle_multi_timeout(b, now);

    /* ---- 按下沿 --------------------------------------------------- */
    if (physical && !b->down) {
        b->down                = true;
        b->t_press_start       = now;
        b->long_reported       = false;
        b->long_next_repeat_ms = 0u;
        b->last_duration_cb_ms = now;

        /* 若距上次多击未超时，延长多击窗口。 */
        if (b->t_last_multi_click != 0u
            && (uint32_t)(now - b->t_last_multi_click) < BUTTON_MULTI_CLICK_INTERVAL_MS) {
            b->multi_deadline_ms = now + BUTTON_MULTI_CLICK_INTERVAL_MS;
        }
        if (b->cbs.on_press) {
            b->cbs.on_press();
        }
        BLOG("down t=%lu\n", (unsigned long)now);

    /* ---- 释放沿 --------------------------------------------------- */
    } else if (!physical && b->down) {
        b->down = false;
        if (b->cbs.on_release) {
            b->cbs.on_release();
        }

        const uint32_t dur = now - b->t_press_start;
        BLOG("up   dur=%lu ms\n", (unsigned long)dur);

        /* 按住时长在短按窗口内 → 触发单击/多击。 */
        if (dur >= BUTTON_SHORT_PRESS_MIN_MS && dur <= BUTTON_SHORT_PRESS_MAX_MS) {
            fire_click_event(b, now);
        }

        b->long_reported       = false;
        b->long_next_repeat_ms = 0u;
    }

    /* ---- 按住期间检测 --------------------------------------------- */
    if (b->down) {
        const uint32_t dur = now - b->t_press_start;

        /* 按下时长回调。 */
        if (b->cbs.on_press_duration
            && (uint32_t)(now - b->last_duration_cb_ms) >= BUTTON_DURATION_CB_INTERVAL_MS) {
            b->cbs.on_press_duration(dur);
            b->last_duration_cb_ms = now;
        }

        /* 长按检测。 */
        if (dur >= BUTTON_LONG_PRESS_MS) {
            if (!b->long_reported) {
                /* 首次达到长按阈值。 */
                b->long_reported = true;
                if (b->cbs.on_long_press) {
                    b->cbs.on_long_press();
                }
                BLOG("long press\n");
                b->long_next_repeat_ms = now + BUTTON_LONG_REPEAT_MS;
            } else if (b->long_next_repeat_ms != 0u
                       && (int32_t)(now - b->long_next_repeat_ms) >= 0) {
                /* 长按连发。 */
                if (b->cbs.on_long_press_repeat) {
                    b->cbs.on_long_press_repeat();
                }
#if BUTTON_LOG && BUTTON_LOG_LONG_REPEAT
                BLOG("long repeat\n");
#endif
                b->long_next_repeat_ms = now + BUTTON_LONG_REPEAT_MS;
            }
        }
    }
}

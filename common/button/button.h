/*
 * button.h - 多实例按键状态机（轮询，由 tick_ms 推进，无独立定时器）。
 *
 * 模块本身不访问任何 GPIO 寄存器，读脚由 read_pressed(ctx) 提供，
 * 时间基准由 tick_ms(ctx) 提供。板级在 bsp/board.c 中创建实例并绑定回调。
 *
 * 时序阈值由 BUTTON_*_MS 宏静态配置，可在构建系统 -D 覆盖。
 *
 * 典型用法（板级）：
 *   button_t board_btn;
 *   button_cfg_t cfg = { .read_pressed = my_read, .tick_ms = my_tick };
 *   button_init(&board_btn, &cfg);
 *   button_set_callbacks(&board_btn, &my_cbs);
 *   每个循环里 button_poll(&board_btn);
 */
#ifndef BUTTON_H
#define BUTTON_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 时序阈值（编译期宏，可 -D 覆盖）--------------------------------- */

#ifndef BUTTON_SHORT_PRESS_MIN_MS
#define BUTTON_SHORT_PRESS_MIN_MS        20u
#endif
#ifndef BUTTON_SHORT_PRESS_MAX_MS
#define BUTTON_SHORT_PRESS_MAX_MS        500u
#endif

#ifndef BUTTON_MULTI_CLICK_INTERVAL_MS
#define BUTTON_MULTI_CLICK_INTERVAL_MS   250u
#endif
#ifndef BUTTON_LONG_PRESS_MS
#define BUTTON_LONG_PRESS_MS             1000u
#endif
#ifndef BUTTON_LONG_REPEAT_MS
#define BUTTON_LONG_REPEAT_MS            200u
#endif
#ifndef BUTTON_DURATION_CB_INTERVAL_MS
#define BUTTON_DURATION_CB_INTERVAL_MS   10u
#endif

/* ---- 调试日志（0=关，1=开）------------------------------------------- */

#ifndef BUTTON_LOG
#define BUTTON_LOG 1
#endif
#if BUTTON_LOG
/* 长按连发日志很吵，默认关闭。 */
#ifndef BUTTON_LOG_LONG_REPEAT
#define BUTTON_LOG_LONG_REPEAT 0
#endif
#endif

/* ---- 回调集 ---------------------------------------------------------- */

typedef struct {
    void (*on_press)(void);
    void (*on_release)(void);
    void (*on_click)(void);
    void (*on_multi_click)(uint16_t count);
    /* 多击窗口到期，上报最终连击次数。 */
    void (*on_click_timeout)(uint16_t count);
    void (*on_long_press)(void);
    void (*on_long_press_repeat)(void);
    /* 按下时长回调（按 DURATION_CB_INTERVAL_MS 间隔触发）。 */
    void (*on_press_duration)(uint32_t duration_ms);
} button_callbacks_t;

/* 板级注入：读脚是否按下 + 时间基准。 */
typedef struct {
    bool     (*read_pressed)(void *ctx); /* 必填 */
    uint32_t (*tick_ms)(void *ctx);      /* 必填 */
    void      *ctx;
} button_cfg_t;

/* 实例句柄。字段对外可见仅为让应用可静态分配，不要直接读写。 */
typedef struct button_s {
    button_cfg_t       cfg;
    button_callbacks_t cbs;

    bool     down;
    uint32_t t_press_start;
    uint32_t t_last_multi_click;
    uint16_t click_count;
    bool     long_reported;
    uint32_t multi_deadline_ms;
    uint32_t long_next_repeat_ms;
    uint32_t last_duration_cb_ms;
} button_t;

/* ---- 多实例 API ------------------------------------------------------ */

void button_init(button_t *b, const button_cfg_t *cfg);
void button_set_callbacks(button_t *b, const button_callbacks_t *cbs);
void button_poll(button_t *b);

/* 复位状态并同步当前脚电平（如从休眠唤醒后调用）。 */
void button_reset_state(button_t *b);
bool button_is_pressed(const button_t *b);

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_H */

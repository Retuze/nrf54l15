/*
 * indicator.h - 多实例 LED 状态机，支持优先级抢占与运行时注册灯语。
 *
 * 核心机制：
 *   - 每种"灯语"作为一个 pattern 在运行时注册（最多 8 个/实例）。
 *   - 每个 pattern 有优先级（值越小越高）。任何时候只有优先级最高
 *     且 recover() 返回 true 的 pattern 运行。高优先级可以打断低优先级。
 *   - pattern 支持两种驱动方式：
 *       1. 内置 FSM：ON → OFF → (重复或结束)
 *       2. Custom 回调：完全绕过 FSM，由回调自行控制 LED，回调返回
 *          0 表示本轮结束（按 rep 处理），>0 表示 d ms 后再调。
 *   - start(id) 强制激活一个 pattern（事件触发，如按键反馈）。
 *     有限 rep 跑完自动停；FOREVER 每个周期末尾检查 recover，
 *     返回 false 则退出。
 *
 * 板级移植：提供 set_on(ctx, bool) + set_pwm(ctx, duty) + tick_ms(ctx) 三个回调。
 * set_pwm 为可选项（NULL = 不支持 PWM，custom 回调回退到 set_on 阈值判断）。
 */
#ifndef INDICATOR_H
#define INDICATOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LED_INDICATOR_REP_FOREVER  0xFFFFFFFFu
#define LED_INDICATOR_MAX_PATTERNS 8
#define LED_PATTERN_NONE           0xFF

typedef uint8_t led_pattern_id_t;

struct led_indicator_s;

/* 自定义灯语回调。 */
typedef uint32_t (*led_indicator_custom_fn)(struct led_indicator_s *h, void **state);

/* recover 判定（幂等纯函数）：返回 true 表示"我想接班/继续"。NULL=从不。*/
typedef bool (*led_indicator_recover_fn)(void);

/* ---- 板级注入 -------------------------------------------------------- */

typedef struct {
    void     (*set_on)(void *ctx, bool on);
    void     (*set_pwm)(void *ctx, uint8_t duty); /* optional, NULL = PWM not supported */
    uint32_t (*tick_ms)(void *ctx);
    void      *ctx;
} led_indicator_cfg_t;

/* ---- pattern 注册参数 ------------------------------------------------ */

typedef struct {
    uint32_t priority;   /* 越小优先级越高 */
    uint32_t on_ms;      /* ON 时长(ms)；0=由 custom 回调接管 */
    uint32_t off_ms;     /* OFF 时长(ms)；0=常亮（仅 FSM 模式） */
    uint32_t rep;        /* 重复次数，REP_FOREVER=无限 */

    led_indicator_custom_fn  custom;    /* NULL=用内置 FSM */
    void                    *cus_state; /* custom 私有状态 */

    led_indicator_recover_fn recover;   /* NULL=从不自动恢复 */
} led_pattern_cfg_t;

/* ---- 实例句柄 -------------------------------------------------------- */

typedef struct led_indicator_s {
    led_indicator_cfg_t  cfg;

    /* 注册表 */
    led_pattern_cfg_t    p[LED_INDICATOR_MAX_PATTERNS];
    uint8_t              p_count;

    /* 运行时 */
    uint8_t              active;    /* 当前活跃 pattern ID */
    uint8_t              fsm;       /* 0=idle, 1=ON, 2=OFF */
    uint8_t              _rsvd[2];  /* 对齐到 4 字节边界 */
    uint32_t             tick;      /* 当前阶段起始 tick */
    uint32_t             cnt;       /* 剩余重复次数 */
    uint32_t             cus_due;   /* custom 下次到期 tick */
} led_indicator_t;

/* ---- API ------------------------------------------------------------- */

void led_indicator_init(led_indicator_t *h, const led_indicator_cfg_t *cfg);

/* 注册灯语，返回 ID（失败返回 PATTERN_NONE）。 */
led_pattern_id_t led_indicator_register(led_indicator_t *h,
                                        const led_pattern_cfg_t *pcfg);

/* 强制激活（忽略 recover，但仍受优先级约束）。 */
void led_indicator_start(led_indicator_t *h, led_pattern_id_t id);

/* 停用指定灯语。若该灯语当前活跃则立刻熄灭并选举下一个。 */
void led_indicator_stop(led_indicator_t *h, led_pattern_id_t id);

/* 停用全部灯语。 */
void led_indicator_stop_all(led_indicator_t *h);

/* 每周期调用：推进 FSM/custom + 选举下一个 eligible pattern。 */
void led_indicator_poll(led_indicator_t *h);

bool led_indicator_is_idle(const led_indicator_t *h);
led_pattern_id_t led_indicator_active(const led_indicator_t *h);

/* 修改已注册灯语的 recover 钩子。 */
void led_indicator_set_recover(led_indicator_t *h, led_pattern_id_t id,
                               led_indicator_recover_fn fn);

#ifdef __cplusplus
}
#endif

#endif /* INDICATOR_H */

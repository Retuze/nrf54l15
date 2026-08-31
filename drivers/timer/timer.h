/*
 * timer.h — 普通定时器驱动（TIMER 外设，多实例，不透明句柄）。
 *
 * 与 GRTC 的分工：GRTC/time 驱动是 BLE 专用时基（LL 的 now_us/delay_us +
 * 04_async_ll 的锚点闹钟，ch∈{0,1} 已被 time_alarm 占用）；本驱动面向应用
 * 通用定时——周期回调、单次延时。接口形状与 uart 同形（跨仓库约定）：
 *   - timer_init(timer_port_t port, const timer_cfg_t *cfg) → 不透明句柄：
 *     8 个 CC 通道是驱动内静态槽位，init 自动分配空闲通道，无堆分配
 *   - cfg 结构体聚合参数（加字段不改签名）
 *
 * 实例模型：应用核只有 TIMER00 一块外设（TIMER10 归 FLPR），8 个 CC 通道
 * 各承载一个实例（上限 8）。
 *
 * 单次语义的注意点：ONESHOTEN 的单次触发会停掉整块 TIMER——多实例共存时
 * 会冻结别人的周期定时器。所以本驱动的单次 = ISR 里"触发即解除该通道"
 * （INTEN 位清除），计数器永不停。
 *
 * 节拍：1MHz（PRESCALER=7，假设外设时钟 128MHz——板级验证见 06_timer：
 * 1s 周期回调对照 GRTC）。回调在 TIMER00 IRQ 上下文执行：必须短小。
 */
#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

typedef enum { TIMER00 } timer_port_t;

typedef struct {
    uint32_t freq_hz;        /* 节拍频率：当前只实现 1000000（µs 节拍） */
    uint8_t  irq_prio;       /* NVIC 优先级预写 */
} timer_cfg_t;

typedef struct timer_hw timer_hw_t;   /* 不透明句柄（定义在 timer.c） */
typedef void (*timer_cb_t)(timer_hw_t *t, void *arg);

typedef enum {
    TIMER_ONESHOT,      /* 触发一次后自动解除（ISR 内解除本通道） */
    TIMER_PERIODIC,     /* 周期触发（CC 自动 CLEAR） */
} timer_mode_t;

/* 初始化并占用一个 CC 通道（8 槽静态池）。非法参数/无空闲通道返回 NULL。 */
timer_hw_t *timer_init(timer_port_t port, const timer_cfg_t *cfg);

/* 注册回调（一次；start 不重复传）。cb=NULL 清除。 */
void timer_set_callback(timer_hw_t *t, timer_cb_t cb, void *arg);

/* 启动：µs 后触发（1MHz 节拍即计数；ONESHOT 触发一次，PERIODIC 周期触发）。
 * 0 成功，-1 参数错。 */
int  timer_start(timer_hw_t *t, uint32_t us, timer_mode_t mode);

/* 停触发（保留已注册回调，可再次 start）。TIMER 本体继续为其他实例服务。 */
void timer_stop(timer_hw_t *t);

/* 去初始化：停 + 清回调 + 释放通道。 */
void timer_deinit(timer_hw_t *t);

#endif /* TIMER_H */

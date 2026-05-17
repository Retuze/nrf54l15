/*
 * app_led_ctl.c - 业务层：定义全部 LED 灯语并绑定按键/系统事件。
 *
 * 灯语一览（优先级值越小越高）：
 *   pri  1  开机：常亮 3s                            [start() 触发，不可抢占]
 *   pri  5  长按反馈：100ms 闪 3 次                  [start() 触发]
 *   pri 10  单击反馈：100ms 闪 1 次                  [start() 触发]
 *   pri 10  双击反馈：100ms 闪 2 次                  [start() 触发]
 *   pri 12  充电中：慢呼吸 ~2.5s/半周期，FOREVER       [recover: g_charging]
 *   pri 15  低电量：快闪 3 次 + 停 2s，FOREVER       [recover: g_low_battery]
 *   pri 20  BLE 未连接：500ms 心跳，FOREVER           [recover: !g_ble_connected]
 *   pri 30  BLE 已连接：按 BLE 写入值控制 LED，FOREVER [recover: g_ble_connected]
 *   pri 190 退出充电：100ms 闪 2 次                  [start() 触发]
 *   pri 250 休眠：2s 慢闪，FOREVER                   [recover: g_sleeping]
 */
#include "app_led_ctl.h"
#include <stddef.h>
#include <stdio.h>
#include "board.h"
#include "rtt.h"
#include "button.h"
#include "event_bus.h"
#include "hal_delay.h"
#include "indicator.h"

/* ---- 全局状态 -------------------------------------------------------- */

static bool g_ble_connected = false;
static bool g_charging      = false;
static bool g_low_battery   = false;
static bool g_sleeping      = false;
static bool g_ble_led_state = false;

static led_indicator_t *led(void) { return board_led_get(); }

/* ---- pattern ID ------------------------------------------------------ */

static led_pattern_id_t id_startup;
static led_pattern_id_t id_ble_disconnected;
static led_pattern_id_t id_ble_connected;
static led_pattern_id_t id_click_single;
static led_pattern_id_t id_click_double;
static led_pattern_id_t id_long_press;
static led_pattern_id_t id_low_battery;
static led_pattern_id_t id_charging;
static led_pattern_id_t id_exit_charging;
static led_pattern_id_t id_sleep;

/* 1：RTT 打印按键→灯语（排查完改 0） */
#define APP_LED_CTL_LOG  1

#if APP_LED_CTL_LOG
static const char *pat_tag(led_pattern_id_t id)
{
    if (id == LED_PATTERN_NONE) return "none";
    if (id == id_click_single) return "single";
    if (id == id_click_double) return "double";
    if (id == id_long_press) return "long";
    if (id == id_charging) return "charging";
    if (id == id_startup) return "startup";
    return "?";
}

static void led_ctl_start(const char *evt, uint16_t click_cnt, led_pattern_id_t id)
{
    led_indicator_t *  h      = led();
    led_pattern_id_t   before = led_indicator_active(h);
    const uint32_t     pri_b  = (before != LED_PATTERN_NONE) ? h->p[before].priority : 99u;
    const uint32_t     pri_r  = h->p[id].priority;

    led_indicator_start(h, id);

    const led_pattern_id_t after = led_indicator_active(h);
    const char *           res;
    if (after == id) {
        res = "ok";
    } else if (before != LED_PATTERN_NONE && pri_r > pri_b) {
        res = "block_pri";
    } else {
        res = "no_start";
    }

    char buf[140];
    int  n = snprintf(buf, sizeof(buf),
                      "[led_ctl] %s cnt=%u req=%s(%u) pri_req=%u "
                      "act_before=%s(%u,pri%u) act_after=%s(%u) %s\r\n",
                      evt, (unsigned)click_cnt, pat_tag(id), (unsigned)id,
                      (unsigned)pri_r, pat_tag(before), (unsigned)before, (unsigned)pri_b,
                      pat_tag(after), (unsigned)after, res);
    if (n > 0) {
        rtt_write(buf, (uint32_t)n);
    }
}
#else
static void led_ctl_start(const char *evt, uint16_t click_cnt, led_pattern_id_t id)
{
    (void)evt;
    (void)click_cnt;
    led_indicator_start(led(), id);
}
#endif

/* ---- recover 判定 ---------------------------------------------------- */

static bool rv_ble_disconnected(void) { return !g_ble_connected; }
static bool rv_ble_connected(void)    { return g_ble_connected; }
static bool rv_low_battery(void)      { return g_low_battery; }
static bool rv_charging(void)         { return g_charging; }
static bool rv_sleep(void)            { return g_sleeping; }

/* ---- BLE 已连接：按 g_ble_led_state 直写 LED ------------------------- */

static uint32_t ble_connected_cb(led_indicator_t *h, void **state)
{
    (void)state;
    h->cfg.set_on(h->cfg.ctx,g_ble_led_state);
    return 100u;
}

/* ---- 充电呼吸灯 ------------------------------------------------------ */

/* 步进间隔（indicator cus_due；10ms/级 → 半周期 ~2.5s，poll 10ms） */
#define BREATH_STEP_MS  10u

typedef struct { int16_t brightness; int8_t dir; } breathing_t;
static breathing_t s_breathing;

static uint32_t breathing_cb(led_indicator_t *h, void **state)
{
    breathing_t *bs = (breathing_t *)(*state);
    if (bs == NULL) {
        return 0u;
    }

    if (h->cus_due == 0u) {
        bs->brightness = 0;
        bs->dir        = 1;
        if (h->cfg.set_pwm) {
            h->cfg.set_pwm(h->cfg.ctx, 0u);
        } else if (h->cfg.set_on) {
            h->cfg.set_on(h->cfg.ctx, false);
        }
        return BREATH_STEP_MS;
    }

    /* 三角波 0↔255 */
    bs->brightness += bs->dir;
    if (bs->brightness >= 255) {
        bs->brightness = 255;
        bs->dir        = -1;
    } else if (bs->brightness <= 0) {
        bs->brightness = 0;
        bs->dir        = 1;
    }

    uint8_t pwm = (uint8_t)bs->brightness;

    if (h->cfg.set_pwm) {
        h->cfg.set_pwm(h->cfg.ctx, pwm);
    } else if (h->cfg.set_on) {
        h->cfg.set_on(h->cfg.ctx, pwm >= 128);
    }

    return BREATH_STEP_MS;
}

/* ---- 低电量：快闪 3 次 + 停 2s --------------------------------------- */

typedef struct { uint8_t n; uint8_t phase; uint32_t tick; } low_bat_t;
static low_bat_t s_low_bat;

static uint32_t low_bat_cb(led_indicator_t *h, void **state)
{
    low_bat_t *st = (low_bat_t *)(*state);
    if (st == NULL) return 0u;

    uint32_t now     = millis();
    uint32_t elapsed = (uint32_t)(now - st->tick);

    switch (st->phase) {
    case 0: /* ON 100ms */
        if (elapsed < 100u) return 100u - elapsed;
        h->cfg.set_on(h->cfg.ctx,false);
        st->phase = 1;
        st->tick  = now;
        return 100u;

    case 1: /* OFF 100ms */
        if (elapsed < 100u) return 100u - elapsed;
        if (++st->n >= 3u) {
            /* 3 次闪完 → 进入停 2s。 */
            st->phase = 2;
            st->tick  = now;
            return 2000u;
        }
        h->cfg.set_on(h->cfg.ctx,true);
        st->phase = 0;
        st->tick  = now;
        return 100u;

    case 2: /* 停 2s */
        if (elapsed < 2000u) return 2000u - elapsed;
        /* 重新开始。 */
        st->n     = 0;
        st->phase = 0;
        st->tick  = now;
        h->cfg.set_on(h->cfg.ctx,true);
        return 100u;

    default:
        st->phase = 0;
        st->n     = 0;
        return 0u;
    }
}

/* ---- EventBus -------------------------------------------------------- */

static void on_ble_event(const event_t *evt)
{
    switch (evt->id) {
    case EVENT_BLE_CONNECTED:    g_ble_connected = true;  break;
    case EVENT_BLE_DISCONNECTED: g_ble_connected = false; break;
    default: break;
    }
}

static void on_lbs_led_write(const event_t *evt)
{
    if (evt->data != NULL && evt->data_len >= 1)
        g_ble_led_state = (*(const uint8_t *)evt->data) != 0;
}

static void on_charging_event(const event_t *evt)
{
    switch (evt->id) {
    case EVENT_CHARGING_IN:
        app_led_ctl_set_charging(true);
        break;
    case EVENT_CHARGING_OUT:
        app_led_ctl_set_charging(false);
        break;
    default:
        break;
    }
}

/* ---- 灯语注册 -------------------------------------------------------- */

void app_led_ctl_init(void)
{
    s_breathing.brightness = 0;
    s_breathing.dir        = 1;
    /* 开机：常亮 3s，最高优先级不可抢占。 */
    id_startup = led_indicator_register(led(), &(led_pattern_cfg_t){
        .priority = 1, .on_ms = 3000, .off_ms = 0, .rep = 1,
    });
    led_indicator_start(led(), id_startup);

    /* BLE 未连接：500ms 亮 + 500ms 灭 = 心跳灯 */
    id_ble_disconnected = led_indicator_register(led(), &(led_pattern_cfg_t){
        .priority = 20, .on_ms = 500, .off_ms = 500,
        .rep = LED_INDICATOR_REP_FOREVER, .recover = rv_ble_disconnected,
    });

    /* BLE 已连接：按手机写入值控制 LED。 */
    id_ble_connected = led_indicator_register(led(), &(led_pattern_cfg_t){
        .priority = 30, .on_ms = 0, .off_ms = 0,
        .rep = LED_INDICATOR_REP_FOREVER,
        .custom = ble_connected_cb,
        .recover = rv_ble_connected,
    });

    /* 低电量：快闪 3 + 停 2s，custom。 */
    id_low_battery = led_indicator_register(led(), &(led_pattern_cfg_t){
        .priority = 15, .on_ms = 0, .off_ms = 0,
        .rep = LED_INDICATOR_REP_FOREVER,
        .custom = low_bat_cb, .cus_state = &s_low_bat,
        .recover = rv_low_battery,
    });

    /* 充电中：慢呼吸，custom。优先级高于 BLE 未连接(20)。 */
    id_charging = led_indicator_register(led(), &(led_pattern_cfg_t){
        .priority = 12, .on_ms = 0, .off_ms = 0,
        .rep = LED_INDICATOR_REP_FOREVER,
        .custom = breathing_cb, .cus_state = &s_breathing,
        .recover = rv_charging,
    });

    /* 退出充电：100ms 闪 2 次。 */
    id_exit_charging = led_indicator_register(led(), &(led_pattern_cfg_t){
        .priority = 190, .on_ms = 100, .off_ms = 100, .rep = 2,
    });

    /* 休眠：2s 亮 + 2s 灭。 */
    id_sleep = led_indicator_register(led(), &(led_pattern_cfg_t){
        .priority = 250, .on_ms = 2000, .off_ms = 2000,
        .rep = LED_INDICATOR_REP_FOREVER, .recover = rv_sleep,
    });

    /* 单击反馈。 */
    id_click_single = led_indicator_register(led(), &(led_pattern_cfg_t){
        .priority = 10, .on_ms = 100, .off_ms = 0, .rep = 1,
    });

    /* 双击反馈。 */
    id_click_double = led_indicator_register(led(), &(led_pattern_cfg_t){
        .priority = 10, .on_ms = 100, .off_ms = 100, .rep = 2,
    });

    /* 长按反馈。 */
    id_long_press = led_indicator_register(led(), &(led_pattern_cfg_t){
        .priority = 5, .on_ms = 100, .off_ms = 100, .rep = 3,
    });

#if APP_LED_CTL_LOG
    {
        char buf[80];
        int  n = snprintf(buf, sizeof(buf),
                          "[led_ctl] reg single=%u double=%u long=%u (max=%u)\r\n",
                          (unsigned)id_click_single, (unsigned)id_click_double,
                          (unsigned)id_long_press, (unsigned)LED_INDICATOR_MAX_PATTERNS);
        if (n > 0) {
            rtt_write(buf, (uint32_t)n);
        }
    }
#endif

    /* 订阅事件。 */
    event_bus_subscribe(EVENT_BLE_CONNECTED,    on_ble_event);
    event_bus_subscribe(EVENT_BLE_DISCONNECTED, on_ble_event);
    event_bus_subscribe(EVENT_LBS_LED_WRITE,    on_lbs_led_write);
    event_bus_subscribe(EVENT_CHARGING_IN,  on_charging_event);
    event_bus_subscribe(EVENT_CHARGING_OUT, on_charging_event);

    board_btn_set_callbacks(&(button_callbacks_t){
        .on_click         = on_click_single,
        .on_multi_click   = on_multi_click,
        .on_click_timeout = on_click_timeout,
        .on_long_press    = on_long_press,
    });

    /* 验证：模拟接入充电（有充电检测后改由检测模块 emit） */
    event_bus_emit(EVENT_CHARGING_IN, NULL, 0);
}

/* ---- 按键回调 -------------------------------------------------------- */

/* 单击/双击在多击窗口结束后由 on_click_timeout 判定，避免每按一次就闪 single。 */
void on_click_single(void) { (void)0; }

void on_multi_click(uint16_t c) { (void)c; }

void on_long_press(void) { led_ctl_start("long_press", 0u, id_long_press); }

void on_click_timeout(uint16_t c)
{
    if (c == 1u) {
        led_ctl_start("timeout_1", c, id_click_single);
    } else if (c == 2u) {
        led_ctl_start("timeout_2", c, id_click_double);
    }
#if APP_LED_CTL_LOG
    else {
        char buf[48];
        int  n = snprintf(buf, sizeof(buf), "[led_ctl] timeout cnt=%u (ignore)\r\n",
                          (unsigned)c);
        if (n > 0) {
            rtt_write(buf, (uint32_t)n);
        }
    }
#endif
}

/* ---- 系统状态 -------------------------------------------------------- */

void app_led_ctl_set_charging(bool on)
{
    if (on == g_charging) return;
    g_charging = on;
    if (!on) led_indicator_start(led(), id_exit_charging);
}

void app_led_ctl_set_low_battery(bool on) { g_low_battery = on; }
void app_led_ctl_set_sleeping(bool on)    { g_sleeping = on; }

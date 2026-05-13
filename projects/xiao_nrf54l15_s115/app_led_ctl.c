/*
 * app_led_ctl.c - 业务层：定义全部 LED 灯语并绑定按键/系统事件。
 *
 * 灯语一览（优先级值越小越高）：
 *   pri  5  长按反馈：100ms 闪 3 次                  [start() 触发]
 *   pri 10  单击反馈：100ms 闪 1 次                  [start() 触发]
 *   pri 10  双击反馈：100ms 闪 2 次                  [start() 触发]
 *   pri 15  低电量：快闪 3 次 + 停 2s，FOREVER       [recover: g_low_battery]
 *   pri 20  BLE 未连接：短亮+长灭，FOREVER            [recover: !g_ble_connected]
 *   pri 30  BLE 已连接：无灯语（LED 灭）              [自动]
 *   pri 180 充电中：慢呼吸 1.5s/1.5s，FOREVER        [recover: g_charging]
 *   pri 190 退出充电：100ms 闪 2 次                  [start() 触发]
 *   pri 200 开机：常亮 3s                            [start() 触发]
 *   pri 250 休眠：2s 慢闪，FOREVER                   [recover: g_sleeping]
 */
#include "app_led_ctl.h"
#include <stddef.h>
#include "board.h"
#include "button.h"
#include "event_bus.h"
#include "hal_delay.h"
#include "indicator.h"

/* ---- 全局状态 -------------------------------------------------------- */

static bool g_ble_connected = false;
static bool g_charging      = false;
static bool g_low_battery   = false;
static bool g_sleeping      = false;

static led_indicator_t *led(void) { return board_led_get(); }

/* ---- pattern ID ------------------------------------------------------ */

static led_pattern_id_t id_startup;
static led_pattern_id_t id_ble_disconnected;
static led_pattern_id_t id_click_single;
static led_pattern_id_t id_click_double;
static led_pattern_id_t id_long_press;
static led_pattern_id_t id_low_battery;
static led_pattern_id_t id_charging;
static led_pattern_id_t id_exit_charging;
static led_pattern_id_t id_sleep;

/* ---- recover 判定 ---------------------------------------------------- */

static bool rv_ble_disconnected(void) { return !g_ble_connected; }
static bool rv_low_battery(void)      { return g_low_battery; }
static bool rv_charging(void)         { return g_charging; }
static bool rv_sleep(void)            { return g_sleeping; }

/* ---- 充电呼吸灯 ------------------------------------------------------ */

typedef struct { uint8_t phase; uint32_t tick; } breathing_t;
static breathing_t s_breathing;

static uint32_t breathing_cb(led_indicator_t *h, void **state)
{
    breathing_t *bs = (breathing_t *)(*state);
    if (bs == NULL) return 0u;

    uint32_t now     = millis();
    uint32_t elapsed = (uint32_t)(now - bs->tick);

    if (bs->phase == 0) {
        /* ON 阶段：保持 1.5s 后切灭。 */
        if (elapsed < 1500u) return 1500u - elapsed;
        led_indicator_raw_set(h, false);
        bs->phase = 1;
        bs->tick  = now;
        return 1500u;
    } else {
        /* OFF 阶段：保持 1.5s 后切亮。 */
        if (elapsed < 1500u) return 1500u - elapsed;
        led_indicator_raw_set(h, true);
        bs->phase = 0;
        bs->tick  = now;
        return 1500u;
    }
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
        led_indicator_raw_set(h, false);
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
        led_indicator_raw_set(h, true);
        st->phase = 0;
        st->tick  = now;
        return 100u;

    case 2: /* 停 2s */
        if (elapsed < 2000u) return 2000u - elapsed;
        /* 重新开始。 */
        st->n     = 0;
        st->phase = 0;
        st->tick  = now;
        led_indicator_raw_set(h, true);
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

/* ---- 灯语注册 -------------------------------------------------------- */

void app_led_ctl_init(void)
{
    /* BLE 未连接：500ms 亮 + 500ms 灭 = 心跳灯 */
    id_ble_disconnected = led_indicator_register(led(), &(led_pattern_cfg_t){
        .priority = 20, .on_ms = 500, .off_ms = 500,
        .rep = LED_INDICATOR_REP_FOREVER, .recover = rv_ble_disconnected,
    });
    led_indicator_start(led(), id_ble_disconnected);

    /* 开机：常亮 3s。 */
    id_startup = led_indicator_register(led(), &(led_pattern_cfg_t){
        .priority = 200, .on_ms = 3000, .off_ms = 0, .rep = 1,
    });
    led_indicator_start(led(), id_startup);

    /* 低电量：快闪 3 + 停 2s，custom。 */
    id_low_battery = led_indicator_register(led(), &(led_pattern_cfg_t){
        .priority = 15, .on_ms = 0, .off_ms = 0,
        .rep = LED_INDICATOR_REP_FOREVER,
        .custom = low_bat_cb, .cus_state = &s_low_bat,
        .recover = rv_low_battery,
    });

    /* 充电中：慢呼吸，custom。 */
    id_charging = led_indicator_register(led(), &(led_pattern_cfg_t){
        .priority = 180, .on_ms = 0, .off_ms = 0,
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

    /* 订阅事件。 */
    event_bus_subscribe(EVENT_BLE_CONNECTED,    on_ble_event);
    event_bus_subscribe(EVENT_BLE_DISCONNECTED, on_ble_event);

    board_btn_set_callbacks(&(button_callbacks_t){
        .on_click         = on_click_single,
        .on_multi_click   = on_multi_click,
        .on_click_timeout = on_click_timeout,
        .on_long_press    = on_long_press,
    });
}

/* ---- 按键回调 -------------------------------------------------------- */

void on_click_single(void)      { led_indicator_start(led(), id_click_single); }
void on_multi_click(uint16_t c) { (void)c; led_indicator_start(led(), id_click_single); }
void on_long_press(void)        { led_indicator_start(led(), id_long_press); }
void on_click_timeout(uint16_t c) { if (c == 2u) led_indicator_start(led(), id_click_double); }

/* ---- 系统状态 -------------------------------------------------------- */

void app_led_ctl_set_charging(bool on)
{
    if (on == g_charging) return;
    g_charging = on;
    if (!on) led_indicator_start(led(), id_exit_charging);
}

void app_led_ctl_set_low_battery(bool on) { g_low_battery = on; }
void app_led_ctl_set_sleeping(bool on)    { g_sleeping = on; }

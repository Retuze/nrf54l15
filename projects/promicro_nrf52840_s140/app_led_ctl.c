/*
 * app_led_ctl.c - ProMicro nRF52840 LED 灯语（无按键版本）。
 *
 * 灯语一览：
 *   pri 20  BLE 未连接：短亮+长灭，FOREVER
 *   pri 30  BLE 已连接：无灯语（LED 灭）
 *   pri 180 充电中：慢呼吸 1.5s/1.5s，FOREVER
 *   pri 200 开机：常亮 3s
 *   pri 250 休眠：2s 慢闪，FOREVER
 *
 * ProMicro 无用户按键，因此没有单击/双击/长按反馈灯语。
 * 后续可通过 BLE 写特征值或 USB 检测来触发额外灯语。
 */
#include "app_led_ctl.h"
#include <stddef.h>
#include "board.h"
#include "event_bus.h"
#include "hal_delay.h"
#include "indicator.h"

/* ---- 全局状态 -------------------------------------------------------- */

static bool g_ble_connected = false;
static bool g_charging      = false;
static bool g_sleeping      = false;

static led_indicator_t *led(void) { return board_led_get(); }

/* ---- pattern ID ------------------------------------------------------ */

static led_pattern_id_t id_startup;
static led_pattern_id_t id_ble_disconnected;
static led_pattern_id_t id_charging;
static led_pattern_id_t id_sleep;

/* ---- recover 判定 ---------------------------------------------------- */

static bool rv_ble_disconnected(void) { return !g_ble_connected; }
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
        if (elapsed < 1500u) return 1500u - elapsed;
        led_indicator_raw_set(h, false);
        bs->phase = 1;
        bs->tick  = now;
        return 1500u;
    } else {
        if (elapsed < 1500u) return 1500u - elapsed;
        led_indicator_raw_set(h, true);
        bs->phase = 0;
        bs->tick  = now;
        return 1500u;
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
    /* 开机：常亮 3s，一次。 */
    id_startup = led_indicator_register(led(), &(led_pattern_cfg_t){
        .priority = 200, .on_ms = 3000, .off_ms = 0, .rep = 1,
    });
    led_indicator_start(led(), id_startup);

    /* BLE 未连接：100ms 亮 + 1s 灭。 */
    id_ble_disconnected = led_indicator_register(led(), &(led_pattern_cfg_t){
        .priority = 20, .on_ms = 100, .off_ms = 1000,
        .rep = LED_INDICATOR_REP_FOREVER, .recover = rv_ble_disconnected,
    });

    /* 充电中：慢呼吸。 */
    id_charging = led_indicator_register(led(), &(led_pattern_cfg_t){
        .priority = 180, .on_ms = 0, .off_ms = 0,
        .rep = LED_INDICATOR_REP_FOREVER,
        .custom = breathing_cb, .cus_state = &s_breathing,
        .recover = rv_charging,
    });

    /* 休眠：2s 亮 + 2s 灭。 */
    id_sleep = led_indicator_register(led(), &(led_pattern_cfg_t){
        .priority = 250, .on_ms = 2000, .off_ms = 2000,
        .rep = LED_INDICATOR_REP_FOREVER, .recover = rv_sleep,
    });

    event_bus_subscribe(EVENT_BLE_CONNECTED,    on_ble_event);
    event_bus_subscribe(EVENT_BLE_DISCONNECTED, on_ble_event);
}

/* ---- 系统状态 -------------------------------------------------------- */

void app_led_ctl_set_charging(bool on) { g_charging = on; }
void app_led_ctl_set_sleeping(bool on) { g_sleeping = on; }

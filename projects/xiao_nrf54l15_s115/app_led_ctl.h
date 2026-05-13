/*
 * app_led_ctl.h - 业务层：LED 灯语注册 + 按键事件绑定 + 系统状态同步。
 *
 * 使用方式：
 *   1. board_init() 之后调用 app_led_ctl_init() 一次
 *   2. 每个 poll 循环中 board_led_poll() 即可
 *   3. 充电/电量/休眠状态变化时调用对应的 set 函数
 */
#ifndef APP_LED_CTL_H
#define APP_LED_CTL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：注册全部灯语 + 绑定按键回调 + 订阅 BLE 事件。 */
void app_led_ctl_init(void);

/* ---- 按键回调（由 button 模块触发，无需手动调用）--------------------- */

void on_click_single(void);
void on_multi_click(uint16_t count);
void on_click_timeout(uint16_t count);
void on_long_press(void);

/* ---- 系统状态设置 ---------------------------------------------------- */

/* USB 充电插入/拔出。拔出时自动触发"退出充电"闪 2 次。 */
void app_led_ctl_set_charging(bool on);

/* 低电量警告。 */
void app_led_ctl_set_low_battery(bool on);

/* 进入/退出休眠。 */
void app_led_ctl_set_sleeping(bool on);

#ifdef __cplusplus
}
#endif

#endif /* APP_LED_CTL_H */

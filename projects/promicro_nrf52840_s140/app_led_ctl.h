/*
 * app_led_ctl.h - ProMicro LED 灯语（无按键版本）。
 */
#ifndef APP_LED_CTL_H
#define APP_LED_CTL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void app_led_ctl_init(void);
void app_led_ctl_set_charging(bool on);
void app_led_ctl_set_sleeping(bool on);

#ifdef __cplusplus
}
#endif

#endif /* APP_LED_CTL_H */

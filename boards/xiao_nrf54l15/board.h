/*
 * board.h - Seeed Studio XIAO nRF54L15 板级定义。
 *
 * 参考: https://wiki.seeedstudio.com/cn/xiao_nrf54l15_sense_getting_started/
 *
 * 板载硬件:
 *   USER LED  : P2.00  (低电平有效)
 *   USER KEY  : P0.00  (低电平有效，内部上拉)
 *   UART TX    : P1.09
 *   UART RX    : P1.08
 * 排针 (XIAO D0..D15):
 *   D0  P1.04   D8  P2.01
 *   D1  P1.05   D9  P2.04
 *   D2  P1.06   D10 P2.02
 *   D3  P1.07   D11 P0.03
 *   D4  P1.10   D12 P0.04
 *   D5  P1.11   D13 P2.10
 *   D6  P2.08   D14 P2.09
 *   D7  P2.07   D15 P2.06
 *
 * 其他:
 *   RF Switch Power  P2.03
 *   RF Switch Select P2.05
 *   AIN7_VBAT        P1.14
 */
#ifndef BOARD_H
#define BOARD_H

#include "hal_gpio.h"
#include "indicator.h"
#include "button.h"
#include "shell.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 板载引脚 -------------------------------------------------------- */

#define LED_PIN         PIN_P2(0)
#define LED_ACTIVE_LOW  1

#define KEY_PIN         PIN_P0(0)
#define KEY_ACTIVE_LOW  1

/* ---- 排针 ------------------------------------------------------------ */

#define D0   PIN_P1(4)
#define D1   PIN_P1(5)
#define D2   PIN_P1(6)
#define D3   PIN_P1(7)
#define D4   PIN_P1(10)
#define D5   PIN_P1(11)
#define D6   PIN_P2(8)
#define D7   PIN_P2(7)
#define D8   PIN_P2(1)
#define D9   PIN_P2(4)
#define D10  PIN_P2(2)
#define D11  PIN_P0(3)
#define D12  PIN_P0(4)
#define D13  PIN_P2(10)
#define D14  PIN_P2(9)
#define D15  PIN_P2(6)

/* ---- 板载 LED -------------------------------------------------------- */

/* 获取实例指针，供注册 pattern / start / stop 等高级操作。 */
led_indicator_t *board_led_get(void);

/* 便捷：直接写亮灭。 */
void board_led_on(void);
void board_led_off(void);
void board_led_set(bool on);
void board_led_pwm(uint8_t duty);

/* 每周期 poll（驱动内置 FSM 和自定义回调）。 */
void board_led_poll(void);

/* ---- 板载按键 -------------------------------------------------------- */

button_t *board_btn_get(void);
void board_btn_set_callbacks(const button_callbacks_t *cbs);
void board_btn_poll(void);
bool board_btn_is_pressed(void);

/* 板载 USER KEY 原始读取（true = 按下）。 */
bool board_user_key_pressed(void);

/* ---- Shell ----------------------------------------------------------- */

shell_t *board_shell_get(void);
bool board_shell_poll(void);

/* ---- 初始化 ---------------------------------------------------------- */

/* 初始化 SysTick、RTT、板载 LED/Button 驱动及 RF 开关。 */
void board_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */

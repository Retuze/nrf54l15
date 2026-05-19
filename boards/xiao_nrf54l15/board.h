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
#include "hal_sd.h"
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

/* ---- UART ------------------------------------------------------------ */

#define UART_TX_PIN  PIN_P1(9)
#define UART_RX_PIN  PIN_P1(8)

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

/* ---- I2S (D0-D3 on header, contiguous, logic-analyzer friendly) ---- */

#define I2S_SCK_PIN   PIN_P1(4)   /* D0 */
#define I2S_LRCK_PIN  PIN_P1(5)   /* D1 */
#define I2S_SDOUT_PIN PIN_P1(6)   /* D2 */
#define I2S_MCK_PIN   PIN_P1(7)   /* D3 */
#define I2S_SDIN_PIN  PIN_P1(10)  /* D4 — 回环测试: 用杜邦线将 D2(SDOUT) 连到 D4(SDIN) */

/* ---- SD 卡 SPI (D6-D9) ------------------------------------------------ */

#define SD_SCK_PIN    PIN_P2(8)   /* D6 */
#define SD_MOSI_PIN   PIN_P2(7)   /* D7 */
#define SD_MISO_PIN   PIN_P2(1)   /* D8 */
#define SD_CS_PIN     PIN_P2(4)   /* D9 */

/* ---- 板载 LED -------------------------------------------------------- */

/* 获取实例指针，供注册 pattern / start / stop 等高级操作。 */
led_indicator_t *board_led_get(void);

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

/* ---- I2S 正弦波输出 -------------------------------------------------- */

/* 初始化 I2S 并启动连续正弦波输出 + 回环接收校验 (16-bit, 15.625 kHz 采样率).
 * 需先用杜邦线连接 D2(SDOUT) → D4(SDIN). */
void board_i2s_loopback_start(void);
void board_i2s_stop(void);

/* 初始化 SD 卡 (SPI 模式, D6-D9). */
void board_sd_init(void);

/* ---- 初始化 ---------------------------------------------------------- */

/* 初始化 SysTick、RTT、板载 LED/Button 驱动及 RF 开关。 */
void board_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */

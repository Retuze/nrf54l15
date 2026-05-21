/*
 * board.h - Seeed Studio XIAO nRF54L15 board-level configuration.
 *
 * 主要选项: LED 低有效, KEY 低有效, RT-Thread 配置, UART/I2S 引脚复用等.
 *
 * 引脚映射 (核心):
 *   LED_R          P2.00  (低有效)
 *   KEY            P1.15  (低有效, 按下 = LOW)
 *   UART_TX / D5   P0.04  → P1.09 [ESP-AT]
 *   UART_RX / D6   P0.05  → P1.08 [ESP-AT]
 *   I2S_SCK  / D0  P1.04
 *   I2S_LRCK / D1  P1.05
 *   I2S_SDOUT/ D2  P1.06
 *   I2S_MCK  / D3  P1.07
 *   I2S_SDIN / D4  P1.10
 *   SD_SCK   / D6  P0.05 (复用, 与 UART_RX 时空隔离)
 *   SD_MOSI  / D7  P0.06
 *   SD_MISO  / D8  P0.07
 *   SD_CS    / D9  P0.08
 * 其他:
 *   RF Switch Power  P2.03
 *   RF Switch Select P2.05
 *   BAT_EN           P1.14  (TPS22916 ON 引脚, 高电平使能)
 *   BAT_ADC          P1.13  (分压后电压, SAADC 读取)
 */
#ifndef BOARD_H
#define BOARD_H

#include "led_indicator.h"
#include "button_driver.h"
#include "shell.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 板级配置 --------------------------------------------------------- */

/* 项目名必须为驱动注册时所需的精确字符串。 */
#define BOARD_NAME         "xiao_nrf54l15"

#define RT_HEAP_SIZE        (64 * 1024)
#define RT_TICK_PER_SECOND  1000

/* ---- 引脚 ------------------------------------------------------------- */

#define PIN_P1(n)  ((n) + 0x100u)
#define PIN_P2(n)  ((n) + 0x200u)
#define PIN_P0(n)  (n)

#define LED_PIN    PIN_P2(0)
#define KEY_PIN    PIN_P1(15)

#define LED_ACTIVE_LOW  1
#define KEY_ACTIVE_LOW  1

#define UART_TX_PIN  PIN_P1(9)
#define UART_RX_PIN  PIN_P1(8)

/* ---- 电池 ------------------------------------------------------------ */

#define BAT_EN_PIN   PIN_P1(15)  /* TPS22916 使能, 高电平有效 */
#define BAT_ADC_PIN  PIN_P1(14)  /* 分压后 ADC 输入 (AIN7) */

/* ---- 排针 ------------------------------------------------------------ */

#define D0   PIN_P1(4)
#define D1   PIN_P1(5)
#define D2   PIN_P1(6)
#define D3   PIN_P1(7)
#define D4   PIN_P1(10)
#define D5   PIN_P0(4)
#define D6   PIN_P0(5)
#define D7   PIN_P0(6)
#define D8   PIN_P0(7)
#define D9   PIN_P0(8)

/* ---- I2S 引脚 (同 D0-D4) ---------------------------------------------- */

#define I2S_SCK_PIN   PIN_P1(4)   /* D0 */
#define I2S_LRCK_PIN  PIN_P1(5)   /* D1 */
#define I2S_SDOUT_PIN PIN_P1(6)   /* D2 */
#define I2S_MCK_PIN   PIN_P1(7)   /* D3 */
#define I2S_SDIN_PIN  PIN_P1(10)  /* D4 */

/* ---- SD 卡 (SPI, D6-D9) ---------------------------------------------- */

#define SD_SCK_PIN    PIN_P0(5)
#define SD_MOSI_PIN   PIN_P0(6)
#define SD_MISO_PIN   PIN_P0(7)
#define SD_CS_PIN     PIN_P0(8)

/* ---- LED / Button ----------------------------------------------------- */

led_indicator_t *board_led_get(void);
void board_led_poll(void);

button_t *board_btn_get(void);
void board_btn_set_callbacks(const struct button_callbacks *cbs);
void board_btn_poll(void);
bool board_btn_is_pressed(void);

/* 板载 USER KEY 原始读取（true = 按下）。 */
bool board_user_key_pressed(void);

/* ---- Shell ----------------------------------------------------------- */

shell_t *board_shell_get(void);
bool board_shell_poll(void);

/* ---- Opus 音频播放 (I2S TX-only, D2/SDOUT) --------------------------- */

void board_opus_play_start(void);
void board_opus_play_stop(void);

/* ---- Opus 正弦编解码回环测试 ------------------------------------------- */
void board_opus_loopback_start(void);

/* 初始化 SD 卡 (SPI 模式, D6-D9). */
void board_sd_init(void);

/* ---- I2S 音频播放 ---------------------------------------------------- */

/* I2S 回环测试: 正弦波输出 + 回环接收校验 (16-bit, 15.625 kHz 采样率).
 * 需先用杜邦线连接 D2(SDOUT) → D4(SDIN). */
void board_i2s_loopback_start(void);

/* 停止当前 I2S 传输 (回环或播放). */
void board_i2s_stop(void);

/* ---- 电池 ------------------------------------------------------------ */

/* 执行一次电池采集 + 滑窗 + 打印. 首次调用自动初始化 SAADC. */
void board_battery_sample(void);

/* 读取电池电压 (窗口平均), 返回值 mV. */
int board_battery_read_mv(void);

/* ---- 初始化 ---------------------------------------------------------- */

/* 初始化 SysTick、RTT、板载 LED/Button 驱动及 RF 开关。 */
void board_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */

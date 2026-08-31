/*
 * board.h - Seeed Studio XIAO nRF54L15 board-level configuration.
 *
 * 主要选项: LED 低有效, KEY 低有效, RT-Thread 配置, UART/I2S 引脚复用等.
 *
 * 引脚映射 (核心):
 *   LED_R          P2.00  (低有效)
 *   KEY            P0.00  (低有效, 按下 = LOW)
 *   D0  / I2S_SCK  P1.04
 *   D1  / I2S_LRCK P1.05
 *   D2  / I2S_SDOUT P1.06
 *   D3  / I2S_MCK  P1.07
 *   D4  / I2S_SDIN P1.10
 *   D5  / I2C_SDA  P1.11
 *   D6  / UART_TX  P2.08
 *   D7  / UART_RX  P2.07
 *   D8  / SPI_MOSI P2.01
 *   D9  / SPI_SCK  P2.04
 *   D10 / SPI_MISO P2.02
 *   D11 / I2C_SCL  P0.03
 *   D12            P0.04
 *   D13            P2.10
 *   D14            P2.09
 *   D15            P2.06
 * 其他:
 *   RF Switch Power  P2.03
 *   RF Switch Select P2.05
 *   BAT_EN           P1.15  (TPS22916 ON 引脚, 高电平使能)
 *   BAT_ADC          P1.14  (分压后电压, SAADC 读取)
 */
#ifndef BOARD_H
#define BOARD_H

#include "indicator.h"
#include "button.h"
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

#include "hal_gpio.h"

#define LED_PIN    PIN_P2(0)
#define KEY_PIN    PIN_P0(0)

#define LED_ACTIVE_LOW  1
#define KEY_ACTIVE_LOW  1

#define UART_TX_PIN  PIN_P1(9)   /* SAMD11_RX, UART TX */
#define UART_RX_PIN  PIN_P1(8)   /* SAMD11_TX, UART RX */

/* ---- 电池 ------------------------------------------------------------ */

#define BAT_EN_PIN   PIN_P1(15)  /* TPS22916 使能, 高电平有效 */
#define BAT_ADC_PIN  PIN_P1(14)  /* 分压后 ADC 输入 (AIN7) */

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

/* ---- I2S 引脚 (同 D0-D4) ---------------------------------------------- */

#define I2S_SCK_PIN   PIN_P1(4)   /* D0 */
#define I2S_LRCK_PIN  PIN_P1(5)   /* D1 */
#define I2S_SDOUT_PIN PIN_P1(6)   /* D2 */
#define I2S_MCK_PIN   PIN_P1(7)   /* D3 */
#define I2S_SDIN_PIN  PIN_P1(10)  /* D4 */

/* ---- SD 卡 (SPI) --------------------------------------------------------
 *  D8=MOSI(P2.01), D9=SCK(P2.04), D10=MISO(P2.02), CS 需额外 GPIO */

#define SD_SCK_PIN    PIN_P2(4)   /* D9 */
#define SD_MOSI_PIN   PIN_P2(1)   /* D8 */
#define SD_MISO_PIN   PIN_P2(2)   /* D10 */
#define SD_CS_PIN     PIN_P2(0)   /* 暂用 P2.00, 与 LED 共享 */

/* ---- LED / Button ----------------------------------------------------- */

led_indicator_t *board_led_get(void);
void board_led_poll(void);

button_t *board_btn_get(void);
void board_btn_set_callbacks(const button_callbacks_t *cbs);
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

/*
 * boards/xiao_nrf54l15.h — 板级定义：Seeed XIAO nRF54L15
 *
 * 约定：这里只写"这块板子的硬件事实"（接哪根引脚、有哪些外设、怎么烧录），
 * 不写行为开关（放实验 config.h）也不写寄存器（放 vendor/mdk）。
 * 引脚用裸数字宏（PORT + PIN）——54L 暂无 gpio 驱动，PORT 对应寄存器组 NRF_Pn_S。
 */
#pragma once

/* ---- 板载 LED ------------
 * 用户 LED = P2.00，低电平点亮（54L 的 P2 是独立 GPIO 寄存器组）
 */
#define BOARD_LED_PORT          2u
#define BOARD_LED_PIN           0u
#define BOARD_LED_ACTIVE_LEVEL  0u

/* ---- 用户按键 ------------
 * P0.00，低电平按下，带内部上拉（暂未使用）
 */
#define BOARD_BUTTON_PORT       0u
#define BOARD_BUTTON_PIN        0u
#define BOARD_BUTTON_ACTIVE_LEVEL 0u

/* ---- 调试串口 ------------
 * UARTE20 TX = P1.09，路由到板载 SAMD11 USB-serial 桥。115200 8N1。
 */
#define BOARD_CONSOLE_TX_PORT   1u
#define BOARD_CONSOLE_TX_PIN    9u

#define BOARD_VENDOR            "Seeed"
#define BOARD_MODEL             "XIAO nRF54L15"

/* ---- 烧录 ----------
 * 板载 SAMD11 CMSIS-DAP 调试器：pyocd flash -t nrf54l（scripts/flash.sh）
 */
#define BOARD_FLASH_METHOD      "pyOCD (onboard CMSIS-DAP, target nrf54l)"

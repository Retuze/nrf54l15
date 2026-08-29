/*
 * project/01_conn/config.h — 实验 1 配置装配点。
 *
 * 两层内容：
 *   1) 板卡接入：include 所用板卡头（板库 boards/，库可放仓库外）
 *   2) 控制台配置（drivers 的 uart.c 会按约定 include 本文件
 *      —— path 由工程 CMakeLists 的 NRF54_CONSOLE_CFG_DIR 注入）
 *
 * 换板 = 改 board include；换串口配置 = 改 CONFIG_CONSOLE_* 两行。
 */
/* 当前板卡：Seeed XIAO nRF54L15 */
#include "xiao_nrf54l15.h"

/* ---- 控制台（UARTE20，经板载 SAMD11 桥到 USB 串口）------ */
#define CONFIG_CONSOLE_TX_PORT  BOARD_CONSOLE_TX_PORT
#define CONFIG_CONSOLE_TX_PIN   BOARD_CONSOLE_TX_PIN

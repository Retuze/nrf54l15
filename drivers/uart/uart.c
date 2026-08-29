#include "uart.h"
#include "nrf.h"

/*
 * 控制台接线由实验 config.h 提供（include 路径经 NRF54_CONSOLE_CFG_DIR 注入，
 * 见 drivers/CMakeLists.txt）；无 config 链时回退到默认值（仍可独立编译）。
 */
#include "config.h"
#ifndef CONFIG_CONSOLE_TX_PORT
#define CONFIG_CONSOLE_TX_PORT  1u
#endif
#ifndef CONFIG_CONSOLE_TX_PIN
#define CONFIG_CONSOLE_TX_PIN   9u   /* P1.09 -> SAMD11 USB serial */
#endif

#define UART      NRF_UARTE20_S
#define TX_PORT   CONFIG_CONSOLE_TX_PORT
#define TX_PIN    CONFIG_CONSOLE_TX_PIN

/* EasyDMA source buffer (must live in RAM). */
static uint8_t tx_buf[256];

void uart_init(void)
{
    /* Idle-high TX pin via GPIO before the UARTE takes it over. */
    NRF_P1_S->PIN_CNF[TX_PIN] =
        (GPIO_PIN_CNF_DIR_Output       << GPIO_PIN_CNF_DIR_Pos) |
        (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos);
    NRF_P1_S->OUTSET = (1u << TX_PIN);

    UART->PSEL.TXD = (TX_PORT << UARTE_PSEL_TXD_PORT_Pos) | TX_PIN;  /* connected */
    UART->BAUDRATE = UARTE_BAUDRATE_BAUDRATE_Baud115200;
    UART->ENABLE   = UARTE_ENABLE_ENABLE_Enabled;
}

void uart_write(const void *buf, uint32_t len)
{
    if (len == 0) {
        return;
    }
    if (UART->ENABLE != UARTE_ENABLE_ENABLE_Enabled) {
        return;   /* not initialized yet — avoid blocking on DMA that never ends */
    }
    if (len > sizeof(tx_buf)) {
        len = sizeof(tx_buf);
    }
    const uint8_t *s = (const uint8_t *)buf;
    for (uint32_t i = 0; i < len; i++) {
        tx_buf[i] = s[i];
    }

    UART->DMA.TX.PTR    = (uint32_t)tx_buf;
    UART->DMA.TX.MAXCNT = len;
    UART->EVENTS_DMA.TX.END = 0;
    UART->TASKS_DMA.TX.START = 1;
    while (UART->EVENTS_DMA.TX.END == 0) {
    }
}

/* -------------------------------------------- picolibc stdio 落点 -- */
/* printf 的输出最终经 drivers/core/syscalls.c 的强 write() 落到 uart_write。
 * uart_init() 之前调用 printf 会直接丢字符（uart_write 的 ENABLE 守卫），
 * 所以控制台初始化要放在第一条 printf 前。 */


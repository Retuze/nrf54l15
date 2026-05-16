#include "hal_uart.h"
#include "board.h"
#include <nrfx_uarte.h>

static nrfx_uarte_t g_uarte = NRFX_UARTE_INSTANCE(20);
static uint8_t      g_tx_cache[1]; /* DMA 要求源数据在 RAM 中 */

static nrf_uarte_baudrate_t map_baud(uint32_t baud)
{
    switch (baud) {
    case 1200:    return NRF_UARTE_BAUDRATE_1200;
    case 2400:    return NRF_UARTE_BAUDRATE_2400;
    case 4800:    return NRF_UARTE_BAUDRATE_4800;
    case 9600:    return NRF_UARTE_BAUDRATE_9600;
    case 14400:   return NRF_UARTE_BAUDRATE_14400;
    case 19200:   return NRF_UARTE_BAUDRATE_19200;
    case 28800:   return NRF_UARTE_BAUDRATE_28800;
    case 38400:   return NRF_UARTE_BAUDRATE_38400;
    case 57600:   return NRF_UARTE_BAUDRATE_57600;
    case 76800:   return NRF_UARTE_BAUDRATE_76800;
    case 115200:  return NRF_UARTE_BAUDRATE_115200;
    case 230400:  return NRF_UARTE_BAUDRATE_230400;
    case 250000:  return NRF_UARTE_BAUDRATE_250000;
    case 460800:  return NRF_UARTE_BAUDRATE_460800;
    case 921600:  return NRF_UARTE_BAUDRATE_921600;
    case 1000000: return NRF_UARTE_BAUDRATE_1000000;
    default:      return NRF_UARTE_BAUDRATE_115200;
    }
}

void serialBegin(uint32_t baud)
{
    nrfx_uarte_config_t cfg = NRFX_UARTE_DEFAULT_CONFIG(UART_TX_PIN, UART_RX_PIN);
    cfg.baudrate = map_baud(baud);
    cfg.tx_cache.p_buffer = g_tx_cache;
    cfg.tx_cache.length   = sizeof(g_tx_cache);

    nrfx_uarte_init(&g_uarte, &cfg, NULL); /* NULL handler → 阻塞模式 */
}

void serialWrite(const uint8_t *buf, size_t len)
{
    nrfx_uarte_tx(&g_uarte, buf, len, 0);
}

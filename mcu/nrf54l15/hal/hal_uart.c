#include "hal_uart.h"
#include "board.h"
#include <hal/nrf_gpio.h>
#include <hal/nrf_uarte.h>
#include <nrf.h>
#include <nrfx_clock.h>

static bool     g_uart_ok;
static uint8_t  g_tx_byte;

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

static void uart_clock_start(void)
{
    if (!nrfx_clock_init_check()) {
        if (nrfx_clock_init(NULL) != NRFX_SUCCESS) {
            return;
        }
        nrfx_clock_enable();
    }
    if (!nrfx_clock_hfclk_is_running()) {
        nrfx_clock_hfclk_start();
    }
}

static void uart_pins_prepare(void)
{
#if NRF_GPIO_HAS_SEL
    nrf_gpio_pin_control_select(UART_TX_PIN, NRF_GPIO_PIN_SEL_GPIO);
    nrf_gpio_pin_control_select(UART_RX_PIN, NRF_GPIO_PIN_SEL_GPIO);
#endif
    nrf_gpio_pin_set(UART_TX_PIN);
    nrf_gpio_cfg(UART_TX_PIN,
                 NRF_GPIO_PIN_DIR_OUTPUT,
                 NRF_GPIO_PIN_INPUT_DISCONNECT,
                 NRF_GPIO_PIN_NOPULL,
                 NRF_GPIO_PIN_S0S1,
                 NRF_GPIO_PIN_NOSENSE);
    nrf_gpio_cfg(UART_RX_PIN,
                 NRF_GPIO_PIN_DIR_INPUT,
                 NRF_GPIO_PIN_INPUT_CONNECT,
                 NRF_GPIO_PIN_PULLUP,
                 NRF_GPIO_PIN_S0S1,
                 NRF_GPIO_PIN_NOSENSE);
}

/* 与 Zephyr uart_nrfx uarte_tx_path_init 相同：预置 TXSTOPPED 状态 */
static void uarte_tx_path_prime(NRF_UARTE_Type * p)
{
    nrf_uarte_shorts_enable(p, NRF_UARTE_SHORT_ENDTX_STOPTX);

    nrf_uarte_enable(p);
    nrf_uarte_tx_buffer_set(p, &g_tx_byte, 0);
    nrf_uarte_event_clear(p, NRF_UARTE_EVENT_TXSTOPPED);
    nrf_uarte_task_trigger(p, NRF_UARTE_TASK_STARTTX);
    while (!nrf_uarte_event_check(p, NRF_UARTE_EVENT_TXSTOPPED)) {
    }
    nrf_uarte_disable(p);
}

static void uarte_putc(uint8_t byte)
{
    NRF_UARTE_Type * p = NRF_UARTE20;

    g_tx_byte = byte;

    nrf_uarte_event_clear(p, NRF_UARTE_EVENT_TXSTOPPED);
    nrf_uarte_tx_buffer_set(p, &g_tx_byte, 1);
    nrf_uarte_enable(p);
    nrf_uarte_task_trigger(p, NRF_UARTE_TASK_STARTTX);

    while (!nrf_uarte_event_check(p, NRF_UARTE_EVENT_TXSTOPPED)) {
    }

    nrf_uarte_disable(p);
}

void serialBegin(uint32_t baud)
{
    nrf_uarte_config_t cfg = {
        .parity = NRF_UARTE_PARITY_EXCLUDED,
        .hwfc   = NRF_UARTE_HWFC_DISABLED,
    };

    uart_clock_start();
    uart_pins_prepare();

    nrf_uarte_disable(NRF_UARTE20);
    nrf_uarte_baudrate_set(NRF_UARTE20, map_baud(baud));
    nrf_uarte_configure(NRF_UARTE20, &cfg);
    nrf_uarte_txrx_pins_set(NRF_UARTE20, UART_TX_PIN, UART_RX_PIN);

    uarte_tx_path_prime(NRF_UARTE20);

    g_uart_ok = true;
}

void serialWrite(const uint8_t *buf, size_t len)
{
    if (!g_uart_ok || buf == NULL || len == 0u) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        uarte_putc(buf[i]);
    }
}

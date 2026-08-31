#include "hal_uart.h"
#include "board.h"
#include <hal/nrf_gpio.h>
#include <hal/nrf_uarte.h>
#include <nrf.h>
#include <nrfx_clock.h>
#include <string.h>

/* ── TX ring buffer (DMA 从 ring 直接取数据, 零额外拷贝) ──────────────── */

#define UART_TX_RING_SIZE 512U
#define UART_TX_RING_MASK (UART_TX_RING_SIZE - 1U)

static uint8_t  g_tx_ring[UART_TX_RING_SIZE];
static volatile uint16_t g_tx_head;   /* ISR 消费位置 */
static volatile uint16_t g_tx_tail;   /* serialWrite 生产位置 */
static volatile bool     g_tx_running;

/* ── ring 辅助 ──────────────────────────────────────────────────────── */

static inline uint16_t ring_used(void)
{
    return (g_tx_tail - g_tx_head) & UART_TX_RING_MASK;
}

static inline uint16_t ring_free(void)
{
    return UART_TX_RING_SIZE - 1U - ring_used();
}

static inline uint16_t ring_contig_from(uint16_t pos)
{
    /* 从 pos 到 tail 的连续字节数 (不跨 ring 末尾) */
    uint16_t tail = g_tx_tail;
    if (tail >= pos) return tail - pos;
    return UART_TX_RING_SIZE - pos;
}

/* ── 波特率映射 ─────────────────────────────────────────────────────── */

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

/* ── 时钟 & 引脚 ────────────────────────────────────────────────────── */

static void uart_clock_start(void)
{
    if (!nrfx_clock_init_check()) {
        if (nrfx_clock_init(NULL) != NRFX_SUCCESS) return;
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

/* ── DMA 引擎: 从 ring 直接取数据喂给 EasyDMA ──────────────────────── */

static void uart_dma_feed(void)
{
    uint16_t head = g_tx_head;
    uint16_t n    = ring_contig_from(head);

    if (n == 0) {
        g_tx_running = false;
        nrf_uarte_disable(NRF_UARTE20);
        return;
    }

    nrf_uarte_event_clear(NRF_UARTE20, NRF_UARTE_EVENT_ENDTX);
    nrf_uarte_tx_buffer_set(NRF_UARTE20, &g_tx_ring[head], n);
    g_tx_head = (head + n) & UART_TX_RING_MASK;
    nrf_uarte_task_trigger(NRF_UARTE20, NRF_UARTE_TASK_STARTTX);
}

static void uart_dma_kick(void)
{
    g_tx_running = true;
    nrf_uarte_enable(NRF_UARTE20);
    uart_dma_feed();
}

/* ── UARTE20 ISR ────────────────────────────────────────────────────── */

void SERIAL20_IRQHandler(void)
{
    if (nrf_uarte_event_check(NRF_UARTE20, NRF_UARTE_EVENT_ENDTX)) {
        uart_dma_feed();
    }
}

/* ── public API ─────────────────────────────────────────────────────── */

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

    nrf_uarte_int_enable(NRF_UARTE20, NRF_UARTE_INT_ENDTX_MASK);
    NVIC_ClearPendingIRQ(UARTE20_IRQn);
    NVIC_SetPriority(UARTE20_IRQn, 6);
    NVIC_EnableIRQ(UARTE20_IRQn);

    g_tx_head    = 0;
    g_tx_tail    = 0;
    g_tx_running = false;
}

void serialWrite(const uint8_t *buf, size_t len)
{
    if (buf == NULL || len == 0) return;

    while (len > 0) {
        while (ring_free() == 0) {
            /* ring 满, 等 ISR 消费 */
        }

        uint16_t tail  = g_tx_tail;
        uint16_t space = UART_TX_RING_SIZE - tail;
        uint16_t chunk = len;
        if (chunk > ring_free()) chunk = ring_free();
        if (chunk > space) chunk = space;

        memcpy(&g_tx_ring[tail], buf, chunk);
        g_tx_tail = (tail + chunk) & UART_TX_RING_MASK;
        buf += chunk;
        len -= chunk;

        /* 关中断避免与 ISR 竞态: ISR 可能在 ring 非空时刚设 running=false */
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        if (!g_tx_running && ring_used() > 0) {
            uart_dma_kick();
        }
        __set_PRIMASK(primask);
    }
}

void serialFlush(void)
{
    while (g_tx_running || ring_used() > 0) {
        /* 等待 DMA + ring 全部消费完 */
    }
}

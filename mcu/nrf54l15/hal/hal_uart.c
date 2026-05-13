#include "hal_uart.h"

/* Phase 1: UARTE on nRF54L15 uses EasyDMA (DMA-based TX/RX).
 * RTT is the primary console via rtt_write() in rt_hw_console_output.
 * UART will be properly initialized in Phase 2 when needed. */

void hal_uart_init(void) {}

void hal_uart_write(const char *str, size_t len)
{
    (void)str;
    (void)len;
}

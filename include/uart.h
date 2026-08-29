/*
 * Minimal UARTE20 TX logger for the Seeed XIAO nRF54L15.
 * TX = P1.09, routed to the onboard SAMD11 USB-serial bridge. 115200 8N1.
 */
#ifndef UART_H
#define UART_H

#include <stdint.h>

void uart_init(void);
void uart_write(const void *buf, uint32_t len);

/* Tiny printf: supports %s %c %d %u %x %02x (and %% ). */
void uprintf(const char *fmt, ...);

#endif /* UART_H */

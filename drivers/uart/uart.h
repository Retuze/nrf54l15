/*
 * Minimal UARTE20 TX logger for the Seeed XIAO nRF54L15.
 * TX = P1.09, routed to the onboard SAMD11 USB-serial bridge. 115200 8N1.
 *
 * 纯芯片驱动：只提供 uart_init/uart_write。文本输出统一走 picolibc printf
 * （posix-console → write(1) → drivers/core/syscalls.c 的强 write() →
 * uart_write）；裸字节发送用 uart_write 本身。
 */
#ifndef UART_H
#define UART_H

#include <stdint.h>

void uart_init(void);
void uart_write(const void *buf, uint32_t len);

#endif /* UART_H */

#ifndef HAL_UART_H
#define HAL_UART_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 硬件串口 API — UARTE20, EasyDMA + 中断驱动 */
void serialBegin(uint32_t baud);
void serialWrite(const uint8_t *buf, size_t len);
void serialFlush(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_UART_H */

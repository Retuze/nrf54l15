#ifndef HAL_UART_H
#define HAL_UART_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Arduino 风格串口 API */
void serialBegin(uint32_t baud);
void serialWrite(const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* HAL_UART_H */

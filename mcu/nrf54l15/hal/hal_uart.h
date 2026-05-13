#ifndef HAL_UART_H
#define HAL_UART_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void hal_uart_init(void);
void hal_uart_write(const char *str, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* HAL_UART_H */

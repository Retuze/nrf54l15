#ifndef HAL_DELAY_H
#define HAL_DELAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void delay_us(uint32_t us);
void delay_ms(uint32_t ms);

/* Arduino-compatible aliases */
static inline void delayMicroseconds(uint32_t us) { delay_us(us); }
static inline void delay(uint32_t ms) { delay_ms(ms); }

/* Returns milliseconds since boot */
uint32_t millis(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_DELAY_H */

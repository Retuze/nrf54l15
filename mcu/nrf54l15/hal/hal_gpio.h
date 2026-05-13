#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pin encoding: (port * 32 + pin_number) */
#define PIN_P0(n)  ((0 * 32) + (n))
#define PIN_P1(n)  ((1 * 32) + (n))
#define PIN_P2(n)  ((2 * 32) + (n))

#define PIN_PORT(p)  ((p) / 32)
#define PIN_NUM(p)   ((p) % 32)

enum PinMode {
    INPUT           = 0,
    OUTPUT          = 1,
    INPUT_PULLUP    = 2,
    INPUT_PULLDOWN  = 3,
};

enum PinValue {
    LOW  = 0,
    HIGH = 1,
};

void pinMode(uint32_t pin, int mode);
void digitalWrite(uint32_t pin, int value);
int  digitalRead(uint32_t pin);

#ifdef __cplusplus
}
#endif

#endif /* HAL_GPIO_H */

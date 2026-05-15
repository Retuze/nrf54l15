#ifndef HAL_PWM_H
#define HAL_PWM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Arduino 风格 PWM 输出，duty 0-255（0=关，255=最大亮度）。
 * 首次调用自动初始化 PWM 硬件，后续调用原地更新占空比。 */
void analogWrite(uint32_t pin, int duty);

/* 释放 PWM 对引脚的占用，恢复 GPIO 控制。 */
void analogWriteRelease(uint32_t pin);

#ifdef __cplusplus
}
#endif

#endif /* HAL_PWM_H */

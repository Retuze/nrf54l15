/*
 * Minimal GPIO driver for nRF54L15, Arduino-style API.
 *
 * 引脚用线性编号（Arduino 惯例）：GPIO_PIN(port, pin) = port<<5 | pin，
 * 板头（boards/）仍只给裸 PORT/PIN 宏，用 GPIO_PIN() 组合。
 *
 * 54L 的 P0/P1/P2 是三个独立的 GPIO 寄存器组（NRF_Pn_S，不是 nRF52 的
 * 单一大寄存器组），映射在 gpio.c 内部做，调用方无感。
 *
 * gpio_write 假定引脚已配为输出（Arduino digitalWrite 语义的简化：
 * 本工程暂无"写输入脚=切上拉"的需求，需要时再加）。
 */
#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>

#define GPIO_PIN(port, pin) (((uint32_t)(port) << 5) | (uint32_t)(pin))

enum {
    GPIO_OUTPUT = 0,        /* DIR=Output, INPUT=Disconnect          */
    GPIO_INPUT  = 1,        /* DIR=Input,  INPUT=Connect, 无上下拉   */
    GPIO_INPUT_PULLUP = 2,  /* DIR=Input,  INPUT=Connect, 上拉       */
};

enum {
    GPIO_LOW  = 0,
    GPIO_HIGH = 1,
};

void gpio_mode(uint32_t pin, int mode);
void gpio_write(uint32_t pin, int level);
int  gpio_read(uint32_t pin);          /* 未初始化时读到的电平不保证；非法 port 返回 0 */

#endif /* GPIO_H */

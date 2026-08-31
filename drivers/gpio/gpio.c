#include "gpio.h"
#include "nrf.h"   /* NRF_GPIO_Type / NRF_Pn_S（drivers 库 PUBLIC 传出 MDK 头） */

/*
 * port → 寄存器组映射。54L 的 GPIO 按 port 独立成组（NRF_P0_S/P1_S/P2_S），
 * 地址不连续，所以不能像 nRF52 那样用基址+偏移算。
 * DRIVE/SENSE 不碰（保持复位值：标准驱动、无 sense）。
 */
static NRF_GPIO_Type *regs_of(uint32_t port)
{
    switch (port) {
    case 0u: return NRF_P0_S;
    case 1u: return NRF_P1_S;
    case 2u: return NRF_P2_S;
    default: return 0;
    }
}

void gpio_mode(uint32_t pin, int mode)
{
    NRF_GPIO_Type *g = regs_of(pin >> 5);
    if (g == 0) {
        return;
    }
    uint32_t idx = pin & 31u;
    uint32_t cnf;

    switch (mode) {
    case GPIO_INPUT_PULLUP:
        cnf = (GPIO_PIN_CNF_DIR_Input         << GPIO_PIN_CNF_DIR_Pos) |
              (GPIO_PIN_CNF_INPUT_Connect     << GPIO_PIN_CNF_INPUT_Pos) |
              (GPIO_PIN_CNF_PULL_Pullup       << GPIO_PIN_CNF_PULL_Pos);
        break;
    case GPIO_INPUT:
        cnf = (GPIO_PIN_CNF_DIR_Input         << GPIO_PIN_CNF_DIR_Pos) |
              (GPIO_PIN_CNF_INPUT_Connect     << GPIO_PIN_CNF_INPUT_Pos) |
              (GPIO_PIN_CNF_PULL_Disabled     << GPIO_PIN_CNF_PULL_Pos);
        break;
    default:   /* GPIO_OUTPUT（与 01_conn 原 LED 初始化序列一致） */
        cnf = (GPIO_PIN_CNF_DIR_Output        << GPIO_PIN_CNF_DIR_Pos) |
              (GPIO_PIN_CNF_INPUT_Disconnect  << GPIO_PIN_CNF_INPUT_Pos);
        break;
    }
    g->PIN_CNF[idx] = cnf;
}

void gpio_write(uint32_t pin, int level)
{
    NRF_GPIO_Type *g = regs_of(pin >> 5);
    if (g == 0) {
        return;
    }
    uint32_t b = 1u << (pin & 31u);
    if (level) {
        g->OUTSET = b;
    } else {
        g->OUTCLR = b;
    }
}

int gpio_read(uint32_t pin)
{
    NRF_GPIO_Type *g = regs_of(pin >> 5);
    if (g == 0) {
        return 0;
    }
    return (g->IN >> (pin & 31u)) & 1u;
}

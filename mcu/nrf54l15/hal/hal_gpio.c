#include "hal_gpio.h"
#include <hal/nrf_gpio.h>

void pinMode(uint32_t pin, int mode)
{
    switch (mode) {
    case OUTPUT:
        nrf_gpio_cfg_output(pin);
        break;
    case INPUT:
        nrf_gpio_cfg_input(pin, NRF_GPIO_PIN_NOPULL);
        break;
    case INPUT_PULLUP:
        nrf_gpio_cfg_input(pin, NRF_GPIO_PIN_PULLUP);
        break;
    case INPUT_PULLDOWN:
        nrf_gpio_cfg_input(pin, NRF_GPIO_PIN_PULLDOWN);
        break;
    }
}

void digitalWrite(uint32_t pin, int value)
{
    nrf_gpio_pin_write(pin, value);
}

int digitalRead(uint32_t pin)
{
    return (int)nrf_gpio_pin_read(pin);
}

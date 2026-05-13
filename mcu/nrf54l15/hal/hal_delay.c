#include "hal_delay.h"
#include <soc/nrfx_coredep.h>
#include <rtthread.h>

void delay_us(uint32_t us)
{
    nrfx_coredep_delay_us(us);
}

void delay_ms(uint32_t ms)
{
    while (ms--) {
        nrfx_coredep_delay_us(1000);
    }
}

uint32_t millis(void)
{
    return rt_tick_get();
}

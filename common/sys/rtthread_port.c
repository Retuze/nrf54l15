#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <rtthread.h>
#include <rthw.h>

#include "board.h"
#include "hal_delay.h"
#include "hal_uart.h"
#include "rtt.h"

#include <nrf.h>
#include "system_nrf54l.h"

static rt_uint8_t g_rt_heap[RT_HEAP_SIZE] ALIGN(RT_ALIGN_SIZE);

void SysTick_Handler(void)
{
    rt_interrupt_enter();
    rt_tick_increase();
    rt_interrupt_leave();
}

void rt_hw_board_init(void)
{
    board_init();
    rt_system_heap_init(g_rt_heap, g_rt_heap + sizeof(g_rt_heap));

    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / RT_TICK_PER_SECOND);
}

void rt_hw_console_output(const char *str)
{
    if (str == NULL) return;

    size_t len = strlen(str);
    rtt_write(str, (uint32_t)len);
    hal_uart_write(str, len);
}

void rt_hw_us_delay(rt_uint32_t us)
{
    if (us == 0u) {
        return;
    }
    delayMicroseconds((uint32_t)us);
}

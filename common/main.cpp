/*
 * main.cpp - Firmware entry point (shared across all projects).
 */
#include <rtthread.h>

extern "C" void rt_hw_interrupt_disable(void);
extern "C" void rt_hw_board_init(void);
extern "C" void libcpp_threads_init(void);
extern "C" void app_init(void);

extern "C" int main(void)
{
    rt_hw_interrupt_disable();
    rt_hw_board_init();

    rt_show_version();
    rt_system_timer_init();
    rt_system_scheduler_init();
#ifdef RT_USING_SIGNALS
    rt_system_signal_init();
#endif
    libcpp_threads_init();

    app_init();

    rt_system_timer_thread_init();
    rt_thread_idle_init();

#ifdef RT_USING_SMP
    rt_hw_spin_lock(&_cpus_lock);
#endif
    rt_system_scheduler_start();
    return 0;
}

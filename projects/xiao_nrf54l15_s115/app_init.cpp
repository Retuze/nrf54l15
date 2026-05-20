#include "app_led_ctl.h"
#include "board.h"

#include <rtthread.h>
#include <print>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

static void app_thread_entry()
{
    app_led_ctl_init();
    std::print("Hello, {}!\n", "C++23");
    std::print("World!\n");

    int bat_tick = 0;
    for (;;) {
        board_btn_poll();
        board_led_poll();
        board_shell_poll();

        /* 电池采集: ~1s 一次 (100 × 10ms) */
        if (++bat_tick >= 100) {
            bat_tick = 0;
            board_battery_sample();
        }

        std::this_thread::sleep_for(10ms);
    }
}

// 该函数在线程调度前被调用，请勿在此做除了创建线程以外的操作
extern "C" void app_init(void)
{
    std::thread(app_thread_entry).detach();
}

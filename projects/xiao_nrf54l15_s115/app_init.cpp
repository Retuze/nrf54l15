#include "app_led_ctl.h"
#include "board.h"

#include <rtthread.h>
#include <stdio.h>
#include <print>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

static void app_thread_entry()
{
    app_led_ctl_init();
    std::print("Hello, {}!\n", "C++23");
    std::print("World!\n");

    for (;;) {
        board_btn_poll();
        board_led_poll();
        board_shell_poll();
        std::this_thread::sleep_for(10ms);
    }
}

// 该函数在线程调度前被调用，请勿在此做除了创建线程以外的操作
extern "C" void app_init(void)
{
    std::thread(app_thread_entry).detach();
}

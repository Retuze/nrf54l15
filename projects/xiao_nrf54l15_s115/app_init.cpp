#include "board.h"
#include <rtthread.h>

#include <thread>
#include <chrono>

using namespace std::chrono_literals;

static void app_thread_entry()
{
    for (;;) {
        rt_kprintf("hello world\r\n");
        std::this_thread::sleep_for(1000ms);
    }
}

extern "C" void app_init(void)
{
    std::thread(app_thread_entry).detach();
}

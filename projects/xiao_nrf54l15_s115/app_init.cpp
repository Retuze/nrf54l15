/*
 * Application initialization: pre-kernel bring-up, module registration,
 * and thread creation. Phase 1: no SoftDevice — just LED blink + shell.
 */
#include "app_led_ctl.h"
#include "board.h"

#include <rtthread.h>
#include <stdio.h>
#include <print>
#include <thread>

/* ---- thread entry points --------------------------------------------- */

static void app_thread_entry()
{
    app_led_ctl_init();
    std::print("Hello, {}!\n", "C++23");
    std::print("World!\n");

    for (;;) {
        board_btn_poll();
        board_led_poll();
        rt_thread_mdelay(10);
    }
}

/* ---- public entry ---------------------------------------------------- */

extern "C" void app_init(void)
{
    std::thread(app_thread_entry).detach();
}

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

extern "C" rt_base_t rt_hw_interrupt_disable(void);
extern "C" void rt_hw_interrupt_enable(rt_base_t level);
extern "C" void rt_hw_board_init(void);
extern "C" void libcpp_threads_init(void);
extern "C" int  shell_port_init(const char *greeting, const char *prompt);

/* ---- thread entry points --------------------------------------------- */

static void app_thread_entry()
{
    app_led_ctl_init();
    std::print("Hello, {}!\n", "C++23");
    std::print("Phase 1: nRFx GPIO test — LED should blink at 1Hz\n");

    for (;;) {
        board_btn_poll();
        board_led_poll();
        rt_thread_mdelay(10);
    }
}

/* ---- public entry ---------------------------------------------------- */

extern "C" void app_init(void)
{
    /*
     * Phase 1: nrfx + RT-Thread only, no BLE.
     * Phase 2: + MPSL init → SDC init → NimBLE init → LBS service.
     * libmpsl.a and libsoftdevice_controller_multirole.a are static
     * libraries linked directly into the firmware ELF.
     */

    shell_port_init("\r\n=== XIAO nRF54L15 (Phase 1: nRFx) ===\r\n", "nrf54> ");

    std::thread app_thread(app_thread_entry);
    app_thread.detach();
}

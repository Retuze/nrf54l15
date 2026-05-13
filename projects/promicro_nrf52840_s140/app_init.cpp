/*
 * Application initialization for ProMicro nRF52840 + S140.
 *
 * No user button on ProMicro -> no button poll, no LBS button notify.
 * LED patterns still work via BLE connection state and (future) charging state.
 */
#include "app_led_ctl.h"
#include "board.h"
#include "nrf52840.h"
#include "softdevice_port.h"
#include "services/lbs_app.h"

#include <rtthread.h>
#include <stdio.h>
#include <format>
#include <print>
#include <thread>

extern "C" rt_base_t rt_hw_interrupt_disable(void);
extern "C" void     rt_hw_interrupt_enable(rt_base_t level);
extern "C" void     rt_hw_board_init(void);
extern "C" void     libcpp_threads_init(void);
extern "C" int      shell_port_init(const char *greeting, const char *prompt);

/* ---- thread entry points --------------------------------------------- */

static void app_thread_entry()
{
    app_led_ctl_init();
    std::print("Hello, {}!\n", "ProMicro nRF52840");

    for (;;) {
        board_led_poll();
        rt_thread_mdelay(10);
    }
}

static void ble_thread_entry()
{
    if (!softdevice_port_is_enabled()) {
        std::print("ble_thread: SoftDevice not enabled, exiting\n");
        return;
    }
    std::print("softdevice event thread started\n");

    while (true) {
        (void)softdevice_port_wait_event(500u);
        softdevice_port_poll();
    }
}

/* ---- pre-kernel SoftDevice bring-up ---------------------------------- */

static int softdevice_bring_up_pre_kernel(void)
{
    const uint32_t systick_ctrl = SysTick->CTRL;
    SysTick->CTRL = 0;
    SCB->ICSR = (1UL << 25);

    const rt_base_t saved = rt_hw_interrupt_disable();
    rt_hw_interrupt_enable(0);

    int rc = softdevice_port_bring_up();
    if (rc != 0) {
        rt_hw_interrupt_enable(saved);
        SysTick->VAL  = 0;
        SysTick->CTRL = systick_ctrl;
        return rc;
    }

    rc = lbs_app_init();
    if (rc != 0) {
        std::print("LBS service init failed\n");
        rt_hw_interrupt_enable(saved);
        SysTick->VAL  = 0;
        SysTick->CTRL = systick_ctrl;
        return rc;
    }

    rc = softdevice_port_start_advertising();
    if (rc != 0) {
        std::print("softdevice advertising failed\n");
    } else {
        std::print("softdevice advertising started\n");
    }

    rt_hw_interrupt_enable(saved);
    SCB->ICSR = (1UL << 25);
    SysTick->VAL  = 0;
    SysTick->CTRL = systick_ctrl;
    return rc;
}

/* ---- public entry ---------------------------------------------------- */

extern "C" void app_init(void)
{
    softdevice_bring_up_pre_kernel();
    shell_port_init("\r\n=== ProMicro nRF52840 Shell ===\r\n", "nrf52> ");

    std::thread ble_thread(ble_thread_entry);
    ble_thread.detach();

    std::thread app_thread(app_thread_entry);
    app_thread.detach();
}

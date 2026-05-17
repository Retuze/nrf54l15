#include "hal_pwm.h"
#include "hal_gpio.h"
#include <hal/nrf_gpio.h>
#include <nrfx_pwm.h>
#include <nrfx_power.h>

/*
 * PWM20/21/22 仅支持 Port P1。P2 等其它端口请用 board 层亮度控制（见 board.c）。
 */
static nrfx_pwm_t         g_pwm       = NRFX_PWM_INSTANCE(20);
static nrf_pwm_sequence_t g_seq;
static uint16_t __attribute__((aligned(4))) g_duty_value;
static uint32_t           g_pin;
static bool               g_hw_init;

static bool pin_on_port1(uint32_t pin)
{
    return PIN_PORT(pin) == 1u;
}

static void pwm_hw_init(void)
{
    nrfx_pwm_config_t cfg = NRFX_PWM_DEFAULT_CONFIG(
        g_pin,
        NRF_PWM_PIN_NOT_CONNECTED,
        NRF_PWM_PIN_NOT_CONNECTED,
        NRF_PWM_PIN_NOT_CONNECTED
    );
    cfg.top_value = 255;
    cfg.base_clock = NRF_PWM_CLK_1MHz;

    nrfx_pwm_init(&g_pwm, &cfg, NULL, NULL);

    g_duty_value = 0;
    g_seq.values.p_raw = &g_duty_value;
    g_seq.length   = 1;
    g_seq.repeats  = 0;
    g_seq.end_delay = 0;

    g_hw_init = true;
}

void analogWrite(uint32_t pin, int duty)
{
    if (!pin_on_port1(pin)) {
        /* P2 等：由 board_led_set_pwm 直接 GPIO，此处忽略 */
        (void)duty;
        return;
    }

    uint8_t d = (uint8_t)(duty & 0xFF);

    if (!g_hw_init || g_pin != pin) {
        if (g_hw_init) {
            nrfx_pwm_stop(&g_pwm, true);
            nrfx_pwm_uninit(&g_pwm);
            g_hw_init = false;
        }
        g_pin = pin;
        pwm_hw_init();
    }

    g_duty_value = d;

    if (nrfx_pwm_stopped_check(&g_pwm)) {
        nrfx_pwm_simple_playback(&g_pwm, &g_seq, 1, NRFX_PWM_FLAG_LOOP);
    } else {
        nrfx_pwm_sequence_update(&g_pwm, 0, &g_seq);
        nrfx_pwm_sequence_update(&g_pwm, 1, &g_seq);
    }
}

void analogWriteRelease(uint32_t pin)
{
    (void)pin;
    if (!g_hw_init) {
        return;
    }
    nrfx_pwm_stop(&g_pwm, true);
    nrfx_pwm_uninit(&g_pwm);
    g_hw_init = false;
}

#include "hal_pwm.h"
#include <hal/nrf_gpio.h>
#include <nrfx_power.h>
#include <nrfx_pwm.h>

/* PWM20 instance, single channel */
static nrfx_pwm_t         g_pwm       = NRFX_PWM_INSTANCE(20);
static nrf_pwm_sequence_t g_seq;
static uint16_t __attribute__((aligned(4))) g_duty_value; /* EasyDMA 要求 32-bit 对齐 */
static uint32_t           g_pin;
static bool               g_initialized;

static void pwm_hw_init(void)
{
    /* PWM20 属于 PERI 电源域（低功耗外设，对应 P1），P2 属于 MCU 电源域（128MHz 高速 GPIO）。
     * 跨电源域使用 GPIO 需要 Constant Latency 子电源模式。 */
    nrfx_power_constlat_mode_request();

    /* CTRLSEL=GPIO：引脚由外设 PSEL 控制（同域也需要，跨域同理） */
    nrf_gpio_pin_control_select(g_pin, NRF_GPIO_PIN_SEL_GPIO);

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

    g_initialized = true;
}

void analogWrite(uint32_t pin, int duty)
{
    return;
    if (!g_initialized) {
        g_pin = pin;
        pwm_hw_init();
    }

    g_duty_value = (uint16_t)(duty & 0xFF);

    if (nrfx_pwm_stopped_check(&g_pwm)) {
        nrfx_pwm_simple_playback(&g_pwm, &g_seq, 1, NRFX_PWM_FLAG_LOOP);
    } else {
        nrfx_pwm_sequence_update(&g_pwm, 0, &g_seq);
        nrfx_pwm_sequence_update(&g_pwm, 1, &g_seq);
    }
}

void analogWriteRelease(uint32_t pin)
{
    return;
    (void)pin;
    if (!g_initialized) return;
    nrfx_pwm_stop(&g_pwm, true);
    nrfx_pwm_uninit(&g_pwm);
    nrfx_power_constlat_mode_free();
    g_initialized = false;
}

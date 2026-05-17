/*
 * board.c - Seeed Studio XIAO nRF54L15 board init (nRFx-based).
 *
 * Responsibilities:
 *   - SysTick for tick_ms baseline (GRTC reserved for Phase 2 MPSL/SDC)
 *   - RF antenna switch config
 *   - Onboard LED / KEY driver instances
 *   - RTT console init
 */
#include "board.h"
#include <stddef.h>
#include <rtthread.h>
#include "hal_delay.h"
#include "hal_gpio.h"
#include "hal_uart.h"
#include <hal/nrf_gpio.h>
#include <hal/nrf_timer.h>
#include <nrfx_power.h>
#include <nrfx_power_clock.h>
#include <nrf.h>
#include "rtt.h"
#include "shell.h"

void CLOCK_POWER_IRQHandler(void)
{
    nrfx_power_clock_irq_handler();
}

/* ---- Onboard LED instance --------------------------------------------- */

static led_indicator_t s_board_led;

/*
 * P2.00 软件 PWM：TIMER20 @ 1MHz，16 位 0..1023 → 载波 ~977Hz。
 * 单周期：COMPARE1(counter=PERIOD)→CLEAR/起点→亮；counter 增至 CC0→COMPARE0→灭；至 PERIOD 重复。
 * [25] errata：装 CC0 前清 COMPARE0 事件并 TASKS_CLEAR。
 * 上层 duty 0..255 为感知线性；LUT 为呼吸灯定制曲线（见 k_led_pwm_gamma_lut 注释）。
 */
#define LED_PWM_PERIOD   1023u

/* linear i → pwm：i=0 → 0；1≤i≤255 → round(254·(i/255)^2.2)+1 */
static const uint8_t k_led_pwm_gamma_lut[256] = {
      0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
      2,  2,  2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,
      4,  4,  4,  4,  4,  5,  5,  5,  5,  6,  6,  6,  6,  7,  7,  7,
      7,  8,  8,  8,  9,  9,  9, 10, 10, 10, 11, 11, 12, 12, 12, 13,
     13, 14, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20,
     21, 21, 22, 22, 23, 24, 24, 25, 25, 26, 27, 27, 28, 29, 29, 30,
     31, 31, 32, 33, 33, 34, 35, 36, 36, 37, 38, 39, 39, 40, 41, 42,
     43, 43, 44, 45, 46, 47, 48, 48, 49, 50, 51, 52, 53, 54, 55, 56,
     57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72,
     73, 74, 75, 77, 78, 79, 80, 81, 82, 84, 85, 86, 87, 88, 90, 91,
     92, 93, 95, 96, 97, 98,100,101,102,104,105,106,108,109,111,112,
    113,115,116,118,119,120,122,123,125,126,128,129,131,132,134,135,
    137,139,140,142,143,145,147,148,150,151,153,155,156,158,160,162,
    163,165,167,168,170,172,174,176,177,179,181,183,185,186,188,190,
    192,194,196,198,200,201,203,205,207,209,211,213,215,217,219,221,
    223,225,227,229,232,234,236,238,240,242,244,246,248,251,253,255,
};

static volatile uint8_t s_led_pwm_duty;     /* 当前周期（ISR） */
static volatile uint8_t s_led_pwm_pending; /* 线程写入，COMPARE1 时生效 */
static volatile bool    s_led_pwm_on;

static inline void led_pwm_pin_on(void)
{
#if LED_ACTIVE_LOW
    nrf_gpio_pin_write(LED_PIN, 0u);
#else
    nrf_gpio_pin_write(LED_PIN, 1u);
#endif
}

static inline void led_pwm_pin_off(void)
{
#if LED_ACTIVE_LOW
    nrf_gpio_pin_write(LED_PIN, 1u);
#else
    nrf_gpio_pin_write(LED_PIN, 0u);
#endif
}

static uint32_t led_pwm_duty_to_cc(uint8_t linear)
{
    const uint8_t duty = k_led_pwm_gamma_lut[linear];

    if (duty == 0u) {
        return 0u;
    }
    if (duty >= 255u) {
        return (uint32_t)LED_PWM_PERIOD;
    }
    uint32_t cc = ((uint32_t)duty * (uint32_t)LED_PWM_PERIOD + 127u) / 255u;
    if (cc == 0u) {
        cc = 1u;
    }
    return cc;
}

/* 周期起点装 CC0（COMPARE1 ISR / 首次 START）；线程只写 pending，经 board_led_set_pwm */
static void led_pwm_hw_apply(uint8_t duty)
{
    const uint32_t cc = led_pwm_duty_to_cc(duty);

    if (cc == 0u) {
        led_pwm_pin_off();
    } else {
        led_pwm_pin_on();
    }

    nrf_timer_event_clear(NRF_TIMER20, NRF_TIMER_EVENT_COMPARE0);
    NRF_TIMER20->TASKS_CLEAR = 1;
    s_led_pwm_duty = duty;
    nrf_timer_cc_set(NRF_TIMER20, NRF_TIMER_CC_CHANNEL0, cc);
}

void TIMER20_IRQHandler(void)
{
    if (nrf_timer_event_check(NRF_TIMER20, NRF_TIMER_EVENT_COMPARE1)) {
        nrf_timer_event_clear(NRF_TIMER20, NRF_TIMER_EVENT_COMPARE1);
        led_pwm_hw_apply(s_led_pwm_pending);
    }
    if (nrf_timer_event_check(NRF_TIMER20, NRF_TIMER_EVENT_COMPARE0)) {
        nrf_timer_event_clear(NRF_TIMER20, NRF_TIMER_EVENT_COMPARE0);
        if (s_led_pwm_duty > 0u && s_led_pwm_duty < 255u) {
            led_pwm_pin_off();
        }
    }
}

static void board_led_set_pwm(void *ctx,uint8_t duty)
{
    s_led_pwm_pending = duty;

    if (!s_led_pwm_on) {
        nrf_gpio_cfg_output(LED_PIN);

        nrf_timer_mode_set(NRF_TIMER20, NRF_TIMER_MODE_TIMER);
        nrf_timer_bit_width_set(NRF_TIMER20, NRF_TIMER_BIT_WIDTH_16);
        nrf_timer_prescaler_set(NRF_TIMER20, NRF_TIMER_FREQ_1MHz);
        nrf_timer_cc_set(NRF_TIMER20, NRF_TIMER_CC_CHANNEL1, LED_PWM_PERIOD);
        nrf_timer_shorts_enable(NRF_TIMER20, NRF_TIMER_SHORT_COMPARE1_CLEAR_MASK);
        nrf_timer_shorts_disable(NRF_TIMER20, NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK);
        nrf_timer_event_clear(NRF_TIMER20, NRF_TIMER_EVENT_COMPARE0);
        nrf_timer_event_clear(NRF_TIMER20, NRF_TIMER_EVENT_COMPARE1);
        nrf_timer_int_enable(NRF_TIMER20,
                             NRF_TIMER_INT_COMPARE0_MASK | NRF_TIMER_INT_COMPARE1_MASK);

        NVIC_ClearPendingIRQ(TIMER20_IRQn);
        NVIC_SetPriority(TIMER20_IRQn, 6);
        NVIC_EnableIRQ(TIMER20_IRQn);

        led_pwm_hw_apply(s_led_pwm_pending);
        nrf_timer_task_trigger(NRF_TIMER20, NRF_TIMER_TASK_START);
        s_led_pwm_on = true;
    }
}

static void board_led_set_on(void *ctx, bool on)
{
    board_led_set_pwm(ctx,on ? 255u : 0u);
}

led_indicator_t *board_led_get(void) { return &s_board_led; }
void board_led_poll(void)            { led_indicator_poll(&s_board_led); }

/* ---- Onboard button instance ------------------------------------------ */

static button_t s_board_btn;

static bool board_button_read_pressed(void *ctx)
{
    (void)ctx;
#if KEY_ACTIVE_LOW
    return digitalRead(KEY_PIN) == LOW;
#else
    return digitalRead(KEY_PIN) == HIGH;
#endif
}

bool board_user_key_pressed(void)
{
    return board_button_read_pressed(NULL);
}

button_t *board_btn_get(void)        { return &s_board_btn; }
void board_btn_set_callbacks(const button_callbacks_t *cbs) { button_set_callbacks(&s_board_btn, cbs); }
void board_btn_poll(void)            { button_poll(&s_board_btn); }
bool board_btn_is_pressed(void)      { return button_is_pressed(&s_board_btn); }

/* ---- Shell instance (RTT-backed) --------------------------------------- */

static shell_t g_shell;

static void shell_rtt_output(const void *buf, size_t len)
{
    rtt_write((const char *)buf, (uint32_t)len);
}

shell_t *board_shell_get(void)
{
    return &g_shell;
}

bool board_shell_poll(void)
{
    char buf[256];
    uint32_t n = rtt_read(buf, sizeof(buf));
    for (uint32_t i = 0; i < n; i++)
        ringbuf_put(&g_shell.rb, (uint8_t)buf[i]);

    return shell_poll(&g_shell);
}

/* ---- tick_ms adapter (SysTick via rt_tick_get, 1ms resolution) -------- */

static uint32_t board_tick_ms(void *ctx)
{
    (void)ctx;
    return rt_tick_get();
}


void rt_hw_console_output(const char *str)
{
    extern int write(int fd, const void *buf, unsigned int len);
    (void)write(1, str, (unsigned int)rt_strlen(str));
}

/* ---- Init ------------------------------------------------------------- */
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

/*
 * GRTC initialization is deferred to Phase 2 MPSL/SDC init.
 * SysTick (rt_tick_get) provides the tick_ms baseline for Phase 1.
 */
void board_init(void)
{
    nrfx_power_config_t pwr_cfg = { .dcdcen = true };
    nrfx_power_init(&pwr_cfg);

    rtt_init();
    shell_init(&g_shell, shell_rtt_output);
    serialBegin(115200);
    {
        static const char boot_msg[] = "XIAO nRF54L15 UART ready\r\n";
        serialWrite((const uint8_t *)boot_msg, sizeof(boot_msg) - 1u);
    }

    /* ---- Onboard LED ------------------------------------------------- */
    pinMode(LED_PIN, OUTPUT);

    led_indicator_cfg_t led_cfg = {
        .set_on  = board_led_set_on,
        .set_pwm = board_led_set_pwm,
        .tick_ms = board_tick_ms,
        .ctx     = NULL,
    };
    led_indicator_init(&s_board_led, &led_cfg);

    /* ---- Onboard KEY ------------------------------------------------- */
#if KEY_ACTIVE_LOW
    pinMode(KEY_PIN, INPUT_PULLUP);
#else
    pinMode(KEY_PIN, INPUT_PULLDOWN);
#endif

    /* ---- RF antenna switch -------------------------------------------- */
    /*
     * XIAO nRF54L15 RF antenna switch:
     *   P2.03 = RF switch power → HIGH powers the SP3T switch
     *   P2.05 = RF switch select → LOW = PCB antenna, HIGH = IPEX/u.FL
     */
    pinMode(PIN_P2(3), OUTPUT);
    digitalWrite(PIN_P2(3), HIGH);
    pinMode(PIN_P2(5), OUTPUT);
    digitalWrite(PIN_P2(5), LOW);

    /* ---- Button instance ---------------------------------------------- */
    button_cfg_t btn_cfg = {
        .read_pressed = board_button_read_pressed,
        .tick_ms      = board_tick_ms,
        .ctx          = NULL,
    };
    button_init(&s_board_btn, &btn_cfg);
}

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
#include <nrf.h>
#include "rtt.h"

/* ---- Onboard LED instance --------------------------------------------- */

static led_indicator_t s_board_led;

static void board_led_set_on(void *ctx, bool on)
{
    (void)ctx;
#if LED_ACTIVE_LOW
    digitalWrite(LED_PIN, on ? LOW : HIGH);
#else
    digitalWrite(LED_PIN, on ? HIGH : LOW);
#endif
}

led_indicator_t *board_led_get(void) { return &s_board_led; }
void board_led_on(void)              { led_indicator_raw_on(&s_board_led); }
void board_led_off(void)             { led_indicator_raw_off(&s_board_led); }
void board_led_set(bool on)          { led_indicator_raw_set(&s_board_led, on); }
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

/* ---- tick_ms adapter (SysTick via rt_tick_get, 1ms resolution) -------- */

static uint32_t board_tick_ms(void *ctx)
{
    (void)ctx;
    return rt_tick_get();
}

/* ---- Init ------------------------------------------------------------- */

/*
 * GRTC initialization is deferred to Phase 2 MPSL/SDC init.
 * SysTick (rt_tick_get) provides the tick_ms baseline for Phase 1.
 */
void board_init(void)
{
    rtt_init();
    hal_uart_init();

    /* ---- Onboard LED ------------------------------------------------- */
    pinMode(LED_PIN, OUTPUT);

    led_indicator_cfg_t led_cfg = {
        .set_on  = board_led_set_on,
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

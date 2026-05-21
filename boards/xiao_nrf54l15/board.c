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
#include "hal_sd.h"
#include <hal/nrf_gpio.h>
#include <hal/nrf_timer.h>
#include <nrfx_power.h>
#include <nrfx_power_clock.h>
#include "opus_player.h"
#include "opus_loopback.h"
#include <nrfx_i2s.h>
#include <nrfx_saadc.h>
#include <nrf.h>
#include <math.h>
#include <string.h>
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

    /* ---- SD card ---------------------------------------------------- */
    // board_sd_init();

    /* Audio playback (I2S PCM / Opus) deferred to app_init() —
     * the RT-Thread priority table isn't ready yet. */
}

/* ---- I2S 回环测试 (Full-Duplex TX+RX) ---------------------------------- */

/*
 * I2S 配置 (Master, Full-Duplex):
 *   格式: I2S, 16-bit, left-aligned, left channel
 *   MCK:  4 MHz   (32MDIV8)
 *   LRCK: 4 MHz / 256 = 15.625 kHz (≈16 kHz 采样率)
 *   SCK:  LRCK × 32 = 500 kHz
 *
 * 引脚:
 *   D0 / P1.04 → SCK
 *   D1 / P1.05 → LRCK
 *   D2 / P1.06 → SDOUT  (TX)
 *   D3 / P1.07 → MCK
 *   D4 / P1.10 → SDIN   (RX) ← 用杜邦线连接 D2(SDOUT) → D4(SDIN)
 *
 * DMA 双缓冲 ping-pong, 每缓冲 256 samples.
 * 每完成一次传输比对 TX/RX 数据, 累计误码并周期打印.
 */
#define I2S_SINE_TABLE_SIZE 256u
#define I2S_BUF_WORDS       I2S_SINE_TABLE_SIZE
#define I2S_LOG_INTERVAL    64u  /* 每 64 次回调打印一次统计 */
#define I2S_DBG_DUMP_COUNT   1u   /* 前 N 次回调打印 TX/RX hex dump */

static nrfx_i2s_t  g_i2s = NRFX_I2S_INSTANCE(20);
static int16_t     g_sine_table[I2S_SINE_TABLE_SIZE];

/* TX 双缓冲 */
static uint32_t g_i2s_tx_a[I2S_BUF_WORDS] __attribute__((aligned(4)));
static uint32_t g_i2s_tx_b[I2S_BUF_WORDS] __attribute__((aligned(4)));
/* RX 双缓冲 */
static uint32_t g_i2s_rx_a[I2S_BUF_WORDS] __attribute__((aligned(4)));
static uint32_t g_i2s_rx_b[I2S_BUF_WORDS] __attribute__((aligned(4)));

static volatile bool g_i2s_running;

/* 回环统计 */
static volatile uint32_t g_i2s_cb_count;    /* 回调计数 */
static volatile uint32_t g_i2s_mismatches;  /* 累计误码 word 数 */
static volatile uint32_t g_i2s_total_words; /* 累计传输 word 数 */

static void i2s_fill_buffer(uint32_t *buf)
{
    for (uint16_t i = 0; i < I2S_BUF_WORDS; i++) {
        buf[i] = ((uint32_t)(uint16_t)g_sine_table[i]) << 16;
    }
}

static void i2s_data_handler(nrfx_i2s_buffers_t const *p_released,
                             uint32_t                   status)
{
    if (status & NRFX_I2S_STATUS_TRANSFER_STOPPED) {
        g_i2s_running = false;
        return;
    }

    /* 比对刚完成的 TX/RX 数据
     * I2S Full-Duplex 有 1 frame 固有延迟: TX[N] → RX[N+1].
     * 比对 TX[0..N-2] ↔ RX[1..N-1], 忽略 RX[0] (初值 0) 和 TX[N-1] (无对应 RX). */
    if (p_released && p_released->p_tx_buffer && p_released->p_rx_buffer) {
        #define I2S_CMP_COUNT (I2S_BUF_WORDS - 1)
        uint32_t local_mismatch = 0;
        for (uint16_t i = 0; i < I2S_CMP_COUNT; i++) {
            if (p_released->p_tx_buffer[i] != p_released->p_rx_buffer[i + 1]) {
                local_mismatch++;
            }
        }
        g_i2s_mismatches += local_mismatch;
        g_i2s_total_words += I2S_CMP_COUNT;
        g_i2s_cb_count++;

        /* 首次传输完成: 打印 TX/RX 前 20 words 的 hex dump，便于诊断 */
        if (g_i2s_cb_count <= I2S_DBG_DUMP_COUNT) {
            rt_kprintf("I2S cb#%u: TX[0..19] =",
                       (unsigned)g_i2s_cb_count);
            for (uint8_t j = 0; j < 20; j++) {
                rt_kprintf(" %08lX", p_released->p_tx_buffer[j]);
            }
            rt_kprintf("\n             RX[0..19] =");
            for (uint8_t j = 0; j < 20; j++) {
                rt_kprintf(" %08lX", p_released->p_rx_buffer[j]);
            }
            rt_kprintf("\n             mismatches=%u/%u (offset=1)\n",
                       (unsigned)local_mismatch, (unsigned)I2S_CMP_COUNT);
        }

        if ((g_i2s_cb_count % I2S_LOG_INTERVAL) == 0) {
            uint32_t ber_milli = g_i2s_total_words
                ? (uint32_t)((uint64_t)g_i2s_mismatches * 100000 / g_i2s_total_words)
                : 0;
            rt_kprintf("I2S loopback: %u/%u words BER=%u.%03u%%\n",
                       (unsigned)g_i2s_mismatches,
                       (unsigned)g_i2s_total_words,
                       (unsigned)(ber_milli / 1000),
                       (unsigned)(ber_milli % 1000));
        }
    } else if (!p_released) {
        g_i2s_cb_count++;
    }

    /* 乒乓切换: 将刚释放的 buffer 重新提交 */
    const uint32_t *next_tx;
    uint32_t       *next_rx;

    if (p_released && p_released->p_tx_buffer) {
        next_tx = p_released->p_tx_buffer;
        next_rx = (p_released->p_tx_buffer == g_i2s_tx_a)
                  ? g_i2s_rx_a : g_i2s_rx_b;
    } else {
        /* 首次回调: 提交 B 组 buffer */
        next_tx = g_i2s_tx_b;
        next_rx = g_i2s_rx_b;
    }

    nrfx_i2s_buffers_t next = {
        .p_tx_buffer = next_tx,
        .p_rx_buffer = next_rx,
        .buffer_size = I2S_BUF_WORDS,
    };
    nrfx_i2s_next_buffers_set(&g_i2s, &next);
}

void board_i2s_loopback_start(void)
{
    if (g_i2s_running) return;

    /* 生成正弦查找表 (16-bit signed, full swing) */
    for (uint16_t i = 0; i < I2S_SINE_TABLE_SIZE; i++) {
        double phase = 2.0 * 3.141592653589793 * (double)i / (double)I2S_SINE_TABLE_SIZE;
        g_sine_table[i] = (int16_t)(sin(phase) * 32767.0);
    }

    /* 预填充 TX 双缓冲, RX 缓冲清零 */
    i2s_fill_buffer(g_i2s_tx_a);
    i2s_fill_buffer(g_i2s_tx_b);
    for (uint16_t i = 0; i < I2S_BUF_WORDS; i++) {
        g_i2s_rx_a[i] = 0;
        g_i2s_rx_b[i] = 0;
    }

    nrfx_i2s_config_t cfg = NRFX_I2S_DEFAULT_CONFIG(
        I2S_SCK_PIN,
        I2S_LRCK_PIN,
        I2S_MCK_PIN,
        I2S_SDOUT_PIN,
        I2S_SDIN_PIN
    );
    cfg.sample_width = NRF_I2S_SWIDTH_16BIT;
    cfg.alignment    = NRF_I2S_ALIGN_LEFT;
    cfg.channels     = NRF_I2S_CHANNELS_LEFT;
    cfg.mck_setup    = NRF_I2S_MCK_32MDIV8;
    cfg.ratio        = NRF_I2S_RATIO_256X;
    cfg.irq_priority = 6;

    nrfx_err_t err = nrfx_i2s_init(&g_i2s, &cfg, i2s_data_handler);
    if (err != NRFX_SUCCESS) {
        rt_kprintf("I2S loopback init failed: %u\n", (unsigned)err);
        return;
    }

    g_i2s_cb_count    = 0;
    g_i2s_mismatches  = 0;
    g_i2s_total_words = 0;

    nrfx_i2s_buffers_t initial = {
        .p_tx_buffer = g_i2s_tx_a,
        .p_rx_buffer = g_i2s_rx_a,
        .buffer_size = I2S_BUF_WORDS,
    };
    err = nrfx_i2s_start(&g_i2s, &initial, 0);
    if (err != NRFX_SUCCESS) {
        rt_kprintf("I2S loopback start failed: %u\n", (unsigned)err);
        return;
    }

    g_i2s_running = true;
    rt_kprintf("I2S loopback started: LRCK=15.625kHz Full-Duplex\n");
    rt_kprintf("  Connect D2(SDOUT) -> D4(SDIN) with a jumper wire\n");
    rt_kprintf("  SCK=D0 LRCK=D1 SDOUT=D2 MCK=D3 SDIN=D4\n");
}

void board_i2s_stop(void)
{
    if (!g_i2s_running) return;
    nrfx_i2s_stop(&g_i2s);
    g_i2s_running = false;
    rt_kprintf("I2S stopped: total %u words, %u mismatches\n",
               (unsigned)g_i2s_total_words, (unsigned)g_i2s_mismatches);
}

/* ---- SD 卡初始化 ------------------------------------------------------- */

void board_sd_init(void)
{
    sd_err_t err = sd_init();
    if (err == SD_OK) {
        sd_get_info();
    } else {
        rt_kprintf("SD: init failed (err=%d) — check wiring D6=SCK D7=MOSI D8=MISO D9=CS\n",
                   (int)err);
    }
}

/* ---- Opus 音频播放 (I2S TX-only, D2/SDOUT) ----------------------------- */

void board_opus_play_start(void)
{
    opus_player_start();
}

void board_opus_play_stop(void)
{
    opus_player_stop();
}

void board_opus_loopback_start(void)
{
    opus_loopback_start();
}

/* ---- 电池电压测量 (SAADC, P1.14, TPS22916 开关 P1.15) ---------------- */

/*
 * 电路: 电池 → TPS22916 (P1.15 使能) → 1:2 分压 (1M+1M) → P1.14 (ADC).
 * 采集流程: P1.15 HIGH → 延时稳定 → SAADC 采样 (阻塞, <1ms) → P1.15 LOW.
 *
 * SAADC: 单端, 增益 1/3, 内部 1.024V 参考, 14-bit, 256x 过采样 (最高精度).
 * 量程: 0 ~ 3.072V (引脚) → 0 ~ 6.144V (电池).
 * 14-bit 单次范围 0..16383, 256x 过采样累加后 ≈ 0..4194303.
 *
 * 滑动窗口: 8 个样本取平均, 软件定时器 ~1s 周期.
 */
#define BAT_WINDOW_SIZE   8
#define BAT_GAIN_VAL      (1.0f / 3.0f)
#define BAT_VREF          1.024f
#define BAT_ADC_BITS      14
#define BAT_OVERSAMPLE    256
#define BAT_DIVIDER       2.0f

#define BAT_RES_MAX  ((1u << BAT_ADC_BITS) - 1)  /* 16383, 过采样不改变输出范围 */
#define BAT_MV_PER_LSB (BAT_VREF / BAT_GAIN_VAL / (float)(BAT_RES_MAX + 1) \
                        * BAT_DIVIDER * 1000.0f)  /* ≈ 0.375 mV/LSB */

static uint16_t g_bat_window[BAT_WINDOW_SIZE];
static uint8_t  g_bat_idx;
static uint8_t  g_bat_count;
static int      g_bat_mv;

/* 执行一次电池采集 + 滑窗 + 打印. 由 app 线程周期性调用. */
void board_battery_sample(void)
{
    static uint16_t sample;
    static bool     saadc_inited;

    if (!saadc_inited) {
        nrfx_err_t err = nrfx_saadc_init(NRFX_SAADC_DEFAULT_CONFIG_IRQ_PRIORITY);
        if (err != NRFX_SUCCESS && err != NRFX_ERROR_ALREADY) {
            rt_kprintf("[BAT] saadc init failed: %u\n", (unsigned)err);
            return;
        }

        nrfx_saadc_channel_t ch = {
            .channel_config = {
                .gain      = NRF_SAADC_GAIN1_3,
                .reference = NRF_SAADC_REFERENCE_INTERNAL,
#if NRF_SAADC_HAS_ACQTIME_ENUM
                .acq_time  = NRF_SAADC_ACQTIME_40US,
#else
                .acq_time  = 200,
#endif
                .mode      = NRF_SAADC_MODE_SINGLE_ENDED,
                .burst     = NRF_SAADC_BURST_DISABLED,
            },
            .pin_p         = BAT_ADC_PIN,
            .pin_n         = NRF_SAADC_INPUT_DISABLED,
            .channel_index = 0,
        };
        nrf_gpio_cfg_output(BAT_EN_PIN);
        nrf_gpio_pin_write(BAT_EN_PIN, 0);

        err = nrfx_saadc_channel_config(&ch);
        if (err != NRFX_SUCCESS) {
            rt_kprintf("[BAT] channel config failed: %u\n", (unsigned)err);
            return;
        }

        err = nrfx_saadc_simple_mode_set(1u << 0,
            NRF_SAADC_RESOLUTION_14BIT, NRF_SAADC_OVERSAMPLE_256X, NULL);
        if (err != NRFX_SUCCESS) {
            rt_kprintf("[BAT] simple_mode_set failed: %u\n", (unsigned)err);
            return;
        }

        saadc_inited = true;
    }

    /* 使能 TPS22916, 等稳定 */
    nrf_gpio_pin_write(BAT_EN_PIN, 1);
    for (volatile int i = 0; i < 200; i++) {}

    /* 阻塞采集: 过采样后只产生 1 个结果 */
    nrfx_saadc_buffer_set((nrf_saadc_value_t *)&sample, 1);
    nrfx_saadc_mode_trigger();

    /* 关断开关, 省电 */
    nrf_gpio_pin_write(BAT_EN_PIN, 0);

    uint16_t single = sample;

    /* 滑动窗口 */
    g_bat_window[g_bat_idx] = single;
    g_bat_idx = (g_bat_idx + 1) % BAT_WINDOW_SIZE;
    if (g_bat_count < BAT_WINDOW_SIZE) g_bat_count++;

    /* 窗口平均值 */
    uint32_t avg = 0;
    for (uint8_t i = 0; i < g_bat_count; i++) {
        avg += g_bat_window[i];
    }
    avg /= g_bat_count;
    g_bat_mv = (int)((float)avg * BAT_MV_PER_LSB + 0.5f);

    /* 打印 */
    rt_kprintf("[BAT] raw=0x%04X mv=%d avg_mv=%d (window=%u/%u)\n",
               (unsigned)single, (int)((float)single * BAT_MV_PER_LSB + 0.5f),
               g_bat_mv, (unsigned)g_bat_count, (unsigned)BAT_WINDOW_SIZE);
}

int board_battery_read_mv(void)
{
    return g_bat_mv;
}

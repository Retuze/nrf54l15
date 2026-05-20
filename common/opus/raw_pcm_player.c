/* Raw PCM I2S player — no Opus, just DMA. Used to verify I2S output.
 *
 * Loops raw_pcm_data[] continuously through I2S TX (D2/SDOUT).
 * 16 kHz sample rate via I2S at ~15.625 kHz LRCK (< 2.5% pitch error).
 */
#include "raw_pcm_player.h"
#include "raw_pcm.h"
#include "board.h"
#include <rtthread.h>
#include <nrfx_i2s.h>
#include <nrf.h>
#include <string.h>

#define BUF_SAMPLES  256u

static nrfx_i2s_t g_i2s = NRFX_I2S_INSTANCE(20);
static uint32_t __attribute__((aligned(4))) g_tx_a[BUF_SAMPLES];
static uint32_t __attribute__((aligned(4))) g_tx_b[BUF_SAMPLES];
static volatile uint32_t g_pcm_idx;
static volatile bool g_running;

/* Fill an I2S TX buffer from raw_pcm_data[], wrapping. */
static void fill_buf(uint32_t *tx)
{
    for (uint32_t i = 0; i < BUF_SAMPLES; i++) {
        tx[i] = ((uint32_t)(uint16_t)raw_pcm_data[g_pcm_idx]) << 16;
        g_pcm_idx++;
        if (g_pcm_idx >= RAW_PCM_SAMPLE_COUNT)
            g_pcm_idx = 0;
    }
}

static void i2s_handler(nrfx_i2s_buffers_t const *p_released, uint32_t status)
{
    if (status & NRFX_I2S_STATUS_TRANSFER_STOPPED) {
        g_running = false;
        return;
    }

    uint32_t *next_tx;
    if (p_released && p_released->p_tx_buffer)
        next_tx = (uint32_t *)p_released->p_tx_buffer;
    else
        next_tx = g_tx_b;

    fill_buf(next_tx);

    nrfx_i2s_buffers_t next = {
        .p_tx_buffer = next_tx,
        .p_rx_buffer = NULL,
        .buffer_size = BUF_SAMPLES,
    };
    nrfx_i2s_next_buffers_set(&g_i2s, &next);
}

bool raw_pcm_player_start(void)
{
    if (g_running) return true;

    nrfx_i2s_config_t cfg = NRFX_I2S_DEFAULT_CONFIG(
        I2S_SCK_PIN, I2S_LRCK_PIN, I2S_MCK_PIN, I2S_SDOUT_PIN,
        NRF_I2S_PIN_NOT_CONNECTED
    );
    cfg.sample_width = NRF_I2S_SWIDTH_16BIT;
    cfg.alignment    = NRF_I2S_ALIGN_LEFT;
    cfg.channels     = NRF_I2S_CHANNELS_LEFT;
    cfg.mck_setup    = NRF_I2S_MCK_32MDIV8;
    cfg.ratio        = NRF_I2S_RATIO_256X;
    cfg.irq_priority = 6;

    nrfx_err_t err = nrfx_i2s_init(&g_i2s, &cfg, i2s_handler);
    if (err != NRFX_SUCCESS) {
        rt_kprintf("rawpcm: I2S init failed (%u)\n", (unsigned)err);
        return false;
    }

    memset(g_tx_a, 0, sizeof(g_tx_a));
    memset(g_tx_b, 0, sizeof(g_tx_b));
    g_pcm_idx = 0;

    /* Pre-fill first buffer */
    fill_buf(g_tx_a);

    nrfx_i2s_buffers_t initial = {
        .p_tx_buffer = g_tx_a,
        .p_rx_buffer = NULL,
        .buffer_size = BUF_SAMPLES,
    };
    err = nrfx_i2s_start(&g_i2s, &initial, 0);
    if (err != NRFX_SUCCESS) {
        rt_kprintf("rawpcm: I2S start failed (%u)\n", (unsigned)err);
        return false;
    }

    g_running = true;
    rt_kprintf("rawpcm: started (%u samples, %u Hz, loop)\n",
               (unsigned)RAW_PCM_SAMPLE_COUNT, (unsigned)RAW_PCM_SAMPLE_RATE);
    return true;
}

void raw_pcm_player_stop(void)
{
    if (!g_running) return;
    nrfx_i2s_stop(&g_i2s);
    g_running = false;
    rt_kprintf("rawpcm: stopped\n");
}

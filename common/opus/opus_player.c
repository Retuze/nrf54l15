/* Opus audio player via I2S TX-only (D2/SDOUT pin).
 *
 * Decodes embedded Opus packets on a background thread, feeds PCM to the
 * I2S peripheral through a ping-pong DMA double-buffer.
 *
 * The I2S runs at ~15.625 kHz LRCK (16-bit, left-aligned), close enough to
 * the 16 kHz Opus sample rate for a < 2.5% pitch error.
 */
#include "opus_player.h"
#include "opus.h"
#include "embedded_audio.h"
#include "board.h"
#include <rtthread.h>
#include <nrfx_i2s.h>
#include <nrf.h>
#include <string.h>

/* ---- Decoder state (static, aligned) ---------------------------------- */
/* opus_decoder_get_size(1) == 18468 bytes for mono at 16 kHz */
#define OPUS_DECODER_SIZE  18468
#define OPUS_ALIGN          8

static uint8_t __attribute__((aligned(OPUS_ALIGN))) s_dec_buf[OPUS_DECODER_SIZE];

/* ---- I2S config ------------------------------------------------------- */
#define I2S_BUF_SAMPLES  256u          /* DMA buffer depth in samples */
#define I2S_BUF_WORDS    I2S_BUF_SAMPLES

static nrfx_i2s_t g_i2s = NRFX_I2S_INSTANCE(20);

/* TX ping-pong buffers (no RX — we only send). */
static uint32_t __attribute__((aligned(4))) g_tx_a[I2S_BUF_WORDS];
static uint32_t __attribute__((aligned(4))) g_tx_b[I2S_BUF_WORDS];

/* ---- PCM ring buffer -------------------------------------------------- */
#define RING_SIZE  2048u                /* int16_t samples; must be power-of-two */
#define RING_MASK  (RING_SIZE - 1u)

static int16_t g_ring[RING_SIZE];
static volatile uint32_t g_ring_read;   /* ISR advances this */
static uint32_t g_ring_write;           /* decode thread advances this */

/* ---- Synchronisation -------------------------------------------------- */
static rt_sem_t g_sem = RT_NULL;

/* ---- State ------------------------------------------------------------ */
static volatile bool g_playing;

/* ---- Forward decls ---------------------------------------------------- */
static void i2s_tx_handler(nrfx_i2s_buffers_t const *p_released, uint32_t status);

/* ========================================================================
 * Ring buffer helpers
 * ======================================================================== */

/* Call only from thread context. */
static inline uint32_t ring_avail_read(void)
{
    return (g_ring_write - g_ring_read) & RING_MASK;
}

/* Call only from thread context. */
static inline uint32_t ring_avail_write(void)
{
    return RING_SIZE - ring_avail_read();
}

/* ISR pops samples from the ring and packs them into an I2S TX buffer.
 * Each I2S word is the 16-bit sample left-aligned in a 32-bit frame. */
static void ring_pop_isr(uint32_t *tx_buf, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        tx_buf[i] = ((uint32_t)(uint16_t)g_ring[g_ring_read]) << 16;
        g_ring_read = (g_ring_read + 1u) & RING_MASK;
    }
}

/* Thread pushes samples into the ring. Blocks if not enough space. */
static void ring_push(int16_t const *src, uint32_t count)
{
    while (count > 0) {
        /* Busy-wait for space (should never take long). */
        uint32_t avail;
        do {
            avail = ring_avail_write();
        } while (avail == 0);

        uint32_t n = avail < count ? avail : count;
        for (uint32_t i = 0; i < n; i++) {
            g_ring[g_ring_write] = src[i];
            g_ring_write = (g_ring_write + 1u) & RING_MASK;
        }
        src   += n;
        count -= n;
    }
}

/* ========================================================================
 * I2S data handler (called from ISR)
 * ======================================================================== */

static void i2s_tx_handler(nrfx_i2s_buffers_t const *p_released, uint32_t status)
{
    if (status & NRFX_I2S_STATUS_TRANSFER_STOPPED) {
        g_playing = false;
        return;
    }

    /* Pick the buffer that's just been released (or the second one on
     * first call where p_released->p_tx_buffer is NULL). */
    uint32_t *next_tx;
    if (p_released && p_released->p_tx_buffer) {
        next_tx = (uint32_t *)p_released->p_tx_buffer;
    } else {
        next_tx = g_tx_b;
    }

    /* Fill it from the ring. */
    ring_pop_isr(next_tx, I2S_BUF_SAMPLES);

    /* Re-submit it. */
    nrfx_i2s_buffers_t next_buf = {
        .p_tx_buffer = next_tx,
        .p_rx_buffer = NULL,           /* TX only */
        .buffer_size = I2S_BUF_WORDS,
    };
    nrfx_i2s_next_buffers_set(&g_i2s, &next_buf);

    /* Wake the decode thread. */
    if (g_sem) {
        rt_sem_release(g_sem);
    }
}

/* ========================================================================
 * Decode thread
 * ======================================================================== */

/* Static thread stack — avoids heap allocation from the 32 KB kernel heap. */
static rt_uint8_t __attribute__((aligned(8))) s_decode_stack[12288]; /* 12 KB */
static struct rt_thread s_decode_thread;

static void decode_thread_entry(void *arg)
{
    (void)arg;
    OpusDecoder *dec = (OpusDecoder *)s_dec_buf;

    /* Parse the length-prefixed packet format:
     *   uint16_le num_packets
     *   for each packet: uint16_le len + len bytes of raw Opus data
     */
    const uint8_t *p = embedded_opus_data;
    uint16_t total_packets = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    p += 2;

    uint16_t pkt_idx = 0;

    /* Pre-fill the ring with several decoded frames before starting I2S,
     * so that the DMA pipeline never starves. */
    float pcm[320]; /* 20 ms @ 16 kHz */
    uint32_t prefill_samples = RING_SIZE / 2; /* half the ring */

    while (ring_avail_read() < prefill_samples && pkt_idx < total_packets) {
        uint16_t pkt_len = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
        p += 2;

        int ret = opus_decode_float(dec, p, pkt_len, pcm, 320, 0);
        p += pkt_len;
        pkt_idx++;

        if (ret <= 0) continue; /* skip FEC/PLC/errors */

        /* Convert float -> int16 */
        int16_t buf[320];
        for (int i = 0; i < ret; i++) {
            float s = pcm[i] * 32768.0f;
            if (s > 32767.0f) s = 32767.0f;
            if (s < -32768.0f) s = -32768.0f;
            buf[i] = (int16_t)s;
        }

        /* Pad to 320 exactly if decoder returned fewer (shouldn't). */
        for (int i = ret; i < 320; i++)
            buf[i] = 0;

        ring_push(buf, 320);
    }

    /* Main decode loop: wake on semaphore, decode next frame, push to ring. */
    while (g_playing && g_sem) {
        /* Wait for ISR to tell us there's space. */
        rt_sem_take(g_sem, RT_WAITING_FOREVER);

        if (!g_playing) break;

        /* Decode packets while there's room for at least one frame. */
        while (ring_avail_write() >= 320 && pkt_idx < total_packets) {
            uint16_t pkt_len = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
            p += 2;

            int ret = opus_decode_float(dec, p, pkt_len, pcm, 320, 0);
            p += pkt_len;
            pkt_idx++;

            if (ret <= 0) continue;

            int16_t buf[320];
            for (int i = 0; i < ret; i++) {
                float s = pcm[i] * 32768.0f;
                if (s > 32767.0f) s = 32767.0f;
                if (s < -32768.0f) s = -32768.0f;
                buf[i] = (int16_t)s;
            }
            for (int i = ret; i < 320; i++)
                buf[i] = 0;

            ring_push(buf, 320);
        }

        /* Loop playback if we've exhausted the data. */
        if (pkt_idx >= total_packets) {
            p = embedded_opus_data + 2; /* reset to first packet */
            pkt_idx = 0;
            /* Reset decoder so it doesn't carry PLC state across loops. */
            opus_decoder_init(dec, EMBEDDED_AUDIO_SAMPLE_RATE, 1);
        }
    }
}

/* ========================================================================
 * Public API
 * ======================================================================== */

bool opus_player_start(void)
{
    if (g_playing) return true;

    /* ---- Init Opus decoder ------------------------------------------- */
    int err = opus_decoder_init((OpusDecoder *)s_dec_buf,
                                EMBEDDED_AUDIO_SAMPLE_RATE, 1);
    if (err != OPUS_OK) {
        rt_kprintf("opus: decoder init failed (%d)\n", err);
        return false;
    }

    /* ---- Init I2S (TX-only master) ----------------------------------- */
    nrfx_i2s_config_t cfg = NRFX_I2S_DEFAULT_CONFIG(
        I2S_SCK_PIN,
        I2S_LRCK_PIN,
        I2S_MCK_PIN,
        I2S_SDOUT_PIN,
        NRF_I2S_PIN_NOT_CONNECTED      /* SDIN — not used */
    );
    cfg.sample_width = NRF_I2S_SWIDTH_16BIT;
    cfg.alignment    = NRF_I2S_ALIGN_LEFT;
    cfg.channels     = NRF_I2S_CHANNELS_LEFT;
    cfg.mck_setup    = NRF_I2S_MCK_32MDIV8;
    cfg.ratio        = NRF_I2S_RATIO_256X;    /* LRCK ≈ 15.625 kHz */
    cfg.irq_priority = 6;

    nrfx_err_t nrf_err = nrfx_i2s_init(&g_i2s, &cfg, i2s_tx_handler);
    if (nrf_err != NRFX_SUCCESS) {
        rt_kprintf("opus: I2S init failed (%u)\n", (unsigned)nrf_err);
        return false;
    }

    /* ---- Create semaphore for ISR → thread signalling ---------------- */
    if (g_sem == RT_NULL) {
        g_sem = rt_sem_create("opus", 0, RT_IPC_FLAG_FIFO);
        if (g_sem == RT_NULL) {
            rt_kprintf("opus: sem create failed\n");
            return false;
        }
    }

    /* ---- Pre-fill TX buffers with silence ---------------------------- */
    memset(g_tx_a, 0, sizeof(g_tx_a));
    memset(g_tx_b, 0, sizeof(g_tx_b));
    g_ring_read  = 0;
    g_ring_write = 0;
    memset(g_ring, 0, sizeof(g_ring));

    /* ---- Start decode thread ----------------------------------------- */
    rt_err_t ret = rt_thread_init(
        &s_decode_thread,
        "opusd", decode_thread_entry, RT_NULL,
        s_decode_stack, sizeof(s_decode_stack),
        12,     /* priority: between main(10) and idle(31); smooth playback */
        5       /* tick */
    );
    if (ret != RT_EOK) {
        rt_kprintf("opus: thread init failed (%d)\n", ret);
        return false;
    }

    g_playing = true;
    rt_thread_startup(&s_decode_thread);

    /* ---- Start I2S --------------------------------------------------- */
    nrfx_i2s_buffers_t initial = {
        .p_tx_buffer = g_tx_a,
        .p_rx_buffer = NULL,
        .buffer_size = I2S_BUF_WORDS,
    };
    nrf_err = nrfx_i2s_start(&g_i2s, &initial, 0);
    if (nrf_err != NRFX_SUCCESS) {
        rt_kprintf("opus: I2S start failed (%u)\n", (unsigned)nrf_err);
        g_playing = false;
        return false;
    }

    rt_kprintf("opus: playback started, %u packets, %u Hz\n",
               (unsigned)EMBEDDED_AUDIO_PACKET_COUNT,
               (unsigned)EMBEDDED_AUDIO_SAMPLE_RATE);
    return true;
}

void opus_player_stop(void)
{
    if (!g_playing) return;
    nrfx_i2s_stop(&g_i2s);
    g_playing = false;
    rt_kprintf("opus: playback stopped\n");
}

bool opus_player_is_playing(void)
{
    return g_playing;
}

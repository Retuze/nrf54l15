/* Opus audio player via I2S TX-only (D2/SDOUT pin).
 *
 * Decodes embedded Opus packets on a background thread, feeds PCM to the
 * I2S peripheral through a ping-pong DMA double-buffer.
 *
 * I2S runs at ~15.625 kHz LRCK (16-bit, left-aligned, stereo).
 * Each 32-bit DMA word packs L[31:16] | R[15:0].
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
/*
 * Producer (decode thread) → ring buffer → Consumer (I2S ISR).
 *
 * Rate mismatch: I2S plays at 15.625 kHz, Opus decodes at 16 kHz.
 * The consumer is ~2.34% slower, so without throttling the ring grows
 * without bound.  Water-level control solves this:
 *
 *   RING_HIGH_WATER (1400): decoder pauses  — consumer has enough data
 *   RING_LOW_WATER   (400): decoder resumes — prevents underflow
 *
 * Buffer: 2048 int16_t = 128 ms at 16 kHz.
 * Each Opus frame = 320 samples (20 ms).  H−L gap = 1000 ≈ 3.1 frames.
 * ISR drains ~262 source samples per DMA callback (256 stereo frames × 128/125).
 * From H→L takes ~1000/262 ≈ 4 callbacks ≈ 65 ms @ 15.625 kHz.
 */
#define RING_SIZE        2048u
#define RING_MASK        (RING_SIZE - 1u)
#define RING_HIGH_WATER  1400u  /* decoder pauses above this */
#define RING_LOW_WATER    400u  /* decoder resumes below this */

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

/* Call from thread context (decode thread). */
static inline uint32_t ring_avail_read(void)
{
    return (g_ring_write - g_ring_read) & RING_MASK;
}

/* Call from thread context (decode thread). */
static inline uint32_t ring_avail_write(void)
{
    return RING_SIZE - ring_avail_read();
}

/* ISR pops source samples from the ring, packs them into I2S stereo DMA words.
 * STEREO mode: each 32-bit DMA word = L[31:16] | R[15:0].
 * I2S LRCK = 15.625 kHz, Opus decodes @ 16 kHz.  Ratio = 128/125.
 * Linear interpolation between adjacent source samples for smooth rate conversion. */
static void ring_pop_isr(uint32_t *tx_buf, uint32_t count)
{
    static uint8_t phase;  /* fractional source position, 125 units per input sample */

    for (uint32_t i = 0; i < count; i++) {
        int16_t s0 = g_ring[g_ring_read] >> 1;
        int16_t s1 = g_ring[(g_ring_read + 1u) & RING_MASK] >> 1;

        int16_t s_interp = (int16_t)(s0 + (int32_t)(s1 - s0) * (int32_t)phase / 125);
        uint32_t w = ((uint32_t)(uint16_t)s_interp << 16) | (uint32_t)(uint16_t)s_interp;

        tx_buf[i] = w;  /* L+R same mono sample in one stereo word */

        phase += 128;
        while (phase >= 125) {
            phase -= 125;
            g_ring_read = (g_ring_read + 1u) & RING_MASK;
        }
    }
}

/* Thread pushes samples into the ring.  Caller must ensure space via
 * ring_avail_write() before calling — watermark logic guarantees this. */
static void ring_push(int16_t const *src, uint32_t count)
{
    uint32_t n = count;
    while (n > 0) {
        uint32_t chunk = ring_avail_write();
        if (chunk > n) chunk = n;
        if (chunk == 0) {
            /* Should never happen with watermarks; yield as safety valve. */
            rt_thread_yield();
            continue;
        }
        for (uint32_t i = 0; i < chunk; i++) {
            g_ring[g_ring_write] = src[i];
            g_ring_write = (g_ring_write + 1u) & RING_MASK;
        }
        src += chunk;
        n   -= chunk;
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
static rt_uint8_t __attribute__((aligned(8))) s_decode_stack[24576]; /* 24 KB — decoder VLA */
static struct rt_thread s_decode_thread;

static void decode_thread_entry(void *arg)
{
    (void)arg;
    OpusDecoder *dec = (OpusDecoder *)s_dec_buf;

    /* Wire format: uint16_le num_packets, then per packet: uint16_le len + raw Opus data */
    const uint8_t *p = embedded_opus_data;
    uint16_t total_packets = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    p += 2;

    uint16_t pkt_idx = 0;
    float pcm[320]; /* 20 ms @ 16 kHz */

    /* ---- Phase 1: pre-fill ring to HIGH_WATER before I2S starts --------- */
    while (ring_avail_read() < RING_HIGH_WATER && pkt_idx < total_packets) {
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

    /* ---- Phase 2: main loop — water-level throttled decode -------------- */
    while (g_playing && g_sem) {
        rt_sem_take(g_sem, RT_WAITING_FOREVER);
        if (!g_playing) break;

        /* Decode frames while below high watermark and packets remain.
         * When the ISR drains the ring below HIGH_WATER, we refill to
         * HIGH_WATER and then stop — the consumer catches up over the
         * next ~65 ms (H→L takes ~4 DMA callbacks). */
        while (ring_avail_read() < RING_HIGH_WATER && pkt_idx < total_packets) {
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

        /* Loop playback when all packets consumed. */
        if (pkt_idx >= total_packets) {
            p = embedded_opus_data + 2;
            pkt_idx = 0;
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
    cfg.channels     = NRF_I2S_CHANNELS_STEREO;
    cfg.mck_setup    = NRF_I2S_MCK_32MDIV8;
    cfg.ratio        = NRF_I2S_RATIO_256X;    /* LRCK = 15.625 kHz */
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

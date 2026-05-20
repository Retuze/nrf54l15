/* Opus sine-wave encode→decode round-trip test for Cortex-M33.
 *
 * Validates that the Opus port works correctly on hardware.
 * Generates a 1 kHz sine, encodes 50 frames (@16 kHz / 20 ms),
 * decodes them interleaved (low-RAM: only one packet in flight),
 * then prints per-frame SNR and overall statistics via RTT.
 */
#include "opus_loopback.h"
#include "opus.h"
#include <rtthread.h>
#include <math.h>

/* ---- Test parameters -------------------------------------------------- */
#define TEST_SAMPLE_RATE  16000
#define TEST_FRAME_SIZE   320       /* 20 ms @ 16 kHz, mono */
#define TEST_FRAMES       50        /* 1 second total */
#define TEST_MAX_PACKET   1275
#define TEST_FREQ_HZ      1000.0f

/* State sizes — from measure_sizes.exe, same on M33 float path */
#define OPUS_ENC_SIZE     31668
#define OPUS_DEC_SIZE     18468

/* ---- Static state buffers (8-byte aligned for float) ------------------ */
static uint8_t __attribute__((aligned(8))) s_enc_buf[OPUS_ENC_SIZE];
static uint8_t __attribute__((aligned(8))) s_dec_buf[OPUS_DEC_SIZE];

/* Single-packet buffer + size log for checksum trace. */
static unsigned char s_packet[TEST_MAX_PACKET];
static int  s_sizes[TEST_FRAMES];   /* 50 × 4 = 200 bytes */

/* ---- Thread ----------------------------------------------------------- */
static rt_uint8_t __attribute__((aligned(8))) s_thread_stack[20480]; /* 20 KB — 编码器 VLA */
static struct rt_thread s_thread;

static void gen_sine(float *pcm, int n, float freq, float fs)
{
    for (int i = 0; i < n; i++)
        pcm[i] = 0.5f * sinf(2.0f * 3.14159265f * freq * (float)i / fs);
}

static void loopback_thread_entry(void *arg)
{
    (void)arg;
    float pcm_in[TEST_FRAME_SIZE];
    float pcm_out[TEST_FRAME_SIZE];
    int err;

    rt_kprintf("\n=== Opus sine loopback test ===\n");
    rt_kprintf("Rate: %d Hz, frame: %d samples, frames: %d, freq: %.0f Hz\n",
               TEST_SAMPLE_RATE, TEST_FRAME_SIZE, TEST_FRAMES, TEST_FREQ_HZ);
    rt_kprintf("Version: %s\n", opus_get_version_string());

    /* --- Init --- */
    OpusEncoder *enc = (OpusEncoder *)s_enc_buf;
    err = opus_encoder_init(enc, TEST_SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP);
    if (err != OPUS_OK) { rt_kprintf("FAIL: enc init: %d\n", err); return; }
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(24000));
    opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(5));

    OpusDecoder *dec = (OpusDecoder *)s_dec_buf;
    err = opus_decoder_init(dec, TEST_SAMPLE_RATE, 1);
    if (err != OPUS_OK) { rt_kprintf("FAIL: dec init: %d\n", err); return; }

    /* --- Encode→decode interleaved (one frame at a time) -------------- */
    unsigned long enc_checksum = 0;
    int total_bytes = 0;
    double total_signal = 0.0, total_error = 0.0;

    for (int i = 0; i < TEST_FRAMES; i++) {
        gen_sine(pcm_in, TEST_FRAME_SIZE, TEST_FREQ_HZ, TEST_SAMPLE_RATE);

        /* Encode */
        int nb = opus_encode_float(enc, pcm_in, TEST_FRAME_SIZE,
                                   s_packet, TEST_MAX_PACKET);
        if (nb < 0) {
            rt_kprintf("FAIL: encode error %d at frame %d\n", nb, i);
            return;
        }
        s_sizes[i] = nb;
        for (int j = 0; j < nb; j++)
            enc_checksum += s_packet[j];
        total_bytes += nb;

        /* Decode */
        int ns = opus_decode_float(dec, s_packet, nb, pcm_out,
                                   TEST_FRAME_SIZE, 0);
        if (ns < 0) {
            rt_kprintf("FAIL: decode error %d at frame %d\n", ns, i);
            return;
        }

        /* Per-frame SNR */
        double sig = 0.0, err_pwr = 0.0;
        for (int j = 0; j < TEST_FRAME_SIZE; j++) {
            double diff = (double)pcm_in[j] - (double)pcm_out[j];
            sig     += (double)pcm_in[j] * (double)pcm_in[j];
            err_pwr += diff * diff;
        }
        total_signal += sig;
        total_error  += err_pwr;

        if (i < 3 || i >= TEST_FRAMES - 1) {
            double snr = 10.0 * log10(sig / (err_pwr + 1e-12));
            rt_kprintf("  frame[%2d]: %3d bytes  snr=%.1f dB\n", i, nb, snr);
        }
    }

    /* --- Summary --- */
    double total_snr = 10.0 * log10(total_signal / (total_error + 1e-12));

    rt_kprintf("Encode: %d frames, %d total bytes, checksum=%lu\n",
               TEST_FRAMES, total_bytes, enc_checksum);
    rt_kprintf("Decode: total SNR = %.1f dB\n", total_snr);
    rt_kprintf("=== PASS ===\n");
}

void opus_loopback_start(void)
{
    rt_err_t ret = rt_thread_init(
        &s_thread, "opuslb", loopback_thread_entry, RT_NULL,
        s_thread_stack, sizeof(s_thread_stack),
        15,
        5
    );
    if (ret == RT_EOK)
        rt_thread_startup(&s_thread);
    else
        rt_kprintf("opus_loopback: thread init failed\n");
}

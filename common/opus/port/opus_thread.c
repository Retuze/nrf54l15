/* RT-Thread Opus integration example for Cortex-M33.
 *
 * Usage:
 *   - Place opus_thread.c in your RT-Thread application
 *   - Include the opus_cm33 library in your build (see CMakeLists.txt)
 *   - <rtthread.h> must be included BEFORE <opus.h> so the OVERRIDE_OPUS_ALLOC
 *     macros (defined via -D in CMake) can see rt_malloc/rt_free
 *   - Adjust stack size and buffer sizes based on channel count
 *
 * Measured state sizes (PC build, float path):
 *   Encoder mono:  31668 bytes (30 KB)
 *   Encoder stereo: 48684 bytes (47 KB)
 *   Decoder mono:  18468 bytes (18 KB)
 *   Decoder stereo: 27236 bytes (26 KB)
 */
#include <rtthread.h>
#include "opus.h"

/* ============================================================
 * Configuration
 * ============================================================ */
#define OPUS_SAMPLE_RATE    16000
#define OPUS_CHANNELS       1
#define OPUS_FRAME_SIZE     320     /* 20ms at 16kHz */
#define OPUS_MAX_PACKET     1275
#define OPUS_BITRATE        24000
#define OPUS_COMPLEXITY     5

/* State buffer sizes — verified by measure_sizes.c */
#define OPUS_ENC_SIZE       31668
#define OPUS_DEC_SIZE       18468

/* RT-Thread thread stack — must accommodate VLAs during encode/decode */
#define OPUS_THREAD_STACK   16384
#define OPUS_THREAD_PRIO    12
#define OPUS_THREAD_TICK    20

/* ============================================================
 * Static state buffers (aligned for float access)
 * ============================================================ */
static uint8_t __attribute__((aligned(8))) enc_state_buf[OPUS_ENC_SIZE];
static uint8_t __attribute__((aligned(8))) dec_state_buf[OPUS_DEC_SIZE];

static OpusEncoder *opus_enc;
static OpusDecoder *opus_dec;

/* ============================================================
 * PCM ring buffers (application-specific, adjust as needed)
 * ============================================================ */
static float pcm_tx_buf[OPUS_FRAME_SIZE];  /* mic in -> encode */
static float pcm_rx_buf[OPUS_FRAME_SIZE];  /* decode -> speaker out */
static unsigned char opus_packet[OPUS_MAX_PACKET];

/* ============================================================
 * Initialization
 * ============================================================ */
static int opus_init(void)
{
    int err;

    opus_enc = (OpusEncoder*)enc_state_buf;
    err = opus_encoder_init(opus_enc, OPUS_SAMPLE_RATE, OPUS_CHANNELS,
                            OPUS_APPLICATION_VOIP);
    if (err != OPUS_OK) {
        rt_kprintf("[opus] encoder init failed: %d\n", err);
        return -1;
    }

    opus_encoder_ctl(opus_enc, OPUS_SET_BITRATE(OPUS_BITRATE));
    opus_encoder_ctl(opus_enc, OPUS_SET_COMPLEXITY(OPUS_COMPLEXITY));
    opus_encoder_ctl(opus_enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));

    opus_dec = (OpusDecoder*)dec_state_buf;
    err = opus_decoder_init(opus_dec, OPUS_SAMPLE_RATE, OPUS_CHANNELS);
    if (err != OPUS_OK) {
        rt_kprintf("[opus] decoder init failed: %d\n", err);
        return -1;
    }

    rt_kprintf("[opus] init OK: enc=%d dec=%d bytes\n",
               OPUS_ENC_SIZE, OPUS_DEC_SIZE);
    return 0;
}

/* ============================================================
 * Encode: float PCM -> Opus packet
 * Returns packet size, or negative on error
 * ============================================================ */
static int opus_encode_frame(const float *pcm, unsigned char *packet, int max_len)
{
    int nb = opus_encode_float(opus_enc, pcm, OPUS_FRAME_SIZE, packet, max_len);
    if (nb < 0) {
        rt_kprintf("[opus] encode error: %d\n", nb);
    }
    return nb;
}

/* ============================================================
 * Decode: Opus packet -> float PCM
 * Returns number of samples decoded, or negative on error
 * ============================================================ */
static int opus_decode_frame(const unsigned char *packet, int len,
                              float *pcm, int frame_size)
{
    int ns = opus_decode_float(opus_dec, packet, len, pcm, frame_size, 0);
    if (ns < 0) {
        rt_kprintf("[opus] decode error: %d\n", ns);
    }
    return ns;
}

/* ============================================================
 * Main Opus thread
 * ============================================================ */
static void opus_thread_entry(void *parameter)
{
    if (opus_init() != 0) {
        rt_kprintf("[opus] init failed, thread exiting\n");
        return;
    }

    while (1) {
        /* --- Capture PCM from microphone / audio source --- */
        /* User fills pcm_tx_buf[] with OPUS_FRAME_SIZE float samples */

        /* --- Encode --- */
        int packet_len = opus_encode_frame(pcm_tx_buf, opus_packet,
                                            OPUS_MAX_PACKET);
        if (packet_len > 0) {
            /* --- Transmit opus_packet[] of packet_len bytes --- */
            /* User sends via network/radio/SPI/etc. */
        }

        /* --- Receive packet from network/radio --- */
        /* User fills opus_packet[] with packet_len bytes */

        /* --- Decode --- */
        int decoded = opus_decode_frame(opus_packet, packet_len,
                                         pcm_rx_buf, OPUS_FRAME_SIZE);
        if (decoded > 0) {
            /* --- Play pcm_rx_buf[] to speaker / audio sink --- */
            /* User outputs decoded PCM to DAC/I2S */
        }

        rt_thread_mdelay(OPUS_THREAD_TICK);
    }
}

/* ============================================================
 * Thread creation (call from application init)
 * ============================================================ */
int opus_thread_start(void)
{
    rt_thread_t tid;

    tid = rt_thread_create("opus",
                           opus_thread_entry,
                           RT_NULL,
                           OPUS_THREAD_STACK,
                           OPUS_THREAD_PRIO,
                           OPUS_THREAD_TICK);

    if (tid != RT_NULL) {
        rt_thread_startup(tid);
        rt_kprintf("[opus] thread started, stack=%d\n", OPUS_THREAD_STACK);
        return 0;
    }

    rt_kprintf("[opus] failed to create thread\n");
    return -1;
}

/* Export to RT-Thread's auto-init */
#ifdef RT_USING_COMPONENTS_INIT
#  include <rtthread.h>
INIT_APP_EXPORT(opus_thread_start);
#endif

/* Bit-exactness verification: trimmed port vs full Opus library.
 * Compile twice: once against full opus.lib, once against opus_cm33.lib,
 * and compare encoded/decoded output.
 */
#include "opus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FS          16000
#define FRAME_SIZE  320
#define DURATION    50
#define MAX_PACKET  1275

/* Generate a 1kHz sine wave */
static void gen_sine(float *pcm, int n, float freq, float fs)
{
    int i;
    for (i = 0; i < n; i++)
        pcm[i] = 0.5f * sinf(2.0f * 3.14159265f * freq * i / fs);
}

int main(int argc, char **argv)
{
    int use_trimmed = (argc > 1);
    float pcm_in[FRAME_SIZE];
    unsigned char packets[DURATION][MAX_PACKET];
    int packet_sizes[DURATION];
    float pcm_out[DURATION * FRAME_SIZE];
    int i, err;

    printf("Opus port verification — %s library\n",
           use_trimmed ? "TRIMMED" : "FULL");
    printf("Version: %s\n", opus_get_version_string());

    /* --- Encode --- */
    {
        int enc_size = opus_encoder_get_size(1);
        unsigned char *enc_buf = (unsigned char*)malloc(enc_size);
        OpusEncoder *enc = (OpusEncoder*)enc_buf;
        err = opus_encoder_init(enc, FS, 1, OPUS_APPLICATION_VOIP);
        if (err != OPUS_OK) { printf("FAIL: encoder init: %d\n", err); free(enc_buf); return 1; }
        opus_encoder_ctl(enc, OPUS_SET_BITRATE(24000));
        opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(5));

        for (i = 0; i < DURATION; i++) {
            gen_sine(pcm_in, FRAME_SIZE, 1000.0f, FS);
            packet_sizes[i] = opus_encode_float(enc, pcm_in, FRAME_SIZE,
                                                 packets[i], MAX_PACKET);
            if (packet_sizes[i] < 0) {
                printf("FAIL: encode error %d at frame %d\n", packet_sizes[i], i);
                free(enc_buf); return 1;
            }
        }
        printf("Encode OK: %d frames encoded\n", DURATION);
        free(enc_buf);
    }

    /* Print checksum of encoded data for external comparison */
    {
        unsigned long csum = 0;
        int total_bytes = 0;
        for (i = 0; i < DURATION; i++) {
            int j;
            for (j = 0; j < packet_sizes[i]; j++)
                csum += packets[i][j];
            total_bytes += packet_sizes[i];
        }
        printf("Encoded checksum: %lu (total %d bytes)\n", csum, total_bytes);
    }

    /* --- Decode --- */
    {
        int dec_size = opus_decoder_get_size(1);
        unsigned char *dec_buf = (unsigned char*)malloc(dec_size);
        OpusDecoder *dec = (OpusDecoder*)dec_buf;
        err = opus_decoder_init(dec, FS, 1);
        if (err != OPUS_OK) { printf("FAIL: decoder init: %d\n", err); free(dec_buf); return 1; }

        int total_samples = 0;
        for (i = 0; i < DURATION; i++) {
            int ns = opus_decode_float(dec, packets[i], packet_sizes[i],
                                        pcm_out + total_samples,
                                        FRAME_SIZE, 0);
            if (ns < 0) {
                printf("FAIL: decode error %d at frame %d\n", ns, i);
                free(dec_buf); return 1;
            }
            total_samples += ns;
        }
        printf("Decode OK: %d total samples\n", total_samples);

        /* Compute output checksum */
        double energy = 0;
        for (i = 0; i < total_samples; i++)
            energy += pcm_out[i] * pcm_out[i];
        printf("Output energy: %.6f\n", energy);

        free(dec_buf);
    }

    printf("PASS\n");
    return 0;
}

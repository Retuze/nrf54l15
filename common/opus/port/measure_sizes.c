/* Measure Opus encoder/decoder state sizes for Cortex-M33 port.
 * Compile against standard Opus library to get exact sizes.
 */
#include "opus.h"
#include <stdio.h>

int main(void)
{
    int enc_mono  = opus_encoder_get_size(1);
    int enc_stereo = opus_encoder_get_size(2);
    int dec_mono  = opus_decoder_get_size(1);
    int dec_stereo = opus_decoder_get_size(2);

    printf("=== Opus State Size Measurements ===\n");
    printf("opus_encoder_get_size(1) = %6d bytes (%d KB)\n", enc_mono,  enc_mono/1024);
    printf("opus_encoder_get_size(2) = %6d bytes (%d KB)\n", enc_stereo, enc_stereo/1024);
    printf("opus_decoder_get_size(1) = %6d bytes (%d KB)\n", dec_mono,  dec_mono/1024);
    printf("opus_decoder_get_size(2) = %6d bytes (%d KB)\n", dec_stereo, dec_stereo/1024);

    return 0;
}

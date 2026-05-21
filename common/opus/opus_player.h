#ifndef OPUS_PLAYER_H
#define OPUS_PLAYER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start Opus playback via I2S TX (D2/SDOUT pin).
 * The audio data must be in embedded_audio.h (embedded_opus_data + embedded_opus_size).
 * I2S runs at ~15.625 kHz LRCK, 16-bit, left-aligned, stereo.
 * Returns true on success. */
bool opus_player_start(void);

/* Stop playback. */
void opus_player_stop(void);

/* Returns true while playing. */
bool opus_player_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif /* OPUS_PLAYER_H */

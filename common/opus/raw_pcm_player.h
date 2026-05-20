#ifndef RAW_PCM_PLAYER_H
#define RAW_PCM_PLAYER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start raw PCM playback via I2S TX (D2/SDOUT).
 * Data from raw_pcm.h, 16 kHz mono 16-bit, loops. */
bool raw_pcm_player_start(void);
void raw_pcm_player_stop(void);

#ifdef __cplusplus
}
#endif

#endif

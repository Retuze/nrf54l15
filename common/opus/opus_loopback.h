#ifndef OPUS_LOOPBACK_H
#define OPUS_LOOPBACK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Run a sine-wave encode→decode round-trip test on a background thread.
 * Prints encoded checksum, decoded energy, and per-frame SNR via RTT.
 * Uses static encoder + decoder (~50 KB RAM). */
void opus_loopback_start(void);

#ifdef __cplusplus
}
#endif

#endif

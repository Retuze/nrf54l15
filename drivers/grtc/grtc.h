/*
 * Minimal GRTC (Global Real-time Counter) driver for nRF54L15, bare metal.
 *
 * The GRTC replaces the nRF52 RTC/TIMER as the low-power, always-on time
 * base. Its SYSCOUNTER is a 52-bit up-counter; on nRF54L15 it ticks at
 * 1 MHz, i.e. 1 count == 1 microsecond (verified empirically, see
 * scripts/grtc_rate.py).
 *
 * "now" is read with the CAPTURE->CC mechanism (domain-independent), so we
 * don't depend on the per-core SYSCOUNTER[] index.
 */

#ifndef GRTC_H
#define GRTC_H

#include <stdint.h>

/* Start the SYSCOUNTER. Call once after reset (HFXO not required). */
void grtc_init(void);

/* Current 52-bit microsecond timestamp. Monotonic, wraps after ~142 years. */
uint64_t grtc_now(void);

/* Busy-wait for the given number of microseconds. */
void grtc_delay_us(uint32_t us);

#endif /* GRTC_H */

/* Opus port configuration for ARM Cortex-M33 with RT-Thread
 *
 * This config is for the FLOATING-POINT path (M33 has FPv5-SP).
 * For a fixed-point build, define FIXED_POINT and use silk/fixed/ instead.
 */
#ifndef OPUS_PORT_CONFIG_H
#define OPUS_PORT_CONFIG_H

/* Opus uses its own static inline malloc/free wrappers in os_support.h.
 * These go through picolibc's malloc/free -> sbrk, using the 16 KB heap.
 * No overrides needed. */

#define PACKAGE_VERSION "1.6-port-cm33"

/* ============================================================
 * Float path (NOT fixed point) — uses M33 FPv5-SP hardware FPU
 * ============================================================ */
/* #undef FIXED_POINT */

/* ============================================================
 * Stack allocation: use C99 VLAs (fastest, simple).
 * Thread stack must be ≥ 48 KB for 16 kHz mono encode+decode.
 * ============================================================ */
#define VAR_ARRAYS 1

/* ============================================================
 * Math functions — available in newlib-nano
 * ============================================================ */
#define HAVE_LRINTF 1
#define HAVE_LRINT 1

/* ============================================================
 * No platform intrinsics (M33 has no NEON, no SSE, no AVX)
 * ============================================================ */
/* OPUS_ARM_MAY_HAVE_NEON intentionally not defined */
/* OPUS_ARM_PRESUME_NEON intentionally not defined */
/* OPUS_X86_* intentionally not defined */

/* ============================================================
 * Disable optional features to reduce code size
 * ============================================================ */
/* #undef ENABLE_DRED */
/* #undef ENABLE_OSCE */
/* #undef ENABLE_DEEP_PLC */
/* #undef ENABLE_QEXT */
/* #undef CUSTOM_MODES */
/* #undef ENABLE_ASSERTIONS */

/* ============================================================
 * Keep float API available
 * ============================================================ */
/* DISABLE_FLOAT_API is NOT defined */

/* ============================================================
 * No runtime CPU detection on embedded target
 * ============================================================ */
/* #undef OPUS_HAVE_RTCD */

#endif /* OPUS_PORT_CONFIG_H */

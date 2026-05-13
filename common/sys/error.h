/*
 * Unified error codes and macros shared across all modules.
 * Maps SoftDevice NRF_ERROR_* codes into app-level error_t with zero =
 * success, negative = error, positive = non-error status (e.g. timeout).
 */
#ifndef ERROR_H
#define ERROR_H

#include <stdint.h>
#include <nrf_error.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t error_t;

#define ERROR_OK           0
#define ERROR_TIMEOUT      1    /* non-error: operation timed out */
#define ERROR_NOT_READY    (-1)
#define ERROR_INVALID_PARAM (-2)
#define ERROR_NOT_FOUND    (-3)
#define ERROR_BUSY         (-4)
#define ERROR_NO_MEM       (-5)
#define ERROR_NOT_CONNECTED (-6)
#define ERROR_NOT_ENABLED  (-7)

/* Convert SoftDevice uint32_t error to app error_t. */
static inline error_t sd_to_error(uint32_t sd_err)
{
    if (sd_err == NRF_SUCCESS) {
        return ERROR_OK;
    }
    return -(int32_t)(sd_err & 0x7FFFFFFFu);
}

/*
 * Evaluate `call` (which returns uint32_t SoftDevice error code) and return
 * from the calling function with the converted error_t on failure.
 */
#define CHECK_SD(call)                                                         \
    do {                                                                       \
        uint32_t _sd_err = (call);                                             \
        if (_sd_err != NRF_SUCCESS) {                                          \
            return sd_to_error(_sd_err);                                       \
        }                                                                      \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* ERROR_H */

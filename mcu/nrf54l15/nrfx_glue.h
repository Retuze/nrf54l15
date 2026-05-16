#ifndef NRFX_GLUE_H__
#define NRFX_GLUE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ---- Assert ---- */
#define NRFX_ASSERT(expression)
#define NRFX_STATIC_ASSERT(expression) _Static_assert(expression, #expression)

/* ---- IRQ via CMSIS NVIC ---- */
#define NRFX_IRQ_PRIORITY_SET(irq_number, priority) \
    NVIC_SetPriority((IRQn_Type)(irq_number), (priority))
#define NRFX_IRQ_ENABLE(irq_number) \
    NVIC_EnableIRQ((IRQn_Type)(irq_number))
#define NRFX_IRQ_IS_ENABLED(irq_number) \
    (NVIC_GetEnableIRQ((IRQn_Type)(irq_number)) != 0)
#define NRFX_IRQ_DISABLE(irq_number) \
    NVIC_DisableIRQ((IRQn_Type)(irq_number))
#define NRFX_IRQ_PENDING_SET(irq_number) \
    NVIC_SetPendingIRQ((IRQn_Type)(irq_number))
#define NRFX_IRQ_PENDING_CLEAR(irq_number) \
    NVIC_ClearPendingIRQ((IRQn_Type)(irq_number))
#define NRFX_IRQ_IS_PENDING(irq_number) \
    (NVIC_GetPendingIRQ((IRQn_Type)(irq_number)) != 0)

/* ---- Critical sections (RT-Thread-aware) ---- */
#include <rtthread.h>
#include <rthw.h>

#define NRFX_CRITICAL_SECTION_ENTER() \
    do { rt_base_t __nrfx_crit = rt_hw_interrupt_disable()

#define NRFX_CRITICAL_SECTION_EXIT() \
    rt_hw_interrupt_enable(__nrfx_crit); } while (0)

/* ---- Delay (DWT-based, Cortex-M33 has DWT) ---- */
#include <soc/nrfx_coredep.h>
#define NRFX_DELAY_DWT_BASED 1
#define NRFX_DELAY_US(us_time) nrfx_coredep_delay_us(us_time)

/* ---- Atomic (compiler builtins) ---- */
typedef uint32_t nrfx_atomic_t;

#define NRFX_ATOMIC_FETCH_STORE(p_data, value) \
    __atomic_exchange_n((p_data), (value), __ATOMIC_SEQ_CST)
#define NRFX_ATOMIC_FETCH_OR(p_data, value) \
    __atomic_fetch_or((p_data), (value), __ATOMIC_SEQ_CST)
#define NRFX_ATOMIC_FETCH_AND(p_data, value) \
    __atomic_fetch_and((p_data), (value), __ATOMIC_SEQ_CST)
#define NRFX_ATOMIC_FETCH_XOR(p_data, value) \
    __atomic_fetch_xor((p_data), (value), __ATOMIC_SEQ_CST)
#define NRFX_ATOMIC_FETCH_ADD(p_data, value) \
    __atomic_fetch_add((p_data), (value), __ATOMIC_SEQ_CST)
#define NRFX_ATOMIC_FETCH_SUB(p_data, value) \
    __atomic_fetch_sub((p_data), (value), __ATOMIC_SEQ_CST)

static inline bool nrfx_atomic_cas(uint32_t *p_data, uint32_t old_value, uint32_t new_value)
{
    return __atomic_compare_exchange_n(p_data, &old_value, new_value,
                                       false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
#define NRFX_ATOMIC_CAS(p_data, old_value, new_value) \
    nrfx_atomic_cas((p_data), (old_value), (new_value))

/* ---- CLZ / CTZ ---- */
#define NRFX_CLZ(value) __builtin_clz(value)
#define NRFX_CTZ(value) __builtin_ctz(value)

/* ---- Error codes ---- */
#define NRFX_CUSTOM_ERROR_CODES 0

/* ---- Event readback ---- */
#define NRFX_EVENT_READBACK_ENABLED 1

/* ---- Cache (none on nRF54L15 app core) ---- */
#define NRFY_CACHE_WB(p_buffer, size)
#define NRFY_CACHE_INV(p_buffer, size)
#define NRFY_CACHE_WBINV(p_buffer, size)

/* ---- Reserved peripheral channels ---- */
#define NRFX_DPPI_CHANNELS_USED   0
#define NRFX_DPPI_GROUPS_USED     0
#define NRFX_PPI_CHANNELS_USED    0
#define NRFX_PPI_GROUPS_USED      0
#define NRFX_GPIOTE_CHANNELS_USED 0
#define NRFX_EGUS_USED            0
#define NRFX_TIMERS_USED          0

#ifdef __cplusplus
}
#endif

#endif /* NRFX_GLUE_H__ */

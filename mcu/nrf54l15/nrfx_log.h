#ifndef NRFX_LOG_H__
#define NRFX_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <rtthread.h>

#define NRFX_LOG_ERROR(fmt, ...)   rt_kprintf("nrfx[E]: " fmt "\n", ##__VA_ARGS__)
#define NRFX_LOG_WARNING(fmt, ...) rt_kprintf("nrfx[W]: " fmt "\n", ##__VA_ARGS__)
#define NRFX_LOG_INFO(fmt, ...)    rt_kprintf("nrfx[I]: " fmt "\n", ##__VA_ARGS__)
#define NRFX_LOG_DEBUG(fmt, ...)

#ifdef __cplusplus
}
#endif

#endif /* NRFX_LOG_H__ */

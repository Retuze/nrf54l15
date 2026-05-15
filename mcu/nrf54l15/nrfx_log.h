#ifndef NRFX_LOG_H__
#define NRFX_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <rtthread.h>
#include <stdio.h>

static inline const char *nrfx_log_err_str(uint32_t code)
{
    static char s_buf[12];
    snprintf(s_buf, sizeof(s_buf), "%u", (unsigned)code);
    return s_buf;
}

static inline void nrfx_log_hexdump(const uint8_t *p, uint16_t len)
{
    rt_kprintf("nrfx[D] hexdump %p [%u]:", (const void *)p, len);
    for (uint16_t i = 0; i < len; i++) {
        if ((i & 0x0F) == 0) rt_kprintf("\n  ");
        rt_kprintf("%02X ", p[i]);
    }
    rt_kprintf("\n");
}

#define NRFX_LOG_ERROR(fmt, ...)        rt_kprintf("nrfx[E]: " fmt "\n", ##__VA_ARGS__)
#define NRFX_LOG_WARNING(fmt, ...)      rt_kprintf("nrfx[W]: " fmt "\n", ##__VA_ARGS__)
#define NRFX_LOG_INFO(fmt, ...)         rt_kprintf("nrfx[I]: " fmt "\n", ##__VA_ARGS__)
#define NRFX_LOG_DEBUG(fmt, ...)

#define NRFX_LOG_ERROR_STRING_GET(code) nrfx_log_err_str(code)

#define NRFX_LOG_HEXDUMP_DEBUG(p, len) nrfx_log_hexdump((const uint8_t *)(p), (uint16_t)(len))

#ifdef __cplusplus
}
#endif

#endif /* NRFX_LOG_H__ */

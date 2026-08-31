#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "rtt.h"
#include "hal_uart.h"
#include <rtthread.h>
#include <rthw.h>

ssize_t write(int fd, const void *buf, size_t len)
{
    if (fd == 1 || fd == 2) {
        rtt_write((const char *)buf, (uint32_t)len);
        // serialWrite((const uint8_t *)buf, len);
        // serialFlush();
        return (ssize_t)len;
    }
    errno = EBADF;
    return -1;
}

/* picolibc / libc++ 可能走 _write 而非 write */
ssize_t _write(int fd, const void *buf, size_t len)
{
    return write(fd, buf, len);
}

ssize_t read(int fd, void *buf, size_t len)
{
    if (fd == 0) {
        return (ssize_t)rtt_read((char *)buf, (uint32_t)len);
    }
    errno = EBADF;
    return -1;
}

int close(int fd) { (void)fd; errno = EBADF; return -1; }

int fstat(int fd, struct stat *st)
{
    (void)fd;
    st->st_mode = S_IFCHR;
    return 0;
}

int isatty(int fd) { (void)fd; return 1; }

off_t lseek(int fd, off_t off, int whence)
{
    (void)fd;
    (void)off;
    (void)whence;
    errno = ESPIPE;
    return -1;
}

extern char _end;
extern char __heap_end; /* 链接脚本：堆区上界(不含) */

void *sbrk(ptrdiff_t incr)
{
    static char *   heap_ptr;
    char *          prev;
    const uintptr_t h_start = (uintptr_t)&_end;
    const uintptr_t h_limit = (uintptr_t)(void const *)&__heap_end;
    uintptr_t       h;

    if (heap_ptr == 0) {
        heap_ptr = &_end;
    }
    h = (uintptr_t)heap_ptr;
    if (incr < 0) {
        const size_t shrink = (size_t)(-incr);
        if (shrink > h - h_start) {
            errno = EINVAL;
            return (void *)-1;
        }
    } else if (incr > 0) {
        if ((size_t)incr > h_limit - h) {
            errno = ENOMEM;
            return (void *)-1;
        }
    }
    prev     = heap_ptr;
    heap_ptr = (char *)h + incr;
    return (void *)prev;
}

int clock_gettime(clockid_t clk_id, struct timespec *tp)
{
    (void)clk_id;
    uint64_t ns  = (uint64_t)rt_tick_get_millisecond() * 1000000ULL;
    time_t   sec = (time_t)(ns / 1000000000ULL);
    long     n   = (long)(ns % 1000000000ULL);
    tp->tv_sec   = sec;
    tp->tv_nsec  = n;
    return 0;
}

void _exit(int status) __attribute__((noreturn));
void _exit(int status)
{
    (void)status;
    for (;;) { }
}

/* ---- malloc / free → RT-Thread heap (thread-safe) ------------------------
 * Using ld --wrap so that Opus encoder/decoder scratch allocations are
 * redirected from picolibc's non-thread-safe malloc to RT-Thread's rt_malloc.
 * See CMakeLists.txt for -Wl,--wrap=malloc etc.
 */
#include <string.h>

void *__wrap_malloc(size_t size)
{
    return rt_malloc((rt_size_t)size);
}

void __wrap_free(void *ptr)
{
    rt_free(ptr);
}

void *__wrap_realloc(void *ptr, size_t size)
{
    return rt_realloc(ptr, (rt_size_t)size);
}

void *__wrap_calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *p = rt_malloc((rt_size_t)total);
    if (p) memset(p, 0, total);
    return p;
}

/* ---- RT-Thread print forwarding to libc --------------------------------
 * rt_vsnprintf / rt_kprintf are RT_WEAK in kservice.c → safe to override.
 * rt_snprintf / rt_sprintf are strong in kservice.c → delegate to
 * rt_vsnprintf which we've already replaced, so no override needed.
 */

int rt_vsnprintf(char *buf, rt_size_t size, const char *fmt, va_list args)
{
    return vsnprintf(buf, (size_t)size, fmt, args);
}

int rt_kprintf(const char *fmt, ...)
{
    va_list args;
    static char rt_log_buf[256];

    va_start(args, fmt);
    int n = vsnprintf(rt_log_buf, sizeof(rt_log_buf), fmt, args);
    va_end(args);

    rt_hw_console_output(rt_log_buf);
    return n;
}

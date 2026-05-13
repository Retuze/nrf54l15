/*
 * RT-Thread kernel configuration for XIAO nRF54L15 firmware.
 */
#ifndef RT_CONFIG_H__
#define RT_CONFIG_H__

/* ---- kernel ---------------------------------------------------------- */
#define RT_NAME_MAX                    8
#define RT_ALIGN_SIZE                  8
#define RT_THREAD_PRIORITY_MAX         32
#define RT_TICK_PER_SECOND             1000

/* startup is driven manually from src/main.cpp */
#define RT_USING_CONSOLE
#define RT_USING_LIBC
#define RT_USING_NEWLIB
#define RT_USING_HOOK
#define RT_HOOK_USING_FUNC_PTR
#define RT_MAIN_THREAD_STACK_SIZE      2048
#define RT_MAIN_THREAD_PRIORITY        10
#define RT_IDLE_THREAD_STACK_SIZE      512

/* ---- IPC ------------------------------------------------------------- */
#define RT_USING_SEMAPHORE
#define RT_USING_MUTEX
#define RT_USING_EVENT
#define RT_USING_TIMER_SOFT

/* ---- memory ---------------------------------------------------------- */
#define RT_USING_HEAP
#define RT_USING_SMALL_MEM
#define RT_USING_SMALL_MEM_AS_HEAP
#define RT_HEAP_SIZE                   (32 * 1024)

/* ---- console --------------------------------------------------------- */
#define RT_CONSOLEBUF_SIZE             128

#endif /* RT_CONFIG_H__ */

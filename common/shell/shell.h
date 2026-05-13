/*
 * shell.h - 简易命令行 Shell，基于 Segger RTT I/O。
 *
 * poll 模式：无独立线程，由应用主循环调用 shell_poll() 推进。
 * 实例为内部单例，命令表在 shell.c 内置。
 *
 * 典型用法：
 *   shell_init();
 *   // 每个循环里 shell_poll();
 */
#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHELL_LINE_BUF_SIZE 128
#define SHELL_MAX_ARGS      8

typedef void (*shell_cmd_handler_t)(int argc, char *argv[]);

typedef struct {
    const char           *name;
    const char           *desc;
    shell_cmd_handler_t   handler;
} shell_cmd_t;

void shell_init(void);
void shell_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* SHELL_H */

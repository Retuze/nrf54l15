/*
 * syscalls.c — picolibc 要求的"系统调用"桩。
 * 全部弱符号：实验工程或驱动（比如 uart.c 里的 stdio 落点）给强定义时自动覆盖。
 * （对照 nrf52840 仓库的 drivers/core/syscalls.c）
 */
#include <stddef.h>

void _exit(int code) __attribute__((weak, noreturn));
void _exit(int code)
{
    /* 死循环 + 断电不睡（保证调试器能看到状态） */
    (void)code;
    for (;;) { }
}

/* 不碰文件系统 / 终端的环境下函数返回不可用即可 */
int _open(const char *path, int flags, int mode) __attribute__((weak));
int _open(const char *path, int flags, int mode) { (void)path; (void)flags; (void)mode; return -1; }
int _close(int fd) __attribute__((weak));
int _close(int fd) { (void)fd; return -1; }
int _lseek(int fd, long off, int whence) __attribute__((weak));
int _lseek(int fd, long off, int whence) { (void)fd; (void)off; (void)whence; return -1; }
int _fstat(int fd, void *st) __attribute__((weak));
int _fstat(int fd, void *st) { (void)fd; (void)st; return -1; }
int _isatty(int fd) __attribute__((weak));
int _isatty(int fd) { (void)fd; return 1; }
int _kill(int pid, int sig) __attribute__((weak));
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
int _getpid(void) __attribute__((weak));
int _getpid(void) { return 1; }

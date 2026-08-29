/*
 * syscalls.c — picolibc 要求的"系统调用"桩。
 *
 * 命名约定：picolibc（非 newlib 兼容层）直接引用 POSIX 名的系统调用
 * （open/close/lseek/read/…），只有 _exit 保留 newlib 的下划线惯例
 * （libc/stdlib/exit.c 里调用 _exit）。
 *
 * 全部弱符号：实验工程或驱动给强定义时自动覆盖——
 * 比如 drivers/uart/uart.c 里 write 的强实现（printf 的最终出口）。
 */
#include <stddef.h>

/* 死循环 + 断电不睡（保证调试器能看到状态） */
void _exit(int code) __attribute__((weak, noreturn));
void _exit(int code)
{
    (void)code;
    for (;;) { }
}

/* 不碰文件系统 / 终端的环境下函数返回不可用即可 */
int open(const char *path, int oflag, ...) __attribute__((weak));
int open(const char *path, int oflag, ...) { (void)path; (void)oflag; return -1; }
int close(int fd) __attribute__((weak));
int close(int fd) { (void)fd; return -1; }
long lseek(int fd, long off, int whence) __attribute__((weak));
long lseek(int fd, long off, int whence) { (void)fd; (void)off; (void)whence; return -1; }
int read(int fd, void *buf, size_t len) __attribute__((weak));
int read(int fd, void *buf, size_t len) { (void)fd; (void)buf; (void)len; return -1; }
int fstat(int fd, void *st) __attribute__((weak));
int fstat(int fd, void *st) { (void)fd; (void)st; return -1; }
int isatty(int fd) __attribute__((weak));
int isatty(int fd) { (void)fd; return 1; }
int kill(int pid, int sig) __attribute__((weak));
int kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
int getpid(void) __attribute__((weak));
int getpid(void) { return 1; }

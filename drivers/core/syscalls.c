/*
 * syscalls.c — picolibc 系统调用的弱桩。
 *
 * 策略：强实现（write）在 drivers/console/console.c（控制台模块持有自己的
 * uart 实例）；这里只放返回"不支持"的弱桩——
 *   - 弱桩 -1：本环境没有文件系统（open/lseek/fstat）、没有输入通路
 *     （read，板载 SAMD11 USB 桥只接了 TX）、没有多进程（kill/getpid）。
 *     在 POSIX 语义下，-1 就是"不支持"的正确返回值——是设计如此，不是没实现。
 *   - isatty 返回 1：控制台 fd 就是 tty。
 *   - _exit：死循环 + 断电不睡（保证调试器能看到状态）。
 *
 * 全部弱符号：实验工程或驱动给强定义即自动覆盖——比如以后某个实验把
 * read 接上 UART RX，或某个实验挂载了自己的存储给 open/lseek 强实现。
 *
 * 命名：picolibc 直接用 POSIX 名（open/close/lseek/read/write/...），
 * 只有 _exit 保留 newlib 下划线惯例（libc/stdlib/exit.c 里调用 _exit）。
 */
#include <stddef.h>
#include <sys/types.h>   /* ssize_t */

/* ------------------------------ 弱桩：不支持的操作 ------------------- */

void _exit(int code) __attribute__((weak, noreturn));
void _exit(int code)
{
    (void)code;
    for (;;) { }
}

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

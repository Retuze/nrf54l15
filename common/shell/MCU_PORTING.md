# MCU 移植指南

## 架构

`shell.c` 不含任何平台依赖，支持多实例。每实例持有独立状态，命令回调签名含 `shell_t *sh`。

```
UART RX ──→ ringbuf_put(&sh.rb, ch) ──→ shell_poll(&sh) ──→ sh.output()
```

## 第一步：实现 output 函数

```c
void uart_output(const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    for (size_t i = 0; i < len; i++)
        uart_putchar(p[i]);
}
```

## 第二步：把输入字节喂进 ringbuf

UART 接收中断：

```c
void UART_RX_IRQHandler(void) {
    uint8_t ch = UART->DR;
    ringbuf_put(&g_shell.rb, ch);
}
```

轮询方式：

```c
while (1) {
    if (uart_rx_ready())
        ringbuf_put(&g_shell.rb, uart_getchar());
    shell_poll(&g_shell);
    delay_ms(10);
}
```

## 第三步：组合

```c
#include "shell.h"

static shell_t g_shell;   /* 单例 */

/* 命令签名: void cmd(shell_t *sh, int argc, char **argv) */
static void cmd_led(shell_t *sh, int argc, char **argv) { ... }

int main(void) {
    uart_init(115200);
    shell_init(&g_shell, uart_output);
    shell_register(&g_shell, "led", cmd_led, "led on|off");

    while (1) {
        shell_poll(&g_shell);
        delay_ms(10);
    }
}
```

## 内存分配

命令表默认用 `malloc`/`free`。替换方式：

```c
// include 之前
#define SHELL_MALLOC(sz)  pool_alloc(&cmd_pool, sz)
#define SHELL_FREE(ptr)   pool_free(&cmd_pool, ptr)
#include "shell.h"

// 或编译参数: -DSHELL_MALLOC=my_alloc -DSHELL_FREE=my_free
```

## 依赖清单

| 符号 | 来源 | 说明 |
|------|------|------|
| `ringbuf_*` | `ringbuf.c` | 无平台依赖 |
| `memmove` `memset` `strcmp` `strlen` | `<string.h>` | 可手写替换 |
| `malloc` `free` | `<stdlib.h>` | 可用宏替换 |
| `uint8_t` `uint32_t` `size_t` | `<stdint.h>` | 标准头 |

## 调整尺寸

```c
// shell.h
#define SHELL_LINE_MAX  128   // 命令行最大长度
#define SHELL_ARGS_MAX  8     // 最大参数个数

// ringbuf.h
#define RINGBUF_SIZE    256   // 输入缓冲，2 的幂
```

## 可选项

- **ANSI 转义序列**：`shell.c` 中三个 `term_*` 函数实现改为空，适合不带 ANSI 终端的串口
- **内置命令**：`shell_init` 里不调 `shell_register`，只注册应用命令

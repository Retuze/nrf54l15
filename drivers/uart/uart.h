/*
 * UARTE driver for the nRF54L15, instance-based（不透明句柄，无 malloc）。
 *
 * 与 ch32x035 仓库的 uart 驱动同形（跨仓库约定）：
 *   - uart_init(uart_port_t port, const uart_cfg_t *cfg) → 不透明 uart_hw_t*：
 *     每 port 一块静态实例（驱动内定长存储，无堆分配），调用方不碰细节；
 *     port 单独一参（实例选择是第一公民），cfg 聚合其余打开参数
 *   - 发送唯一语义：uart_tx 入队即返（驱动内 ring，满丢弃计数），
 *     uart_tx_wait 主动排空（自己踢 DMA，不等 ISR——HardFault 里也能走完）；
 *     "阻塞发送" = uart_tx + uart_tx_wait 组合（console 层做）
 *   - RX = DMA 缓冲 + 全满/空闲成块投递（无字节中断：RXDRDY 事件仅做计数；
 *     ENDRX=缓冲满投递、FRAMETIMEOUT=总线空闲投递——54L 的"半满/全满"对应物）
 *   - 不内建 RX ring：回调把块灌进 common/ring 由应用组装
 *
 * 芯片差异：54L UARTE 是 EasyDMA-only 且无 AMOUNT 寄存器（STOP 也不刷新），
 * 所以空闲投递的字节数靠 RXDRDY 事件计数（每字节 ~10 条指令，115200 下
 * ~1% CPU）。控制台/日志装配见 drivers/console（stdio 落点不在本模块）。
 */
#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stddef.h>

/* 54L 应用核的 UARTE 实例（另有一个 UARTE00 归 FLPR 核） */
typedef enum { UARTE20, UARTE21, UARTE22, UARTE30 } uart_port_t;

typedef enum {
    UART_BAUD_9600   = 9600,
    UART_BAUD_19200  = 19200,
    UART_BAUD_38400  = 38400,
    UART_BAUD_57600  = 57600,
    UART_BAUD_115200 = 115200,
    UART_BAUD_230400 = 230400,
    UART_BAUD_460800 = 460800,
} uart_baud_t;

/* 数据格式（当前只实现 8N1；枚举留给 8E1/8O1/9N1 扩展） */
typedef enum {
    UART_8N1 = 0,
} uart_fmt_t;

typedef enum {
    UART_EVT_RX_DATA,     /* 成块收到字节：data/len 有效 */
    UART_EVT_RX_ERROR,    /* 接收错误（错误源已清）：data/len 不用 */
    UART_EVT_TX_DONE,     /* 所有已入队字节已完整上完线（ring 空 + 外设完成）：
                             下电/复位前可依赖；data/len 不用 */
} uart_evt_t;
typedef struct uart_hw uart_hw_t;   /* 不透明句柄（定义在 uart.c） */

/* 块交付/事件回调：RX_DATA 的 data 指向驱动内 RX 缓冲，回调返回前有效
 * （下次重装会覆盖）。回调在 SERIAL IRQ 上下文执行：必须短小
 * （典型：灌进 common/ring 即返）。 */
typedef void (*uart_cb_t)(uart_hw_t *u, uart_evt_t evt,
                          const uint8_t *data, uint32_t len, void *arg);

#define UART_PIN_NONE 0xFFu   /* rx_pin 专用：只发不收（GPIO_PIN 编号最大 127） */

/* 打开参数（port 单独传参；结构体聚合其余——以后加字段不改签名） */
typedef struct {
    uint8_t  tx_pin;             /* GPIO_PIN(port,pin) 线性编号 */
    uint8_t  rx_pin;             /* GPIO_PIN(...) 或 UART_PIN_NONE */
    uart_baud_t baud;
    uart_fmt_t fmt;
    uint8_t  irq_prio;           /* NVIC 优先级预写（无回调不使能中断线） */
} uart_cfg_t;

/* 初始化（每 port 静态实例，重复 init = 重新配置同一句柄）。
 * 非法 port / 参数返回 NULL。 */
uart_hw_t *uart_init(uart_port_t port, const uart_cfg_t *cfg);

/* 去初始化：关外设、清中断、丢弃未发数据。 */
void uart_deinit(uart_hw_t *u);

/* 发送：入队即返（驱动内 ring；满拒收并计数），返回实际接受字节数。
 * buf 无需在 RAM 存活——入队即拷贝。 */
size_t uart_tx(uart_hw_t *u, const uint8_t *buf, size_t len);

/* 排空：自己踢 DMA 直到 ring 空 + 通道空闲（不依赖 ISR——故障路径可用）。 */
void uart_tx_wait(uart_hw_t *u);

/* 入队时因 ring 满丢弃的字节数。 */
uint32_t uart_tx_dropped(const uart_hw_t *u);

/* 中止在途 TX 并丢弃 ring（故障现场打印前归零用：STOP 不产生 END，
 * 丢弃保证随后的 uart_tx+uart_tx_wait 必然属于本次发送）。 */
void uart_tx_abort(uart_hw_t *u);

/* 注册回调 → 开中断 + NVIC（RX 块交付 / TX 完成事件；TX-only 实例也能用）；
 * cb=NULL 关中断。 */
void uart_register_callback(uart_hw_t *u, uart_cb_t cb, void *arg);

#endif /* UART_H */

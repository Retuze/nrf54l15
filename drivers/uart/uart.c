#include "uart.h"
#include "gpio.h"
#include "nvic.h"
#include "ring.h"
#include "nrf.h"   /* NRF_UARTE_Type / 寄存器宏 */

/*
 * 外设表：port → 寄存器/IRQ 线。54L 应用核 UARTE 的 IRQ 是共享 SERIAL 线
 * （SERIAL20_IRQn=198 …，与 SPIM/SPIS 共用；本工程只用 UARTE）。
 */
typedef struct {
    NRF_UARTE_Type *regs;
    uint32_t irq;
} uart_info_t;

static const uart_info_t uart_info[] = {
    [UARTE20] = { NRF_UARTE20_S, 198u },   /* SERIAL20_IRQn */
    [UARTE21] = { NRF_UARTE21_S, 199u },   /* SERIAL21_IRQn */
    [UARTE22] = { NRF_UARTE22_S, 200u },   /* SERIAL22_IRQn */
    [UARTE30] = { NRF_UARTE30_S, 260u },   /* SERIAL30_IRQn */
};

#define TX_RING_BUF 512u    /* 驱动内 TX ring（uart_tx 入队，ISR/排空共同踢） */
#define TX_DMA_BUF  128u    /* EasyDMA 空中缓冲 */
#define RX_BUF      256u

/* 每 port 一块静态实例（无堆分配；不透明句柄） */
struct uart_hw {
    uint8_t  inited;
    uint8_t  tx_pin, rx_pin;
    /* ---- TX：驱动内 ring + 空中缓冲 ---- */
    uint8_t  tx_ring_buf[TX_RING_BUF];
    ring_t   tx_ring;
    uint8_t  tx_dma[TX_DMA_BUF];
    volatile uint8_t tx_active;  /* 软件"在途"标志（END==0 复位态不能当忙标记） */
    uint32_t tx_dropped;
    /* ---- RX ---- */
    uint8_t  rx_buf[RX_BUF];
    uint32_t rx_count;          /* RXDRDY 事件计数（FRAMETIMEOUT 投递量） */
    uart_cb_t cb;
    void *cb_arg;
};

static uart_hw_t s_inst[4];

/* RX 中断掩码：RXDRDY（仅计数）+ 缓冲满（ENDRX）+ 总线空闲（FRAMETIMEOUT）
 * + 错误 + 半字节超时。RXDRDY 的每字节 IRQ 约 10 条指令（115200 下 ~1% CPU），
 * 换 FRAMETIMEOUT 投递所需的字节计数——UARTE 无 AMOUNT、STOP 也不刷新。 */
#define UART_IRQ_RX (UARTE_INTEN_RXDRDY_Msk | UARTE_INTEN_DMARXEND_Msk | \
                     UARTE_INTEN_FRAMETIMEOUT_Msk | UARTE_INTEN_ERROR_Msk | \
                     UARTE_INTEN_RXTO_Msk)

/* FRAMETIMEOUT：总线空闲多少个位时间后触发（16 位 ≈ 1.5 字节 @8N1） */
#define UART_FRAME_TIMEOUT_BITS 16u

static NRF_UARTE_Type *uart_regs(const uart_hw_t *u)
{
    return uart_info[u - s_inst].regs;
}

static uint32_t baud_reg(uart_baud_t b)
{
    switch (b) {
    case UART_BAUD_9600:   return UARTE_BAUDRATE_BAUDRATE_Baud9600;
    case UART_BAUD_19200:  return UARTE_BAUDRATE_BAUDRATE_Baud19200;
    case UART_BAUD_38400:  return UARTE_BAUDRATE_BAUDRATE_Baud38400;
    case UART_BAUD_57600:  return UARTE_BAUDRATE_BAUDRATE_Baud57600;
    case UART_BAUD_115200: return UARTE_BAUDRATE_BAUDRATE_Baud115200;
    case UART_BAUD_230400: return UARTE_BAUDRATE_BAUDRATE_Baud230400;
    case UART_BAUD_460800: return UARTE_BAUDRATE_BAUDRATE_Baud460800;
    default:               return UARTE_BAUDRATE_BAUDRATE_Baud115200;
    }
}

/* ==================================================== TX -- */
/* 把 ring 灌进 EasyDMA 并发射。"在途"用软件标志 tx_active（END==0 是复位态，
 * 不能当忙标记——否则第一次发送永远进不来；STOP 也不产生 END）。
 * 关键：kick 自己查硬件 END 收尾——不依赖 ISR 清 tx_active（console 实例
 * 通常没有注册回调，TX END 中断从未使能，若只靠 ISR 续排，第一包之后
 * 所有日志会永久积压在 ring 里；ISR 的作用只是更快地续排）。
 * 填充段包 PRIMASK 临界区：ring 的消费与 DMA 重配不能被 ISR 里的
 * kick/uart_tx 并发（ISR 内嵌套屏蔽是安全的）。 */
static void tx_kick(uart_hw_t *u)
{
    NRF_UARTE_Type *r = uart_regs(u);
    if (u->tx_active && r->EVENTS_DMA.TX.END) {
        u->tx_active = 0;             /* 硬件已完成上一包：自行收尾 */
    }
    if (u->tx_active) {
        return;                       /* 空中还有一包 */
    }
    uint32_t pm = nvic_mask_all();
    uint32_t n = ring_count(&u->tx_ring);
    if (n == 0) {
        nvic_unmask(pm);              /* 无数据：保持空闲 */
        return;
    }
    if (n > sizeof(u->tx_dma)) {
        n = sizeof(u->tx_dma);
    }
    for (uint32_t i = 0; i < n; i++) {
        u->tx_dma[i] = (uint8_t)ring_get(&u->tx_ring);
    }
    r->DMA.TX.PTR    = (uint32_t)u->tx_dma;
    r->DMA.TX.MAXCNT = n;
    r->EVENTS_DMA.TX.END = 0;
    r->TASKS_DMA.TX.START = 1;
    u->tx_active = 1;
    nvic_unmask(pm);
}

size_t uart_tx(uart_hw_t *u, const uint8_t *buf, size_t len)
{
    if (!u->inited) {
        return 0;
    }
    uint32_t pm = nvic_mask_all();    /* 入队与踢包原子化（ISR 里也安全） */
    size_t accepted = 0;
    for (size_t i = 0; i < len; i++) {
        if (ring_put(&u->tx_ring, buf[i]) == 0) {
            accepted++;
        } else {
            u->tx_dropped++;
        }
    }
    tx_kick(u);
    nvic_unmask(pm);
    return accepted;
}

void uart_tx_wait(uart_hw_t *u)
{
    NRF_UARTE_Type *r = uart_regs(u);
    /* 主动排空：自己查 END 收尾（不等 ISR——HardFault 上下文也能走完） */
    while (ring_count(&u->tx_ring) != 0 || u->tx_active) {
        if (r->EVENTS_DMA.TX.END) {
            u->tx_active = 0;         /* 硬件完成：自己清在途标记 */
        }
        tx_kick(u);
    }
}

uint32_t uart_tx_dropped(const uart_hw_t *u)
{
    return u->tx_dropped;
}

void uart_tx_abort(uart_hw_t *u)
{
    NRF_UARTE_Type *r = uart_regs(u);
    uint32_t pm = nvic_mask_all();
    r->TASKS_DMA.TX.STOP = 1;         /* STOP 不产生 END：在途包作废 */
    u->tx_active = 0;                 /* 关键：清软件在途标志，下次 kick 可用 */
    ring_init(&u->tx_ring, u->tx_ring_buf, sizeof(u->tx_ring_buf));
    r->EVENTS_DMA.TX.END = 0;
    nvic_unmask(pm);
}

/* ==================================================== RX -- */
static void rx_rearm(NRF_UARTE_Type *r)
{
    r->EVENTS_RXDRDY = 0;
    r->EVENTS_DMA.RX.END = 0;
    r->EVENTS_FRAMETIMEOUT = 0;
    r->TASKS_DMA.RX.START = 1;
}

uart_hw_t *uart_init(uart_port_t port, const uart_cfg_t *cfg)
{
    if (cfg == 0 || (uint32_t)port >= sizeof(uart_info) / sizeof(uart_info[0])) {
        return 0;
    }
    uart_hw_t *u = &s_inst[port];
    u->inited = 0;
    u->tx_pin = cfg->tx_pin;
    u->rx_pin = cfg->rx_pin;
    u->tx_dropped = 0;
    u->cb = 0;
    u->cb_arg = 0;
    u->rx_count = 0;
    u->tx_active = 0;
    ring_init(&u->tx_ring, u->tx_ring_buf, sizeof(u->tx_ring_buf));

    NRF_UARTE_Type *r = uart_regs(u);

    /* Idle-high TX pin via GPIO before the UARTE takes it over
     * （引脚配置复用 gpio 驱动，port→寄存器组映射只此一份）。 */
    gpio_mode(cfg->tx_pin, GPIO_OUTPUT);
    gpio_write(cfg->tx_pin, GPIO_HIGH);
    r->PSEL.TXD = ((uint32_t)(cfg->tx_pin >> 5) << UARTE_PSEL_TXD_PORT_Pos) |
                  (cfg->tx_pin & 31u);

    /* RX（可选）：PSEL + DMA 缓冲 + 空闲超时 */
    if (cfg->rx_pin != UART_PIN_NONE) {
        r->PSEL.RXD = ((uint32_t)(cfg->rx_pin >> 5) << UARTE_PSEL_RXD_PORT_Pos) |
                      (cfg->rx_pin & 31u);
        r->DMA.RX.PTR = (uint32_t)u->rx_buf;
        r->DMA.RX.MAXCNT = sizeof(u->rx_buf);
        r->CONFIG = (r->CONFIG & ~UARTE_CONFIG_FRAMETIMEOUT_Msk) |
                    (UARTE_CONFIG_FRAMETIMEOUT_ENABLED << UARTE_CONFIG_FRAMETIMEOUT_Pos);
        r->FRAMETIMEOUT = UART_FRAME_TIMEOUT_BITS;
    }

    /* 数据格式：当前只实现 8N1（cfg->fmt 枚举留扩展） */
    r->BAUDRATE = baud_reg(cfg->baud);
    r->ENABLE = UARTE_ENABLE_ENABLE_Enabled;

    if (cfg->rx_pin != UART_PIN_NONE) {
        rx_rearm(r);
    }

    nvic_set_prio(uart_info[port].irq, cfg->irq_prio);
    u->inited = 1;
    return u;
}

void uart_deinit(uart_hw_t *u)
{
    NRF_UARTE_Type *r = uart_regs(u);
    r->INTENCLR = UART_IRQ_RX | UARTE_INTEN_DMATXEND_Msk;
    r->ENABLE = UARTE_ENABLE_ENABLE_Disabled;
    uart_tx_abort(u);                 /* 丢弃未发数据 */
    u->inited = 0;
}

void uart_register_callback(uart_hw_t *u, uart_cb_t cb, void *arg)
{
    u->cb = cb;
    u->cb_arg = arg;
    NRF_UARTE_Type *r = uart_regs(u);
    if (cb) {
        /* TX 完成事件对 TX-only 实例同样生效；RX 事件按接线使能 */
        r->INTENSET = UARTE_INTEN_DMATXEND_Msk;
        if (u->rx_pin != UART_PIN_NONE) {
            r->INTENSET = UART_IRQ_RX;
        }
        nvic_enable(uart_info[u - s_inst].irq);
    } else {
        r->INTENCLR = UART_IRQ_RX | UARTE_INTEN_DMATXEND_Msk;
        nvic_disable(uart_info[u - s_inst].irq);
    }
}

/* ==================================================== ISR -- */
static void uart_irq_dispatch(uart_hw_t *u)
{
    NRF_UARTE_Type *r = uart_regs(u);

    if (r->EVENTS_DMA.TX.END) {
        u->tx_active = 0;             /* 硬件完成：清在途标记 */
        tx_kick(u);                   /* TX 后台续排（RX 事件可能同帧存在） */
        /* 最后一包完成后 ring 空 → 全部字节已上完线（ENDTX 即线上完成） */
        if (ring_count(&u->tx_ring) == 0 && u->cb) {
            u->cb(u, UART_EVT_TX_DONE, 0, 0, u->cb_arg);
        }
    }

    /* RXDRDY：只计数不投递（FRAMETIMEOUT 需要字节数；UARTE 无 AMOUNT） */
    if (r->EVENTS_RXDRDY) {
        r->EVENTS_RXDRDY = 0;
        if (u->rx_count < sizeof(u->rx_buf)) {
            u->rx_count++;
        }
    }

    /* 缓冲满：整块投递 + 重装。ENDRX 只在缓冲满时触发——直接按上限投递，
     * 不信任 RXDRDY 计数（事件是粘滞的，中断延迟下会漏计）。 */
    if (r->EVENTS_DMA.RX.END) {
        r->EVENTS_DMA.RX.END = 0;
        u->rx_count = 0;
        if (u->cb) {
            u->cb(u, UART_EVT_RX_DATA, u->rx_buf, sizeof(u->rx_buf), u->cb_arg);
        }
        rx_rearm(r);
    }

    /* 总线空闲：STOP 停 DMA → 按计数投递部分块 + 重装
     * （STOP 不产生 END、不刷新计数——计数来自上面的 RXDRDY） */
    if (r->EVENTS_FRAMETIMEOUT) {
        r->EVENTS_FRAMETIMEOUT = 0;
        r->TASKS_DMA.RX.STOP = 1;
        uint32_t n = u->rx_count;
        u->rx_count = 0;
        if (u->cb) {
            u->cb(u, UART_EVT_RX_DATA, u->rx_buf, n, u->cb_arg);
        }
        rx_rearm(r);
    }

    if (r->EVENTS_ERROR) {
        /* ERRORSRC 写 1 清零（Nordic UARTE 约定）：读到的置位位写回 */
        r->ERRORSRC = r->ERRORSRC;
        r->EVENTS_ERROR = 0;
        if (u->cb) {
            u->cb(u, UART_EVT_RX_ERROR, 0, 0, u->cb_arg);
        }
        r->TASKS_FLUSHRX = 1;
        u->rx_count = 0;
        rx_rearm(r);
    }
    if (r->EVENTS_RXTO) {
        r->EVENTS_RXTO = 0;
        r->TASKS_FLUSHRX = 1;
        rx_rearm(r);                   /* 半字节：flush 后重装，不投递 */
    }
}

/* 向量表槽在 drivers/core/startup.c（弱别名 Default_Handler，此处强定义接管） */
void SERIAL20_IRQHandler(void) { if (s_inst[UARTE20].inited) uart_irq_dispatch(&s_inst[UARTE20]); }
void SERIAL21_IRQHandler(void) { if (s_inst[UARTE21].inited) uart_irq_dispatch(&s_inst[UARTE21]); }
void SERIAL22_IRQHandler(void) { if (s_inst[UARTE22].inited) uart_irq_dispatch(&s_inst[UARTE22]); }
void SERIAL30_IRQHandler(void) { if (s_inst[UARTE30].inited) uart_irq_dispatch(&s_inst[UARTE30]); }

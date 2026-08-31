/*
 * Minimal startup for nRF54L15 application core (Cortex-M33 / ARMv8-M).
 *
 * Responsibilities:
 *   1. Provide the vector table (initial SP + Reset + core exceptions + IRQs).
 *   2. On reset: enable the FPU, copy .data/.tdata from RRAM to RAM, zero
 *      .tbss+.bss, install the picolibc TLS block (_set_tls), run static
 *      constructors (__libc_init_array), then jump to main().
 *
 * No SystemInit magic: the SoC boots on its internal oscillator, which is
 * enough for GPIO. Clock/HFXO setup is done later, per-experiment.
 */

#include <stdint.h>
#include <stdio.h>    /* printf：HardFault 现场打印 */
#include "console.h"  /* console_tx_abort：打印前归零 TX 通道 */

/* Symbols provided by the linker script (link.ld)。
 * 链接器符号用法约定："有地址语义"的符号（_sidata/_sdata/...）按对象声明、
 * 用 &取地址；"值语义"的符号（__tdata_size，其值就是尺寸）按数组声明、
 * 用退化指针取值——反过来用会从"地址=符号值"处读内存（曾因此踩过
 * .tdata 为空时从向量表读 0x20040000 当循环上界、拷出 RAM 顶端的 bug）。 */
extern uint32_t _sidata;   /* .data load address (in RRAM)            */
extern uint32_t _sdata;    /* .data start (in RAM)                    */
extern uint32_t _edata;    /* .data end   (in RAM)                    */
extern uint32_t _ebss;     /* .bss end                                */
extern uint32_t _estack;   /* top of stack (end of RAM)               */

/* picolibc TLS / init-array contract */
extern uint32_t __tdata_source;  /* .tdata load address (in RRAM)     */
extern uint32_t __tdata_start;   /* .tdata start (in RAM)             */
extern char     __tdata_size[];  /* 值语义：数组首址 = .tdata 字节数  */
extern uint32_t __bss_start;     /* = ADDR(.tbss)：清零覆盖 tbss+bss  */
extern char     __tls_base[];    /* 静态 TLS 块基址（link.ld 提供）    */
extern void _set_tls(void *tls);         /* picolibc                 */
extern void __libc_init_array(void);     /* picolibc                 */

int  main(void);
void Reset_Handler(void);
void Default_Handler(void);

/* Core exception handlers: weakly aliased to Default_Handler so a real
 * project can override any of them by just defining a function of the
 * same name. */
#define WEAK_ALIAS __attribute__((weak, alias("Default_Handler")))

void HardFault_Handler(void);  /* defined below (records fault, no wfi) */

void NMI_Handler(void)        WEAK_ALIAS;
void MemManage_Handler(void)  WEAK_ALIAS;
void BusFault_Handler(void)   WEAK_ALIAS;
void UsageFault_Handler(void) WEAK_ALIAS;
void SecureFault_Handler(void)WEAK_ALIAS;
void SVC_Handler(void)        WEAK_ALIAS;
void DebugMon_Handler(void)   WEAK_ALIAS;
void PendSV_Handler(void)     WEAK_ALIAS;
void SysTick_Handler(void)    WEAK_ALIAS;

/* All peripheral IRQs default to Default_Handler. Named handlers (weak, alias
 * to Default_Handler) let a driver take over a line just by defining a strong
 * function of the same name (e.g. GRTC_2_IRQHandler in drivers/time/time.c). */
void Default_IRQHandler(void) WEAK_ALIAS;

/* nRF54L15 app 核 IRQn 最大到 269（GPIOTE30_1），所以表至少 16+270 项。
 * 注意：旧版只留了 128 个 IRQ 槽——GRTC_2（228）这类高编号中断若被使能，
 * 会去取表外的垃圾当向量，所以表必须够大，别改小。 */
void GRTC_2_IRQHandler(void) WEAK_ALIAS;   /* GRTC_2_IRQn = 228（app secure） */

/* UARTE 共享 SERIAL 线（本工程只用 UARTE；处理函数在 drivers/uart/uart.c） */
void SERIAL20_IRQHandler(void) WEAK_ALIAS;  /* SERIAL20_IRQn = 198 */
void SERIAL21_IRQHandler(void) WEAK_ALIAS;  /* SERIAL21_IRQn = 199 */
void SERIAL22_IRQHandler(void) WEAK_ALIAS;  /* SERIAL22_IRQn = 200 */
void SERIAL30_IRQHandler(void) WEAK_ALIAS;  /* SERIAL30_IRQn = 260 */

/* 普通定时器（drivers/timer/timer.c） */
void TIMER00_IRQHandler(void) WEAK_ALIAS;   /* TIMER00_IRQn = 85 */

/* RADIO（drivers/radio/radio.c 异步 API） */
void RADIO_0_IRQHandler(void) WEAK_ALIAS;   /* RADIO_0_IRQn = 138 */

typedef void (*vector_t)(void);

/* 16 core vectors + IRQ slots. 270 IRQ slots covers the nRF54L15
 * (IRQn max 269); unused slots simply point at Default_Handler. */
__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[16 + 270] = {
    (vector_t)(&_estack),   /* 0x00 Initial Stack Pointer */
    Reset_Handler,          /* 0x04 Reset                 */
    NMI_Handler,            /* 0x08 NMI                   */
    HardFault_Handler,      /* 0x0C HardFault             */
    MemManage_Handler,      /* 0x10 MemManage             */
    BusFault_Handler,       /* 0x14 BusFault              */
    UsageFault_Handler,     /* 0x18 UsageFault            */
    SecureFault_Handler,    /* 0x1C SecureFault           */
    0, 0, 0,                /* 0x20-0x28 Reserved         */
    SVC_Handler,            /* 0x2C SVCall                */
    DebugMon_Handler,       /* 0x30 DebugMonitor          */
    0,                      /* 0x34 Reserved              */
    PendSV_Handler,         /* 0x38 PendSV                */
    SysTick_Handler,        /* 0x3C SysTick               */

    /* IRQ0..IRQ269 -> Default_IRQHandler。
     * 具名外设 IRQ 用不相交区间挂槽（处理函数本体由对应驱动给强定义，
     * 此处是弱别名占位）：槽号 = 16 + IRQn。 */
    [16 ... 16 + 85 - 1]                 = Default_IRQHandler,
    [16 + 85]                            = TIMER00_IRQHandler,
    [16 + 86 ... 16 + 138 - 1]           = Default_IRQHandler,
    [16 + 138]                           = RADIO_0_IRQHandler,
    [16 + 139 ... 16 + 198 - 1]          = Default_IRQHandler,
    [16 + 198]                           = SERIAL20_IRQHandler,
    [16 + 199]                           = SERIAL21_IRQHandler,
    [16 + 200]                           = SERIAL22_IRQHandler,
    [16 + 201 ... 16 + 228 - 1]          = Default_IRQHandler,
    [16 + 228]                           = GRTC_2_IRQHandler,
    [16 + 229 ... 16 + 260 - 1]          = Default_IRQHandler,
    [16 + 260]                           = SERIAL30_IRQHandler,
    [16 + 261 ... 16 + 270 - 1]          = Default_IRQHandler,
};

void Reset_Handler(void)
{
    /* Enable the FPU (CP10 & CP11 full access) before any FP instruction,
     * required because we compile with -mfloat-abi=hard. */
    volatile uint32_t *CPACR = (volatile uint32_t *)0xE000ED88u;
    *CPACR |= (0xFu << 20);
    __asm volatile ("dsb");
    __asm volatile ("isb");

    /* Copy initialized data from RRAM (LMA) to RAM (VMA). */
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    /* Copy .tdata（TLS 已初始化数据）from its RRAM image. */
    char *s = (char *)&__tdata_source;
    char *d = (char *)&__tdata_start;
    uint32_t tsize = (uint32_t)(uintptr_t)__tdata_size;   /* 值语义符号 */
    for (uint32_t i = 0; i < tsize; i++) {
        d[i] = s[i];
    }

    /* Zero .tbss + .bss（__bss_start = ADDR(.tbss)）. */
    dst = &__bss_start;
    while (dst < &_ebss) {
        *dst++ = 0u;
    }

    /* Install the picolibc TLS block pointer (errno/reent 都靠它），
     * then run static constructors. */
    _set_tls(__tls_base);
    __libc_init_array();

    (void)main();

    /* main() should not return; if it does, trap here. */
    for (;;) {
        __asm volatile ("wfi");
    }
}

/* Fault diagnostics, readable via pyOCD:
 *   g_fault[0] = magic 0xFA017000 once a HardFault happened
 *   g_fault[1] = CFSR, [2] = HFSR, [3] = stacked PC, [4] = stacked LR
 * Spin WITHOUT wfi so the debug AP stays powered and we can always re-attach.
 *
 * 现场打印：先 g_fault[] 记现场（零 libc/驱动依赖，pyocd 永远可读），再
 * console_tx_abort() 中止可能在途的 EasyDMA 传输并丢 ring（把 TX 通道
 * 确定性归零；console 未 init 时是空操作），然后走正常 printf（行缓冲，
 * \n 触发 flush → write → uart_tx + uart_tx_wait——uart_tx_wait 自己踢
 * DMA 不等 ISR，HardFault 上下文里同样能走完）。
 * 残余风险：fault 恰好发生在 vfprintf 内部时，bufio 静态状态被重入踩——
 * 输出乱码有界、不死锁，且 g_fault[] 已先行记录。uart_init/uart_console_bind
 * 之前的 fault 会经 console 未初始化守卫静默丢弃串口输出
 * （现场仍靠 g_fault[]）。 */
volatile uint32_t g_fault[8];

__attribute__((used)) void hardfault_c(uint32_t *frame)
{
    g_fault[0] = 0xFA017000u;
    g_fault[1] = *(volatile uint32_t *)0xE000ED28u; /* CFSR */
    g_fault[2] = *(volatile uint32_t *)0xE000ED2Cu; /* HFSR */
    g_fault[3] = frame[6];                          /* stacked PC */
    g_fault[4] = frame[5];                          /* stacked LR */

    console_tx_abort();
    printf("\n!!! HARDFAULT  CFSR=0x%08x HFSR=0x%08x PC=0x%08x LR=0x%08x\n",
            g_fault[1], g_fault[2], g_fault[3], g_fault[4]);
    for (;;) {
    }
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4        \n"
        "ite eq            \n"
        "mrseq r0, msp     \n"
        "mrsne r0, psp     \n"
        "b hardfault_c     \n");
}

void Default_Handler(void)
{
    for (;;) {
        /* spin, no wfi */
    }
}

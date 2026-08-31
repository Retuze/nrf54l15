/*
 * 02_fault — HardFault 现场打印验证实验。
 *
 * 流程：
 *   1. 初始化 LED/UART/GRTC，打印横幅（异步 log 链路）。
 *   2. LED 闪 3 次（程序存活指示——串口坏了也能看到）。
 *   3. 向 0xFFFFFFF0（对齐、未映射区域）写内存 → 精确总线错误（BusFault）。
 *      本工程的 startup.c 从未在 SHCSR 里使能 BusFault/UsageFault/MemManage
 *      独立处理器（复位默认关闭），所以该错误会升级成 HardFault。
 *   4. HardFault 处理器先记 g_fault[] 现场，再 console_tx_abort() 归零
 *      TX 通道，最后 printf 打印现场行（drivers/core/startup.c）——printf
 *      全工程只在这里用（打印策略，见 README）。
 *
 * 验证点：
 *   - 串口最后一行应为 "!!! HARDFAULT  CFSR=... HFSR=... PC=... LR=..."。
 *     预期 CFSR=0x00008200（BFARVALID|PRECISERR）、HFSR=0x40000000
 *     （FORCED，即"从子故障升级而来"）、PC 指向下面的触发写指令。
 *   - pyocd 读现场（不挂 GDB 也能读）：
 *       pyocd cmd -t nrf54l -c "read32 0x2000xxxx"   # g_fault[] 地址见
 *                                                     # build/02_fault/02_fault.map
 *     g_fault[0]=0xFA017000（magic）、[1]=CFSR、[3]=PC。
 *   - LED 停在最后一次状态（处理器死循环不睡，调试器随时可连）。
 */

#include <stdint.h>
#include "xiao_nrf54l15.h"
#include "gpio.h"
#include "time.h"
#include "console.h"
#include "log.h"

/* LED：P2.00 低电平点亮（板头只给裸 PORT/PIN，线性编号由 GPIO_PIN 组合） */
#define LED GPIO_PIN(BOARD_LED_PORT, BOARD_LED_PIN)

static void led_init(void)
{
    gpio_mode(LED, GPIO_OUTPUT);
    gpio_write(LED, !BOARD_LED_ACTIVE_LEVEL);   /* 初始灭 */
}

static void led_blink(int n)
{
    for (int i = 0; i < n; i++) {
        gpio_write(LED, BOARD_LED_ACTIVE_LEVEL);   /* 点亮 */
        time_delay_us(100000);
        gpio_write(LED, !BOARD_LED_ACTIVE_LEVEL);
        time_delay_us(100000);
    }
}

static const uart_cfg_t console_cfg = {
        .tx_pin = GPIO_PIN(BOARD_CONSOLE_TX_PORT, BOARD_CONSOLE_TX_PIN),
    .rx_pin = UART_PIN_NONE,            /* 板载 SAMD11 桥只接了 TX */
    .baud = UART_BAUD_115200,
    .fmt = UART_8N1,
    .irq_prio = 0,};

int main(void)
{
    led_init();
    console_init(UARTE20, &console_cfg);
    console_log_init();   /* 打印策略：日志统一走 log（printf 仅 HardFault） */
    time_init();

    log_puts("\n=== 02_fault: HardFault dump test ===\n");
    log_puts("LED blinks 3x, then a bus fault is triggered\n");
    led_blink(3);

    log_puts("triggering bus fault (write 0xFFFFFFF0)...\n");
    /* 触发前排空异步 TX：现场打印前的 uart_tx_abort（STOP）会截断在途
     * EasyDMA，排空保证这条日志完整离开发送器（现场行由 printf 走
     * 阻塞路径，HardFault 里 ISR 不抢跑）。 */
    console_flush();
    /* 注意别用 0xFFFFFFFF：它 & 3 == 3（未对齐），且落在 Device 属性内存区——
     * 未对齐的 Device 访问无视 CCR.UNALIGN_TRP 一律报 UNALIGNED UsageFault
     * （实板验证过：CFSR=0x01000000），不是总线错误。
     * 0xFFFFFFF0 对齐且在未映射区域 → 精确总线错误（PRECISERR）。 */
    volatile uint32_t *p = (volatile uint32_t *)0xFFFFFFF0u;
    *p = 0xDEADBEEFu;   /* BusFault → 升级 HardFault（SHCSR 未使能子处理器） */

    log_puts("unreachable\n");
    for (;;) { }
}

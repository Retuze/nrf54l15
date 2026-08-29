/*
 * 02_fault — HardFault 现场打印验证实验。
 *
 * 流程：
 *   1. 初始化 LED/UART/GRTC，打印横幅（验证 printf 链路）。
 *   2. LED 闪 3 次（程序存活指示——串口坏了也能看到）。
 *   3. 向 0xFFFFFFF0（对齐、未映射区域）写内存 → 精确总线错误（BusFault）。
 *      本工程的 startup.c 从未在 SHCSR 里使能 BusFault/UsageFault/MemManage
 *      独立处理器（复位默认关闭），所以该错误会升级成 HardFault。
 *   4. HardFault 处理器先记 g_fault[] 现场，再 uart_tx_abort() 归零
 *      TX 通道，最后 printf 打印现场行（drivers/core/startup.c）。
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
#include <stdio.h>
#include "config.h"
#include "nrf.h"
#include "grtc.h"
#include "uart.h"

/* LED：P2.00 低电平点亮（板头只给端口号，见 boards/xiao_nrf54l15.h） */
#define LED_REG  NRF_P2_S   /* BOARD_LED_PORT == 2 */
#define LED_MASK (1u << BOARD_LED_PIN)

static void led_init(void)
{
    LED_REG->PIN_CNF[BOARD_LED_PIN] =
        (GPIO_PIN_CNF_DIR_Output       << GPIO_PIN_CNF_DIR_Pos) |
        (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos);
    LED_REG->DIRSET = LED_MASK;
    LED_REG->OUTSET = LED_MASK;
}

static void led_blink(int n)
{
    for (int i = 0; i < n; i++) {
        LED_REG->OUTCLR = LED_MASK;   /* 低电平点亮 */
        grtc_delay_us(100000);
        LED_REG->OUTSET = LED_MASK;
        grtc_delay_us(100000);
    }
}

int main(void)
{
    led_init();
    uart_init();
    grtc_init();

    printf("\n=== 02_fault: HardFault dump test ===\n");
    printf("LED blinks 3x, then a bus fault is triggered\n");
    led_blink(3);

    printf("triggering bus fault (write 0xFFFFFFF0)...\n");
    /* 注意别用 0xFFFFFFFF：它 & 3 == 3（未对齐），且落在 Device 属性内存区——
     * 未对齐的 Device 访问无视 CCR.UNALIGN_TRP 一律报 UNALIGNED UsageFault
     * （实板验证过：CFSR=0x01000000），不是总线错误。
     * 0xFFFFFFF0 对齐且在未映射区域 → 精确总线错误（PRECISERR）。 */
    volatile uint32_t *p = (volatile uint32_t *)0xFFFFFFF0u;
    *p = 0xDEADBEEFu;   /* BusFault → 升级 HardFault（SHCSR 未使能子处理器） */

    printf("unreachable\n");
    for (;;) { }
}

/*
 * nvic.h — NVIC 直写辅助（vendor/cmsis 无 core_cm33.h，各驱动共用）。
 * ISER/ICER/IPR 按 Cortex-M 标准地址，内联零开销。
 */
#ifndef NVIC_H
#define NVIC_H

#include <stdint.h>

static inline void nvic_set_prio(uint32_t irq, uint8_t prio)
{
    *(volatile uint8_t *)(0xE000E400u + irq) = prio;   /* IPRn，8 位/线 */
}

static inline void nvic_enable(uint32_t irq)
{
    *(volatile uint32_t *)(0xE000E100u + (irq >> 5) * 4u) = 1u << (irq & 31u);   /* ISER */
}

static inline void nvic_disable(uint32_t irq)
{
    *(volatile uint32_t *)(0xE000E180u + (irq >> 5) * 4u) = 1u << (irq & 31u);   /* ICER */
}

/* 全局临界区（PRIMASK）：多上下文共享数据的短临界段用。
 * mask = nvic_mask_all() → … → nvic_unmask(mask)。
 * 可在 ISR 内嵌套：mask 返回 1，unmask(1) 保持屏蔽。 */
static inline uint32_t nvic_mask_all(void)
{
    uint32_t pm;
    __asm volatile ("mrs %0, primask" : "=r"(pm));
    __asm volatile ("cpsid i");
    return pm;
}

static inline void nvic_unmask(uint32_t pm)
{
    __asm volatile ("msr primask, %0" : : "r"(pm));
}

#endif /* NVIC_H */

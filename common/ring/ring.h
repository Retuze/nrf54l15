/*
 * ring.h — 字节环形缓冲（SPSC 无锁），平台无关（common 规则）。
 *
 * 目标用法：ISR 只 put（生产）、主循环只 get（消费）——head 只由生产者写、
 * tail 只由消费者写，2 的幂容量 + mask 索引，无锁无临界区，put/get 均为 O(1)。
 *
 * 约束：buf 容量必须是 2 的幂（ring_init 不校验，文档契约）；head/tail 是
 * uint32 自由回绕（count = head - tail 在回绕后依然正确，前提 count < 2^32）。
 * 溢出策略留给调用方：满时 ring_put 返回 -1（丢/重试由上层决定）。
 */
#ifndef RING_H
#define RING_H

#include <stdint.h>

typedef struct {
    uint8_t *buf;
    uint32_t size;            /* 2 的幂 */
    volatile uint32_t head;   /* 只由生产者写 */
    volatile uint32_t tail;   /* 只由消费者写 */
} ring_t;

void ring_init(ring_t *r, uint8_t *buf, uint32_t size);

int ring_put(ring_t *r, uint8_t b);   /* 满返回 -1，成功 0 */
int ring_get(ring_t *r);              /* 空返回 -1，否则返回字节 */

uint32_t ring_count(const ring_t *r);
uint32_t ring_free(const ring_t *r);

#endif /* RING_H */

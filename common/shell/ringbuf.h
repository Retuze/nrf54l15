#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdint.h>
#include <stdbool.h>

#define RINGBUF_SIZE 256  /* power of 2 */

typedef struct {
    uint8_t          buf[RINGBUF_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
} ringbuf_t;

void      ringbuf_init(ringbuf_t *rb);
bool      ringbuf_put(ringbuf_t *rb, uint8_t ch);
bool      ringbuf_get(ringbuf_t *rb, uint8_t *ch);
uint32_t  ringbuf_available(ringbuf_t *rb);
bool      ringbuf_is_empty(ringbuf_t *rb);

#endif

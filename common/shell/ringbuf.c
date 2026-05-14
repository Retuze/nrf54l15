#include "ringbuf.h"

void ringbuf_init(ringbuf_t *rb) {
    rb->head = 0;
    rb->tail = 0;
}

bool ringbuf_put(ringbuf_t *rb, uint8_t ch) {
    uint32_t next = (rb->head + 1) & (RINGBUF_SIZE - 1);
    if (next == rb->tail) return false;
    rb->buf[rb->head] = ch;
    rb->head = next;
    return true;
}

bool ringbuf_get(ringbuf_t *rb, uint8_t *ch) {
    uint32_t t = rb->tail;
    if (t == rb->head) return false;
    *ch = rb->buf[t];
    rb->tail = (t + 1) & (RINGBUF_SIZE - 1);
    return true;
}

uint32_t ringbuf_available(ringbuf_t *rb) {
    return (rb->head - rb->tail) & (RINGBUF_SIZE - 1);
}

bool ringbuf_is_empty(ringbuf_t *rb) {
    return rb->head == rb->tail;
}

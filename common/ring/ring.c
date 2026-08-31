#include "ring.h"

void ring_init(ring_t *r, uint8_t *buf, uint32_t size)
{
    r->buf = buf;
    r->size = size;
    r->head = 0;
    r->tail = 0;
}

int ring_put(ring_t *r, uint8_t b)
{
    uint32_t h = r->head;
    if (h - r->tail >= r->size) {
        return -1;                        /* 满 */
    }
    r->buf[h & (r->size - 1u)] = b;
    r->head = h + 1u;                     /* 自然回绕 */
    return 0;
}

int ring_get(ring_t *r)
{
    uint32_t t = r->tail;
    if (r->head == t) {
        return -1;                        /* 空 */
    }
    uint8_t b = r->buf[t & (r->size - 1u)];
    r->tail = t + 1u;
    return b;
}

uint32_t ring_count(const ring_t *r)
{
    return r->head - r->tail;
}

uint32_t ring_free(const ring_t *r)
{
    return r->size - ring_count(r);
}

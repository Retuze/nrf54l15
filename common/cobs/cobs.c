#include "cobs.h"

uint32_t cobs_encode(const uint8_t *src, uint32_t len, uint8_t *dst)
{
    uint32_t read = 0, write = 1;
    uint32_t code_idx = 0;
    uint32_t code = 1;

    while (read < len) {
        if (src[read] == 0) {
            dst[code_idx] = (uint8_t)code;   /* 隐式 0x00，下一个是 code 字节 */
            code = 1;
            code_idx = write++;
            read++;
        } else {
            dst[write++] = src[read++];
            code++;
            if (code == 0xFF) {              /* 254 个非零无隐零 */
                dst[code_idx] = (uint8_t)code;
                code = 1;
                code_idx = write++;
            }
        }
    }
    dst[code_idx] = (uint8_t)code;           /* 帧尾 code（隐式末 0x00） */
    dst[write++] = 0;                        /* 定界符 */
    return write;
}

void cobs_dec_reset(cobs_dec_t *d)
{
    d->code = 0;
    d->out_len = 0;
    d->in_frame = 0;
    d->pending_zero = 0;
    d->has_zero = 0;
}

static int dec_out(cobs_dec_t *d, uint8_t b, uint8_t *dst, uint32_t cap)
{
    if (d->out_len >= cap) {
        return -1;                            /* 溢出：帧作废 */
    }
    dst[d->out_len++] = b;
    return 0;
}

int cobs_dec_feed(cobs_dec_t *d, uint8_t b, uint8_t *dst, uint32_t cap)
{
    /* 块尾隐零待决：定界符 = 该零是帧尾终止符（不输出）；否则先补输出 */
    if (d->pending_zero) {
        d->pending_zero = 0;
        if (b == 0) {
            uint32_t n = d->out_len;
            cobs_dec_reset(d);
            return (int)n;                    /* 帧完成（终止零不计数） */
        }
        int rc = dec_out(d, 0, dst, cap);     /* 隐零是真实数据 */
        if (rc < 0) return rc;
    }

    if (b == 0) {
        if (d->in_frame && d->out_len > 0) {  /* 定界：帧完成 */
            uint32_t n = d->out_len;
            cobs_dec_reset(d);
            return (int)n;
        }
        cobs_dec_reset(d);                    /* 空帧/帧间 0：忽略（兼噪声清零） */
        return 0;
    }

    d->in_frame = 1;
    if (d->code == 0) {
        /* code 字节：c==0xFF → 254 数据无隐零；否则 c-1 数据 + 隐式 0x00 */
        if (b == 0xFF) {
            d->code = 254u;
            d->has_zero = 0;
        } else {
            d->code = (uint32_t)b - 1u;
            d->has_zero = 1;
            if (d->code == 0) {
                d->pending_zero = 1;          /* c==1：下个字节起待决隐零 */
            }
        }
        return 0;
    }

    /* 数据字节 */
    int rc = dec_out(d, b, dst, cap);
    if (rc < 0) return rc;
    d->code--;
    if (d->code == 0 && d->has_zero) {
        d->pending_zero = 1;                  /* 块结尾（0xFF 块无隐零） */
    }
    return 0;
}

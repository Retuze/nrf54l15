/*
 * cobs.h — COBS（Consistent Overhead Byte Stuffing）帧定界编解码，
 * UART 传输专用（common/proto 的传输帧经 COBS 包裹后上串口；BLE 不用）。
 *
 * 无长度上限：最坏开销 = len/254 + 2（len 恰为 254 倍数时 +1 个 code 字节）。
 * 编码器是纯函数；解码器是逐字节增量状态机（适配"串口 RX 回调 1 字节/次"
 * 的喂入方式），收到 0x00 定界返回完整帧长度。
 * 注意：COBS 只有帧尾定界没有帧头——线路噪声会混进当前帧。上电/噪声场景
 * 发送侧先发一个 0x00（空帧）把对端状态清零。
 */
#ifndef COBS_H
#define COBS_H

#include <stdint.h>

/* 编码：dst 需 len + len/254 + 2 字节。返回编码后总长（含尾 0x00）。 */
uint32_t cobs_encode(const uint8_t *src, uint32_t len, uint8_t *dst);

/* 增量解码器状态（调用方持有，cobs_dec_reset 初始化） */
typedef struct {
    uint32_t code;      /* 剩余数据字节数（0 = 下一个是 code 字节） */
    uint32_t out_len;   /* 本帧已解码字节数 */
    int      in_frame;  /* 已进入帧（收到过首个非 0 字节） */
    int      pending_zero; /* 块尾隐零待决：下个字节是定界符=帧尾终止符（不
                              输出），否则先补输出该零 */
    int      has_zero;     /* 当前块结尾带隐零（0xFF 块不带） */
} cobs_dec_t;

void cobs_dec_reset(cobs_dec_t *d);

/* 逐字节喂入。返回：
 *   >0 = 帧完成，返回值为解码后长度（dst 里是解码结果；cap 防溢出）
 *    0 = 继续喂
 *   -1 = 解码输出超过 cap（调用方 reset 后重来，该帧作废） */
int cobs_dec_feed(cobs_dec_t *d, uint8_t b, uint8_t *dst, uint32_t cap);

#endif /* COBS_H */

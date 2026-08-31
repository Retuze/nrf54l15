/*
 * proto.h — 固件/app 通用应用协议（common 规则：传输经 proto_ops 注入）。
 *
 * 线格式（docs/proto.md 有完整规格）：
 *   传输帧 = TYPE u16BE | SEQFLAGS u8 | MSG_ID u8 | LEN u16BE | PAYLOAD | CRC16
 *   - UART：整条消息单帧（COBS 定界，无分片）；BLE：按 mtu_get() 分片
 *   - CRC16-CCITT(FALSE)（poly 0x1021 init 0xFFFF），proto_crc16() 公开给
 *     app 侧移植对照
 *
 * 语义：
 *   - GET_REQ（app→fw）：载荷 = 请求的 T 列表，空 = 全部字段
 *   - REPORT（fw→app）：TLV 列表；ACK_REQ 分两级送达——状态类需确认
 *     （app 回 ACK，fw 超时同 id 重发），遥测类尽力而为
 *   - SET（app→fw）：TLV 列表；MSG_ID 幂等——超时同 id 重发，fw 同 id
 *     去重（重发 ACK 不重复执行）
 *   - ACK（双向）：{err u8, acked_msg_id u8}
 *   - 重传/超时是应用层策略（定时器 + 同 id 重调发送函数），本层无状态
 *   - MSG_ID 各自方向独立自增（app 管 GET/SET，fw 管 REPORT/ACK）
 *
 * TLV：T u8 只增不改；字段不够用时载荷首字节做子命令扩展（对 proto
 * 层透明）。on_* 回调里的 tlv 指针指向内部重组缓冲，回调返回前有效。
 */
#ifndef PROTO_H
#define PROTO_H

#include <stdint.h>

/* 消息类型（只增不改） */
enum {
    PROTO_GET_REQ = 0x0001,   /* app→fw */
    PROTO_REPORT  = 0x0002,   /* fw→app */
    PROTO_SET     = 0x0003,   /* app→fw */
    PROTO_ACK     = 0x0004,   /* 双向 */
};

/* 帧头标志（SEQFLAGS 字节） */
#define PROTO_F_MORE    0x10u   /* 后续还有分片（仅 BLE 路径） */
#define PROTO_F_ACK_REQ 0x20u   /* REPORT 需确认（状态类） */
#define PROTO_SEQ_MASK  0x0Fu

/* 消息总长上限（UART 接收 cap；全量 TLV 同步 ~100B） */
#define PROTO_MSG_MAX 1024u

/* TLV 字段注册表（首批实现；扩展规则见 docs/proto.md） */
enum {
    PROTO_T_BATTERY  = 0x01,   /* {level u8, voltage_mv u16BE, flags u8}     L=4 */
    PROTO_T_TIME     = 0x02,   /* {epoch_s u32BE, tz_min i16BE}              L=6 */
    PROTO_T_DEV_INFO = 0x03,   /* {fw_maj, fw_min, fw_patch, hw_rev, name[]} 变长 */
    PROTO_T_UPTIME   = 0x04,   /* {uptime_s u32BE}                           L=4 */
    PROTO_T_STATUS   = 0x05,   /* {flags u32BE}                              L=4 */
};

typedef struct {
    uint8_t        t;
    uint8_t        l;
    const uint8_t *v;
} proto_tlv_t;

typedef struct proto_ops {
    /* 发送一个已分片+CRC 的传输帧（UART 侧再包 COBS；BLE 侧即 ATT 载荷） */
    void (*send)(const uint8_t *frag, uint32_t len, void *arg);
    /* BLE=协商后 ATT 载荷上限（如 244）；UART=0（整包单帧不分片） */
    uint32_t (*mtu_get)(void);
    void *arg;
    /* 事件回调（对应端实现，均可 NULL） */
    void (*on_get)(const uint8_t *types, uint32_t n, uint8_t msg_id);
    void (*on_set)(const proto_tlv_t *tlvs, uint32_t n, uint8_t msg_id);
    void (*on_report)(const proto_tlv_t *tlvs, uint32_t n, uint8_t msg_id);
    void (*on_ack)(uint8_t err, uint8_t msg_id);
} proto_ops_t;

void proto_init(const proto_ops_t *ops);

/* 喂入一个传输帧（UART=COBS 解码后；BLE=notification 载荷）。
 * CRC 错/长度不符/SEQ 断片：静默丢弃，计数可观测。 */
void proto_feed(const uint8_t *frag, uint32_t len);

/* 发送（按 mtu_get 自动分片；mtu=0 整包）。0 成功，-1 参数错/超上限。 */
int proto_get_req(const uint8_t *types, uint32_t n, uint8_t msg_id);   /* n=0 全部 */
int proto_report(const proto_tlv_t *tlvs, uint32_t n, uint8_t msg_id, int ack_req);
int proto_set(const proto_tlv_t *tlvs, uint32_t n, uint8_t msg_id);
int proto_ack(uint8_t err, uint8_t acked_msg_id);

/* CRC16 参考实现（app 侧移植对照用；"123456789" → 0x29B1） */
uint16_t proto_crc16(const uint8_t *p, uint32_t n);

/* 错误观测 */
uint32_t proto_crc_errors(void);
uint32_t proto_parse_errors(void);

#endif /* PROTO_H */

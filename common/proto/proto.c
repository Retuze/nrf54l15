/*
 * proto.c — 固件/app 通用应用协议实现（纯 C，无寄存器访问，宿主可测）。
 * 帧格式/语义见 proto.h 与 docs/proto.md。
 */
#include "proto.h"

#define HDR_LEN 6u
#define CRC_LEN 2u

static const proto_ops_t *s_ops;
static uint32_t s_crc_errors;
static uint32_t s_parse_errors;

/* ------------------------------------------------ CRC16 -- */
uint16_t proto_crc16(const uint8_t *p, uint32_t n)
{
    uint16_t crc = 0xFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        crc ^= (uint16_t)p[i] << 8;
        for (int k = 0; k < 8; k++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint32_t proto_crc_errors(void)  { return s_crc_errors; }
uint32_t proto_parse_errors(void) { return s_parse_errors; }

/* ------------------------------------------------ 字节序 -- */
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static void put_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

/* ------------------------------------------------ 发送 -- */
static uint8_t s_tx[PROTO_MSG_MAX];     /* 载荷序列化缓冲 */
static uint8_t s_send[PROTO_MSG_MAX];   /* 发送组装缓冲（与 s_tx 分离，
                                           否则 CRC 会覆盖尚未分发的载荷尾） */

/* TLV 列表序列化进 s_tx[HDR_LEN..]，返回载荷长度（-1 超限） */
static int build_tlv_payload(const proto_tlv_t *tlvs, uint32_t n)
{
    uint32_t pos = HDR_LEN;
    for (uint32_t i = 0; i < n; i++) {
        if (pos + 2u + tlvs[i].l > PROTO_MSG_MAX - CRC_LEN) {
            return -1;
        }
        s_tx[pos++] = tlvs[i].t;
        s_tx[pos++] = tlvs[i].l;
        for (uint32_t k = 0; k < tlvs[i].l; k++) {
            s_tx[pos++] = tlvs[i].v[k];
        }
    }
    return (int)(pos - HDR_LEN);
}

static void send_frag(uint16_t type, uint8_t flags, uint8_t msg_id,
                      const uint8_t *payload, uint32_t plen)
{
    put_be16(s_send, type);
    s_send[2] = flags;
    s_send[3] = msg_id;
    put_be16(s_send + 4, (uint16_t)plen);
    for (uint32_t i = 0; i < plen; i++) {
        s_send[HDR_LEN + i] = payload[i];
    }
    uint16_t crc = proto_crc16(s_send, HDR_LEN + plen);
    s_send[HDR_LEN + plen]     = (uint8_t)(crc >> 8);
    s_send[HDR_LEN + plen + 1] = (uint8_t)crc;
    s_ops->send(s_send, HDR_LEN + plen + CRC_LEN, s_ops->arg);
}

static int send_message(uint16_t type, uint8_t msg_id, int ack_req,
                        const uint8_t *payload, uint32_t plen)
{
    if (s_ops == 0 || s_ops->send == 0) {
        return -1;
    }
    if (HDR_LEN + plen + CRC_LEN > PROTO_MSG_MAX) {
        return -1;
    }
    uint32_t mtu = (s_ops->mtu_get != 0) ? s_ops->mtu_get() : 0u;
    if (mtu != 0u && mtu < HDR_LEN + CRC_LEN + 1u) {
        return -1;                        /* chunk 会下溢：mtu 至少装下头+CRC+1 */
    }
    if (mtu == 0u || HDR_LEN + plen + CRC_LEN <= mtu) {
        /* 整包单帧（UART 路径恒定；BLE 小消息也走这里） */
        uint8_t flags = (uint8_t)(ack_req ? PROTO_F_ACK_REQ : 0u);
        send_frag(type, flags, msg_id, payload, plen);
        return 0;
    }
    /* BLE 分片：只切载荷，SEQ/MORE 在头里 */
    uint32_t chunk = mtu - HDR_LEN - CRC_LEN;
    uint32_t nfrag = (plen + chunk - 1u) / chunk;
    if (nfrag > 16u) {
        return -1;                        /* SEQ 4bit 上限 */
    }
    for (uint32_t i = 0; i < nfrag; i++) {
        uint32_t off = i * chunk;
        uint32_t n = plen - off;
        if (n > chunk) n = chunk;
        uint8_t flags = (uint8_t)(i & PROTO_SEQ_MASK);
        if (i + 1u < nfrag) flags |= PROTO_F_MORE;
        if (ack_req) flags |= PROTO_F_ACK_REQ;
        send_frag(type, flags, msg_id, payload + off, n);
    }
    return 0;
}

int proto_get_req(const uint8_t *types, uint32_t n, uint8_t msg_id)
{
    if (n > PROTO_MSG_MAX - HDR_LEN - CRC_LEN) {
        return -1;
    }
    return send_message(PROTO_GET_REQ, msg_id, 0, types, n);
}

int proto_report(const proto_tlv_t *tlvs, uint32_t n, uint8_t msg_id, int ack_req)
{
    int plen = build_tlv_payload(tlvs, n);
    if (plen < 0) {
        return -1;
    }
    return send_message(PROTO_REPORT, msg_id, ack_req, s_tx + HDR_LEN, (uint32_t)plen);
}

int proto_set(const proto_tlv_t *tlvs, uint32_t n, uint8_t msg_id)
{
    int plen = build_tlv_payload(tlvs, n);
    if (plen < 0) {
        return -1;
    }
    return send_message(PROTO_SET, msg_id, 0, s_tx + HDR_LEN, (uint32_t)plen);
}

int proto_ack(uint8_t err, uint8_t acked_msg_id)
{
    uint8_t payload[2] = { err, acked_msg_id };
    return send_message(PROTO_ACK, 0u, 0, payload, 2u);
}

/* ------------------------------------------------ 接收 -- */
static uint8_t  s_rx[PROTO_MSG_MAX];
static uint32_t s_rx_len;
static uint16_t s_rx_type;
static uint8_t  s_rx_msg_id;
static uint8_t  s_rx_seq;                 /* 期望的下一个 SEQ */
static int      s_rx_active;

/* 把 PAYLOAD 里的 TLV 序列解析成指针数组（指向 s_rx，回调返回前有效）。
 * 返回 TLV 数；载荷损坏返回 -1（parse_errors 已计）。 */
#define RX_TLV_MAX 64u
static int parse_tlvs(const uint8_t *p, uint32_t len, proto_tlv_t *out)
{
    uint32_t pos = 0, n = 0;
    while (pos < len) {
        if (len - pos < 2u) {
            return -1;
        }
        uint8_t l = p[pos + 1];
        if (2u + l > len - pos) {
            return -1;
        }
        if (n >= RX_TLV_MAX) {
            return -1;
        }
        out[n].t = p[pos];
        out[n].l = l;
        out[n].v = p + pos + 2;
        n++;
        pos += 2u + l;
    }
    return (int)n;
}

static void dispatch(uint16_t type, uint8_t msg_id,
                     const uint8_t *payload, uint32_t plen)
{
    switch (type) {
    case PROTO_GET_REQ:
        if (s_ops && s_ops->on_get) {
            s_ops->on_get(payload, plen, msg_id);
        }
        break;
    case PROTO_REPORT: {
        proto_tlv_t tlvs[RX_TLV_MAX];
        int n = parse_tlvs(payload, plen, tlvs);
        if (n < 0) { s_parse_errors++; break; }
        if (s_ops && s_ops->on_report) {
            s_ops->on_report(tlvs, (uint32_t)n, msg_id);
        }
        break;
    }
    case PROTO_SET: {
        proto_tlv_t tlvs[RX_TLV_MAX];
        int n = parse_tlvs(payload, plen, tlvs);
        if (n < 0) { s_parse_errors++; break; }
        if (s_ops && s_ops->on_set) {
            s_ops->on_set(tlvs, (uint32_t)n, msg_id);
        }
        break;
    }
    case PROTO_ACK:
        if (plen >= 2u && s_ops && s_ops->on_ack) {
            s_ops->on_ack(payload[0], payload[1]);
        }
        break;
    default:
        s_parse_errors++;
        break;
    }
}

void proto_feed(const uint8_t *frag, uint32_t len)
{
    /* 帧长上限是协议层自己的契约（PROTO_MSG_MAX）：必须在拷贝前拒绝，
     * 否则 s_rx 越界（UART 路径的 COBS 解码 cap 由传输层给，不信任） */
    if (len > PROTO_MSG_MAX) {
        s_parse_errors++;
        s_rx_active = 0;
        return;
    }
    if (len < HDR_LEN + CRC_LEN) {
        s_parse_errors++;
        s_rx_active = 0;
        return;
    }
    if (proto_crc16(frag, len - CRC_LEN) != be16(frag + len - CRC_LEN)) {
        s_crc_errors++;
        s_rx_active = 0;                   /* 半包作废 */
        return;
    }

    uint16_t type   = be16(frag);
    uint8_t  flags  = frag[2];
    uint8_t  msg_id = frag[3];
    uint32_t plen   = be16(frag + 4);
    if (plen != len - HDR_LEN - CRC_LEN) {
        s_parse_errors++;
        s_rx_active = 0;
        return;
    }
    uint8_t seq  = flags & PROTO_SEQ_MASK;
    int     more = (flags & PROTO_F_MORE) != 0;

    if (seq == 0u) {
        /* 新消息开始（单帧或首片） */
        s_rx_type   = type;
        s_rx_msg_id = msg_id;
        s_rx_seq    = 1u;
        s_rx_len    = plen;
        s_rx_active = 1;
        for (uint32_t i = 0; i < plen; i++) {
            s_rx[i] = frag[HDR_LEN + i];
        }
        if (!more) {
            s_rx_active = 0;
            dispatch(s_rx_type, s_rx_msg_id, s_rx, s_rx_len);
        }
        return;
    }

    /* 续片：SEQ 连续 + 同 type/msg_id + 不超限（减法比较防回绕） */
    if (s_rx_active && seq == s_rx_seq && type == s_rx_type &&
        msg_id == s_rx_msg_id && plen <= PROTO_MSG_MAX - s_rx_len) {
        for (uint32_t i = 0; i < plen; i++) {
            s_rx[s_rx_len + i] = frag[HDR_LEN + i];
        }
        s_rx_len += plen;
        s_rx_seq++;
        if (!more) {
            s_rx_active = 0;
            dispatch(s_rx_type, s_rx_msg_id, s_rx, s_rx_len);
        }
        return;
    }

    s_parse_errors++;                      /* 断片/错序/不匹配：整包作废 */
    s_rx_active = 0;
}

void proto_init(const proto_ops_t *ops)
{
    s_ops = ops;
    s_crc_errors = 0;
    s_parse_errors = 0;
    s_rx_active = 0;
}

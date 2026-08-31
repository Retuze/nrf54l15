/*
 * proto_test.c — 应用协议宿主测试（fake 传输注入，扮演 app/fw 两端）。
 *
 * 覆盖：CRC16 标准向量、REPORT/SET/GET/ACK 往返、分片（mtu 注入）与
 * 断片丢弃、ack_req 标志、CRC 损坏、长度不符、TLV 损坏、超限拒绝。
 */
#include <string.h>
#include "tf.h"
#include "proto.h"

/* ---------------- fake 传输 ---------------- */
static uint8_t  sent[32][1100];
static uint32_t sent_len[32];
static uint32_t n_sent;
static uint32_t fake_mtu;

static void fake_send(const uint8_t *frag, uint32_t len, void *arg)
{
    (void)arg;
    if (n_sent < 32u) {
        for (uint32_t i = 0; i < len; i++) sent[n_sent][i] = frag[i];
        sent_len[n_sent] = len;
    }
    n_sent++;
}
static uint32_t fake_mtu_get(void) { return fake_mtu; }

/* ---------------- 回调记录 ---------------- */
static uint8_t  cb_types[64];  static uint32_t cb_ntypes;
static uint8_t  cb_get_id;
static proto_tlv_t cb_tlvs[64]; static uint32_t cb_ntlvs;
static uint8_t  cb_msg_id;
static uint8_t  cb_ack_err, cb_ack_id; static uint32_t cb_ack_n;

static void fake_on_get(const uint8_t *types, uint32_t n, uint8_t id)
{
    cb_ntypes = n; cb_get_id = id;
    for (uint32_t i = 0; i < n && i < 64u; i++) cb_types[i] = types[i];
}
static void fake_on_report(const proto_tlv_t *tlvs, uint32_t n, uint8_t id)
{
    cb_ntlvs = n; cb_msg_id = id;
    for (uint32_t i = 0; i < n && i < 64u; i++) cb_tlvs[i] = tlvs[i];
}
static void fake_on_set(const proto_tlv_t *tlvs, uint32_t n, uint8_t id)
{
    cb_ntlvs = n; cb_msg_id = id;
    for (uint32_t i = 0; i < n && i < 64u; i++) cb_tlvs[i] = tlvs[i];
}
static void fake_on_ack(uint8_t err, uint8_t id)
{
    cb_ack_err = err; cb_ack_id = id; cb_ack_n++;
}

static proto_ops_t ops = {
    .send = fake_send, .mtu_get = fake_mtu_get, .arg = 0,
    .on_get = fake_on_get, .on_report = fake_on_report,
    .on_set = fake_on_set, .on_ack = fake_on_ack,
};

static void fake_reset(void)
{
    n_sent = 0;
    cb_ntypes = 0; cb_get_id = 0;
    cb_ntlvs = 0; cb_msg_id = 0;
    cb_ack_err = cb_ack_id = 0; cb_ack_n = 0;
    proto_init(&ops);
}

/* 手工造帧（测试损坏路径用） */
static uint8_t frm[1100];
static uint32_t make_frame(uint16_t type, uint8_t flags, uint8_t msg_id,
                           const uint8_t *payload, uint32_t plen)
{
    frm[0] = (uint8_t)(type >> 8); frm[1] = (uint8_t)type;
    frm[2] = flags; frm[3] = msg_id;
    frm[4] = (uint8_t)(plen >> 8); frm[5] = (uint8_t)plen;
    for (uint32_t i = 0; i < plen; i++) frm[6 + i] = payload[i];
    uint16_t crc = proto_crc16(frm, 6 + plen);
    frm[6 + plen] = (uint8_t)(crc >> 8);
    frm[6 + plen + 1] = (uint8_t)crc;
    return 8 + plen;
}

/* ---------------- 用例 ---------------- */
static void test_crc16_vector(void)
{
    CHECK_EQ(proto_crc16((const uint8_t *)"123456789", 9u), 0x29B1u);
}

static void test_report_roundtrip(void)
{
    fake_reset();
    fake_mtu = 0;                       /* UART：整包 */
    proto_tlv_t tlvs[3] = {
        { PROTO_T_BATTERY, 4, (const uint8_t[]){ 85, 0x0E, 0x60, 0 } },
        { PROTO_T_UPTIME, 4, (const uint8_t[]){ 0, 0, 0, 0x10 } },
        { PROTO_T_TIME, 6, (const uint8_t[]){ 0, 0, 0, 0, 0, 0 } },
    };

    CHECK_EQ(proto_report(tlvs, 3, 7u, 1), 0);
    CHECK_EQ(n_sent, 1u);               /* mtu=0：单帧 */
    CHECK_EQ(sent[0][2] & PROTO_F_ACK_REQ, PROTO_F_ACK_REQ);
    CHECK_EQ(sent[0][3], 7u);           /* msg_id */

    proto_feed(sent[0], sent_len[0]);
    CHECK_EQ(cb_ntlvs, 3u);
    CHECK_EQ(cb_msg_id, 7u);
    CHECK_EQ(cb_tlvs[0].t, PROTO_T_BATTERY);
    CHECK_EQ(cb_tlvs[0].l, 4u);
    CHECK_EQ(cb_tlvs[0].v[0], 85u);
    CHECK_EQ(cb_tlvs[0].v[1], 0x0Eu);   /* 大端电压高字节 */
    CHECK_EQ(cb_tlvs[1].t, PROTO_T_UPTIME);
    CHECK_EQ(cb_tlvs[2].t, PROTO_T_TIME);
    CHECK_EQ(cb_tlvs[2].v[5], 0u);
}

static void test_ack_roundtrip(void)
{
    fake_reset();
    fake_mtu = 0;
    CHECK_EQ(proto_ack(3u, 9u), 0);
    proto_feed(sent[0], sent_len[0]);
    CHECK_EQ(cb_ack_n, 1u);
    CHECK_EQ(cb_ack_err, 3u);
    CHECK_EQ(cb_ack_id, 9u);
}

static void test_set_and_get(void)
{
    fake_reset();
    fake_mtu = 0;
    /* SET{TIME} */
    proto_tlv_t t = { PROTO_T_TIME, 6, (const uint8_t[]){ 0x12, 0x34, 0x56, 0x78, 0, 0 } };
    CHECK_EQ(proto_set(&t, 1, 42u), 0);
    proto_feed(sent[0], sent_len[0]);
    CHECK_EQ(cb_ntlvs, 1u);
    CHECK_EQ(cb_msg_id, 42u);
    CHECK_EQ(cb_tlvs[0].t, PROTO_T_TIME);

    /* GET 空 = 全部 */
    fake_reset();
    CHECK_EQ(proto_get_req(0, 0, 5u), 0);
    proto_feed(sent[0], sent_len[0]);
    CHECK_EQ(cb_ntypes, 0u);
    CHECK_EQ(cb_get_id, 5u);

    /* GET 显式字段 */
    fake_reset();
    const uint8_t req[2] = { PROTO_T_BATTERY, PROTO_T_TIME };
    CHECK_EQ(proto_get_req(req, 2, 6u), 0);
    proto_feed(sent[0], sent_len[0]);
    CHECK_EQ(cb_ntypes, 2u);
    CHECK_EQ(cb_types[0], PROTO_T_BATTERY);
    CHECK_EQ(cb_types[1], PROTO_T_TIME);
}

static void test_fragmentation(void)
{
    fake_reset();
    fake_mtu = 20;                      /* BLE：chunk = 12 */
    uint8_t big[50];
    for (int i = 0; i < 50; i++) big[i] = (uint8_t)i;
    proto_tlv_t t = { PROTO_T_DEV_INFO, 50, big };
    CHECK_EQ(proto_report(&t, 1, 3u, 0), 0);

    CHECK_EQ(n_sent, 5u);               /* ceil(50/12) */
    for (uint32_t i = 0; i < 4; i++) {
        CHECK_EQ(sent[i][2] & PROTO_SEQ_MASK, i);       /* SEQ 0..3 */
        CHECK((sent[i][2] & PROTO_F_MORE) != 0);        /* 非尾片 MORE */
    }
    CHECK_EQ(sent[4][2] & PROTO_SEQ_MASK, 4u);
    CHECK_EQ(sent[4][2] & PROTO_F_MORE, 0u);

    /* 逐片喂入 → 恰好派发一次 */
    for (uint32_t i = 0; i < n_sent; i++) {
        proto_feed(sent[i], sent_len[i]);
    }
    CHECK_EQ(cb_ntlvs, 1u);
    CHECK_EQ(cb_msg_id, 3u);
    CHECK_EQ(cb_tlvs[0].l, 50u);
    CHECK(memcmp(cb_tlvs[0].v, big, 50) == 0);
}

static void test_dropped_fragment(void)
{
    fake_reset();
    fake_mtu = 20;
    uint8_t big[40];
    for (int i = 0; i < 40; i++) big[i] = (uint8_t)(i + 1);
    proto_tlv_t t = { PROTO_T_DEV_INFO, 40, big };
    CHECK_EQ(proto_report(&t, 1, 1u, 0), 0);

    uint32_t nf = n_sent;
    /* 丢中间片（idx 1） */
    for (uint32_t i = 0; i < nf; i++) {
        if (i == 1u) continue;
        proto_feed(sent[i], sent_len[i]);
    }
    CHECK_EQ(cb_ntlvs, 0u);             /* 未派发 */
    CHECK(proto_parse_errors() > 0);

    /* 断片后新消息仍正常 */
    fake_reset();
    fake_mtu = 0;
    proto_tlv_t ok = { PROTO_T_UPTIME, 4, (const uint8_t[]){ 0, 0, 0, 1 } };
    CHECK_EQ(proto_report(&ok, 1, 2u, 0), 0);
    proto_feed(sent[0], sent_len[0]);
    CHECK_EQ(cb_ntlvs, 1u);
    CHECK_EQ(cb_msg_id, 2u);
}

static void test_crc_corruption(void)
{
    fake_reset();
    fake_mtu = 0;
    proto_tlv_t t = { PROTO_T_UPTIME, 4, (const uint8_t[]){ 0, 0, 0, 9 } };
    CHECK_EQ(proto_report(&t, 1, 1u, 0), 0);

    uint32_t base = proto_crc_errors();
    sent[0][sent_len[0] / 2] ^= 0x55u;  /* 损坏一个字节 */
    proto_feed(sent[0], sent_len[0]);
    CHECK_EQ(proto_crc_errors(), base + 1u);
    CHECK_EQ(cb_ntlvs, 0u);
}

static void test_len_mismatch(void)
{
    /* LEN 字段声称 5、实际载荷 9：CRC 按"头(含假 LEN)+9 载荷"计算可骗过
     * 校验，随后 plen != len-8 应落 parse_errors */
    fake_reset();
    fake_mtu = 0;
    uint8_t payload[9] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99 };
    frm[0] = 0; frm[1] = PROTO_REPORT; frm[2] = 0; frm[3] = 1u;
    frm[4] = 0; frm[5] = 5;              /* 声称 5 */
    for (int i = 0; i < 9; i++) frm[6 + i] = payload[i];
    uint16_t crc = proto_crc16(frm, 6 + 9);
    frm[15] = (uint8_t)(crc >> 8);
    frm[16] = (uint8_t)crc;
    uint32_t base = proto_parse_errors();
    proto_feed(frm, 8 + 9);
    CHECK_EQ(proto_parse_errors(), base + 1u);
    CHECK_EQ(cb_ntlvs, 0u);
}

static void test_tlv_corrupt(void)
{
    fake_reset();
    fake_mtu = 0;
    /* TLV 声明 l=100 但载荷只有 1 字节 */
    uint8_t payload[3] = { PROTO_T_BATTERY, 100, 0x50 };
    uint32_t n = make_frame(PROTO_REPORT, 0, 1u, payload, 3);
    uint32_t base = proto_parse_errors();
    proto_feed(frm, n);
    CHECK_EQ(proto_parse_errors(), base + 1u);
    CHECK_EQ(cb_ntlvs, 0u);
}

static void test_oversize(void)
{
    fake_reset();
    fake_mtu = 0;
    uint8_t big[PROTO_MSG_MAX];
    proto_tlv_t t = { PROTO_T_DEV_INFO, (uint8_t)255, big };
    proto_tlv_t many[5];
    for (int i = 0; i < 5; i++) many[i] = t;   /* 5×257 > 上限 */
    CHECK_EQ(proto_report(many, 5, 1u, 0), -1);
}

static void test_no_ops(void)
{
    proto_init(0);
    fake_mtu = 0;
    proto_tlv_t t = { PROTO_T_UPTIME, 4, (const uint8_t[]){ 0, 0, 0, 1 } };
    CHECK_EQ(proto_report(&t, 1, 1u, 0), -1);
}

/* 超长帧必须在拷贝前拒绝（PROTO_MSG_MAX 契约） */
static void test_feed_oversize(void)
{
    fake_reset();
    fake_mtu = 0;
    uint8_t big[PROTO_MSG_MAX + 16];
    uint32_t base = proto_parse_errors();
    proto_feed(big, sizeof(big));
    CHECK_EQ(proto_parse_errors(), base + 1u);
    CHECK_EQ(cb_ntlvs, 0u);
}

/* mtu 小于头+CRC+1 会 chunk 下溢：直接拒绝发送 */
static void test_mtu_too_small(void)
{
    fake_reset();
    fake_mtu = 8;
    proto_tlv_t t = { PROTO_T_UPTIME, 4, (const uint8_t[]){ 0, 0, 0, 1 } };
    CHECK_EQ(proto_report(&t, 1, 1u, 0), -1);
    CHECK_EQ(n_sent, 0u);

    fake_mtu = 9;                            /* 头+CRC+1：chunk=1，合法分片 */
    fake_reset();
    CHECK_EQ(proto_report(&t, 1, 1u, 0), 0);
    CHECK_EQ(n_sent, 6u);                    /* 载荷 = TLV(2 头 + 4 值) / chunk 1 */
}

int main(void)
{
    test_crc16_vector();
    test_report_roundtrip();
    test_ack_roundtrip();
    test_set_and_get();
    test_fragmentation();
    test_dropped_fragment();
    test_crc_corruption();
    test_len_mismatch();
    test_tlv_corrupt();
    test_oversize();
    test_no_ops();
    test_feed_oversize();
    test_mtu_too_small();
    TF_END();
}

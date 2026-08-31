/*
 * gatt_test.c — ATT/GATT 服务端字节级宿主测试（纯协议，无硬件依赖）。
 *
 * 直接编译 common/bluetooth/gatt/gatt.c，验证：
 *   - MTU 交换（RSP 恒为 247，g_mtu = min(client, 247) 且下限 23）
 *   - READ_BY_TYPE/READ_BY_GROUP 定位与 end-group 计算
 *   - READ/BLOB 的 chunk 上限随 MTU/DLE 变化（max_chunk 路径）
 *   - 错误路径 OP_ERROR 的 5 字节格式
 *   - WRITE_REQ/WRITE_CMD 行为
 *
 * gatt.c 的 g_mtu/g_tx_octets 是模块 static，测试靠 gatt_on_connect()/
 * gatt_set_tx_octets()/gatt_dbg_*() 控制与观测。
 */
#include "tf.h"
#include "gatt.h"

static uint8_t rsp[300];

/* 断言应答前 n 字节与期望一致（并顺带断言总长度） */
static void expect(const uint8_t *want, uint32_t want_len, uint32_t got_len)
{
    CHECK_EQ(got_len, want_len);
    for (uint32_t i = 0; i < want_len; i++) {
        if (rsp[i] != want[i]) {
            TF_FAIL("rsp[%u] = 0x%02x, want 0x%02x", i, rsp[i], want[i]);
            break;
        }
    }
}

/* ------------------------------------------------ MTU exchange ---- */
static void test_mtu(void)
{
    const uint8_t req_client23[] = { 0x02, 0x17, 0x00 };   /* client MTU 23 */
    const uint8_t want23[]       = { 0x03, 0xF7, 0x00 };   /* RSP 恒 247 */
    gatt_on_connect();
    expect(want23, 3, gatt_handle_att(req_client23, 3, rsp));
    CHECK_EQ(gatt_dbg_mtu(), 23u);

    const uint8_t req_client512[] = { 0x02, 0x00, 0x02 };  /* 512 > 247 */
    gatt_handle_att(req_client512, 3, rsp);
    CHECK_EQ(gatt_dbg_mtu(), 247u);

    const uint8_t req_client247[] = { 0x02, 0xF7, 0x00 };
    gatt_handle_att(req_client247, 3, rsp);
    CHECK_EQ(gatt_dbg_mtu(), 247u);

    const uint8_t req_client10[] = { 0x02, 0x0A, 0x00 };   /* 低于下限 */
    gatt_handle_att(req_client10, 3, rsp);
    CHECK_EQ(gatt_dbg_mtu(), 23u);

    /* len < 3：无 client MTU 字段 → 按 23 处理 */
    const uint8_t req_bare[] = { 0x02 };
    gatt_handle_att(req_bare, 1, rsp);
    CHECK_EQ(gatt_dbg_mtu(), 23u);

    /* 空请求 */
    CHECK_EQ(gatt_handle_att(req_bare, 0, rsp), 0u);
}

/* ------------------------------------------- READ_BY_TYPE ---- */
static void test_read_by_type(void)
{
    /* t=0x2803 s=1 e=0xFFFF → handle 2 的声明 */
    const uint8_t req[] = { 0x08, 0x01, 0x00, 0xFF, 0xFF, 0x03, 0x28 };
    const uint8_t want[] = { 0x09, 7, 0x02, 0x00, 0x02, 0x03, 0x00, 0x00, 0x2A };
    expect(want, 9, gatt_handle_att(req, 7, rsp));

    /* 范围内没有 → OP_ERROR(REQ=0x08, handle=s, E_ATTR_NOT_FOUND) */
    const uint8_t req_none[] = { 0x08, 0x0D, 0x00, 0x14, 0x00, 0x03, 0x28 };
    const uint8_t want_err[] = { 0x01, 0x08, 0x0D, 0x00, 0x0A };
    expect(want_err, 5, gatt_handle_att(req_none, 7, rsp));

    /* len 不足 → 错误（handle 0） */
    const uint8_t req_short[] = { 0x08, 0x01 };
    const uint8_t want_short[] = { 0x01, 0x08, 0x00, 0x00, 0x0A };
    expect(want_short, 5, gatt_handle_att(req_short, 2, rsp));
}

/* ------------------------------------------ READ_BY_GROUP ---- */
static void test_read_by_group(void)
{
    /* t=0x2800 s=1 → GAP service：end group = 7（下一个 0x2800 是 8） */
    const uint8_t req1[] = { 0x10, 0x01, 0x00, 0xFF, 0xFF, 0x00, 0x28 };
    const uint8_t want1[] = { 0x11, 6, 0x01, 0x00, 0x07, 0x00, 0x00, 0x18 };
    expect(want1, 8, gatt_handle_att(req1, 7, rsp));

    /* s=8 → BAT service：end group = 10 */
    const uint8_t req2[] = { 0x10, 0x08, 0x00, 0xFF, 0xFF, 0x00, 0x28 };
    const uint8_t want2[] = { 0x11, 6, 0x08, 0x00, 0x0A, 0x00, 0x0F, 0x18 };
    expect(want2, 8, gatt_handle_att(req2, 7, rsp));

    /* s=11 → custom service：end group = 14（0xFFF1 值后带 CCCD） */
    const uint8_t req3[] = { 0x10, 0x0B, 0x00, 0xFF, 0xFF, 0x00, 0x28 };
    const uint8_t want3[] = { 0x11, 6, 0x0B, 0x00, 0x0E, 0x00, 0xF0, 0xFF };
    expect(want3, 8, gatt_handle_att(req3, 7, rsp));
}

/* --------------------------------------------- FIND_INFO ---- */
static void test_find_info(void)
{
    const uint8_t req[] = { 0x04, 0x03, 0x00, 0x03, 0x00 };
    const uint8_t want[] = { 0x05, 0x01, 0x03, 0x00, 0x00, 0x2A };
    expect(want, 6, gatt_handle_att(req, 5, rsp));

    /* 0x000E = 0xFFF1 的 CCCD（notify 订阅开关） */
    const uint8_t req_cccd[] = { 0x04, 0x0E, 0x00, 0x0F, 0x00 };
    const uint8_t want_cccd[] = { 0x05, 0x01, 0x0E, 0x00, 0x02, 0x29 };
    expect(want_cccd, 6, gatt_handle_att(req_cccd, 5, rsp));

    const uint8_t req_none[] = { 0x04, 0x0F, 0x00, 0x0F, 0x00 };
    const uint8_t want_err[] = { 0x01, 0x04, 0x0F, 0x00, 0x0A };
    expect(want_err, 5, gatt_handle_att(req_none, 5, rsp));
}

/* ------------------------------------- 客户端角色 MTU 交换 ---- */
static void test_client_mtu_exchange(void)
{
    uint8_t out[8];
    gatt_on_connect();
    /* 未交换：pull 给出 MTU_REQ（一次性） */
    CHECK_EQ(gatt_client_pull(out, sizeof out), 3u);
    CHECK_EQ(out[0], 0x02u);
    CHECK_EQ(out[1], 0xF7u);                 /* 247 LE */
    CHECK_EQ(out[2], 0x00u);
    CHECK_EQ(gatt_client_pull(out, sizeof out), 0u);   /* 不重发 */

    /* MTU_RSP（对端 server rx=185）→ 采纳 min(247,185)，且无响应 */
    const uint8_t rsp[] = { 0x03, 185, 0x00 };
    CHECK_EQ(gatt_handle_att(rsp, 3, out), 0u);
    CHECK_EQ(gatt_dbg_mtu(), 185u);

    /* 重连复位后：若对端先发 MTU_REQ，我们不再发起 */
    gatt_on_connect();
    const uint8_t req[] = { 0x02, 0x00, 0x02 };        /* client rx=512 */
    gatt_handle_att(req, 3, out);
    CHECK_EQ(gatt_dbg_mtu(), 247u);                    /* min(512,247) */
    CHECK_EQ(gatt_client_pull(out, sizeof out), 0u);

    /* 迟到/未发起时的杂散 MTU_RSP：吞掉不回，也不改 MTU */
    gatt_on_connect();
    const uint8_t rsp2[] = { 0x03, 50, 0x00 };
    CHECK_EQ(gatt_handle_att(rsp2, 3, out), 0u);
    CHECK_EQ(gatt_dbg_mtu(), 23u);
    CHECK_EQ(gatt_client_pull(out, sizeof out), 0u);   /* exch 已标完成 */
}

/* --------------------------------------------------- CCCD ---- */
static void test_cccd_subscribe(void)
{
    gatt_on_connect();
    CHECK_EQ(gatt_notify_enabled(), 0u);

    /* WRITE_REQ CCCD = 0x0001 → WRITE_RSP，订阅生效 */
    const uint8_t req_on[] = { 0x12, 0x0E, 0x00, 0x01, 0x00 };
    const uint8_t want_rsp[] = { 0x13 };
    expect(want_rsp, 1, gatt_handle_att(req_on, 5, rsp));
    CHECK_EQ(gatt_notify_enabled(), 1u);

    /* 读回 CCCD 值 */
    const uint8_t req_rd[] = { 0x0A, 0x0E, 0x00 };
    const uint8_t want_rd[] = { 0x0B, 0x01, 0x00 };
    expect(want_rd, 3, gatt_handle_att(req_rd, 3, rsp));

    /* WRITE_CMD CCCD = 0x0000 → 无响应，订阅关闭 */
    const uint8_t req_off[] = { 0x52, 0x0E, 0x00, 0x00, 0x00 };
    CHECK_EQ(gatt_handle_att(req_off, 5, rsp), 0u);
    CHECK_EQ(gatt_notify_enabled(), 0u);

    /* 断链复位后保持关闭 */
    gatt_handle_att(req_on, 5, rsp);
    gatt_on_connect();
    CHECK_EQ(gatt_notify_enabled(), 0u);
}

/* --------------------------------------- READ / READ_BLOB ---- */
static void test_read_chunking(void)
{
    /* 默认状态（MTU 23 / tx 27）：max_chunk = min(22, 22) = 22 */
    gatt_on_connect();
    const uint8_t req_big[] = { 0x0A, 0x0D, 0x00 };   /* handle 0x000D，180B 大值 */
    uint32_t n = gatt_handle_att(req_big, 3, rsp);
    CHECK_EQ(n, 23u);                     /* 1 + 22 */
    CHECK_EQ(rsp[0], 0x0Bu);
    CHECK_EQ(rsp[1], 0u);                 /* v_big 填充 0..179 */
    CHECK_EQ(rsp[22], 21u);

    /* 小值整读：Device Name 8 字节 */
    const uint8_t req_name[] = { 0x0A, 0x03, 0x00 };
    n = gatt_handle_att(req_name, 3, rsp);
    CHECK_EQ(n, 9u);
    CHECK_EQ(rsp[0], 0x0Bu);
    CHECK(rsp[1] == '5' && rsp[8] == 'T');

    /* 非法 handle → E_INVALID_HANDLE */
    const uint8_t req_bad[] = { 0x0A, 0x55, 0x00 };
    const uint8_t want_bad[] = { 0x01, 0x0A, 0x55, 0x00, 0x01 };
    expect(want_bad, 5, gatt_handle_att(req_bad, 3, rsp));

    /* DLE + MTU 提升后：max_chunk = min(247-1, 251-4-1) = 246，
     * 但大值只有 180 字节 < 上限 → 整读返回（1 + 180） */
    const uint8_t req_mtu[] = { 0x02, 0xF7, 0x00 };
    gatt_handle_att(req_mtu, 3, rsp);
    gatt_set_tx_octets(251);
    CHECK_EQ(gatt_dbg_txoct(), 251u);
    n = gatt_handle_att(req_big, 3, rsp);
    CHECK_EQ(n, 181u);
    CHECK_EQ(rsp[1], 0u);
    CHECK_EQ(rsp[180], 179u);
}

static void test_read_blob(void)
{
    /* 复位状态后再测（不受上一个测试的 MTU 影响） */
    gatt_on_connect();

    /* offset 0 → 同 READ 首块 */
    const uint8_t req0[] = { 0x0C, 0x0D, 0x00, 0x00, 0x00 };
    uint32_t n = gatt_handle_att(req0, 5, rsp);
    CHECK_EQ(n, 23u);
    CHECK_EQ(rsp[0], 0x0Du);

    /* offset 170 → 剩余 10 字节 */
    const uint8_t req170[] = { 0x0C, 0x0D, 0x00, 0xAA, 0x00 };
    n = gatt_handle_att(req170, 5, rsp);
    CHECK_EQ(n, 11u);
    CHECK_EQ(rsp[1], 170u);
    CHECK_EQ(rsp[10], 179u);

    /* offset == vlen → 空块（n=0，只有 opcode） */
    const uint8_t req180[] = { 0x0C, 0x0D, 0x00, 0xB4, 0x00 };
    n = gatt_handle_att(req180, 5, rsp);
    CHECK_EQ(n, 1u);
    CHECK_EQ(rsp[0], 0x0Du);

    /* offset > vlen → E_INVALID_OFFSET */
    const uint8_t req200[] = { 0x0C, 0x0D, 0x00, 0xC8, 0x00 };
    const uint8_t want_off[] = { 0x01, 0x0C, 0x0D, 0x00, 0x07 };
    expect(want_off, 5, gatt_handle_att(req200, 5, rsp));
}

/* --------------------------------------------- WRITE ---- */
static uint16_t w_handle;
static uint8_t  w_val[260];
static uint32_t w_len, w_n;
static uint32_t fake_write_cb(uint16_t h, const uint8_t *v, uint32_t l, void *arg)
{
    (void)arg;
    w_handle = h;
    w_len = l;
    w_n++;
    for (uint32_t i = 0; i < l && i < 260u; i++) w_val[i] = v[i];
    return 0;
}

static void test_write(void)
{
    /* 未注册写回调：WRITE_REQ → E_WRITE_NOT_PERM；WRITE_CMD → 静默丢弃 */
    gatt_set_write_cb(0, 0);
    w_n = 0;
    const uint8_t req[] = { 0x12, 0x0D, 0x00, 0xAA };
    const uint8_t want[] = { 0x01, 0x12, 0x0D, 0x00, 0x03 };
    expect(want, 5, gatt_handle_att(req, 4, rsp));

    const uint8_t cmd0[] = { 0x52, 0x0D, 0x00, 0xAA };
    CHECK_EQ(gatt_handle_att(cmd0, 4, rsp), 0u);
    CHECK_EQ(w_n, 0u);

    /* 注册写回调：WRITE_CMD 走回调（值拷贝、无 ATT 响应） */
    gatt_set_write_cb(fake_write_cb, 0);
    const uint8_t cmd[] = { 0x52, 0x0D, 0x00, 0xAA, 0xBB };
    CHECK_EQ(gatt_handle_att(cmd, 5, rsp), 0u);
    CHECK_EQ(w_n, 1u);
    CHECK_EQ(w_handle, 0x000Du);
    CHECK_EQ(w_len, 2u);
    CHECK_EQ(w_val[0], 0xAAu);
    CHECK_EQ(w_val[1], 0xBBu);

    /* WRITE_REQ 走同一回调 + 空 WRITE_RSP */
    const uint8_t req2[] = { 0x12, 0x0D, 0x00, 0x01, 0x02, 0x03 };
    const uint8_t want_rsp[] = { 0x13 };
    expect(want_rsp, 1, gatt_handle_att(req2, 6, rsp));
    CHECK_EQ(w_n, 2u);
    CHECK_EQ(w_len, 3u);
    CHECK_EQ(w_val[2], 0x03u);
}

/* 0xFFF1 声明属性 = Read|WriteNoResp|Notify (0x16) */
static void test_notify_decl(void)
{
    const uint8_t req[] = { 0x08, 0x0C, 0x00, 0x0C, 0x00, 0x03, 0x28 };
    const uint8_t want[] = { 0x09, 7, 0x0C, 0x00, 0x16, 0x0D, 0x00, 0xF1, 0xFF };
    expect(want, 9, gatt_handle_att(req, 7, rsp));
}

/* --------------------------------------------- misc ---- */
static void test_unknown_op(void)
{
    const uint8_t req[] = { 0xFF, 0x00 };
    const uint8_t want[] = { 0x01, 0xFF, 0x00, 0x00, 0x06 };  /* E_REQ_NOT_SUP */
    expect(want, 5, gatt_handle_att(req, 2, rsp));
}

static void test_txoct_clamp(void)
{
    gatt_on_connect();
    CHECK_EQ(gatt_dbg_txoct(), 27u);
    gatt_set_tx_octets(3);                /* 低于下限 → 钳 27 */
    CHECK_EQ(gatt_dbg_txoct(), 27u);
    gatt_set_tx_octets(9999);             /* 超过上限 → 钳 251 */
    CHECK_EQ(gatt_dbg_txoct(), 251u);
}

int main(void)
{
    gatt_init();                          /* 填充 v_big[180] = 0..179 */
    test_mtu();
    test_read_by_type();
    test_read_by_group();
    test_find_info();
    test_client_mtu_exchange();
    test_cccd_subscribe();
    test_read_chunking();
    test_read_blob();
    test_write();
    test_notify_decl();
    test_unknown_op();
    test_txoct_clamp();
    TF_END();
}

/*
 * BLE Link Layer state machine (peripheral), pure protocol.
 *
 * 平台无关（common 规则）：不 include 任何 nRF 头，全部底层能力经
 * ll_ops_t 注入——目标机上由 main.c 把 drivers 的函数接线进来，
 * 宿主单测里注入 fake time/fake radio。物理时序（T_IFS/LEAD 校准）
 * 归 radio 驱动，本层不出现时序常量。
 *
 * 实现约束（测试依赖）：
 *   - 一切"等待"必须走 ops->delay_us（宿主 fake 靠它推进时间）；
 *     ops->now_us 只用于取时间戳/做差值计算。
 *   - ll_conn_run 阻塞到断链（TERMINATE 或 supervision 超时），
 *     统计填入 *st（环形缓冲也在里面）。
 */
#ifndef LL_H
#define LL_H

#include <stdint.h>

/* 连接参数：CONNECT_IND 解析结果 + update/channel-map 在 Instant 生效后的当前值 */
typedef struct {
    uint32_t aa, crcinit;
    uint32_t interval_us, winsize_us, winoffset_us, timeout_us;
    uint8_t  hop;
    uint8_t  chmap[5];
    uint8_t  used[37];             /* chmap 中已用信道的列表（CSA#1 重映射用） */
    uint8_t  num_used;
} ll_conn_t;

/* 连接结束后的统计（g_rxpdu 环形缓冲也在这里：由 LL 填充、main 打印） */
typedef struct {
    uint32_t events;               /* 总连接事件数 */
    uint32_t hits;                 /* 收包成功数 */
    uint32_t tx_done;              /* 完成发送次数 */
    uint32_t tx_timeouts;          /* 发送超时次数 */
    uint32_t maxrsp;               /* 最大 ATT 响应字节数 */
    uint32_t gap_us;               /* 首个事件 rx_end->tx_end 间隔 */
    uint32_t mtu, tx_octets;       /* 断开时经注入 getter 摘取 */
    uint32_t rxpdu_n;
    uint8_t  rxpdu[6][32];         /* 最近的 LL control / 非空 PDU 环形缓冲 */
    uint8_t  txhdr[6];             /* 对应事件我们回复的头字节（SN/NESN 对账） */
    uint16_t rxevt[6];             /* 对应事件的 connEventCounter */
} ll_stats_t;

/* 广播统计（跨 sweep 累计；adv 间隔与打印留 main 循环） */
typedef struct {
    uint32_t rx_ok, rx_err;
    uint32_t scan_req;             /* 定向到我们的 SCAN_REQ */
    uint32_t scan_rsp;             /* SCAN_RSP 按时发出（radio_reply_at 成功） */
} ll_adv_stats_t;

typedef struct ll_ops {
    /* ---- 时间 ---- */
    uint64_t (*now_us)(void);
    void     (*delay_us)(uint32_t us);
    /* ---- 射频（drivers/radio 的原样接线；T_IFS 时序在驱动内） ---- */
    void (*radio_set_aa)(uint32_t aa, uint32_t crcinit);
    void (*radio_set_channel)(uint32_t freq_off, uint32_t white_ch);
    void (*radio_disable)(void);
    int  (*radio_tx)(const uint8_t *pkt, uint32_t len, uint32_t timeout_us);
    int  (*radio_rx)(uint8_t *pkt, uint32_t maxlen, uint32_t window_us,
                     uint64_t *t_addr_us, uint64_t *t_end_us, int *crc_ok);
    int  (*radio_reply_at)(const uint8_t *pkt, uint32_t len, uint64_t rx_end_us);
    /* ---- 上层回调（ll 不 include gatt.h；均可 NULL） ---- */
    uint32_t (*att_handle)(const uint8_t *req, uint32_t len, uint8_t *out);
    void     (*on_dle)(uint32_t tx_octets);
    void     (*on_connect)(void);
    uint32_t (*mtu_get)(void);
    uint32_t (*txoct_get)(void);
    /* ---- 板级提示（可 NULL） ---- */
    void     (*led)(int on);
    /* ---- 连接事件观察（可 NULL；reply 之后调用 = 安全插桩点，勿阻塞） ---- */
    void     (*on_conn_event)(uint32_t counter, int crc_ok, uint32_t ch);
    /* ---- 下行 ATT 通知拉取（可 NULL）：reply 为空且 DLE 已完成时，LL 调它
     * 取一条待发 ATT 载荷（≤ max 字节）顶替空包；返回长度，0 = 无。
     * L2CAP 包装由 LL 负责。每个事件最多一条（响应优先于通知）。 ---- */
    uint32_t (*att_notify_pull)(uint8_t *att_out, uint32_t max);
} ll_ops_t;

/* 构建 ADV_IND/SCAN_RSP 模板（模块内缓冲）。广播名固定 "54L-GATT"。 */
void ll_init(const uint8_t *adv_addr);

/* 单轮 3 通道广播扫描。返回 1 = 收到发给我们的 CONNECT_IND（conn 已解析，
 * *t_ci_end = 包尾时间戳，供 ll_conn_run 起锚）；ast 累计 rx_ok/rx_err。 */
int  ll_adv_sweep(const ll_ops_t *ops, ll_conn_t *conn,
                  uint64_t *t_ci_end, ll_adv_stats_t *ast);

/* 连接状态机：阻塞到断链（TERMINATE 或 supervision 超时）。st 清零后填充。 */
void ll_conn_run(const ll_ops_t *ops, ll_conn_t *conn,
                 uint64_t t_ci_end, ll_stats_t *st);

#endif /* LL_H */

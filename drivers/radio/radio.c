#include "radio.h"
#include "time.h"
#include "nrf.h"
#include "nvic.h"

/*
 * nRF54L 无硬件 TIFS 回转，RX->TX 切换是软件定时：收到包结束后，等到
 * (T_IFS - TXEN ramp) 时刻发 TASKS_TXEN，回复恰好落在 RX 包尾后 T_IFS。
 * LEAD 按实测校准：rx_end->tx_end 间隙 ~230us（150 + 80）。见 01_conn
 * 原 conn_event 注释与 [conn] gap 统计。
 */
/* 2026-08-31 按实测标定：TXEN→READY ramp=43..45us，busy-wait 退出滞后
 * 2..6us，t_end 读取滞后 ~2us → LEAD=100 时空口 T_IFS ≈ 150us。
 * 手机（窗口严格）对早/晚 ~10us 都会拒收；PC 适配器宽容得多。 */
/* ==== 硬件 T_IFS（DPPI + TIMER10，全射频域，2026-08-31）====
 * 软件定时的 TXEN 有 3~10us 抖动（GRTC 读取量化+路径差异），恰好骑在安卓
 * 接收窗口边缘——温度/构建差异都能推过线（全天反复踩坑的根因之一）。
 * 硬件链：RADIO.PHYEND --DPPI ch0--> TIMER10.CLEAR（包尾精确清零重计）
 *         TIMER10.COMPARE[0] --DPPI ch1--> RADIO.TXEN（到点硬件触发）
 * 软件只负责在触发点前把包装好并使能订阅；发射时刻与软件路径无关，
 * 抖动 = 1 个 TIMER10 tick（62.5ns）。
 * TIFS_CC_US 整机标定：空口 T_IFS = CC + TXEN ramp(~41us)+DPPI 延迟，
 * 与旧软件定时的安卓可收值（TXEN≈PHYEND+102~106us）对齐取 104。 */
#define TIFS_DPPI_CH_PHYEND 0u
#define TIFS_DPPI_CH_TXEN   1u
#define TIFS_TICKS_PER_US   32u          /* TIMER10 @ PRESCALER=0 = 32MHz
                                          * （实测:按 16M 配时触发点对折在 52us） */
/* 触发点 = T_IFS − RX chain delay − TXEN ramp = 150 − 9.4 − 40.9 ≈ 100us
 * （常量取自 Zephyr radio_nrf54lx.h 官方实测:PHYEND 事件比空口末位晚
 * 9.4us,fast ramp TXEN→空口 40.9us）。按键扫掠实测（安卓为检测器,硬件
 * 零抖动）：98 不可见、101 可见、104 不可见——窗口中心 ~100,与理论吻合。
 * 此前软件定时(98+3..10us抖动)恰好骑窗,是全天"时好时坏"的总根源。 */
#define TIFS_CC_US          100u
#define TIFS_ARM_MARGIN_US  5u           /* 装订阅至少提前这么多,否则放弃 */
#define DPPI_EN             0x80000000u

/* 发送等 DISABLED 的上限：251 字节 DLE 空中约 2.1ms */
#define TX_DONE_TIMEOUT_US 3000u

/* CRC 判定最多等这么久（PHYEND 之后硬件很快置位） */
#define CRC_WAIT_US 20u

/* TIFS 软件定时抖动统计（radio_dbg_tifs 读取并复位） */
static uint32_t g_tifs_late_min = 0xFFFFFFFFu;
static uint32_t g_tifs_late_max;
static uint32_t g_ramp_min = 0xFFFFFFFFu;    /* TXEN→READY 实测 ramp（µs） */
static uint32_t g_ramp_max;

void radio_dbg_tifs(uint32_t *late_min, uint32_t *late_max,
                    uint32_t *ramp_min, uint32_t *ramp_max)
{
    *late_min = g_tifs_late_min;
    *late_max = g_tifs_late_max;
    *ramp_min = g_ramp_min;
    *ramp_max = g_ramp_max;
    g_tifs_late_min = 0xFFFFFFFFu;
    g_tifs_late_max = 0u;
    g_ramp_min = 0xFFFFFFFFu;
    g_ramp_max = 0u;
}

static void tifs_hw_init(void)
{
    NRF_TIMER10_S->TASKS_STOP = 1;
    NRF_TIMER10_S->MODE = 0;                          /* timer */
    NRF_TIMER10_S->BITMODE = 3;                       /* 32-bit */
    NRF_TIMER10_S->PRESCALER = 0;                     /* 16 MHz tick */
    NRF_TIMER10_S->CC[0] = TIFS_CC_US * TIFS_TICKS_PER_US;
    NRF_TIMER10_S->SUBSCRIBE_CLEAR = TIFS_DPPI_CH_PHYEND | DPPI_EN;
    NRF_TIMER10_S->PUBLISH_COMPARE[0] = TIFS_DPPI_CH_TXEN | DPPI_EN;
    NRF_RADIO_S->PUBLISH_PHYEND = TIFS_DPPI_CH_PHYEND | DPPI_EN;
    NRF_DPPIC10_S->CHENSET = (1u << TIFS_DPPI_CH_PHYEND) | (1u << TIFS_DPPI_CH_TXEN);
    NRF_TIMER10_S->TASKS_CLEAR = 1;
    NRF_TIMER10_S->TASKS_START = 1;
}

void radio_tifs_set_cc(uint32_t us)
{
    NRF_TIMER10_S->CC[0] = us * TIFS_TICKS_PER_US;
}

void radio_init(void)
{
    tifs_hw_init();
    NRF_RADIO_S->MODE = RADIO_MODE_MODE_Ble_1Mbit << RADIO_MODE_MODE_Pos;
    /* +8dBm：修调（FICR trim）后此档输出正常（安卓 15:29 实测可收）。
     * 注:未修调时此档输出失真(曾测得 0dBm 反而 RSSI 更高)——trim 先于
     * 功率档位选择,勿在未修调状态下评估功率。 */
    NRF_RADIO_S->TXPOWER = RADIO_TXPOWER_TXPOWER_Pos8dBm << RADIO_TXPOWER_TXPOWER_Pos;
    NRF_RADIO_S->PCNF0 =
        (8u << RADIO_PCNF0_LFLEN_Pos) |
        (1u << RADIO_PCNF0_S0LEN_Pos) |
        (RADIO_PCNF0_PLEN_8bit << RADIO_PCNF0_PLEN_Pos);
    NRF_RADIO_S->PCNF1 =
        (251u << RADIO_PCNF1_MAXLEN_Pos) |           /* allow DLE-sized PDUs */
        (3u   << RADIO_PCNF1_BALEN_Pos)  |
        (RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos) |
        (1u   << RADIO_PCNF1_WHITEEN_Pos);
    NRF_RADIO_S->CRCCNF =
        (RADIO_CRCCNF_LEN_Three     << RADIO_CRCCNF_LEN_Pos) |
        (RADIO_CRCCNF_SKIPADDR_Skip << RADIO_CRCCNF_SKIPADDR_Pos);
    NRF_RADIO_S->CRCPOLY = 0x00065Bu;
    NRF_RADIO_S->TXADDRESS   = 0u;
    NRF_RADIO_S->RXADDRESSES = 1u;
}

void radio_set_aa(uint32_t aa, uint32_t crcinit)
{
    NRF_RADIO_S->BASE0   = aa << 8;
    NRF_RADIO_S->PREFIX0 = (aa >> 24) & 0xFF;
    NRF_RADIO_S->CRCINIT = crcinit & 0xFFFFFF;
}

void radio_set_channel(uint32_t freq_off, uint32_t white_ch)
{
    NRF_RADIO_S->FREQUENCY = freq_off;
    NRF_RADIO_S->DATAWHITE =
        (NRF_RADIO_S->DATAWHITE & RADIO_DATAWHITE_POLY_Msk) | (0x40u | white_ch);
}

/* 异步模式状态（radio_irq_init/rx_arm/reply_arm + RADIO_0_IRQHandler） */
#define AMODE_IDLE 0u
#define AMODE_RX   1u
#define AMODE_TX   2u
#define AIRQ_MASK (RADIO_INTENSET00_ADDRESS_Msk | RADIO_INTENSET00_PHYEND_Msk | \
                   RADIO_INTENSET00_DISABLED_Msk)
static radio_evt_cb_t s_evt_cb;
static volatile uint8_t  s_amode;
static volatile uint64_t s_t_addr, s_t_end;

void radio_disable(void)
{
    NRF_RADIO_S->INTENCLR00 = AIRQ_MASK;     /* 异步模式撤收；同步路径无害 */
    NRF_RADIO_S->SUBSCRIBE_TXEN = 0;         /* 撤硬件 T_IFS 订阅 */
    s_amode = AMODE_IDLE;
    NRF_RADIO_S->SHORTS = 0;
    NRF_RADIO_S->EVENTS_DISABLED = 0;
    NRF_RADIO_S->TASKS_DISABLE = 1;
    while (NRF_RADIO_S->EVENTS_DISABLED == 0) {
    }
}

int radio_tx(const uint8_t *pkt, uint32_t len, uint32_t timeout_us)
{
    (void)len;   /* PLEN 由包内长度字节决定，此处只装指针 */
    NRF_RADIO_S->PACKETPTR = (uint32_t)pkt;
    NRF_RADIO_S->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_PHYEND_DISABLE_Msk;
    NRF_RADIO_S->EVENTS_DISABLED = 0;
    NRF_RADIO_S->TASKS_TXEN = 1;
    if (timeout_us == 0u) {
        while (NRF_RADIO_S->EVENTS_DISABLED == 0) {
        }
        return 1;
    }
    uint64_t tw = time_now_us();
    while (NRF_RADIO_S->EVENTS_DISABLED == 0) {
        if ((time_now_us() - tw) > timeout_us) {
            radio_disable();
            return 0;
        }
    }
    return 1;
}

int radio_rx(uint8_t *pkt, uint32_t maxlen, uint32_t window_us,
             uint64_t *t_addr_us, uint64_t *t_end_us, int *crc_ok)
{
    (void)maxlen;
    NRF_RADIO_S->PACKETPTR = (uint32_t)pkt;
    NRF_RADIO_S->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_PHYEND_DISABLE_Msk;
    NRF_RADIO_S->EVENTS_DISABLED = 0;
    NRF_RADIO_S->EVENTS_CRCOK    = 0;
    NRF_RADIO_S->EVENTS_CRCERROR = 0;
    NRF_RADIO_S->EVENTS_ADDRESS  = 0;
    NRF_RADIO_S->EVENTS_PHYEND   = 0;

    uint64_t t0 = time_now_us();
    NRF_RADIO_S->TASKS_RXEN = 1;

    /* 等包开始（ADDRESS）或窗口过期。 */
    while (NRF_RADIO_S->EVENTS_ADDRESS == 0) {
        if ((time_now_us() - t0) > window_us) {
            radio_disable();
            return 0;
        }
    }
    uint64_t addr_ts = time_now_us();

    /* 等包结束（PHYEND）。已知限制：无超时（沿袭重构前行为）。 */
    while (NRF_RADIO_S->EVENTS_PHYEND == 0 && NRF_RADIO_S->EVENTS_DISABLED == 0) {
    }
    uint64_t t_phyend = time_now_us();

    /* CRC 结果落在 PHYEND 之后。 */
    while (NRF_RADIO_S->EVENTS_CRCOK == 0 && NRF_RADIO_S->EVENTS_CRCERROR == 0) {
        if ((time_now_us() - t_phyend) > CRC_WAIT_US) {
            break;
        }
    }
    *crc_ok = NRF_RADIO_S->EVENTS_CRCOK ? 1 : 0;
    *t_addr_us = addr_ts;
    *t_end_us = t_phyend;
    return 1;
}

/* 装硬件 T_IFS 订阅（公共核）：包尾时 TIMER10 已被 PHYEND 硬件清零重计。
 * 返回 1 = 已装订阅（TXEN 将由硬件在 CC 时刻触发）；0 = 已过触发点，
 * 放弃（宁缺毋晚——迟到的包对端不认;响应留 LL pend 队列等对端重传）。
 * late 统计 = 软件装订阅时刻（PHYEND 后 µs）——即构建耗时的直接观测。 */
static int tifs_hw_arm(const uint8_t *pkt)
{
    NRF_RADIO_S->PACKETPTR = (uint32_t)pkt;
    NRF_TIMER10_S->TASKS_CAPTURE[1] = 1;
    uint32_t t = NRF_TIMER10_S->CC[1];
    {
        uint32_t us = t / TIFS_TICKS_PER_US;
        if (us < g_tifs_late_min) g_tifs_late_min = us;
        if (us > g_tifs_late_max) g_tifs_late_max = us;
    }
    if (t + TIFS_ARM_MARGIN_US * TIFS_TICKS_PER_US >= NRF_TIMER10_S->CC[0]) {
        radio_disable();
        return 0;
    }
    NRF_TIMER10_S->EVENTS_COMPARE[0] = 0;
    NRF_RADIO_S->SUBSCRIBE_TXEN = TIFS_DPPI_CH_TXEN | DPPI_EN;
    return 1;
}

int radio_reply_at(const uint8_t *pkt, uint32_t len, uint64_t rx_end_us)
{
    (void)len; (void)rx_end_us;
    /* RX 通道先停（PHYEND_DISABLE 短接保证收到包后必然自动停）。 */
    while (NRF_RADIO_S->EVENTS_DISABLED == 0) {
    }
    NRF_RADIO_S->EVENTS_DISABLED = 0;
    NRF_RADIO_S->EVENTS_PHYEND   = 0;

    if (!tifs_hw_arm(pkt)) {
        return 0;
    }

    uint64_t tw = time_now_us();
    while (NRF_RADIO_S->EVENTS_DISABLED == 0) {
        if ((time_now_us() - tw) > TX_DONE_TIMEOUT_US) {
            radio_disable();
            return 0;
        }
    }
    NRF_RADIO_S->SUBSCRIBE_TXEN = 0;   /* 一次性：防我们 TX 的 PHYEND 重触发 */
    {   /* 全程实测 rx_end→tx_end（借 ramp 统计槽;空包预期 ≈231us） */
        uint32_t gap = (uint32_t)(time_now_us() - rx_end_us);
        if (gap < g_ramp_min) g_ramp_min = gap;
        if (gap > g_ramp_max) g_ramp_max = gap;
    }
    return 1;
}

/* ==================================================== 异步（IRQ）API ==== */

void radio_irq_init(radio_evt_cb_t cb)
{
    s_evt_cb = cb;
    NRF_RADIO_S->INTENCLR00 = AIRQ_MASK;
    nvic_set_prio(RADIO_0_IRQn, 0u);         /* 与 GRTC 同优先级（都是默认 0）：
                                              * 回调互不抢占，LL 引擎单上下文 */
    nvic_enable(RADIO_0_IRQn);
}

void radio_rx_arm(uint8_t *pkt, uint32_t maxlen)
{
    (void)maxlen;                            /* MAXLEN 由 PCNF1 静态配置 */
    NRF_RADIO_S->PACKETPTR = (uint32_t)pkt;
    NRF_RADIO_S->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_PHYEND_DISABLE_Msk;
    NRF_RADIO_S->EVENTS_DISABLED = 0;
    NRF_RADIO_S->EVENTS_CRCOK    = 0;
    NRF_RADIO_S->EVENTS_CRCERROR = 0;
    NRF_RADIO_S->EVENTS_ADDRESS  = 0;
    NRF_RADIO_S->EVENTS_PHYEND   = 0;
    s_amode = AMODE_RX;
    NRF_RADIO_S->INTENSET00 = AIRQ_MASK;
    NRF_RADIO_S->TASKS_RXEN = 1;
}

int radio_reply_arm(const uint8_t *pkt, uint32_t len, uint64_t rx_end_us)
{
    (void)len; (void)rx_end_us;
    /* 调用方处于 RX 完成回调（DISABLED 已发生并被 ISR 清除），radio 空闲。
     * TXEN 由硬件 T_IFS 在 CC 时刻触发；TX 完成走 DISABLED IRQ（ISR 里撤
     * 订阅）。 */
    if (!tifs_hw_arm(pkt)) {
        return 0;
    }
    s_amode = AMODE_TX;
    return 1;
}

void RADIO_0_IRQHandler(void)
{
    /* 进门先取时间戳再分发：PHYEND 时刻是 T_IFS 的基准，晚读 2-3us 就会把
     * 回复推出安卓的接收窗口（同步引擎的紧轮询滞后 ~1us，这里要对齐）。 */
    uint64_t t_now = time_now_us();
    if (NRF_RADIO_S->EVENTS_ADDRESS) {
        NRF_RADIO_S->EVENTS_ADDRESS = 0;
        s_t_addr = t_now;                    /* RX：锚点时间戳（TX 时无害覆盖） */
    }
    if (NRF_RADIO_S->EVENTS_PHYEND) {
        NRF_RADIO_S->EVENTS_PHYEND = 0;
        s_t_end = t_now;                     /* RX：包尾时间戳（T_IFS 基准） */
    }
    if (NRF_RADIO_S->EVENTS_DISABLED) {
        NRF_RADIO_S->EVENTS_DISABLED = 0;
        uint8_t mode = s_amode;
        if (mode == AMODE_RX) {
            /* CRC 判定在 PHYEND 后极短时间内落位；DISABLED（经短接）到此
             * 已有 ~6us + ISR 延迟，兜底再等最多 CRC_WAIT_US。 */
            uint64_t t0 = time_now_us();
            while (NRF_RADIO_S->EVENTS_CRCOK == 0 && NRF_RADIO_S->EVENTS_CRCERROR == 0) {
                if ((time_now_us() - t0) > CRC_WAIT_US) break;
            }
            int crc_ok = NRF_RADIO_S->EVENTS_CRCOK ? 1 : 0;
            s_amode = AMODE_IDLE;            /* 回调内 reply_arm 可切到 TX */
            if (s_evt_cb) s_evt_cb(1, s_t_addr, s_t_end, crc_ok);
        } else if (mode == AMODE_TX) {
            NRF_RADIO_S->SUBSCRIBE_TXEN = 0;   /* 一次性硬件 T_IFS 撤订阅 */
            NRF_RADIO_S->INTENCLR00 = AIRQ_MASK;
            s_amode = AMODE_IDLE;
            if (s_evt_cb) s_evt_cb(0, 0, 0, 0);
        }
    }
}

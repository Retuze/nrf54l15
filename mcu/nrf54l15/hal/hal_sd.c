/*
 * hal_sd.c — SD card SPI driver (SPIM22, blocking EasyDMA).
 *
 * 参考: SD Specifications Part 1: Simplified Physical Layer
 */
#include "hal_sd.h"
#include "hal_gpio.h"
#include <hal/nrf_spim.h>
#include <hal/nrf_gpio.h>
#include <rtthread.h>

/* ---- 引脚 ------------------------------------------------------------- */
#define SD_SCK_PIN   PIN_P2(8)   /* D6 */
#define SD_MOSI_PIN  PIN_P2(7)   /* D7 */
#define SD_MISO_PIN  PIN_P2(1)   /* D8 */
#define SD_CS_PIN    PIN_P2(4)   /* D9 */

/* ---- SPIM22 时钟 ------------------------------------------------------ */
#define SPIM_BASE_HZ     16000000U
#define SPIM_SLOW_HZ     400000U
#define SPIM_FAST_HZ     8000000U
#define SPIM_SLOW_PSC    (SPIM_BASE_HZ / SPIM_SLOW_HZ)   /* 40 */
#define SPIM_FAST_PSC    (SPIM_BASE_HZ / SPIM_FAST_HZ)   /* 2  */

/* ---- SD 命令 ---------------------------------------------------------- */
#define CMD0    0     /* GO_IDLE_STATE      */
#define CMD8    8     /* SEND_IF_COND       */
#define CMD17   17    /* READ_SINGLE_BLOCK  */
#define CMD24   24    /* WRITE_BLOCK        */
#define CMD55   55    /* APP_CMD            */
#define CMD58   58    /* READ_OCR           */
#define ACMD41  41    /* SD_SEND_OP_COND    */

/* 响应标志 */
#define R1_IDLE      0x01
#define R1_ILLEGAL   0x04
#define R1_CRC_ERR   0x08
#define TOKEN_DATA   0xFE
#define TOKEN_ERR    0x01

/* ---- 卡状态 ----------------------------------------------------------- */
static bool     g_sd_ok;           /* 初始化成功 */
static bool     g_sd_sdhc;         /* SDHC/SDXC 卡 (true) 或 SDSC (false) */
static uint64_t g_sd_capacity;

static uint8_t  g_csd[16];
static uint8_t  g_cid[16];

/* ---- CS 控制 ---------------------------------------------------------- */
static inline void sd_cs_low(void)  { nrf_gpio_pin_write(SD_CS_PIN, 0); }
static inline void sd_cs_high(void) { nrf_gpio_pin_write(SD_CS_PIN, 1); }

/* ---- 超时 ------------------------------------------------------------- */
static uint32_t sd_timeout(void)
{
    return rt_tick_get() + (rt_tick_t)200; /* 200 ms */
}

/* ---- SPI 全双工传输 (阻塞) --------------------------------------------- */

static uint8_t g_sd_tx_buf[520] __attribute__((aligned(4)));
static uint8_t g_sd_rx_buf[520] __attribute__((aligned(4)));

static void spim_xfer_inner(const uint8_t *tx, uint8_t *rx, size_t len)
{
    if (!tx) {
        /* 纯 RX 模式: TX 发 0xFF (dummy), RX 收数据 */
        for (size_t i = 0; i < len; i++) {
            g_sd_tx_buf[i] = 0xFF;
        }
        tx = g_sd_tx_buf;
    }
    nrf_spim_event_clear(NRF_SPIM22, NRF_SPIM_EVENT_END);
    nrf_spim_tx_buffer_set(NRF_SPIM22, tx, len);
    nrf_spim_rx_buffer_set(NRF_SPIM22, rx, len);
    nrf_spim_task_trigger(NRF_SPIM22, NRF_SPIM_TASK_START);

    uint32_t deadline = sd_timeout();
    while (!nrf_spim_event_check(NRF_SPIM22, NRF_SPIM_EVENT_END)) {
        if (rt_tick_get() > deadline) break;
    }
}

static void spim_set_speed(uint32_t prescaler)
{
    nrf_spim_prescaler_set(NRF_SPIM22, prescaler);
}

/* ---- SPI 字节交换 ----------------------------------------------------- */

static uint8_t spi_byte(uint8_t tx)
{
    g_sd_tx_buf[0] = tx;
    spim_xfer_inner(g_sd_tx_buf, g_sd_rx_buf, 1);
    return g_sd_rx_buf[0];
}

/* ---- SD 命令 ---------------------------------------------------------- */

/*
 * 发送 6-byte SD 命令 + 接收 R1/R3/R7 响应。
 * cmd: 命令号 (0..63)
 * arg: 32-bit 参数
 * resp: 响应缓冲区 (可为 NULL)
 * resp_len: 期望的响应字节数 (通常 1, 3, 5)
 * 返回: R1 字节 (最低字节), 或 0xFF 表示超时
 */
static uint8_t sd_cmd(uint8_t cmd, uint32_t arg,
                      uint8_t *resp, uint8_t resp_len)
{
    /* 构建命令帧 */
    g_sd_tx_buf[0] = 0x40 | (cmd & 0x3F);
    g_sd_tx_buf[1] = (uint8_t)(arg >> 24);
    g_sd_tx_buf[2] = (uint8_t)(arg >> 16);
    g_sd_tx_buf[3] = (uint8_t)(arg >> 8);
    g_sd_tx_buf[4] = (uint8_t)(arg);
    /* CRC (只有 CMD0 和 CMD8 需要校验) */
    if (cmd == CMD0) {
        g_sd_tx_buf[5] = 0x95;
    } else if (cmd == CMD8) {
        g_sd_tx_buf[5] = 0x87;
    } else {
        g_sd_tx_buf[5] = 0x01; /* dummy */
    }

    /* 填充剩余 TX 为 0xFF, RX 全部接收 */
    uint8_t total = (uint8_t)(6 + resp_len);
    for (uint8_t i = 6; i < total; i++) {
        g_sd_tx_buf[i] = 0xFF;
    }

    spim_xfer_inner(g_sd_tx_buf, g_sd_rx_buf, total);

    /* 搜索响应起始 (跳过 0xFF) */
    uint8_t r1 = 0xFF;
    for (uint8_t i = 0; i < total; i++) {
        if (g_sd_rx_buf[i] != 0xFF) {
            r1 = g_sd_rx_buf[i];
            if (resp && resp_len > 0 && (i + 1 + resp_len - 1) < total) {
                for (uint8_t j = 0; j < resp_len; j++) {
                    resp[j] = g_sd_rx_buf[i + j];
                }
            }
            break;
        }
    }
    return r1;
}

/* ---- SD 初始化 -------------------------------------------------------- */

sd_err_t sd_init(void)
{
    if (g_sd_ok) return SD_OK;

    /* 1. 配置 GPIO */
    nrf_gpio_cfg_output(SD_SCK_PIN);
    nrf_gpio_cfg_output(SD_MOSI_PIN);
    nrf_gpio_cfg_input(SD_MISO_PIN, NRF_GPIO_PIN_PULLUP);
    nrf_gpio_cfg_output(SD_CS_PIN);

    /* CS 默认高, 引脚 disconnect → reset */
    sd_cs_high();

    /* 2. 配置 SPIM22: Mode 0, MSB first, ORC=0xFF */
    nrf_spim_pins_set(NRF_SPIM22, SD_SCK_PIN, SD_MOSI_PIN, SD_MISO_PIN);
    nrf_spim_configure(NRF_SPIM22, NRF_SPIM_MODE_0, NRF_SPIM_BIT_ORDER_MSB_FIRST);
    nrf_spim_orc_set(NRF_SPIM22, 0xFF);

    /* 禁用所有中断 (阻塞模式) */
    nrf_spim_int_disable(NRF_SPIM22, (uint32_t)-1);

    nrf_spim_enable(NRF_SPIM22);
    spim_set_speed(SPIM_SLOW_PSC);

    /* 3. 发送 80+ 个时钟脉冲 (CS=H) — 让卡进入 SPI 模式 */
    sd_cs_high();
    for (int i = 0; i < 12; i++) {
        spi_byte(0xFF);
    }

    /* 4. CMD0: GO_IDLE_STATE */
    sd_cs_low();
    uint8_t r1 = sd_cmd(CMD0, 0, NULL, 0);
    if (r1 != R1_IDLE) {
        sd_cs_high();
        return SD_ERR_INIT;
    }

    /* 5. CMD8: SEND_IF_COND (检查电压 2.7-3.6V) */
    uint8_t r7[5];
    r1 = sd_cmd(CMD8, 0x000001AA, r7, 5);
    if (r1 == R1_IDLE) {
        /* 回显正确 → v2.0+ 卡 */
        if (r7[3] == 0x01 && r7[4] == 0xAA) {
            /* voltage accepted */
        }
    } else if (r1 == (R1_IDLE | R1_ILLEGAL)) {
        /* v1.x 卡或 MMC → 可能不支持 CMD8 */
    }

    /* 6. ACMD41: 初始化, 等待退出 IDLE (HCS=1 兼容 SDSC/SDHC) */
    uint32_t deadline = sd_timeout();
    do {
        if (rt_tick_get() > deadline) {
            sd_cs_high();
            return SD_ERR_TIMEOUT;
        }
        r1 = sd_cmd(CMD55, 0, NULL, 0);       /* APP_CMD 前缀 */
        if (r1 > 1) continue;
        r1 = sd_cmd(ACMD41, 0x40000000UL, NULL, 0); /* HCS=1 */
    } while (r1 == R1_IDLE);

    if (r1 != 0) {
        sd_cs_high();
        return SD_ERR_INIT;
    }

    /* 7. CMD58: READ_OCR — 检查 CCS (bit30) 判断 SDHC/SDXC */
    uint8_t ocr[4];
    r1 = sd_cmd(CMD58, 0, ocr, 4);
    if (r1 == 0) {
        if (ocr[0] & 0x40) {
            g_sd_sdhc = true;  /* CCS=1 → SDHC/SDXC, 地址 = LBA */
        } else {
            g_sd_sdhc = false; /* CCS=0 → SDSC, 地址 = 字节偏移 */
        }
    }

    /* 8. 读取 CSD / CID */
    (void)sd_cmd(9, 0, g_csd, 16);   /* SEND_CSD */
    (void)sd_cmd(10, 0, g_cid, 16);  /* SEND_CID */

    /* 9. 解析容量 */
    uint8_t csd_ver = (g_csd[0] >> 6) & 0x03;
    if (csd_ver == 0) {
        /* CSD v1.0 (SDSC): C_SIZE + C_SIZE_MULT + READ_BL_LEN */
        uint32_t c_size   = ((uint32_t)(g_csd[6] & 0x03) << 10)
                          | ((uint32_t)g_csd[7] << 2)
                          | ((uint32_t)g_csd[8] >> 6);
        uint8_t  c_mult   = ((g_csd[9] & 0x03) << 1) | (g_csd[10] >> 7);
        uint8_t  bl_len   = g_csd[5] & 0x0F;
        g_sd_capacity = (uint64_t)(c_size + 1) * (1ULL << (c_mult + 2)) * (1ULL << bl_len);
    } else if (csd_ver == 1) {
        /* CSD v2.0 (SDHC/SDXC): C_SIZE */
        uint32_t c_size = ((uint32_t)(g_csd[7] & 0x3F) << 16)
                        | ((uint32_t)g_csd[8] << 8)
                        |  (uint32_t)g_csd[9];
        g_sd_capacity = (uint64_t)(c_size + 1) * 512ULL * 1024ULL;
    }

    /* 10. 切换到高速 SPI */
    spim_set_speed(SPIM_FAST_PSC);

    sd_cs_high();
    g_sd_ok = true;
    return SD_OK;
}

/* ---- 块读/写 ---------------------------------------------------------- */

/*
 * 读一个 512-byte 数据块。
 * 流程: CMD17 → 等 TOKEN_DATA (0xFE) → 读 512 bytes + 2 CRC
 */
sd_err_t sd_read_block(uint32_t block_addr, uint8_t *buf)
{
    if (!g_sd_ok) return SD_ERR_INIT;

    uint32_t addr = g_sd_sdhc ? block_addr : (block_addr * 512);

    sd_cs_low();
    uint8_t r1 = sd_cmd(CMD17, addr, NULL, 0);
    if (r1 != 0) {
        sd_cs_high();
        return SD_ERR_CMD;
    }

    /* 等待数据令牌 0xFE */
    uint32_t deadline = sd_timeout();
    uint8_t  token;
    do {
        token = spi_byte(0xFF);
        if (token != 0xFF) break;
    } while (rt_tick_get() < deadline);

    if (token != TOKEN_DATA) {
        sd_cs_high();
        return SD_ERR_TIMEOUT;
    }

    /* 读 512 + 2 CRC */
    spim_xfer_inner(NULL, g_sd_rx_buf, 514);
    for (uint16_t i = 0; i < 512; i++) {
        buf[i] = g_sd_rx_buf[i];
    }

    sd_cs_high();
    /* 额外 8 个时钟 */
    spi_byte(0xFF);
    return SD_OK;
}

/*
 * 写一个 512-byte 数据块 (CMD24)。
 */
sd_err_t sd_write_block(uint32_t block_addr, const uint8_t *buf)
{
    if (!g_sd_ok) return SD_ERR_INIT;

    uint32_t addr = g_sd_sdhc ? block_addr : (block_addr * 512);

    sd_cs_low();
    uint8_t r1 = sd_cmd(CMD24, addr, NULL, 0);
    if (r1 != 0) {
        sd_cs_high();
        return SD_ERR_CMD;
    }

    /* 发送数据令牌 + 512 bytes + 2 bytes 假 CRC */
    g_sd_tx_buf[0] = TOKEN_DATA;
    for (uint16_t i = 0; i < 512; i++) {
        g_sd_tx_buf[i + 1] = buf[i];
    }
    g_sd_tx_buf[513] = 0xFF; /* CRC 占位 */
    g_sd_tx_buf[514] = 0xFF;

    spim_xfer_inner(g_sd_tx_buf, g_sd_rx_buf, 515);

    /* 检查数据应答 (低 5 位: 010=接受, 101=CRC错, 110=写错) */
    uint8_t resp = g_sd_rx_buf[514] & 0x1F;
    if (resp != 0x05) {
        sd_cs_high();
        return (resp == 0x0B) ? SD_ERR_CRC : SD_ERR_CMD;
    }

    /* 等待写完成 (卡拉低 busy, 轮询直到 0xFF) */
    uint32_t deadline = sd_timeout();
    do {
        resp = spi_byte(0xFF);
        if (resp == 0xFF) break;
    } while (rt_tick_get() < deadline);

    sd_cs_high();
    return (resp == 0xFF) ? SD_OK : SD_ERR_TIMEOUT;
}

/* ---- 卡信息 ----------------------------------------------------------- */

void sd_get_info(void)
{
    if (!g_sd_ok) {
        rt_kprintf("SD: not initialized\n");
        return;
    }

    rt_kprintf("SD: %s capacity=%llu MB\n",
               g_sd_sdhc ? "SDHC/SDXC" : "SDSC",
               (unsigned long long)(g_sd_capacity / 1024 / 1024));

    rt_kprintf("  CID: ");
    for (int i = 0; i < 16; i++) rt_kprintf("%02X ", g_cid[i]);
    rt_kprintf("\n  CSD: ");
    for (int i = 0; i < 16; i++) rt_kprintf("%02X ", g_csd[i]);
    rt_kprintf("\n");
}

uint64_t sd_capacity(void)
{
    return g_sd_capacity;
}

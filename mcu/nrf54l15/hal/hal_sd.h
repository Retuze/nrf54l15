/*
 * hal_sd.h — SD card SPI driver (SPIM22, GPIO2, blocking EasyDMA).
 *
 * 引脚 (XIAO D6-D9):
 *   SCK  = D6 (P2.08)    MOSI = D7 (P2.07)
 *   MISO = D8 (P2.01)    CS   = D9 (P2.04)
 *
 * 用法:
 *   sd_err_t err = sd_init();
 *   if (err == SD_OK) {
 *       uint8_t buf[512];
 *       sd_read_block(0, buf);
 *       sd_get_info();   // 打印 CID/CSD/容量
 *   }
 */
#ifndef HAL_SD_H
#define HAL_SD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SD_OK          = 0,
    SD_ERR_TIMEOUT = 1,
    SD_ERR_CMD     = 2,
    SD_ERR_PARAM   = 3,
    SD_ERR_CRC     = 4,
    SD_ERR_WP      = 5,
    SD_ERR_INIT    = 6,
} sd_err_t;

/* 初始化 SD 卡 (SPI 模式)。返回 SD_OK 表示成功。 */
sd_err_t sd_init(void);

/* 读单个 512-byte 块。block_addr: 字节地址 (SDHC) 或 LBA。 */
sd_err_t sd_read_block(uint32_t block_addr, uint8_t *buf);

/* 写单个 512-byte 块。 */
sd_err_t sd_write_block(uint32_t block_addr, const uint8_t *buf);

/* 打印卡信息 (CID/CSD/容量)。 */
void sd_get_info(void);

/* 获取容量 (单位: 字节), 0 表示未初始化或未知。 */
uint64_t sd_capacity(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SD_H */

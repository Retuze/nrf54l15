/*
 * Minimal GATT/ATT server for the bare-metal BLE peripheral.
 *
 * gatt_handle_att() takes the ATT payload of a received request and builds the
 * ATT payload of the response, returning its length (0 = no response due).
 * MTU and data-length state let responses grow up to the negotiated size.
 */
#ifndef GATT_H
#define GATT_H

#include <stdint.h>

void gatt_init(void);                      /* fill demo data once at boot */
void gatt_on_connect(void);                /* reset MTU / data-length state */
void gatt_set_tx_octets(uint32_t octets);  /* effective data-PDU payload (DLE) */

uint32_t gatt_handle_att(const uint8_t *req, uint32_t req_len, uint8_t *rsp_out);

uint32_t gatt_dbg_mtu(void);
uint32_t gatt_dbg_txoct(void);

/* 0xFFF1 值属性的 ATT 柄（写回调按柄分发；UUID 0xFFF1 只是类型）。 */
#define GATT_FFF1_VAL_HANDLE 0x000Du

/* 0xFFF1 的 CCCD 订阅状态（bit0 = notify）。断链复位（无绑定存储）。 */
uint32_t gatt_notify_enabled(void);

/* ---- 写回调（app→fw 数据通道）----
 * WRITE_CMD（no response）与 WRITE_REQ 到达时调用同一回调（WRITE_REQ
 * 额外回 ATT 响应，见 gatt.c）。值为拷贝语义：回调里即时消费。
 * 不注册回调时 WRITE_REQ 维持 E_WRITE_NOT_PERM、WRITE_CMD 静默丢弃。 */
typedef uint32_t (*gatt_write_cb_t)(uint16_t handle, const uint8_t *val,
                                    uint32_t len, void *arg);
void gatt_set_write_cb(gatt_write_cb_t cb, void *arg);

#endif /* GATT_H */

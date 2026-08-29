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

#endif /* GATT_H */

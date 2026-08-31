/*
 * Clock driver for nRF54L15, bare metal.
 *
 * 目前只有 HFXO 启动（BLE 射频需要 64 MHz 晶振基准）；
 * LFRC/LFCLK 选择由 time 驱动（GRTC）自理，不经过这里。
 */
#ifndef CLOCK_H
#define CLOCK_H

/* Start the HFXO and busy-wait until XOSTARTED（2s 超时）。Call once,
 * before radio。0 = 起振成功；-1 = 超时（晶振虚焊/坏板——可观测挂点）。 */
int clock_hfxo_start(void);

#endif /* CLOCK_H */

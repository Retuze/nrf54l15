---
name: flash
description: 烧录固件到 nRF54L15 开发板
---

# Flash firmware to nRF54L15

使用 OpenOCD 烧录 firmware.hex 到 XIAO nRF54L15 开发板。

## 步骤

1. 检查 `build/debug/firmware.hex` 是否存在（如果不存在，先编译）
2. 检查端口 3333 和 9090 是否被占用，如占用则杀掉对应进程
3. 执行烧录：

```
openocd -f boards/xiao_nrf54l15/support/openocd.cfg -c "init;reset halt;nrf54l-load build/debug/firmware.hex;" -c "shutdown;"
```

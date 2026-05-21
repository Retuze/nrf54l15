---
name: flash
description: 烧录固件到 nRF54L15 开发板
---

# Flash firmware to nRF54L15

使用 pyOCD 烧录 firmware.hex 到 XIAO nRF54L15 开发板。

## 步骤

1. 检查 `build/debug/firmware.hex` 是否存在（如果不存在，先编译）
2. 执行烧录（pyOCD 会自动处理 APPROTECT 解锁和全片擦除）：

```
pyocd flash -t nrf54l build/debug/firmware.hex
```

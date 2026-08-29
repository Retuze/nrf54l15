# boards — 板库

每个**板卡**一个头文件，内容约定（对照 `xiao_nrf54l15.h` 抄改）：

- 只写硬件事实：引脚分配（`BOARD_LED_*` / `BOARD_BUTTON_*` / `BOARD_CONSOLE_*`）、
  板载外设、烧录方式
- 不写行为开关（放实验 `config.h`），不写寄存器（放 `vendor/mdk`）
- 引脚用裸数字宏（PORT + PIN），PORT 对应 nRF54L 的 `NRF_Pn_S` 寄存器组
  （暂无 gpio 驱动，将来有驱动后统一改用驱动侧引脚类型）

换板 = 实验 `config.h` 里改一行 include；新板 = 复制本目录一个文件改宏。

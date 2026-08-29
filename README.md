# nrf54l15

手写 BLE 协议栈（低功耗蓝牙链路层 + ATT/GATT）的学习工程，从零到一：不用 Zephyr / Nordic SDK，
所有代码自己写。目标板：**Seeed XIAO nRF54L15**（应用核 Cortex-M33），后续再实验 FLPR（RISC-V）核。

## 工具链

- 编译器：宿主 **clang**（本机为 clang 22.1.8），按 `--target=arm-none-eabi` 交叉编译
- 链接器：**lld**（随 clang 同版本安装）
- C 库：**picolibc**（自建，随仓库 vendor，clone 即开箱即用；不用 apt/gcc-arm-none-eabi）
- 编译器辅助库：**clang builtins**（自建 compiler-rt，同样随仓库 vendor，替代 libgcc）
- 构建：**CMake + Ninja**
- 烧录：**pyOCD**（板载 SAMD11 CMSIS-DAP，target `nrf54l`，无需 SEGGER J-Link）

工具链库的构建参数与重建方式见 **docs/picolibc.md** 和 `scripts/build_toolchain_libs.sh`。

## 目录结构

```
nrf54l15/
├── cmake/            # 通用构建体系（芯片无关）
│   ├── toolchain.cmake      # clang+lld 交叉工具链模板
│   ├── embedded.cmake       # embedded_app()：链接脚本/libc/hex/bin/size 一条龙
│   └── targets/             # 芯片定义：nrf54l15.cmake（CPU/ABI + MDK 路由宏；
│                            # 无 libc 路线）；换芯片 = 加一个 targets/<名字>.cmake
├── boards/           # 板库：xiao_nrf54l15.h（板级硬件事实；换板 = config.h 换 include）
├── common/           # 平台无关协议库（不 include 任何 54L 寄存器）
│   └── bluetooth/gatt/      # ATT/GATT 服务端（纯协议，host 可单测）
├── drivers/          # nRF54L15 芯片驱动（全部编成一个 libdrivers.a）
│   ├── core/                # syscalls.c：picolibc 系统调用弱桩
│   ├── grtc/                # 52 位全局实时计数器（时间基/延时）
│   └── uart/                # UARTE20 TX 日志 + tiny printf + picolibc stdio 落点
│                            # （posix-console 的 write(1) → 强 _write → uart_write；
│                            #   接线走实验 config.h）
├── project/          # 实验工程目录，每个实验一个子目录（自包含）
│   └── 01_conn/             # 实验 01：BLE 连接 + ATT/GATT（当前）
│       ├── main.c  startup.c  link.ld  config.h  CMakeLists.txt
│       # startup.c: FPU → .data/.tdata 拷贝 → bss 清零 → _set_tls → __libc_init_array
│       # link.ld: RRAM/RAM 布局 + TLS 块（__tls_base 等）+ 堆（__heap_start/__heap_end）
├── docs/             # picolibc.md：自建工具链库的选项与踩坑记录
├── scripts/          # build.sh / check.sh / flash.sh / build_toolchain_libs.sh
│                     # + python 分析工具（serial_log 等）+ picolibc-arm-cross.txt
└── vendor/           # Nordic MDK + CMSIS 头（寄存器视图）+ 自建工具链库
    ├── picolibc/arm-none-eabi/      # libc.a/libm.a + 头（自建，1.8.12）
    └── compiler-rt/arm-none-eabi/   # libclang_rt.builtins-armhf.a（自建）
```

约定：`common` 放与芯片无关的；`drivers` 放芯片驱动（单库）；每个实验在 `project/` 下自包含
（`main.c`, `startup.c`, `link.ld`, `config.h`），复制目录改个名就是新实验
（`cp -r project/01_conn project/02_xxx`）。

## 构建 & 烧录

```sh
./scripts/build.sh 01_conn   # 产物在 build/01_conn/01_conn.elf/.hex/.bin
./scripts/check.sh 01_conn   # 构建 + 回归断言（向量表/符号契约/内存范围），改框架后跑它
./scripts/flash.sh 01_conn   # pyOCD -t nrf54l（板载 CMSIS-DAP）
```

## 板卡事实（XIAO nRF54L15，验证自 Zephyr DTS + Nordic MDK）

| 项目 | 值 |
|---|---|
| 内核 | Cortex-M33 @ up to 128 MHz + FLPR (RV32EMC) |
| 代码存储 | RRAM，1524 KB @ 0x00000000 |
| RAM | 256 KB @ 0x20000000 |
| 用户 LED | P2.00，**低电平点亮** |
| 用户按键 | P0.00，低电平，带内部上拉 |
| 调试串口 | UARTE20 TX = P1.09 → 板载 SAMD11 USB 桥 |
| GPIO2 (P2) secure 基址 | 0x50050400 |
| GPIO 寄存器布局（nRF54L） | OUT 0x00, OUTSET 0x04, OUTCLR 0x08, IN 0x0C, DIR 0x10, DIRSET 0x14, DIRCLR 0x18, PIN_CNF[n] 0x80+4n（与 nRF52 布局不同） |
| pyOCD target | `nrf54l`（builtin） |

## 调试

Terminal 1 — GDB server：

```
pyocd gdbserver -t nrf54l
```

Terminal 2 — attach GDB：

```
arm-none-eabi-gdb build/01_conn/01_conn.elf -ex "target remote :3333" -ex "load" -ex "monitor reset halt"
```

寄存器快查（不挂 GDB）：

```
pyocd cmd -t nrf54l -c "reset" -c "halt" -c "reg pc" -c "read32 0x50050410"
```

BLE 侧日志：`python3 scripts/serial_log.py`（串口日志），`scripts/scan_adv.py` / `dump_rx.py` /
`dump_conn.py` / `connect_try.py` 为 host 侧分析工具。

## 实验路线图

| 实验 | 主题 | 状态 |
|---|---|---|
| 01_conn | BLE 连接（广告 → CONNECT_IND → 数据信道 SN/NESN + DLE + LL 过程）+ ATT/GATT 服务 | 代码就绪 · **待实板验证** |
| （待拆） | 01_blink / 02_adv / 03_rx / 04_conn … 按 nrf52840 系列拆阶段 | 计划 |
| （待做） | tests/：gatt 纯协议 host 单测（对照 nrf52840 的 tests/run.sh） | 计划 |
| （待做） | tools/：SVD → 寄存器头生成（gen_soc.py 迁移）、vendor/ 裁剪到只用到的芯片头 | 计划 |
| （待做） | FLPR（RISC-V）核实验 | 计划 |

链路层当前整体在 `project/01_conn/main.c`（广告/连接/CSA#1/LL 控制过程），后续拆分层：
`common/bluetooth/ll/`（纯协议）+ `drivers/radio/`（RADIO 硬件接入），对照 nrf52840 仓库的
`ll_pdu.c/ll_chan.c/ll_ww.c/ll_conn.c` + `ll_backend.c` 那套分层。

## 启动流程（project/01_conn/startup.c，纯 C）

1. 使能 FPU（CPACR，CP10/CP11 全访问——编译用 `-mfloat-abi=hard`）
2. 拷贝 `.data`（`_sidata → _sdata`）
3. 清零 `.bss`（`_sbss`，长度到 `_ebss`）
4. `main()`；返回后 wfi 死循环

链接脚本 `link.ld` 实验内自包含：`MEMORY`（RRAM 1524K / RAM 256K）+ 分段布局 + 符号契约
（`_estack/_sidata/_sdata/_edata/_sbss/_ebss`）。向量表用 `.isr_vector` 段落在 RRAM 0x0
（standalone 镜像，无 MCUboot）。

HardFault 诊断：`startup.c` 里 `g_fault[]` 记录 magic/CFSR/HFSR/PC/LR（pyOCD 可读），
配合 UART 打印——调试 BLE 时序时非常有用。

## 决策记录

- **picolibc 自建 + 随仓库分发**：不再依赖 apt 的 `picolibc-arm-none-eabi` 和
  `gcc-arm-none-eabi`——libc 用 meson cross 构建（clang+lld，cortex-m33 hard，
  `-Dio-long-long=true` 等选项见 docs/picolibc.md），libgcc 换成自建 compiler-rt
  builtins。产物 commit 进 vendor/，clone 即开箱即用；升级工具链后用
  `scripts/build_toolchain_libs.sh` 重建。
- **printf 统一**：picolibc 的 printf（`-Dposix-console=true`：库自带 fd 0/1/2 的
  带缓冲 FILE，行缓冲换行即 flush，`write(1)` 由 drivers/uart 的强 `write` 落地；
  支持 %llu/浮点）。原 tiny uprintf 已删（main.c 全部换 printf）；HardFault 现场
  打印也用 printf——单线程无锁、重入最坏覆盖半行旧输出，可接受（见 startup.c 注释）。
- **MDK 路由宏**：`vendor/mdk/nrf.h` 靠 `-DNRF54L15_XXAA -DNRF_APPLICATION` 选到
  54L 应用核的头，放在 targets 文件的 `EMBED_CPU_DEFINES`。
- **vendor/mdk 全量保留**：MDK 含全部 nRF 芯片头（~95M），当前只用到 54L；
  裁剪留给 tools/ 任务。

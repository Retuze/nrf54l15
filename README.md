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
├── boards/           # 板库：xiao_nrf54l15.h（板级硬件事实；换板 = main.c 换 include）
├── common/           # 平台无关协议库（不 include 任何 54L 寄存器，底层能力走注入）
│   ├── bluetooth/           # gatt/：ATT/GATT 服务端 + 主动 MTU 交换（纯协议，host 可单测）
│   │                       # ll/：BLE 链路层（广播/CONNECT_IND/CSA#1/LL control/
│   │                       #      连接状态机,同步+异步双引擎共享协议核;sched 调度器
│   │                       #      雏形;scan 被动扫描——全经 ll_ops_t 注入,host 可单测）
│   ├── ring/                # SPSC 字节环形缓冲（ISR put / 主循环 get，无锁）
│   ├── log/                 # 实时日志：格式化 + log_sink 注入（目标机=uart 异步
│   │                        #   TX，宿主测试=fake；不改 printf 阻塞路径）
│   ├── cobs/                # COBS 帧定界（UART 传输层，无长度上限，增量解码）
│   └── proto/               # 固件/app 应用协议：帧/TLV/分片/CRC16 + 两级 REPORT
│                            #   送达 + SET 幂等重传（规格见 docs/proto.md）
├── drivers/          # nRF54L15 芯片驱动（全部编成一个 libdrivers.a）
│   ├── core/                # 共用 startup.c（向量表 16+270 项 + 启动序列）与
│   │                        # nrf54l15.ld（RRAM/RAM 布局 + TLS 块 + 堆符号）；
│   │                        # syscalls.c：POSIX 系统调用弱桩（强 write 在
│   │                        #   drivers/console，应用可强定义覆盖）
│   ├── gpio/                # Arduino 风格：GPIO_PIN(port,pin) 线性编号 + mode/write/read
│   ├── time/                # BLE 专用时基（GRTC 实现）：now_us/delay_us + CC 闹钟（IRQ）
│   ├── timer/               # 普通定时器（TIMER00 多实例）：周期/单次回调，1MHz 节拍
│   ├── uart/                # UARTE 实例式驱动（uart_init(port,cfg) 不透明句柄；
│   │                        #   uart_tx 入队即返 + uart_tx_wait 主动排空——阻塞是
│   │                        #   console 层的组合；RX DMA 按满/空闲成块投递）
│   └── console/             # stdio 落点 + 默认 log sink（printf 仅 HardFault 用）
│   ├── radio/               # RADIO 寄存器驱动 + 硬件 T_IFS（DPPI+TIMER10）+ 异步 IRQ API
│   └── clock/               # HFXO 起振 + XOTUNE 调谐/周期重调谐
├── project/          # 实验工程目录，每个实验一个子目录
│   └── 01_conn/             # 实验 01：BLE 连接 + ATT/GATT（当前）
│       ├── main.c  CMakeLists.txt     # main 只做驱动 init + ll_ops 接线 + 打印
│       # startup.c/link.ld 默认共用 drivers/core/ 的共享版；实验要自定义
│       # 就把同名文件放进本目录，embedded_app 自动优先用工程自己的版本
├── tests/            # 宿主侧单测（host 编译，不进交叉工具链）：自研 tf.h + 注入测试
│                     # gatt_test.c（字节级）+ ll_test.c（fake time/radio 剧本）
├── docs/             # picolibc.md：自建工具链库的选项与踩坑记录
├── scripts/          # build.sh / check.sh / flash.sh / run_tests.sh / build_toolchain_libs.sh
│                     # + python 分析工具（serial_log 等）+ picolibc-arm-cross.txt
└── vendor/           # Nordic MDK + CMSIS 头（寄存器视图）+ 自建工具链库
    ├── picolibc/arm-none-eabi/      # libc.a/libm.a + 头（自建，1.8.12）
    └── compiler-rt/arm-none-eabi/   # libclang_rt.builtins-armhf.a（自建）
```

约定：`common` 放与芯片无关的纯协议（底层能力经 ops 结构体注入，宿主可单测）；
`drivers` 放芯片驱动（单库，startup/链接脚本也在这共用）；
每个实验在 `project/` 下只有 `main.c` + `CMakeLists.txt`（板头直接 include），
复制目录改个名就是新实验（`cp -r project/01_conn project/02_xxx`）。
startup.c / link.ld 默认共用 `drivers/core/`；实验要自定义（比如改栈大小、改内存布局），
把同名文件放进实验目录即可，`embedded_app()` 自动优先用工程自己的版本。

## 构建 & 烧录

```sh
./scripts/build.sh 01_conn   # 产物在 build/01_conn/01_conn.elf/.hex/.bin
./scripts/run_tests.sh       # 宿主侧单测（common 的 gatt/ll/ring/log 注入测试）
./scripts/coverage.sh        # common 行覆盖率报告（默认门槛 80%，build/coverage/）
./scripts/check.sh           # 全量回归：全部实验 + 宿主单测 + 覆盖率门槛
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
`dump_conn.py` / `connect_try.py` 为 host 侧分析工具；`proto_client.py` 为 05_proto 的
全协议实板验证客户端（bleak：GET/REPORT/ACK + SET{TIME} 幂等重发 + 时间同步校验）。

坑：Windows 异常断链后系统会自动回连占住板子（不广播、串口安静），
`pyocd cmd -t nrf54l -c reset` 解开；同一地址换 GATT 表后 bleak 可能因系统
GATT 缓存报 services=0 或找不到特征，重试/重启蓝牙即可。

**RF 三大必修课**（2026-08-31 全天实板追出，缺一个安卓就收不到）：
1. **FICR 出厂修调必须应用**（startup.c `apply_ficr_trims`）：TRIMCNF[64]
   含 RADIO 模拟前端等 trim，SystemInit 的职责，裸机 startup 漏掉的代价是
   载波质量边缘化（宽容接收机能收但 RSSI 低，安卓拒收）。终止符是
   ADDR==0xFFFFFFFF **或 0**——按 0xFFFFFFFF 跳过会把 0 写进地址 0 锁死。
2. **HFXO 要 XOTUNE 且周期重调谐**（clock_hfxo_start/retune）：XOSTARTED
   只是起振，XOTUNED 才是调谐完成（Zephyr 原话"HFCLK is stable after
   XOTUNED"）；调谐是一次性的，芯片温漂后载波偏移——症状是**长 PDU 先
   失联**（相位随包长累积，短 ACK 能收、长 REPORT 收不到）。
3. **T_IFS 用硬件定时**（radio.c：RADIO.PHYEND →DPPI→ TIMER10.CLEAR，
   TIMER10.COMPARE →DPPI→ RADIO.TXEN，TIMER10 @32MHz）：触发点 =
   150 − RX chain delay(9.4us，PHYEND 比空口末位晚这么多) − TXEN ramp
   (40.9us) ≈ **100us**（常量取自 Zephyr radio_nrf54lx.h；按键扫掠实测
   98 不可见/101 可见/104 不可见，窗口中心吻合）。安卓接收窗口仅 ±2us，
   软件定时的 3..10us 抖动恰好骑窗=全天"时好时坏"的总根源。构建超时的
   回复宁可放弃也不晚发：响应在 LL pend 队列，对端重传时下事件秒回。

对端宽容度谱系：PC(Realtek) 早/晚几十 us 都收 ≫ iOS ≫ 安卓（±2us 窗 +
SCAN_RSP 收不到就不上报设备，表现为"扫不到"）。**验证 RF 改动永远拿
安卓当裁判，PC 能收证明不了任何事**。

**iOS 系统级自动回连占线坑**：iOS 对连过的外设会后台无限回连（连上后零
GATT 活动的纯空闲连接，断开秒回连,换广播地址都甩不掉）；占线期间不广播
=谁都扫不到。排查"扫不到"先看串口是否 `[conn] connected`。解法：关 iOS
蓝牙或让它忘记设备。

**E-SafeNet 加密（本机）**：源文件透明加密，白名单外的工具（sed/head/
awk 等）读写皆密文——**sed -i 会毁文件**。只能用 Claude 的 Read/Edit/Write
或 git checkout 恢复；llvm-cov gcov 也因此读密文，coverage.sh 已改用
source-based coverage（报告阶段不读源文件）。

## 实验路线图

| 实验 | 主题 | 状态 |
|---|---|---|
| 01_conn | BLE 连接（广告 → CONNECT_IND → 数据信道 SN/NESN + DLE + LL 过程）+ ATT/GATT 服务 | **实板验证通过**（2026-08-31） |
| 02_fault | HardFault 现场打印验证：故意触发总线错误 → g_fault[] + console_tx_abort() 归零 TX → printf 现场行（预期 CFSR=0x00008200、HFSR=0x40000000） | **实板验证通过**（2026-08-31） |
| 03_conn_log | 连接态实时日志（方案 A）：common/log + uart_tx + ll on_conn_event 钩子，逐事件 "evt=N ok/miss ch=X"（全工程 printf 仅 HardFault） | **实板验证通过**（2026-08-31） |
| 04_async_ll | 事件化 LL（方案 B）：GRTC 闹钟定锚/窗口超时 + RADIO IRQ 收发，连接态全在 IRQ，主循环解放（连接期间心跳照走，spins≈396k/s）。协议逻辑与同步引擎共享（conn_begin/conn_event_setup/conn_reply），ll_async_start 即开即返。已知项：IRQ 路径构建预算更紧,tx_timeouts 略高于同步版（重传兜底,零丢包）,根治待 DPPI 硬件定时 TXEN | **实板验证通过**（2026-08-31，PC 零丢失 152/152） |
| 07_adv_scan | LE 双角色第一步：ll_sched 时间片调度器雏形（(t_start,prio) 选择 + 窗口重叠低优让步计数）+ ll_scan 被动扫描（AdvA 去重设备表 + AD 名字提取）。两种调度参数：谐波打包（周期整数倍+相位错开=构造性零碰撞,默认）与碰撞观测（DUO_COLLIDE_DEMO,~49% 让步率验证机制）。CONNECT_IND 切 04 异步引擎,断链自动恢复双角色 | **实板验证通过**（2026-08-31,扫到 16 环境设备含名字;谐波配置 scan 让步 0） |
| 08_central_conn | central 侧连接（计划）：扫描 → 收 ADV → 发 CONNECT_IND → 主机 anchor 时序 + WinOffset 相位避碰 | 计划 |
| 09_multi_role | 多连接/多角色完整调度（计划）：参数避碰 + 优先级让步 + 多 conn 实例 | 计划 |
| 05_proto | 应用协议固件接线：gatt 0xFFF1 写回调+通知队列 + proto 挂载，GET→全量 REPORT(ACK_REQ 超时重发)/SET{TIME}/ACK（fw 侧完整语义） | **实板验证通过**（2026-08-31） |
| 06_timer | 普通定时器（TIMER00 多实例）：1s 周期回调闪灯 + 3s 单次回调，主循环自由——GRTC 是 BLE 专用时基，应用定时不占它 | **实板验证通过**（2026-08-31） |
| （待做） | tools/：SVD → 寄存器头生成（gen_soc.py 迁移）、vendor/ 裁剪到只用到的芯片头 | 计划 |
| （待做） | FLPR（RISC-V）核实验 | 计划 |

分层现状（2026-08-30 重构后）：链路层在 `common/bluetooth/ll/`（纯协议，全部底层能力经
`ll_ops_t` 注入，`tests/ll_test.c` 用 fake time/radio 剧本做宿主单测，144 项断言全绿）；
RADIO 寄存器 + T_IFS 软件时序在 `drivers/radio/`；`project/01_conn/main.c` 只剩驱动
初始化、`ll_ops` 接线和日志打印（~150 行）。对照 nrf52840 仓库的 ll_pdu/ll_chan/ll_ww/
ll_conn 拆分（ll.c 内按函数分区，将来按需拆文件）。

## 启动流程（drivers/core/startup.c，纯 C，各实验共用）

1. 使能 FPU（CPACR，CP10/CP11 全访问——编译用 `-mfloat-abi=hard`）
2. 拷贝 `.data`（`_sidata → _sdata`）
3. 清零 `.bss`（`_sbss`，长度到 `_ebss`）
4. `main()`；返回后 wfi 死循环

链接脚本 `drivers/core/nrf54l15.ld`（共用版）自包含：`MEMORY`（RRAM 1524K / RAM 256K）+
分段布局 + 符号契约（`_estack/_sidata/_sdata/_edata/_sbss/_ebss` + TLS/堆符号）。
向量表用 `.isr_vector` 段落在 RRAM 0x0（standalone 镜像，无 MCUboot）。

HardFault 诊断：`startup.c` 里 `g_fault[]` 记录 magic/CFSR/HFSR/PC/LR（pyOCD 可读），
配合 UART 打印——调试 BLE 时序时非常有用。

## 决策记录

- **picolibc 自建 + 随仓库分发**：不再依赖 apt 的 `picolibc-arm-none-eabi` 和
  `gcc-arm-none-eabi`——libc 用 meson cross 构建（clang+lld，cortex-m33 hard，
  `-Dio-long-long=true` 等选项见 docs/picolibc.md），libgcc 换成自建 compiler-rt
  builtins。产物 commit 进 vendor/，clone 即开箱即用；升级工具链后用
  `scripts/build_toolchain_libs.sh` 重建。
- **打印策略（2026-08-30 起）**：printf 只归 HardFault 现场打印（startup.c
  hardfault_c，阻塞路径保证现场行确定性落地）；日常日志统一走 common/log
  （格式化 + log_sink 注入，目标机 sink = uart_tx 入队即返、驱动内 512B ring
  + TX END 中断后台排空，微秒级返回不阻塞连接时序；满则丢弃、log_dropped
  可观测）。装配：`console_log_init()`（console_init 之后调一次）。printf 的
  stdio 链路（posix-console：库内弱 stdout 512B 行缓冲 → write(1) → console.c
  强 write = uart_tx + uart_tx_wait）保留，专供 HardFault 路径。
- **MDK 路由宏**：`vendor/mdk/nrf.h` 靠 `-DNRF54L15_XXAA -DNRF_APPLICATION` 选到
  54L 应用核的头，放在 targets 文件的 `EMBED_CPU_DEFINES`。
- **vendor/mdk 全量保留**：MDK 含全部 nRF 芯片头（~95M），当前只用到 54L；
  裁剪留给 tools/ 任务。

# Opus 移植到 ARM Cortex-M33 (RT-Thread)

## 概述

从 [xiph/opus](https://github.com/xiph/opus) 上游源码裁剪移植，目标平台为 ARM Cortex-M33 单片机，运行 RT-Thread RTOS，场景为 16kHz 宽带语音编解码。

**上游源码未做任何修改** — 所有移植通过编译配置层实现，可随时升级 Opus 版本。

## 硬件要求

| 项目 | 要求 |
|------|------|
| CPU | ARM Cortex-M33 (ARMv8-M) |
| FPU | FPv5-SP (单精度硬件浮点) |
| Flash | ≥ 200 KB |
| RAM | ≥ 70 KB (含 codec 状态 + 线程栈) |

## 源文件清单

共 132 个 .c 文件，从上游三个子目录裁剪而来：

- **CELT** (18 文件): 核心 MDCT/FFT/量化/熵编码，移除所有 ARM NEON / x86 SSE / MIPS 优化
- **SILK** (77 文件): 语音编码器通用部分 + `silk/float/` 浮点变体，移除 `silk/fixed/` 定点路径
- **Opus 顶层** (8 文件): 核心编解码 + 分析 + repacketizer，移除 multistream / projection

排除的模块: `dnn/` (深度神经网络), `silk/arm/`, `silk/fixed/`, `silk/mips/`, `silk/x86/`, `celt/arm/`, `celt/x86/`, `src/opus_multistream*`, `src/opus_projection*`

## 内存用量

在 16kHz / mono / 浮点路径下实测：

| 对象 | 大小 |
|------|------|
| Encoder 状态 | 30 KB |
| Decoder 状态 | 18 KB |
| 编解码器同时存在 | 48 KB |
| RT-Thread 线程栈 (建议) | ≥ 16 KB |
| **RAM 总计** | **~65 KB** |

Stereo 模式：Encoder 47 KB，Decoder 26 KB，总计 ~73 KB + 栈。

## 构建

### CMake (推荐)

```cmake
# 在你的工程 CMakeLists.txt 中:
add_subdirectory(port)
target_link_libraries(your_app opus_cm33)
```

交叉编译 (ARM GCC):

```bash
cmake -S port -B build_m33 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=your_arm_gcc_toolchain.cmake \
  -DOPUS_CROSS_COMPILE=ON
```

PC 端验证 (不交叉编译):

```bash
cmake -S port -B build_pc -G Ninja -DOPUS_CROSS_COMPILE=OFF
cmake --build build_pc
```

### SCons (RT-Thread 原生)

将 `SConscript` 放入 RT-Thread BSP 的包目录，在 `rt_config.h` 中开启 `PKG_USING_OPUS`。

## 集成方式

### 方式一：静态缓冲区 (推荐嵌入式使用)

```c
#include <rtthread.h>
#include "opus.h"

// 预分配状态缓冲区
static uint8_t __attribute__((aligned(8))) enc_buf[31668];  // mono encoder
static uint8_t __attribute__((aligned(8))) dec_buf[18468];  // mono decoder

void opus_setup(void)
{
    OpusEncoder *enc = (OpusEncoder*)enc_buf;
    int err = opus_encoder_init(enc, 16000, 1, OPUS_APPLICATION_VOIP);
    if (err != OPUS_OK) { /* handle error */ }

    OpusDecoder *dec = (OpusDecoder*)dec_buf;
    err = opus_decoder_init(dec, 16000, 1);
    if (err != OPUS_OK) { /* handle error */ }
}
```

### 方式二：RT-Thread 动态堆 (需 CMake -D 重载)

CMakeLists.txt 中已配置 `OVERRIDE_OPUS_ALLOC=rt_malloc` 等宏，可直接使用 Create 接口：

```c
int err;
OpusEncoder *enc = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &err);
OpusDecoder *dec = opus_decoder_create(16000, 1, &err);
// ...
opus_encoder_destroy(enc);
opus_decoder_destroy(dec);
```

**注意**: `<rtthread.h>` 必须在 `<opus.h>` 之前 include，否则 `rt_malloc` 未声明。

### 关键 API

```c
// 编码: float PCM → Opus packet
opus_int32 len = opus_encode_float(enc, pcm, frame_size, packet, max_packet);

// 解码: Opus packet → float PCM
int samples = opus_decode_float(dec, packet, len, pcm, frame_size, 0);

// 参数控制
opus_encoder_ctl(enc, OPUS_SET_BITRATE(24000));
opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(5));
opus_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
```

帧大小: 16kHz 下 20ms = 320 samples，最大压缩包 ≤ 1275 bytes。

## 比特一致性

精简库与上游完整 Opus 库输出完全一致（已验证）：

```
Full library:     checksum=361958  energy=1978.328343
Trimmed library:  checksum=361958  energy=1978.328343
```

## 文件说明

| 文件 | 用途 |
|------|------|
| `config.h` | M33 配置宏 (浮点路径 / VLAs / 裁剪开关) |
| `CMakeLists.txt` | CMake 构建，含完整源文件列表和 M33 编译选项 |
| `SConscript` | RT-Thread SCons 构建 |
| `opus_thread.c` | RT-Thread 集成示例 (编码/解码线程) |
| `measure_sizes.c` | PC 端测量状态大小的工具 |
| `verify_port.c` | 比特一致性验证工具 |

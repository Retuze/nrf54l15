# cmake/targets — 芯片定义

每个**芯片目标**一个文件，内容两块（对照 nrf54l15.cmake 抄改）：

1. `EMBED_CPU_FLAGS`：CPU/ABI 编译参数
   - 例：`-mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb`
   - 需要 `-march` 时可写全
2. `EMBED_C_COMPILER_TARGET` / `EMBED_SYSTEM_PROCESSOR`：clang 的 `--target` 三元组与系统名

有 libc 的目标再补第 3 块（nRF54L15 暂无 libc，这两项留空不定义）：

3. `EMBED_PICOLIBC_BASE` / `EMBED_LIBGCC_DIR`：libc 与编译器辅助库位置
   （`embedded.cmake` 检测到才衔接 c/m/gcc 与 `-L` 搜索路径）

## 用法（project/<name>/CMakeLists.txt）

```cmake
# 直接 include 目标（不用 set/target 变量间接选择）
include(${PROJ_ROOT}/cmake/targets/nrf54l15.cmake)
set(CMAKE_TOOLCHAIN_FILE ${PROJ_ROOT}/cmake/toolchain.cmake CACHE STRING "" FORCE)
project(test C)
```

新芯片 = 复制本目录一个文件、改上面参数，工程里换 include 路径。

> 改动 toolchain.cmake / targets/ 里的变量定义后，记得 `rm -rf build/<工程名>`
> 重新生成——CMake 的 cache 会保留旧值并遮蔽新的普通 set（这是 CMake 语义，
> 不是 bug；`scripts/build.sh` 自带 `--fresh`，直接重跑即可）。

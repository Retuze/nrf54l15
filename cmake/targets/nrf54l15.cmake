# 目标定义：nRF54L15 应用核（Cortex-M33，单精度 FPU）
# 由工程 CMakeLists 在 toolchain 之前 include（提供 EMBED_* 参数）。
#
# 注意：全部用普通变量（CMake cache 变量会遮蔽普通 set）。
# 改动本文件后，重新运行 scripts/build.sh 即自动重配（build.sh 用 --fresh）。
set(EMBED_CPU_FLAGS
    "-mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb")

set(EMBED_C_COMPILER_TARGET  "arm-none-eabi")
set(EMBED_SYSTEM_PROCESSOR   "arm")

# vendor/mdk 的 nrf.h 靠这两个宏路由到 nRF54L15 应用核的头
# （NRF54L15_XXAA = 芯片型号；NRF_APPLICATION = 应用核而非 FLPR）
set(EMBED_CPU_DEFINES "-DNRF54L15_XXAA -DNRF_APPLICATION")

# --- C 运行库：暂无（-nostdlib + drivers/uart 自带 tiny printf）---
# 本芯片走无 libc 路线；以后要 malloc/printf 全家桶时再引入 picolibc，
# 照 nrf52840 的 targets 文件补这两行（embedded.cmake 会自动衔接）：
#   set(EMBED_PICOLIBC_BASE "/usr/lib/picolibc/arm-none-eabi")
#   set(EMBED_LIBGCC_DIR   "/usr/lib/gcc/arm-none-eabi/<版本>/")

# 目标定义：nRF54L15 应用核（Cortex-M33，单精度 FPU）
# 由工程 CMakeLists 在 toolchain 之前 include（提供 EMBED_* 参数）。
#
# 注意：全部用普通变量（CMake cache 变量会遮蔽普通 set）。
# 改动本文件后，重新运行 scripts/build.sh 即自动重配（build.sh 用 --fresh）。
#
# -ftls-model=local-exec：picolibc 的 TLS（errno/reent）用本地执行模型，
# 静态 TLS 块由 link.ld 的 __tls_base 提供、startup.c 里 _set_tls 安装。
set(EMBED_CPU_FLAGS
    "-mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -ftls-model=local-exec")

set(EMBED_C_COMPILER_TARGET  "arm-none-eabi")
set(EMBED_SYSTEM_PROCESSOR   "arm")

# vendor/mdk 的 nrf.h 靠这两个宏路由到 nRF54L15 应用核的头
# （NRF54L15_XXAA = 芯片型号；NRF_APPLICATION = 应用核而非 FLPR）
set(EMBED_CPU_DEFINES "-DNRF54L15_XXAA -DNRF_APPLICATION")

# --- C 运行库：picolibc（自建，随仓库 vendor，开箱即用）---
# 构建方式见 scripts/picolibc-arm-cross.txt（meson cross file）与
# scripts/build_toolchain_libs.sh：clang --target=arm-none-eabi +
# cortex-m33 hard-float；关键选项 -Dmultilib=false -Dpicocrt=false
# -Dthread-local-storage=picolibc（默认）-Dio-long-long=true。
set(EMBED_PICOLIBC_BASE "${PROJ_ROOT}/vendor/picolibc/arm-none-eabi")

# --- 编译器辅助库：clang builtins（自建 compiler-rt，随仓库 vendor）---
# 除法/64 位运算/软浮点等 __aeabi_* 辅助函数；
# 不再依赖 gcc-arm-none-eabi 的 libgcc，本机只要有 clang 就能开箱即用。
set(EMBED_COMPILER_RT_LIB
    "${PROJ_ROOT}/vendor/compiler-rt/arm-none-eabi/lib/libclang_rt.builtins-armhf.a")

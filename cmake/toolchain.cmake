# 通用交叉编译工具链（clang + lld），芯片无关。
#
# 用法（project/<name>/CMakeLists.txt，在 project() 之前）：
#   include(${PROJ_ROOT}/cmake/targets/<芯片>.cmake)     # 先提供 EMBED_* 参数
#   set(CMAKE_TOOLCHAIN_FILE .../cmake/toolchain.cmake)
#   project(<name> ...)
#
# 本文件只做工具链的"共性"，芯片/板卡参数由 targets/<name>.cmake 提供：
#   EMBED_CPU_FLAGS / EMBED_C_COMPILER_TARGET / EMBED_SYSTEM_PROCESSOR
#   EMBED_PICOLIBC_BASE / EMBED_LIBGCC_DIR（有 libc 目标才定义）
# 缺了任何参数会直接体现在 CMake 报错上。

# EMBED_* 参数一律来自包含的 targets/<name>.cmake，此处不做任何默认/兜底：
# 漏 include 会以 CMake 报错或链接失败直接暴露，不在工具链里藏状态。

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR ${EMBED_SYSTEM_PROCESSOR})

set(CMAKE_C_COMPILER        clang)
set(CMAKE_ASM_COMPILER      clang)
set(CMAKE_C_COMPILER_TARGET ${EMBED_C_COMPILER_TARGET})

# 交叉编译目标没法试运行，跳过 ABI 探测的链接阶段
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 所有"目标级"参数（含 libc 依赖的 -ftls-model 等）都在 targets/<name>.cmake，
# 这里只保留工具链本身需要的通用项
set(CMAKE_C_FLAGS_INIT
    "${EMBED_CPU_FLAGS} ${EMBED_CPU_DEFINES} -ffreestanding -fno-common -ffunction-sections -fdata-sections -g -Wall -Wextra")
set(CMAKE_ASM_FLAGS_INIT "${EMBED_CPU_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")

# 后处理工具（随 clang 一起装）
set(CMAKE_OBJCOPY llvm-objcopy)
set(CMAKE_SIZE    llvm-size)

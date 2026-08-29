# LLVM/Clang bare-metal toolchain for nRF54L15 (Cortex-M33).
# Usage: cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-clang.cmake
# Requires clang / llvm tools on PATH.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   clang)
set(CMAKE_ASM_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
find_program(CMAKE_OBJCOPY NAMES llvm-objcopy REQUIRED)
find_program(CMAKE_OBJDUMP NAMES llvm-objdump)
find_program(CMAKE_SIZE    NAMES llvm-size    REQUIRED)

# We are cross-compiling for bare metal: don't try to link a full test exe.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Cortex-M33 with single-precision FPU (fpv5-sp-d16), hard float ABI.
set(CPU_FLAGS "--target=arm-none-eabi -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16")

set(CMAKE_C_FLAGS_INIT   "${CPU_FLAGS} -ffreestanding -fno-common -ffunction-sections -fdata-sections -Wall -Wextra")
set(CMAKE_ASM_FLAGS_INIT "${CPU_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${CPU_FLAGS} -ffreestanding -fno-common -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti")

# lld linker, no host libc/CRT, garbage-collect unused sections.
set(CMAKE_EXE_LINKER_FLAGS_INIT "${CPU_FLAGS} -fuse-ld=lld -nostdlib -Wl,--gc-sections")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

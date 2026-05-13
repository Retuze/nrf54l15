# toolchain-nrf54l15.cmake — clang cross-compilation for nRF54L15 (Cortex-M33).
#
# Usage:
#   cmake -B build -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=../../cmake/toolchain-nrf54l15.cmake \
#         -DSDK_ROOT=E:/lib-build/out/arm-none-eabi/cortex-m33-hard-fpv5-sp-d16

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Host clang + LLVM binutils
set(CMAKE_C_COMPILER   clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER clang)
set(CMAKE_AR           llvm-ar)
set(CMAKE_RANLIB       llvm-ranlib)
set(CMAKE_LINKER       ld.lld)

# Architecture flags
set(_arch "--target=arm-none-eabi;-mcpu=cortex-m33;-mfloat-abi=hard;-mfpu=fpv5-sp-d16")
foreach(f IN LISTS _arch)
    string(APPEND CMAKE_C_FLAGS_INIT   " ${f}")
    string(APPEND CMAKE_CXX_FLAGS_INIT " ${f}")
    string(APPEND CMAKE_ASM_FLAGS_INIT " ${f}")
endforeach()

# SDK root — override with -DSDK_ROOT=...
if(NOT DEFINED SDK_ROOT)
    set(SDK_ROOT "E:/lib-build/out/arm-none-eabi/cortex-m33-hard-fpv5-sp-d16"
        CACHE PATH "lib-build install root")
endif()

# Sysroot = picolibc (C library + crt0 + linker scripts)
set(CMAKE_SYSROOT "${SDK_ROOT}/picolibc")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

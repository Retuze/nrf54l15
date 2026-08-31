cmake_minimum_required(VERSION 3.20)

# 1) 芯片定义：直接 include 目标文件（提供 EMBED_* 参数）
set(PROJ_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
include(${PROJ_ROOT}/cmake/targets/nrf54l15.cmake)

# 2) 通用交叉工具链（clang + lld）
set(CMAKE_TOOLCHAIN_FILE ${PROJ_ROOT}/cmake/toolchain.cmake
    CACHE STRING "" FORCE)
project(03_conn_log C)

# 3) 通用构建辅助
include(${PROJ_ROOT}/cmake/embedded.cmake)

# 4) 依赖（芯片驱动单库 + 平台无关协议库）
add_subdirectory(${PROJ_ROOT}/drivers drivers)
add_subdirectory(${PROJ_ROOT}/common  common)

# 5) 应用本体（startup.c/link.ld 默认共用 drivers/core/ 的共享版）
add_executable(03_conn_log main.c)
target_include_directories(03_conn_log PRIVATE ${PROJ_ROOT}/boards)
embedded_app(03_conn_log)
target_link_libraries(03_conn_log PRIVATE drivers gatt ll ring log)

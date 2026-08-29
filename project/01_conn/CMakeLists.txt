cmake_minimum_required(VERSION 3.20)

# 1) 芯片定义：直接 include 目标文件（提供 EMBED_* 参数）
set(PROJ_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
include(${PROJ_ROOT}/cmake/targets/nrf54l15.cmake)

# 2) 通用交叉工具链（clang + lld）
set(CMAKE_TOOLCHAIN_FILE ${PROJ_ROOT}/cmake/toolchain.cmake
    CACHE STRING "" FORCE)
project(01_conn C)

# 3) 通用构建辅助
include(${PROJ_ROOT}/cmake/embedded.cmake)

# 4) 依赖（芯片驱动单库 + 平台无关协议库）
#    console 的接线配置在本实验 config.h；其引用的板头在 boards/——
#    两处 include 路径都注入 drivers 的编译（见 drivers/CMakeLists）
set(NRF54_CONSOLE_CFG_DIR     ${CMAKE_CURRENT_SOURCE_DIR})
set(NRF54_CONSOLE_BOARDS_DIR  ${PROJ_ROOT}/boards)
add_subdirectory(${PROJ_ROOT}/drivers drivers)
add_subdirectory(${PROJ_ROOT}/common  common)

# 5) 应用本体
add_executable(01_conn main.c startup.c)
# 板库 include（boards/，可整体替换为外部目录；config.h 在实验目录内自包含）
target_include_directories(01_conn PRIVATE ${PROJ_ROOT}/boards)
embedded_app(01_conn ${CMAKE_CURRENT_SOURCE_DIR}/link.ld)
target_link_libraries(01_conn PRIVATE drivers gatt)

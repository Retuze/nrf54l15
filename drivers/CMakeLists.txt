# —— nRF54L15 芯片驱动（单一库 libdrivers.a）——
# 实验工程只需 target_link_libraries(<app> PRIVATE drivers <common 库名>)

add_library(drivers STATIC
  grtc/grtc.c
  uart/uart.c)
# 驱动头 + vendor（nrf.h 等 Nordic MDK/CMSIS 头）经 PUBLIC 传给应用
target_include_directories(drivers PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/grtc
  ${CMAKE_CURRENT_SOURCE_DIR}/uart
  ${PROJ_ROOT}/vendor/mdk
  ${PROJ_ROOT}/vendor/cmsis)
# uart.c 的"控制台接线"：由实验声明配置链（目录 + 板库）——config.h 与
# 它 include 的板头在 uart.c 编译时可见（含 CONFIG_CONSOLE_TX_* 宏）
if(DEFINED NRF54_CONSOLE_CFG_DIR)
  target_include_directories(drivers SYSTEM PRIVATE ${NRF54_CONSOLE_CFG_DIR})
endif()
if(DEFINED NRF54_CONSOLE_BOARDS_DIR)
  target_include_directories(drivers SYSTEM PRIVATE ${NRF54_CONSOLE_BOARDS_DIR})
endif()

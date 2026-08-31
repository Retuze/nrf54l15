# —— nRF54L15 芯片驱动（单一库 libdrivers.a）——
# 实验工程只需 target_link_libraries(<app> PRIVATE drivers <common 库名>)

add_library(drivers STATIC
  core/syscalls.c
  clock/clock.c
  console/console.c
  gpio/gpio.c
  radio/radio.c
  time/time.c
  timer/timer.c
  uart/uart.c)
# 驱动头 + vendor（nrf.h 等 Nordic MDK/CMSIS 头）经 PUBLIC 传给应用
target_include_directories(drivers PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/console
  ${CMAKE_CURRENT_SOURCE_DIR}/core
  ${CMAKE_CURRENT_SOURCE_DIR}/clock
  ${CMAKE_CURRENT_SOURCE_DIR}/gpio
  ${CMAKE_CURRENT_SOURCE_DIR}/radio
  ${CMAKE_CURRENT_SOURCE_DIR}/time
  ${CMAKE_CURRENT_SOURCE_DIR}/timer
  ${CMAKE_CURRENT_SOURCE_DIR}/uart
  ${PROJ_ROOT}/common/ring
  ${PROJ_ROOT}/vendor/mdk
  ${PROJ_ROOT}/vendor/cmsis)
# uart 的 TX ring 用 common/ring；console 的默认 log sink 用 common/log
# （纯软件，无反向依赖）；目标在 common/ 定义，CMake 生成期解析名字即可
target_link_libraries(drivers PUBLIC ring log)
# startup.c 用了 picolibc 头（stdio.h 的 printf）；无 libc 目标时跳过
if(DEFINED EMBED_PICOLIBC_BASE AND NOT EMBED_PICOLIBC_BASE STREQUAL "")
  target_include_directories(drivers SYSTEM PRIVATE ${EMBED_PICOLIBC_BASE}/include)
endif()

# CMake build for XIAO nRF54L15 firmware.
#   Phase 1: nrfx + RT-Thread   (no BLE)
#   Phase 2: + libmpsl.a + libsoftdevice_controller_multirole.a + NimBLE
#
# libmpsl.a and libsoftdevice_controller_multirole.a are static libraries
# linked directly into the firmware ELF — no separate SoftDevice hex.
cmake_minimum_required(VERSION 3.21)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- SDK & MCU roots ---------------------------------------------------
set(SDK_ROOT  "${CMAKE_CURRENT_SOURCE_DIR}/../../common/cortex-m33-hard-fpv5-sp-d16"
    CACHE PATH "picolibc + libc++ SDK")
set(NRFX_LOCAL "${CMAKE_CURRENT_SOURCE_DIR}/../../mcu/nrf54l15")

include(../../cmake/common.cmake)

project(firmware LANGUAGES C CXX ASM)

# ---- Sources -----------------------------------------------------------
set(SOURCES
    # RT-Thread kernel
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/rt-thread/src/clock.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/rt-thread/src/components.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/rt-thread/src/device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/rt-thread/src/idle.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/rt-thread/src/ipc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/rt-thread/src/irq.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/rt-thread/src/kservice.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/rt-thread/src/mem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/rt-thread/src/object.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/rt-thread/src/scheduler.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/rt-thread/src/thread.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/rt-thread/src/timer.c
    # RT-Thread Cortex-M33 port
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/rt-thread/libcpu/arm/cortex-m33/cpuport.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/rt-thread/libcpu/arm/cortex-m33/trustzone.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/rt-thread/libcpu/arm/cortex-m33/context_gcc.S

    # Common infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/segger-rtt/rtt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/shell/shell.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/shell/ringbuf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/thread/libcpp_threads.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/event_bus/event_bus.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/indicator/indicator.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/button/button.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/main.cpp

    # nRF54L15 MDK system init
    ${NRFX_LOCAL}/mdk/system_nrf54l.c

    # nrfx drivers
    ${NRFX_LOCAL}/nrfx/drivers/src/nrfx_grtc.c
    ${NRFX_LOCAL}/nrfx/drivers/src/nrfx_clock.c
    ${NRFX_LOCAL}/nrfx/drivers/src/nrfx_power.c
    ${NRFX_LOCAL}/nrfx/drivers/src/nrfx_uarte.c
    ${NRFX_LOCAL}/nrfx/drivers/src/nrfx_pwm.c
    ${NRFX_LOCAL}/nrfx/drivers/src/nrfx_gpiote.c
    ${NRFX_LOCAL}/nrfx/drivers/src/nrfx_i2s.c
    ${NRFX_LOCAL}/nrfx/drivers/src/nrfx_saadc.c

    # Project HAL wrappers
    ${NRFX_LOCAL}/hal/hal_gpio.c
    ${NRFX_LOCAL}/hal/hal_delay.c
    ${NRFX_LOCAL}/hal/hal_uart.c
    ${NRFX_LOCAL}/hal/hal_pwm.c

    # Startup
    ${CMAKE_CURRENT_SOURCE_DIR}/startup/startup_nrf54l15.c

    # Board
    ${CMAKE_CURRENT_SOURCE_DIR}/../../boards/xiao_nrf54l15/board.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../boards/xiao_nrf54l15/pcm_audio.c

    # Project app
    ${CMAKE_CURRENT_SOURCE_DIR}/app_init.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app_led_ctl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/syscalls.c
)

add_executable(${PROJECT_NAME} ${SOURCES})

# ---- Include paths -----------------------------------------------------
# System headers (libc++, nrfx)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
    "${SDK_ROOT}/include/c++/v1"
    ${NRFX_LOCAL}/mdk
    ${NRFX_LOCAL}/cmsis
    ${NRFX_LOCAL}/nrfx                    # nrfx.h, <hal/nrf_*.h>, <drivers/nrfx_*.h>
    ${NRFX_LOCAL}/nrfx/drivers/include    # <nrfx_grtc.h>, etc.
    ${NRFX_LOCAL}/nrfx/templates          # nrfx_config.h, chip-specific config
)

# Project headers
target_include_directories(${PROJECT_NAME} PRIVATE
    ${NRFX_LOCAL}                        # nrfx_glue.h, nrfx_log.h
    ${NRFX_LOCAL}/hal                     # hal_gpio.h, hal_delay.h, hal_uart.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/segger-rtt
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/rt-thread/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/sys
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/shell
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/thread
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/event_bus
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/indicator
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/button
    ${CMAKE_CURRENT_SOURCE_DIR}/../../boards/xiao_nrf54l15
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ---- Compile definitions -----------------------------------------------
target_compile_definitions(${PROJECT_NAME} PRIVATE
    NRF54L15_XXAA
    NRF_APPLICATION
    NRFX_POWER_ENABLED=1
    NRFX_CLOCK_ENABLED=1
    NRFX_PWM_ENABLED=1
    NRFX_PWM20_ENABLED=1
    NRFX_UARTE_ENABLED=1
    NRFX_UARTE20_ENABLED=1
    NRFX_I2S_ENABLED=1
    NRFX_I2S20_ENABLED=1
    NRFX_SAADC_ENABLED=1
    NRF_CONFIG_CPU_FREQ_MHZ=128
)

# ---- Compile options ---------------------------------------------------
nrf_common_options(${PROJECT_NAME})

# ---- Linking -----------------------------------------------------------
nrf_link_libraries(${PROJECT_NAME}    "${CMAKE_CURRENT_SOURCE_DIR}/startup/nrf54l15.ld")

# ---- Post-build --------------------------------------------------------
nrf_post_build(${PROJECT_NAME})

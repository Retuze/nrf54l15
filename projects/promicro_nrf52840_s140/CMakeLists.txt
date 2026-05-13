# CMake build for ProMicro nRF52840 + S140 SoftDevice firmware.
cmake_minimum_required(VERSION 4.3)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(APP_TARGET firmware)

# M4F SDK from lib-build (picolibc + libc++ + compiler-rt).
set(SDK_ROOT_DEFAULT "E:/lib-build/out/arm-none-eabi/cortex-m4-hard-fpv4-sp-d16")
if(NOT DEFINED SDK_ROOT OR SDK_ROOT STREQUAL "")
    set(SDK_ROOT "${SDK_ROOT_DEFAULT}" CACHE PATH "SDK sysroot (M4F)" FORCE)
endif()

include(../../cmake/common.cmake)

project(${APP_TARGET} LANGUAGES C CXX ASM)

# S140 SDK (our minimal headers: nrf_sd_isr.h, nrf_sdm.h)
# S140 API stubs (nrf_sdm.h + nrf_sd_isr.h)
# nrf_svc.h, nrf_error.h, nrf_error_sdm.h, nrf_soc.h from S115 path
if(NOT SOFTDEVICE_API_ROOT)
    set(SOFTDEVICE_API_ROOT "${CMAKE_SOURCE_DIR}/../../softdevice/s140/api" CACHE PATH "S140 API" FORCE)
endif()
# S115 BLE headers (shared: ble.h, ble_gap.h, ble_gatts.h — not in S140 API)
set(S115_BLE_API "E:/nrf-bm/nrf-bm/components/softdevice/nrf54l/s115/s115_API/include"
    CACHE PATH "S115 BLE headers")

# Sources
set(SOURCES
    ../../common/segger-rtt/rtt.c
    ../../framework/thread/libcpp_threads.c
    ../../framework/pm/pm_port.c
    ../../framework/event_bus/event_bus.c
    ../../framework/indicator/indicator.c
    ../../framework/button/button.c
    ../../framework/services/ble_lbs.c
    ../../framework/services/lbs_app.c
    ../../common/rt-thread/src/clock.c
    ../../common/rt-thread/src/components.c
    ../../common/rt-thread/src/device.c
    ../../common/rt-thread/src/idle.c
    ../../common/rt-thread/src/ipc.c
    ../../common/rt-thread/src/irq.c
    ../../common/rt-thread/src/kservice.c
    ../../common/rt-thread/src/mem.c
    ../../common/rt-thread/src/object.c
    ../../common/rt-thread/src/scheduler.c
    ../../common/rt-thread/src/thread.c
    ../../common/rt-thread/src/timer.c
    ../../common/rt-thread/libcpu/arm/cortex-m4/cpuport.c
    ../../common/rt-thread/libcpu/arm/cortex-m4/context_gcc.S
    ../../mcu/nrf52840/ll/ll_gpio.c
    ../../mcu/nrf52840/ll/ll_systick.c
    ../../mcu/nrf52840/ll/ll_nvic.c
    ../../mcu/nrf52840/ll/ll_gpiote.c
    ../../mcu/nrf52840/ll/system_nrf52840.c
    ../../mcu/nrf52840/hal/hal_gpio.c
    ../../mcu/nrf52840/hal/hal_delay.c
    startup/startup_nrf52840.c
    ../../softdevice/s140/softdevice_port.c
    ../../softdevice/s140/softdevice_irq_forward.S
    ../../boards/promicro_nrf52840/board.c
    ../../framework/sys/rtthread_port.c
    ../../framework/shell_port.c
    ../../framework/main.cpp
    app_init.cpp
    app_led_ctl.c
    syscalls.c
)

add_executable(${APP_TARGET}.elf ${SOURCES})

target_include_directories(${APP_TARGET}.elf SYSTEM PRIVATE
    "${SDK_ROOT}/include/c++/v1"
    "${SOFTDEVICE_API_ROOT}"
    "${S115_BLE_API}"
)
target_include_directories(${APP_TARGET}.elf PRIVATE
    ../../common/segger-rtt
    ../../common/rt-thread/include
    ../../hal
    ../../mcu/nrf52840/ll
    ../../mcu/nrf52840/hal
    ../../boards/promicro_nrf52840
    ../../softdevice/s140
    ../../framework
    ../../framework/sys
    ../../framework/thread
    ../../framework/pm
    ../../framework/indicator
    ../../framework/button
    ../../framework/event_bus
    ../../framework/services
    ../../softdevice/s140
    .
)

target_compile_definitions(${APP_TARGET}.elf PRIVATE NRF52840)

set(LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/startup/nrf52840.ld")
set_target_properties(${APP_TARGET}.elf PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})
set(CRT0_OBJ "${SDK_ROOT}/picolibc/lib/crt0.o")

target_compile_options(${APP_TARGET}.elf PRIVATE
    -ffreestanding
    -Wno-unused-parameter
    $<$<CONFIG:Debug>:-O0 -g3>
    $<$<CONFIG:MinSizeRel>:-Oz -g>
    $<$<CONFIG:Release>:-O2>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -nostdinc++>
)

target_link_directories(${APP_TARGET}.elf PRIVATE
    "${SDK_ROOT}/lib"
    "${SDK_ROOT}/lib/baremetal"
    "${SDK_ROOT}/picolibc/lib"
)

target_link_options(${APP_TARGET}.elf PRIVATE
    -fuse-ld=lld -nostdlib -nostdlib++ -nostartfiles
    -Wl,--gc-sections
    -T${LINKER_SCRIPT}
    -Wl,-Map=${APP_TARGET}.map
    -Wl,--print-memory-usage
    ${CRT0_OBJ}
)

target_link_libraries(${APP_TARGET}.elf PRIVATE
    c++ c++abi c m :libclang_rt.builtins-arm.a
)

# ---- SoftDevice merge --------------------------------------------------
# Download: https://www.nordicsemi.com/Products/Development-software/S140/Download
# Save to: E:/nrf-bm/nrf-bm/components/softdevice/nrf52840/s140/
find_package(Python3 COMPONENTS Interpreter)
set(SD_HEX_DEFAULT "E:/nrf-bm/nrf-bm/components/softdevice/nrf52840/s140/s140_nrf52_7.3.0_softdevice.hex")
set(SD_HEX "${SD_HEX_DEFAULT}" CACHE FILEPATH "S140 SoftDevice hex")
set(MERGEHEX "E:/nrf-bm/zephyr/scripts/build/mergehex.py" CACHE FILEPATH "mergehex.py")
set(_merge_ok FALSE)
if(EXISTS "${MERGEHEX}" AND Python3_EXECUTABLE AND EXISTS "${SD_HEX}")
    set(_merge_ok TRUE)
endif()

set(_post_cmds
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${APP_TARGET}.elf> ${APP_TARGET}_app.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${APP_TARGET}.elf> ${APP_TARGET}.bin
    COMMAND ${CMAKE_SIZE}    $<TARGET_FILE:${APP_TARGET}.elf>
)
if(_merge_ok)
    list(APPEND _post_cmds
        COMMAND "${Python3_EXECUTABLE}" "${MERGEHEX}"
            -o "${CMAKE_CURRENT_BINARY_DIR}/${APP_TARGET}.hex"
            "${SD_HEX}" "${CMAKE_CURRENT_BINARY_DIR}/${APP_TARGET}_app.hex")
else()
    list(APPEND _post_cmds
        COMMAND ${CMAKE_COMMAND} -E rename
            "${CMAKE_CURRENT_BINARY_DIR}/${APP_TARGET}_app.hex"
            "${CMAKE_CURRENT_BINARY_DIR}/${APP_TARGET}.hex")
endif()
add_custom_command(TARGET ${APP_TARGET}.elf POST_BUILD ${_post_cmds}
    COMMENT "Post-build" VERBATIM)

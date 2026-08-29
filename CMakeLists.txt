cmake_minimum_required(VERSION 3.20)

# Toolchain must be set on the command line:
#   cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-clang.cmake
project(nrf54l15_bare C ASM)

set(LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/linker/nrf54l15_cpuapp.ld")

add_executable(app.elf
    src/startup_nrf54l15.c
    src/main.c
    src/grtc.c
    src/uart.c
    src/gatt.c
)

target_include_directories(app.elf PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/vendor/mdk
    ${CMAKE_SOURCE_DIR}/vendor/cmsis
)

# Select the nRF54L15 application core in the Nordic MDK headers.
target_compile_definitions(app.elf PRIVATE
    NRF54L15_XXAA
    NRF_APPLICATION
)

target_link_options(app.elf PRIVATE
    -T${LINKER_SCRIPT}
    -Wl,-Map=${CMAKE_BINARY_DIR}/app.map
    -Wl,--print-memory-usage
)

set_target_properties(app.elf PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

# Produce .hex and .bin, and print size, after linking.
add_custom_command(TARGET app.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:app.elf> ${CMAKE_BINARY_DIR}/app.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:app.elf> ${CMAKE_BINARY_DIR}/app.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:app.elf>
    COMMENT "Generating app.hex / app.bin and size report"
)

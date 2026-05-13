# common.cmake — shared build rules for nRF projects.

# C/C++ standards
set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Post-build hook: generate .hex, .bin, and size report
function(nrf_post_build TARGET)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND llvm-objcopy -O ihex   $<TARGET_FILE:${TARGET}> ${TARGET}.hex
        COMMAND llvm-objcopy -O binary $<TARGET_FILE:${TARGET}> ${TARGET}.bin
        COMMAND llvm-size  $<TARGET_FILE:${TARGET}>
        COMMENT "Post-build: .hex, .bin, size"
        VERBATIM
    )
endfunction()

# Common compile options for all targets
function(nrf_common_options TARGET)
    target_compile_options(${TARGET} PRIVATE
        -ffreestanding
        -fno-common
        -fdata-sections
        -ffunction-sections
        -Wall -Wextra
        -Wno-unused-parameter
        -Wno-gnu-anonymous-struct
        -Wno-nested-anon-types
        -Wno-reserved-id-macro
        -Wno-unused-but-set-variable
        -Wno-c11-extensions
        $<$<CONFIG:Debug>:-O0 -g3>
        $<$<CONFIG:MinSizeRel>:-Oz -g>
        $<$<CONFIG:Release>:-O2>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -nostdinc++>
    )
endfunction()

# Link with picolibc C library + libc++ + compiler-rt, using crt0.o
function(nrf_link_libraries TARGET LINKER_SCRIPT)
    set(CRT0_OBJ "${SDK_ROOT}/picolibc/lib/crt0.o")

    set_target_properties(${TARGET} PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

    target_link_directories(${TARGET} PRIVATE
        "${SDK_ROOT}/lib"
        "${SDK_ROOT}/lib/baremetal"
        "${SDK_ROOT}/picolibc/lib"
    )

    target_link_options(${TARGET} PRIVATE
        -fuse-ld=lld -nostdlib -nostdlib++ -nostartfiles
        -Wl,--gc-sections
        -T${LINKER_SCRIPT}
        -Wl,-Map=${TARGET}.map
        -Wl,--print-memory-usage
        ${CRT0_OBJ}
    )

    target_link_libraries(${TARGET} PRIVATE
        c c++abi c++ m :libclang_rt.builtins-arm.a
    )
endfunction()

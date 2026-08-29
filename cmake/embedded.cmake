# 嵌入式应用构建辅助（芯片无关）。
# 依赖：toolchain.cmake 已加载（可选提供 EMBED_PICOLIBC_BASE / EMBED_LIBGCC_DIR）。
#
# 用法（project/<name>/CMakeLists.txt）：
#   include(${PROJ_ROOT}/cmake/embedded.cmake)
#   add_executable(test main.c startup.c)
#   embedded_app(test ${CMAKE_CURRENT_SOURCE_DIR}/link.ld)

function(embedded_app app_name linker_script)
  target_compile_options(${app_name} PRIVATE -Os -Wall -Wextra)

  # 有 libc 目标（targets 文件定义了 EMBED_PICOLIBC_BASE）时提供头/库/搜索路径。
  # libc/libm/libgcc 必须排在对象文件之后（target_link_libraries 自动保证顺序）
  if(DEFINED EMBED_PICOLIBC_BASE AND NOT EMBED_PICOLIBC_BASE STREQUAL "")
    target_include_directories(${app_name} SYSTEM PRIVATE ${EMBED_PICOLIBC_BASE}/include)
    target_link_libraries(${app_name} PRIVATE c m gcc)
  endif()

  # 自包含链接脚本 -T 显式指定；--gc-sections 剔未引用段
  target_link_options(${app_name} PRIVATE
    -nostdlib -nostartfiles
    -T${linker_script}
    -Wl,--gc-sections
    -Wl,-Map,${CMAKE_BINARY_DIR}/${app_name}.map
  )
  # 库搜索路径只在有 libc 目标时给出（无 libc 目标不能残留 -L/lib 这类无效项）
  if(DEFINED EMBED_PICOLIBC_BASE AND NOT EMBED_PICOLIBC_BASE STREQUAL "")
    target_link_options(${app_name} PRIVATE
      -L${EMBED_PICOLIBC_BASE}/lib
      -L${EMBED_LIBGCC_DIR}
    )
  endif()

  # 产出 .elf 后生成 .hex/.bin 并打印尺寸
  add_custom_command(TARGET ${app_name} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex  $<TARGET_FILE:${app_name}> ${CMAKE_BINARY_DIR}/${app_name}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${app_name}> ${CMAKE_BINARY_DIR}/${app_name}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${app_name}>
  )
endfunction()

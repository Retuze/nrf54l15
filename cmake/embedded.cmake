# 嵌入式应用构建辅助（芯片无关）。
# 依赖：toolchain.cmake 已加载（可选提供 EMBED_PICOLIBC_BASE / EMBED_LIBGCC_DIR）。
#
# 用法（project/<name>/CMakeLists.txt）：
#   include(${PROJ_ROOT}/cmake/embedded.cmake)
#   add_executable(test main.c)     # 只列应用本体
#   embedded_app(test)
#
# 启动/链接自动装配：默认用 targets 文件里的共享版
# （EMBED_STARTUP_SRC / EMBED_LINKER_SCRIPT，drivers/core/）；
# 实验目录里放同名 startup.c / link.ld 即自动改用工程自己的版本。

function(embedded_app app_name)
  target_compile_options(${app_name} PRIVATE -Os -Wall -Wextra)

  # 启动文件与链接脚本：工程目录优先，否则共享版
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/startup.c")
    set(_startup "${CMAKE_CURRENT_SOURCE_DIR}/startup.c")
  else()
    set(_startup "${EMBED_STARTUP_SRC}")
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/link.ld")
    set(_ld "${CMAKE_CURRENT_SOURCE_DIR}/link.ld")
  else()
    set(_ld "${EMBED_LINKER_SCRIPT}")
  endif()
  target_sources(${app_name} PRIVATE ${_startup})
  set_target_properties(${app_name} PROPERTIES LINK_DEPENDS ${_ld})

  # 有 libc 目标（targets 文件定义了 EMBED_PICOLIBC_BASE）时提供头/库/搜索路径。
  # libc/libm/编译器辅助库（clang builtins 替代 libgcc）必须排在对象文件之后
  # （target_link_libraries 自动保证顺序：libc 引用的 __aeabi_* 在最后解析）
  if(DEFINED EMBED_PICOLIBC_BASE AND NOT EMBED_PICOLIBC_BASE STREQUAL "")
    target_include_directories(${app_name} SYSTEM PRIVATE ${EMBED_PICOLIBC_BASE}/include)
    target_link_libraries(${app_name} PRIVATE c m ${EMBED_COMPILER_RT_LIB})
  endif()

  # 自包含链接脚本 -T 显式指定；--gc-sections 剔未引用段
  target_link_options(${app_name} PRIVATE
    -nostdlib -nostartfiles
    -T${_ld}
    -Wl,--gc-sections
    -Wl,-Map,${CMAKE_BINARY_DIR}/${app_name}.map
  )
  # 库搜索路径只在有 libc 目标时给出（无 libc 目标不能残留 -L/lib 这类无效项）
  if(DEFINED EMBED_PICOLIBC_BASE AND NOT EMBED_PICOLIBC_BASE STREQUAL "")
    target_link_options(${app_name} PRIVATE
      -L${EMBED_PICOLIBC_BASE}/lib
    )
  endif()

  # 产出 .elf 后生成 .hex/.bin 并打印尺寸
  add_custom_command(TARGET ${app_name} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex  $<TARGET_FILE:${app_name}> ${CMAKE_BINARY_DIR}/${app_name}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${app_name}> ${CMAKE_BINARY_DIR}/${app_name}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${app_name}>
  )
endfunction()

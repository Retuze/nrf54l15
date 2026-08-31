# —— common 协议层宿主单测 ——
# host 普通构建（不进交叉工具链/embedded.cmake），直接把 common 源码编
# 进来：被测代码即线上代码，测试靠注入。编译器固定 clang（与交叉编译
# 同一编译器，警告行为一致；覆盖率走 llvm-cov，见 scripts/coverage.sh）。
cmake_minimum_required(VERSION 3.20)
set(CMAKE_C_COMPILER clang)   # 必须在 project() 之前
project(tests C)

add_executable(test_gatt
  gatt_test.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/bluetooth/gatt/gatt.c)
target_include_directories(test_gatt PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/bluetooth/gatt)

add_executable(test_ll
  ll_test.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/bluetooth/ll/ll.c)
target_include_directories(test_ll PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/bluetooth/ll)

add_executable(test_ring
  ring_test.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/ring/ring.c)
target_include_directories(test_ring PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/ring)

add_executable(test_log
  log_test.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/log/log.c)
target_include_directories(test_log PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/log)

add_executable(test_cobs
  cobs_test.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/cobs/cobs.c)
target_include_directories(test_cobs PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/cobs)

add_executable(test_proto
  proto_test.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/proto/proto.c)
target_include_directories(test_proto PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/proto)

add_executable(test_sched
  sched_test.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/bluetooth/ll/sched.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/bluetooth/ll/scan.c)
target_include_directories(test_sched PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/bluetooth/ll)

foreach(t test_gatt test_ll test_ring test_log test_cobs test_proto test_sched)
  target_compile_options(${t} PRIVATE -Wall -Wextra)
endforeach()

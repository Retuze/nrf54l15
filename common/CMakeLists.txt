# 平台无关组件（协议等）集中在这里。
# 规则：不 include 任何 nRF54L15 寄存器；需要底层能力时通过调用方注入的接口。

# ATT/GATT 服务端：纯协议（无寄存器访问），host 可单测（tests/gatt_test.c）
add_library(gatt STATIC
  bluetooth/gatt/gatt.c)
target_include_directories(gatt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/bluetooth/gatt)

# LL 链路层：纯协议 + ll_ops 注入（无寄存器访问），host 可单测（tests/ll_test.c）
add_library(ll STATIC
  bluetooth/ll/ll.c)
target_include_directories(ll PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/bluetooth/ll)

# SPSC 字节环形缓冲（平台无关，ISR 生产/主循环消费），host 可单测
add_library(ring STATIC
  ring/ring.c)
target_include_directories(ring PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ring)

# 实时日志：格式化 + 注入（log_sink 注入，无寄存器访问），host 可单测
add_library(log STATIC
  log/log.c)
target_include_directories(log PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/log)
# log.c 用 picolibc 的 vsnprintf/strlen（目标机格式化）；无 libc 目标时跳过
if(DEFINED EMBED_PICOLIBC_BASE AND NOT EMBED_PICOLIBC_BASE STREQUAL "")
  target_include_directories(log SYSTEM PRIVATE ${EMBED_PICOLIBC_BASE}/include)
endif()

# COBS 帧定界编解码（UART 传输层，无长度上限），host 可单测
add_library(cobs STATIC
  cobs/cobs.c)
target_include_directories(cobs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cobs)

# 固件/app 通用协议（帧/TLV/分片/CRC16，传输注入），host 可单测
add_library(proto STATIC
  proto/proto.c)
target_include_directories(proto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/proto)

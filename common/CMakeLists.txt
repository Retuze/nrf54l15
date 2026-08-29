# 平台无关组件（协议等）集中在这里。
# 规则：不 include 任何 nRF54L15 寄存器；需要底层能力时通过调用方注入的接口。

# ATT/GATT 服务端：纯协议（无寄存器访问），host 可单测（tests/ 待建）
add_library(gatt STATIC
  bluetooth/gatt/gatt.c)
target_include_directories(gatt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/bluetooth/gatt)

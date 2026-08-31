# 固件/app 应用协议规格（common/proto + common/cobs）

固件与 app（Android BLE / host 串口工具）之间传输无关的应用协议。
C 参考实现：`common/proto`（帧/TLV/分片/CRC16）+ `common/cobs`（UART 定界），
宿主测试 90 项断言、覆盖率 93-97%。**本文件是 app 侧移植契约。**

## 1. 分层

```
应用层（字段语义、触发策略、重传定时器）
common/proto   逻辑消息 + TLV + 分片重组 + CRC16（传输无关）
common/cobs    UART 专用帧定界（BLE 不用）
传输           UART = COBS(proto 传输帧) + 0x00 定界
               BLE  = proto 传输帧原样 = ATT notification/write 载荷
```

## 2. 传输帧格式（大端）

```
off 0  TYPE     u16     消息类型
off 2  SEQFLAGS u8      bit5=ACK_REQ | bit4=MORE | bit3..0=SEQ(0..15)
off 3  MSG_ID   u8      各自方向独立自增
off 4  LEN      u16     本帧 PAYLOAD 长度
off 6  PAYLOAD  LEN 字节
off 6+LEN  CRC16 (2 字节, 大端, 覆盖 off0..6+LEN-1)
```

- CRC16：poly 0x1021 init 0xFFFF（CCITT-FALSE，无反射无 xorout）。
  参考实现 `proto_crc16()`，"123456789" → 0x29B1
- **分片**：仅 BLE 路径——逻辑消息 > mtu 时按 SEQ 递增拆发，MORE=1 标记
  非尾片；接收端 SEQ 不连续即丢弃整包。UART 路径恒单帧（SEQ=0/MORE=0），
  COBS 无长度上限
- 消息总长上限 1024 字节（UART 接收 cap）

## 3. 消息类型（TYPE u16，只增不改）

| TYPE | 消息 | 方向 | 载荷 |
|---|---|---|---|
| 0x0001 | GET_REQ | app→fw | 请求的 T 字节序列；空 = 全部字段 |
| 0x0002 | REPORT | fw→app | TLV 序列；ACK_REQ 分两级送达 |
| 0x0003 | SET | app→fw | TLV 序列；MSG_ID 幂等 |
| 0x0004 | ACK | 双向 | `{err u8, acked_msg_id u8}` |

**送达语义**：
- REPORT ACK_REQ=1（状态类：首次同步/一次性事件）：app 必须回 ACK；
  fw 超时同 id 重发，3 次未确认上抛（多半断链）
- REPORT ACK_REQ=0（遥测类：IMU/RSSI 流）：尽力而为，丢了发更新的
- SET：app 超时（500ms 建议）同 id 同载荷重发；fw 同 id 去重（重发 ACK
  不重复执行）——飞控命令类 SET 尽量写成绝对目标形式，配合 id 幂等
- MSG_ID 回绕（255→0）在秒级重传窗口内不碰撞，无需处理
- 重传/超时是应用层策略，协议层无状态（重调发送函数即可）

## 4. TLV 字段（T u8 只增不改）

```
TLV = { T u8, L u8, V[L] }（单字段 L ≤ 255）
```

| T | 字段 | 载荷布局（大端） | 状态 |
|---|---|---|---|
| 0x01 | BATTERY | level u8, voltage_mv u16, flags u8（bit0=充电中） | 实现 |
| 0x02 | TIME | epoch_s u32（2038 前有效）, tz_min i16 | 实现 |
| 0x03 | DEV_INFO | fw_maj u8, fw_min u8, fw_patch u8, hw_rev u8, name[] | 实现 |
| 0x04 | UPTIME | uptime_s u32 | 实现 |
| 0x05 | STATUS | flags u32（位定义逐位补充） | 实现 |
| 0x06 | RSSI | rssi i8 | 预留（需 ll 层 RSSISAMPLE） |
| 0x07 | CONN_PARAMS | interval_ms u16, timeout_ms u16, mtu u16 | 预留 |
| 0x08 | IMU6 | ax..az i16×3, gx..gz i16×3 | 预留（飞控） |
| 0x09 | HR_STEPS | bpm u8, steps u24 | 预留（手环） |
| 0x0A | GPS_FIX | lat i32, lon i32, alt i16, sats u8 | 预留（飞控） |
| 0x0B | ALARM | alarm_epoch u32, flags u8 | 预留（手表） |
| 0x0C | ERR_LOG | code u8, arg u32 | 预留 |
| 0x0D | BOOT_REASON | reason u8, reset_count u32 | 预留 |
| 0x0E | MCU_TEMP | temp_mc i16 | 预留 |

**扩展约定**：T 不够用时载荷首字节做**子命令**细分（对 proto 层透明，
应用解析）：`V = [sub u8 | 子载荷]`。消息 TYPE 管大类（飞控命令族/固件
更新族），T 管字段，子命令管细分——三层扩展空间。

## 5. 首次连接同步（一次交互）

```
BLE:  connect → DLE/MTU 协商（固件已有）→ app GET_REQ{}（空=全部）
      → fw REPORT{ BATTERY, TIME, DEV_INFO, UPTIME, STATUS } ACK_REQ
      → app ACK{0, id} → 同步完成
UART: 打开串口 →（发送侧先发 0x00 空帧清对端 COBS 状态）
      app GET_REQ{} → fw REPORT{...} ACK_REQ → app ACK
```

## 6. 传输映射（54L 实现状态，05_proto 实验）

- **BLE**：custom service 0xFFF0/0xFFF1，0xFFF1 属性 = Read|WriteNoResp|
  Notify（0x16）。上行 app→fw：**一个 write = 一个 proto 传输帧**（write-
  no-response 为主，with-response 同一回调——调试工具默认走后者）；
  下行 fw→app：proto 帧进**通知队列**（4 槽），LL 每个连接事件顶替空
  回复发一条（DLE 完成后才发大帧；纯空 ack 被通知顶替是合法的，SN/NESN
  由通知承载）。单条 REPORT ≤ 244 字节走单帧；多帧报告依赖队列深度 4。
  标准 Battery Service (0x2A19) 保持原样给通用 BLE 工具
- **UART**（待接线实验）：独立串口实例（非 console）；RX 回调逐字节进
  COBS 解码器，帧完成进 proto_feed；发送 = proto 传输帧经 cobs_encode 后
  走 uart_tx_async。噪声后先发 0x00 空帧（COBS 无帧头，只有帧尾定界）

## 7. app 侧移植要点

- 按第 2 节格式组帧/解帧；CRC16 参考向量 "123456789" → 0x29B1
- BLE：分片 SEQ 重装；UART：COBS 解码（隐零规则见 common/cobs/cobs.c 注释）
- msg_id 自增、SET 超时重发、REPORT ACK_REQ 回 ACK——三件套一个不能少
- 字节序：全部字段大端

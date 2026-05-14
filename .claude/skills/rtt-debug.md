---
name: rtt-debug
description: 启动 OpenOCD RTT 调试，telnet 端口 9090
---

# OpenOCD RTT Debug

启动 OpenOCD 并开启 RTT telnet 服务。固件会在首次提示符前发送 `IAC WILL ECHO` + `IAC WILL SUPPRESS_GO_AHEAD`，telnet 客户端收到后自动关闭本地回显，不会双回显。

## 步骤

1. 检查端口 9090 是否被占用，如占用则杀掉对应进程
2. 启动 OpenOCD（后台运行）：

```
openocd -f boards/xiao_nrf54l15/support/openocd.cfg -c "init;reset run;nrf54l-rtt"
```

3. 用户直接连接：

```
telnet 127.0.0.1 9090
```

无需额外代理。

# 验证指南

## 自动回归

按 [README](../README.md) 构建后运行。需要 Python 3 和 `socat`，并保证本机 TCP 1502、1506 端口可用。

```bash
ctest --test-dir build-cmake --output-on-failure
```

| CTest 用例 | 覆盖范围 |
|---|---|
| `libmodbus_version` | libmodbus 版本检查程序正常运行 |
| `gateway_system` | TCP 后端转发、TCP→RTU 跨连接读写、TCP 分段输入、RTU Unit ID、配置校验及每连接一请求的边界 |
| `gateway_capacity` | 队满和排队过期回复 `0x06`，过期写请求不会到达后端 |
| `gateway_disconnect` | 客户端在转发时断开，以及后来复用文件描述符的连接仍收到自己的回复 |
| `gateway_shutdown` | `SIGINT` / `SIGTERM` 退出、等待请求取消和监听端口重新绑定 |

系统回归还检查 RTU 非法寄存器地址返回 `0x02`，设备不存在或不回复时返回 `0x0B`，随后另一条连接仍可读回寄存器。TCP 后端断开时验证 `0x0A`，并在重新启动后验证正常转发。

## 手动联调

从项目根目录执行以下命令。两种模式均使用默认 TCP 监听地址 `127.0.0.1:1506`，每次复测前重新启动从站和网关。

### TCP 后端

在终端 1 启动模拟从站。

```bash
./build-cmake/modbus_server
```

在终端 2 启动网关，然后用下节的客户端脚本发送请求。

```bash
./build-cmake/gateway_shared_backend_demo
```

### RTU 后端

在终端 1 建立虚拟串口对，保持 `socat` 运行。

```bash
socat -d -d \
  pty,raw,echo=0,link=/tmp/modbus-rtu-server \
  pty,raw,echo=0,link=/tmp/modbus-rtu-client
```

在终端 2 启动地址为 1、串口参数为 9600 8N1 的模拟从站，接收两条请求。

```bash
./build-cmake/modbus_rtu_server /tmp/modbus-rtu-server 2
```

在终端 3 启动网关，然后用下节的客户端脚本发送请求。

```bash
./build-cmake/gateway_shared_backend_demo /tmp/modbus-rtu-client
```

### 跨连接写入与读回

再开一个终端。TCP 后端执行 `export UNIT=255`，RTU 后端执行 `export UNIT=1`，再运行以下脚本。连接 A 将保持寄存器地址 1 写为 999，连接 B 读回地址 0～2 的三个值。

```bash
python3 - <<'PY'
import os
import socket
import struct

unit = int(os.environ["UNIT"])
address = ("127.0.0.1", 1506)

def request(transaction, function, register, value):
    return struct.pack(">HHHBBHH", transaction, 0, 6, unit, function, register, value)

with socket.create_connection(address, timeout=3) as client, client.makefile("rb") as reply:
    write_request = request(1, 6, 1, 999)
    client.sendall(write_request)
    assert reply.read(12) == write_request

with socket.create_connection(address, timeout=3) as client, client.makefile("rb") as reply:
    client.sendall(request(2, 3, 0, 3))
    expected = struct.pack(">HHHBBB3H", 2, 0, 9, unit, 3, 6, 123, 999, 789)
    response = reply.read(len(expected))
    assert response == expected, response.hex(" ")
    print(struct.unpack(">3H", response[9:]))
PY
```

预期输出 `(123, 999, 789)`。两条连接处理结束后，网关退出。

## 性能基准

构建时启用 `BUILD_TESTING`（默认开启），然后运行 [gateway_benchmark.py](../tests/gateway_benchmark.py)。

```bash
GATEWAY_FOUR_CLIENTS="$PWD/build-cmake/gateway_four_clients" \
  python3 -B tests/gateway_benchmark.py --rounds 25
```

基准比较逐条发送和四条并发发送，每种模式 25 轮、每轮四条读请求。虚拟 RTU 设备每次回复前等待 20 ms。脚本输出吞吐、客户端 RTT 的 P50/P95、排队和转发耗时，可用于观察固定后端处理时间下的并发排队成本。

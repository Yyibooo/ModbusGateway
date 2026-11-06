# ModbusGateway

这是一个在 WSL Ubuntu 上逐步实现的 Linux C 网关项目。当前可运行的链路是 TCP 客户端 → 多客户端前端 → 有界请求队列 → 单个后端工作线程 → Modbus TCP 或 RTU 模拟设备。

TCP → RTU 已用虚拟串口联调。两个 TCP 客户端可以分别写、读同一台 RTU 设备；A 写入地址 1 为 999 后，B 读到 123、999、789。当前是教学版，每轮接入两个客户端，每个连接处理一条请求。当前学习位置见[学习进度](docs/学习进度.md)。

## 项目文件

| 路径 | 用途 |
|---|---|
| [gateway_shared_backend_demo.c](gateway_shared_backend_demo.c) | 当前的多客户端网关，默认使用 TCP 后端，传入串口路径时使用 RTU 后端 |
| [modbus_gateway.c](modbus_gateway.c) | 早期单客户端 TCP 代理，用来对照连接与转发流程 |
| [modbus_server.c](modbus_server.c) / [modbus_client.c](modbus_client.c) | TCP 模拟设备和一写一读客户端，客户端默认连接单客户端代理的 1503 端口 |
| [modbus_rtu_server.c](modbus_rtu_server.c) / [modbus_rtu_client.c](modbus_rtu_client.c) | RTU 模拟设备和独立主站 |
| [examples/](examples/) | Socket 收发、libmodbus 版本检查、单槽位、请求队列、双连接接入等分阶段练习 |
| [tests/](tests/) | 连接在途断开、文件描述符复用等回归测试 |
| [docs/](docs/) | 学习计划、验收记录、经验和会话交接 |
| build/ | 本地构建产物，由 Git 忽略 |

## 构建

当前环境是 WSL Ubuntu 24.04，项目位于 `/home/craig/ModbusGateway`，使用 GCC、GNU Make、Python 3、libmodbus 3.2.0 和 socat。Makefile 默认在用户目录 `~/.local/libmodbus-3.2.0/lib/pkgconfig` 查找库；安装路径不同可传入 `MODBUS_PKGCONFIG_DIR=/path/to/pkgconfig`。VS Code 用 WSL 模式打开项目。

```bash
cd ~/ModbusGateway
make check-modbus
make modbus_rtu_server modbus_rtu_client gateway_shared_backend_demo
```

`make` 默认构建 echo 双端。各程序可用 `make <程序名>` 单独构建，运行文件在 `build/<程序名>`。`make clean` 清理该目录下 Makefile 管理的产物。

## 复现 TCP → RTU 写入和读回

在四个 WSL 终端按顺序运行。除 socat 外，其余命令从项目目录执行。

**终端 1** 建立一对虚拟串口，保持 socat 运行。当前 WSL 将 socat 安装在用户目录；系统安装时可直接调用 `socat`。

```bash
~/.local/bin/socat -d -d \
  pty,raw,echo=0,link=/tmp/modbus-rtu-server \
  pty,raw,echo=0,link=/tmp/modbus-rtu-client
```

**终端 2** 启动设备地址为 1 的 RTU 从站。最后的 2 表示这一轮接收两条请求；省略时接收一条。

```bash
./build/modbus_rtu_server /tmp/modbus-rtu-server 2
```

**终端 3** 启动网关。TCP 前端监听 1506，RTU 后端打开串口对的另一端，串口参数为 9600 8N1。

```bash
./build/gateway_shared_backend_demo /tmp/modbus-rtu-client
```

**终端 4** 往网关的两条 TCP 连接分别发送写和读。两条请求的 Unit ID 都是 1。脚本先等写入确认，再请求读取三个寄存器。

```bash
python3 - <<'PY'
import socket
import struct

with socket.create_connection(("127.0.0.1", 1506), timeout=3) as a, \
     socket.create_connection(("127.0.0.1", 1506), timeout=3) as b, \
     a.makefile("rb") as reply_a, b.makefile("rb") as reply_b:
    write = bytes.fromhex("00 01 00 00 00 06 01 06 00 01 03 E7")
    a.sendall(write)
    assert reply_a.read(12) == write
    b.sendall(bytes.fromhex("00 01 00 00 00 06 01 03 00 00 00 03"))
    reply = reply_b.read(15)
    assert reply == bytes.fromhex("00 01 00 00 00 09 01 03 06 00 7B 03 E7 03 15")
    print("B 读回", struct.unpack(">3H", reply[9:]))
PY
```

预期打印 `B 读回 (123, 999, 789)`。读请求在 TCP 一侧是 12 字节进、15 字节出，在 RTU 一侧是 8 字节进、11 字节出。复测时保持 socat 运行，重新启动从站和网关。独立 RTU 主站实验使用同一对虚拟串口，客户端端点由 `./build/modbus_rtu_client /tmp/modbus-rtu-client` 打开。

## 连接断开回归

腾出 TCP 1502 和 1506 后运行。脚本会自行启动测试后端和网关，验证 A 请求在途时断开，B 仍收到正确的值，以及后来连接复用 A 的描述符编号时仍能收到自己的回复。

```bash
make gateway_shared_backend_demo
python3 tests/gateway_disconnect_demo.py
```

## 学习记录

- [学习计划](docs/学习计划.md)
- [学习进度和验收证据](docs/学习进度.md)
- [交接与经验](docs/交接与经验.md)

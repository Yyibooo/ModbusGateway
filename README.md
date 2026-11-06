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

## CMake 构建与一键回归

在 WSL 项目目录执行。CTest 的 RTU 用例需要 `socat`，会优先使用 PATH 中的版本，再查找 `~/.local/bin/socat`。

```bash
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-cmake -j
ctest --test-dir build-cmake --output-on-failure
```

可用 `-DMODBUS_PKGCONFIG_DIR=/path/to/pkgconfig` 指定库路径，或用 `-DBUILD_TESTING=OFF` 只构建程序。测试版本和正常程序都由 CMake 构建。队满、断连和退出回归使用 TCP 1502 与 1506，运行前应腾出这两个端口。

## 运行时配置

网关启动时读取以下环境变量。给 RTU 传入的第二个位置参数优先于 `MBGW_RESPONSE_TIMEOUT_MS`。配置有误时会在创建连接前报告字段并退出。

| 环境变量 | 默认值 | 范围或用途 |
|---|---|---|
| `MBGW_LISTEN_ADDRESS` / `MBGW_LISTEN_PORT` | `127.0.0.1` / `1506` | TCP 前端，IPv4 地址与端口 1～65535 |
| `MBGW_BACKEND_ADDRESS` / `MBGW_BACKEND_PORT` | `127.0.0.1` / `1502` | TCP 后端，IPv4 地址与端口 1～65535 |
| `MBGW_RTU_BAUD` | `9600` | 1～4000000，实际速率由串口驱动和设备支持 |
| `MBGW_RTU_DATA_BITS` / `MBGW_RTU_PARITY` / `MBGW_RTU_STOP_BITS` | `8` / `N` / `1` | 数据位 5～8，校验 N/E/O，停止位 1～2 |
| `MBGW_RESPONSE_TIMEOUT_MS` | libmodbus 默认 `500` | TCP 或 RTU 后端响应等待，正整数毫秒 |
| `MBGW_QUEUE_CAPACITY` | `2` | 等待槽位 1～256，运行中的后端任务不占槽位 |
| `MBGW_QUEUE_TIMEOUT_MS` | `0` | 出队时检查排队期限，0 表示关闭 |
| `MBGW_SEND_TIMEOUT_MS` | `1000` | 前端 socket 的发送等待上限，正整数毫秒 |

```bash
MBGW_LISTEN_PORT=1606 MBGW_QUEUE_CAPACITY=1 MBGW_QUEUE_TIMEOUT_MS=100 \
  ./build-cmake/gateway_shared_backend_demo /tmp/modbus-rtu-client 200
```

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

## 复现 RTU 超时和下一条请求（单元 19）

沿用上面的 socat 串口对。在项目目录运行 `make modbus_rtu_server gateway_shared_backend_demo`。每轮重新启动一次从站和网关，从站寄存器恢复为 123、456、789。

| 实验 | 终端 2 的从站命令 | 终端 4 的 `UNIT` |
|---|---|---|
| 设备地址不存在 | `./build/modbus_rtu_server /tmp/modbus-rtu-server` | `UNIT=2` |
| 设备收到请求但沉默 | `./build/modbus_rtu_server /tmp/modbus-rtu-server 2 --drop-first` | `UNIT=1` |

第一轮从站只处理 B 的一条有效请求。第二轮从站的首条有效请求计入请求数，并打印跳过回复；第二条照常回复。两轮的终端 3 都运行 `./build/gateway_shared_backend_demo /tmp/modbus-rtu-client`。终端 4 先按表设置 `UNIT`，再复制下面整段：

```bash
UNIT="$UNIT" python3 - <<'PY'
import os
import socket
import struct
import time

unit = int(os.environ["UNIT"])
with socket.create_connection(("127.0.0.1", 1506), timeout=3) as a, \
     socket.create_connection(("127.0.0.1", 1506), timeout=3) as b, \
     a.makefile("rb") as reply_a, b.makefile("rb") as reply_b:
    started = time.monotonic()
    a.sendall(bytes.fromhex(f"00 01 00 00 00 06 {unit:02x} 03 00 00 00 03"))
    exception = reply_a.read(9)
    elapsed = (time.monotonic() - started) * 1000
    assert exception == bytes.fromhex(f"00 01 00 00 00 03 {unit:02x} 83 0b"), exception.hex(" ")
    print(f"A：设备 {unit}，异常码 0x{exception[-1]:02X}，约 {elapsed:.0f} ms")

    b.sendall(bytes.fromhex("00 02 00 00 00 06 01 03 00 00 00 03"))
    reply = reply_b.read(15)
    assert reply == bytes.fromhex("00 02 00 00 00 09 01 03 06 00 7b 01 c8 03 15"), reply.hex(" ")
    print("B 读回", struct.unpack(">3H", reply[9:]))
PY
```

两轮都应看到 A 收到网关异常 `0x0B`（目标设备未响应），网关打印 `Connection timed out`，随后 B 读到 `(123, 456, 789)`。只有第二轮的从站打印“收到 8 字节 RTU 请求，按实验设置跳过回复”。A 的计时是 TCP 客户端观察到的总耗时，包含网关调度和串口等待。省略超时参数时，网关使用 libmodbus 的默认 500 ms 后端响应超时；模拟从站的 100 ms 用于忽略其他设备地址后等待并丢弃可能出现的回复。

## 配置 RTU 响应超时（单元 21）

在串口路径后追加正整数毫秒。用单元 19 的设备沉默场景复测：终端 2 使用 `./build/modbus_rtu_server /tmp/modbus-rtu-server 2 --drop-first`，终端 4 使用 `UNIT=1` 和同一段 Python。每轮重新启动从站和网关。

```bash
make gateway_shared_backend_demo
./build/gateway_shared_backend_demo /tmp/modbus-rtu-client 200
```

把网关命令和省略 `200` 的一轮对比。A 仍收到 `0x0B`，客户端总耗时从约 500 ms 缩短到约 200 ms，B 仍读回原值。把 `200` 换成 `0` 或 `200ms` 会在启动时报告参数错误，进程返回 1。

## 看懂网关日志（单元 22）

每条转发日志包含前端连接的文件描述符、设备地址、功能码和两段耗时。排队从连接线程入队算到工作线程出队。转发从调用 `modbus_proxy()` 算到返回，包含后端收发与前端回复。耗时以整数毫秒显示，`-1` 表示取时失败。

本轮结束后汇总转发完成数、失败数，以及失败中报告超时的次数。从站返回的 Modbus 异常若成功转发，也计为完成。

## 队满与排队过期回归（单元 23）

腾出 TCP 1502 和 1506 后运行。脚本临时构建三连接版网关，把等待槽位设为 1，并启动可控 TCP 后端。A 占用工作线程时，B 和 C 争用同一槽位，只有一条能入队，另一条立即收到忙异常 `0x06`。释放 A 的回复后，已入队请求正常完成。

脚本还会用四连接版本和运行时参数 `MBGW_QUEUE_TIMEOUT_MS=100` 验证排队期限。工作线程出队时检查任务已等待的毫秒数，达到期限就直接回复忙异常 `0x06`，跳过后端转发。回归验证一条过期写请求之后，后端收到的下一帧是新的合法读请求。默认排队期限为 `0`，表示关闭检查。开启期限后，异常回复仍在任务出队时发送。

```bash
python3 -B tests/gateway_capacity_demo.py
```

## 正常退出回归（单元 24）

网关收到 `Ctrl+C`（`SIGINT`）或 `SIGTERM` 后停止接入，取消仍在队列里的任务，并唤醒等待的客户端线程。已经取出的后端任务沿用原有 I/O 等待，回收完成后进程退出。腾出 TCP 1502 和 1506 后运行：

```bash
make gateway_shared_backend_demo
python3 -B tests/gateway_shutdown_demo.py
```

脚本验证空闲退出、前端已连接时退出、后端在途时取消排队写入，以及退出后重新绑定监听端口。

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
- [工程验收、性能数据和 3～5 分钟讲解](docs/工程验收与复盘.md)

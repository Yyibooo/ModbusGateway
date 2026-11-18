# ModbusGateway

ModbusGateway 是一个运行在 Linux 上的 C 网关。多个 Modbus TCP 客户端通过有界队列共享一个 Modbus TCP 或 RTU 后端，由一个工作线程顺序转发请求。

默认程序每次启动接入两条 TCP 连接，每条连接最多处理一条请求。两条连接结束后，进程退出。TCP 后端断开后需要重启网关。RTU 联调使用虚拟串口，实际 RS-485 环境需要单独验证。

## 项目目录

| 路径 | 用途 |
|---|---|
| [src/](src/) | 多线程网关主程序 |
| [examples/](examples/) | TCP 和 RTU 模拟设备、客户端、单客户端代理及 Socket 和线程示例 |
| [tests/](tests/) | 自动回归和性能基准 |
| [docs/](docs/) | [网关架构](docs/architecture.md)和[验证指南](docs/verification.md) |
| [CMakeLists.txt](CMakeLists.txt) / [Makefile](Makefile) | 构建入口，CMake 同时注册 CTest 用例 |

## 构建与测试

需要 Linux（含 WSL 2）、支持 C11 的编译器、CMake ≥ 3.20、`pkg-config` 和 libmodbus ≥ 3.2.0。回归测试另需 Python 3 和 `socat`。

```bash
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-cmake --parallel
ctest --test-dir build-cmake --output-on-failure
```

若 `pkg-config` 找不到 libmodbus，可在配置时追加 `-DMODBUS_PKGCONFIG_DIR=/path/to/pkgconfig`。该目录应包含 `libmodbus.pc`。只构建程序时可追加 `-DBUILD_TESTING=OFF`。

也可以通过 GNU Make 构建指定目标，产物位于 `build/`。

```bash
make gateway_shared_backend_demo modbus_server modbus_rtu_server
```

使用自定义库路径时，给 `make` 传入 `MODBUS_PKGCONFIG_DIR=/path/to/pkgconfig`。

## 运行

使用 TCP 后端时，先在一个终端执行 `./build-cmake/modbus_server` 启动模拟从站，再在另一个终端执行 `./build-cmake/gateway_shared_backend_demo`。网关默认监听 `127.0.0.1:1506`，连接 `127.0.0.1:1502`。

使用 RTU 后端时，传入串口设备路径，可选第二个参数设置后端响应超时，单位为毫秒。

```bash
./build-cmake/gateway_shared_backend_demo /dev/ttyUSB0 200
```

通过 Make 构建时，将运行路径中的 `build-cmake/` 换为 `build/`。完整的 TCP 读写和虚拟串口联调步骤见[验证指南](docs/verification.md)。

## 运行配置

网关在启动时检查配置。RTU 命令行的超时参数优先于 `MBGW_RESPONSE_TIMEOUT_MS`。

| 环境变量 | 默认值 | 说明 |
|---|---|---|
| `MBGW_LISTEN_ADDRESS` | `127.0.0.1` | TCP 前端监听地址，IPv4 |
| `MBGW_LISTEN_PORT` | `1506` | TCP 前端监听端口，1～65535 |
| `MBGW_BACKEND_ADDRESS` | `127.0.0.1` | TCP 后端地址，IPv4 |
| `MBGW_BACKEND_PORT` | `1502` | TCP 后端端口，1～65535 |
| `MBGW_RTU_BAUD` | `9600` | 串口波特率，1～4000000，受驱动与设备支持范围限制 |
| `MBGW_RTU_DATA_BITS` | `8` | 数据位，5～8 |
| `MBGW_RTU_PARITY` | `N` | 校验位，`N`、`E` 或 `O` |
| `MBGW_RTU_STOP_BITS` | `1` | 停止位，1～2 |
| `MBGW_RESPONSE_TIMEOUT_MS` | `500`（libmodbus 默认） | 后端响应超时，正整数毫秒 |
| `MBGW_QUEUE_CAPACITY` | `2` | 等待槽位，1～256，后端取出任务后立即释放槽位 |
| `MBGW_QUEUE_TIMEOUT_MS` | `0` | 出队时检查的等待期限，0 表示关闭 |
| `MBGW_SEND_TIMEOUT_MS` | `1000` | TCP 前端套接字发送超时，正整数毫秒 |

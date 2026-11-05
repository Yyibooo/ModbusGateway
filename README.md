# ModbusGateway

这是一个逐步完成的 Linux Modbus TCP–RTU 网关学习项目。
当前的 TCP 实验支持客户端发送 hello 并收到 OK。Modbus TCP 模拟设备已实现监听。

## 当前环境

- WSL Ubuntu，项目目录为 `/home/craig/ModbusGateway`。
- 使用 GCC、GNU Make、Git。
- Modbus 目标使用 libmodbus 3.2.0 或更新版本。Makefile 默认查找 `~/.local/libmodbus-3.2.0/lib/pkgconfig`。如果库装在别处，可通过 `MODBUS_PKGCONFIG_DIR` 指定相应目录。

## 编译与运行

```bash
cd /home/craig/ModbusGateway
make
./build/echo_server
```

在第二个 WSL 终端从项目目录运行 `./build/echo_client`。预期客户端收到 OK，服务端打印 hello，随后检测到客户端断开并退出。

编译和运行 Modbus TCP 模拟设备：

```bash
make modbus_server
./build/modbus_server
```

运行 `make check-modbus` 会编译并运行库版本检查程序。

## 当前 Makefile

- `make` 默认构建 echo 服务端和客户端。可用 `make echo_server` 或 `make echo_client` 单独构建。
- `BUILD_DIR` 指定产物目录。可执行文件统一输出到 `build/`，该目录由 Git 忽略。
- Makefile 在构建时创建 `build/`。已有产物且对应源码没有变化时，重复执行 make 不会重编译。
- `make clean` 删除 Makefile 生成的可执行文件，保留源码。

## 每次修改的 Git 练习

```bash
git status
git diff
make
```

验证当前功能后，按实际修改的文件选择暂存，例如修改了 echo_server.c：

```bash
git add echo_server.c
git diff --cached
git commit -m "说明本次完成的小功能"
git log --oneline -5
```

`git add` 把选中的修改放进暂存区，`git commit` 将暂存区保存为一个本地版本。
学习区分工作区、暂存区和提交记录；后续练习查看历史、创建分支与 git revert。
生成的程序、对象文件和构建目录由 .gitignore 排除。

## 学习材料

- [学习计划](docs/学习计划.md)
- [学习进度](docs/学习进度.md)

计划和进度与源代码一起纳入 Git；之后以项目 docs 目录中的内容为准。

# ModbusGateway

这是一个逐步完成的 Linux Modbus TCP–RTU 网关学习项目。
当前版本是单客户端 TCP 连通实验：接收一段文本、回复两个字节 OK，然后退出。

## 当前环境

- WSL Ubuntu，项目目录：`/home/craig/ModbusGateway`。
- 使用 GCC、GNU Make、Git。
- 当前 TCP 实验不需要 libmodbus，协议通信阶段再准备依赖。

## 编译与运行

```bash
cd /home/craig/ModbusGateway
make
./echo_server
```

`make` 根据 Makefile 编译；源文件没有变化时不会重复编译。
`make clean` 删除生成的 echo_server 程序，保留源代码。

在第二个 WSL 终端中，可使用以下临时客户端验证：

```bash
python3 -c 'import socket; s=socket.create_connection(("127.0.0.1",1502)); s.sendall(b"hello"); print(s.makefile("rb").read(2)); s.close()'
```

期望客户端收到 `b'OK'`，服务端打印接收内容并退出。

## 当前 Makefile

- `CC`：使用的编译器。
- `CFLAGS`：编译选项，开启常见警告并生成调试信息。
- `TARGET`：生成的程序名。
- `all`：默认构建目标。
- `echo_server: echo_server.c`：说明程序依赖这个源文件。
- 规则下方的命令必须以 Tab 开头。

目前先用单文件 Makefile；增加客户端时学习多个目标，拆分模块时学习 .o 和头文件依赖。
后续再用 CMake 构建同一个项目，比较两种工具的职责和用法。

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

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>      // close()
#include <modbus.h>

int main(void)
{
    int status = EXIT_FAILURE;   // 默认失败，只有全部成功才改成 SUCCESS
    int listen_fd = -1;          // 监听 socket，-1 表示尚未创建
    int client_fd = -1;          // 客户端连接 fd，-1 表示尚未 accept
    int backend_rc = -1;         // backend 连接结果，0 表示成功
    modbus_t *backend = NULL;    // 后端（模拟设备）上下文

    // 前端：作为 Modbus 服务器，监听 1503，等待外部客户端连进来
    modbus_t *frontend = modbus_new_tcp("127.0.0.1", 1503);
    if (frontend == NULL)
    {
        perror("modbus_new_tcp frontend");
        return EXIT_FAILURE;     // 此时还没别的资源，直接返回
    }

    // 后端：作为 Modbus 客户端，去连真实/模拟设备 1502
    backend = modbus_new_tcp("127.0.0.1", 1502);
    if (backend == NULL)
    {
        perror("modbus_new_tcp backend");
        goto cleanup;            // 统一走 cleanup，释放 frontend
    }

    // 让 frontend 进入监听状态，返回监听 fd
    // 参数 1 = backlog，最多排队 1 个未处理的连接
    listen_fd = modbus_tcp_listen(frontend, 1);
    if (listen_fd == -1)
    {
        perror("modbus_tcp_listen");
        goto cleanup;
    }

    // 主动连接后端设备（TCP 三次握手）
    backend_rc = modbus_connect(backend);
    if (backend_rc == -1)
    {
        fprintf(stderr, "连接模拟设备失败：%s\n", modbus_strerror(errno));
        goto cleanup;
    }

    printf("代理监听 1503，已连接模拟设备 1502\n");

    // 阻塞等待一个客户端接入；成功后 frontend 内部 socket 切换为该客户端
    client_fd = modbus_tcp_accept(frontend, &listen_fd);
    if (client_fd == -1)
    {
        perror("modbus_tcp_accept");
        goto cleanup;
    }

    uint8_t request[MODBUS_TCP_MAX_ADU_LENGTH];  // 接收原始请求的缓冲区
    int handled = 0;                             // 已成功转发的次数

    // 只处理 2 次转发就退出（演示用，真实代理应为死循环）
    while (handled < 2)
    {
        // 从客户端收一帧原始请求（不解析，只是字节流）
        int request_len = modbus_receive(frontend, request);
        if (request_len == -1)
        {
            fprintf(stderr, "接收请求失败：%s\n", modbus_strerror(errno));
            goto cleanup;
        }
        if (request_len == 0)
        {
            continue;            // 收到 0 字节，跳过重新收
        }

        // ⚠️ 核心：把请求原样转发给 backend，再把 backend 的回复发回 frontend
        // 注意：modbus_proxy 不是 libmodbus 官方 API，需自行实现（见下方说明）
        int reply_len = modbus_proxy(frontend, backend, request, request_len);
        if (reply_len == -1)
        {
            fprintf(stderr, "转发失败：%s\n", modbus_strerror(errno));
            goto cleanup;
        }

        ++handled;
        printf("第 %d 次转发：请求 %d 字节，回复 %d 字节\n",
               handled, request_len, reply_len);
    }

    status = EXIT_SUCCESS;       // 两次转发都成功，标记成功

cleanup:
    // 只有 accept 成功过，frontend 内部才有"客户端连接"需要关闭
    if (client_fd != -1)
    {
        modbus_close(frontend);
    }
    // 只有 backend 连接成功过才需要 close
    if (backend_rc == 0)
    {
        modbus_close(backend);
    }
    // 监听 fd 是独立于 frontend 的，必须单独 close
    if (listen_fd != -1)
    {
        close(listen_fd);
    }
    if (backend != NULL)
    {
        modbus_free(backend);
    }
    modbus_free(frontend);       // frontend 一定非 NULL，直接释放
    return status;
}
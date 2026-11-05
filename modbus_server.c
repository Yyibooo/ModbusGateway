#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <modbus.h>

int main(void)
{
    modbus_t *ctx = modbus_new_tcp("127.0.0.1", 1502);
    if (ctx == NULL)
    {
        perror("modbus_new_tcp");
        return EXIT_FAILURE;
    }

    int listen_fd = modbus_tcp_listen(ctx, 1);
    if (listen_fd == -1)
    {
        perror("modbus_tcp_listen");
        modbus_free(ctx);
        return EXIT_FAILURE;
    }

    printf("Modbus TCP 监听成功：127.0.0.1:1502\n");

    // 创建一个 Modbus 数据映射（服务器端对外暴露的数据区）
    // 参数：nb_bits=0（不分配线圈）、nb_input_bits=0（不分配离散输入）、
    //       nb_registers=3（分配 3 个保持寄存器）、nb_input_registers=0（不分配输入寄存器）
    modbus_mapping_t *mapping = modbus_mapping_new(0, 0, 3, 0);

    // 检查内存分配是否成功；失败时 mapping 为 NULL
    if (mapping == NULL)
    {
        perror("modbus_mapping_new");   // 打印错误原因（注意：libmodbus 错误建议用 modbus_strerror(errno)）
        close(listen_fd);               // 关闭之前已创建的监听 socket，避免 fd 泄漏
        modbus_free(ctx);               // 释放之前创建的 modbus 上下文 ctx
        return EXIT_FAILURE;            // 返回失败状态码，结束程序
    }
    mapping->tab_registers[0]=123;
    mapping->tab_registers[1]=456;    
    mapping->tab_registers[2]=789;

    int client_fd=modbus_tcp_accept(ctx,&listen_fd);
         if (client_fd == -1)
    {
            perror("modbus_tcp_accept");    // 打印 accept 失败原因
            modbus_mapping_free(mapping);   // 释放 mapping 数据区
            close(listen_fd);               // 关闭监听 socket
            modbus_free(ctx);               // 释放 modbus 上下文
            return EXIT_FAILURE;            // 返回失败，结束程序
    }
    // 定义接收缓冲区；MODBUS_TCP_MAX_ADU_LENGTH 是 Modbus TCP 一帧的最大长度
        uint8_t request[MODBUS_TCP_MAX_ADU_LENGTH];
        // 阻塞接收客户端发来的原始请求字节（MBAP 头 + PDU），返回实际收到的字节数
        int request_len = modbus_receive(ctx, request);

        // 默认先把结果设为失败；只有成功应答后才改成 EXIT_SUCCESS
        int status = EXIT_FAILURE;
        // 接收失败（-1），可能是连接断开或出错
        if (request_len == -1)
        {
            perror("modbus_receive");       // 打印接收失败原因
        }
            else if (request_len > 0)
        {
            // 核心：让 libmodbus 自动解析功能码、按地址读写 mapping、打包并发送回复
            int reply_len = modbus_reply(ctx, request, request_len, mapping);

            // 回复失败（-1）
            if (reply_len == -1)
            {
                perror("modbus_reply");      // 打印回复失败原因
            }
            else
            {
                // 打印本次请求/回复的字节数，便于调试
                printf("收到 %d 字节请求，发出 %d 字节回复\n",
                request_len, reply_len);
                status = EXIT_SUCCESS;     // 标记本次处理成功
            }
        }
            

}
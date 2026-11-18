#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <modbus.h>

int main(void)
{
    /* ① 服务端上下文 */
    modbus_t *ctx = modbus_new_tcp("127.0.0.1", 1502);
    if (ctx == NULL) { perror("modbus_new_tcp"); return EXIT_FAILURE; }

    /* ② 监听：返回独立 fd */
    int listen_fd = modbus_tcp_listen(ctx, 1);
    if (listen_fd == -1) { perror("listen"); modbus_free(ctx); return EXIT_FAILURE; }

    /* ③ 设备数据区：0 线圈、0 离散输入、3 保持寄存器、0 输入寄存器 */
    modbus_mapping_t *mapping = modbus_mapping_new(0, 0, 3, 0);
    if (mapping == NULL) {
        perror("mapping"); close(listen_fd); modbus_free(ctx); return EXIT_FAILURE;
    }
    mapping->tab_registers[0] = 123;
    mapping->tab_registers[1] = 456;
    mapping->tab_registers[2] = 789;

    /* ④ 接入一个客户端：连接 fd 存进 ctx->s */
    int client_fd = modbus_tcp_accept(ctx, &listen_fd);
    if (client_fd == -1) {
        perror("accept"); modbus_mapping_free(mapping);
        close(listen_fd); modbus_free(ctx); return EXIT_FAILURE;
    }

    /* ⑤ 收一条、回一条；处理两条后退出 */
    uint8_t request[MODBUS_TCP_MAX_ADU_LENGTH];
    for (int handled = 0; handled < 2; ++handled) {
        int len = modbus_receive(ctx, request);
        if (len <= 0) { fprintf(stderr, "receive: %s\n", modbus_strerror(errno)); break; }

        int sent = modbus_reply(ctx, request, len, mapping);
        if (sent == -1) { fprintf(stderr, "reply: %s\n", modbus_strerror(errno)); break; }
        printf("收到 %d 字节，回复 %d 字节\n", len, sent);
    }

    /* ⑥ 顺序：先关连接，再关监听，再释放数据区/上下文 */
    modbus_close(ctx);              // 关闭 client_fd（ctx->s）
    close(listen_fd);               // 监听 fd 是独立的
    modbus_mapping_free(mapping);
    modbus_free(ctx);
    return 0;
}

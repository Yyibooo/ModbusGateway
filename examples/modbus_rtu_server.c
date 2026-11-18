#include <errno.h>
#include <limits.h>
#include <modbus.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc < 2 || argc > 4)
    {
        fprintf(stderr, "用法：%s <串口设备路径> [请求数 [--drop-first]]\n", argv[0]);
        return EXIT_FAILURE;
    }

    int request_limit = 1;
    if (argc >= 3)
    {
        char *end;
        errno = 0;
        long count = strtol(argv[2], &end, 10);
        if (errno != 0 || end == argv[2] || *end != '\0' || count < 1 || count > INT_MAX)
        {
            fprintf(stderr, "请求数必须是正整数\n");
            return EXIT_FAILURE;
        }
        request_limit = (int)count;
    }

    int drop_first = 0;
    if (argc == 4)
    {
        int comparison = strcmp(argv[3], "--drop-first");
        if (comparison != 0)
        {
            fprintf(stderr, "未知选项：%s\n", argv[3]);
            return EXIT_FAILURE;
        }
        drop_first = 1;
    }

    modbus_t *ctx = modbus_new_rtu(argv[1], 9600, 'N', 8, 1);
    if (ctx == NULL)
    {
        fprintf(stderr, "创建 RTU 上下文失败：%s\n", modbus_strerror(errno));
        return EXIT_FAILURE;
    }

    int rc = modbus_set_slave(ctx, 1);
    if (rc == -1)
    {
        fprintf(stderr, "设置设备地址失败：%s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return EXIT_FAILURE;
    }

    // 忽略发给其他设备的帧后，先于网关结束对该设备回复的等待。
    rc = modbus_set_response_timeout(ctx, 0, 100000);
    if (rc == -1)
    {
        fprintf(stderr, "设置 RTU 回复等待时间失败：%s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return EXIT_FAILURE;
    }

    rc = modbus_connect(ctx);
    if (rc == -1)
    {
        fprintf(stderr, "打开串口 %s 失败：%s\n", argv[1], modbus_strerror(errno));
        modbus_free(ctx);
        return EXIT_FAILURE;
    }

    modbus_mapping_t *mapping = modbus_mapping_new(0, 0, 3, 0);
    if (mapping == NULL)
    {
        fprintf(stderr, "创建寄存器表失败：%s\n", modbus_strerror(errno));
        modbus_close(ctx);
        modbus_free(ctx);
        return EXIT_FAILURE;
    }

    mapping->tab_registers[0] = 123;
    mapping->tab_registers[1] = 456;
    mapping->tab_registers[2] = 789;

    printf("RTU 设备 1 已打开 %s，9600 8N1，等待请求\n", argv[1]);

    uint8_t request[MODBUS_RTU_MAX_ADU_LENGTH];
    int handled = 0;
    int status = EXIT_SUCCESS;

    while (handled < request_limit)
    {
        int request_len = modbus_receive(ctx, request);
        if (request_len == -1)
        {
            fprintf(stderr, "接收 RTU 请求失败：%s\n", modbus_strerror(errno));
            status = EXIT_FAILURE;
            break;
        }
        if (request_len == 0)
        {
            continue;
        }

        if (drop_first && handled == 0)
        {
            ++handled;
            printf("收到 %d 字节 RTU 请求，按实验设置跳过回复\n", request_len);
            continue;
        }

        int reply_len = modbus_reply(ctx, request, request_len, mapping);
        if (reply_len == -1)
        {
            fprintf(stderr, "回复 RTU 请求失败：%s\n", modbus_strerror(errno));
            status = EXIT_FAILURE;
            break;
        }

        ++handled;
        printf("收到 %d 字节 RTU 请求，回复 %d 字节\n", request_len, reply_len);
    }

    modbus_mapping_free(mapping);
    modbus_close(ctx);
    modbus_free(ctx);
    return status;
}

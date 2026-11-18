#include <errno.h>
#include <modbus.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "用法：%s <串口设备路径>\n", argv[0]);
        return EXIT_FAILURE;
    }

    modbus_t *ctx = modbus_new_rtu(argv[1], 9600, 'N', 8, 1);
    if (ctx == NULL)
    {
        fprintf(stderr, "创建 RTU 上下文失败：%s\n", modbus_strerror(errno));
        return EXIT_FAILURE;
    }

    const int device_id = 1;
    int rc = modbus_set_slave(ctx, device_id);
    if (rc == -1)
    {
        fprintf(stderr, "设置目标设备地址失败：%s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return EXIT_FAILURE;
    }

    rc = modbus_set_response_timeout(ctx, 0, 500000);
    if (rc == -1)
    {
        fprintf(stderr, "设置响应超时失败：%s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return EXIT_FAILURE;
    }

    rc = modbus_connect(ctx);
    if (rc == -1)
    {
        fprintf(stderr, "打开串口失败：%s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return EXIT_FAILURE;
    }

    uint16_t values[3];
    int count = modbus_read_registers(ctx, 0, 3, values);
    if (count == -1)
    {
        fprintf(stderr, "读取设备 %d 失败：%s\n", device_id, modbus_strerror(errno));
        modbus_close(ctx);
        modbus_free(ctx);
        return EXIT_FAILURE;
    }

    for (int i = 0; i < count; ++i)
    {
        printf("设备 %d，寄存器[%d] = %u\n", device_id, i, (unsigned int)values[i]);
    }

    modbus_close(ctx);
    modbus_free(ctx);
    return EXIT_SUCCESS;
}
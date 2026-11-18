// 引入 errno 相关定义，用于获取系统调用/库函数失败时的错误码
#include <errno.h>

// 引入 uint16_t / uint8_t 等定长整数类型
#include <stdint.h>

// 引入 printf / fprintf / perror 等标准输入输出函数
#include <stdio.h>

// 引入 EXIT_SUCCESS / EXIT_FAILURE 以及 malloc/free 等
#include <stdlib.h>

// 引入 libmodbus 的公开 API（modbus_t、modbus_new_tcp、modbus_read_registers 等）
#include <modbus.h>

int main(void)
{
    // 创建一个 Modbus TCP 客户端上下文：
    //   第一个参数 "127.0.0.1" 是服务器 IP（本机回环地址）
    //   第二个参数 1503 是网关监听的 TCP 端口
    // 注意：这里只是"准备好连接参数"，还没有真正建立连接
    modbus_t *ctx = modbus_new_tcp("127.0.0.1", 1503);

    // 创建失败（通常是内存不足）时 ctx 为 NULL
    if (ctx == NULL)
    {
        perror("modbus_new_tcp");   // 打印失败原因
        return EXIT_FAILURE;        // 直接退出，没有需要释放的资源
    }

    // 真正向服务器发起 TCP 连接（三次握手）
    // 成功返回 0，失败返回 -1
    int rc = modbus_connect(ctx);

    // 连接失败：比如服务器没启动、端口不对、被防火墙拦截
    if (rc == -1)
    {
        // 用 libmodbus 自己的错误描述打印（比 perror 更准确）
        fprintf(stderr, "连接失败：%s\n", modbus_strerror(errno));
        modbus_free(ctx);           // 释放 ctx（连接没建立，不需要 modbus_close）
        return EXIT_FAILURE;
    }

    int written = modbus_write_register(ctx, 1, 999);
    if (written == -1)
    {
        fprintf(stderr, "写入失败：%s\n", modbus_strerror(errno));
        modbus_close(ctx);
        modbus_free(ctx);
        return EXIT_FAILURE;
    }

    printf("写入成功，准备读回验证\n");

    // 要读取的保持寄存器起始地址（对应服务器端 tab_registers 的下标 0）
    const int start_addr = 0;

    // 用于接收读到的寄存器值；3 个元素对应要读 3 个寄存器
    // 注意：类型必须是 uint16_t，因为 Modbus 寄存器是 16 位的
    uint16_t values[3];

    // 核心调用：读取保持寄存器（功能码 0x03）
    //   ctx        ：已连接的上下文（内部持有 socket）
    //   start_addr ：起始地址 0
    //   3          ：要读的寄存器数量
    //   values     ：输出缓冲区，函数会把读到的值填进来
    // 返回值：成功时 = 实际读到的寄存器个数（这里是 3）；失败返回 -1
    int count = modbus_read_registers(ctx, start_addr, 3, values);

    // 读取失败：可能是服务器返回异常、连接断开、超时等
    if (count == -1)
    {
        fprintf(stderr, "读取失败：%s\n", modbus_strerror(errno));
        modbus_close(ctx);          // 关闭已建立的连接
        modbus_free(ctx);           // 释放上下文
        return EXIT_FAILURE;
    }

    // 遍历打印读到的每个寄存器值
    // 用 count（实际读到的个数）而不是硬编码 3，更健壮
    for (int i = 0; i < count; ++i)
    {
        // start_addr + i 显示的是"逻辑地址"，
        // values[i]       是第 i 个寄存器的值
        // %u 打印无符号整数，强转 unsigned int 避免格式不匹配
        printf("寄存器[%d] = %u\n", start_addr + i, (unsigned int)values[i]);
    }

    // 关闭与服务器的 TCP 连接（释放 socket）
    modbus_close(ctx);

    // 释放 ctx 占用的内存
    modbus_free(ctx);

    // 正常退出
    return EXIT_SUCCESS;
}
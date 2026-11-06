#include <errno.h>
#include <modbus.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

enum { DEMO_PORT = 1505, CLIENT_COUNT = 2 };

static void *serve_client(void *arg)
{
    int client_fd = *(const int *)arg;
    modbus_t *frontend = modbus_new_tcp("127.0.0.1", DEMO_PORT);
    if (frontend == NULL)
    {
        perror("modbus_new_tcp");
        close(client_fd);
        return NULL;
    }

    int rc = modbus_set_socket(frontend, client_fd);
    if (rc == -1)
    {
        fprintf(stderr, "设置连接失败：%s\n", modbus_strerror(errno));
        close(client_fd);
        modbus_free(frontend);
        return NULL;
    }

    modbus_mapping_t *mapping = modbus_mapping_new(0, 0, 3, 0);
    if (mapping == NULL)
    {
        perror("modbus_mapping_new");
        modbus_close(frontend);
        modbus_free(frontend);
        return NULL;
    }

    mapping->tab_registers[0] = 123;
    mapping->tab_registers[1] = 456;
    mapping->tab_registers[2] = 789;

    printf("连接 fd=%d：等待请求\n", client_fd);
    uint8_t request[MODBUS_TCP_MAX_ADU_LENGTH];
    int len = modbus_receive(frontend, request);
    if (len == -1)
    {
        fprintf(stderr, "连接 fd=%d：%s\n", client_fd, modbus_strerror(errno));
    }
    else if (len > 0)
    {
        int sent = modbus_reply(frontend, request, len, mapping);
        if (sent == -1)
        {
            fprintf(stderr, "回复 fd=%d 失败：%s\n", client_fd, modbus_strerror(errno));
        }
        else
        {
            printf("连接 fd=%d：已回复 %d 字节\n", client_fd, sent);
        }
    }

    modbus_mapping_free(mapping);
    modbus_close(frontend);
    modbus_free(frontend);
    return NULL;
}

int main(void)
{
    modbus_t *listener = modbus_new_tcp("127.0.0.1", DEMO_PORT);
    if (listener == NULL)
    {
        perror("modbus_new_tcp");
        return EXIT_FAILURE;
    }

    int listen_fd = modbus_tcp_listen(listener, CLIENT_COUNT);
    if (listen_fd == -1)
    {
        perror("modbus_tcp_listen");
        modbus_free(listener);
        return EXIT_FAILURE;
    }

    printf("前端实验监听 %d\n", DEMO_PORT);

    pthread_t workers[CLIENT_COUNT];
    int client_fds[CLIENT_COUNT];
    int started = 0;
    for (; started < CLIENT_COUNT; ++started)
    {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd == -1)
        {
            perror("accept");
            break;
        }

        client_fds[started] = client_fd;
        int rc = pthread_create(&workers[started], NULL, serve_client, &client_fds[started]);
        if (rc != 0)
        {
            fprintf(stderr, "pthread_create: %s\n", strerror(rc));
            close(client_fd);
            break;
        }
    }

    close(listen_fd);
    modbus_free(listener);

    for (int i = 0; i < started; ++i)
    {
        int rc = pthread_join(workers[i], NULL);
        if (rc != 0)
        {
            fprintf(stderr, "pthread_join: %s\n", strerror(rc));
            return EXIT_FAILURE;
        }
    }

    return started == CLIENT_COUNT ? EXIT_SUCCESS : EXIT_FAILURE;
}
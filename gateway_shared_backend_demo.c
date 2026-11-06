#include <errno.h>
#include <modbus.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef GATEWAY_DEMO_CLIENT_COUNT
#define GATEWAY_DEMO_CLIENT_COUNT 2
#endif

enum { FRONTEND_PORT = 1506, BACKEND_PORT = 1502, CLIENT_COUNT = GATEWAY_DEMO_CLIENT_COUNT };
enum { QUEUE_CAPACITY = 2 };

struct forward_job
{
    modbus_t *frontend;
    uint8_t request[MODBUS_TCP_MAX_ADU_LENGTH];
    int request_len;
    int done;
    pthread_cond_t completed;
};

struct request_queue
{
    struct forward_job *items[QUEUE_CAPACITY];
    int head;
    int tail;
    int count;
    int stopping;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
};

struct backend_args
{
    modbus_t *backend;
    struct request_queue *queue;
};

struct client_args
{
    int fd;
    struct request_queue *queue;
};

static int submit_and_wait(struct request_queue *queue, struct forward_job *job)
{
    pthread_mutex_lock(&queue->mutex);
    if (queue->count == QUEUE_CAPACITY)
    {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    queue->items[queue->tail] = job;
    queue->tail = (queue->tail + 1) % QUEUE_CAPACITY;
    ++queue->count;
    pthread_cond_signal(&queue->not_empty);

    // 转发结束之前，连接线程让工作线程独占这条连接的上下文。
    while (!job->done)
    {
        pthread_cond_wait(&job->completed, &queue->mutex);
    }
    pthread_mutex_unlock(&queue->mutex);
    return 0;
}

static void *backend_worker(void *arg)
{
    struct backend_args *args = arg;
    struct request_queue *queue = args->queue;

    for (;;)
    {
        pthread_mutex_lock(&queue->mutex);
        while (queue->count == 0 && !queue->stopping)
        {
            pthread_cond_wait(&queue->not_empty, &queue->mutex);
        }
        if (queue->count == 0)
        {
            pthread_mutex_unlock(&queue->mutex);
            break;
        }

        struct forward_job *job = queue->items[queue->head];
        queue->head = (queue->head + 1) % QUEUE_CAPACITY;
        --queue->count;
        pthread_mutex_unlock(&queue->mutex);

        int fd = modbus_get_socket(job->frontend);
        int reply_len = modbus_proxy(job->frontend, args->backend,
                                     job->request, job->request_len);
        if (reply_len == -1)
        {
            fprintf(stderr, "后端转发 fd=%d 失败：%s\n", fd, modbus_strerror(errno));
        }
        else
        {
            printf("后端转发 fd=%d：请求 %d 字节，回复 %d 字节\n",
                   fd, job->request_len, reply_len);
        }

        pthread_mutex_lock(&queue->mutex);
        job->done = 1;
        pthread_cond_signal(&job->completed);
        pthread_mutex_unlock(&queue->mutex);
    }

    modbus_close(args->backend);
    modbus_free(args->backend);
    return NULL;
}

static void *serve_client(void *arg)
{
    struct client_args *args = arg;
    modbus_t *frontend = modbus_new_tcp("127.0.0.1", FRONTEND_PORT);
    if (frontend == NULL)
    {
        perror("modbus_new_tcp frontend");
        close(args->fd);
        return NULL;
    }

    int rc = modbus_set_socket(frontend, args->fd);
    if (rc == -1)
    {
        perror("modbus_set_socket");
        close(args->fd);
        modbus_free(frontend);
        return NULL;
    }

    struct forward_job job = {
        .frontend = frontend,
        .completed = PTHREAD_COND_INITIALIZER
    };

    job.request_len = modbus_receive(frontend, job.request);
    if (job.request_len == -1)
    {
        fprintf(stderr, "接收 fd=%d 失败：%s\n", args->fd, modbus_strerror(errno));
    }
    else if (job.request_len > 0)
    {
        rc = submit_and_wait(args->queue, &job);
        if (rc == -1)
        {
            modbus_reply_exception(frontend, job.request, MODBUS_EXCEPTION_SLAVE_OR_SERVER_BUSY);
        }
    }

    pthread_cond_destroy(&job.completed);
    modbus_close(frontend);
    printf("连接 fd=%d：已关闭\n", args->fd);
    modbus_free(frontend);
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc > 2)
    {
        fprintf(stderr, "用法：%s [RTU 串口设备路径]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *serial_path = argc == 2 ? argv[1] : NULL;
    int status = EXIT_FAILURE;
    modbus_t *listener = modbus_new_tcp("127.0.0.1", FRONTEND_PORT);
    if (listener == NULL)
    {
        perror("modbus_new_tcp listener");
        return EXIT_FAILURE;
    }

    int listen_fd = modbus_tcp_listen(listener, CLIENT_COUNT);
    if (listen_fd == -1)
    {
        perror("modbus_tcp_listen");
        modbus_free(listener);
        return EXIT_FAILURE;
    }
    modbus_free(listener);

    modbus_t *backend = serial_path == NULL
        ? modbus_new_tcp("127.0.0.1", BACKEND_PORT)
        : modbus_new_rtu(serial_path, 9600, 'N', 8, 1);
    if (backend == NULL)
    {
        fprintf(stderr, "创建后端上下文失败：%s\n", modbus_strerror(errno));
        close(listen_fd);
        return EXIT_FAILURE;
    }

    int rc = modbus_connect(backend);
    if (rc == -1)
    {
        fprintf(stderr, "连接模拟设备失败：%s\n", modbus_strerror(errno));
        modbus_free(backend);
        close(listen_fd);
        return EXIT_FAILURE;
    }

    struct request_queue queue = {
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .not_empty = PTHREAD_COND_INITIALIZER
    };
    struct backend_args backend_args = { .backend = backend, .queue = &queue };
    pthread_t backend_thread;
    rc = pthread_create(&backend_thread, NULL, backend_worker, &backend_args);
    if (rc != 0)
    {
        fprintf(stderr, "创建后端线程失败：%s\n", strerror(rc));
        modbus_close(backend);
        modbus_free(backend);
        close(listen_fd);
        return EXIT_FAILURE;
    }

    if (serial_path == NULL)
    {
        printf("共享后端实验监听 %d，后端 %d\n", FRONTEND_PORT, BACKEND_PORT);
    }
    else
    {
        printf("共享后端实验监听 %d，RTU 后端 %s，9600 8N1\n",
               FRONTEND_PORT, serial_path);
    }

    pthread_t client_threads[CLIENT_COUNT];
    struct client_args clients[CLIENT_COUNT];
    int started = 0;
    for (; started < CLIENT_COUNT; ++started)
    {
        int fd = accept(listen_fd, NULL, NULL);
        if (fd == -1)
        {
            perror("accept");
            break;
        }

        clients[started] = (struct client_args){ .fd = fd, .queue = &queue };
        rc = pthread_create(&client_threads[started], NULL, serve_client, &clients[started]);
        if (rc != 0)
        {
            fprintf(stderr, "创建连接线程失败：%s\n", strerror(rc));
            close(fd);
            break;
        }
    }

    close(listen_fd);
    for (int i = 0; i < started; ++i)
    {
        rc = pthread_join(client_threads[i], NULL);
        if (rc != 0)
        {
            fprintf(stderr, "等待连接线程失败：%s\n", strerror(rc));
            return EXIT_FAILURE;
        }
    }

    pthread_mutex_lock(&queue.mutex);
    queue.stopping = 1;
    pthread_cond_signal(&queue.not_empty);
    pthread_mutex_unlock(&queue.mutex);

    rc = pthread_join(backend_thread, NULL);
    if (rc != 0)
    {
        fprintf(stderr, "等待后端线程失败：%s\n", strerror(rc));
        return EXIT_FAILURE;
    }

    pthread_cond_destroy(&queue.not_empty);
    pthread_mutex_destroy(&queue.mutex);
    if (started == CLIENT_COUNT)
    {
        status = EXIT_SUCCESS;
    }
    return status;
}

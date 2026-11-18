#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <modbus.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifndef GATEWAY_DEMO_CLIENT_COUNT
#define GATEWAY_DEMO_CLIENT_COUNT 2
#endif

#ifndef GATEWAY_DEMO_QUEUE_CAPACITY
#define GATEWAY_DEMO_QUEUE_CAPACITY 2
#endif

#ifndef GATEWAY_DEMO_QUEUE_TIMEOUT_MS
#define GATEWAY_DEMO_QUEUE_TIMEOUT_MS 0
#endif

_Static_assert(GATEWAY_DEMO_QUEUE_CAPACITY > 0, "queue capacity must be positive");
_Static_assert(GATEWAY_DEMO_QUEUE_TIMEOUT_MS >= 0, "queue timeout cannot be negative");

enum { CLIENT_COUNT = GATEWAY_DEMO_CLIENT_COUNT };
enum { MAX_QUEUE_CAPACITY = 256 };

_Static_assert(GATEWAY_DEMO_QUEUE_CAPACITY <= MAX_QUEUE_CAPACITY, "queue capacity exceeds maximum");

struct gateway_config
{
    const char *listen_address;
    const char *backend_address;
    const char *serial_path;
    uint32_t listen_port;
    uint32_t backend_port;
    uint32_t baud;
    char parity;
    uint32_t data_bits;
    uint32_t stop_bits;
    uint32_t response_timeout_ms;
    uint32_t queue_capacity;
    uint32_t queue_timeout_ms;
    uint32_t send_timeout_ms;
};
enum { TCP_UNIT_ID_OFFSET = 6, TCP_FUNCTION_OFFSET = 7 };

struct forward_job
{
    modbus_t *frontend;
    uint8_t request[MODBUS_TCP_MAX_ADU_LENGTH];
    int request_len;
    int64_t queued_at_ms;
    int done;
    pthread_cond_t completed;
};

struct request_queue
{
    struct forward_job *items[MAX_QUEUE_CAPACITY];
    int capacity;
    uint32_t timeout_ms;
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
    int completion_fd;
    struct request_queue *queue;
    const struct gateway_config *config;
};

enum submit_result { SUBMIT_DONE, SUBMIT_FULL, SUBMIT_STOPPED };

static int64_t monotonic_ms(void)
{
    struct timespec now;
    int rc = clock_gettime(CLOCK_MONOTONIC, &now);
    if (rc == -1)
    {
        return -1;
    }

    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int64_t elapsed_ms(int64_t start, int64_t end)
{
    if (start < 0 || end < 0)
    {
        return -1;
    }
    return end - start;
}

static enum submit_result submit_and_wait(struct request_queue *queue, struct forward_job *job)
{
    pthread_mutex_lock(&queue->mutex);
    if (queue->stopping)
    {
        pthread_mutex_unlock(&queue->mutex);
        return SUBMIT_STOPPED;
    }
    if (queue->count == queue->capacity)
    {
        pthread_mutex_unlock(&queue->mutex);
        return SUBMIT_FULL;
    }

    job->queued_at_ms = monotonic_ms();
    queue->items[queue->tail] = job;
    queue->tail = (queue->tail + 1) % queue->capacity;
    ++queue->count;
    pthread_cond_signal(&queue->not_empty);

    // 转发结束之前，连接线程让工作线程独占这条连接的上下文。
    while (!job->done)
    {
        pthread_cond_wait(&job->completed, &queue->mutex);
    }
    pthread_mutex_unlock(&queue->mutex);
    return SUBMIT_DONE;
}

static void *backend_worker(void *arg)
{
    struct backend_args *args = arg;
    struct request_queue *queue = args->queue;
    unsigned int forwarded = 0;
    unsigned int failed = 0;
    unsigned int timed_out = 0;
    unsigned int queue_rejected = 0;

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
        queue->head = (queue->head + 1) % queue->capacity;
        --queue->count;
        int64_t dequeued_at_ms = monotonic_ms();
        pthread_mutex_unlock(&queue->mutex);

        int fd = modbus_get_socket(job->frontend);
        unsigned int unit_id = job->request[TCP_UNIT_ID_OFFSET];
        unsigned int function = job->request[TCP_FUNCTION_OFFSET];
        int64_t queued_ms = elapsed_ms(job->queued_at_ms, dequeued_at_ms);
        if (queue->timeout_ms > 0 && (queued_ms < 0 || queued_ms >= queue->timeout_ms))
        {
            ++queue_rejected;
            const char *reason = queued_ms < 0 ? "排队计时失败" : "排队过期";
            int reply_len = modbus_reply_exception(job->frontend, job->request,
                                                   MODBUS_EXCEPTION_SLAVE_OR_SERVER_BUSY);
            if (reply_len == -1)
            {
                fprintf(stderr, "%s fd=%d 设备=%u 功能=0x%02X 排队=%" PRId64
                        " ms，忙异常发送失败：%s\n",
                        reason, fd, unit_id, function, queued_ms, modbus_strerror(errno));
            }
            else
            {
                printf("%s fd=%d 设备=%u 功能=0x%02X 排队=%" PRId64
                       " ms，已回复忙异常 0x06\n",
                       reason, fd, unit_id, function, queued_ms);
            }
        }
        else
        {
            int64_t forwarding_at_ms = monotonic_ms();
            int reply_len = modbus_proxy(job->frontend, args->backend,
                                         job->request, job->request_len);
            int proxy_error = errno;
            int64_t finished_at_ms = monotonic_ms();

            int64_t forwarding_ms = elapsed_ms(forwarding_at_ms, finished_at_ms);
            if (reply_len == -1)
            {
                ++failed;
                if (proxy_error == ETIMEDOUT)
                {
                    ++timed_out;
                }
                fprintf(stderr, "后端转发 fd=%d 设备=%u 功能=0x%02X 排队=%" PRId64
                        " ms 转发=%" PRId64 " ms 失败：%s\n",
                        fd, unit_id, function, queued_ms, forwarding_ms,
                        modbus_strerror(proxy_error));
            }
            else
            {
                ++forwarded;
                printf("后端转发 fd=%d 设备=%u 功能=0x%02X 排队=%" PRId64
                       " ms 转发=%" PRId64 " ms：请求 %d 字节，回复 %d 字节\n",
                       fd, unit_id, function, queued_ms, forwarding_ms,
                       job->request_len, reply_len);
            }
        }

        pthread_mutex_lock(&queue->mutex);
        job->done = 1;
        pthread_cond_signal(&job->completed);
        pthread_mutex_unlock(&queue->mutex);
    }

    printf("本轮转发统计：完成=%u 失败=%u 其中超时=%u 排队拒绝=%u\n",
           forwarded, failed, timed_out, queue_rejected);
    modbus_close(args->backend);
    modbus_free(args->backend);
    return NULL;
}

static void serve_client(struct client_args *args, int fd)
{
    const struct gateway_config *config = args->config;
    modbus_t *frontend = modbus_new_tcp(config->listen_address, config->listen_port);
    if (frontend == NULL)
    {
        perror("modbus_new_tcp frontend");
        return;
    }

    int rc = modbus_set_socket(frontend, fd);
    if (rc == -1)
    {
        perror("modbus_set_socket");
        modbus_free(frontend);
        return;
    }

    struct forward_job job = {
        .frontend = frontend,
        .completed = PTHREAD_COND_INITIALIZER
    };

    job.request_len = modbus_receive(frontend, job.request);
    if (job.request_len == -1)
    {
        int receive_error = errno;
        pthread_mutex_lock(&args->queue->mutex);
        int stopping = args->queue->stopping;
        pthread_mutex_unlock(&args->queue->mutex);
        if (!stopping)
        {
            fprintf(stderr, "接收 fd=%d 失败：%s\n", fd, modbus_strerror(receive_error));
        }
    }
    else if (job.request_len > 0)
    {
        enum submit_result result = submit_and_wait(args->queue, &job);
        if (result == SUBMIT_FULL)
        {
            int reply_len = modbus_reply_exception(frontend, job.request,
                                                   MODBUS_EXCEPTION_SLAVE_OR_SERVER_BUSY);
            if (reply_len == -1)
            {
                fprintf(stderr, "队列已满 fd=%d，忙异常发送失败：%s\n",
                        fd, modbus_strerror(errno));
            }
            else
            {
                printf("队列已满 fd=%d，已回复忙异常 0x06\n", fd);
            }
        }
    }

    pthread_cond_destroy(&job.completed);
    modbus_free(frontend);
}

static void *client_worker(void *arg)
{
    struct client_args *args = arg;
    int fd = args->fd;
    serve_client(args, fd);

    // 退出线程也会访问 fd；关闭和标记失效必须一起做。
    pthread_mutex_lock(&args->queue->mutex);
    close(fd);
    args->fd = -1;
    pthread_mutex_unlock(&args->queue->mutex);

    printf("连接 fd=%d：已关闭\n", fd);
    int rc = eventfd_write(args->completion_fd, 1);
    if (rc == -1)
    {
        perror("通知连接结束");
    }
    return NULL;
}

static int stop_gateway(struct request_queue *queue, struct client_args *clients, int started)
{
    pthread_mutex_lock(&queue->mutex);
    queue->stopping = 1;
    int cancelled = queue->count;
    while (queue->count > 0)
    {
        struct forward_job *job = queue->items[queue->head];
        queue->head = (queue->head + 1) % queue->capacity;
        --queue->count;
        job->done = 1;
        pthread_cond_signal(&job->completed);
    }
    for (int i = 0; i < started; ++i)
    {
        if (clients[i].fd >= 0)
        {
            int rc = shutdown(clients[i].fd, SHUT_RDWR);
            if (rc == -1 && errno != ENOTCONN)
            {
                perror("关闭前端收发");
            }
        }
    }
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
    return cancelled;
}

static int run_gateway(int listen_fd, modbus_t *backend, const struct gateway_config *config)
{
    int status = EXIT_FAILURE;
    sigset_t stop_signals;
    sigemptyset(&stop_signals);
    sigaddset(&stop_signals, SIGINT);
    sigaddset(&stop_signals, SIGTERM);
    int rc = pthread_sigmask(SIG_BLOCK, &stop_signals, NULL);
    if (rc != 0)
    {
        fprintf(stderr, "屏蔽退出信号失败：%s\n", strerror(rc));
        modbus_close(backend);
        modbus_free(backend);
        close(listen_fd);
        return status;
    }

    int signal_fd = signalfd(-1, &stop_signals, SFD_CLOEXEC);
    if (signal_fd == -1)
    {
        perror("signalfd");
        modbus_close(backend);
        modbus_free(backend);
        close(listen_fd);
        return status;
    }
    int completion_fd = eventfd(0, EFD_CLOEXEC);
    if (completion_fd == -1)
    {
        perror("eventfd");
        close(signal_fd);
        modbus_close(backend);
        modbus_free(backend);
        close(listen_fd);
        return status;
    }

    struct request_queue queue = {
        .capacity = config->queue_capacity,
        .timeout_ms = config->queue_timeout_ms,
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
        goto cleanup;
    }

    printf("共享后端实验监听 %u，地址 %s，队列容量 %u\n",
           config->listen_port, config->listen_address, config->queue_capacity);
    if (config->serial_path == NULL)
    {
        printf("TCP 后端 %s:%u\n", config->backend_address, config->backend_port);
    }
    else
    {
        printf("RTU 后端 %s，%u %u%c%u\n", config->serial_path, config->baud,
               config->data_bits, config->parity, config->stop_bits);
    }

    struct pollfd events[] = {
        { .fd = signal_fd, .events = POLLIN },
        { .fd = completion_fd, .events = POLLIN },
        { .fd = listen_fd, .events = POLLIN }
    };
    pthread_t client_threads[CLIENT_COUNT];
    struct client_args clients[CLIENT_COUNT];
    int started = 0;
    int finished = 0;
    while (finished < CLIENT_COUNT)
    {
        int ready = poll(events, sizeof(events) / sizeof(events[0]), -1);
        if (ready == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            perror("poll");
            break;
        }
        if ((events[0].revents | events[1].revents | events[2].revents) &
            (POLLERR | POLLHUP | POLLNVAL))
        {
            fprintf(stderr, "监听或通知描述符异常\n");
            break;
        }
        if (events[0].revents & POLLIN)
        {
            struct signalfd_siginfo signal_info;
            ssize_t size = read(signal_fd, &signal_info, sizeof(signal_info));
            if (size != sizeof(signal_info))
            {
                fprintf(stderr, "读取退出信号失败\n");
            }
            else
            {
                printf("收到退出信号 %u\n", signal_info.ssi_signo);
                status = EXIT_SUCCESS;
            }
            break;
        }
        if (events[1].revents & POLLIN)
        {
            eventfd_t completed;
            rc = eventfd_read(completion_fd, &completed);
            if (rc == -1)
            {
                perror("读取连接完成事件");
                break;
            }
            finished += (int)completed;
        }
        if (events[2].revents & POLLIN)
        {
            int fd = accept(listen_fd, NULL, NULL);
            if (fd == -1)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                {
                    continue;
                }
                perror("accept");
                break;
            }
            struct timeval send_timeout = {
                .tv_sec = config->send_timeout_ms / 1000,
                .tv_usec = (config->send_timeout_ms % 1000) * 1000
            };
            rc = setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
            if (rc == -1)
            {
                perror("设置 TCP 回复发送超时");
                close(fd);
                break;
            }
            clients[started] = (struct client_args){ .fd = fd, .completion_fd = completion_fd,
                                                    .queue = &queue, .config = config };
            rc = pthread_create(&client_threads[started], NULL, client_worker, &clients[started]);
            if (rc != 0)
            {
                fprintf(stderr, "创建连接线程失败：%s\n", strerror(rc));
                close(fd);
                break;
            }
            ++started;
            printf("连接 fd=%d：已接入\n", fd);
            if (started == CLIENT_COUNT)
            {
                close(listen_fd);
                listen_fd = -1;
                events[2].fd = -1;
            }
        }
    }
    if (finished == CLIENT_COUNT)
    {
        status = EXIT_SUCCESS;
    }

    if (listen_fd >= 0)
    {
        close(listen_fd);
        listen_fd = -1;
    }
    int cancelled = stop_gateway(&queue, clients, started);
    printf("停止接入，取消排队请求 %d 条\n", cancelled);
    for (int i = 0; i < started; ++i)
    {
        rc = pthread_join(client_threads[i], NULL);
        if (rc != 0)
        {
            fprintf(stderr, "等待连接线程失败：%s\n", strerror(rc));
            return EXIT_FAILURE;
        }
    }
    rc = pthread_join(backend_thread, NULL);
    if (rc != 0)
    {
        fprintf(stderr, "等待后端线程失败：%s\n", strerror(rc));
        return EXIT_FAILURE;
    }

cleanup:
    pthread_cond_destroy(&queue.not_empty);
    pthread_mutex_destroy(&queue.mutex);
    close(completion_fd);
    close(signal_fd);
    if (listen_fd >= 0)
    {
        close(listen_fd);
    }
    return status;
}

static int read_uint(const char *name, const char *value, uint32_t min, uint32_t max,
                     uint32_t *out)
{
    if (value == NULL)
    {
        return 0;
    }

    char *end;
    errno = 0;
    unsigned long parsed = strtoul(value, &end, 10);
    if (value[0] < '0' || value[0] > '9' || *end != '\0' ||
        errno == ERANGE || parsed < min || parsed > max)
    {
        fprintf(stderr, "%s 必须是 %u 到 %u 的整数，收到：%s\n", name, min, max, value);
        return -1;
    }
    *out = (uint32_t)parsed;
    return 0;
}

static int read_address(const char *name, const char **out)
{
    const char *value = getenv(name);
    if (value == NULL)
    {
        return 0;
    }
    struct in_addr address;
    int rc = inet_pton(AF_INET, value, &address);
    if (rc != 1)
    {
        fprintf(stderr, "%s 必须是 IPv4 地址，收到：%s\n", name, value);
        return -1;
    }
    *out = value;
    return 0;
}

static int load_config(struct gateway_config *config, const char *timeout_argument)
{
    struct {
        const char *name;
        uint32_t min;
        uint32_t max;
        uint32_t *out;
    } settings[] = {
        { "MBGW_LISTEN_PORT", 1, 65535, &config->listen_port },
        { "MBGW_BACKEND_PORT", 1, 65535, &config->backend_port },
        { "MBGW_RTU_BAUD", 1, 4000000, &config->baud },
        { "MBGW_RTU_DATA_BITS", 5, 8, &config->data_bits },
        { "MBGW_RTU_STOP_BITS", 1, 2, &config->stop_bits },
        { "MBGW_QUEUE_CAPACITY", 1, MAX_QUEUE_CAPACITY, &config->queue_capacity },
        { "MBGW_QUEUE_TIMEOUT_MS", 0, UINT32_MAX, &config->queue_timeout_ms },
        { "MBGW_SEND_TIMEOUT_MS", 1, UINT32_MAX, &config->send_timeout_ms }
    };
    for (size_t i = 0; i < sizeof(settings) / sizeof(settings[0]); ++i)
    {
        const char *value = getenv(settings[i].name);
        int rc = read_uint(settings[i].name, value, settings[i].min, settings[i].max,
                           settings[i].out);
        if (rc == -1)
        {
            return -1;
        }
    }

    int rc = read_address("MBGW_LISTEN_ADDRESS", &config->listen_address);
    if (rc == -1)
    {
        return -1;
    }
    rc = read_address("MBGW_BACKEND_ADDRESS", &config->backend_address);
    if (rc == -1)
    {
        return -1;
    }

    const char *parity = getenv("MBGW_RTU_PARITY");
    if (parity != NULL)
    {
        if ((parity[0] != 'N' && parity[0] != 'E' && parity[0] != 'O') || parity[1] != '\0')
        {
            fprintf(stderr, "MBGW_RTU_PARITY 必须是 N、E 或 O，收到：%s\n", parity);
            return -1;
        }
        config->parity = parity[0];
    }

    const char *name = "RTU 响应超时毫秒";
    const char *value = timeout_argument;
    if (value == NULL)
    {
        name = "MBGW_RESPONSE_TIMEOUT_MS";
        value = getenv(name);
    }
    return read_uint(name, value, 1, UINT32_MAX, &config->response_timeout_ms);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc > 3)
    {
        fprintf(stderr, "用法：%s [RTU 串口设备路径 [响应超时毫秒]]\n", argv[0]);
        return EXIT_FAILURE;
    }

    struct gateway_config config = {
        .listen_address = "127.0.0.1",
        .backend_address = "127.0.0.1",
        .serial_path = argc >= 2 ? argv[1] : NULL,
        .listen_port = 1506,
        .backend_port = 1502,
        .baud = 9600,
        .parity = 'N',
        .data_bits = 8,
        .stop_bits = 1,
        .queue_capacity = GATEWAY_DEMO_QUEUE_CAPACITY,
        .queue_timeout_ms = GATEWAY_DEMO_QUEUE_TIMEOUT_MS,
        .send_timeout_ms = 1000
    };
    int rc = load_config(&config, argc == 3 ? argv[2] : NULL);
    if (rc == -1)
    {
        return EXIT_FAILURE;
    }
    modbus_t *listener = modbus_new_tcp(config.listen_address, config.listen_port);
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

    // poll 返回后连接也可能消失，避免重新阻塞在 accept。
    int flags = fcntl(listen_fd, F_GETFL);
    if (flags == -1)
    {
        perror("读取监听描述符状态");
        close(listen_fd);
        return EXIT_FAILURE;
    }
    int listen_rc = fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);
    if (listen_rc == -1)
    {
        perror("设置非阻塞监听");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    modbus_t *backend = config.serial_path == NULL
        ? modbus_new_tcp(config.backend_address, config.backend_port)
        : modbus_new_rtu(config.serial_path, config.baud, config.parity,
                         config.data_bits, config.stop_bits);
    if (backend == NULL)
    {
        fprintf(stderr, "创建后端上下文失败：%s\n", modbus_strerror(errno));
        close(listen_fd);
        return EXIT_FAILURE;
    }

    if (config.response_timeout_ms > 0)
    {
        uint32_t seconds = config.response_timeout_ms / 1000;
        uint32_t microseconds = (config.response_timeout_ms % 1000) * 1000;
        rc = modbus_set_response_timeout(backend, seconds, microseconds);
        if (rc == -1)
        {
            fprintf(stderr, "设置后端响应超时失败：%s\n", modbus_strerror(errno));
            modbus_free(backend);
            close(listen_fd);
            return EXIT_FAILURE;
        }
    }

    rc = modbus_connect(backend);
    if (rc == -1)
    {
        fprintf(stderr, "连接模拟设备失败：%s\n", modbus_strerror(errno));
        modbus_free(backend);
        close(listen_fd);
        return EXIT_FAILURE;
    }

    return run_gateway(listen_fd, backend, &config);
}

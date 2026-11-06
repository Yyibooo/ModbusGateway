#include <modbus.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { QUEUE_CAPACITY = 2 };

struct queued_request
{
    uint8_t bytes[MODBUS_TCP_MAX_ADU_LENGTH];
    int len;
};

struct request_queue
{
    struct queued_request items[QUEUE_CAPACITY];
    int head;
    int tail;
    int count;
    int stopping;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
};

static int queue_push(struct request_queue *queue, const uint8_t *bytes, int len)
{
    if (len <= 0 || len > MODBUS_TCP_MAX_ADU_LENGTH)
    {
        return -1;
    }

    pthread_mutex_lock(&queue->mutex);
    if (queue->stopping)
    {
        pthread_mutex_unlock(&queue->mutex);
        return -2;
    }
    if (queue->count == QUEUE_CAPACITY)
    {
        pthread_mutex_unlock(&queue->mutex);
        return 0;
    }

    struct queued_request *item = &queue->items[queue->tail];
    memcpy(item->bytes, bytes, (size_t)len);
    item->len = len;
    queue->tail = (queue->tail + 1) % QUEUE_CAPACITY;
    ++queue->count;

    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
    return 1;
}

static int queue_pop(struct request_queue *queue, struct queued_request *out)
{
    pthread_mutex_lock(&queue->mutex);
    while (queue->count == 0 && !queue->stopping)
    {
        puts("后端线程：队列为空，等待任务");
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }

    if (queue->count == 0)
    {
        pthread_mutex_unlock(&queue->mutex);
        return 0;
    }

    *out = queue->items[queue->head];
    queue->head = (queue->head + 1) % QUEUE_CAPACITY;
    --queue->count;

    pthread_mutex_unlock(&queue->mutex);
    return 1;
}

static void queue_stop(struct request_queue *queue)
{
    pthread_mutex_lock(&queue->mutex);
    queue->stopping = 1;
    pthread_cond_broadcast(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
}

static void *backend_worker(void *arg)
{
    struct request_queue *queue = arg;
    struct queued_request item;

    int popped = queue_pop(queue, &item);
    while (popped == 1)
    {
        printf("出队：功能码 0x%02X，长度 %d 字节\n",
               (unsigned int)item.bytes[7], item.len);
        popped = queue_pop(queue, &item);
    }

    return NULL;
}

int main(void)
{
    struct request_queue queue = {
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .not_empty = PTHREAD_COND_INITIALIZER
    };
    uint8_t request[MODBUS_TCP_MAX_ADU_LENGTH];

    const uint8_t write_request[] = {
        0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0xFF, 0x06, 0x00, 0x01, 0x03, 0xE7
    };
    const uint8_t read_request[] = {
        0x00, 0x02, 0x00, 0x00, 0x00, 0x06, 0xFF, 0x03, 0x00, 0x00, 0x00, 0x03
    };

    pthread_t worker;
    int rc = pthread_create(&worker, NULL, backend_worker, &queue);
    if (rc != 0)
    {
        fprintf(stderr, "pthread_create: %s\n", strerror(rc));
        return EXIT_FAILURE;
    }

    puts("按 Enter 模拟收到两条请求，空行 Ctrl+D 直接结束");
    if (getchar() != EOF)
    {
        memcpy(request, write_request, sizeof(write_request));
        int pushed = queue_push(&queue, request, sizeof(write_request));
        printf("第一次入队：%d\n", pushed);

        memcpy(request, read_request, sizeof(read_request));
        pushed = queue_push(&queue, request, sizeof(read_request));
        printf("第二次入队：%d\n", pushed);
    }

    queue_stop(&queue);
    rc = pthread_join(worker, NULL);
    if (rc != 0)
    {
        fprintf(stderr, "pthread_join: %s\n", strerror(rc));
        return EXIT_FAILURE;
    }

    pthread_cond_destroy(&queue.not_empty);
    pthread_mutex_destroy(&queue.mutex);
    puts("后端线程已结束");
    return EXIT_SUCCESS;
}

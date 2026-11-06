#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct task_slot
{
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    int function_code;
    int has_task;
    int stopping;
};

static void submit_task(struct task_slot *slot, int function_code)
{
    pthread_mutex_lock(&slot->mutex);
    while (slot->has_task)
    {
        pthread_cond_wait(&slot->changed, &slot->mutex);
    }

    slot->function_code = function_code;
    slot->has_task = 1;
    pthread_cond_signal(&slot->changed);
    pthread_mutex_unlock(&slot->mutex);
}

static void *backend_worker(void *arg)
{
    struct task_slot *slot = arg;

    for (;;)
    {
        pthread_mutex_lock(&slot->mutex);
        while (!slot->has_task && !slot->stopping)
        {
            puts("后端线程：槽位为空，等待任务");
            pthread_cond_wait(&slot->changed, &slot->mutex);
        }

        if (!slot->has_task && slot->stopping)
        {
            pthread_mutex_unlock(&slot->mutex);
            return NULL;
        }

        int function_code = slot->function_code;
        slot->has_task = 0;
        pthread_cond_signal(&slot->changed);
        pthread_mutex_unlock(&slot->mutex);

        printf("后端线程：处理 0x%02X\n", (unsigned int)function_code);
    }
}

int main(void)
{
    struct task_slot slot = {
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .changed = PTHREAD_COND_INITIALIZER
    };

    pthread_t worker;
    int rc = pthread_create(&worker, NULL, backend_worker, &slot);
    if (rc != 0)
    {
        fprintf(stderr, "pthread_create: %s\n", strerror(rc));
        return EXIT_FAILURE;
    }

    puts("按 Enter 提交一写一读，Ctrl+D 直接退出");
    if (getchar() != EOF)
    {
        submit_task(&slot, 0x06);
        submit_task(&slot, 0x03);
    }

    pthread_mutex_lock(&slot.mutex);
    slot.stopping = 1;
    pthread_cond_signal(&slot.changed);
    pthread_mutex_unlock(&slot.mutex);

    rc = pthread_join(worker, NULL);
    if (rc != 0)
    {
        fprintf(stderr, "pthread_join: %s\n", strerror(rc));
        return EXIT_FAILURE;
    }

    pthread_cond_destroy(&slot.changed);
    pthread_mutex_destroy(&slot.mutex);
    puts("主线程：后端线程已结束");
    return EXIT_SUCCESS;
}
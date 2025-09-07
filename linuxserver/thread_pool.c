#include "thread_pool.h"

/**
 * @brief 初始化线程池（创建线程、初始化锁和信号量）
 * @param 无参数
 * @return 无返回值
 */
void thread_pool_init()
{
    // 初始化互斥锁（保护任务队列）
    pthread_mutex_init(&thread_pool.mutex, NULL);
    // 初始化信号量（控制线程唤醒，初始值0表示无任务）
    sem_init(&thread_pool.semaphore, 0, 0);
    // 初始化任务队列（空队列）
    thread_pool.front = 0;
    thread_pool.rear = 0;
    thread_pool.count = 0;

    // 创建线程池中的工作线程
    for (int i = 0; i < THREAD_POOL_SIZE; i++)
    {
        pthread_create(&thread_pool.threads[i], NULL, thread_function, NULL);
    }
}

/**
 * @brief 向线程池任务队列添加任务
 * @param task 要添加的任务（包含客户端fd、任务类型、客户端地址）
 * @return 无返回值
 */
void thread_pool_add_task(Task task)
{
    pthread_mutex_lock(&thread_pool.mutex); // 加锁保护队列

    // 任务队列未满，添加任务
    if (thread_pool.count < MAX_QUEUE_SIZE)
    {
        thread_pool.queue[thread_pool.rear] = task;
        thread_pool.rear = (thread_pool.rear + 1) % MAX_QUEUE_SIZE; // 循环队列
        thread_pool.count++;
        sem_post(&thread_pool.semaphore); // 信号量+1，唤醒一个等待的线程
    }
    else
    {
        // 队列已满，打印警告日志
        write_log(LOG_LEVEL_WARN, "任务队列已满，无法添加新任务");
    }

    pthread_mutex_unlock(&thread_pool.mutex); // 解锁
}

/**
 * @brief 线程池工作线程函数（循环获取任务并执行）
 * @param arg 线程参数（无实际意义，仅满足pthread_create要求）
 * @return 无返回值（返回NULL）
 */
void *thread_function(void *arg)
{
    while (server_running)
    {                                     // 服务器运行时循环
        sem_wait(&thread_pool.semaphore); // 等待任务（信号量-1，无任务则阻塞）

        if (!server_running)
            break; // 服务器退出，终止线程

        pthread_mutex_lock(&thread_pool.mutex); // 加锁获取任务

        // 从队列头取出任务（循环队列）
        Task task = thread_pool.queue[thread_pool.front];
        thread_pool.front = (thread_pool.front + 1) % MAX_QUEUE_SIZE;
        thread_pool.count--;

        pthread_mutex_unlock(&thread_pool.mutex); // 解锁

        // 记录任务开始时间（统计耗时）
        struct timeval start, end;
        gettimeofday(&start, NULL);

        // 根据任务类型执行对应处理
        switch (task.type)
        {
        case TASK_CLIENT_MESSAGE:
            handle_client_message(task.client_fd, task.client_addr);
            break;
        case TASK_UPLOAD_DATA:
            handle_upload(task.client_fd);
            break;
        case TASK_DOWNLOAD_DATA:
            handle_download(task.client_fd);
            break;
        }

        // 记录任务结束时间，计算耗时（毫秒）
        gettimeofday(&end, NULL);
        long sec_diff = end.tv_sec - start.tv_sec;
        long usec_diff = end.tv_usec - start.tv_usec;
        // 处理跨秒的微秒差（避免负数）
        long elapsed_ms = sec_diff * 1000 + (usec_diff >= 0 ? usec_diff / 1000 : (usec_diff + 1000000) / 1000);
    }

    return NULL;
}
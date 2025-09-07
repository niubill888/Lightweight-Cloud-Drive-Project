#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "cloud_disk.h"

/**
 * @brief 初始化线程池（创建线程、初始化锁和信号量）
 * @param 无参数
 * @return 无返回值
 */
void thread_pool_init();

/**
 * @brief 向线程池任务队列添加任务
 * @param task 要添加的任务（包含客户端fd、任务类型、客户端地址）
 * @return 无返回值
 */
void thread_pool_add_task(Task task);

/**
 * @brief 线程池工作线程函数（循环获取任务并执行）
 * @param arg 线程参数（无实际意义，仅满足pthread_create要求）
 * @return 无返回值（返回NULL）
 */
void *thread_function(void *arg);

#endif // THREAD_POOL_H
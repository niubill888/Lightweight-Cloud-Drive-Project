#include "daemon_signal.h"

/**
 * @brief 将进程转为守护进程（脱离终端、后台运行）
 * @param log_file 日志文件路径（守护进程的输出重定向）
 * @return 无返回值
 */
void daemonize(const char *log_file)
{
    pid_t pid;

    // 第一步：fork子进程，父进程退出（脱离终端）
    if ((pid = fork()) < 0)
    {
        perror("fork失败");
        exit(EXIT_FAILURE);
    }
    if (pid > 0)
    {
        exit(EXIT_SUCCESS);
    }

    // 第二步：设置文件权限掩码（确保创建文件时权限正确）
    umask(0);

    // 第三步：创建新会话（脱离原会话组和进程组）
    if (setsid() < 0)
    {
        perror("setsid失败");
        exit(EXIT_FAILURE);
    }

    // 第四步：再次fork（避免进程成为会话首进程，无法脱离终端）
    if ((pid = fork()) < 0)
    {
        perror("fork失败");
        exit(EXIT_FAILURE);
    }
    if (pid > 0)
    {
        exit(EXIT_SUCCESS);
    }

    // 第五步：关闭所有打开的文件描述符（脱离原终端的标准输入/输出/错误）
    close(STDIN_FILENO);  // 0
    close(STDOUT_FILENO); // 1
    close(STDERR_FILENO); // 2

    // 第六步：重定向标准输入/输出/错误到/dev/null（避免占用终端）
    open("/dev/null", O_RDWR);
    dup(0); // 标准输出重定向到/dev/null
    dup(0); // 标准错误重定向到/dev/null

    write_log(LOG_LEVEL_INFO, "云盘服务器守护进程启动成功");
}

/**
 * @brief 信号处理函数（捕获SIGINT、SIGTERM、SIGPIPE）
 * @param signo 捕获到的信号编号
 * @return 无返回值
 */
void signal_handler(int signo)
{
    switch (signo)
    {
    // 处理Ctrl+C（SIGINT）和kill命令（SIGTERM）：优雅退出
    case SIGINT:
    case SIGTERM:
        write_log(LOG_LEVEL_INFO, "收到终止信号，正在优雅退出...");
        server_running = 0; // 设置服务器退出标志

        // 唤醒所有等待的线程（避免线程阻塞在sem_wait）
        for (int i = 0; i < THREAD_POOL_SIZE; i++)
        {
            sem_post(&thread_pool.semaphore);
        }

        // 关闭文件描述符
        if (epfd != -1)
            close(epfd);
        if (server_fd != -1)
            close(server_fd);

        // 关闭MySQL连接
        mysql_close(&mysql);

        // 等待所有线程退出
        for (int i = 0; i < THREAD_POOL_SIZE; i++)
        {
            pthread_join(thread_pool.threads[i], NULL);
        }

        // 销毁锁和信号量
        pthread_mutex_destroy(&thread_pool.mutex);
        sem_destroy(&thread_pool.semaphore);

        // 记录退出日志，关闭syslog
        write_log(LOG_LEVEL_INFO, "服务器已成功关闭");
        closelog();
        exit(EXIT_SUCCESS);
        break;

    // 处理SIGPIPE（客户端断开连接后继续写数据）
    case SIGPIPE:
        write_log(LOG_LEVEL_WARN, "收到SIGPIPE信号，客户端可能已断开连接");
        break;

    // 其他信号忽略
    default:
        break;
    }
}
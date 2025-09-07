#include <signal.h>
#include "cloud_disk.h"

// ========================== 全局变量定义 ==========================
int server_running = 1;                              // 服务器运行状态标志（1=运行，0=退出）
int server_fd = -1;                                  // 服务器监听socket文件描述符
int epfd = -1;                                       // epoll实例文件描述符
UserCache user_cache[MAX_USERS];                     // 用户信息缓存数组
int user_cache_count = 0;                            // 缓存的用户数量
char server_ip[INET_ADDRSTRLEN] = "192.168.112.10";  // 服务器IP地址
ClientUploadInfo client_up_info[MAX_EVENTS] = {0};   // 客户端上传信息数组（按fd索引）
ClientDownloadInfo client_dl_info[MAX_EVENTS] = {0}; // 客户端下载信息数组（按fd索引）
char client_username[MAX_EVENTS][50] = {0};          // 客户端fd->用户名映射（登录后绑定）
struct sockaddr_in client_addrs[MAX_EVENTS];         // 客户端fd->IP地址映射
ThreadPool thread_pool;                              // 线程池实例

/**
 * @brief 主函数：服务器入口（初始化、epoll事件循环）
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组（-f 表示前台运行，默认后台守护进程）
 * @return 0=正常退出，1=异常退出
 */
int main(int argc, char *argv[])
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1 ||
        sigaction(SIGTERM, &sa, NULL) == -1 ||
        sigaction(SIGPIPE, &sa, NULL) == -1)
    {
        perror("设置信号处理失败");
        exit(EXIT_FAILURE);
    }

    int daemon_mode = 1;
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-f") == 0)
        {
            daemon_mode = 0;
            break;
        }
    }
    if (daemon_mode)
    {
        daemonize(SERVER_ROOT "/server.log");
    }

    // 初始化服务器核心模块
    init_server();      // 初始化服务器根目录
    init_mysql();       // 初始化MySQL连接
    thread_pool_init(); // 初始化线程池

    // 创建服务器监听socket（TCP）
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
    {
        perror("socket失败");
        write_log(LOG_LEVEL_ERROR, "socket创建失败: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // 设置socket选项：允许端口复用（避免重启时端口占用）
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 设置socket为非阻塞模式（配合epoll边缘触发）
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    // 绑定IP和端口
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(server_ip);
    addr.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        perror("bind失败");
        write_log(LOG_LEVEL_ERROR, "bind失败: %s", strerror(errno));
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // 开始监听（backlog=12，最大等待连接数）
    if (listen(server_fd, 12) == -1)
    {
        perror("listen失败");
        write_log(LOG_LEVEL_ERROR, "listen失败: %s", strerror(errno));
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    printf("服务器启动，监听 %s:%d...\n", server_ip, PORT);
    write_log(LOG_LEVEL_INFO, "服务器启动，监听 %s:%d", server_ip, PORT);

    // 创建epoll实例（用于IO多路复用）
    epfd = epoll_create1(0);
    if (epfd == -1)
    {
        perror("epoll_create失败");
        write_log(LOG_LEVEL_ERROR, "epoll_create失败: %s", strerror(errno));
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // 将监听socket添加到epoll（边缘触发+读事件）
    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = server_fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev) == -1)
    {
        perror("epoll_ctl失败");
        write_log(LOG_LEVEL_ERROR, "epoll_ctl失败: %s", strerror(errno));
        close(epfd);
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // epoll事件循环（服务器核心逻辑）
    while (server_running)
    {
        // 等待epoll事件（阻塞，直到有事件发生）
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (nfds == -1)
        {
            // 忽略中断错误（信号导致的暂时返回）
            if (errno == EINTR)
                continue;
            perror("epoll_wait失败");
            write_log(LOG_LEVEL_ERROR, "epoll_wait失败: %s", strerror(errno));
            break;
        }

        // 遍历处理所有发生的事件
        for (int i = 0; i < nfds; i++)
        {
            int fd = events[i].data.fd;
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);

            // 事件1：监听socket有新连接
            if (fd == server_fd)
            {
                // 循环接受所有新连接（边缘触发需一次性处理完）
                while (1)
                {
                    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
                    if (client_fd == -1)
                    {
                        // 没有更多连接（非阻塞accept的正常返回）
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        perror("accept失败");
                        write_log(LOG_LEVEL_ERROR, "accept失败: %s", strerror(errno));
                        break;
                    }

                    // 设置客户端socket为非阻塞模式
                    flags = fcntl(client_fd, F_GETFL, 0);
                    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

                    // 记录客户端地址信息
                    client_addrs[client_fd] = client_addr;
                    printf("新客户端连接：fd=%d, IP=%s\n", client_fd, inet_ntoa(client_addr.sin_addr));
                    write_log(LOG_LEVEL_INFO, "新客户端连接：fd=%d, IP=%s", client_fd, inet_ntoa(client_addr.sin_addr));

                    // 将客户端socket添加到epoll（边缘触发+读事件）
                    ev.events = EPOLLIN | EPOLLET;
                    ev.data.fd = client_fd;
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev) == -1)
                    {
                        perror("epoll_ctl添加客户端失败");
                        write_log(LOG_LEVEL_ERROR, "epoll_ctl添加客户端 %d 失败: %s", client_fd, strerror(errno));
                        close(client_fd);
                    }
                }
            }
            // 事件2：客户端socket有上传数据（处于上传中状态）
            else if (client_up_info[fd].state == UP_STATE_RECEIVING)
            {
                // 添加上传任务到线程池
                Task task;
                task.client_fd = fd;
                task.type = TASK_UPLOAD_DATA;
                task.client_addr = client_addrs[fd];
                thread_pool_add_task(task);
            }
            // 下载推模式：EPOLLOUT事件且处于发送中
            else if ((events[i].events & EPOLLOUT) && client_dl_info[fd].state == DL_STATE_SENDING)
            {
                Task task;
                task.client_fd = fd;
                task.type = TASK_DOWNLOAD_DATA;
                task.client_addr = client_addrs[fd];
                thread_pool_add_task(task);
            }
            // 其他普通消息
            else if (events[i].events & EPOLLIN)
            {
                Task task;
                task.client_fd = fd;
                task.type = TASK_CLIENT_MESSAGE;
                task.client_addr = client_addrs[fd];
                thread_pool_add_task(task);
            }
        }
    }

    // 服务器退出前清理资源
    close(epfd);
    close(server_fd);
    mysql_close(&mysql);

    // 等待所有线程退出
    for (int i = 0; i < THREAD_POOL_SIZE; i++)
    {
        pthread_join(thread_pool.threads[i], NULL);
    }

    // 销毁线程池同步资源
    pthread_mutex_destroy(&thread_pool.mutex);
    sem_destroy(&thread_pool.semaphore);

    write_log(LOG_LEVEL_INFO, "服务器已退出");
    closelog();
    return 0;
}
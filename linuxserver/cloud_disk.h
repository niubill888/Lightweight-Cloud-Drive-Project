#ifndef CLOUD_DISK_H
#define CLOUD_DISK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <errno.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <syslog.h>
#include <semaphore.h>
#include "cJSON.h"
#include <mysql/mysql.h>
#include <time.h>
#include <libgen.h>   // 用于 dirname/basename 函数
#include <stdarg.h>   // 用于日志函数可变参数
#include <sys/time.h> // 用于时间统计

// ========================== 常量定义 ==========================
#define PORT 8000                          // 服务器端口号
#define MAX_EVENTS 1024                    // 最大epoll事件数（最大用户连接数）
#define BUFFER_SIZE 4096                   // 单次数据传输缓冲区大小
#define MAX_USERS 100                      // 最大缓存用户数
#define SERVER_ROOT "/home/tmn/servertest" // 服务器根目录（所有用户目录的父目录）
#define THREAD_POOL_SIZE 8                 // 线程池大小
#define MAX_QUEUE_SIZE 100                 // 请求队列最大长度
#define MAX_PATH_LEN 4096                  // 最大文件路径长度

// ========================== 枚举类型定义 ==========================
/**
 * @brief 上传状态枚举
 */
typedef enum
{
    UP_STATE_IDLE,     // 上传空闲状态
    UP_STATE_RECEIVING // 接收上传数据状态
} UploadState;

/**
 * @brief 下载状态枚举
 */
typedef enum
{
    DL_STATE_IDLE,   // 下载空闲状态
                     // DL_STATE_WAIT_ACK,    // 等待客户端下载确认状态
    DL_STATE_SENDING // 发送下载文件数据状态
} DownloadState;

/**
 * @brief 日志级别枚举（避免与syslog.h冲突）
 */
typedef enum
{
    LOG_LEVEL_INFO,  // 信息级日志
    LOG_LEVEL_ERROR, // 错误级日志
    LOG_LEVEL_WARN   // 警告级日志
} LogLevel;

/**
 * @brief 任务类型枚举（线程池任务分类）
 */
typedef enum
{
    TASK_CLIENT_MESSAGE, // 客户端普通消息任务
    TASK_UPLOAD_DATA,    // 上传数据处理任务
    TASK_DOWNLOAD_DATA   // 下载数据处理任务
} TaskType;

// ========================== 结构体定义 ==========================
/**
 * @brief 客户端上传信息结构体（记录单个客户端的上传状态）
 */
typedef struct
{
    UploadState state;           // 上传状态
    char filepath[MAX_PATH_LEN]; // 上传文件的完整路径
    long long filesize;          // 上传文件总大小
    long long received;          // 已接收文件大小
    int fd;                      // 上传文件的文件描述符
} ClientUploadInfo;

/**
 * @brief 客户端下载信息结构体（记录单个客户端的下载状态）
 */
typedef struct
{
    DownloadState state;             // 下载状态
    char filepath[MAX_PATH_LEN];     // 下载文件的完整路径
    long long filesize;              // 下载文件总大小
    int tar_fd;                      // 文件夹下载时的tar包文件描述符
    long long sent;                  // 已发送文件大小
    long long offset;                // 当前文件读取偏移位置
    long long total_sent;            // 累计发送大小（包括多次发送）
    int fd;                          // 当前下载文件的文件描述符
    char remaining_buf[BUFFER_SIZE]; // 固定缓冲区，存没发完的字节
    size_t remaining_len;            // 缓冲区中未发的字节数（初始0）
} ClientDownloadInfo;

/**
 * @brief 用户缓存结构体（内存中缓存用户信息，减少数据库查询）
 */
typedef struct
{
    char username[50];           // 用户名
    char root_dir[MAX_PATH_LEN]; // 用户根目录路径
} UserCache;

/**
 * @brief 线程池任务结构体（单个任务的信息）
 */
typedef struct
{
    int client_fd;                  // 客户端文件描述符
    TaskType type;                  // 任务类型
    struct sockaddr_in client_addr; // 客户端地址信息
} Task;

/**
 * @brief 线程池结构体（管理线程池的线程、任务队列、同步锁）
 */
typedef struct
{
    pthread_t threads[THREAD_POOL_SIZE]; // 线程池中的线程ID数组
    Task queue[MAX_QUEUE_SIZE];          // 任务队列
    int front;                           // 队列头索引（出队）
    int rear;                            // 队列尾索引（入队）
    int count;                           // 队列中任务数量
    pthread_mutex_t mutex;               // 任务队列互斥锁
    sem_t semaphore;                     // 任务队列信号量（控制线程唤醒）
} ThreadPool;

// ========================== 全局变量extern声明 ==========================
extern int server_running;                            // 服务器运行状态标志（1=运行，0=退出）
extern int server_fd;                                 // 服务器监听socket文件描述符
extern int epfd;                                      // epoll实例文件描述符
extern UserCache user_cache[MAX_USERS];               // 用户信息缓存数组
extern int user_cache_count;                          // 缓存的用户数量
extern char server_ip[INET_ADDRSTRLEN];               // 服务器IP地址
extern ClientUploadInfo client_up_info[MAX_EVENTS];   // 客户端上传信息数组（按fd索引）
extern ClientDownloadInfo client_dl_info[MAX_EVENTS]; // 客户端下载信息数组（按fd索引）
extern MYSQL mysql;                                   // MySQL连接句柄
extern char client_username[MAX_EVENTS][50];          // 客户端fd->用户名映射（登录后绑定）
extern struct sockaddr_in client_addrs[MAX_EVENTS];   // 客户端fd->IP地址映射
extern ThreadPool thread_pool;                        // 线程池实例

// ========================== 函数声明（跨文件调用） ==========================
// 1. 工具函数（utils.c）
void write_log(LogLevel level, const char *format, ...);
int is_safe_path(const char *base_dir, const char *user_path);
int mkdir_recursive(const char *path, mode_t mode);
void build_full_path(char *full_path, const char *base_dir, const char *user_path, const char *filename);
void send_json_response(int client_fd, cJSON *root);
int pack_directory(const char *dir_path, const char *tar_path);

// 2. MySQL工具函数（mysql_utils.c）
void init_mysql();
int check_mysql_connection();
int mysql_find_user(const char *username);
void insert_operation_log(int client_fd, const char *username, const char *ip,
                          const char *operation, const char *filename, const char *status);

// 3. 用户管理函数（user.c）
int get_user_root_dir(const char *username, char *root_dir);
int create_user_root_dir(const char *username);

// 4. 线程池函数（thread_pool.c）
void thread_pool_init();
void thread_pool_add_task(Task task);
void *thread_function(void *arg);

// 5. 守护进程+信号处理函数（daemon_signal.c）
void daemonize(const char *log_file);
void signal_handler(int signo);

// 6. 业务逻辑函数（business.c）
void init_server();
void handle_login(int client_fd, cJSON *req, struct sockaddr_in client_addr);
void handle_register(int client_fd, cJSON *req);
void handle_file_list(int client_fd, cJSON *req);
void handle_upload_ctl(int client_fd, cJSON *req);
void handle_upload_dir(int client_fd, cJSON *req);
int handle_upload(int client_fd);
void handle_download_ctl(int client_fd, cJSON *req);
void handle_download_dir(int client_fd, cJSON *req);
int handle_download(int client_fd);
void handle_delete(int client_fd, cJSON *req, struct sockaddr_in client_addr);
void handle_share(int client_fd, cJSON *req);
void handle_history_query(int client_fd, cJSON *req);
void handle_client_message(int client_fd, struct sockaddr_in client_addr);

#endif // CLOUD_DISK_H
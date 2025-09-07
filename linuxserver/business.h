#ifndef BUSINESS_H
#define BUSINESS_H

#include "cloud_disk.h"
#include <sys/file.h> // 包含 flock 相关宏定义（LOCK_EX、LOCK_NB 等）

/**
 * @brief 初始化服务器（创建服务器根目录）
 * @param 无参数
 * @return 无返回值
 */
void init_server();

/**
 * @brief 处理客户端登录请求
 * @param client_fd 客户端文件描述符
 * @param req 客户端JSON请求（含用户名、密码）
 * @param client_addr 客户端地址信息
 * @return 无返回值
 */
void handle_login(int client_fd, cJSON *req, struct sockaddr_in client_addr);

/**
 * @brief 处理客户端注册请求
 * @param client_fd 客户端文件描述符
 * @param req 客户端JSON请求（含用户名、密码）
 * @return 无返回值
 */
void handle_register(int client_fd, cJSON *req);

/**
 * @brief 处理客户端文件列表请求
 * @param client_fd 客户端文件描述符
 * @param req 客户端JSON请求（含目标路径）
 * @return 无返回值
 */
void handle_file_list(int client_fd, cJSON *req);

/**
 * @brief 处理客户端文件上传控制请求（初始化上传）
 * @param client_fd 客户端文件描述符
 * @param req 客户端JSON请求（含文件名、文件大小、目标路径）
 * @return 无返回值
 */
void handle_upload_ctl(int client_fd, cJSON *req);

/**
 * @brief 处理客户端目录上传请求（创建目标目录）
 * @param client_fd 客户端文件描述符
 * @param req 客户端JSON请求（含目录名、目标路径）
 * @return 无返回值
 */
void handle_upload_dir(int client_fd, cJSON *req);

/**
 * @brief 处理客户端上传数据（接收并写入文件）
 * @param client_fd 客户端文件描述符
 * @return 0=处理成功，-1=处理失败
 */
int handle_upload(int client_fd);

/**
 * @brief 处理客户端文件下载控制请求（初始化下载）
 * @param client_fd 客户端文件描述符
 * @param req 客户端JSON请求（含文件名、目标路径）
 * @return 无返回值
 */
void handle_download_ctl(int client_fd, cJSON *req);

/**
 * @brief 处理客户端目录下载请求（打包目录并初始化下载）
 * @param client_fd 客户端文件描述符
 * @param req 客户端JSON请求（含目录名、目标路径）
 * @return 无返回值
 */
void handle_download_dir(int client_fd, cJSON *req);

/**
 * @brief 处理客户端下载数据（读取文件并发送）
 * @param client_fd 客户端文件描述符
 * @return 0=处理成功，-1=处理失败
 */
int handle_download(int client_fd);

/**
 * @brief 处理客户端文件/目录删除请求
 * @param client_fd 客户端文件描述符
 * @param req 客户端JSON请求（含文件名、目标路径）
 * @param client_addr 客户端地址信息
 * @return 无返回值
 */
void handle_delete(int client_fd, cJSON *req, struct sockaddr_in client_addr);

/**
 * @brief 处理客户端文件分享请求
 * @param client_fd 客户端文件描述符
 * @param req 客户端JSON请求（含接收者、文件路径、权限）
 * @return 无返回值
 */
void handle_share(int client_fd, cJSON *req);

/**
 * @brief 处理客户端操作历史查询请求
 * @param client_fd 客户端文件描述符
 * @param req 客户端JSON请求（无额外参数）
 * @return 无返回值
 */
void handle_history_query(int client_fd, cJSON *req);

/**
 * @brief 处理客户端普通消息（解析JSON并分发到对应业务函数）
 * @param client_fd 客户端文件描述符
 * @param client_addr 客户端地址信息
 * @return 无返回值
 */
void handle_client_message(int client_fd, struct sockaddr_in client_addr);

void handle_share_response(int client_fd, cJSON *req);
void check_pending_shares(int client_fd);
int copy_file(const char *src, const char *dest);

#endif // BUSINESS_H
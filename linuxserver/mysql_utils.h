#ifndef MYSQL_UTILS_H
#define MYSQL_UTILS_H

#include "cloud_disk.h"

/**
 * @brief 初始化MySQL连接，包含重试机制
 * @param 无参数
 * @return 无返回值（连接失败时直接退出程序）
 * @details 最多重试3次，每次间隔2秒，确保数据库连接稳定性
 */
void init_mysql();

/**
 * @brief 检查MySQL连接状态，断开时自动重连
 * @param 无参数
 * @return 1=连接正常/重连成功，0=重连失败
 */
int check_mysql_connection();

/**
 * @brief 查询数据库中是否存在指定用户
 * @param username 要查询的用户名
 * @return 1=用户存在，-1=用户不存在/查询失败
 */
int mysql_find_user(const char *username);

/**
 * @brief 插入用户操作记录到数据库（登录、上传、下载等）
 * @param client_fd 客户端文件描述符
 * @param username 操作用户名
 * @param ip 客户端IP地址
 * @param operation 操作类型（如"login"、"upload"）
 * @param filename 操作的文件名（无则传NULL）
 * @param status 操作状态（如"成功"、"失败"）
 * @return 无返回值
 */
void insert_operation_log(int client_fd, const char *username, const char *ip,
                          const char *operation, const char *filename, const char *status);

#endif // MYSQL_UTILS_H
#ifndef UTILS_H
#define UTILS_H

#include "cloud_disk.h"

/**
 * @brief 写入日志到服务器日志文件
 * @param level 日志级别（LOG_LEVEL_INFO/LOG_LEVEL_ERROR/LOG_LEVEL_WARN）
 * @param format 日志格式字符串（类似printf）
 * @param ... 可变参数（对应格式字符串中的占位符）
 * @return 无返回值
 */
void write_log(LogLevel level, const char *format, ...);

/**
 * @brief 检查用户指定路径是否在安全目录内（防止路径穿越攻击）
 * @param base_dir 基准安全目录（如用户根目录）
 * @param user_path 用户传入的路径（需检查的路径）
 * @return 1=路径安全，0=路径不安全（含路径穿越）
 */
int is_safe_path(const char *base_dir, const char *user_path);

/**
 * @brief 递归创建目录（模拟mkdir -p命令）
 * @param path 要创建的目录路径（支持多级目录）
 * @param mode 目录权限（如0700）
 * @return 0=创建成功，-1=创建失败
 */
int mkdir_recursive(const char *path, mode_t mode);

/**
 * @brief 构建文件的完整路径（基准目录+用户路径+文件名）
 * @param full_path 输出参数：存储构建后的完整路径
 * @param base_dir 基准目录（如用户根目录）
 * @param user_path 用户指定的相对路径（如"/docs"）
 * @param filename 文件名（如"test.txt"）
 * @return 无返回值
 */
void build_full_path(char *full_path, const char *base_dir, const char *user_path, const char *filename);

/**
 * @brief 向客户端发送JSON格式的响应（含长度前缀）
 * @param client_fd 客户端文件描述符
 * @param root cJSON对象（存储响应数据）
 * @return 无返回值
 */
void send_json_response(int client_fd, cJSON *root);

/**
 * @brief 将目录打包为tar文件（使用系统tar命令）
 * @param dir_path 要打包的目录路径
 * @param tar_path 输出tar文件的路径
 * @return 0=打包成功，-1=打包失败
 */
int pack_directory(const char *dir_path, const char *tar_path);

#endif // UTILS_H
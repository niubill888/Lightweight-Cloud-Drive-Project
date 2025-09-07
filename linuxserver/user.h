#ifndef USER_H
#define USER_H

#include "cloud_disk.h"

/**
 * @brief 获取用户根目录（优先从缓存获取，缓存未命中则查数据库）
 * @param username 用户名
 * @param root_dir 输出参数：存储用户根目录路径
 * @return 1=获取成功，0=获取失败（用户不存在）
 */
int get_user_root_dir(const char *username, char *root_dir);

/**
 * @brief 创建用户根目录（并更新数据库和缓存）
 * @param username 用户名
 * @return 1=创建成功，0=创建失败
 */
int create_user_root_dir(const char *username);

#endif // USER_H
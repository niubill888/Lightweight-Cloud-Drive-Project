#include "user.h"

/**
 * @brief 获取用户根目录（优先从缓存获取，缓存未命中则查数据库）
 * @param username 用户名
 * @param root_dir 输出参数：存储用户根目录路径
 * @return 1=获取成功，0=获取失败（用户不存在）
 */
int get_user_root_dir(const char *username, char *root_dir)
{
    // 1. 先查内存缓存
    for (int i = 0; i < user_cache_count; i++)
    {
        if (strcmp(user_cache[i].username, username) == 0)
        {
            strcpy(root_dir, user_cache[i].root_dir);
            size_t len = strlen(root_dir);
            // 仅在结尾没有/时才添加，且确保有空间
            if (len > 0 && len + 1 < MAX_PATH_LEN && root_dir[len - 1] != '/')
            {
                root_dir[len] = '/';
                root_dir[len + 1] = '\0'; // 正确设置终止符
            }
            return 1;
        }
    }

    // 2. 缓存未命中，查询数据库
    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT root_dir FROM user WHERE username='%s'", username);
    if (mysql_query(&mysql, sql) != 0)
    {
        write_log(LOG_LEVEL_ERROR, "查询用户根目录失败: %s", mysql_error(&mysql));
        return 0;
    }

    MYSQL_RES *res = mysql_store_result(&mysql);
    if (mysql_num_rows(res) == 0)
    {
        mysql_free_result(res);
        return 0;
    }

    // 提取用户根目录（修复核心）
    MYSQL_ROW row = mysql_fetch_row(res);
    strcpy(root_dir, row[0]);
    size_t len = strlen(root_dir);
    // 仅在结尾没有/时才添加，且确保有空间
    if (len > 0 && len + 1 < MAX_PATH_LEN && root_dir[len - 1] != '/')
    {
        root_dir[len] = '/';
        root_dir[len + 1] = '\0'; // 正确设置终止符
    }
    mysql_free_result(res);

    // 3. 更新内存缓存
    if (user_cache_count < MAX_USERS)
    {
        strcpy(user_cache[user_cache_count].username, username);
        strcpy(user_cache[user_cache_count].root_dir, root_dir);
        user_cache_count++;
    }

    return 1;
}

/**
 * @brief 创建用户根目录（并更新数据库和缓存）
 * @param username 用户名
 * @return 1=创建成功，0=创建失败
 */
int create_user_root_dir(const char *username)
{
    char root_dir[MAX_PATH_LEN];
    // 构建用户根目录路径（服务器根目录/用户名）
    snprintf(root_dir, MAX_PATH_LEN, "%s/%s/", SERVER_ROOT, username);

    // 递归创建用户根目录（权限0700：仅用户可读写执行）
    if (mkdir_recursive(root_dir, 0700) == -1)
    {
        write_log(LOG_LEVEL_ERROR, "创建用户根目录失败: %s, 路径: %s", strerror(errno), root_dir);
        return 0;
    }

    // 更新数据库中的用户根目录字段
    char sql[1024];
    snprintf(sql, sizeof(sql),
             "UPDATE user SET root_dir='%s' WHERE username='%s'",
             root_dir, username);
    if (mysql_query(&mysql, sql) != 0)
    {
        write_log(LOG_LEVEL_ERROR, "更新用户根目录失败: %s", mysql_error(&mysql));
        return 0;
    }

    // 更新内存缓存（已存在则覆盖，不存在则添加）
    for (int i = 0; i < user_cache_count; i++)
    {
        if (strcmp(user_cache[i].username, username) == 0)
        {
            strcpy(user_cache[i].root_dir, root_dir);
            return 1;
        }
    }

    // 缓存未满，添加新用户缓存
    if (user_cache_count < MAX_USERS)
    {
        strcpy(user_cache[user_cache_count].username, username);
        strcpy(user_cache[user_cache_count].root_dir, root_dir);
        user_cache_count++;
    }

    return 1;
}
#include "mysql_utils.h"

// 全局MySQL连接句柄（定义，声明在cloud_disk.h）
MYSQL mysql;

/**
 * @brief 初始化MySQL连接，包含重试机制
 * @param 无参数
 * @return 无返回值（连接失败时直接退出程序）
 * @details 最多重试3次，每次间隔2秒，确保数据库连接稳定性
 */
void init_mysql()
{
    mysql_init(&mysql);
    mysql_set_character_set(&mysql, "utf8");
    int retry_count = 3;    // 最多重试3次
    int retry_interval = 2; // 重试间隔2秒

    while (retry_count-- > 0)
    {
        // 尝试连接MySQL（主机：localhost，用户：root，密码：123，数据库：linuxpro，端口：3306）
        if (mysql_real_connect(&mysql, "localhost", "root", "123", "linuxpro", 3306, NULL, 0))
        {
            printf("MySQL初始化成功\n");
            write_log(LOG_LEVEL_INFO, "MySQL初始化成功");
            return;
        }

        // 连接失败，输出错误并准备重试
        fprintf(stderr, "MySQL连接失败（剩余重试次数：%d）: %s\n", retry_count, mysql_error(&mysql));
        write_log(LOG_LEVEL_ERROR, "MySQL连接失败（剩余重试次数：%d）: %s", retry_count, mysql_error(&mysql));

        if (retry_count > 0)
        {
            sleep(retry_interval); // 重试前等待
        }
    }

    // 多次重试失败后退出程序
    fprintf(stderr, "MySQL连接失败，程序无法继续运行\n");
    write_log(LOG_LEVEL_ERROR, "MySQL连接失败，程序无法继续运行");
    exit(1);
}

/**
 * @brief 检查MySQL连接状态，断开时自动重连
 * @param 无参数
 * @return 1=连接正常/重连成功，0=重连失败
 */
int check_mysql_connection()
{
    // 发送ping包检查连接
    if (mysql_ping(&mysql) != 0)
    {
        write_log(LOG_LEVEL_WARN, "MySQL连接已断开，尝试重连...");

        // 关闭旧连接
        mysql_close(&mysql);
        mysql_init(&mysql);

        // 尝试重连
        if (mysql_real_connect(&mysql, "localhost", "root", "123", "linuxpro", 3306, NULL, 0))
        {
            write_log(LOG_LEVEL_INFO, "MySQL重连成功");
            return 1;
        }
        else
        {
            write_log(LOG_LEVEL_ERROR, "MySQL重连失败: %s", mysql_error(&mysql));
            return 0;
        }
    }
    return 1;
}

/**
 * @brief 查询数据库中是否存在指定用户
 * @param username 要查询的用户名
 * @return 1=用户存在，-1=用户不存在/查询失败
 */
int mysql_find_user(const char *username)
{
    char sql[256];
    // 构建查询SQL（查询用户ID，存在则返回非空）
    snprintf(sql, sizeof(sql), "SELECT id FROM user WHERE username='%s'", username);
    if (mysql_query(&mysql, sql) != 0)
    {
        fprintf(stderr, "查询用户失败: %s\n", mysql_error(&mysql));
        write_log(LOG_LEVEL_ERROR, "查询用户失败: %s", mysql_error(&mysql));
        return -1;
    }

    // 获取查询结果
    MYSQL_RES *res = mysql_store_result(&mysql);
    // 判定用户是否存在（行数>0表示存在）
    int id = (mysql_num_rows(res) > 0) ? 1 : -1;
    mysql_free_result(res); // 释放结果集内存
    return id;
}

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
// 插入操作记录的函数
void insert_operation_log(int client_fd, const char *username, const char *ip,
                          const char *operation, const char *filename, const char *status)
{

    char sql[1024];
    // 处理filename：非空则用单引号包裹（字符串），为空则用NULL（无需引号）
    const char *filename_sql;
    if (filename && strlen(filename) > 0)
    {
        // 为避免SQL注入和语法错误，简单处理单引号（基础防护）
        static char quoted_filename[256 * 2 + 2]; // 预留足够空间
        snprintf(quoted_filename, sizeof(quoted_filename), "'%s'", filename);
        filename_sql = quoted_filename;
    }
    else
    {
        filename_sql = "NULL"; // 为空时直接用NULL（不带引号）
    }

    // 拼接SQL：插入操作记录（7个字段，与值对应）
    snprintf(sql, sizeof(sql),
             "INSERT INTO operation_log ("
             "username, client_fd, ip, operation, filename, time, status"
             ") VALUES ('%s', %d, '%s', '%s', %s, NOW(), '%s');",
             username,
             client_fd,
             ip,
             operation,
             filename_sql, // 直接使用处理后的filename（带引号或NULL）
             status);

    // 检查连接状态，失败则返回
    if (!check_mysql_connection())
    {
        return;
    }

    // 执行SQL，失败则打印错误
    if (mysql_query(&mysql, sql) != 0)
    {
        fprintf(stderr, "插入操作记录失败: %s（SQL: %s）\n", mysql_error(&mysql), sql); // 打印完整SQL方便调试
        write_log(LOG_LEVEL_ERROR, "插入操作记录失败: %s", mysql_error(&mysql));
    }
}
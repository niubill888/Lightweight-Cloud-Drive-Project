#include "utils.h"

/**
 * @brief 写入日志到服务器日志文件
 * @param level 日志级别（LOG_LEVEL_INFO/LOG_LEVEL_ERROR/LOG_LEVEL_WARN）
 * @param format 日志格式字符串（类似printf）
 * @param ... 可变参数（对应格式字符串中的占位符）
 * @return 无返回值
 */
void write_log(LogLevel level, const char *format, ...)
{
    const char *log_path = SERVER_ROOT "/server.log";
    FILE *log_file = fopen(log_path, "a+");
    if (!log_file)
    {
        perror("日志文件打开失败（write_log）");
        fprintf(stderr, "日志路径：%s\n", log_path);
        return;
    }

    // 获取当前时间并格式化
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm);

    // 映射日志级别到字符串
    const char *level_str;
    switch (level)
    {
    case LOG_LEVEL_INFO:
        level_str = "INFO";
        break;
    case LOG_LEVEL_ERROR:
        level_str = "ERROR";
        break;
    case LOG_LEVEL_WARN:
        level_str = "WARN";
        break;
    }

    // 处理可变参数并写入日志
    va_list args;
    va_start(args, format);
    fprintf(log_file, "[%s] [%s] ", time_str, level_str);
    vfprintf(log_file, format, args);
    fprintf(log_file, "\n");
    fflush(log_file); // 强制刷新缓冲区，确保日志实时写入
    va_end(args);

    fclose(log_file);
}

/**
 * @brief 检查用户指定路径是否在安全目录内（防止路径穿越攻击）
 * @param base_dir 基准安全目录（如用户根目录）
 * @param user_path 用户传入的路径（需检查的路径）
 * @return 1=路径安全，0=路径不安全（含路径穿越）
 */
int is_safe_path(const char *base_dir, const char *user_path)
{
    char real_base[MAX_PATH_LEN], real_user[MAX_PATH_LEN];
    // 获取路径的真实绝对路径（解析软链接、..等）
    char *base_res = realpath(base_dir, real_base);
    char *user_res = realpath(user_path, real_user);

    // 真实路径获取失败则判定为不安全
    if (!base_res || !user_res)
    {
        return 0;
    }

    // 检查用户路径是否以基准目录为前缀（确保在安全范围内）
    return strstr(real_user, real_base) == real_user;
}

/**
 * @brief 递归创建目录（模拟mkdir -p命令）
 * @param path 要创建的目录路径（支持多级目录）
 * @param mode 目录权限（如0700）
 * @return 0=创建成功，-1=创建失败
 */
int mkdir_recursive(const char *path, mode_t mode)
{
    // 1. 防御空路径
    if (!path || strlen(path) == 0)
    {
        write_log(LOG_LEVEL_ERROR, "mkdir_recursive: 空路径或NULL");
        return -1;
    }

    // 2. 用栈缓冲区替代 strdup（大小 = MAX_PATH_LEN，栈内存安全）
    char dup_path[MAX_PATH_LEN];
    // 安全拷贝：限制长度，避免栈溢出
    size_t safe_len = strnlen(path, MAX_PATH_LEN - 1);
    memcpy(dup_path, path, safe_len);
    dup_path[safe_len] = '\0'; // 强制终止符（确保是合法字符串）

    // 3. 后续创建目录的逻辑不变（用栈缓冲区 dup_path）
    char *p = dup_path;
    if (*p == '/')
    {
        p++; // 跳过根目录的 '/'
    }
    while (*p)
    {
        if (*p == '/')
        {
            *p = '\0';
            // 创建中间目录（忽略已存在的错误）
            if (mkdir(dup_path, mode) == -1 && errno != EEXIST)
            {
                write_log(LOG_LEVEL_ERROR, "创建目录失败: %s, 原因: %s", dup_path, strerror(errno));
                return -1;
            }
            *p = '/'; // 恢复 '/'
        }
        p++;
    }
    // 创建最后一级目录
    if (mkdir(dup_path, mode) == -1 && errno != EEXIST)
    {
        write_log(LOG_LEVEL_ERROR, "创建目录失败: %s, 原因: %s", dup_path, strerror(errno));
        return -1;
    }

    return 0; // 栈缓冲区自动释放，无需 free
}
/**
 * @brief 构建文件的完整路径（基准目录+用户路径+文件名）
 * @param full_path 输出参数：存储构建后的完整路径
 * @param base_dir 基准目录（如用户根目录）
 * @param user_path 用户指定的相对路径（如"/docs"）
 * @param filename 文件名（如"test.txt"）
 * @return 无返回值
 */
void build_full_path(char *full_path, const char *root_dir, const char *user_path, const char *filename)
{

    // 1. 处理user_path：确保不以/开头、以/结尾（非空时）
    char clean_user[MAX_PATH_LEN] = "";
    if (user_path && *user_path != '\0')
    {
        // 去掉开头所有/（确保不以/开头）
        const char *start = user_path;
        while (*start == '/')
            start++;

        // 复制处理后的内容
        strncpy(clean_user, start, MAX_PATH_LEN - 2); // 留位置给结尾/
        size_t user_len = strlen(clean_user);

        // 确保以/结尾（非空时）
        if (user_len > 0 && clean_user[user_len - 1] != '/')
        {
            clean_user[user_len] = '/';
            clean_user[user_len + 1] = '\0';
        }
    }

    // 2. 直接拼接：root_dir（带/） + clean_user（空或带/） + filename
    full_path[0] = '\0';
    strncat(full_path, root_dir, MAX_PATH_LEN - 1); // 拼接根目录（已带/）
    if (*clean_user != '\0')
    {
        strncat(full_path, clean_user, MAX_PATH_LEN - strlen(full_path) - 1); // 拼接用户目录
    }
    if (filename && *filename != '\0')
    {
        strncat(full_path, filename, MAX_PATH_LEN - strlen(full_path) - 1); // 拼接文件名
    }
    full_path[MAX_PATH_LEN - 1] = '\0';
}

/**
 * @brief 向客户端发送JSON格式的响应（含长度前缀）
 * @param client_fd 客户端文件描述符
 * @param root cJSON对象（存储响应数据）
 * @return 无返回值
 */
void send_json_response(int client_fd, cJSON *root)
{
    // 将cJSON对象转为无格式JSON字符串
    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str)
        return;

    // 计算JSON字符串长度，转换为网络字节序（大端）
    uint32_t data_len = strlen(json_str);
    uint32_t net_len = htonl(data_len);

    // 先发送长度前缀，再发送JSON数据
    send(client_fd, &net_len, 4, 0);
    send(client_fd, json_str, data_len, 0);

    free(json_str); // 释放JSON字符串内存
}

/**
 * @brief 将目录打包为tar文件（使用系统tar命令）
 * @param dir_path 要打包的目录路径
 * @param tar_path 输出tar文件的路径
 * @return 0=打包成功，-1=打包失败
 */
int pack_directory(const char *dir_path, const char *tar_path)
{
    // 构建tar命令：tar -cf 目标tar路径 -C 源目录上级目录 源目录名（避免打包时包含完整路径）
    char tar_cmd[MAX_PATH_LEN * 2]; // 足够大的缓冲区
    char dir_parent[MAX_PATH_LEN];
    char dir_name[MAX_PATH_LEN];

    // 拆分目录的「上级路径」和「目录名」（比如/aaa/bbb/ccc → 上级路径/aaa/bbb，目录名ccc）
    strncpy(dir_parent, dir_path, sizeof(dir_parent) - 1);
    dirname(dir_parent);                                                 // 获取上级路径
    strncpy(dir_name, basename((char *)dir_path), sizeof(dir_name) - 1); // 获取目录名

    // 构建tar命令：-cf 打包，-C 切换到上级目录（避免tar包内包含完整路径）
    snprintf(tar_cmd, sizeof(tar_cmd),
             "tar -cf %s -C %s %s ", // 2>/dev/null 屏蔽tar命令的错误输出
             tar_path, dir_parent, dir_name);

    // 执行tar命令，返回0表示成功
    int ret = system(tar_cmd);
    if (ret != 0)
    {
        write_log(LOG_LEVEL_ERROR, "tar命令执行失败: %s", tar_cmd);
        unlink(tar_path); // 删除创建失败的空tar文件
        return -1;
    }

    // 检查tar文件是否创建成功（存在且非空）
    struct stat tar_st;
    if (stat(tar_path, &tar_st) == -1 || tar_st.st_size == 0)
    {
        write_log(LOG_LEVEL_ERROR, "tar文件创建失败或为空: %s", tar_path);
        unlink(tar_path);
        return -1;
    }

    return 0;
}
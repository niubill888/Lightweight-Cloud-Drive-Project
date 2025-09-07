#include "business.h"
/**
 * @brief 初始化服务器（创建服务器根目录）
 * @param 无参数
 * @return 无返回值
 */
void init_server()
{
    // 创建服务器根目录（权限0755：所有者可读写执行，其他只读）
    mkdir(SERVER_ROOT, 0755);
    printf("服务器初始化完成，根目录：%s\n", SERVER_ROOT);
    write_log(LOG_LEVEL_INFO, "服务器初始化完成，根目录：%s", SERVER_ROOT);
}

/**
 * @brief 处理客户端登录请求
 * @param client_fd 客户端文件描述符
 * @param req 客户端JSON请求（含用户名、密码）
 * @param client_addr 客户端地址信息
 * @return 无返回值
 */
void handle_login(int client_fd, cJSON *req, struct sockaddr_in client_addr)
{
    // 提取JSON中的用户名和密码
    cJSON *username = cJSON_GetObjectItem(req, "username");
    cJSON *password = cJSON_GetObjectItem(req, "password");

    // 初始化响应JSON
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "type", "login_result");

    // 验证参数格式（必须是字符串）
    if (!cJSON_IsString(username) || !cJSON_IsString(password))
    {
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "用户名或密码格式错误");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    // 验证用户密码（查询数据库）
    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT password FROM user WHERE username='%s'", username->valuestring);
    if (mysql_query(&mysql, sql) != 0)
    {
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "查询失败");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    // 获取查询结果
    MYSQL_RES *mysql_res = mysql_store_result(&mysql);
    if (mysql_num_rows(mysql_res) == 0)
    { // 无此用户
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "用户不存在");
        mysql_free_result(mysql_res);
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    // 验证密码
    MYSQL_ROW row = mysql_fetch_row(mysql_res);
    if (strcmp(row[0], password->valuestring) != 0)
    { // 密码不匹配
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "密码错误");
        mysql_free_result(mysql_res);
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }
    mysql_free_result(mysql_res);

    // 登录成功 - 检查并创建用户根目录（首次登录时创建）
    char root_dir[MAX_PATH_LEN];
    if (!get_user_root_dir(username->valuestring, root_dir) || strlen(root_dir) == 0)
    {
        // 首次登录，创建根目录
        if (!create_user_root_dir(username->valuestring))
        {
            cJSON_AddBoolToObject(res, "success", 0);
            cJSON_AddStringToObject(res, "message", "创建用户目录失败");
            send_json_response(client_fd, res);
            cJSON_Delete(res);
            return;
        }
        get_user_root_dir(username->valuestring, root_dir); // 重新获取目录路径
    }
    // 登录成功处理：返回响应、绑定用户名到客户端fd、记录操作日志
    cJSON_AddBoolToObject(res, "success", 1);
    cJSON_AddStringToObject(res, "message", "登录成功");
    strncpy(client_username[client_fd], username->valuestring, sizeof(client_username[client_fd]) - 1);
    // 插入登录操作日志
    insert_operation_log(client_fd, username->valuestring,
                         inet_ntoa(client_addrs[client_fd].sin_addr),
                         "login", NULL, "成功");

    send_json_response(client_fd, res);
    cJSON_Delete(res);
    printf("用户登录成功: %s, 根目录: %s\n", username->valuestring, root_dir);
    write_log(LOG_LEVEL_INFO, "用户登录成功: %s, 根目录: %s", username->valuestring, root_dir);
}

/**
 * @brief 处理客户端注册请求
 * @param client_fd 客户端文件描述符
 * @param req 客户端JSON请求（含用户名、密码）
 * @return 无返回值
 */
void handle_register(int client_fd, cJSON *req)
{
    // 提取JSON中的用户名和密码
    cJSON *username = cJSON_GetObjectItem(req, "username");
    cJSON *password = cJSON_GetObjectItem(req, "password");

    // 初始化响应JSON
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "type", "register_result");

    // 验证参数格式（必须是字符串）
    if (!cJSON_IsString(username) || !cJSON_IsString(password))
    {
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "用户名或密码格式错误");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    // 检查用户名是否已存在
    if (mysql_find_user(username->valuestring) != -1)
    {
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "用户名已存在");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    // 插入新用户到数据库（root_dir默认空，首次登录创建）
    char sql[512];
    snprintf(sql, sizeof(sql),
             "INSERT INTO user (username, password, root_dir) VALUES ('%s', '%s', '')",
             username->valuestring, password->valuestring);
    if (mysql_query(&mysql, sql) != 0)
    {
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "注册失败");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    // 注册成功：返回响应
    cJSON_AddBoolToObject(res, "success", 1);
    cJSON_AddStringToObject(res, "message", "注册成功");
    send_json_response(client_fd, res);
    cJSON_Delete(res);
    printf("用户注册成功: %s\n", username->valuestring);
    write_log(LOG_LEVEL_INFO, "用户注册成功: %s", username->valuestring);
}

/**
 * @brief 根据用户名获取在线客户端fd
 * @param username 目标用户名
 * @return 在线fd，找不到返回-1
 */
int get_online_client_fd(const char *username)
{
    for (int fd = 0; fd < MAX_EVENTS; fd++)
    {
        // client_username[fd]是“fd→用户名”的映射（原方案已有），反向查找
        if (client_username[fd] != NULL && strcmp(client_username[fd], username) == 0)
        {
            return fd; // 找到在线fd
        }
    }
    return -1; // 接收者离线
}

/**
 * @brief 处理客户端文件列表请求
 * @param client_fd 客户端文件描述符
 * @param req 客户端JSON请求（含目标路径）
 * @return 无返回值
 */
void handle_file_list(int client_fd, cJSON *req)
{
    const char *username = client_username[client_fd];
    if (strlen(username) == 0)
    {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "type", "file_list");
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "未登录");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    char root_dir[MAX_PATH_LEN];
    if (!get_user_root_dir(username, root_dir))
    {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "type", "file_list");
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "获取用户目录失败");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    const char *user_path = "/";
    cJSON *path_json = cJSON_GetObjectItem(req, "path");
    if (cJSON_IsString(path_json))
    {
        user_path = path_json->valuestring;
    }

    char full_path[MAX_PATH_LEN];
    build_full_path(full_path, root_dir, user_path, "");

    if (!is_safe_path(root_dir, full_path))
    {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "type", "file_list");
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "路径非法");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }
    mkdir_recursive(full_path, 0700);

    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "type", "file_list");
    cJSON_AddBoolToObject(res, "success", 1);
    cJSON *files = cJSON_CreateArray();

    DIR *dir = opendir(full_path);
    if (dir)
    {
        struct dirent *entry;
        struct stat st;
        while ((entry = readdir(dir)) != NULL)
        {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            char entry_path[MAX_PATH_LEN];
            snprintf(entry_path, MAX_PATH_LEN, "%s/%s", full_path, entry->d_name);
            if (stat(entry_path, &st) == 0)
            {
                cJSON *file = cJSON_CreateObject();
                cJSON_AddStringToObject(file, "name", entry->d_name);
                cJSON_AddBoolToObject(file, "is_directory", S_ISDIR(st.st_mode));
                cJSON_AddNumberToObject(file, "size", st.st_size);
                cJSON_AddItemToArray(files, file);
            }
        }
        closedir(dir);
    }

    cJSON_AddItemToObject(res, "files", files);
    send_json_response(client_fd, res);
    cJSON_Delete(res);
}

/**
 * @brief 处理客户端文件上传控制请求（初始化上传）
 * @param client_fd 客户端文件描述符
 * @param req 客户端JSON请求（含文件名、文件大小、目标路径）
 * @return 无返回值
 */
void handle_upload_ctl(int client_fd, cJSON *req)
{
    // 检查是否已登录
    const char *username = client_username[client_fd];
    if (strlen(username) == 0)
    {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "type", "upload_result");
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "未登录");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    // 获取用户根目录
    char root_dir[MAX_PATH_LEN];
    if (!get_user_root_dir(username, root_dir))
    {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "type", "upload_result");
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "获取用户目录失败");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    // 提取上传参数：文件名、文件大小、目标路径
    cJSON *filename = cJSON_GetObjectItem(req, "filename");
    cJSON *size_json = cJSON_GetObjectItem(req, "size");
    cJSON *path_json = cJSON_GetObjectItem(req, "path");
    const char *user_path = "/";
    if (cJSON_IsString(path_json))
    {
        user_path = path_json->valuestring;
    }

    // 初始化响应JSON
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "type", "upload_result");

    // 构建上传文件的完整路径
    char filepath[MAX_PATH_LEN];
    build_full_path(filepath, root_dir, user_path, filename->valuestring);

    // 关键修正 1：正确打开文件（O_RDWR 支持读写，O_CREAT 不存在则创建，O_TRUNC 存在则清空）
    int file_fd = open(filepath, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (file_fd == -1)
    {
        perror("文件打开失败（上传）");
        write_log(LOG_LEVEL_ERROR, "客户端 %d 文件打开失败（上传）: %s", client_fd, strerror(errno));
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "创建文件失败");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    // 关键修正 2：获取文件实际大小（覆盖客户端传来的 size，确保准确性）
    struct stat st;
    if (fstat(file_fd, &st) == -1)
    {
        perror("fstat 失败");
        write_log(LOG_LEVEL_ERROR, "客户端 %d fstat 失败：%s", client_fd, strerror(errno));
        close(file_fd); // 关闭无效文件描述符
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "获取文件大小失败");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }
    long long actual_file_size = size_json->valuedouble; // 客户端实际文件大小

    // 初始化客户端上传状态（绑定到 client_fd）
    if (client_fd < MAX_EVENTS)
    {
        client_up_info[client_fd].state = UP_STATE_RECEIVING;
        strncpy(client_up_info[client_fd].filepath, filepath, sizeof(client_up_info[client_fd].filepath) - 1);
        client_up_info[client_fd].filesize = actual_file_size; // 使用实际大小
        client_up_info[client_fd].received = 0;
        client_up_info[client_fd].fd = file_fd; // 保存文件描述符
    }

    // 通知客户端：服务器已准备好接收数据
    cJSON *ready = cJSON_CreateObject();
    cJSON_AddStringToObject(ready, "type", "ready_to_receive");
    send_json_response(client_fd, ready);
    cJSON_Delete(ready);
    cJSON_Delete(res);
}

/**
 * @brief 处理客户端上传数据（接收并写入文件）
 * @param client_fd 客户端文件描述符
 * @return 0=处理成功，-1=处理失败
 */
int handle_upload(int client_fd)
{
    // 检查上传状态（仅处理“接收中”状态的请求）
    if (client_up_info[client_fd].state != UP_STATE_RECEIVING)
    {
        return -1;
    }

    // 从上传状态中提取关键信息
    int file_fd = client_up_info[client_fd].fd;
    long long file_size = client_up_info[client_fd].filesize;

    // 循环读取，直到缓冲区为空或数据接收完成
    while (client_up_info[client_fd].received < file_size)
    {
        char file_buf[BUFFER_SIZE];
        ssize_t len = recv(client_fd, file_buf, BUFFER_SIZE, 0);
        if (len < 0)
        {
            // 区分 "缓冲区空"（EAGAIN）和 "真实错误"
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break; // 缓冲区空，退出循环
            }
            // 其他错误（如客户端断开）
            perror("上传数据接收失败");
            write_log(LOG_LEVEL_ERROR, "客户端 %d 上传数据接收失败: %s", client_fd, strerror(errno));
            close(file_fd);
            client_up_info[client_fd].state = UP_STATE_IDLE;
            // 发送失败响应
            cJSON *progress_res = cJSON_CreateObject();
            cJSON_AddStringToObject(progress_res, "type", "upload_progress");
            cJSON_AddBoolToObject(progress_res, "success", 0);
            send_json_response(client_fd, progress_res);
            cJSON_Delete(progress_res);
            return -1;
        }
        else if (len == 0)
        {
            // 客户端主动断开
            perror("客户端断开连接");
            write_log(LOG_LEVEL_WARN, "客户端 %d 上传时断开连接", client_fd);
            close(file_fd);
            client_up_info[client_fd].state = UP_STATE_IDLE;
            return -1;
        }

        // 写入文件
        ssize_t written = write(file_fd, file_buf, len);
        if (written != len)
        {
            perror("写入文件失败");
            write_log(LOG_LEVEL_ERROR, "客户端 %d 写入文件失败: %s", client_fd, strerror(errno));
            close(file_fd);
            client_up_info[client_fd].state = UP_STATE_IDLE;
            return -1;
        }

        // 更新接收进度，并检查是否已完成
        client_up_info[client_fd].received += written;
        printf("DEBUG: 小文件接收 %zd 字节，累计 %lld/%lld\n", len, client_up_info[client_fd].received, file_size);

        if (client_up_info[client_fd].received >= file_size)
        {
            break; // 数据已接收完，提前退出循环
        }
    }
    // ====================== 结束文件处理 ======================

    // ====================== 进度计算与响应 ======================
    bool isComplete = (client_up_info[client_fd].received >= client_up_info[client_fd].filesize);
    double progress = isComplete ? 100.0 : (double)client_up_info[client_fd].received / client_up_info[client_fd].filesize * 100;

    // 发送上传进度响应
    cJSON *progress_res = cJSON_CreateObject();
    cJSON_AddStringToObject(progress_res, "type", "upload_progress");
    cJSON_AddBoolToObject(progress_res, "success", 1);
    cJSON_AddNumberToObject(progress_res, "progress", progress);
    cJSON_AddNumberToObject(progress_res, "received", client_up_info[client_fd].received);
    cJSON_AddNumberToObject(progress_res, "total", client_up_info[client_fd].filesize);
    send_json_response(client_fd, progress_res);
    cJSON_Delete(progress_res);

    // 上传完成判断（基于整数比较，避免浮点误差）
    if (isComplete)
    {
        // 记录上传成功日志
        insert_operation_log(client_fd, client_username[client_fd],
                             inet_ntoa(client_addrs[client_fd].sin_addr),
                             "upload", client_up_info[client_fd].filepath, "成功");

        close(file_fd); // 关闭文件描述符
        // 发送上传完成响应
        cJSON *finish_res = cJSON_CreateObject();
        cJSON_AddStringToObject(finish_res, "type", "upload_result");
        cJSON_AddBoolToObject(finish_res, "success", 1);
        cJSON_AddStringToObject(finish_res, "message", "文件上传完成");
        send_json_response(client_fd, finish_res);
        cJSON_Delete(finish_res);

        client_up_info[client_fd].state = UP_STATE_IDLE; // 重置上传状态
        printf("客户端 %d 文件上传完成：%s\n", client_fd, client_up_info[client_fd].filepath);
        write_log(LOG_LEVEL_INFO, "客户端 %d 文件上传完成：%s", client_fd, client_up_info[client_fd].filepath);
    }

    return 0;
}

/**
 * @brief 处理客户端文件下载控制请求（初始化下载）
 * @param client_fd 客户端文件描述符
 * @param req 客户端JSON请求（含文件名、目标路径）
 * @return 无返回值
 */
void handle_download_ctl(int client_fd, cJSON *req)
{
    // 检查是否已登录
    const char *username = client_username[client_fd];
    if (strlen(username) == 0)
    {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "type", "download_result");
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "未登录");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    // 获取用户根目录
    char root_dir[MAX_PATH_LEN];
    if (!get_user_root_dir(username, root_dir))
    {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "type", "download_result");
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "获取用户目录失败");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    // 提取下载参数：文件名、目标路径（默认根目录）
    cJSON *filename = cJSON_GetObjectItem(req, "filename");
    cJSON *path_json = cJSON_GetObjectItem(req, "path");
    const char *user_path = "/";
    if (cJSON_IsString(path_json))
    {
        user_path = path_json->valuestring;
    }

    // 验证参数格式（文件名必须是字符串）
    if (!cJSON_IsString(filename))
    {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "type", "download_result");
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "参数错误");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    // 构建下载文件的完整路径
    char filepath[MAX_PATH_LEN];
    build_full_path(filepath, root_dir, user_path, filename->valuestring);
    strncpy(client_dl_info[client_fd].filepath, filepath, sizeof(client_dl_info[client_fd].filepath) - 1);
    // 路径安全检查（防止路径穿越）
    if (!is_safe_path(root_dir, filepath))
    {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "type", "download_result");
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "路径非法");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    // 检查文件是否存在
    struct stat st;
    if (stat(filepath, &st) == -1)
    {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "type", "download_result");
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "文件不存在");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }
    long long fileSize = st.st_size; // 获取文件大小

    // 发送文件元数据（文件名、大小、是否为目录）
    cJSON *meta = cJSON_CreateObject();
    cJSON_AddStringToObject(meta, "type", "download_meta");
    cJSON_AddStringToObject(meta, "filename", filename->valuestring);
    cJSON_AddNumberToObject(meta, "size", fileSize);
    cJSON_AddBoolToObject(meta, "is_directory", 0); // 标记为文件
    send_json_response(client_fd, meta);
    cJSON_Delete(meta);

    // 在 handle_download_ctl 末尾加上
    client_dl_info[client_fd].filesize = fileSize;
    client_dl_info[client_fd].offset = 0;
    client_dl_info[client_fd].total_sent = 0;
    client_dl_info[client_fd].fd = -1; // 表示未打开
}

/**
 * @brief 处理客户端下载数据（读取文件并发送）
 * @param client_fd 客户端文件描述符
 * @return 0=处理成功，-1=处理失败
 */
int handle_download(int client_fd)
{
    // 检查下载状态
    printf("handle_download: client_fd=%d, state=%d\n", client_fd, client_dl_info[client_fd].state);
    // 从下载状态中提取关键信息
    const char *filepath = client_dl_info[client_fd].filepath;
    long long fileSize = client_dl_info[client_fd].filesize;

    // 以只读方式打开下载文件
    int fd;
    if (client_dl_info[client_fd].fd < 0)
    {
        fd = open(filepath, O_RDONLY);
        printf("open file: %s, fd=%d\n", filepath, fd);
        lseek(fd, client_dl_info[client_fd].offset, SEEK_SET);
        client_dl_info[client_fd].fd = fd;
    }
    else
    {
        fd = client_dl_info[client_fd].fd;
        lseek(fd, client_dl_info[client_fd].offset, SEEK_SET);
    }

    if (fd == -1)
    {
        perror("下载文件打开失败");
        write_log(LOG_LEVEL_ERROR, "客户端 %d 下载文件打开失败: %s", client_fd, strerror(errno));
        // 记录下载失败日志
        insert_operation_log(client_fd, client_username[client_fd], inet_ntoa(client_addrs[client_fd].sin_addr), "download", filepath, "失败");
        // 发送下载失败响应
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "type", "download_result");
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "文件打开失败");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        client_dl_info[client_fd].state = DL_STATE_IDLE; // 重置下载状态
        return -1;
    }
    // 通知客户端：服务器已准备好发送数据
    if (client_dl_info[client_fd].state != DL_STATE_SENDING)
    { // 新增判断
        cJSON *ready = cJSON_CreateObject();
        cJSON_AddStringToObject(ready, "type", "ready_to_send");
        send_json_response(client_fd, ready);
        cJSON_Delete(ready);
        client_dl_info[client_fd].state = DL_STATE_SENDING; // 只在首次准备时更新状态
    }
    printf("客户端 %d 准备发送文件：%s (%lld bytes)\n", client_fd, filepath, fileSize);

    // 小文件使用传统 read/write 方式
    char file_buf[BUFFER_SIZE];
    ssize_t read_len;
    long long total_sent = client_dl_info[client_fd].total_sent; // 继续上次发送进度

    printf("fd=%d, offset=%lld, fileSize=%lld, 未发数据长度=%zu\n",
           fd, client_dl_info[client_fd].offset, fileSize,
           client_dl_info[client_fd].remaining_len); // 新增：打印未发长度

    // 【新增逻辑：先发上次没发完的remaining数据】
    if (client_dl_info[client_fd].remaining_len > 0)
    {
        size_t total_written = 0;
        // 循环发送remaining_buf中的未发数据
        while (total_written < client_dl_info[client_fd].remaining_len)
        {
            ssize_t sent = send(client_fd,
                                client_dl_info[client_fd].remaining_buf + total_written,
                                client_dl_info[client_fd].remaining_len - total_written,
                                0);
            if (sent < 0)
            {
                if (errno == EINTR)
                    continue;
                else if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    // 仍没发完，更新remaining数据（把已发的部分删掉）
                    memmove(client_dl_info[client_fd].remaining_buf,
                            client_dl_info[client_fd].remaining_buf + total_written,
                            client_dl_info[client_fd].remaining_len - total_written);
                    client_dl_info[client_fd].remaining_len -= total_written;
                    // 更新累计发送量，保存进度
                    client_dl_info[client_fd].total_sent += total_written;
                    return 0; // 等下次epoll事件再发
                }
                else
                {
                    // 其他错误，清理
                    perror("未发数据发送失败");
                    close(fd);
                    client_dl_info[client_fd].state = DL_STATE_IDLE;
                    client_dl_info[client_fd].remaining_len = 0; // 重置未发长度
                    return -1;
                }
            }
            total_written += sent;
        }
        // 所有remaining数据发完，重置缓冲区
        client_dl_info[client_fd].remaining_len = 0;
        total_sent += total_written; // 更新累计发送量
        printf("上次未发数据已发完，累计发送：%lld\n", total_sent);
    }

    while ((total_sent < fileSize) && (read_len = read(fd, file_buf, BUFFER_SIZE)) > 0)
    { // 程序没进循环！
        printf("read_len=%zd\n", read_len);
        ssize_t total_written = 0;
        // 确保所有读取的数据都发送完毕
        while (total_written < read_len)
        {
            ssize_t sent = send(client_fd, file_buf + total_written, read_len - total_written, 0);
            if (sent < 0)
            {
                if (errno == EINTR)
                {
                    continue; // 被信号中断，重试
                }
                else if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    // printf("EAGAIN: 缓冲区满，等待下次发送\n");
                    //  缓冲区满，保存进度，等待下次epoll事件
                    //  不要close(fd)，不要发完成响应
                    //  新逻辑：1. 暂存未发数据 2. 更新正确的offset 3. 保存进度
                    size_t unsent_len = read_len - total_written; // 本次未发完的字节数
                    // 把未发的部分copy到remaining_buf
                    memcpy(client_dl_info[client_fd].remaining_buf,
                           file_buf + total_written,
                           unsent_len);
                    client_dl_info[client_fd].remaining_len = unsent_len; // 记录未发长度

                    // 关键：offset += read_len（文件已读了read_len字节，下次从这之后读）
                    client_dl_info[client_fd].offset += read_len;
                    // 关键：累计发送量 += 已发的字节数
                    client_dl_info[client_fd].total_sent += total_written;

                    printf("EAGAIN：暂存未发数据%zu字节，下次从文件offset=%lld继续\n",
                           unsent_len, client_dl_info[client_fd].offset);
                    return 0;
                }
                else
                {
                    // 其他错误，处理异常
                    perror("文件发送失败");
                    write_log(LOG_LEVEL_ERROR, "客户端 %d 文件发送失败: %s", client_fd, strerror(errno));
                    close(fd);
                    client_dl_info[client_fd].state = DL_STATE_IDLE;
                    return -1;
                }
            }
            total_written += sent;
        }
        total_sent += total_written;
        client_dl_info[client_fd].offset += read_len;      // 更新文件偏移
        client_dl_info[client_fd].total_sent = total_sent; // 保存累计发送量
        printf("read_len=%zd, total_written=%zd, total_sent=%lld\n", read_len, total_written, total_sent);
        // // 发送下载进度
        // double progress = (double)total_sent / fileSize * 100;
        // cJSON* progress_res = cJSON_CreateObject();
        // cJSON_AddStringToObject(progress_res, "type", "download_progress");
        // cJSON_AddNumberToObject(progress_res, "progress", progress);
        // send_json_response(client_fd, progress_res);
        // cJSON_Delete(progress_res);
        printf("DEBUG: 发送 %zd 字节，累计 %lld/%lld\n", total_written, total_sent, fileSize);
    }
    // 只有真正全部发完才执行下面
    if (total_sent >= fileSize)
    {
        close(fd);
        // 发送下载完成响应
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "type", "download_result");
        cJSON_AddBoolToObject(res, "success", 1);
        cJSON_AddStringToObject(res, "message", "下载完成");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        client_dl_info[client_fd].state = DL_STATE_IDLE;
        // 记录下载成功日志
        insert_operation_log(client_fd, client_username[client_fd],
                             inet_ntoa(client_addrs[client_fd].sin_addr),
                             "download", filepath, "成功");

        printf("客户端 %d 文件下载完成：%s\n", client_fd, filepath);
        write_log(LOG_LEVEL_INFO, "客户端 %d 文件下载完成：%s", client_fd, filepath);

        // 文件全部发送完毕后
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = client_fd;
        epoll_ctl(epfd, EPOLL_CTL_MOD, client_fd, &ev);

        // close(fd);
        return 0;
    }
}

/**
 * @brief 处理客户端文件/目录删除请求
 * @param client_fd 客户端文件描述符
 * @param req 客户端JSON请求（含文件名、目标路径）
 * @param client_addr 客户端地址信息
 * @return 无返回值
 */
void handle_delete(int client_fd, cJSON *req, struct sockaddr_in client_addr)
{
    // 检查是否已登录
    const char *username = client_username[client_fd];
    if (strlen(username) == 0)
    {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "type", "delete_result");
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "未登录");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    // 获取用户根目录
    char root_dir[MAX_PATH_LEN];
    if (!get_user_root_dir(username, root_dir))
    {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "type", "delete_result");
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "获取用户目录失败");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    // 提取删除参数：文件名、目标路径（默认根目录）
    cJSON *filename = cJSON_GetObjectItem(req, "filename");
    cJSON *path_json = cJSON_GetObjectItem(req, "path");
    const char *user_path = "/";
    if (cJSON_IsString(path_json))
    {
        user_path = path_json->valuestring;
    }

    // 验证参数格式（文件名必须是字符串）
    if (!cJSON_IsString(filename))
    {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "type", "delete_result");
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "参数错误");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    // 构建删除文件/目录的完整路径
    char filepath[MAX_PATH_LEN];
    build_full_path(filepath, root_dir, user_path, filename->valuestring);

    // 路径安全检查（防止路径穿越）
    if (!is_safe_path(root_dir, filepath))
    {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "type", "delete_result");
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "路径非法");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    // 检查文件/目录是否存在
    struct stat st;
    if (stat(filepath, &st) != 0)
    {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "type", "delete_result");
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "文件不存在");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    int success = 0;
    // 判断是目录还是文件，分别处理删除逻辑
    if (S_ISDIR(st.st_mode))
    {
        // 递归删除目录（先删子文件/子目录，再删当前目录）
        DIR *dir = opendir(filepath);
        if (dir)
        {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL)
            {
                // 跳过当前目录（.）和上级目录（..）
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                    continue;

                // 构建子文件/子目录的完整路径
                char entry_path[MAX_PATH_LEN];
                snprintf(entry_path, MAX_PATH_LEN, "%s/%s", filepath, entry->d_name);
                struct stat entry_st;
                if (stat(entry_path, &entry_st) == 0)
                {
                    if (S_ISDIR(entry_st.st_mode))
                    {
                        // 递归删除子目录（使用系统命令 rm -rf，简化逻辑）
                        char rm_cmd[1024];
                        snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf %s", entry_path);
                        system(rm_cmd);
                    }
                    else
                    {
                        // 删除子文件
                        unlink(entry_path);
                    }
                }
            }
            closedir(dir);
            // 删除空目录
            success = rmdir(filepath) == 0;
        }
    }
    else
    {
        // 删除文件
        success = unlink(filepath) == 0;
    }

    // 发送删除结果响应
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "type", "delete_result");
    cJSON_AddBoolToObject(res, "success", success);
    cJSON_AddStringToObject(res, "message", success ? "删除成功" : "删除失败");
    send_json_response(client_fd, res);
    cJSON_Delete(res);

    // 记录删除操作日志（区分成功/失败）
    if (success)
    {
        insert_operation_log(client_fd, username,
                             inet_ntoa(client_addr.sin_addr),
                             "delete", filename->valuestring, "成功");
        write_log(LOG_LEVEL_INFO, "客户端 %d 删除成功: %s", client_fd, filename->valuestring);
    }
    else
    {
        insert_operation_log(client_fd, username,
                             inet_ntoa(client_addr.sin_addr),
                             "delete", filename->valuestring, "失败");
        write_log(LOG_LEVEL_WARN, "客户端 %d 删除失败: %s", client_fd, filename->valuestring);
    }
}
/**
 * @brief 处理客户端对分享的响应（接受/拒绝）
 * @param client_fd 客户端文件描述符
 * @param req 客户端JSON请求
 * @return 无返回值
 */
void handle_share_response(int client_fd, cJSON *req)
{
    const char *username = client_username[client_fd];
    int share_id = cJSON_GetNumberValue(cJSON_GetObjectItem(req, "share_id"));
    const char *action = cJSON_GetStringValue(cJSON_GetObjectItem(req, "action"));

    // 查询分享信息
    char sql[1024];
    snprintf(sql, sizeof(sql),
             "SELECT owner, filepath, filename FROM file_share "
             "WHERE id = %d AND recipient = '%s' AND status = 'pending'",
             share_id, username);

    if (mysql_query(&mysql, sql) != 0)
    {
        write_log(LOG_LEVEL_ERROR, "处理分享响应查询失败: %s", mysql_error(&mysql));
        return;
    }
    MYSQL_RES *res = mysql_store_result(&mysql);
    if (!res || mysql_num_rows(res) == 0)
    {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "type", "share_result");
        cJSON_AddBoolToObject(response, "success", 0);
        cJSON_AddStringToObject(response, "message", "分享请求不存在或已处理");
        send_json_response(client_fd, response);
        cJSON_Delete(response);
        return;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    const char *owner = row[0];
    const char *filepath = row[1];
    const char *filename = row[2];
    mysql_free_result(res);

    // 更新分享状态
    snprintf(sql, sizeof(sql),
             "UPDATE file_share SET status = '%s', accept_time = NOW() "
             "WHERE id = %d",
             (strcmp(action, "accept") == 0) ? "accepted" : "rejected", share_id);

    if (mysql_query(&mysql, sql) != 0)
    {
        write_log(LOG_LEVEL_ERROR, "更新分享状态失败: %s", mysql_error(&mysql));
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "type", "share_result");
        cJSON_AddBoolToObject(response, "success", 0);
        cJSON_AddStringToObject(response, "message", "处理分享请求失败");
        send_json_response(client_fd, response);
        cJSON_Delete(response);
        return;
    }

    // 如果是接受分享，需要复制文件到接收者目录
    if (strcmp(action, "accept") == 0)
    {
        char owner_root[MAX_PATH_LEN];
        get_user_root_dir(owner, owner_root);

        char full_src_path[MAX_PATH_LEN];
        snprintf(full_src_path, sizeof(full_src_path), "%s/%s/%s",
                 owner_root, filepath, filename);

        char recipient_root[MAX_PATH_LEN];
        get_user_root_dir(username, recipient_root);

        char full_dest_path[MAX_PATH_LEN];
        snprintf(full_dest_path, sizeof(full_dest_path), "%s/shared/%s",
                 recipient_root, filename);

        // 创建shared目录
        char shared_dir[MAX_PATH_LEN];
        snprintf(shared_dir, sizeof(shared_dir), "%s/shared", recipient_root);
        mkdir_recursive(shared_dir, 0700);

        // 复制文件
        if (copy_file(full_src_path, full_dest_path) != 0)
        {
            write_log(LOG_LEVEL_ERROR, "复制分享文件失败: %s -> %s", full_src_path, full_dest_path);
            cJSON *response = cJSON_CreateObject();
            cJSON_AddStringToObject(response, "type", "share_result");
            cJSON_AddBoolToObject(response, "success", 0);
            cJSON_AddStringToObject(response, "message", "接收文件失败");
            send_json_response(client_fd, response);
            cJSON_Delete(response);
            return;
        }
    }

    // 记录操作日志
    insert_operation_log(client_fd, username, inet_ntoa(client_addrs[client_fd].sin_addr),
                         (strcmp(action, "accept") == 0) ? "accept_share" : "reject_share",
                         filename, "成功");

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", "share_result");
    cJSON_AddBoolToObject(response, "success", 1);
    cJSON_AddStringToObject(response, "message",
                            (strcmp(action, "accept") == 0) ? "已接受分享" : "已拒绝分享");
    send_json_response(client_fd, response);
    cJSON_Delete(response);
}

/**
 * @brief 获取文件的所有者用户名
 * @param filepath 文件路径
 * @param owner 输出参数，存储所有者用户名
 * @return 成功返回1，失败返回0
 */
int get_file_owner(const char *filepath, char *owner)
{
    if (!filepath || !owner)
        return 0;

    char sql[512];
    // 假设存在 file 表，包含 filepath 和 owner 字段
    snprintf(sql, sizeof(sql), "SELECT owner FROM file WHERE filepath='%s'", filepath);

    if (mysql_query(&mysql, sql) != 0)
    {
        write_log(LOG_LEVEL_ERROR, "查询文件所有者失败: %s", mysql_error(&mysql));
        return 0;
    }

    MYSQL_RES *res = mysql_store_result(&mysql);
    if (!res || mysql_num_rows(res) == 0)
    {
        mysql_free_result(res);
        return 0; // 未找到文件所有者
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    strncpy(owner, row[0], 20 - 1);
    mysql_free_result(res);
    return 1;
}

// 添加文件复制函数
int copy_file(const char *src, const char *dest)
{
    int src_fd = open(src, O_RDONLY);
    if (src_fd == -1)
    {
        return -1;
    }

    int dest_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dest_fd == -1)
    {
        close(src_fd);
        return -1;
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    while ((bytes_read = read(src_fd, buffer, BUFFER_SIZE)) > 0)
    {
        if (write(dest_fd, buffer, bytes_read) != bytes_read)
        {
            close(src_fd);
            close(dest_fd);
            return -1;
        }
    }

    close(src_fd);
    close(dest_fd);
    return bytes_read == 0 ? 0 : -1;
}
/**
 * @brief 处理客户端文件分享请求
 * @param client_fd 客户端文件描述符
 * @param req 客户端JSON请求（含接收者、文件路径、权限）
 * @return 无返回值
 */
void handle_share(int client_fd, cJSON *req)
{
    const char *owner = client_username[client_fd];
    const char *recipient = cJSON_GetStringValue(cJSON_GetObjectItem(req, "recipient"));
    const char *filepath = cJSON_GetStringValue(cJSON_GetObjectItem(req, "path")); // 注意：客户端传的是"path"，对应表的filepath
    const char *filename = cJSON_GetStringValue(cJSON_GetObjectItem(req, "filename"));

    // 1. 转义变量（避免SQL注入）
    char owner_esc[512], recipient_esc[512], filepath_esc[512], filename_esc[512], perm_esc[512];
    mysql_real_escape_string(&mysql, owner_esc, owner, strlen(owner));
    mysql_real_escape_string(&mysql, recipient_esc, recipient, strlen(recipient));
    mysql_real_escape_string(&mysql, filepath_esc, filepath, strlen(filepath));
    mysql_real_escape_string(&mysql, filename_esc, filename, strlen(filename));

    // 2. 插入file_share表（补全filename字段）
    char sql[1024];
    snprintf(sql, sizeof(sql),
             "INSERT INTO file_share (owner, recipient, filepath, filename, share_time, status) "
             "VALUES ('%s', '%s', '%s', '%s', NOW(), 'pending')",
             owner_esc, recipient_esc, filepath_esc, filename_esc);

    if (mysql_query(&mysql, sql) != 0)
    {
        write_log(LOG_LEVEL_ERROR, "文件分享失败: %s", mysql_error(&mysql));
        // 给分享者返回失败响应
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "type", "share_result");
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "分享失败：数据库错误");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    // 3. 关键：获取刚插入的分享ID（用于接收者响应时关联）
    int share_id = mysql_insert_id(&mysql); // 获取自增ID

    // 4. 检查接收者是否在线，若在线则推送share_request消息
    int recipient_fd = get_online_client_fd(recipient);
    if (recipient_fd != -1)
    {
        // 构造推送的JSON消息（和接收者上线时check_pending_shares的格式一致）
        cJSON *push_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(push_msg, "type", "share_request"); // 实时推送用share_request
        cJSON_AddNumberToObject(push_msg, "id", share_id);          // 分享ID
        cJSON_AddStringToObject(push_msg, "owner", owner);          // 分享者
        cJSON_AddStringToObject(push_msg, "filename", filename);    // 文件名
        // 发送推送消息给接收者
        send_json_response(recipient_fd, push_msg);
        cJSON_Delete(push_msg);
        write_log(LOG_LEVEL_INFO, "已推送分享请求给在线用户 %s（fd: %d）", recipient, recipient_fd);
    }
    else
    {
        write_log(LOG_LEVEL_INFO, "接收者 %s 离线，等待上线后推送", recipient);
    }

    // 5. 给分享者返回成功响应
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "type", "share_result");
    cJSON_AddBoolToObject(res, "success", 1);
    cJSON_AddStringToObject(res, "message", "分享已发起，等待接收者确认");
    send_json_response(client_fd, res);
    cJSON_Delete(res);
}

/**
 * @brief 处理客户端操作历史查询请求
 * @param client_fd 客户端文件描述符
 * @param req 客户端JSON请求（无额外参数）
 * @return 无返回值
 */
void handle_history_query(int client_fd, cJSON *req)
{
    // 检查是否已登录
    const char *username = client_username[client_fd];
    if (strlen(username) == 0)
    {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "type", "history_result");
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "未登录");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    // 构建查询SQL：获取该用户最近100条操作记录
    char sql[1024];
    snprintf(sql, sizeof(sql),
             "SELECT filename, operation, time, status FROM operation_log "
             "WHERE username='%s' ORDER BY time DESC LIMIT 100", // 按时间倒序，取前100条
             username);

    // 记录SQL执行开始时间（调试用）
    struct timeval sql_start;
    gettimeofday(&sql_start, NULL);
    // 执行SQL查询
    if (mysql_query(&mysql, sql) != 0)
    {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "type", "history_result");
        cJSON_AddBoolToObject(res, "success", 0);
        cJSON_AddStringToObject(res, "message", "查询失败");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        return;
    }

    // 获取查询结果
    MYSQL_RES *res = mysql_store_result(&mysql);
    MYSQL_ROW row;
    // 记录SQL执行结束时间，计算耗时（调试用）
    struct timeval sql_end;
    gettimeofday(&sql_end, NULL);
    long sql_elapsed_ms = (sql_end.tv_sec - sql_start.tv_sec) * 1000 +
                          (sql_end.tv_usec - sql_start.tv_usec) / 1000;
    printf("[服务器] 客户端 %d SQL执行耗时：%ld ms\n", client_fd, sql_elapsed_ms);

    // 构建历史记录响应
    cJSON *history_res = cJSON_CreateObject();
    cJSON_AddStringToObject(history_res, "type", "history_result");
    cJSON_AddBoolToObject(history_res, "success", 1);
    cJSON *records = cJSON_CreateArray(); // 存储多条记录

    // 遍历查询结果，添加到响应中
    while ((row = mysql_fetch_row(res)) != NULL)
    {
        cJSON *record = cJSON_CreateObject();
        cJSON_AddStringToObject(record, "filename", row[0] ? row[0] : "");   // 文件名（空则填空字符串）
        cJSON_AddStringToObject(record, "operation", row[1] ? row[1] : "");  // 操作类型
        cJSON_AddStringToObject(record, "time", row[2] ? row[2] : "");       // 操作时间
        cJSON_AddStringToObject(record, "status", row[3] ? row[3] : "未知"); // 操作状态（默认“未知”）
        cJSON_AddItemToArray(records, record);
    }
    mysql_free_result(res); // 释放结果集内存

    // 添加记录数组到响应，发送给客户端
    cJSON_AddItemToObject(history_res, "records", records);
    send_json_response(client_fd, history_res);
    cJSON_Delete(history_res);
}

/**
 * @brief 处理客户端普通消息（解析JSON并分发到对应业务函数）
 * @param client_fd 客户端文件描述符
 * @param client_addr 客客户端地址信息
 * @return 无返回值
 */
void handle_client_message(int client_fd, struct sockaddr_in client_addr)
{
    // 第一步：读取JSON消息长度前缀（4字节，网络字节序）
    int net_len;
    ssize_t recv_len = recv(client_fd, &net_len, 4, 0);
    if (recv_len <= 0)
    {
        // 处理客户端断开（正常/异常）
        if (recv_len == 0)
        {
            write_log(LOG_LEVEL_INFO, "客户端 %d 正常断开", client_fd);
        }
        else
        {
            write_log(LOG_LEVEL_WARN, "客户端 %d 异常掉线: %s", client_fd, strerror(errno));
        }

        // 客户端已登录，记录退出日志
        if (strlen(client_username[client_fd]) > 0)
        {
            insert_operation_log(client_fd, client_username[client_fd],
                                 inet_ntoa(client_addr.sin_addr),
                                 "logout", NULL, "成功");
            memset(client_username[client_fd], 0, sizeof(client_username[client_fd]));
        }
        close(client_fd);
        return;
    }

    // 转换长度为本地字节序
    int data_len = ntohl(net_len);
    // 分配缓冲区存储JSON数据（+1用于存储字符串结束符）
    char *json_buf = malloc(data_len + 1);
    if (!json_buf)
    {
        write_log(LOG_LEVEL_ERROR, "内存不足，无法分配JSON缓冲区");
        close(client_fd);
        return;
    }

    // 第二步：读取完整JSON数据（处理粘包/分包）
    ssize_t total_read = 0;
    while (total_read < data_len)
    {
        recv_len = recv(client_fd, json_buf + total_read, data_len - total_read, 0);
        if (recv_len < 0)
        {
            // 非致命错误（中断或暂时无法读取），重试
            if (errno != EINTR && errno != EAGAIN)
            {
                write_log(LOG_LEVEL_ERROR, "客户端 %d 读取JSON失败: %s", client_fd, strerror(errno));
                free(json_buf);
                close(client_fd);
                return;
            }
        }
        else if (recv_len == 0)
        {
            // 客户端断开，数据不完整
            write_log(LOG_LEVEL_WARN, "客户端 %d 断开，数据不完整", client_fd);
            free(json_buf);
            memset(client_username[client_fd], 0, sizeof(client_username[client_fd]));
            close(client_fd);
            return;
        }
        else
        {
            total_read += recv_len;
        }
    }
    json_buf[data_len] = '\0'; // 添加字符串结束符

    // 第三步：解析JSON数据
    cJSON *root = cJSON_Parse(json_buf);
    free(json_buf); // 释放缓冲区（已解析，无需保留）
    if (!root)
    {
        // JSON解析失败
        write_log(LOG_LEVEL_ERROR, "客户端 %d JSON解析失败: %s", client_fd, cJSON_GetErrorPtr());
        close(client_fd);
        return;
    }

    // 第四步：提取请求类型（type字段），分发到对应业务函数
    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type))
    {
        write_log(LOG_LEVEL_WARN, "客户端 %d 请求缺少type字段", client_fd);
        cJSON_Delete(root);
        close(client_fd);
        return;
    }

    // 打印请求类型（调试用）
    printf("收到客户端 %d 请求: %s\n", client_fd, type->valuestring);
    write_log(LOG_LEVEL_INFO, "收到客户端 %d 请求: %s", client_fd, type->valuestring);

    // 根据请求类型分发处理
    if (strcmp(type->valuestring, "login") == 0)
    {
        handle_login(client_fd, root, client_addr);
    }
    else if (strcmp(type->valuestring, "register") == 0)
    {
        handle_register(client_fd, root);
    }
    else if (strcmp(type->valuestring, "list") == 0)
    {
        handle_file_list(client_fd, root);
    }
    else if (strcmp(type->valuestring, "upload") == 0)
    {
        handle_upload_ctl(client_fd, root);
    }
    else if (strcmp(type->valuestring, "download") == 0)
    {
        handle_download_ctl(client_fd, root);
    }
    else if (strcmp(type->valuestring, "ready_to_receive") == 0)
    {
        // 客户端确认准备好，进入发送状态
        client_dl_info[client_fd].state = DL_STATE_SENDING;
        struct epoll_event ev;
        ev.events = EPOLLOUT | EPOLLET;
        ev.data.fd = client_fd;
        epoll_ctl(epfd, EPOLL_CTL_MOD, client_fd, &ev);
        printf("客户端 %d 准备好接收数据，切换为EPOLLOUT\n", client_fd);
    }
    else if (strcmp(type->valuestring, "delete") == 0)
    {
        handle_delete(client_fd, root, client_addr);
    }
    else if (strcmp(type->valuestring, "history_query") == 0)
    {
        handle_history_query(client_fd, root); // 历史记录查询
    }
    else if (strcmp(type->valuestring, "share") == 0)
    {
        handle_share(client_fd, root);
    }
    else if (strcmp(type->valuestring, "share_response") == 0)
    {
        handle_share_response(client_fd, root);
    }
    else
    {
        // 未知请求类型，返回错误
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "type", "error");
        cJSON_AddStringToObject(res, "message", "未知请求");
        send_json_response(client_fd, res);
        cJSON_Delete(res);
        write_log(LOG_LEVEL_WARN, "客户端 %d 未知请求类型: %s", client_fd, type->valuestring);
    }

    cJSON_Delete(root); // 释放cJSON对象内存
}
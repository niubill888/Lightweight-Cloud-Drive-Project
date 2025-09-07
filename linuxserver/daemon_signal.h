#ifndef DAEMON_SIGNAL_H
#define DAEMON_SIGNAL_H

#include "cloud_disk.h"

/**
 * @brief 将进程转为守护进程（脱离终端、后台运行）
 * @param log_file 日志文件路径（守护进程的输出重定向）
 * @return 无返回值
 */
void daemonize(const char *log_file);

/**
 * @brief 信号处理函数（捕获SIGINT、SIGTERM、SIGPIPE）
 * @param signo 捕获到的信号编号
 * @return 无返回值
 */
void signal_handler(int signo);

#endif // DAEMON_SIGNAL_H
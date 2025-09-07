# 编译器与选项
CC = gcc
CFLAGS = -Wall -g -std=c99 -D_POSIX_C_SOURCE=199309L
LDFLAGS = -lpthread -lm -lcjson -lmysqlclient

# 目标与源文件（自动获取所有.c文件）
TARGET = cloud_disk_server
SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

# 默认目标
all: $(TARGET)

# 链接
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "编译完成：$@"

# 编译规则（利用Makefile自动推导）
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 清理
clean:
	rm -f $(OBJS) $(TARGET)
	@echo "清理完成"

# 运行与安装（保留常用功能）
run: $(TARGET)
	./$< -f

install: $(TARGET)
	sudo cp $< /usr/local/bin/
	@echo "安装完成：$(TARGET)"

uninstall:
	sudo rm -f /usr/local/bin/$(TARGET)
	@echo "卸载完成"
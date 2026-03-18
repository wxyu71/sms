# Makefile — 智能家居项目构建脚本
#
# 用法：
#   make          构建可执行文件 build/main
#   make clean    清除 build 目录
#
# 交叉编译（ARM 嵌入式）：
#   make CC=arm-linux-gnueabihf-gcc

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -g -Iinclude
TARGET  = build/main

# 所有参与编译的源文件
SRCS    = src/core/main.c \
          src/core/lcd.c  \
          src/core/touch.c \
          src/core/ui.c   \
          src/modules/led_beep.c \
          src/modules/music.c    \
          src/modules/sensor.c   \
          src/modules/photo.c

# 从源文件自动生成目标文件列表
OBJS    = $(patsubst src/%.c,build/%.o,$(SRCS))
DEPS    = $(OBJS:.o=.d)

.PHONY: all clean

all: $(TARGET)

# 链接
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# 编译并生成头文件依赖
build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

-include $(DEPS)

clean:
	rm -rf build

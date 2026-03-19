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
LDFLAGS =
TARGET  = build/main
CLIENT_TARGET = build/client
SERVER_TARGET = build/mock_server
ASR_SERVER_TARGET = build/asr_server

# 可选：启用讯飞离线语法识别（需要 SDK 头文件 + 库）
# 用法示例：
#   make ASR=1 IFLYTEK_INC=include IFLYTEK_LIB=libs/arm
ASR ?= 0
IFLYTEK_INC ?= include
# 默认库目录选择：优先 ARM，其次 x64/x86（你也可以显式传 IFLYTEK_LIB=... 覆盖）
IFLYTEK_LIB ?= $(if $(wildcard libs/arm/libmsc.so),libs/arm,$(if $(wildcard libs/x64/libmsc.so),libs/x64,libs/x86))

ifeq ($(ASR),1)
CFLAGS  += -DASR_WITH_IFLYTEK_SDK -I$(IFLYTEK_INC)
LDFLAGS += -L$(IFLYTEK_LIB) -lmsc -ldl -lpthread -lm

ifneq ($(strip $(ASR_APPID)),)
CFLAGS  += -DASR_APPID=\"$(ASR_APPID)\"
endif
endif

# 所有参与编译的源文件
SRCS    = src/core/main.c \
          src/core/lcd.c  \
          src/core/touch.c \
          src/core/ui.c   \
          src/core/asr.c  \
          src/modules/led_beep.c \
          src/modules/music.c    \
          src/modules/sensor.c   \
          src/modules/photo.c

# 从源文件自动生成目标文件列表
OBJS    = $(patsubst src/%.c,build/%.o,$(SRCS))
DEPS    = $(OBJS:.o=.d)

.PHONY: all clean client server asr_server

all: $(TARGET)

client: $(CLIENT_TARGET)

server: $(SERVER_TARGET)

asr_server: $(ASR_SERVER_TARGET)

# 链接
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(CLIENT_TARGET): build/client/main.o build/client/client.o
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

build/client/%.o: client/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(SERVER_TARGET): build/tools/mock_server.o
	$(CC) $(CFLAGS) -o $@ $^

$(ASR_SERVER_TARGET): build/tools/asr_server.o build/core/asr.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/tools/%.o: tools/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# 编译并生成头文件依赖
build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

-include $(DEPS)

clean:
	rm -rf build

#define _XOPEN_SOURCE 700

#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "asr.h"

static int read_full(int fd, void *buf, size_t len)
{
    size_t done = 0;
    while (done < len) {
        ssize_t r = read(fd, (char *)buf + done, len - done);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
            return -1;
        done += (size_t)r;
    }
    return 0;
}

static int write_full(int fd, const void *buf, size_t len)
{
    size_t done = 0;
    while (done < len) {
        ssize_t w = write(fd, (const char *)buf + done, len - done);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (w == 0)
            return -1;
        done += (size_t)w;
    }
    return 0;
}

static int recv_pcm_to_tmp(int conn, int file_size, char *out_path, size_t out_cap)
{
    /* 写到临时文件，让现有 asr 模块按路径识别 */
    const char *tmpl = "/tmp/asr_pcm_XXXXXX";
    if (out_cap < strlen(tmpl) + 1)
        return -1;

    snprintf(out_path, out_cap, "%s", tmpl);
    int fd = mkstemp(out_path);
    if (fd < 0)
        return -1;

    int left = file_size;
    char buf[4096];
    while (left > 0) {
        int chunk = left > (int)sizeof(buf) ? (int)sizeof(buf) : left;
        ssize_t r = read(conn, buf, (size_t)chunk);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            unlink(out_path);
            return -1;
        }
        if (r == 0) {
            close(fd);
            unlink(out_path);
            return -1;
        }
        if (write_full(fd, buf, (size_t)r) != 0) {
            close(fd);
            unlink(out_path);
            return -1;
        }
        left -= (int)r;
    }

    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[1]);
        return 1;
    }

    /* 初始化讯飞离线语法识别（只做一次） */
    int ret = voice_init();
    if (ret != 0) {
        fprintf(stderr, "voice_init failed: %d (did you build with ASR=1 and provide libmsc?)\n", ret);
        return 2;
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        voice_deinit();
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind");
        close(listen_fd);
        voice_deinit();
        return 1;
    }

    if (listen(listen_fd, 8) != 0) {
        perror("listen");
        close(listen_fd);
        voice_deinit();
        return 1;
    }

    printf("asr_server listening on %d\n", port);
    printf("Protocol: [4-byte int file_size][file_size bytes pcm] -> [4-byte int id]\n");

    while (1) {
        int conn = accept(listen_fd, NULL, NULL);
        if (conn < 0) {
            if (errno == EINTR)
                continue;
            perror("accept");
            break;
        }

        while (1) {
            int file_size = 0;
            if (read_full(conn, &file_size, 4) != 0)
                break;

            if (file_size <= 0 || file_size > (50 * 1024 * 1024)) {
                fprintf(stderr, "bad file_size=%d\n", file_size);
                break;
            }

            char pcm_path[64];
            if (recv_pcm_to_tmp(conn, file_size, pcm_path, sizeof(pcm_path)) != 0) {
                perror("recv pcm");
                break;
            }

            int asr_ret = voice_identify(pcm_path);
            int id = voice_get_last_id();
            unlink(pcm_path);

            if (asr_ret != 0 || id < 0) {
                /* 识别失败：返回 -1 给 client */
                id = -1;
            }

            if (write_full(conn, &id, 4) != 0)
                break;

            printf("handled one pcm (%d bytes), id=%d\n", file_size, id);
        }

        close(conn);
    }

    close(listen_fd);
    voice_deinit();
    return 0;
}

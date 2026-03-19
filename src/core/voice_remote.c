#include "voice_remote.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static pthread_t g_voice_tid;
static int g_voice_started = 0;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_pending_id = 0;
static int g_has_pending = 0;

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

static int connect_server(const char *ip, int port)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        return -1;

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)port);
    if (inet_aton(ip, &server_addr.sin_addr) == 0) {
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        close(sockfd);
        return -1;
    }

    return sockfd;
}

static int send_pcm_file(int sockfd, const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    int file_size = (int)lseek(fd, 0, SEEK_END);
    if (file_size <= 0) {
        close(fd);
        return -1;
    }

    if (lseek(fd, 0, SEEK_SET) < 0) {
        close(fd);
        return -1;
    }

    if (write_full(sockfd, &file_size, 4) != 0) {
        close(fd);
        return -1;
    }

    char buf[4096];
    while (1) {
        ssize_t r = read(fd, buf, sizeof(buf));
        if (r < 0) {
            close(fd);
            return -1;
        }
        if (r == 0)
            break;
        if (write_full(sockfd, buf, (size_t)r) != 0) {
            close(fd);
            return -1;
        }
    }

    close(fd);
    return 0;
}

static void publish_id(int id)
{
    pthread_mutex_lock(&g_lock);
    g_pending_id = id;
    g_has_pending = 1;
    pthread_mutex_unlock(&g_lock);
}

static void *voice_thread(void *arg)
{
    char ip_buf[64];
    memset(ip_buf, 0, sizeof(ip_buf));
    if (arg != NULL) {
        snprintf(ip_buf, sizeof(ip_buf), "%s", (const char *)arg);
        free(arg);
    }

    const char *ip = ip_buf;

    const char *port_str = getenv("VOICE_SERVER_PORT");
    int port = port_str ? atoi(port_str) : 9000;

    /* 录音输出文件放当前目录，避免 /tmp 挂载差异 */
    const char *pcm_path = "1.pcm";

    while (1) {
        int sockfd = connect_server(ip, port);
        if (sockfd < 0) {
            sleep(1);
            continue;
        }

        while (1) {
            /* 1) 录音 4s */
            system("arecord -d4 -c1 -r16000 -traw -fS16_LE 1.pcm");

            /* 2) 发送 PCM */
            if (send_pcm_file(sockfd, pcm_path) != 0) {
                close(sockfd);
                break;
            }

            /* 3) 接收识别 id */
            int id = 0;
            if (read_full(sockfd, &id, 4) != 0) {
                close(sockfd);
                break;
            }

            publish_id(id);
        }
    }

    return NULL;
}

int voice_remote_start_from_env(void)
{
    if (g_voice_started)
        return 0;

    const char *ip = getenv("VOICE_SERVER_IP");
    if (ip == NULL || ip[0] == '\0')
        return -1;

    /* 复制一份 ip，避免环境变量生命周期问题 */
    char *ip_copy = strdup(ip);
    if (ip_copy == NULL)
        return -1;

    int ret = pthread_create(&g_voice_tid, NULL, voice_thread, ip_copy);
    if (ret != 0) {
        free(ip_copy);
        return -1;
    }

    pthread_detach(g_voice_tid);
    g_voice_started = 1;
    return 0;
}

int voice_remote_poll_id(int *out_id)
{
    if (out_id == NULL)
        return 0;

    pthread_mutex_lock(&g_lock);
    int has = g_has_pending;
    int id = g_pending_id;
    g_has_pending = 0;
    pthread_mutex_unlock(&g_lock);

    if (!has)
        return 0;

    *out_id = id;
    return 1;
}

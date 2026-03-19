#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

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

static int drain_bytes(int fd, int bytes)
{
    char buf[4096];
    int left = bytes;
    while (left > 0) {
        int chunk = left > (int)sizeof(buf) ? (int)sizeof(buf) : left;
        ssize_t r = read(fd, buf, (size_t)chunk);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
            return -1;
        left -= (int)r;
    }
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

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
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
        return 1;
    }

    if (listen(listen_fd, 8) != 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    printf("mock_server listening on %d\n", port);
    printf("Protocol: [4-byte int file_size][file_size bytes pcm] -> [4-byte int id]\n");

    int next_id = 1;

    while (1) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);
        int conn = accept(listen_fd, (struct sockaddr *)&cli, &cli_len);
        if (conn < 0) {
            if (errno == EINTR)
                continue;
            perror("accept");
            break;
        }

        char ip[64];
        snprintf(ip, sizeof(ip), "%s", inet_ntoa(cli.sin_addr));
        printf("client connected: %s:%d\n", ip, ntohs(cli.sin_port));

        while (1) {
            int file_size = 0;
            if (read_full(conn, &file_size, 4) != 0) {
                printf("client disconnected\n");
                break;
            }
            if (file_size <= 0 || file_size > (50 * 1024 * 1024)) {
                printf("bad file_size=%d, closing\n", file_size);
                break;
            }

            if (drain_bytes(conn, file_size) != 0) {
                printf("failed to read pcm data, closing\n");
                break;
            }

            int id = next_id;
            next_id++;
            if (next_id > 3)
                next_id = 1;

            if (write_full(conn, &id, 4) != 0) {
                printf("failed to write id, closing\n");
                break;
            }

            printf("handled one pcm (%d bytes), reply id=%d\n", file_size, id);
        }

        close(conn);
    }

    close(listen_fd);
    return 0;
}

#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h> 
#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>


volatile int cmd = 0; //保存接收到的指令
volatile int voice_id = 0; //保存接收到的语音id

static int read_full(int fd, void *buf, size_t len)
{
    size_t done = 0;
    while (done < len) {
        ssize_t r = read(fd, (char *)buf + done, len - done);
        if (r <= 0)
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
        if (w <= 0)
            return -1;
        done += (size_t)w;
    }
    return 0;
}

/*
    init_client:初始化客户端
*/
int init_client(const char *ip, int port)
{
    //1.创建socket套接字
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd == -1)
    {
        perror("socket fail");
        return -1;
    }

    //2.connect连接服务器
    struct sockaddr_in  server_addr;
    socklen_t server_addr_len = sizeof(server_addr);
    server_addr.sin_family = AF_INET; //ipv4
    server_addr.sin_port = htons(port); //绑定端口号
    inet_aton(ip, &server_addr.sin_addr); //绑定ip
    int ret = connect(sockfd, ( struct sockaddr *)&server_addr, server_addr_len);
    if(ret == -1)
    {
        perror("connect fail");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

/*
    read_server_cmd:读取服务端发送的指令
*/
void *read_server_cmd(void *arg) // void *arg = &sockfd;
{
    int sockfd = *(int*)arg;
    while(1)
    {
        char buf[1024] = {0};
        int ret = read(sockfd, buf, 1024);
        if(ret < 0)
        {
            perror("read fail");
            return NULL;
        }
        else if(ret > 0)
        {
            printf("buf:%s\n", buf);
        }

        if(strncmp(buf, "picture", 7) == 0) //cmd==1,控制进电子相册
        { 
            cmd = 1;
        }
        else if(strncmp(buf, "prev", 4) == 0) //上
        { 
            cmd = 2;
        }
        else if(strncmp(buf, "next", 4) == 0) //下
        { 
            cmd = 3;
        }
        else if(strncmp(buf, "main", 4) == 0) //回到主界面
        { 
            cmd = 4;
        }
        else if(strncmp(buf, "break", 5) == 0) //退出继续模式选择
        { 
            cmd = 5;
        }
    }
}

void *voice_function(void *arg)
{
    int sockfd = *(int*)arg;
    while(1)
    {
        //1.录音
        printf("请录音4s....\n");
        system("arecord -d4 -c1 -r16000 -traw -fS16_LE 1.pcm");

        //2.把文件数据发送给服务器
        //打开音频文件
        int voice_fd = open("1.pcm", O_RDONLY);
        if(voice_fd == -1)
        {
            perror("open 1.pcm fail");
            return NULL;
        }
        //发送音频文件的大小
        int file_size = lseek(voice_fd, 0, SEEK_END);
        if (file_size < 0) {
            perror("lseek fail");
            close(voice_fd);
            return NULL;
        }
        if (write_full(sockfd, &file_size, 4) != 0) {
            perror("write file_size fail");
            close(voice_fd);
            return NULL;
        }

        lseek(voice_fd, 0, SEEK_SET); //光标定位到文件开头
        while(1)
        {
            //读取音频文件的数据
            char write_buf[128] = {0};
            int ret = read(voice_fd, write_buf, 128);
            if(ret < 0)
            {
                perror("read fail");
                return NULL;
            }
            else if(ret > 0) //读成功了
            {
                if (write_full(sockfd, write_buf, (size_t)ret) != 0) {
                    perror("write voice data fail");
                    close(voice_fd);
                    return NULL;
                }
            }
            else if(ret == 0) //读到了文件末尾,已经无内容可读
            {
                break;
            }
        }

        close(voice_fd); //关闭音频文件
        
        //3.接收服务器发送过来的识别后的id号
        int recv_id = -1;
        if (read_full(sockfd, &recv_id, 4) != 0) {
            perror("read id fail");
            return NULL;
        }

        voice_id = recv_id;
        cmd = recv_id;
    }
}


#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include "client.h"

int main(int argc, char *argv[]) // ./a.out 192.168.31.100 9000
{
    if(argc != 3)
    {
        printf("Usage: %s <ip> <port>\n", argv[0]);
        return -1;
    }

  	//初始化客户端
    int sock_fd = init_client(argv[1], atoi(argv[2])); 

    //创建线程实现语音控制
    pthread_t voice_tid = -1;
    pthread_create(&voice_tid, NULL, voice_function, &sock_fd);
    // printf("%s  %d\n", __FUNCTION__, __LINE__);
    
    while(1)
    {
		if(cmd == 1) //进入电子相册控制页面
		{
			cmd = -1;
		    printf("photo\n");
		}
		else if(cmd == 2)
		{
			cmd = -1;
			printf("prev\n");
		}
		else if(cmd == 3)
		{
			cmd = -1;
			printf("next\n");
		}
    }
}



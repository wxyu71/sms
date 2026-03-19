#ifndef __CLIENT_H__
#define __CLIENT_H__

extern volatile int cmd;
extern volatile int voice_id;

int init_client(const char *ip, int port);
void *read_server_cmd(void *arg);
void *voice_function(void *arg);

#endif
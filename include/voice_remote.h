/**
 * voice_remote.h — 板端语音远程控制（录音→发到虚拟机→收识别 id）
 *
 * 设计目标：
 * - 与主 UI（LCD/触摸）整合：后台线程工作，前台轮询获取最新 id
 * - 不引入新 UI：是否启用由环境变量决定
 *
 * 环境变量：
 *   VOICE_SERVER_IP   : 虚拟机 IP（例如 192.168.31.100）
 *   VOICE_SERVER_PORT : 端口（例如 9000）
 *
 * 协议：与 client 一致：
 *   发送 [4字节 int file_size] + [PCM字节流]
 *   接收 [4字节 int id]
 */

#ifndef VOICE_REMOTE_H
#define VOICE_REMOTE_H

#ifdef __cplusplus
extern "C" {
#endif

/** 启动后台语音线程（从环境变量读取配置）。未配置则不启动并返回 -1。 */
int voice_remote_start_from_env(void);

/** 获取并清空最近一次收到的语音 id；无新数据返回 0。 */
int voice_remote_poll_id(int *out_id);

#ifdef __cplusplus
}
#endif

#endif /* VOICE_REMOTE_H */

/**
 * asr.h — 语音识别模块（可选：讯飞离线语法识别）
 *
 * 默认情况下（未定义 ASR_WITH_IFLYTEK_SDK），本模块会编译为 stub，
 * 以保证项目在未集成讯飞 SDK 头文件/库时也能正常构建。
 *
 * 若已集成讯飞 SDK：
 *   - 编译时增加：-DASR_WITH_IFLYTEK_SDK
 *   - 并确保能包含 qisr.h / msp_cmn.h / msp_errors.h
 *   - 链接对应的 libmsc 等库（按你的 SDK 文档配置）
 */

#ifndef ASR_H
#define ASR_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * voice_init - 初始化语音识别（登录 + 构建离线语法网络）
 *
 * 返回 0 表示成功；非 0 表示失败（stub 模式下固定失败）。
 */
int voice_init(void);

/**
 * voice_identify - 对指定 PCM 文件进行离线语法识别，并缓存识别出的 id
 *
 * @pcm_path: 16K 单声道 PCM 路径；传 NULL 则使用默认示例路径。
 * 返回 0 表示识别流程成功结束；非 0 表示识别/SDK 调用失败。
 *
 * 识别成功后，可调用 voice_get_last_id() 获取解析出来的 id（解析失败返回 -1）。
 */
int voice_identify(const char *pcm_path);

/**
 * voice_get_last_id - 获取最近一次识别解析到的 id
 *
 * 返回 >=0 表示有效 id；返回 -1 表示没有有效结果/解析失败。
 */
int voice_get_last_id(void);

/**
 * voice_deinit - 释放语音识别资源（登出）
 */
void voice_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* ASR_H */

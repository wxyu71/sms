/**
 * music.h — 音乐播放器模块接口（基于 madplay）
 *
 * 子界面按钮布局（参考设计图 image-20260316104728395.png）：
 *
 *   上方区域：歌曲信息显示区
 *   下方按钮：[上一首] [暂停] [继续] [下一首] [退出]
 *
 * 集成说明：
 *   music.c 已实现完整的 madplay 进程管理逻辑，
 *   将 mp3 文件列表路径替换为实际路径后即可直接运行。
 */

#ifndef MUSIC_H
#define MUSIC_H

/**
 * module_music - 音乐播放器子界面入口
 *
 * 内部维护自己的事件循环，点击"退出"后关闭 madplay 进程并返回。
 */
void module_music(void);

#endif /* MUSIC_H */

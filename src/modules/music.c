/**
 * music.c — 音乐播放器模块
 *
 * 当前为接口占位实现，供主界面集成调试使用。
 * 后续由同事用完整实现文件直接替换本文件即可，
 * 接口签名（module_music）与头文件 music.h 保持不变。
 *
 * 集成约定：
 *   - 函数名：module_music(void)
 *   - 行为：进入子界面事件循环（上一首/暂停/继续/下一首/退出）
 *   - 退出前须停止 madplay 进程
 *   - 依赖：lcd.h（绘图）、touch.h（输入）、ui.h（按钮）
 */

#include "music.h"
#include <stdio.h>

void module_music(void)
{
    /* 占位：等同事替换此文件后，此处改为完整子界面逻辑 */
    printf("[music] module not yet implemented, returning to main.\n");
}

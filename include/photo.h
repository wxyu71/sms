/**
 * photo.h — 电子相册模块接口
 *
 * 子界面按钮布局（参考设计图 image-20260316105353288.png）：
 *
 *   上方区域：BMP 图片显示区
 *   下方按钮：[上一张] [退出] [下一张]
 *
 * 支持两种翻页方式：
 *   1. 点击按钮（上一张 / 下一张）
 *   2. 左右滑动手势（左滑→下一张，右滑→上一张）
 *
 * 集成说明：
 *   将 photo.c 中 BMP_LIST 替换为实际 BMP 文件路径，
 *   lcd_display_bmp() 替换为实际 BMP 解码渲染实现即可。
 */

#ifndef PHOTO_H
#define PHOTO_H

/**
 * module_photo - 电子相册子界面入口
 *
 * 显示 BMP 图片并响应翻页操作，点击"退出"后返回主界面。
 */
void module_photo(void);

#endif /* PHOTO_H */

/**
 * ui.h — UI 组件层接口
 *
 * 提供统一的 Button 数据结构与绘制/命中检测接口。
 * 所有模块（主界面和子界面）均使用同一套组件，避免重复实现。
 *
 * 依赖：lcd.h（颜色宏与绘图函数）
 */

#ifndef UI_H
#define UI_H

#include "lcd.h"

/* ----------------------------------------------------------------
 * Button — 按钮的完整描述
 *
 * 设计原则：
 *   - 所有 UI 状态均从此结构推导，不依赖外部中间状态
 *   - on_click 为函数指针，主界面通过统一调度路径调用各模块
 * ---------------------------------------------------------------- */
typedef struct {
    int   x, y;              /* 按钮左上角坐标（像素） */
    int   w, h;              /* 宽高（像素） */
    const char *line1;       /* 第一行标签（ASCII） */
    const char *line2;       /* 第二行标签（NULL 或空串表示单行） */
    unsigned short color_bg;      /* 按钮主色（背景填充色） */
    unsigned short color_border;  /* 高亮边框色 */
    void (*on_click)(void);       /* 点击回调：模块入口函数 */
} Button;

/**
 * ui_draw_button - 绘制一个按钮
 *
 * 视觉层次（从底到顶）：
 *   1. 外层深色阴影框（2px）—— 立体感
 *   2. 主色背景填充
 *   3. 内侧高亮描边（2px）  —— 发光 / 悬浮感
 *   4. 居中文字（单行或双行）
 */
void ui_draw_button(const Button *btn);

/**
 * ui_hit_test - 判断坐标 (x, y) 命中了哪个按钮
 *
 * @buttons : 按钮数组
 * @count   : 数组元素数量
 * 返回命中的按钮指针；未命中返回 NULL
 */
const Button *ui_hit_test(const Button *buttons, int count, int x, int y);

#endif /* UI_H */

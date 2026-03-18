/**
 * main.c — 嵌入式 Linux 智能家居主界面（调度入口）
 *
 * 文件职责（单一）：
 *   - 定义主界面按钮布局
 *   - 绘制主界面
 *   - 主事件循环：等待点击 → 命中检测 → 调用对应模块 → 返回重绘
 *
 * 分层架构：
 *   lcd.h / lcd.c     —— LCD 帧缓冲绘图层
 *   touch.h / touch.c —— 触摸屏输入层
 *   ui.h / ui.c       —— UI 组件层（Button + 命中检测）
 *   led_beep.h/.c     —— LED & 蜂鸣器控制模块
 *   music.h/.c        —— 音乐播放器模块（madplay）
 *   sensor.h/.c       —— 传感器模块（GY39 + MQ2）
 *   photo.h/.c        —— 电子相册模块（BMP 显示）
 *
 * 编译：
 *   make        （使用 Makefile）
 *   或手动：
 *   gcc -o main main.c lcd.c touch.c ui.c led_beep.c music.c sensor.c photo.c
 */

#include <stdio.h>
#include <stdlib.h>

/* 各层接口头文件 */
#include "ui.h"       /* 包含了 lcd.h */
#include "touch.h"

/* 功能模块入口 */
#include "led_beep.h"
#include "music.h"
#include "sensor.h"
#include "photo.h"

/* ================================================================
 * 一、主界面按钮布局
 *
 * 屏幕 800×480，布局：
 *   ┌──────────────────── SMART HOME SYSTEM ───────────────────────┐
 *   │                     标题栏（60px）                           │
 *   ├───────────────────────────┬──────────────────────────────────┤
 *   │  LED & BEEP（电蓝）       │  MUSIC PLAYER（紫色）            │ y=100
 *   ├───────────────────────────┼──────────────────────────────────┤
 *   │  GY39 & MQ2（绿色）       │  PHOTO ALBUM（琥珀）             │ y=280
 *   └───────────────────────────┴──────────────────────────────────┘
 *
 * 每个按钮：宽 300px，高 140px，左右边距 60px，列间距 80px
 * ================================================================ */

/**
 * 主界面按钮数组（全局只读常量）
 *
 * 布局（800×480）：
 *   列X：左=60, 右=440；行Y：上=100, 下=280；按钮尺寸 300×140
 *
 * on_click 指向各模块 .c 文件中的实现函数，
 * 集成时只需保证对应 .c 文件已编译链接，无需修改此处。
 */
static const Button g_main_buttons[] = {
    /* x,  y,   w,   h,   line1,          line2,    bg,                 border,               callback */
    { 60, 100, 300, 140, "LED & BEEP",   "",        COLOR_LED_MAIN,    COLOR_LED_BORDER,    module_led_beep },
    {440, 100, 300, 140, "MUSIC PLAYER", "",        COLOR_MUSIC_MAIN,  COLOR_MUSIC_BORDER,  module_music    },
    { 60, 280, 300, 140, "GY39 & MQ2",  "SENSOR",  COLOR_SENSOR_MAIN, COLOR_SENSOR_BORDER, module_sensor   },
    {440, 280, 300, 140, "PHOTO ALBUM",  "",        COLOR_PHOTO_MAIN,  COLOR_PHOTO_BORDER,  module_photo    },
};

#define MAIN_BUTTON_COUNT  ((int)(sizeof(g_main_buttons) / sizeof(g_main_buttons[0])))

/* ================================================================
 * 二、主界面绘制
 *
 * 渲染顺序（从底到顶）：
 *   1. 整屏背景
 *   2. 标题栏 + 分割线 + 标题文字
 *   3. 四个功能模块按钮
 *   4. 底部版本信息
 * ================================================================ */
static void draw_main_screen(void)
{
    /* 整屏深色背景 */
    lcd_fill_rect(0, 0, g_lcd_width, g_lcd_height, COLOR_BG_DARK);

    /* 标题栏（顶部 60px） */
    lcd_fill_rect(0, 0, g_lcd_width, 60, COLOR_HEADER_BG);

    /* 标题栏底部装饰线（亮蓝，3px） */
    lcd_draw_hline(0, 60, g_lcd_width, 3, COLOR_HEADER_LINE);

    /* 标题文字居中（"SMART HOME SYSTEM" = 17字符×8px=136px，居中x=(800-136)/2=332） */
    lcd_draw_string(332, 22, "SMART HOME SYSTEM", COLOR_WHITE, COLOR_HEADER_BG);

    /* 功能模块按钮 */
    for (int i = 0; i < MAIN_BUTTON_COUNT; i++)
        ui_draw_button(&g_main_buttons[i]);

    /* 底部版本信息（右下角） */
    lcd_draw_string(600, 455, "V1.0  2026", COLOR_GRAY_LIGHT, COLOR_BG_DARK);
}

/* ================================================================
 * 三、程序入口
 * ================================================================ */

int main(void)
{
    /* 初始化 LCD 帧缓冲（内部通过 ioctl 读取实际分辨率） */
    if (lcd_open() != 0) {
        fprintf(stderr, "Failed to open LCD framebuffer.\n");
        return EXIT_FAILURE;
    }

    /* 首次绘制主界面 */
    draw_main_screen();
    printf("Main interface ready. Waiting for touch input...\n");

    /* 主事件循环：等待点击 → 命中检测 → 调用模块 → 返回重绘 */
    while (1) {
        int tx, ty;

        if (touch_get_tap(&tx, &ty) != 0) {
            /* 触摸设备异常，安全退出 */
            fprintf(stderr, "Touch input error, exiting.\n");
            break;
        }

        printf("Touch: (%d, %d)\n", tx, ty);

        /* 命中检测 */
        const Button *hit = ui_hit_test(g_main_buttons, MAIN_BUTTON_COUNT, tx, ty);

        if (hit != NULL) {
            printf("Module: %s\n", hit->line1);
            /* 进入子模块（模块内部维护自己的事件循环，返回后回到主界面） */
            hit->on_click();
            /* 子模块返回后重绘主界面 */
            draw_main_screen();
        }
        /* 未命中任何按钮：忽略本次点击 */
    }

    lcd_close();
    return EXIT_SUCCESS;
}


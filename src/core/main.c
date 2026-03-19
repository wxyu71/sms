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
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

/* 各层接口头文件 */
#include "ui.h"       /* 包含了 lcd.h */
#include "touch.h"

/* 功能模块入口 */
#include "led_beep.h"
#include "music.h"
#include "sensor.h"
#include "photo.h"

/* ================================================================
 * 一、主界面资源与透明点击区域
 * ================================================================ */

/* 设计图资源：PNG 与 BMP 为同一图，板子上统一显示 BMP。 */
static const char *const MAIN_BMP_CANDIDATES[] = {
    "assets/images/main.bmp",
    "../assets/images/main.bmp",
    "main.bmp",
};

/* 原图尺寸（用于把热点区域按比例映射到当前 LCD 分辨率） */
#define MAIN_DESIGN_W  800
#define MAIN_DESIGN_H  480

/* 图像缓存：避免每次返回主界面都重新解码 BMP。 */
static unsigned short *g_main_bg_rgb565 = NULL;
static int g_main_bg_w = 0;
static int g_main_bg_h = 0;

/* 4 个图标的可点击热区（按设计图测量，覆盖图标+下方文字）。 */
static Button g_main_buttons[4];

/**
 * 将设计稿坐标映射到当前 LCD 分辨率。
 */
static int scale_x(int x) { return x * g_lcd_width  / MAIN_DESIGN_W; }
static int scale_y(int y) { return y * g_lcd_height / MAIN_DESIGN_H; }

/**
 * build_main_hotspots - 初始化图标点击热区
 *
 * 图标顺序（从左到右）：
 * 1) 控制中心     -> LED & BEEP
 * 2) 综合传感器   -> GY39 & MQ2
 * 3) 音乐播放器   -> MUSIC
 * 4) 相册图库     -> PHOTO
 */
static void build_main_hotspots(void)
{
    /* 以 800x480 设计稿为基准：x=90/241/392/543, y=78, w=150, h=206 */
    const int design_x[4] = { 90, 241, 392, 543 };
    const int design_y    = 78;
    const int design_w    = 150;
    const int design_h    = 206;

    g_main_buttons[0] = (Button){
        scale_x(design_x[0]), scale_y(design_y), scale_x(design_w), scale_y(design_h),
        "CONTROL", "", COLOR_LED_MAIN, COLOR_LED_BORDER, module_led_beep
    };
    g_main_buttons[1] = (Button){
        scale_x(design_x[1]), scale_y(design_y), scale_x(design_w), scale_y(design_h),
        "SENSOR", "", COLOR_SENSOR_MAIN, COLOR_SENSOR_BORDER, module_sensor
    };
    g_main_buttons[2] = (Button){
        scale_x(design_x[2]), scale_y(design_y), scale_x(design_w), scale_y(design_h),
        "MUSIC", "", COLOR_MUSIC_MAIN, COLOR_MUSIC_BORDER, module_music
    };
    g_main_buttons[3] = (Button){
        scale_x(design_x[3]), scale_y(design_y), scale_x(design_w), scale_y(design_h),
        "PHOTO", "", COLOR_PHOTO_MAIN, COLOR_PHOTO_BORDER, module_photo
    };
}

/**
 * load_bmp_scaled_rgb565 - 解码 24/32 位 BMP 并缩放到目标分辨率（RGB565）
 *
 * 实现要点：
 * 1) 使用 mmap 直接映射 BMP 文件，减少 read 拷贝开销
 * 2) 兼容 24-bit / 32-bit BMP，支持 bottom-up 与 top-down 存储
 * 3) 采用最近邻缩放，把任意分辨率背景图映射到当前 LCD 尺寸
 *
 * 参数：
 * @path  : BMP 文件路径
 * @dst   : 目标 RGB565 缓冲区（长度 >= dst_w * dst_h）
 * @dst_w : 目标宽
 * @dst_h : 目标高
 *
 * 返回 0 成功，-1 失败。
 */
static int load_bmp_scaled_rgb565(const char *path, unsigned short *dst, int dst_w, int dst_h)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 54) {
        close(fd);
        return -1;
    }

    size_t file_size = (size_t)st.st_size;
    const unsigned char *file_map = (const unsigned char *)mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (file_map == MAP_FAILED)
        return -1;

    unsigned short magic = *(const unsigned short *)(file_map + 0);
    int data_offset      = *(const int *)(file_map + 10);
    int bmp_w            = *(const int *)(file_map + 18);
    int bmp_h            = *(const int *)(file_map + 22);
    short depth          = *(const short *)(file_map + 28);

    if (magic != 0x4D42 || (depth != 24 && depth != 32) || bmp_w <= 0 || bmp_h == 0 || data_offset < 54) {
        munmap((void *)file_map, file_size);
        return -1;
    }

    int bpp = depth / 8;
    int abs_h = (bmp_h > 0) ? bmp_h : -bmp_h;
    int bottom_up = (bmp_h > 0);
    int stride = (bmp_w * bpp + 3) & ~3;
    const unsigned char *src = file_map + data_offset;

    for (int y = 0; y < dst_h; y++) {
        int src_y = y * abs_h / dst_h;
        int row = bottom_up ? (abs_h - 1 - src_y) : src_y;
        const unsigned char *row_ptr = src + row * stride;

        for (int x = 0; x < dst_w; x++) {
            int src_x = x * bmp_w / dst_w;
            const unsigned char *p = row_ptr + src_x * bpp;
            int b = p[0];
            int g = p[1];
            int r = p[2];

            dst[y * dst_w + x] = (unsigned short)(((r & 0xF8) << 8) |
                                                  ((g & 0xFC) << 3) |
                                                  (b >> 3));
        }
    }

    munmap((void *)file_map, file_size);
    return 0;
}

static int ensure_main_background_loaded(void)
{
    /* 缓存命中：尺寸一致时直接复用，避免重复解码 */
    if (g_main_bg_rgb565 != NULL && g_main_bg_w == g_lcd_width && g_main_bg_h == g_lcd_height)
        return 0;

    /* 尺寸变化或首次进入：重建缓存 */
    free(g_main_bg_rgb565);
    g_main_bg_rgb565 = (unsigned short *)malloc((size_t)g_lcd_width * g_lcd_height * sizeof(unsigned short));
    if (g_main_bg_rgb565 == NULL)
        return -1;

    g_main_bg_w = g_lcd_width;
    g_main_bg_h = g_lcd_height;

    /* 兼容不同启动目录：按候选路径依次尝试 */
    int path_count = (int)(sizeof(MAIN_BMP_CANDIDATES) / sizeof(MAIN_BMP_CANDIDATES[0]));
    for (int i = 0; i < path_count; i++) {
        if (load_bmp_scaled_rgb565(MAIN_BMP_CANDIDATES[i], g_main_bg_rgb565, g_main_bg_w, g_main_bg_h) == 0)
            return 0;
    }

    /* 全部失败则释放缓存并通知上层降级绘制 */
    free(g_main_bg_rgb565);
    g_main_bg_rgb565 = NULL;
    return -1;
}

#define MAIN_BUTTON_COUNT  ((int)(sizeof(g_main_buttons) / sizeof(g_main_buttons[0])))

/* ================================================================
 * 二、主界面绘制（显示 main.bmp）
 * ================================================================ */
static void draw_main_screen(void)
{
    if (ensure_main_background_loaded() == 0) {
        lcd_draw_image(0, 0, g_main_bg_w, g_main_bg_h, g_main_bg_rgb565);
    } else {
        /* 资源缺失时的兜底界面，避免黑屏。 */
        lcd_fill_rect(0, 0, g_lcd_width, g_lcd_height, COLOR_BG_DARK);
        lcd_draw_string(220, 210, "MAIN BMP NOT FOUND", COLOR_WHITE, COLOR_BG_DARK);
    }
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

    /* 先根据当前 LCD 分辨率构建点击热区，再绘制主界面。 */
    build_main_hotspots();
    draw_main_screen();
    printf("Main interface ready. Waiting for touch input...\n");

    /*
     * 主事件循环：
     * 1) 阻塞等待一次有效点击
     * 2) 按透明热区做命中检测
     * 3) 命中则进入对应子模块
     * 4) 子模块返回后重绘主界面
     */
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
            /*
             * 进入子模块：
             * 子模块内部维护自己的事件循环（直到点击退出），
             * 返回后继续回到主循环。
             */
            hit->on_click();
            /* 子模块返回后重绘主界面 */
            draw_main_screen();
        }
        /* 未命中任何按钮：忽略本次点击 */
    }

    free(g_main_bg_rgb565);
    g_main_bg_rgb565 = NULL;
    lcd_close();
    return EXIT_SUCCESS;
}


/**
 * photo.c — 电子相册模块实现 (高性能优化版)
 *
 * 核心优化：
 * 1. mmap 零拷贝加载：将 BMP 文件直接映射到进程地址空间，省去 read() 的内核→用户拷贝。
 * 2. 内存缓存池：延迟加载（Lazy Loading），缩放后的 RGB565 数据驻留内存，切换零延迟。
 * 3. 双线性插值：使用定点数算术（Fixed-point arithmetic）代替浮点运算，消除锯齿，提升画质。
 * 4. 软件双缓冲转场：动画帧在内存中逐行 memcpy 合成后整块推送 LCD，消除屏幕撕裂。
 */

#include "photo.h"
#include "ui.h"
#include "touch.h"
#include "lcd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>   /* mmap / munmap */
#include <sys/stat.h>   /* fstat */

/* ----------------------------------------------------------------
 * 资源配置
 * ---------------------------------------------------------------- */
static const char *const BMP_LIST[] = {
    "1.bmp",
};
#define BMP_COUNT  ((int)(sizeof(BMP_LIST) / sizeof(BMP_LIST[0])))

#define IMG_X  0
#define IMG_Y  58
#define IMG_W  800
#define IMG_H  302

/* 内存缓存池：存储解码并缩放后的 RGB565 像素数据 */
static unsigned short *g_photo_cache[BMP_COUNT] = {NULL};

/* ================================================================
 * 高性能图像解码与处理算法
 * ================================================================ */

/**
 * 辅助函数：从源缓冲区安全获取像素颜色 (处理越界)
 */
static inline void get_pixel_bgr(const unsigned char *buf, int x, int y, int w, int h, int stride, int bpp, int *r, int *g, int *b) {
    if (x >= w) x = w - 1;
    if (y >= h) y = h - 1;
    const unsigned char *p = buf + y * stride + x * bpp;
    *b = p[0];
    *g = p[1];
    *r = p[2];
}

/**
 * load_and_scale_bmp - 加载并执行定点数双线性插值缩放
 * 返回 0 成功，-1 失败
 */
/**
 * load_and_scale_bmp - 将 BMP 文件解码并双线性插值缩放到目标尺寸
 *
 * 使用 mmap 零拷贝技术：内核将文件页直接映射到进程地址空间，
 * 省去 read() 的内核→用户空间拷贝，同时 OS 页缓存自动按需换入，
 * 对大文件内存占用更友好。
 *
 * 返回 0 成功，-1 失败。
 */
static int load_and_scale_bmp(const char *path, unsigned short *dst_buf, int dst_w, int dst_h)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    /* 获取文件大小，用于 mmap 长度参数 */
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 54) { close(fd); return -1; }
    size_t file_size = (size_t)st.st_size;

    /* 将整个文件映射到只读内存页 —— 零拷贝，无额外 malloc */
    unsigned char *file_map = (unsigned char *)mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);   /* mmap 建立后 fd 即可关闭，映射独立于文件描述符 */
    if (file_map == MAP_FAILED) return -1;

    /* 直接从映射内存中解析 BMP 头，无需 lseek / read */
    unsigned short magic      = *(unsigned short *)(file_map + 0);
    int            data_offset = *(int           *)(file_map + 10);
    int            bmp_w      = *(int           *)(file_map + 18);
    int            bmp_h      = *(int           *)(file_map + 22);
    short          depth      = *(short          *)(file_map + 28);

    if (magic != 0x4D42 || (depth != 24 && depth != 32)) {
        munmap(file_map, file_size);
        return -1;
    }

    int real_h    = (bmp_h < 0) ? -bmp_h : bmp_h;
    int bottom_up = (bmp_h > 0);
    int bpp       = depth / 8;
    int row_stride = (bmp_w * bpp + 3) & ~3;

    /* 像素数据指针直接指向映射区域，无需二次拷贝 */
    const unsigned char *src_buf = file_map + data_offset;

    /* ---- 定点数双线性插值 (Fixed-point Bilinear Interpolation) ---- */
    /* 使用 16 位定点小数精度，避免浮点除法，适配无 FPU 的嵌入式 CPU */
    unsigned int x_ratio = ((bmp_w - 1) << 16) / dst_w;
    unsigned int y_ratio = ((real_h - 1) << 16) / dst_h;

    for (int sy = 0; sy < dst_h; sy++) {
        unsigned int y_mapped = (unsigned int)sy * y_ratio;
        int src_y  = (int)(y_mapped >> 16);
        int y_diff = (int)((y_mapped >> 8) & 0xFF);  /* 8 位小数权重 */

        int row_idx      = bottom_up ? (real_h - 1 - src_y) : src_y;
        int row_idx_next = bottom_up ? (row_idx - 1) : (row_idx + 1);
        if (row_idx_next < 0)      row_idx_next = 0;
        if (row_idx_next >= real_h) row_idx_next = real_h - 1;

        for (int sx = 0; sx < dst_w; sx++) {
            unsigned int x_mapped = (unsigned int)sx * x_ratio;
            int src_x  = (int)(x_mapped >> 16);
            int x_diff = (int)((x_mapped >> 8) & 0xFF);

            /* 四邻域双线性权重（256 归一化） */
            int w_A = ((256 - x_diff) * (256 - y_diff)) >> 8;
            int w_B = (x_diff         * (256 - y_diff)) >> 8;
            int w_C = ((256 - x_diff) * y_diff)         >> 8;
            int w_D = (x_diff         * y_diff)         >> 8;

            int rA, gA, bA, rB, gB, bB, rC, gC, bC, rD, gD, bD;
            get_pixel_bgr(src_buf, src_x,     row_idx,      bmp_w, real_h, row_stride, bpp, &rA, &gA, &bA);
            get_pixel_bgr(src_buf, src_x + 1, row_idx,      bmp_w, real_h, row_stride, bpp, &rB, &gB, &bB);
            get_pixel_bgr(src_buf, src_x,     row_idx_next, bmp_w, real_h, row_stride, bpp, &rC, &gC, &bC);
            get_pixel_bgr(src_buf, src_x + 1, row_idx_next, bmp_w, real_h, row_stride, bpp, &rD, &gD, &bD);

            int r = (rA * w_A + rB * w_B + rC * w_C + rD * w_D) >> 8;
            int g = (gA * w_A + gB * w_B + gC * w_C + gD * w_D) >> 8;
            int b = (bA * w_A + bB * w_B + bC * w_C + bD * w_D) >> 8;

            /* BGR888 → RGB565 打包 */
            dst_buf[sy * dst_w + sx] = (unsigned short)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
    }

    munmap(file_map, file_size);
    return 0;
}

/* ================================================================
 * UI 控制与渲染逻辑 (含平滑动画引擎)
 * ================================================================ */

#define PBW 150
#define PBH  60
#define PBY 390

/* 与其它模块统一的浅色页面背景风格 */
#define PHOTO_BG_COLOR          RGB565(236, 236, 236)
#define PHOTO_HEADER_BG_COLOR   RGB565(236, 236, 236)
#define PHOTO_TEXT_COLOR        RGB565( 24,  24,  24)
#define PHOTO_SUBTEXT_COLOR     RGB565( 88,  88,  88)

typedef enum { PBTN_PREV = 0, PBTN_EXIT, PBTN_NEXT, PBTN_COUNT } PhotoBtnId;

static const Button s_buttons[PBTN_COUNT] = {
    { 80, PBY, PBW, PBH, "PREV", "", COLOR_PHOTO_MAIN,  COLOR_PHOTO_BORDER,  NULL},
    {325, PBY, PBW, PBH, "EXIT", "", COLOR_BTN_EXIT,    COLOR_BTN_EXIT_BDR,  NULL},
    {570, PBY, PBW, PBH, "NEXT", "", COLOR_PHOTO_MAIN,  COLOR_PHOTO_BORDER,  NULL},
};

static void draw_frame(void)
{
    lcd_fill_rect(0, 0, g_lcd_width, g_lcd_height, PHOTO_BG_COLOR);
    lcd_fill_rect(0, 0, g_lcd_width, 55, PHOTO_HEADER_BG_COLOR);
    lcd_draw_hline(0, 55, g_lcd_width, 3, COLOR_PHOTO_BORDER);
    lcd_draw_string(324, 20, "PHOTO ALBUM", PHOTO_TEXT_COLOR, PHOTO_HEADER_BG_COLOR);

    for (int i = 0; i < PBTN_COUNT; i++)
        ui_draw_button(&s_buttons[i]);
}

/**
 * 模块化拆分 1：专职处理数据加载（懒加载，首次访问时才解码）
 */
static void preload_photo(int idx)
{
    if (g_photo_cache[idx] == NULL) {
        g_photo_cache[idx] = (unsigned short *)malloc(IMG_W * IMG_H * sizeof(unsigned short));
        if (g_photo_cache[idx] != NULL) {
            if (load_and_scale_bmp(BMP_LIST[idx], g_photo_cache[idx], IMG_W, IMG_H) != 0) {
                memset(g_photo_cache[idx], 0, IMG_W * IMG_H * sizeof(unsigned short));
            }
        }
    }
}

/**
 * 模块化拆分 2：专职处理底部状态栏更新
 */
static void draw_photo_info(int idx)
{
    char info[16];
    snprintf(info, sizeof(info), "%d / %d", idx + 1, BMP_COUNT);
    int len = (int)strlen(info);
    lcd_fill_rect(0, 365, g_lcd_width, 20, PHOTO_BG_COLOR);
    lcd_draw_string((g_lcd_width - len * 8) / 2, 365, info, PHOTO_SUBTEXT_COLOR, PHOTO_BG_COLOR);
}

/**
 * 模块化拆分 3：核心转场动画引擎（软件双缓冲滑动切换）
 *
 * 原理：在内存中逐帧合成"旧图 + 新图"的水平拼接画面，再整块推送到 LCD，
 *       避免直接在显存上写像素导致的撕裂感。
 *
 * @param old_idx  当前显示的图片索引
 * @param new_idx  目标图片索引
 * @param dir_left 1:向左滑（看下一张）  0:向右滑（看上一张）
 */
static void slide_photo(int old_idx, int new_idx, int dir_left)
{
    preload_photo(old_idx);
    preload_photo(new_idx);

    unsigned short *old_img = g_photo_cache[old_idx];
    unsigned short *new_img = g_photo_cache[new_idx];

    /* 内存异常时安全降级：直接显示新图，不播放动画 */
    if (!old_img || !new_img) {
        if (new_img) lcd_draw_image(IMG_X, IMG_Y, IMG_W, IMG_H, new_img);
        return;
    }

    /* 分配一帧临时动画缓冲区（800×302×2B ≈ 483KB） */
    unsigned short *anim_buf = (unsigned short *)malloc(IMG_W * IMG_H * sizeof(unsigned short));
    if (!anim_buf) {
        /* 内存不足时安全降级，不影响功能 */
        lcd_draw_image(IMG_X, IMG_Y, IMG_W, IMG_H, new_img);
        return;
    }

    /*
     * 动画步长：每帧移动 40px，800px 宽共 20 帧。
     * LCD 整屏刷新约 20ms，则总动画时长约 400ms，手感流畅。
     * 调大步长 → 动画更快；调小步长 → 动画更细腻。
     */
    const int step = 40;

    for (int offset = step; offset < IMG_W; offset += step) {
        /* 按行内存拼接，利用 CPU 空间局部性（Spatial Locality）最大化缓存命中 */
        for (int row = 0; row < IMG_H; row++) {
            unsigned short *dest_row = anim_buf + row * IMG_W;
            unsigned short *old_row  = old_img  + row * IMG_W;
            unsigned short *new_row  = new_img  + row * IMG_W;

            if (dir_left) {
                /* 旧图向左移出，新图从右侧移入 */
                int old_w = IMG_W - offset;
                memcpy(dest_row,         old_row + offset, old_w * sizeof(unsigned short));
                memcpy(dest_row + old_w, new_row,          offset * sizeof(unsigned short));
            } else {
                /* 旧图向右移出，新图从左侧移入 */
                memcpy(dest_row,                new_row + (IMG_W - offset), offset * sizeof(unsigned short));
                memcpy(dest_row + offset,       old_row,                    (IMG_W - offset) * sizeof(unsigned short));
            }
        }
        /* 整帧一次性推送，消除撕裂 */
        lcd_draw_image(IMG_X, IMG_Y, IMG_W, IMG_H, anim_buf);
    }

    /* 动画结束，推送完整无损的新图确保最终帧对齐 */
    lcd_draw_image(IMG_X, IMG_Y, IMG_W, IMG_H, new_img);
    free(anim_buf);
}

/* 释放内存池，防止内存泄漏 */
static void cleanup_photo_cache(void)
{
    for (int i = 0; i < BMP_COUNT; i++) {
        if (g_photo_cache[i] != NULL) {
            free(g_photo_cache[i]);
            g_photo_cache[i] = NULL;
        }
    }
}

void module_photo(void)
{
    int cur  = 0;
    int next = 0;

    draw_frame();

    /* 初始化：加载并直接显示第一张图 */
    preload_photo(cur);
    if (g_photo_cache[cur])
        lcd_draw_image(IMG_X, IMG_Y, IMG_W, IMG_H, g_photo_cache[cur]);
    draw_photo_info(cur);

    while (1) {
        int x, y;
        TouchDir dir = touch_get_event(&x, &y);

        if (dir == DIR_LEFT || dir == DIR_RIGHT) {
            int dir_left = (dir == DIR_LEFT);
            next = dir_left ? (cur + 1) % BMP_COUNT : (cur - 1 + BMP_COUNT) % BMP_COUNT;
            slide_photo(cur, next, dir_left);
            cur = next;
            draw_photo_info(cur);
            continue;
        }

        if (dir == DIR_TAP) {
            const Button *hit = ui_hit_test(s_buttons, PBTN_COUNT, x, y);
            if (hit == NULL) continue;

            switch ((PhotoBtnId)(hit - s_buttons)) {
                case PBTN_PREV:
                    next = (cur - 1 + BMP_COUNT) % BMP_COUNT;
                    slide_photo(cur, next, 0); /* 0 = 向右滑，符合"上一张"的手势方向 */
                    cur = next;
                    draw_photo_info(cur);
                    break;
                case PBTN_NEXT:
                    next = (cur + 1) % BMP_COUNT;
                    slide_photo(cur, next, 1); /* 1 = 向左滑，符合"下一张"的手势方向 */
                    cur = next;
                    draw_photo_info(cur);
                    break;
                case PBTN_EXIT:
                    cleanup_photo_cache();
                    return;
                default: break;
            }
        }
    }
}
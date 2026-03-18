/**
 * touch.c — 触摸屏输入层实现
 *
 * 基于 Linux input 子系统：
 *   EV_ABS / ABS_X, ABS_Y  → 持续更新当前坐标
 *   EV_KEY / BTN_TOUCH      → 按下(1)记录起点，松开(0)判断方向
 *
 * 坐标映射：
 *   触摸屏上报的是原始 ADC 值（典型范围 0~4095），不是屏幕像素。
 *   打开设备后通过 EVIOCGABS ioctl 读取实际量程，再线性映射到
 *   屏幕像素坐标，保证命中检测与 LCD 绘图坐标系完全一致。
 */

#include "touch.h"
#include "lcd.h"   /* g_lcd_width / g_lcd_height */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>    /* select() — touch_get_tap_timeout 使用 */
#include <linux/input.h>

/* ----------------------------------------------------------------
 * 内部辅助
 * ---------------------------------------------------------------- */
static inline int abs_val(int v) { return v >= 0 ? v : -v; }

/**
 * TouchRange - 触摸屏单轴量程（从 EVIOCGABS 读取）
 */
typedef struct {
    int min;
    int max;
} TouchRange;

/**
 * touch_read_range - 读取指定轴的 ADC 量程
 *
 * 若 ioctl 失败则回退到 GEC6818 常见默认值（0~4095），
 * 避免因驱动差异导致程序崩溃。
 */
static void touch_read_range(int fd, int axis, TouchRange *r)
{
    struct input_absinfo info;
    if (ioctl(fd, EVIOCGABS(axis), &info) == 0) {
        r->min = info.minimum;
        r->max = info.maximum;
    } else {
        /* 默认回退：GEC6818 触摸屏典型 ADC 量程 */
        r->min = 0;
        r->max = 4095;
    }
}

/**
 * map_coord - 将原始 ADC 值线性映射到屏幕像素坐标
 *
 * 公式：pixel = (raw - min) * screen_size / (max - min)
 * 结果钳位到 [0, screen_size-1] 防止边缘越界。
 */
static inline int map_coord(int raw, const TouchRange *r, int screen_size)
{
    int range = r->max - r->min;
    if (range <= 0) return 0;   /* 量程异常保护 */
    int pixel = (raw - r->min) * screen_size / range;
    if (pixel < 0)            return 0;
    if (pixel >= screen_size) return screen_size - 1;
    return pixel;
}

/* ================================================================
 * touch_get_tap
 * ================================================================ */
int touch_get_tap(int *out_x, int *out_y)
{
    int fd = open(TOUCH_DEV, O_RDONLY);
    if (fd < 0) {
        perror("touch_get_tap: open " TOUCH_DEV);
        return -1;
    }

    TouchRange rx, ry;
    touch_read_range(fd, ABS_X, &rx);
    touch_read_range(fd, ABS_Y, &ry);

    struct input_event ev;
    int raw_x = 0, raw_y = 0;
    int tap_x = 0, tap_y = 0;   /* 屏幕像素坐标 */

    while (1) {
        if (read(fd, &ev, sizeof(ev)) != sizeof(ev))
            continue;

        if (ev.type == EV_ABS) {
            if      (ev.code == ABS_X) raw_x = ev.value;
            else if (ev.code == ABS_Y) raw_y = ev.value;
        }

        if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            if (ev.value == 1) {
                /* 按下：立即映射并记录起点像素坐标 */
                tap_x = map_coord(raw_x, &rx, g_lcd_width);
                tap_y = map_coord(raw_y, &ry, g_lcd_height);
            } else if (ev.value == 0) {
                /* 抬起：映射当前坐标，计算曼哈顿距离（单位：像素） */
                int cur_px = map_coord(raw_x, &rx, g_lcd_width);
                int cur_py = map_coord(raw_y, &ry, g_lcd_height);
                int dist   = abs_val(cur_px - tap_x) + abs_val(cur_py - tap_y);
                if (dist <= TAP_THRESHOLD) {
                    close(fd);
                    /* 使用按下/抬起中点，减小手抖造成的命中偏移 */
                    *out_x = (tap_x + cur_px) / 2;
                    *out_y = (tap_y + cur_py) / 2;
                    return 0;
                }
                /* 滑动 → 忽略，继续等待 */
            }
        }
    }
}

/* ================================================================
 * touch_get_event
 * ================================================================ */
TouchDir touch_get_event(int *out_x, int *out_y)
{
    int fd = open(TOUCH_DEV, O_RDONLY);
    if (fd < 0) {
        perror("touch_get_event: open " TOUCH_DEV);
        *out_x = -1;
        *out_y = -1;
        return DIR_TAP;
    }

    TouchRange rx, ry;
    touch_read_range(fd, ABS_X, &rx);
    touch_read_range(fd, ABS_Y, &ry);

    struct input_event ev;
    int raw_x = 0, raw_y = 0;
    int tap_x = 0, tap_y = 0;

    while (1) {
        if (read(fd, &ev, sizeof(ev)) != sizeof(ev))
            continue;

        if (ev.type == EV_ABS) {
            if      (ev.code == ABS_X) raw_x = ev.value;
            else if (ev.code == ABS_Y) raw_y = ev.value;
        }

        if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            if (ev.value == 1) {
                tap_x = map_coord(raw_x, &rx, g_lcd_width);
                tap_y = map_coord(raw_y, &ry, g_lcd_height);
            } else if (ev.value == 0) {
                close(fd);
                int cur_px = map_coord(raw_x, &rx, g_lcd_width);
                int cur_py = map_coord(raw_y, &ry, g_lcd_height);
                *out_x = tap_x;
                *out_y = tap_y;

                int dx     = cur_px - tap_x;
                int dy     = cur_py - tap_y;
                int abs_dx = abs_val(dx);
                int abs_dy = abs_val(dy);

                if (abs_dx < SLIDE_THRESHOLD && abs_dy < SLIDE_THRESHOLD)
                    return DIR_TAP;

                if (abs_dx >= abs_dy)
                    return (dx > 0) ? DIR_RIGHT : DIR_LEFT;
                else
                    return (dy > 0) ? DIR_DOWN  : DIR_UP;
            }
        }
    }
}

/* ================================================================
 * touch_get_tap_timeout
 * ================================================================ */
int touch_get_tap_timeout(int *out_x, int *out_y, int timeout_ms)
{
    int fd = open(TOUCH_DEV, O_RDONLY);
    if (fd < 0) {
        perror("touch_get_tap_timeout: open " TOUCH_DEV);
        return -1;
    }

    TouchRange rx, ry;
    touch_read_range(fd, ABS_X, &rx);
    touch_read_range(fd, ABS_Y, &ry);

    struct input_event ev;
    int raw_x = 0, raw_y = 0;
    int tap_x = 0, tap_y = 0;
    const int tap_threshold = TAP_THRESHOLD + 10;

    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000L;

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        int ret = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (ret == 0) { close(fd); return 0; }
        if (ret <  0) { close(fd); return -1; }

        if (read(fd, &ev, sizeof(ev)) != sizeof(ev))
            continue;

        if (ev.type == EV_ABS) {
            if      (ev.code == ABS_X) raw_x = ev.value;
            else if (ev.code == ABS_Y) raw_y = ev.value;
        }

        if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            if (ev.value == 1) {
                tap_x = map_coord(raw_x, &rx, g_lcd_width);
                tap_y = map_coord(raw_y, &ry, g_lcd_height);
            } else if (ev.value == 0) {
                int cur_px = map_coord(raw_x, &rx, g_lcd_width);
                int cur_py = map_coord(raw_y, &ry, g_lcd_height);
                int dist   = abs_val(cur_px - tap_x) + abs_val(cur_py - tap_y);
                if (dist <= tap_threshold) {
                    close(fd);
                    /* 超时版适当放宽阈值，降低点击偶发漏判 */
                    *out_x = (tap_x + cur_px) / 2;
                    *out_y = (tap_y + cur_py) / 2;
                    return 1;
                }
            }
        }
    }
}

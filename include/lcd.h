/**
 * lcd.h — LCD 帧缓冲绘图层接口
 *
 * 职责：封装对 /dev/fb0 的所有像素级操作。
 *       上层代码只与坐标和颜色交互，不感知 mmap 细节。
 *
 * 像素格式：RGB565（16-bit），屏幕默认分辨率由驱动运行时读取。
 */

#ifndef LCD_H
#define LCD_H

/* ----------------------------------------------------------------
 * 设备路径
 * ---------------------------------------------------------------- */
#define FB_DEV  "/dev/fb0"

/* ----------------------------------------------------------------
 * RGB565 颜色宏：将 8-bit R/G/B 打包为 16-bit RGB565
 *   R: 高 5 bit   G: 中 6 bit   B: 低 5 bit
 * ---------------------------------------------------------------- */
#define RGB565(r, g, b) \
    ((unsigned short)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))

/* ----------------------------------------------------------------
 * 主题色板（供所有模块统一引用，禁止硬编码颜色值）
 * ---------------------------------------------------------------- */
#define COLOR_BG_DARK       RGB565( 18,  24,  43)   /* 深海蓝：主背景      */
#define COLOR_BG_CARD       RGB565( 30,  40,  70)   /* 卡片背景            */
#define COLOR_HEADER_BG     RGB565( 10,  16,  36)   /* 顶部标题栏背景      */
#define COLOR_HEADER_LINE   RGB565( 60, 120, 200)   /* 标题栏下边装饰线    */
#define COLOR_WHITE         RGB565(255, 255, 255)
#define COLOR_GRAY_LIGHT    RGB565(180, 190, 210)
#define COLOR_BLACK         RGB565(  0,   0,   0)

/* 各功能模块按钮主色 / 高亮边框色 */
#define COLOR_LED_MAIN      RGB565( 40, 100, 220)   /* 电蓝 —— LED & 蜂鸣器 */
#define COLOR_LED_BORDER    RGB565( 80, 160, 255)
#define COLOR_MUSIC_MAIN    RGB565(180,  60, 200)   /* 紫色 —— 音乐播放器   */
#define COLOR_MUSIC_BORDER  RGB565(220, 100, 240)
#define COLOR_SENSOR_MAIN   RGB565( 30, 160,  80)   /* 绿色 —— GY39 & MQ2  */
#define COLOR_SENSOR_BORDER RGB565( 60, 220, 120)
#define COLOR_PHOTO_MAIN    RGB565(200, 120,  20)   /* 琥珀 —— 电子相册    */
#define COLOR_PHOTO_BORDER  RGB565(240, 170,  50)

/* 子界面通用按钮色 */
#define COLOR_BTN_NORMAL    RGB565( 50,  60,  90)
#define COLOR_BTN_BORDER    RGB565(100, 120, 180)
#define COLOR_BTN_EXIT      RGB565(160,  30,  30)
#define COLOR_BTN_EXIT_BDR  RGB565(220,  60,  60)

/* ----------------------------------------------------------------
 * 屏幕实际尺寸（运行时由驱动填充，供外部只读）
 * ---------------------------------------------------------------- */
extern int g_lcd_width;
extern int g_lcd_height;

/* ----------------------------------------------------------------
 * 初始化 / 清理
 * ---------------------------------------------------------------- */

/**
 * lcd_open - 打开帧缓冲并 mmap
 * 返回 0 成功，-1 失败（errno 已设置）
 */
int lcd_open(void);

/** lcd_close - 释放 mmap 并关闭文件描述符 */
void lcd_close(void);

/* ----------------------------------------------------------------
 * 基础绘图
 * ---------------------------------------------------------------- */

/**
 * lcd_fill_rect - 用指定颜色填充矩形区域
 * @x, @y : 左上角坐标
 * @w, @h : 宽高（像素）
 */
void lcd_fill_rect(int x, int y, int w, int h, unsigned short color);

/**
 * lcd_draw_border - 绘制空心矩形边框
 * @thickness : 边框粗细（像素数）
 */
void lcd_draw_border(int x, int y, int w, int h, int thickness, unsigned short color);

/**
 * lcd_draw_hline - 绘制水平线
 * @len       : 线段长度
 * @thickness : 线条粗细
 */
void lcd_draw_hline(int x, int y, int len, int thickness, unsigned short color);

/* ----------------------------------------------------------------
 * 单像素 / 图像块绘制
 * ---------------------------------------------------------------- */

/**
 * lcd_draw_pixel - 设置单个像素颜色（带边界保护）
 *
 * 供需要逐像素写入的模块使用（如小图标、调试标记）。
 * 大面积图像渲染请优先使用 lcd_draw_image，性能更高。
 */
void lcd_draw_pixel(int x, int y, unsigned short color);

/**
 * lcd_draw_image - 将 RGB565 像素缓冲区批量写入屏幕矩形区域
 *
 * 实现方式：逐行 memcpy，利用 CPU 缓存行对齐优化，
 * 避免逐像素调用的函数调用开销与边界检查开销。
 *
 * @x, @y  : 目标矩形左上角坐标
 * @w, @h  : 目标矩形宽高（像素）
 * @pixels : 源缓冲区，长度须 >= w * h，行主序 RGB565 数据
 *
 * 注意：超出屏幕边界的部分会被自动裁剪，不会越界写入。
 */
void lcd_draw_image(int x, int y, int w, int h, const unsigned short *pixels);

/* ----------------------------------------------------------------
 * 文字渲染（8×16 ASCII 字模）
 * ---------------------------------------------------------------- */

/**
 * lcd_draw_char - 渲染单个 ASCII 字符（8×16 点阵）
 * @c  : 目标字符（自动转大写；超出字模范围显示空格）
 * @fg : 笔画前景色
 * @bg : 背景色
 */
void lcd_draw_char(int x, int y, char c, unsigned short fg, unsigned short bg);

/**
 * lcd_draw_string - 绘制 ASCII 字符串（水平排列，字符间距 8 px）
 *
 * 注意：若需渲染中文，需另行调用基于 HZK16 的渲染函数（当前版本未实现）。
 */
void lcd_draw_string(int x, int y, const char *str,
                     unsigned short fg, unsigned short bg);

#endif /* LCD_H */

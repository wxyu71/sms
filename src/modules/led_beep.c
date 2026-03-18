/**
 * led_beep.c — LED 灯与蜂鸣器控制模块（完整实现）
 *
 * 硬件驱动层来源：
 *   - led.c  (同事)：ctl_led()  → 写 /dev/led_drv，格式 {state, led_num}
 *   - beep.c (同事)：ctl_pwm()  → 写 /dev/pwm，格式 {state}
 *
 * 两个驱动函数均内联为本文件的 static 函数，逻辑与同事代码完全一致，
 * 不修改接口，不重新设计，仅调整为 static 作用域以遵守单一职责原则。
 *
 * UI 布局（800×480）：
 *
 *   ┌──────────────── LED & BEEP CONTROL ────────────────┐
 *   │  [BEEP ON ]    [D8 ON ]    [D8 OFF]                │
 *   │  [BEEP OFF]    [D9 ON ]    [D9 OFF]                │
 *   │  [EXIT    ]    [D10 ON]    [D10 OFF]               │
 *   └─────────────────────────────────────────────────────┘
 *
 * 退出时自动关闭所有 LED 和蜂鸣器。
 */

#include "led_beep.h"
#include "lcd.h"
#include "ui.h"
#include "touch.h"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

/* ----------------------------------------------------------------
 * 硬件驱动层（内联自同事 led.c / beep.c，逻辑完全一致）
 * ---------------------------------------------------------------- */

#define LED_DEV  "/dev/led_drv"
#define PWM_DEV  "/dev/pwm"

/**
 * ctl_led - 控制指定编号的 LED 亮灭
 * @led_num   : 灯号（8=D8, 9=D9, 10=D10，与同事 led.h 枚举一致）
 * @led_state : 0=灭，1=亮
 */
static int ctl_led(int led_num, int led_state)
{
    int fd = open(LED_DEV, O_WRONLY);
    if (fd < 0) { perror("ctl_led: open " LED_DEV); return -1; }
    char cmd[2] = {(char)led_state, (char)led_num};
    write(fd, cmd, 2);
    close(fd);
    return 0;
}

/**
 * ctl_pwm - 控制蜂鸣器开关
 * @pwm_state : 0=关闭，1=开启
 */
static int ctl_pwm(int pwm_state)
{
    int fd = open(PWM_DEV, O_WRONLY);
    if (fd < 0) { perror("ctl_pwm: open " PWM_DEV); return -1; }
    char state = (char)pwm_state;
    write(fd, &state, 1);
    close(fd);
    return 0;
}

/* ----------------------------------------------------------------
 * 背景图与透明热区映射
 * ---------------------------------------------------------------- */

static const char *const CONTROL_PANEL_BMP_CANDIDATES[] = {
    "assets/images/controlPanel.bmp",
    "../assets/images/controlPanel.bmp",
    "controlPanel.bmp",
};

#define PANEL_DESIGN_W  800
#define PANEL_DESIGN_H  480

static unsigned short *g_panel_bg_rgb565 = NULL;
static int g_panel_bg_w = 0;
static int g_panel_bg_h = 0;

static int scale_x(int x) { return x * g_lcd_width / PANEL_DESIGN_W; }
static int scale_y(int y) { return y * g_lcd_height / PANEL_DESIGN_H; }

typedef enum {
    LB_BEEP_ON = 0, LB_D8_ON,  LB_D8_OFF,   /* 行 0 */
    LB_BEEP_OFF,    LB_D9_ON,  LB_D9_OFF,   /* 行 1 */
    LB_EXIT,        LB_D10_ON, LB_D10_OFF,  /* 行 2 */
    LB_COUNT
} LedBeepBtn;

static Button s_buttons[LB_COUNT];

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
    int data_offset = *(const int *)(file_map + 10);
    int bmp_w = *(const int *)(file_map + 18);
    int bmp_h = *(const int *)(file_map + 22);
    short depth = *(const short *)(file_map + 28);

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

static int ensure_control_panel_loaded(void)
{
    if (g_panel_bg_rgb565 != NULL && g_panel_bg_w == g_lcd_width && g_panel_bg_h == g_lcd_height)
        return 0;

    free(g_panel_bg_rgb565);
    g_panel_bg_rgb565 = (unsigned short *)malloc((size_t)g_lcd_width * g_lcd_height * sizeof(unsigned short));
    if (g_panel_bg_rgb565 == NULL)
        return -1;

    g_panel_bg_w = g_lcd_width;
    g_panel_bg_h = g_lcd_height;

    int path_count = (int)(sizeof(CONTROL_PANEL_BMP_CANDIDATES) / sizeof(CONTROL_PANEL_BMP_CANDIDATES[0]));
    for (int i = 0; i < path_count; i++) {
        if (load_bmp_scaled_rgb565(CONTROL_PANEL_BMP_CANDIDATES[i], g_panel_bg_rgb565, g_panel_bg_w, g_panel_bg_h) == 0)
            return 0;
    }

    free(g_panel_bg_rgb565);
    g_panel_bg_rgb565 = NULL;
    return -1;
}

static void release_control_panel(void)
{
    free(g_panel_bg_rgb565);
    g_panel_bg_rgb565 = NULL;
    g_panel_bg_w = 0;
    g_panel_bg_h = 0;
}

static void build_hotspots(void)
{
    /* 设计稿基准 800x480：坐标按 controlPanel.bmp 量测 */
    s_buttons[LB_BEEP_ON] = (Button){
        scale_x(317), scale_y(180), scale_x(166), scale_y(56),
        "BEEP ON", "", COLOR_LED_MAIN, COLOR_LED_BORDER, NULL
    };
    s_buttons[LB_D8_ON] = (Button){
        scale_x(108), scale_y(379), scale_x(106), scale_y(52),
        "D8 ON", "", COLOR_LED_MAIN, COLOR_LED_BORDER, NULL
    };
    s_buttons[LB_D8_OFF] = (Button){
        scale_x(223), scale_y(379), scale_x(78), scale_y(52),
        "D8 OFF", "", COLOR_BTN_NORMAL, COLOR_BTN_BORDER, NULL
    };
    s_buttons[LB_BEEP_OFF] = (Button){
        scale_x(498), scale_y(180), scale_x(166), scale_y(56),
        "BEEP OFF", "", COLOR_BTN_NORMAL, COLOR_BTN_BORDER, NULL
    };
    s_buttons[LB_D9_ON] = (Button){
        scale_x(314), scale_y(379), scale_x(106), scale_y(52),
        "D9 ON", "", COLOR_LED_MAIN, COLOR_LED_BORDER, NULL
    };
    s_buttons[LB_D9_OFF] = (Button){
        scale_x(427), scale_y(379), scale_x(80), scale_y(52),
        "D9 OFF", "", COLOR_BTN_NORMAL, COLOR_BTN_BORDER, NULL
    };
    s_buttons[LB_EXIT] = (Button){
        scale_x(599), scale_y(37), scale_x(82), scale_y(54),
        "EXIT", "", COLOR_BTN_EXIT, COLOR_BTN_EXIT_BDR, NULL
    };
    s_buttons[LB_D10_ON] = (Button){
        scale_x(522), scale_y(379), scale_x(106), scale_y(52),
        "D10 ON", "", COLOR_LED_MAIN, COLOR_LED_BORDER, NULL
    };
    s_buttons[LB_D10_OFF] = (Button){
        scale_x(633), scale_y(379), scale_x(80), scale_y(52),
        "D10 OFF", "", COLOR_BTN_NORMAL, COLOR_BTN_BORDER, NULL
    };
}

static void draw_frame(void)
{
    if (ensure_control_panel_loaded() == 0) {
        lcd_draw_image(0, 0, g_panel_bg_w, g_panel_bg_h, g_panel_bg_rgb565);
    } else {
        /* 资源缺失时保留可操作的兜底界面 */
        lcd_fill_rect(0, 0, g_lcd_width, g_lcd_height, COLOR_BG_DARK);
        lcd_fill_rect(0, 0, g_lcd_width, 55, COLOR_HEADER_BG);
        lcd_draw_hline(0, 55, g_lcd_width, 3, COLOR_LED_BORDER);
        lcd_draw_string(328, 20, "LED & BEEP CONTROL", COLOR_WHITE, COLOR_HEADER_BG);
        for (int i = 0; i < LB_COUNT; i++)
            ui_draw_button(&s_buttons[i]);
    }
}

void module_led_beep(void)
{
    build_hotspots();
    draw_frame();

    while (1) {
        int x, y;
        if (touch_get_tap(&x, &y) != 0)
            continue;

        const Button *hit = ui_hit_test(s_buttons, LB_COUNT, x, y);
        if (hit == NULL) continue;

        switch ((LedBeepBtn)(hit - s_buttons)) {
            case LB_BEEP_ON:   ctl_pwm(1);       break;
            case LB_BEEP_OFF:  ctl_pwm(0);       break;
            case LB_D8_ON:     ctl_led(8,  1);   break;
            case LB_D8_OFF:    ctl_led(8,  0);   break;
            case LB_D9_ON:     ctl_led(9,  1);   break;
            case LB_D9_OFF:    ctl_led(9,  0);   break;
            case LB_D10_ON:    ctl_led(10, 1);   break;
            case LB_D10_OFF:   ctl_led(10, 0);   break;
            case LB_EXIT:
                /* 退出前确保所有 LED 和蜂鸣器已关闭（符合集成约定） */
                ctl_pwm(0);
                ctl_led(8,  0);
                ctl_led(9,  0);
                ctl_led(10, 0);
                release_control_panel();
                return;
            default: break;
        }
    }
}

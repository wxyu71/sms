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
#include <fcntl.h>
#include <unistd.h>

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
 * UI 布局常量
 * 3 列均匀分布于 800px 宽屏：列间距 = (800 - 3×180) / 4 ≈ 65px
 * ---------------------------------------------------------------- */
#define LBW  180   /* 按钮宽度 */
#define LBH   70   /* 按钮高度 */
#define COL0  65   /* 第 0 列 x */
#define COL1 310   /* 第 1 列 x */
#define COL2 555   /* 第 2 列 x */
#define ROW0 110   /* 第 0 行 y */
#define ROW1 210   /* 第 1 行 y */
#define ROW2 310   /* 第 2 行 y */

typedef enum {
    LB_BEEP_ON = 0, LB_D8_ON,  LB_D8_OFF,   /* 行 0 */
    LB_BEEP_OFF,    LB_D9_ON,  LB_D9_OFF,   /* 行 1 */
    LB_EXIT,        LB_D10_ON, LB_D10_OFF,  /* 行 2 */
    LB_COUNT
} LedBeepBtn;

static const Button s_buttons[LB_COUNT] = {
    /* 行 0 */
    {COL0, ROW0, LBW, LBH, "BEEP ON",  "", COLOR_LED_MAIN,   COLOR_LED_BORDER,   NULL},
    {COL1, ROW0, LBW, LBH, "D8 ON",    "", COLOR_LED_MAIN,   COLOR_LED_BORDER,   NULL},
    {COL2, ROW0, LBW, LBH, "D8 OFF",   "", COLOR_BTN_NORMAL, COLOR_BTN_BORDER,   NULL},
    /* 行 1 */
    {COL0, ROW1, LBW, LBH, "BEEP OFF", "", COLOR_BTN_NORMAL, COLOR_BTN_BORDER,   NULL},
    {COL1, ROW1, LBW, LBH, "D9 ON",    "", COLOR_LED_MAIN,   COLOR_LED_BORDER,   NULL},
    {COL2, ROW1, LBW, LBH, "D9 OFF",   "", COLOR_BTN_NORMAL, COLOR_BTN_BORDER,   NULL},
    /* 行 2 */
    {COL0, ROW2, LBW, LBH, "EXIT",     "", COLOR_BTN_EXIT,   COLOR_BTN_EXIT_BDR, NULL},
    {COL1, ROW2, LBW, LBH, "D10 ON",   "", COLOR_LED_MAIN,   COLOR_LED_BORDER,   NULL},
    {COL2, ROW2, LBW, LBH, "D10 OFF",  "", COLOR_BTN_NORMAL, COLOR_BTN_BORDER,   NULL},
};

static void draw_frame(void)
{
    lcd_fill_rect(0, 0, g_lcd_width, g_lcd_height, COLOR_BG_DARK);
    lcd_fill_rect(0, 0, g_lcd_width, 55, COLOR_HEADER_BG);
    lcd_draw_hline(0, 55, g_lcd_width, 3, COLOR_LED_BORDER);
    /* "LED & BEEP CONTROL" = 18 chars × 8px = 144px → center at (800-144)/2 = 328 */
    lcd_draw_string(328, 20, "LED & BEEP CONTROL", COLOR_WHITE, COLOR_HEADER_BG);

    for (int i = 0; i < LB_COUNT; i++)
        ui_draw_button(&s_buttons[i]);
}

void module_led_beep(void)
{
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
                return;
            default: break;
        }
    }
}

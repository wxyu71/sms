/**
 * sensor.c — 环境传感器模块（GY39 + MQ2 完整实现）
 *
 * 硬件驱动层来源：
 *   - gy39.c (同事)：串口读取光照/温度/湿度，端口 3 = /dev/ttySAC2
 *   - mq2.c  (同事)：串口读取气体浓度，端口 4 = /dev/ttySAC3
 *
 * 两个文件中 init_tty() 实现完全相同，合并为一个静态函数 tty_init()，
 * 避免重复代码。数据采集逻辑完整保留自同事代码，仅调整为 static 作用域。
 *
 * 自动联动控制：
 *   - 光照 < LIGHT_THRESHOLD_LUX → 自动开 D8 LED
 *   - 气体 > GAS_THRESHOLD_PPM  → 蜂鸣器报警
 *
 * 刷新策略：使用 touch_get_tap_timeout() 每 2 秒刷新一次传感器数据，
 * 触摸事件可随时中断等待，点击 EXIT 退出。
 *
 * UI 布局（800×480）：
 *
 *   ┌─────────────── SENSOR DATA ────────────────┐
 *   │  LIGHT:   xxxx LUX                         │
 *   │  TEMP:    xx C                             │
 *   │  HUM:     xx PCT                           │
 *   │  GAS:     xxxx PPM  (超标时红色高亮)        │
 *   │  LED AUTO: ON/OFF     BEEP: OK/ALERT        │
 *   │               [EXIT]                        │
 *   └────────────────────────────────────────────┘
 */

#include "sensor.h"
#include "lcd.h"
#include "ui.h"
#include "touch.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

/* ----------------------------------------------------------------
 * 硬件端口（GEC6818：串口3 = ttySAC2，串口4 = ttySAC3）
 * ---------------------------------------------------------------- */
#define GY39_PORT  "/dev/ttySAC2"   /* GY39：光照/温度/湿度（端口3） */
#define MQ2_PORT   "/dev/ttySAC3"   /* MQ2 ：气体浓度（端口4）       */

/* ----------------------------------------------------------------
 * 自动联动阈值（含迟滞区间，防止 LED 补光导致反馈震荡）
 *
 * 迟滞控制逻辑：
 *   - 开灯：光照下降到 LIGHT_ON_LUX  以下时触发
 *   - 关灯：光照上升到 LIGHT_OFF_LUX 以上时触发
 *   两个阈值之间是"稳定区"，状态保持不变，避免边界附近反复横跳。
 * ---------------------------------------------------------------- */
#define LIGHT_ON_LUX     50   /* 光照低于此值：开 D8 LED        */
#define LIGHT_OFF_LUX    50   /* 光照高于此值：关 D8 LED        */
#define GAS_THRESHOLD_PPM    500   /* 超过此值蜂鸣器告警         */

/* ----------------------------------------------------------------
 * 刷新间隔（毫秒）
 * ---------------------------------------------------------------- */
#define REFRESH_MS  2000

/* ----------------------------------------------------------------
 * 串口驱动层
 * gy39.c 与 mq2.c 中 init_tty() 实现完全相同，合并为一份静态函数。
 * 参数：9600 bps，8N1，无流控，VTIME=10（100ms），VMIN=1
 * ---------------------------------------------------------------- */

static void tty_init(int fd)
{
    struct termios t;
    bzero(&t, sizeof(t));
    cfmakeraw(&t);
    cfsetispeed(&t, B9600);
    cfsetospeed(&t, B9600);
    t.c_cflag |= CLOCAL | CREAD;
    t.c_cflag &= ~CSIZE;
    t.c_cflag |= CS8;
    t.c_cflag &= ~PARENB;
    t.c_cflag &= ~CSTOPB;
    t.c_cc[VTIME] = 10;
    t.c_cc[VMIN]  = 1;
    tcflush(fd, TCIFLUSH);
    if (tcsetattr(fd, TCSANOW, &t) != 0)
        perror("tty_init: tcsetattr failed");
}

/**
 * gy39_read - 读取 GY39 传感器数据（光照/温度/湿度）
 *
 * 协议（来自同事 gy39.c，完整保留）：
 *   请求光照：{0xa5,0x81,0x26}  → 回 9 字节，[4..7] 为 32bit 光照值 ÷100
 *   请求温湿度：{0xa5,0x82,0x27} → 回 15 字节，[4..5] 温度 ÷100，[10..11] 湿度 ÷100
 *
 * @data[0]=光照(lux)  @data[1]=温度(°C)  @data[2]=湿度(%)
 * 返回 0 成功，-1 失败
 */
static int gy39_read(int data[3])
{
    /* 去掉 O_NDELAY：串口 read() 必须阻塞等待传感器回包
     * O_NDELAY 会使 read() 立即返回 0 字节（缓冲区全 0） */
    int fd = open(GY39_PORT, O_RDWR | O_NOCTTY);
    if (fd < 0) { perror("gy39_read: open " GY39_PORT); return -1; }
    tty_init(fd);

    /* 请求光照强度 */
    unsigned char lux_cmd[3] = {0xa5, 0x81, 0x26};
    write(fd, lux_cmd, 3);
    /* 用 unsigned char 接收，防止有符号字节在移位时产生符号扩展 */
    unsigned char lux_buf[9] = {0};
    read(fd, lux_buf, 9);
    data[0] = (int)(((unsigned int)lux_buf[4] << 24) |
                    ((unsigned int)lux_buf[5] << 16) |
                    ((unsigned int)lux_buf[6] <<  8) |
                     (unsigned int)lux_buf[7]) / 100;

    /* 请求温湿度 */
    unsigned char tmp_cmd[3] = {0xa5, 0x82, 0x27};
    write(fd, tmp_cmd, 3);
    unsigned char tmp_buf[15] = {0};
    read(fd, tmp_buf, 15);
    data[1] = (int)(((unsigned int)tmp_buf[4]  << 8) | tmp_buf[5])  / 100;   /* 温度 */
    data[2] = (int)(((unsigned int)tmp_buf[10] << 8) | tmp_buf[11]) / 100;   /* 湿度 */

    close(fd);
    return 0;
}

/**
 * mq2_read - 读取 MQ2 传感器气体浓度
 *
 * 协议（来自同事 mq2.c，完整保留，ZxE 协议）：
 *   请求帧：{0xFF,0x01,0x86,...,0x79}（9 字节）
 *   响应帧：9 字节，[2..3] 为气体浓度高低字节
 *
 * @ppm：输出气体浓度（ppm）
 * 返回 0 成功，-1 失败
 */
static int mq2_read(int *ppm)
{
    /* 同上，去掉 O_NDELAY 确保 read() 阻塞等待回包 */
    int fd = open(MQ2_PORT, O_RDWR | O_NOCTTY);
    if (fd < 0) { perror("mq2_read: open " MQ2_PORT); return -1; }
    tty_init(fd);

    unsigned char req[9] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
    write(fd, req, 9);
    unsigned char buf[9] = {0};
    read(fd, buf, 9);
    *ppm = (int)(((unsigned int)buf[2] << 8) | buf[3]);

    close(fd);
    return 0;
}

/* ----------------------------------------------------------------
 * LED / 蜂鸣器联动控制（与 led_beep.c 使用相同的驱动路径）
 * ---------------------------------------------------------------- */
static void led_set(int num, int on)
{
    int fd = open("/dev/led_drv", O_WRONLY);
    if (fd < 0) return;
    char cmd[2] = {(char)on, (char)num};
    write(fd, cmd, 2);
    close(fd);
}

static void beep_set(int on)
{
    int fd = open("/dev/pwm", O_WRONLY);
    if (fd < 0) return;
    char state = (char)on;
    write(fd, &state, 1);
    close(fd);
}

/* ----------------------------------------------------------------
 * UI 渲染
 * ---------------------------------------------------------------- */

/* 数据行布局 */
#define LABEL_X   60    /* 标签起始列 */
#define VALUE_X  280    /* 数值起始列（清除旧值宽度到屏幕右边） */
#define ROW_H     48    /* 行高（8px 字高 + 行间距） */
#define ROW0_Y    80    /* 第 0 行：光照 */
#define ROW1_Y   (ROW0_Y + ROW_H)
#define ROW2_Y   (ROW1_Y + ROW_H)
#define ROW3_Y   (ROW2_Y + ROW_H)
#define STATUS_Y  320   /* 联动状态行 */

static const Button s_exit_btn = {
    310, 390, 180, 60, "EXIT", "", COLOR_BTN_EXIT, COLOR_BTN_EXIT_BDR, NULL
};

static void draw_frame(void)
{
    lcd_fill_rect(0, 0, g_lcd_width, g_lcd_height, COLOR_BG_DARK);
    lcd_fill_rect(0, 0, g_lcd_width, 55, COLOR_HEADER_BG);
    lcd_draw_hline(0, 55, g_lcd_width, 3, COLOR_SENSOR_BORDER);
    /* "SENSOR DATA" = 11 chars × 8px = 88px → center: (800-88)/2 = 356 */
    lcd_draw_string(356, 20, "SENSOR DATA", COLOR_WHITE, COLOR_HEADER_BG);

    /* 固定标签（只绘制一次，数值区单独刷新） */
    lcd_draw_string(LABEL_X, ROW0_Y, "LIGHT:", COLOR_GRAY_LIGHT, COLOR_BG_DARK);
    lcd_draw_string(LABEL_X, ROW1_Y, "TEMP:",  COLOR_GRAY_LIGHT, COLOR_BG_DARK);
    lcd_draw_string(LABEL_X, ROW2_Y, "HUM:",   COLOR_GRAY_LIGHT, COLOR_BG_DARK);
    lcd_draw_string(LABEL_X, ROW3_Y, "GAS:",   COLOR_GRAY_LIGHT, COLOR_BG_DARK);

    ui_draw_button(&s_exit_btn);
}

/**
 * update_values - 只刷新数值区和状态栏，避免全屏重绘引起闪烁
 */
static void update_values(const int gy39[3], int gas_ppm, int led_on, int beep_on)
{
    char buf[32];
    const int val_w = g_lcd_width - VALUE_X;  /* 数值区宽度 */

    /* 光照 */
    lcd_fill_rect(VALUE_X, ROW0_Y, val_w, 16, COLOR_BG_DARK);
    snprintf(buf, sizeof(buf), "%d LUX", gy39[0]);
    lcd_draw_string(VALUE_X, ROW0_Y, buf, COLOR_WHITE, COLOR_BG_DARK);

    /* 温度 */
    lcd_fill_rect(VALUE_X, ROW1_Y, val_w, 16, COLOR_BG_DARK);
    snprintf(buf, sizeof(buf), "%d C", gy39[1]);
    lcd_draw_string(VALUE_X, ROW1_Y, buf, COLOR_WHITE, COLOR_BG_DARK);

    /* 湿度 */
    lcd_fill_rect(VALUE_X, ROW2_Y, val_w, 16, COLOR_BG_DARK);
    snprintf(buf, sizeof(buf), "%d PCT", gy39[2]);
    lcd_draw_string(VALUE_X, ROW2_Y, buf, COLOR_WHITE, COLOR_BG_DARK);

    /* 气体浓度：超阈值时红色高亮警示 */
    lcd_fill_rect(VALUE_X, ROW3_Y, val_w, 16, COLOR_BG_DARK);
    snprintf(buf, sizeof(buf), "%d PPM", gas_ppm);
    unsigned short gas_color = (gas_ppm > GAS_THRESHOLD_PPM) ? COLOR_BTN_EXIT : COLOR_WHITE;
    lcd_draw_string(VALUE_X, ROW3_Y, buf, gas_color, COLOR_BG_DARK);

    /* 联动状态（左半区：LED，右半区：BEEP） */
    lcd_fill_rect(0, STATUS_Y, g_lcd_width, 20, COLOR_BG_DARK);
    snprintf(buf, sizeof(buf), "LED AUTO: %s", led_on ? "ON " : "OFF");
    lcd_draw_string(LABEL_X, STATUS_Y, buf,
                    led_on ? COLOR_LED_MAIN : COLOR_GRAY_LIGHT, COLOR_BG_DARK);
    snprintf(buf, sizeof(buf), "BEEP: %s", beep_on ? "ALERT" : "OK");
    lcd_draw_string(460, STATUS_Y, buf,
                    beep_on ? COLOR_BTN_EXIT : COLOR_SENSOR_MAIN, COLOR_BG_DARK);
}

void module_sensor(void)
{
    int gy39_data[3] = {0, 0, 0};
    int gas_ppm      = 0;
    int led_on       = 0;
    int beep_on      = 0;

    draw_frame();

    while (1) {
        /* 采集传感器数据（失败时保留上次读数） */
        gy39_read(gy39_data);
        mq2_read(&gas_ppm);

        /* 自动联动控制（迟滞逻辑，防止 LED 补光引起反馈震荡）：
         * - 仅在"状态需要改变"时才写驱动，避免频繁 open/write
         * - 开灯和关灯使用不同阈值，中间区间内保持当前状态不变 */
        if (!led_on  && gy39_data[0] <  LIGHT_ON_LUX)  { led_set(8, 1); led_on  = 1; }
        if ( led_on  && gy39_data[0] >  LIGHT_OFF_LUX) { led_set(8, 0); led_on  = 0; }
        if (!beep_on && gas_ppm      >  GAS_THRESHOLD_PPM) { beep_set(1); beep_on = 1; }
        if ( beep_on && gas_ppm      <= GAS_THRESHOLD_PPM) { beep_set(0); beep_on = 0; }

        /* 刷新 LCD 数值区 */
        update_values(gy39_data, gas_ppm, led_on, beep_on);

        /* 等待触摸，超时后继续下一轮刷新 */
        int x, y;
        int ret = touch_get_tap_timeout(&x, &y, REFRESH_MS);
        if (ret != 1) continue;   /* 超时(0) 或设备错误(-1)，继续刷新 */

        /* 判断是否命中退出按钮 */
        if (ui_hit_test(&s_exit_btn, 1, x, y) != NULL) {
            /* 退出前关闭所有联动设备 */
            led_set(8, 0);
            beep_set(0);
            return;
        }
    }
}

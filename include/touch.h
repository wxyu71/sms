/**
 * touch.h — 触摸屏输入层接口
 *
 * 职责：封装 Linux input event 读取，对上层只暴露"点击坐标"语义。
 *       滑动方向检测（如电子相册翻页）在各子模块中按需调用扩展接口。
 */

#ifndef TOUCH_H
#define TOUCH_H

/* 触摸设备路径 */
#define TOUCH_DEV       "/dev/input/event0"

/* 点击判定阈值（px）：手指抬起时与按下时的曼哈顿距离小于此值视为点击 */
#define TAP_THRESHOLD   20

/* 滑动判定阈值（px）：超过此值才算有效滑动 */
#define SLIDE_THRESHOLD 30

/** 滑动方向枚举（供子模块使用） */
typedef enum {
    DIR_TAP   = 0,  /* 点击（无滑动） */
    DIR_UP,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT
} TouchDir;

/**
 * touch_get_tap - 阻塞等待一次触摸"点击"，输出落点坐标
 *
 * 若手指位移 >= TAP_THRESHOLD，视为滑动，忽略并继续等待下次事件。
 *
 * @out_x, @out_y : 输出按下时的坐标
 * 返回 0 成功，-1 打开设备失败（errno 已设置）
 */
int touch_get_tap(int *out_x, int *out_y);

/**
 * touch_get_tap_timeout - 带超时的触摸点击等待
 *
 * 专为需要定时刷新的子模块（如传感器界面）设计：
 * 在等待触摸的同时允许周期性唤醒，避免无限阻塞。
 *
 * 实现原理：使用 select() 监听触摸 fd，Linux 会在每次 select()
 * 返回后将 tv 更新为剩余时间，下次调用自然递减，无需手动计时。
 *
 * @out_x, @out_y : 有效点击时输出的坐标
 * @timeout_ms    : 超时时间（毫秒，> 0）
 * 返回 1 有效点击，0 超时，-1 设备错误
 */
int touch_get_tap_timeout(int *out_x, int *out_y, int timeout_ms);

/**
 * touch_get_event - 阻塞等待一次触摸事件，同时输出方向与坐标
 *
 * 适用于需要区分点击与滑动的子模块（如电子相册、音乐播放器）。
 *
 * @out_x, @out_y : 输出按下时的坐标（无论点击还是滑动均填充）
 * 返回 TouchDir 枚举值；设备打开失败时返回 DIR_TAP（坐标为 -1）
 */
TouchDir touch_get_event(int *out_x, int *out_y);

#endif /* TOUCH_H */

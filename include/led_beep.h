/**
 * led_beep.h — LED 灯与蜂鸣器控制模块接口
 *
 * 子界面按钮布局（参考设计图 image-20260316104430161.png）：
 *
 *   [ 蜂鸣器开 ]   [ D8亮 ]  [ D8灭 ]
 *   [ 蜂鸣器关 ]   [ D7亮 ]  [ D7灭 ]
 *                  [ D6亮 ]  [ D6灭 ]
 *   [ 退出    ]
 *
 * 集成说明：
 *   将 led_beep.c 中的 TODO 替换为实际的 GPIO / PWM 操作即可。
 *   主界面无需任何改动。
 */

#ifndef LED_BEEP_H
#define LED_BEEP_H

/**
 * module_led_beep - LED & 蜂鸣器控制子界面入口
 *
 * 内部维护自己的事件循环，点击"退出"按钮后返回。
 * 返回前须确保所有 LED 和蜂鸣器已关闭。
 */
void module_led_beep(void);

#endif /* LED_BEEP_H */

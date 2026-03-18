/**
 * sensor.h — 环境传感器模块接口（GY39 + MQ2）
 *
 * GY39：测量光照强度、温度、气压
 * MQ2 ：测量烟雾 / 可燃气体浓度
 *
 * 附加自动控制逻辑：
 *   - 光照强度低于阈值 → 自动开 LED
 *   - 烟雾浓度超阈值   → 蜂鸣器报警
 *
 * 集成说明：
 *   sensor.c 中的 read_gy39() / read_mq2() 函数替换为实际 I2C/ADC 读取实现，
 *   ctl_led_auto() / ctl_pwm_alarm() 替换为实际驱动调用即可。
 */

#ifndef SENSOR_H
#define SENSOR_H

/**
 * module_sensor - 传感器数据显示子界面入口
 *
 * 定时刷新 GY39 / MQ2 数据并显示在 LCD 上，
 * 同时执行光照→LED 和烟雾→蜂鸣器的自动联动控制。
 * 点击屏幕任意位置（"退出"按钮）后返回。
 */
void module_sensor(void);

#endif /* SENSOR_H */

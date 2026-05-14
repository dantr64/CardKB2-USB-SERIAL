#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 RGB LED
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t bsp_rgb_led_init(void);

/**
 * @brief 设置 RGB LED 颜色
 * @param r 红色亮度值 (0-255)
 * @param g 绿色亮度值 (0-255)
 * @param b 蓝色亮度值 (0-255)
 */
void bsp_rgb_led_set_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief 单独设置 RGB LED 的红色通道
 * @param r 红色亮度值 (0-255)
 */
void bsp_rgb_led_set_red(uint8_t r);

/**
 * @brief 单独设置 RGB LED 的绿色通道
 * @param g 绿色亮度值 (0-255)
 */
void bsp_rgb_led_set_green(uint8_t g);

/**
 * @brief 单独设置 RGB LED 的蓝色通道
 * @param b 蓝色亮度值 (0-255)
 */
void bsp_rgb_led_set_blue(uint8_t b);

/**
 * @brief 获取当前 RGB LED 的颜色值
 * @param r 红色通道输出指针
 * @param g 绿色通道输出指针
 * @param b 蓝色通道输出指针
 */
void bsp_rgb_led_get_color(uint8_t *r, uint8_t *g, uint8_t *b);

#ifdef __cplusplus
}
#endif


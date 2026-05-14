#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief I2C 从机初始化
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t bsp_i2c_init(void);

/**
 * @brief I2C 从机反初始化
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t bsp_i2c_deinit(void);

/**
 * @brief 添加按键数据到 I2C 发送队列（ASCII 码模式）
 * @param key 按键 ASCII 码
 */
void bsp_i2c_add_key(uint8_t key);

/**
 * @brief 添加按键 index 到 I2C 发送队列（产测模式）
 * @param button_index 按键索引
 */
void bsp_i2c_add_key_index(uint8_t button_index);

#ifdef __cplusplus
}
#endif


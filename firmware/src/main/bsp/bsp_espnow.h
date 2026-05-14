#ifndef _BSP_ESPNOW_H_
#define _BSP_ESPNOW_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 ESP-NOW 广播模式
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t bsp_espnow_init(void);

/**
 * @brief 反初始化 ESP-NOW
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t bsp_espnow_deinit(void);

/**
 * @brief 通过 ESP-NOW 发送按键字符
 * @param c 按键字符
 */
void bsp_espnow_add_key(char c);

/**
 * @brief 检查 ESP-NOW 是否已初始化
 * @return true 已初始化, false 未初始化
 */
bool bsp_espnow_is_initialized(void);

/**
 * @brief 发送 ESP-NOW 数据包（原始数据）
 * @param data 数据缓冲区
 * @param len 数据长度
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t bsp_espnow_send(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* _BSP_ESPNOW_H_ */


#pragma once

#include "esp_err.h"
#include "iot_button.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int button_index;
    button_event_t event;
} button_event_data_t;

typedef void (*keyboard_event_cb_t)(button_event_data_t *event_data);


/**
 * @brief 初始化键盘
 * @return ESP_OK 成功, 其他失败
 * @note 初始化键盘
 */
esp_err_t bsp_keyboard_init(void);

/**
 * @brief 注册按键事件回调
 * @param cb 回调函数
 * @param event_type 事件类型 (BUTTON_PRESS_DOWN, BUTTON_PRESS_UP, BUTTON_SINGLE_CLICK 等)
 * @return ESP_OK 成功, 其他失败
 * @note 为所有按键注册事件回调
 */
esp_err_t bsp_keyboard_add_event_cb(keyboard_event_cb_t cb, button_event_t event_type);

/**
 * @brief 指定按键注册事件回调
 * @param button_index 按键索引 (0-43)
 * @param cb 回调函数
 * @param event_type 事件类型 (BUTTON_PRESS_DOWN, BUTTON_PRESS_UP, BUTTON_SINGLE_CLICK 等)
 * @return ESP_OK 成功, 其他失败
 * @note 为指定按键注册事件回调
 */
esp_err_t bsp_keyboard_add_event_cb_by_index(int button_index, keyboard_event_cb_t cb, button_event_t event_type);

#ifdef __cplusplus
}
#endif


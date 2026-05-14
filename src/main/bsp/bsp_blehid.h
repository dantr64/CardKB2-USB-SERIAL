#ifndef _BSP_BLEHID_H_
#define _BSP_BLEHID_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// BLE HID 设备名称
#define BLE_HID_DEVICE_NAME     "CardKB2"

// USB HID 修饰键码
#define USB_HID_MODIFIER_NONE           0x00
#define USB_HID_MODIFIER_LEFT_CTRL      0x01
#define USB_HID_MODIFIER_LEFT_SHIFT     0x02
#define USB_HID_MODIFIER_LEFT_ALT       0x04
#define USB_HID_MODIFIER_LEFT_GUI       0x08
#define USB_HID_MODIFIER_RIGHT_CTRL     0x10
#define USB_HID_MODIFIER_RIGHT_SHIFT    0x20
#define USB_HID_MODIFIER_RIGHT_ALT      0x40
#define USB_HID_MODIFIER_RIGHT_GUI      0x80

// USB HID 键码定义
#define USB_HID_KEY_A           0x04
#define USB_HID_KEY_Z           0x1D
#define USB_HID_KEY_1           0x1E
#define USB_HID_KEY_0           0x27
#define USB_HID_KEY_ENTER       0x28
#define USB_HID_KEY_ESC         0x29
#define USB_HID_KEY_BACKSPACE   0x2A
#define USB_HID_KEY_TAB         0x2B
#define USB_HID_KEY_SPACE       0x2C
#define USB_HID_KEY_MINUS       0x2D
#define USB_HID_KEY_EQUAL       0x2E
#define USB_HID_KEY_LEFTBRACE   0x2F
#define USB_HID_KEY_RIGHTBRACE  0x30
#define USB_HID_KEY_BACKSLASH   0x31
#define USB_HID_KEY_SEMICOLON   0x33
#define USB_HID_KEY_APOSTROPHE  0x34
#define USB_HID_KEY_GRAVE       0x35
#define USB_HID_KEY_COMMA       0x36
#define USB_HID_KEY_DOT         0x37
#define USB_HID_KEY_SLASH       0x38

// 方向键
#define USB_HID_KEY_RIGHT       0x4F
#define USB_HID_KEY_LEFT        0x50
#define USB_HID_KEY_DOWN        0x51
#define USB_HID_KEY_UP          0x52

// BLE HID 连接状态
typedef enum {
    BLE_HID_STATE_DISCONNECTED = 0,
    BLE_HID_STATE_ADVERTISING,
    BLE_HID_STATE_CONNECTED,
} ble_hid_state_t;

// BLE HID 事件回调类型
typedef void (*ble_hid_event_cb_t)(ble_hid_state_t state);

/**
 * @brief 初始化 BLE HID 设备
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t bsp_blehid_init(void);

/**
 * @brief 反初始化 BLE HID 设备
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t bsp_blehid_deinit(void);

/**
 * @brief 开始 BLE 广播
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t bsp_blehid_start_advertising(void);

/**
 * @brief 停止 BLE 广播
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t bsp_blehid_stop_advertising(void);

/**
 * @brief 发送键盘按键 (ASCII字符)
 * @param ch ASCII字符
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t bsp_blehid_send_key(char ch);

/**
 * @brief 发送键盘按键 (HID 键码)
 * @param modifier 修饰键
 * @param keycode HID 键码
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t bsp_blehid_send_keycode(uint8_t modifier, uint8_t keycode);

/**
 * @brief 发送键盘释放
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t bsp_blehid_send_key_release(void);

/**
 * @brief 获取 BLE HID 连接状态
 * @return BLE HID 状态
 */
ble_hid_state_t bsp_blehid_get_state(void);

/**
 * @brief 检查 BLE HID 是否已连接
 * @return true 已连接, false 未连接
 */
bool bsp_blehid_is_connected(void);

/**
 * @brief 注册 BLE HID 事件回调
 * @param cb 回调函数
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t bsp_blehid_register_event_cb(ble_hid_event_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* _BSP_BLEHID_H_ */


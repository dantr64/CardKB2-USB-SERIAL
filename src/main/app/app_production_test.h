/**
 * @file app_production_test.h
 * @brief 产测模式头文件
 * @author Enable
 * @date 2025/12/08
 * 
 * 产测命令协议 (UART 115200):
 * 
 * 1. 设置WiFi并连接:
 *    发送: AT+WIFI=SSID,PASSWORD\r\n
 *    响应: +WIFI:CONNECTING\r\n
 *          +WIFI:CONNECTED,192.168.1.100\r\n  (成功)
 *          +WIFI:FAILED,reason\r\n            (失败)
 * 
 * 2. 查询WiFi状态:
 *    发送: AT+WIFI?\r\n
 *    响应: +WIFI:CONNECTED,192.168.1.100\r\n
 *          +WIFI:DISCONNECTED\r\n
 * 
 * 3. 断开WiFi:
 *    发送: AT+WIFI=DISCONNECT\r\n
 *    响应: +WIFI:DISCONNECTED\r\n
 * 
 * 4. 获取固件版本:
 *    发送: AT+VERSION?\r\n
 *    响应: +VERSION:0xF0\r\n
 * 
 * 5. RGB LED 设置颜色:
 *    发送: AT+RGB=R,G,B\r\n  (0-255)
 *    响应: +RGB:OK\r\n
 * 
 * 6. RGB LED 闪烁测试:
 *    发送: AT+RGB_TEST=1\r\n  (启动红绿蓝白黑循环闪烁)
 *    响应: +RGB_TEST:STARTED\r\n
 *    发送: AT+RGB_TEST=0\r\n  (停止闪烁)
 *    响应: +RGB_TEST:STOPPED\r\n
 * 
 * 7. 进入/退出产测模式:
 *    发送: AT+FACTORY=1\r\n  (进入)
 *    发送: AT+FACTORY=0\r\n  (退出)
 *    响应: +FACTORY:1\r\n 或 +FACTORY:0\r\n
 * 
 * 8. 按键事件上报（产测模式下自动上报）:
 *    格式: +KEY:index,event\r\n
 *    - index: 按键索引 (0-43)
 *    - event: 事件类型 (0=按下, 1=释放)
 *    示例: +KEY:22,0\r\n  (按键索引22按下)
 *          +KEY:22,1\r\n  (按键索引22释放)
 */

#ifndef _APP_PRODUCTION_TEST_H_
#define _APP_PRODUCTION_TEST_H_

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// WiFi 连接状态
typedef enum {
    WIFI_STATE_DISCONNECTED = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_FAILED,
} wifi_conn_state_t;

// WiFi 状态回调
typedef void (*wifi_state_callback_t)(wifi_conn_state_t state, const char *info);

/**
 * @brief 初始化产测模式
 * @return ESP_OK 成功
 */
esp_err_t app_production_test_init(void);

/**
 * @brief 反初始化产测模式
 * @return ESP_OK 成功
 */
esp_err_t app_production_test_deinit(void);

/**
 * @brief 连接 WiFi
 * @param ssid SSID
 * @param password 密码
 * @return ESP_OK 成功发起连接
 */
esp_err_t app_production_test_wifi_connect(const char *ssid, const char *password);

/**
 * @brief 断开 WiFi
 * @return ESP_OK 成功
 */
esp_err_t app_production_test_wifi_disconnect(void);

/**
 * @brief 获取 WiFi 状态
 * @return WiFi 连接状态
 */
wifi_conn_state_t app_production_test_wifi_get_state(void);

/**
 * @brief 获取 WiFi IP 地址
 * @return IP 地址字符串，未连接返回 NULL
 */
const char *app_production_test_wifi_get_ip(void);

/**
 * @brief 是否处于产测模式
 * @return true 产测模式
 */
bool app_production_test_is_enabled(void);

/**
 * @brief 设置产测模式
 * @param enable true 进入产测模式
 */
void app_production_test_set_enabled(bool enable);

#ifdef __cplusplus
}
#endif

#endif /* _APP_PRODUCTION_TEST_H_ */


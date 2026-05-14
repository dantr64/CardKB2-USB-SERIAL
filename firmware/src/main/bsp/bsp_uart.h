#ifndef _BSP_UART_H_
#define _BSP_UART_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// UART 配置
#define BSP_UART_NUM            UART_NUM_1
#define BSP_UART_TX_PIN         25  // GPIO 25 (与 I2C SCL 共用)
#define BSP_UART_RX_PIN         26  // GPIO 26 (与 I2C SDA 共用)
#define BSP_UART_BAUD_RATE      115200
#define BSP_UART_BUF_SIZE       256

// 命令回调函数类型
typedef void (*uart_cmd_callback_t)(const char *cmd, int len);

/**
 * @brief 初始化 UART
 * @return ESP_OK 成功
 */
esp_err_t bsp_uart_init(void);

/**
 * @brief 反初始化 UART
 * @return ESP_OK 成功
 */
esp_err_t bsp_uart_deinit(void);

/**
 * @brief 发送数据
 * @param data 数据指针
 * @param len 数据长度
 * @return 发送的字节数
 */
int bsp_uart_send(const uint8_t *data, int len);

/**
 * @brief 发送字符串
 * @param str 字符串
 * @return 发送的字节数
 */
int bsp_uart_send_str(const char *str);

/**
 * @brief 格式化发送
 * @param fmt 格式字符串
 * @return 发送的字节数
 */
int bsp_uart_printf(const char *fmt, ...);

/**
 * @brief 注册命令回调
 * @param cb 回调函数
 */
void bsp_uart_set_cmd_callback(uart_cmd_callback_t cb);

/**
 * @brief 启动UART接收任务
 * @return ESP_OK 成功
 */
esp_err_t bsp_uart_start_recv_task(void);

#ifdef __cplusplus
}
#endif

#endif /* _BSP_UART_H_ */


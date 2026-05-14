
#include "bsp_uart.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bsp/uart";

static bool s_uart_initialized = false;
static uart_cmd_callback_t s_cmd_callback = NULL;
static TaskHandle_t s_recv_task_handle = NULL;

// UART 接收任务
static void uart_recv_task(void *arg)
{
    uint8_t *data = (uint8_t *)malloc(BSP_UART_BUF_SIZE);
    if (data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate UART buffer");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        int len = uart_read_bytes(BSP_UART_NUM, data, BSP_UART_BUF_SIZE - 1, pdMS_TO_TICKS(100));
        if (len > 0) {
            data[len] = '\0';
            ESP_LOGD(TAG, "UART RX: %s", data);
            
            if (s_cmd_callback) {
                s_cmd_callback((const char *)data, len);
            }
        }
    }

    free(data);
    vTaskDelete(NULL);
}

esp_err_t bsp_uart_init(void)
{
    if (s_uart_initialized) {
        return ESP_OK;
    }

    uart_config_t uart_config = {
        .baud_rate = BSP_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install(BSP_UART_NUM, BSP_UART_BUF_SIZE * 2, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %d", ret);
        return ret;
    }

    ret = uart_param_config(BSP_UART_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %d", ret);
        return ret;
    }

    ret = uart_set_pin(BSP_UART_NUM, BSP_UART_TX_PIN, BSP_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %d", ret);
        return ret;
    }

    s_uart_initialized = true;
    ESP_LOGD(TAG, "UART initialized (TX:%d, RX:%d, Baud:%d)", BSP_UART_TX_PIN, BSP_UART_RX_PIN, BSP_UART_BAUD_RATE);

    return ESP_OK;
}

esp_err_t bsp_uart_deinit(void)
{
    if (!s_uart_initialized) {
        return ESP_OK;
    }

    // 停止并删除接收任务
    if (s_recv_task_handle) {
        vTaskDelete(s_recv_task_handle);
        s_recv_task_handle = NULL;
    }

    // 删除 UART 驱动
    esp_err_t ret = uart_driver_delete(BSP_UART_NUM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_delete failed: %d", ret);
        return ret;
    }

    s_uart_initialized = false;
    ESP_LOGD(TAG, "UART 已反初始化");

    return ESP_OK;
}

int bsp_uart_send(const uint8_t *data, int len)
{
    if (!s_uart_initialized || data == NULL || len <= 0) {
        return -1;
    }
    return uart_write_bytes(BSP_UART_NUM, data, len);
}

int bsp_uart_send_str(const char *str)
{
    if (!s_uart_initialized || str == NULL) {
        return -1;
    }
    return uart_write_bytes(BSP_UART_NUM, str, strlen(str));
}

int bsp_uart_printf(const char *fmt, ...)
{
    if (!s_uart_initialized || fmt == NULL) {
        return -1;
    }

    char buf[BSP_UART_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len > 0) {
        return uart_write_bytes(BSP_UART_NUM, buf, len);
    }
    return len;
}

void bsp_uart_set_cmd_callback(uart_cmd_callback_t cb)
{
    s_cmd_callback = cb;
}

esp_err_t bsp_uart_start_recv_task(void)
{
    if (!s_uart_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_recv_task_handle != NULL) {
        return ESP_OK;
    }

    BaseType_t ret = xTaskCreate(uart_recv_task, "uart_recv", 8192, NULL, 10, &s_recv_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UART recv task");
        return ESP_FAIL;
    }

    return ESP_OK;
}


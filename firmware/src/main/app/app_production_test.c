/**
 * @file app_factorytest.c
 * @brief 产测模式实现
 * @author Enable
 * @date 2025/12/08
 */

#include "app_production_test.h"
#include "app_factory.h"
#include "bsp_uart.h"
#include "bsp_rgb_led.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"


static const char *TAG = "app/production_test";

// WiFi 事件组
static EventGroupHandle_t s_wifi_event_group = NULL;
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

// 状态
static bool s_production_test_initialized = false;
static bool s_production_test_enabled = false;
static wifi_conn_state_t s_wifi_state = WIFI_STATE_DISCONNECTED;
static char s_wifi_ip[16] = {0};
static int s_retry_num = 0;
#define WIFI_MAX_RETRY  5

// RGB 测试任务
static TaskHandle_t s_rgb_test_task_handle = NULL;
static bool s_rgb_test_running = false;

// RGB 闪烁测试任务（红绿蓝白黑循环）
static void rgb_test_task(void *arg)
{
    // RGB 颜色定义
    const uint8_t colors[5][3] = {
        {255, 0, 0},       // 红
        {0, 255, 0},       // 绿
        {0, 0, 255},       // 蓝
        {255, 255, 255},   // 白
        {0, 0, 0}          // 黑
    };
    const char *color_names[] = {"红", "绿", "蓝", "白", "黑"};
    const int color_count = 5;
    const int delay_ms = 500;
    int color_index = 0;
    
    ESP_LOGI(TAG, "RGB test task started");
    while (s_rgb_test_running) {
        bsp_rgb_led_set_color(colors[color_index][0], 
                              colors[color_index][1], 
                              colors[color_index][2]);
        
        ESP_LOGI(TAG, "RGB test: %s (R:%d, G:%d, B:%d)", 
                 color_names[color_index],
                 colors[color_index][0],
                 colors[color_index][1],
                 colors[color_index][2]);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        color_index = (color_index + 1) % color_count;
    }
    bsp_rgb_led_set_color(0, 0, 0);
    s_rgb_test_task_handle = NULL;
    ESP_LOGI(TAG, "RGB test task stopped");
    vTaskDelete(NULL);
}

// WiFi 事件处理
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            if (s_wifi_event_group) {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
            s_wifi_state = WIFI_STATE_FAILED;
            bsp_uart_printf("+WIFI:FAILED,max_retry\r\n");
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_wifi_ip, sizeof(s_wifi_ip), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Got IP: %s", s_wifi_ip);
        s_retry_num = 0;
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
        s_wifi_state = WIFI_STATE_CONNECTED;
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            bsp_uart_printf("+WIFI:CONNECTED,%d\r\n", ap_info.rssi);
        } else {
            bsp_uart_printf("+WIFI:CONNECTED,0\r\n");
        }
    }
}

static esp_err_t wifi_init(void)
{
    static bool wifi_initialized = false;
    if (wifi_initialized) {
        return ESP_OK;
    }

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_initialized = true;
    return ESP_OK;
}

static void uart_cmd_handler(const char *cmd, int len)
{
    // 去除首尾空白字符（包括 \r\n）
    while (len > 0 && (cmd[len-1] == '\r' || cmd[len-1] == '\n' || cmd[len-1] == ' ' || cmd[len-1] == '\t')) {
        len--;
    }
    const char *cmd_start = cmd;
    while (len > 0 && (*cmd_start == ' ' || *cmd_start == '\t')) {
        cmd_start++;
        len--;
    }
    if (len == 0) {
        return;
    }
    
    ESP_LOGI(TAG, "CMD: %.*s", len, cmd_start);

    // AT+WIFI=SSID,PASSWORD
    if (strncmp(cmd_start, "AT+WIFI=", 8) == 0) {
        const char *params = cmd_start + 8;
        if (strncmp(params, "DISCONNECT", 10) == 0) {
            app_production_test_wifi_disconnect();
            bsp_uart_printf("+WIFI:DISCONNECTED\r\n");
            return;
        }
        char ssid[33] = {0};
        char password[65] = {0};
        const char *comma = strchr(params, ',');
        if (comma) {
            int ssid_len = comma - params;
            if (ssid_len > 0 && ssid_len < 33) {
                strncpy(ssid, params, ssid_len);
                const char *pwd_start = comma + 1;
                int pwd_len = strlen(pwd_start);
                while (pwd_len > 0 && (pwd_start[pwd_len-1] == '\r' || pwd_start[pwd_len-1] == '\n')) {
                    pwd_len--;
                }
                if (pwd_len < 65) {
                    strncpy(password, pwd_start, pwd_len);
                }
                
                ESP_LOGI(TAG, "WiFi connect: SSID=%s, PWD=%s", ssid, password);
                bsp_uart_printf("+WIFI:CONNECTING\r\n");
                app_production_test_wifi_connect(ssid, password);
            }
        }
    }
    // AT+WIFI?
    else if (strncmp(cmd_start, "AT+WIFI?", 8) == 0) {
        if (s_wifi_state == WIFI_STATE_CONNECTED) {
            // 获取 RSSI
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                bsp_uart_printf("+WIFI:CONNECTED,%d\r\n", ap_info.rssi);
            } else {
                bsp_uart_printf("+WIFI:CONNECTED,0\r\n");
            }
        } else if (s_wifi_state == WIFI_STATE_CONNECTING) {
            bsp_uart_printf("+WIFI:CONNECTING\r\n");
        } else {
            bsp_uart_printf("+WIFI:DISCONNECTED\r\n");
        }
    }
    // AT+VERSION?
    else if (strncmp(cmd_start, "AT+VERSION?", 11) == 0) {
        bsp_uart_printf("+VERSION:0x%02X\r\n", FIRMWARE_VERSION);
    }
    // AT+RGB=R,G,B
    else if (strncmp(cmd_start, "AT+RGB=", 7) == 0) {
        int r = 0, g = 0, b = 0;
        if (sscanf(cmd_start + 7, "%d,%d,%d", &r, &g, &b) == 3) {
            bsp_rgb_led_set_color((uint8_t)r, (uint8_t)g, (uint8_t)b);
            bsp_uart_printf("+RGB:OK\r\n");
        } else {
            bsp_uart_printf("+RGB:ERROR\r\n");
        }
    }
    // AT+FACTORY=0/1
    else if (strncmp(cmd_start, "AT+FACTORY=", 11) == 0) {
        int enable = atoi(cmd_start + 11);
        s_production_test_enabled = (enable != 0);
        bsp_uart_printf("+FACTORY:%d\r\n", s_production_test_enabled ? 1 : 0);
        ESP_LOGI(TAG, "Factory test mode: %s", s_production_test_enabled ? "ENABLED" : "DISABLED");
    }
    // AT+FACTORY?
    else if (strncmp(cmd_start, "AT+FACTORY?", 11) == 0) {
        bsp_uart_printf("+FACTORY:%d\r\n", s_production_test_enabled ? 1 : 0);
    }
    // AT+RGB_TEST=0/1 (启动/停止 RGB 闪烁测试)
    else if (strncmp(cmd_start, "AT+RGB_TEST=", 12) == 0) {
        ESP_LOGI(TAG, "RGB_TEST");
        int enable = atoi(cmd_start + 12);
        if (enable != 0) {
            if (s_rgb_test_task_handle == NULL) {
                s_rgb_test_running = true;
                ESP_LOGI(TAG, "RGB test task started");
                xTaskCreate(rgb_test_task, "rgb_test", 8192, NULL, 3, &s_rgb_test_task_handle);
                bsp_uart_printf("+RGB_TEST:STARTED\r\n");
            } else {
                bsp_uart_printf("+RGB_TEST:ALREADY_RUNNING\r\n");
            }
        } else {
            if (s_rgb_test_task_handle != NULL) {
                s_rgb_test_running = false;
                int wait_count = 0;
                while (s_rgb_test_task_handle != NULL && wait_count < 10) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                    wait_count++;
                }
                if (s_rgb_test_task_handle != NULL) {
                    vTaskDelete(s_rgb_test_task_handle);
                    s_rgb_test_task_handle = NULL;
                }
                bsp_rgb_led_set_color(0, 0, 0);
                bsp_uart_printf("+RGB_TEST:STOPPED\r\n");
            } else {
                bsp_uart_printf("+RGB_TEST:NOT_RUNNING\r\n");
            }
        }
    } else {
        bsp_uart_printf("+ERROR:UNKNOWN_CMD\r\n");
    }
}

esp_err_t app_production_test_init(void)
{
    if (s_production_test_initialized) {
        return ESP_OK;
    }

    esp_err_t ret;

    ret = wifi_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %d", ret);
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(300));

    ret = bsp_uart_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART init failed: %d", ret);
        return ret;
    }

    bsp_uart_set_cmd_callback(uart_cmd_handler);
    ret = bsp_uart_start_recv_task();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART recv task start failed: %d", ret);
        return ret;
    }

    s_production_test_initialized = true;
    ESP_LOGI(TAG, "Production test initialized");
    bsp_uart_printf("+READY\r\n");

    return ESP_OK;
}

esp_err_t app_production_test_deinit(void)
{
    if (!s_production_test_initialized) {
        return ESP_OK;
    }

    if (s_rgb_test_task_handle != NULL) {
        s_rgb_test_running = false;
        int wait_count = 0;
        while (s_rgb_test_task_handle != NULL && wait_count < 10) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wait_count++;
        }
        if (s_rgb_test_task_handle != NULL) {
            vTaskDelete(s_rgb_test_task_handle);
            s_rgb_test_task_handle = NULL;
        }
        bsp_rgb_led_set_color(0, 0, 0);
    }

    app_production_test_wifi_disconnect();
    bsp_uart_deinit();

    s_production_test_initialized = false;
    return ESP_OK;
}

esp_err_t app_production_test_wifi_connect(const char *ssid, const char *password)
{
    if (ssid == NULL || strlen(ssid) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(100));
    wifi_config_t wifi_config = {
        .sta = {
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            .threshold.rssi = -70,
            .threshold.authmode = (password && strlen(password) > 0) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN,
            .threshold.rssi_5g_adjustment = 0,
        },
    };

    strcpy((char *)wifi_config.sta.ssid, ssid);
    if (password && strlen(password) > 0) {
        strcpy((char *)wifi_config.sta.password, password);
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_wifi_state = WIFI_STATE_CONNECTING;
    s_retry_num = 0;
    memset(s_wifi_ip, 0, sizeof(s_wifi_ip));

    ESP_LOGI(TAG, "WiFi connecting to SSID: %s", ssid);
    return ESP_OK;
}

esp_err_t app_production_test_wifi_disconnect(void)
{
    esp_wifi_disconnect();
    esp_wifi_stop();
    s_wifi_state = WIFI_STATE_DISCONNECTED;
    memset(s_wifi_ip, 0, sizeof(s_wifi_ip));
    return ESP_OK;
}

wifi_conn_state_t app_production_test_wifi_get_state(void)
{
    return s_wifi_state;
}

const char *app_production_test_wifi_get_ip(void)
{
    if (s_wifi_state == WIFI_STATE_CONNECTED && strlen(s_wifi_ip) > 0) {
        return s_wifi_ip;
    }
    return NULL;
}

bool app_production_test_is_enabled(void)
{
    return s_production_test_enabled;
}

void app_production_test_set_enabled(bool enable)
{
    s_production_test_enabled = enable;
}


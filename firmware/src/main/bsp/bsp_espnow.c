/**
 * @file bsp_espnow.c
 * @brief ESP-NOW 广播模式 BSP 实现
 * @author Enable
 * @date 2025/12/08
 */

#include "bsp_espnow.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "bsp/espnow";

// ESP-NOW 广播地址
static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
// Wi-Fi 信道 0
#define WIFI_CHANNEL 0
// 是否初始化
static bool espnow_initialized = false;

/**
 * @brief ESP-NOW 发送回调
 */
static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGD(TAG, "ESP-NOW 发送成功");
    } else {
        ESP_LOGW(TAG, "ESP-NOW 发送失败");
    }
}

/**
 * @brief ESP-NOW 初始化
 */
esp_err_t bsp_espnow_init(void)
{
    if (espnow_initialized) {
        return ESP_OK;
    }
 
    ESP_ERROR_CHECK(esp_netif_init());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_err_t ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW 初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }
    esp_now_register_send_cb((esp_now_send_cb_t)espnow_send_cb);

    // 添加广播对等点
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, broadcast_mac, 6);
    peer.channel = WIFI_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    ret = esp_now_add_peer(&peer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "添加广播对等点失败: %s", esp_err_to_name(ret));
        esp_now_deinit();
        return ret;
    }
    espnow_initialized = true;
    ESP_LOGD(TAG, "ESP-NOW 广播模式初始化成功");

    return ESP_OK;
}

/**
 * @brief ESP-NOW 反初始化
 */
esp_err_t bsp_espnow_deinit(void)
{
    if (!espnow_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = esp_now_del_peer(broadcast_mac);
    if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "delete peer failed: %s", esp_err_to_name(ret));
    }

    ret = esp_now_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "deinit esp-now failed: %s", esp_err_to_name(ret));
    }

    ret = esp_wifi_stop();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "stop wifi failed: %s", esp_err_to_name(ret));
    }

    ret = esp_wifi_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "deinit wifi failed: %s", esp_err_to_name(ret));
    }

    esp_netif_deinit();
    
    espnow_initialized = false;

    return ESP_OK;
}

void bsp_espnow_add_key(char c)
{
    if (!espnow_initialized) {
        return;
    }

    if (c == 0) {
        return;
    }

    uint8_t data[1] = {(uint8_t)c};
    esp_err_t ret = esp_now_send(broadcast_mac, data, 1);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "send char: %c (0x%02X)", c, (uint8_t)c);
    } else {
        ESP_LOGW(TAG, "send char failed: %s", esp_err_to_name(ret));
    }
}

bool bsp_espnow_is_initialized(void)
{
    return espnow_initialized;
}

esp_err_t bsp_espnow_send(const uint8_t *data, size_t len)
{
    if (!espnow_initialized || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = esp_now_send(broadcast_mac, data, len);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "send data: len=%u", (unsigned)len);
    } else {
        ESP_LOGW(TAG, "send data failed: %s", esp_err_to_name(ret));
    }
    return ret;
}


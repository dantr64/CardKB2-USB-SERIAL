/******************************************************************************
 * M5Stack Unit CardKB2 Factory
 * @author: Enable
 * @date: 2025/12/06
 * 
 * 
 * component config --> Bluetooth HID --> Enable    
 * 
 ******************************************************************************/
#include "app_factory.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "nvs_flash.h"


static const char *TAG = "main";


void app_main(void)
{
    ESP_LOGI(TAG, "CardKB2 Start");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    xTaskCreate(app_factory, "app_factory", 8192, NULL, 5, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

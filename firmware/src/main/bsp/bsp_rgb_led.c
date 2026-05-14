/*******************************************************************************
 
*******************************************************************************/
#include "bsp_rgb_led.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/gpio.h"

static const char *TAG = "bsp/rgb_led";

// RGB LED 配置
#define RGB_R_PIN       GPIO_NUM_27
#define RGB_G_PIN       GPIO_NUM_28
#define RGB_B_PIN       GPIO_NUM_9
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL_R  LEDC_CHANNEL_0
#define LEDC_CHANNEL_G  LEDC_CHANNEL_1
#define LEDC_CHANNEL_B  LEDC_CHANNEL_2
#define LEDC_DUTY_RES   LEDC_TIMER_8_BIT
#define LEDC_FREQUENCY  5000

// 当前 LED 颜色值（用于单独设置通道）
static uint8_t current_r = 0;
static uint8_t current_g = 0;
static uint8_t current_b = 0;


esp_err_t bsp_rgb_led_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK
    };
    esp_err_t ret = ledc_timer_config(&ledc_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ledc_channel_config_t ledc_channel_r = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL_R,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = RGB_R_PIN,
        .duty = 0,
        .hpoint = 0
    };
    ret = ledc_channel_config(&ledc_channel_r);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Red channel config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ledc_channel_config_t ledc_channel_g = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL_G,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = RGB_G_PIN,
        .duty = 0,
        .hpoint = 0
    };
    ret = ledc_channel_config(&ledc_channel_g);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Green channel config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ledc_channel_config_t ledc_channel_b = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL_B,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = RGB_B_PIN,
        .duty = 0,
        .hpoint = 0
    };
    ret = ledc_channel_config(&ledc_channel_b);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Blue channel config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGD(TAG, "RGB LED initialized");
    return ESP_OK;
}

void bsp_rgb_led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    current_r = r;
    current_g = g;
    current_b = b;
    
    // 共阳极 LED，需要反转 PWM 值
    uint32_t duty_r = 256 - r;
    uint32_t duty_g = 256 - g;
    uint32_t duty_b = 256 - b;

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_R, duty_r);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_R);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_G, duty_g);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_G);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_B, duty_b);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_B);
}

void bsp_rgb_led_set_red(uint8_t r)
{
    current_r = r;
    uint32_t duty_r = 256 - r;
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_R, duty_r);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_R);
}

void bsp_rgb_led_set_green(uint8_t g)
{
    current_g = g;
    uint32_t duty_g = 256 - g;
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_G, duty_g);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_G);
}

void bsp_rgb_led_set_blue(uint8_t b)
{
    current_b = b;
    uint32_t duty_b = 256 - b;
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_B, duty_b);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_B);
}

void bsp_rgb_led_get_color(uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (r) *r = current_r;
    if (g) *g = current_g;
    if (b) *b = current_b;
}


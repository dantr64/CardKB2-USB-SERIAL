/*******************************************************************************
 * M5Stack Unit CardKB2 I2C 从机 BSP
 * 
 * I2C 从机配置：
 * - 地址：0x5F
 * - SCL：GPIO 25
 * - SDA：GPIO 26
 * 
 * 寄存器定义：
 * - 0xF1：固件版本寄存器（写入 0xF1，然后读取获取版本号）
 * - 0xF0：产测模式控制寄存器（写入 0x01 进入产测模式，退出只能通过切换到其他模式）
 * 
 * 使用方法：
 * 1. 读取按键：直接读取 I2C，返回按键 ASCII 码（无按键返回 0）
 * 2. 读取版本：先写入 0xF1，然后读取，返回版本号
 *******************************************************************************/
#include "bsp_i2c.h"
#include "app_factory.h"
#include "esp_log.h"
#include "driver/i2c_slave.h"
#include "driver/i2c_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG = "bsp/i2c";

// I2C 从机配置
#define I2C_SLAVE_PORT             0       // I2C 端口号
#define I2C_SLAVE_SCL_IO           25      // GPIO 25
#define I2C_SLAVE_SDA_IO           26      // GPIO 26
#define I2C_SLAVE_ADDR             0x5F    // I2C 从机地址
#define I2C_SLAVE_TX_BUF_LEN       32      // 发送缓冲区长度
#define I2C_SLAVE_RX_BUF_LEN       32      // 接收缓冲区长度

// 按键队列配置
#define KEY_QUEUE_SIZE             32      // 队列大小

// 按键队列结构（环形缓冲区）
typedef struct {
    uint8_t buffer[KEY_QUEUE_SIZE];
    volatile uint16_t head;        // 队列头（写入位置）
    volatile uint16_t tail;        // 队列尾（读取位置）
    SemaphoreHandle_t mutex;       // 互斥锁
} key_queue_t;

static key_queue_t key_queue = {0};
static i2c_slave_dev_handle_t i2c_slave_handle = NULL;
static volatile uint8_t last_register = 0;  // 最后写入的寄存器地址
static bool i2c_initialized = false;  // I2C 从机初始化状态

// 寄存器地址定义
#define REG_FIRMWARE_VERSION   0xF1  // 固件版本寄存器
#define REG_PRODUCTION_TEST    0xF0  // 产测模式控制寄存器

// 队列操作函数
static bool key_queue_is_full(void)
{
    return ((key_queue.head + 1) % KEY_QUEUE_SIZE) == key_queue.tail;
}

static bool key_queue_is_empty(void)
{
    return key_queue.head == key_queue.tail;
}

static bool key_queue_push(uint8_t key)
{
    if (xSemaphoreTake(key_queue.mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return false;
    }
    
    bool result = false;
    if (!key_queue_is_full()) {
        key_queue.buffer[key_queue.head] = key;
        key_queue.head = (key_queue.head + 1) % KEY_QUEUE_SIZE;
        result = true;
    }
    
    xSemaphoreGive(key_queue.mutex);
    return result;
}

static bool key_queue_pop_from_isr(uint8_t *key)
{
    // 在 ISR 中直接访问，不使用互斥锁
    // 使用 volatile 变量确保可见性
    if (key_queue.head == key_queue.tail) {
        return false;  // 队列为空
    }
    
    *key = key_queue.buffer[key_queue.tail];
    key_queue.tail = (key_queue.tail + 1) % KEY_QUEUE_SIZE;
    return true;
}

static IRAM_ATTR bool i2c_slave_receive_callback(i2c_slave_dev_handle_t handle, const i2c_slave_rx_done_event_data_t *edata, void *user_data)
{
    if (edata->length > 0 && edata->buffer != NULL) {
        if (edata->length >= 1) {
            uint8_t reg = edata->buffer[0];
            last_register = reg;  // 保存寄存器地址
            
            if (edata->length >= 2) {
                // 如果写入 2 字节，第一个是寄存器地址，第二个是值
                uint8_t value = edata->buffer[1];
                
                if (reg == REG_PRODUCTION_TEST) {
                    if (value == 0x01) {
                        // 触发产测模式切换：通过消息队列发送模式值
                        extern QueueHandle_t mode_queue;
                        app_mode_t mode = MODE_PRODTEST;
                        
                        if (mode_queue != NULL) {
                            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                            xQueueSendFromISR(mode_queue, &mode, &xHigherPriorityTaskWoken);
                            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
                        }
                        return true;
                    }
                }
            }
        }
    }
    
    return false;
}

// I2C 从机发送请求回调（当主机请求读取数据时调用）
static IRAM_ATTR bool i2c_slave_request_callback(i2c_slave_dev_handle_t handle, const i2c_slave_request_event_data_t *evt_data, void *user_data)
{
    uint8_t tx_data[1] = {0};
    uint32_t write_len = 0;
    
    // 检查是否是读取寄存器（如果最后写入的寄存器是版本寄存器）
    if (last_register == REG_FIRMWARE_VERSION) {
        tx_data[0] = FIRMWARE_VERSION;
        last_register = 0;
    } else {
        uint8_t key = 0;
        if (!key_queue_pop_from_isr(&key)) {
            key = 0;
        }
        tx_data[0] = key;
    }
    
    i2c_slave_write(handle, tx_data, 1, &write_len, 0);
    
    return false;
}

esp_err_t bsp_i2c_init(void)
{
    if (i2c_initialized) {
        ESP_LOGD(TAG, "I2C slave already initialized");
        return ESP_OK;
    }
    ESP_LOGD(TAG, "initialize I2C slave...");
    
    key_queue.mutex = xSemaphoreCreateMutex();
    if (key_queue.mutex == NULL) {
        ESP_LOGE(TAG, "create mutex failed");
        return ESP_ERR_NO_MEM;
    }
    key_queue.head = 0;
    key_queue.tail = 0;
    
    i2c_slave_config_t i2c_slave_config = {
        .i2c_port = I2C_SLAVE_PORT,
        .sda_io_num = I2C_SLAVE_SDA_IO,
        .scl_io_num = I2C_SLAVE_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .send_buf_depth = I2C_SLAVE_TX_BUF_LEN,
        .receive_buf_depth = I2C_SLAVE_RX_BUF_LEN,
        .slave_addr = I2C_SLAVE_ADDR,
        .addr_bit_len = I2C_ADDR_BIT_LEN_7,
        .intr_priority = 0,
        .flags = {
            .enable_internal_pullup = true,
        }
    };
    
    esp_err_t ret = i2c_new_slave_device(&i2c_slave_config, &i2c_slave_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "create I2C slave device failed: %s", esp_err_to_name(ret));
        vSemaphoreDelete(key_queue.mutex);
        return ret;
    }
    
    i2c_slave_event_callbacks_t cbs = {
        .on_request = i2c_slave_request_callback,
        .on_receive = i2c_slave_receive_callback,
    };
    
    ret = i2c_slave_register_event_callbacks(i2c_slave_handle, &cbs, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register I2C slave event callback failed: %s", esp_err_to_name(ret));
        i2c_del_slave_device(i2c_slave_handle);
        i2c_slave_handle = NULL;
        vSemaphoreDelete(key_queue.mutex);
        return ret;
    }
    
    ESP_LOGD(TAG, "I2C slave initialized (address: 0x%02X, SCL: GPIO%d, SDA: GPIO%d)", I2C_SLAVE_ADDR, I2C_SLAVE_SCL_IO, I2C_SLAVE_SDA_IO);
    
    return ESP_OK;
}

esp_err_t bsp_i2c_deinit(void)
{
    if (i2c_slave_handle == NULL) {
        return ESP_OK;
    }

    esp_err_t ret = i2c_del_slave_device(i2c_slave_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "delete I2C slave device failed: %s", esp_err_to_name(ret));
        return ret;
    }
    i2c_slave_handle = NULL;

    if (key_queue.mutex) {
        vSemaphoreDelete(key_queue.mutex);
        key_queue.mutex = NULL;
    }

    key_queue.head = 0;
    key_queue.tail = 0;
 
    return ESP_OK;
}

void bsp_i2c_add_key(uint8_t key)
{
    if (key == 0) {
        return;
    }
    
    if (key_queue_push(key)) {
        ESP_LOGD(TAG, "key added to I2C queue: 0x%02X ('%c')", key, (key >= 32 && key <= 126) ? key : '?');
    } else {
        ESP_LOGD(TAG, "key queue is full, discard key: 0x%02X", key);
    }
}

void bsp_i2c_add_key_index(uint8_t button_index)
{
    if (key_queue_push(button_index)) {
        ESP_LOGD(TAG, "key index added to I2C queue: %d (row=%d, col=%d)", 
                 button_index, button_index / 11, button_index % 11);
    } else {
        ESP_LOGD(TAG, "key queue is full, discard key index: %d", button_index);
    }
}



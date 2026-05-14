/**
 * @file bsp_blehid.c
 * @brief BLE HID 键盘驱动实现 
 * @author Enable
 * @date 2025/12/08
 */

#include "bsp_blehid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"
#include "esp_hidd.h"
#include "esp_hid_common.h"
#include "esp_mac.h"

static const char *TAG = "BLE_HID";

// 设备名称缓冲区（包含 MAC 后缀）
static char s_ble_device_name[32] = {0};

static esp_err_t esp_hid_ble_gap_adv_start(void);
static void ble_hid_task_start_up(void);
static void ble_hid_task_shut_down(void);

static SemaphoreHandle_t ble_hidh_cb_semaphore = NULL;

typedef struct {
    TaskHandle_t task_hdl;
    esp_hidd_dev_t *hid_dev;
    uint8_t protocol_mode;
    uint8_t *buffer;
} local_param_t;

static local_param_t s_ble_hid_param = {0};

// =========================================================================== 
// Keyboard Report Map 
// ===========================================================================
static const unsigned char keyboardReportMap[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0xE0,        //   Usage Minimum (0xE0)
    0x29, 0xE7,        //   Usage Maximum (0xE7)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Const,Array,Abs) - 保留字节，必须是 Array
    0x95, 0x05,        //   Report Count (5) - LED 保留位部分
    0x75, 0x01,        //   Report Size (1)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (Num Lock)
    0x29, 0x05,        //   Usage Maximum (Kana)
    0x91, 0x02,        //   Output (Data,Var,Abs) - LED 数据位（5 bits）
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3) - LED 保留位（3 bits）
    0x91, 0x01,        //   Output (Const,Array,Abs) - 保留位，必须是 Array
    0x95, 0x06,        //   Report Count (6) - 按键数组（6 个字节）
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0x00,        //   Usage Minimum (0x00)
    0x29, 0x65,        //   Usage Maximum (0x65)
    0x81, 0x00,        //   Input (Data,Array,Abs)
    0xC0,              // End Collection
};

static esp_hid_raw_report_map_t ble_report_maps[] = {
    {
        .data = keyboardReportMap,
        .len = sizeof(keyboardReportMap)
    }
};

static esp_hid_device_config_t ble_hid_config = {
    .vendor_id          = 0x16C0,
    .product_id         = 0x05DF,
    .version            = 0x0100,
    .device_name        = s_ble_device_name,  // 使用动态生成的设备名称
    .manufacturer_name  = "M5Stack",
    .serial_number      = "CardKB2-001",
    .report_maps        = ble_report_maps,
    .report_maps_len    = 1
};

// 状态
static bool s_ble_hid_initialized = false;
static ble_hid_state_t s_ble_hid_state = BLE_HID_STATE_DISCONNECTED;
static ble_hid_event_cb_t s_event_cb = NULL;
static bool s_client_subscribed = false;  // 客户端是否已订阅特征值（Notify/Indicate）


static void char_to_code(uint8_t *buffer, char ch)
{
    if (ch >= 'a' && ch <= 'z') {
        buffer[0] = 0;
        buffer[2] = (uint8_t)(4 + (ch - 'a'));
    } else if (ch >= 'A' && ch <= 'Z') {
        buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT;
        ch = ch - ('A' - 'a');
        buffer[2] = (uint8_t)(4 + (ch - 'a'));
    } else if (ch >= '0' && ch <= '9') {
        buffer[0] = 0;
        if (ch == '0') {
            buffer[2] = 39;
        } else {
            buffer[2] = (uint8_t)(30 + (ch - '1'));
        }
    } else {
        switch (ch) {
            case ' ':  buffer[0] = 0; buffer[2] = 0x2C; break;
            case '.':  buffer[0] = 0; buffer[2] = 0x37; break;
            case '\n': buffer[0] = 0; buffer[2] = 0x28; break;
            case '\b': buffer[0] = 0; buffer[2] = 0x2A; break;
            case '\t': buffer[0] = 0; buffer[2] = 0x2B; break;
            case '?':  buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT; buffer[2] = 0x38; break;
            case '/':  buffer[0] = 0; buffer[2] = 0x38; break;
            case '\\': buffer[0] = 0; buffer[2] = 0x31; break;
            case '|':  buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT; buffer[2] = 0x31; break;
            case ',':  buffer[0] = 0; buffer[2] = 0x36; break;
            case '<':  buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT; buffer[2] = 0x36; break;
            case '>':  buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT; buffer[2] = 0x37; break;
            case '@':  buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT; buffer[2] = 31; break;
            case '!':  buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT; buffer[2] = 30; break;
            case '#':  buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT; buffer[2] = 32; break;
            case '$':  buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT; buffer[2] = 33; break;
            case '%':  buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT; buffer[2] = 34; break;
            case '^':  buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT; buffer[2] = 35; break;
            case '&':  buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT; buffer[2] = 36; break;
            case '*':  buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT; buffer[2] = 37; break;
            case '(':  buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT; buffer[2] = 38; break;
            case ')':  buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT; buffer[2] = 39; break;
            case '-':  buffer[0] = 0; buffer[2] = 0x2D; break;
            case '_':  buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT; buffer[2] = 0x2D; break;
            case '=':  buffer[0] = 0; buffer[2] = 0x2E; break;
            case '+':  buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT; buffer[2] = 0x2E; break;
            case '[':  buffer[0] = 0; buffer[2] = 0x2F; break;
            case '{':  buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT; buffer[2] = 0x2F; break;
            case ']':  buffer[0] = 0; buffer[2] = 0x30; break;
            case '}':  buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT; buffer[2] = 0x30; break;
            case ';':  buffer[0] = 0; buffer[2] = 0x33; break;
            case ':':  buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT; buffer[2] = 0x33; break;
            case '\'': buffer[0] = 0; buffer[2] = 0x34; break;
            case '"':  buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT; buffer[2] = 0x34; break;
            case '`':  buffer[0] = 0; buffer[2] = 0x35; break;
            case '~':  buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT; buffer[2] = 0x35; break;
            default:   buffer[0] = 0; buffer[2] = 0; break;
        }
    }
}

static void send_keyboard(char c)
{
    static uint8_t buffer[8] = {0};
    char_to_code(buffer, c);
    
    esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, 1, buffer, 8);
    memset(buffer, 0, sizeof(uint8_t) * 8);
    esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, 1, buffer, 8);
}

static void ble_hid_task_start_up(void)
{
    if (s_ble_hid_param.task_hdl) {
        return;
    }
    ESP_LOGD(TAG, "BLE HID Keyboard Ready");
    s_ble_hid_state = BLE_HID_STATE_CONNECTED;
    if (s_event_cb) {
        s_event_cb(BLE_HID_STATE_CONNECTED);
    }
}

static void ble_hid_task_shut_down(void)
{
    if (s_ble_hid_param.task_hdl) {
        vTaskDelete(s_ble_hid_param.task_hdl);
        s_ble_hid_param.task_hdl = NULL;
    }
    s_ble_hid_state = BLE_HID_STATE_DISCONNECTED;
    if (s_event_cb) {
        s_event_cb(BLE_HID_STATE_DISCONNECTED);
    }
}

static void ble_hidd_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_hidd_event_t event = (esp_hidd_event_t)id;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;

    switch (event) {
    case ESP_HIDD_START_EVENT: {
        ESP_LOGD(TAG, "START");
        esp_hid_ble_gap_adv_start();
        break;
    }
    case ESP_HIDD_CONNECT_EVENT: {
        ESP_LOGI(TAG, "CONNECT");
        // 连接时，客户端可能还未订阅特征值，先标记为未订阅
        s_client_subscribed = false;
        ble_hid_task_start_up();
        break;
    }
    case ESP_HIDD_PROTOCOL_MODE_EVENT: {
        ESP_LOGI(TAG, "PROTOCOL MODE[%u]: %s", param->protocol_mode.map_index, param->protocol_mode.protocol_mode ? "REPORT" : "BOOT");
        break;
    }
    case ESP_HIDD_CONTROL_EVENT: {
        ESP_LOGD(TAG, "CONTROL[%u]: %sSUSPEND", param->control.map_index, param->control.control ? "EXIT_" : "");
        if (param->control.control) {
            ble_hid_task_start_up();
        } else {
            ble_hid_task_shut_down();
        }
        break;
    }
    case ESP_HIDD_OUTPUT_EVENT: {
        ESP_LOGI(TAG, "OUTPUT[%u]: %8s ID: %2u, Len: %d, Data:", param->output.map_index, esp_hid_usage_str(param->output.usage), param->output.report_id, param->output.length);
        ESP_LOG_BUFFER_HEX(TAG, param->output.data, param->output.length);
        s_client_subscribed = true;
        ESP_LOGI(TAG, "Client subscribed, ready to send HID reports");
        ble_hid_task_start_up();
        break;
    }
    case ESP_HIDD_FEATURE_EVENT: {
        ESP_LOGD(TAG, "FEATURE[%u]: %8s ID: %2u, Len: %d, Data:", param->feature.map_index, esp_hid_usage_str(param->feature.usage), param->feature.report_id, param->feature.length);
        ESP_LOG_BUFFER_HEX(TAG, param->feature.data, param->feature.length);
        break;
    }
    case ESP_HIDD_DISCONNECT_EVENT: {
        ESP_LOGI(TAG, "DISCONNECT: %s", esp_hid_disconnect_reason_str(esp_hidd_dev_transport_get(param->disconnect.dev), param->disconnect.reason));
        s_client_subscribed = false;
        ble_hid_task_shut_down();
        esp_hid_ble_gap_adv_start();
        break;
    }
    case ESP_HIDD_STOP_EVENT: {
        ESP_LOGD(TAG, "STOP");
        break;
    }
    default:
        break;
    }
}

static void ble_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_LOGV(TAG, "BLE GAP ADV_DATA_SET_COMPLETE");
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        ESP_LOGV(TAG, "BLE GAP ADV_START_COMPLETE");
        s_ble_hid_state = BLE_HID_STATE_ADVERTISING;
        break;

    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        if (!param->ble_security.auth_cmpl.success) {
            ESP_LOGE(TAG, "BLE GAP AUTH ERROR: 0x%x", param->ble_security.auth_cmpl.fail_reason);
        } else {
            ESP_LOGI(TAG, "BLE GAP AUTH SUCCESS");
            s_client_subscribed = false;
        }
        ble_hid_task_start_up();
        break;

    case ESP_GAP_BLE_KEY_EVT:
        ESP_LOGD(TAG, "BLE GAP KEY type = %d", param->ble_security.ble_key.key_type);
        break;

    case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:
        ESP_LOGD(TAG, "BLE GAP PASSKEY_NOTIF passkey:%" PRIu32, param->ble_security.key_notif.passkey);
        break;

    case ESP_GAP_BLE_NC_REQ_EVT:
        ESP_LOGD(TAG, "BLE GAP NC_REQ passkey:%" PRIu32, param->ble_security.key_notif.passkey);
        esp_ble_confirm_reply(param->ble_security.key_notif.bd_addr, true);
        break;

    case ESP_GAP_BLE_PASSKEY_REQ_EVT:
        ESP_LOGD(TAG, "BLE GAP PASSKEY_REQ");
        break;

    case ESP_GAP_BLE_SEC_REQ_EVT:
        ESP_LOGD(TAG, "BLE GAP SEC_REQ");
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;

    default:
        break;
    }
}

static esp_err_t esp_hid_ble_gap_adv_init(uint16_t appearance, const char *device_name)
{
    esp_err_t ret;

    const uint8_t hidd_service_uuid128[] = {
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
    };

    esp_ble_adv_data_t ble_adv_data = {
        .set_scan_rsp = false,
        .include_name = true,
        .include_txpower = true,
        .min_interval = 0x0006,
        .max_interval = 0x0010,
        .appearance = appearance,
        .manufacturer_len = 0,
        .p_manufacturer_data = NULL,
        .service_data_len = 0,
        .p_service_data = NULL,
        .service_uuid_len = sizeof(hidd_service_uuid128),
        .p_service_uuid = (uint8_t *)hidd_service_uuid128,
        .flag = 0x6,
    };

    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t key_size = 16;
    uint32_t passkey = 1234;

    if ((ret = esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t))) != ESP_OK) {
        ESP_LOGE(TAG, "GAP set_security_param AUTHEN_REQ_MODE failed: %d", ret);
        return ret;
    }

    if ((ret = esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t))) != ESP_OK) {
        ESP_LOGE(TAG, "GAP set_security_param IOCAP_MODE failed: %d", ret);
        return ret;
    }

    if ((ret = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t))) != ESP_OK) {
        ESP_LOGE(TAG, "GAP set_security_param SET_INIT_KEY failed: %d", ret);
        return ret;
    }

    if ((ret = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t))) != ESP_OK) {
        ESP_LOGE(TAG, "GAP set_security_param SET_RSP_KEY failed: %d", ret);
        return ret;
    }

    if ((ret = esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t))) != ESP_OK) {
        ESP_LOGE(TAG, "GAP set_security_param MAX_KEY_SIZE failed: %d", ret);
        return ret;
    }

    if ((ret = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, &passkey, sizeof(uint32_t))) != ESP_OK) {
        ESP_LOGE(TAG, "GAP set_security_param SET_STATIC_PASSKEY failed: %d", ret);
        return ret;
    }

    if ((ret = esp_ble_gap_set_device_name(device_name)) != ESP_OK) {
        ESP_LOGE(TAG, "GAP set_device_name failed: %d", ret);
        return ret;
    }

    if ((ret = esp_ble_gap_config_adv_data(&ble_adv_data)) != ESP_OK) {
        ESP_LOGE(TAG, "GAP config_adv_data failed: %d", ret);
        return ret;
    }

    return ret;
}

static esp_err_t esp_hid_ble_gap_adv_start(void)
{
    static esp_ble_adv_params_t hidd_adv_params = {
        .adv_int_min        = 0x20,
        .adv_int_max        = 0x30,
        .adv_type           = ADV_TYPE_IND,
        .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
        .channel_map        = ADV_CHNL_ALL,
        .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    };
    return esp_ble_gap_start_advertising(&hidd_adv_params);
}

static esp_err_t init_low_level(uint8_t mode)
{
    esp_err_t ret;
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret) {
        ESP_LOGE(TAG, "esp_bt_controller_mem_release failed: %d", ret);
        return ret;
    }

    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "esp_bt_controller_init failed: %d", ret);
        return ret;
    }

    ret = esp_bt_controller_enable(mode);
    if (ret) {
        ESP_LOGE(TAG, "esp_bt_controller_enable failed: %d", ret);
        return ret;
    }

    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    if (ret) {
        ESP_LOGE(TAG, "esp_bluedroid_init failed: %d", ret);
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "esp_bluedroid_enable failed: %d", ret);
        return ret;
    }

    if ((ret = esp_ble_gap_register_callback(ble_gap_event_handler)) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ble_gap_register_callback failed: %d", ret);
        return ret;
    }

    return ret;
}

static esp_err_t esp_hid_gap_init(uint8_t mode)
{
    esp_err_t ret;
    if (!mode || mode > ESP_BT_MODE_BTDM) {
        ESP_LOGE(TAG, "Invalid mode given!");
        return ESP_FAIL;
    }

    if (ble_hidh_cb_semaphore != NULL) {
        ESP_LOGE(TAG, "Already initialised");
        return ESP_FAIL;
    }

    ble_hidh_cb_semaphore = xSemaphoreCreateBinary();
    if (ble_hidh_cb_semaphore == NULL) {
        ESP_LOGE(TAG, "xSemaphoreCreateMutex failed!");
        return ESP_FAIL;
    }

    ret = init_low_level(mode);
    if (ret != ESP_OK) {
        vSemaphoreDelete(ble_hidh_cb_semaphore);
        ble_hidh_cb_semaphore = NULL;
        return ret;
    }

    return ESP_OK;
}

esp_err_t bsp_blehid_init(void)
{
    if (s_ble_hid_initialized) {
        ESP_LOGW(TAG, "BLE HID already initialized");
        return ESP_OK;
    }

    esp_err_t ret;

    // 生成带 MAC 后缀的设备名称
    uint8_t mac[6];
    ret = esp_read_mac(mac, ESP_MAC_BT);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read BT MAC, using default name");
        snprintf(s_ble_device_name, sizeof(s_ble_device_name), "%s", BLE_HID_DEVICE_NAME);
    } else {
        // 取 MAC 地址的后 4 位（最后 2 字节）作为后缀
        snprintf(s_ble_device_name, sizeof(s_ble_device_name), "%s-%02X%02X", BLE_HID_DEVICE_NAME, mac[4], mac[5]);
        ESP_LOGI(TAG, "BLE device name: %s", s_ble_device_name);
    }

    // 初始化 NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    esp_log_level_set("BLE_HIDD", ESP_LOG_WARN);

    // 1. 初始化 HID GAP
    ESP_LOGD(TAG, "Setting HID GAP, mode: BLE");
    ret = esp_hid_gap_init(ESP_BT_MODE_BLE);
    ESP_ERROR_CHECK(ret);

    // 2. 初始化 BLE GAP 广播
    ret = esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_KEYBOARD, ble_hid_config.device_name);
    ESP_ERROR_CHECK(ret);

    // 3. 注册 GATTS 回调
    if ((ret = esp_ble_gatts_register_callback(esp_hidd_gatts_event_handler)) != ESP_OK) {
        ESP_LOGE(TAG, "GATTS register callback failed: %d", ret);
        return ret;
    }

    // 4. 初始化 HID 设备
    ESP_LOGD(TAG, "Setting BLE device");
    ESP_ERROR_CHECK(
        esp_hidd_dev_init(&ble_hid_config, ESP_HID_TRANSPORT_BLE, ble_hidd_event_callback, &s_ble_hid_param.hid_dev));

    s_ble_hid_initialized = true;
    ESP_LOGD(TAG, "BLE HID Keyboard initialized successfully");

    return ESP_OK;
}

esp_err_t bsp_blehid_deinit(void)
{
    if (!s_ble_hid_initialized) {
        return ESP_OK;
    }

    ble_hid_task_shut_down();

    if (s_ble_hid_param.hid_dev) {
        esp_hidd_dev_deinit(s_ble_hid_param.hid_dev);
        s_ble_hid_param.hid_dev = NULL;
    }

    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();

    if (ble_hidh_cb_semaphore) {
        vSemaphoreDelete(ble_hidh_cb_semaphore);
        ble_hidh_cb_semaphore = NULL;
    }

    s_ble_hid_initialized = false;
    s_ble_hid_state = BLE_HID_STATE_DISCONNECTED;

    return ESP_OK;
}

esp_err_t bsp_blehid_start_advertising(void)
{
    if (!s_ble_hid_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_hid_ble_gap_adv_start();
}

esp_err_t bsp_blehid_stop_advertising(void)
{
    if (!s_ble_hid_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_ble_gap_stop_advertising();
}

esp_err_t bsp_blehid_send_key(char ch)
{
    if (!s_ble_hid_initialized || !s_ble_hid_param.hid_dev) {
        ESP_LOGW(TAG, "BLE HID not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ble_hid_state != BLE_HID_STATE_CONNECTED) {
        ESP_LOGW(TAG, "BLE HID not connected (state=%d)", s_ble_hid_state);
        return ESP_ERR_INVALID_STATE;
    }

    send_keyboard(ch);
    return ESP_OK;
}

esp_err_t bsp_blehid_send_keycode(uint8_t modifier, uint8_t keycode)
{
    if (!s_ble_hid_initialized || !s_ble_hid_param.hid_dev) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ble_hid_state != BLE_HID_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t buffer[8] = {0};
    buffer[0] = modifier;
    buffer[2] = keycode;

    esp_err_t ret = esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, 1, buffer, 8);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "HID input set failed: %s", esp_err_to_name(ret));
        s_client_subscribed = false;
        return ret;
    }
    memset(buffer, 0, sizeof(buffer));
    ret = esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, 1, buffer, 8);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "HID input set (release) failed: %s", esp_err_to_name(ret));
        s_client_subscribed = false;
        return ret;
    }

    return ESP_OK;
}

esp_err_t bsp_blehid_send_key_release(void)
{
    if (!s_ble_hid_initialized || !s_ble_hid_param.hid_dev) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ble_hid_state != BLE_HID_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_client_subscribed) {
        ESP_LOGD(TAG, "Client not subscribed, but sending key release anyway");
    }

    uint8_t buffer[8] = {0};
    esp_err_t ret = esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, 1, buffer, 8);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "HID input set (release) failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

ble_hid_state_t bsp_blehid_get_state(void)
{
    return s_ble_hid_state;
}

bool bsp_blehid_is_connected(void)
{
    return s_ble_hid_state == BLE_HID_STATE_CONNECTED;
}

esp_err_t bsp_blehid_register_event_cb(ble_hid_event_cb_t cb)
{
    s_event_cb = cb;
    return ESP_OK;
}

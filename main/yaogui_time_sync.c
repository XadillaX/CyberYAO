#include "yaogui_time_sync.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char* TAG = "yaogui_time";
static esp_netif_t* s_sta_netif;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static bool s_radio_ready;
static bool s_sntp_started;
static bool s_has_wifi_credentials;
static unsigned s_retries;
static uint8_t s_ble_addr_type;
static volatile uint32_t s_sync_generation;

#define WIFI_RETRY_LIMIT 8U
#define CURRENT_TIME_SERVICE_UUID 0x1805
#define CURRENT_TIME_CHARACTERISTIC_UUID 0x2A2B

static int ble_gap_event(struct ble_gap_event* event, void* arg);

static int current_time_access(uint16_t connection,
                               uint16_t attribute,
                               struct ble_gatt_access_ctxt* context,
                               void* arg) {
  (void)connection;
  (void)attribute;
  (void)arg;
  if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  uint8_t value[10];
  uint16_t length = 0;
  int result = ble_hs_mbuf_to_flat(context->om, value, sizeof(value), &length);
  if (result != 0 || length != sizeof(value)) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }
  const int year = value[0] | ((int)value[1] << 8);
  const int month = value[2];
  const int day = value[3];
  const int hour = value[4];
  const int minute = value[5];
  const int second = value[6];
  if (year < 2024 || year > 2040 || month < 1 || month > 12 || day < 1 ||
      day > 31 || hour > 23 || minute > 59 || second > 59) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  struct tm local = {
      .tm_year = year - 1900,
      .tm_mon = month - 1,
      .tm_mday = day,
      .tm_hour = hour,
      .tm_min = minute,
      .tm_sec = second,
      .tm_isdst = -1,
  };
  setenv("TZ", "CST-8", 1);
  tzset();
  const time_t epoch = mktime(&local);
  if (epoch < 1700000000) return BLE_ATT_ERR_UNLIKELY;
  const struct timeval current = {.tv_sec = epoch, .tv_usec = 0};
  settimeofday(&current, NULL);
  s_sync_generation++;
  ESP_LOGI(TAG,
           "蓝牙校时完成: %04d-%02d-%02d %02d:%02d:%02d",
           year,
           month,
           day,
           hour,
           minute,
           second);
  return 0;
}

static const struct ble_gatt_svc_def TIME_SERVICES[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(CURRENT_TIME_SERVICE_UUID),
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid =
                        BLE_UUID16_DECLARE(CURRENT_TIME_CHARACTERISTIC_UUID),
                    .access_cb = current_time_access,
                    .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                },
                {0},
            },
    },
    {0},
};

static int advertise_time_service(void) {
  struct ble_hs_adv_fields fields = {0};
  const ble_uuid16_t service = BLE_UUID16_INIT(CURRENT_TIME_SERVICE_UUID);
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.uuids16 = (ble_uuid16_t*)&service;
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;
  int result = ble_gap_adv_set_fields(&fields);
  if (result != 0) return result;

  static const char name[] = "CyberYAO-Time";
  struct ble_hs_adv_fields response = {0};
  response.name = (const uint8_t*)name;
  response.name_len = sizeof(name) - 1;
  response.name_is_complete = 1;
  result = ble_gap_adv_rsp_set_fields(&response);
  if (result != 0) return result;

  struct ble_gap_adv_params parameters = {0};
  parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
  parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
  return ble_gap_adv_start(
      s_ble_addr_type, NULL, BLE_HS_FOREVER, &parameters, ble_gap_event, NULL);
}

static int ble_gap_event(struct ble_gap_event* event, void* arg) {
  (void)arg;
  if (event->type == BLE_GAP_EVENT_DISCONNECT ||
      event->type == BLE_GAP_EVENT_ADV_COMPLETE) {
    (void)advertise_time_service();
  }
  return 0;
}

static void ble_reset(int reason) {
  ESP_LOGE(TAG, "蓝牙时间服务复位: %d", reason);
}

static void ble_sync(void) {
  int result = ble_hs_util_ensure_addr(0);
  if (result == 0) result = ble_hs_id_infer_auto(0, &s_ble_addr_type);
  if (result == 0) result = advertise_time_service();
  if (result != 0) ESP_LOGE(TAG, "蓝牙时间广播失败: %d", result);
}

static void ble_host_task(void* arg) {
  (void)arg;
  nimble_port_run();
  nimble_port_freertos_deinit();
}

static esp_err_t start_ble_time_service(void) {
  esp_err_t error = nimble_port_init();
  if (error != ESP_OK) return error;
  ble_svc_gap_init();
  ble_svc_gatt_init();
  int result = ble_svc_gap_device_name_set("CyberYAO-Time");
  if (result == 0) result = ble_gatts_count_cfg(TIME_SERVICES);
  if (result == 0) result = ble_gatts_add_svcs(TIME_SERVICES);
  if (result != 0) {
    nimble_port_deinit();
    return ESP_FAIL;
  }
  ble_hs_cfg.reset_cb = ble_reset;
  ble_hs_cfg.sync_cb = ble_sync;
  nimble_port_freertos_init(ble_host_task);
  ESP_LOGI(TAG, "蓝牙时间服务已启动: CyberYAO-Time");
  return ESP_OK;
}

static void start_sntp(void) {
  if (s_sntp_started) return;
  setenv("TZ", "CST-8", 1);
  tzset();
  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
  config.start = true;
  esp_err_t error = esp_netif_sntp_init(&config);
  if (error == ESP_OK) {
    s_sntp_started = true;
    ESP_LOGI(TAG, "SNTP 已启动，时区为北京时间");
  } else {
    ESP_LOGE(TAG, "SNTP 启动失败: %s", esp_err_to_name(error));
  }
}

static void wifi_event(void* arg,
                       esp_event_base_t event_base,
                       int32_t event_id,
                       void* event_data) {
  (void)arg;
  (void)event_base;
  (void)event_data;
  if (event_id == WIFI_EVENT_STA_START) {
    wifi_config_t config = {0};
    if (!s_has_wifi_credentials ||
        esp_wifi_get_config(WIFI_IF_STA, &config) != ESP_OK)
      return;
    ESP_LOGI(TAG, "使用已保存的 Wi-Fi 凭据连接: %.24s", config.sta.ssid);
    (void)esp_wifi_connect();
  } else if (event_id == WIFI_EVENT_STA_DISCONNECTED &&
             s_retries < WIFI_RETRY_LIMIT) {
    s_retries++;
    (void)esp_wifi_connect();
  }
}

static void ip_event(void* arg,
                     esp_event_base_t event_base,
                     int32_t event_id,
                     void* event_data) {
  (void)arg;
  (void)event_base;
  (void)event_data;
  if (event_id != IP_EVENT_STA_GOT_IP) return;
  s_retries = 0;
  ESP_LOGI(TAG, "网络已连接，开始校准时间");
  start_sntp();
}

esp_err_t yaogui_time_sync_start(void) {
  esp_err_t error = nvs_flash_init();
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "NVS 初始化失败，未擦除现有数据: %s", esp_err_to_name(error));
    return error;
  }
  error = start_ble_time_service();
  if (error == ESP_OK) s_radio_ready = true;
  return error;
}

bool yaogui_time_sync_radio_ready(void) {
  return s_radio_ready;
}

esp_err_t yaogui_time_sync_request(void) {
  if (!s_radio_ready) return ESP_ERR_INVALID_STATE;
  if (ble_gap_adv_active()) return ESP_OK;
  return advertise_time_service() == 0 ? ESP_OK : ESP_FAIL;
}

uint32_t yaogui_time_sync_generation(void) {
  return s_sync_generation;
}

#include "yaogui_app.h"

#include <inttypes.h>
#include <string.h>
#include <time.h>

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "yaogui_ambient_sound.h"
#include "yaogui_coin_sound.h"
#include "yaogui_logic.h"
#include "yaogui_time_sync.h"
#include "yaogui_view.h"

static const char* TAG = "yaogui";

#define ENTROPY_STABILIZE_MS 100U
#define APP_IDLE_DIM_MS 30000U
#define APP_STANDBY_OFF_MS 30000U
#define BATTERY_REFRESH_MS 30000U
#define TIME_SYNC_TIMEOUT_MS 30000U
#define TIME_SYNC_RESULT_MS 2000U

typedef struct {
  bsp_btn_t button;
  bsp_btn_ev_t event;
} key_event_t;

typedef struct {
  bool succeeded;
  uint8_t values[YAOGUI_SHELL_COUNT];
} random_result_t;

typedef enum {
  AUDIO_EVENT_START_AMBIENT,
  AUDIO_EVENT_PLAY_COINS,
} audio_event_t;

static lv_timer_t* s_tick_timer;
static yaogui_view_t* s_view;
static yaogui_model_t s_model;
static QueueHandle_t s_results;
static QueueHandle_t s_key_events;
static QueueHandle_t s_requests;
static QueueHandle_t s_audio_events;
static TaskHandle_t s_random_task;
static TaskHandle_t s_audio_task;
static uint32_t s_last_activity_ms;
static uint32_t s_battery_read_ms;
static bool s_battery_read;
static int s_battery_percent = -1;
static bool s_backlight_dimmed;
static bool s_screen_off;
static bool s_standby_active;
static uint32_t s_standby_entered_ms;
static bool s_wake_gesture_active;
static bsp_btn_t s_wake_button;
static bool s_audio_ready;
static yaogui_time_sync_indicator_t s_time_sync_indicator;
static uint32_t s_time_sync_started_ms;
static uint32_t s_time_sync_generation;

static bool standby_clock(char* date_text,
                          size_t date_text_size,
                          int* minute_of_day,
                          int* year,
                          int* month,
                          int* day) {
  static const char* const weekdays[] = {
      "星期日",
      "星期一",
      "星期二",
      "星期三",
      "星期四",
      "星期五",
      "星期六",
  };
  const time_t current = time(NULL);
  struct tm local;
  if (current >= 1700000000 && localtime_r(&current, &local)) {
    snprintf(date_text,
             date_text_size,
             "%d年%d月%d日 · %s",
             local.tm_year + 1900,
             local.tm_mon + 1,
             local.tm_mday,
             weekdays[local.tm_wday]);
    *minute_of_day = local.tm_hour * 60 + local.tm_min;
    *year = local.tm_year + 1900;
    *month = local.tm_mon + 1;
    *day = local.tm_mday;
    return true;
  }
  snprintf(date_text, date_text_size, "等待校时");
  *minute_of_day = -1;
  *year = 0;
  *month = 0;
  *day = 0;
  return false;
}

static uint32_t now_ms(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

static void request_time_sync(void) {
  if (yaogui_time_sync_request() != ESP_OK) {
    s_time_sync_indicator = YAOGUI_TIME_SYNC_TIMEOUT;
    s_time_sync_started_ms = now_ms();
    ESP_LOGW(TAG, "蓝牙校时请求失败");
    return;
  }
  s_time_sync_generation = yaogui_time_sync_generation();
  s_time_sync_started_ms = now_ms();
  s_time_sync_indicator = YAOGUI_TIME_SYNC_WAITING;
  ESP_LOGI(TAG, "等待外部设备写入蓝牙时间");
}

static esp_err_t teardown_step(esp_err_t first_error,
                               esp_err_t error,
                               const char* operation) {
  if (error == ESP_OK) return first_error;
  ESP_LOGE(TAG, "随机源 %s 失败: %s", operation, esp_err_to_name(error));
  return first_error == ESP_OK ? error : first_error;
}

/*
 * ESP32-C3 硬件 RNG 在 Wi-Fi 射频启用时提供真随机熵。每次投掷独占一次完整
 * init/start/fill/stop/deinit 生命周期，不创建 netif，也不发起扫描或连接。
 */
static esp_err_t fill_coin_bits(uint8_t values[YAOGUI_SHELL_COUNT]) {
  if (!values) return ESP_ERR_INVALID_ARG;

  if (yaogui_time_sync_radio_ready()) {
    uint32_t random_bits = 0;
    esp_fill_random(&random_bits, sizeof(random_bits));
    for (size_t i = 0; i < YAOGUI_SHELL_COUNT; i++) {
      values[i] = (uint8_t)((random_bits >> i) & 1U);
    }
    return ESP_OK;
  }

  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  config.nvs_enable = 0;
  bool initialized = false;
  bool started = false;
  esp_err_t error = esp_wifi_init(&config);
  if (error != ESP_OK) goto cleanup;
  initialized = true;

  error = esp_wifi_set_storage(WIFI_STORAGE_RAM);
  if (error != ESP_OK) goto cleanup;
  error = esp_wifi_set_mode(WIFI_MODE_STA);
  if (error != ESP_OK) goto cleanup;
  error = esp_wifi_start();
  if (error != ESP_OK) goto cleanup;
  started = true;

  vTaskDelay(pdMS_TO_TICKS(ENTROPY_STABILIZE_MS));
  uint32_t random_bits = 0;
  esp_fill_random(&random_bits, sizeof(random_bits));

  uint8_t pending[YAOGUI_SHELL_COUNT];
  for (size_t i = 0; i < YAOGUI_SHELL_COUNT; i++) {
    pending[i] = (uint8_t)((random_bits >> i) & 1U);
  }

cleanup:
  if (started) {
    error = teardown_step(error, esp_wifi_stop(), "停止");
  }
  if (initialized) {
    error = teardown_step(error, esp_wifi_deinit(), "反初始化");
  }
  if (error == ESP_OK) memcpy(values, pending, sizeof(pending));
  return error;
}

static void random_task(void* arg) {
  (void)arg;
  bool request;
  for (;;) {
    if (xQueueReceive(s_requests, &request, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    random_result_t result = {0};
    result.succeeded = fill_coin_bits(result.values) == ESP_OK;
    if (xQueueSend(s_results, &result, pdMS_TO_TICKS(100)) != pdTRUE) {
      ESP_LOGE(TAG, "随机结果队列已满");
    }
  }
}

static void audio_task(void* arg) {
  (void)arg;
  audio_event_t event;
  bool ambient_playing = false;
  size_t ambient_offset = 0;
  for (;;) {
    TickType_t wait = ambient_playing ? 0 : portMAX_DELAY;
    if (xQueueReceive(s_audio_events, &event, wait) == pdTRUE) {
      if (event == AUDIO_EVENT_START_AMBIENT) {
        ambient_playing = true;
        ambient_offset = 0;
        continue;
      }
      if (event == AUDIO_EVENT_PLAY_COINS && s_audio_ready) {
        const uint32_t audio_started_ms = now_ms();
        ESP_LOGI(TAG, "铜钱音效开始 t=%" PRIu32, audio_started_ms);
        size_t offset = 0;
        while (offset < yaogui_coin_sound_sample_count) {
          size_t count = yaogui_coin_sound_sample_count - offset;
          if (count > 1024U) count = 1024U;
          if (bsp_audio_write(yaogui_coin_sound_pcm + offset,
                              count * sizeof(int16_t)) != ESP_OK) {
            ESP_LOGW(TAG, "铜钱音效播放中断");
            s_audio_ready = false;
            ambient_playing = false;
            break;
          }
          offset += count;
        }
        ESP_LOGI(TAG,
                 "铜钱音效结束 t=%" PRIu32 "，历时=%" PRIu32 " ms",
                 now_ms(),
                 now_ms() - audio_started_ms);
        ambient_offset = 0;
      }
      continue;
    }
    if (!ambient_playing || !s_audio_ready) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    size_t count = yaogui_ambient_sound_sample_count - ambient_offset;
    if (count > 1024U) count = 1024U;
    if (bsp_audio_write(yaogui_ambient_sound_pcm + ambient_offset,
                        count * sizeof(int16_t)) != ESP_OK) {
      ESP_LOGW(TAG, "背景音播放中断");
      s_audio_ready = false;
      ambient_playing = false;
      continue;
    }
    ambient_offset += count;
    if (ambient_offset >= yaogui_ambient_sound_sample_count) {
      ambient_offset = 0;
    }
  }
}

static void process_key(const key_event_t* key) {
  s_last_activity_ms = now_ms();
  if (s_standby_active) {
    if (s_wake_gesture_active && key->button == s_wake_button) {
      if (key->button == BSP_BTN_DOWN && key->event == BSP_BTN_LONG) {
        request_time_sync();
      }
      if (key->event == BSP_BTN_CLICK || key->event == BSP_BTN_LONG ||
          key->event == BSP_BTN_DOUBLE) {
        s_wake_gesture_active = false;
      }
      return;
    }
    if (key->event != BSP_BTN_PRESS) return;
    s_standby_entered_ms = s_last_activity_ms;
    const bool waking_from_screen_off = s_screen_off;
    if (s_backlight_dimmed || s_screen_off) {
      bsp_display_backlight(100);
      s_backlight_dimmed = false;
      s_screen_off = false;
    }
    if (key->button == BSP_BTN_OK && !waking_from_screen_off) {
      s_standby_active = false;
    }
    s_wake_gesture_active = true;
    s_wake_button = key->button;
    return;
  }
  /*
   * 一次物理按键会先产生 PRESS，再在抬起后产生 CLICK/LONG。若 PRESS 用于
   * 唤醒待机页，必须吞掉同一手势余下事件，不能让确定键继续触发起卦。
   */
  if (s_wake_gesture_active && key->button == s_wake_button) {
    if (key->event == BSP_BTN_CLICK || key->event == BSP_BTN_LONG ||
        key->event == BSP_BTN_DOUBLE) {
      s_wake_gesture_active = false;
    }
    return;
  }
  if (key->button == BSP_BTN_DOWN && key->event == BSP_BTN_LONG) {
    request_time_sync();
    return;
  }
  /*
   * 继续按住上键经过软件重启后，Bootloader 会执行既有的五秒长按检测，
   * 随后直接加载永久 Recovery。这里不复制 Recovery 的私有 BLE 刷机协议。
   */
  if (key->button == BSP_BTN_UP && key->event == BSP_BTN_LONG) {
    ESP_LOGI(TAG, "上键持续按住：重启并进入永久 Recovery");
    esp_restart();
    return;
  }
  if (!s_model.reading.open && key->button == BSP_BTN_UP &&
      key->event == BSP_BTN_CLICK) {
    bsp_display_backlight(100);
    s_backlight_dimmed = false;
    s_screen_off = false;
    s_standby_active = true;
    s_standby_entered_ms = s_last_activity_ms;
    ESP_LOGI(TAG, "单击上键：保留摇卦状态并返回待机");
    return;
  }
  if (s_model.reading.open && key->event == BSP_BTN_CLICK) {
    if (key->button == BSP_BTN_UP && yaogui_view_reading_scroll(s_view, -1)) {
      return;
    }
    if (key->button == BSP_BTN_DOWN && yaogui_view_reading_scroll(s_view, 1)) {
      return;
    }
  }

  bool request_random = false;
  if (key->event == BSP_BTN_CLICK && key->button == BSP_BTN_UP) {
    request_random = yaogui_model_key(
        &s_model, YAOGUI_KEY_UP, s_last_activity_ms, s_last_activity_ms);
  } else if (key->event == BSP_BTN_CLICK && key->button == BSP_BTN_DOWN) {
    request_random = yaogui_model_key(
        &s_model, YAOGUI_KEY_DOWN, s_last_activity_ms, s_last_activity_ms);
  } else if (key->button == BSP_BTN_OK && key->event == BSP_BTN_CLICK) {
    request_random = yaogui_model_key(
        &s_model, YAOGUI_KEY_OK, s_last_activity_ms, s_last_activity_ms);
  } else if (key->button == BSP_BTN_OK && key->event == BSP_BTN_LONG) {
    request_random = yaogui_model_key(
        &s_model, YAOGUI_KEY_OK_LONG, s_last_activity_ms, s_last_activity_ms);
  }
  if (request_random) {
    const bool request = true;
    ESP_LOGI(TAG, "起卦开始 t=%" PRIu32, s_last_activity_ms);
    if (xQueueSend(s_requests, &request, 0) != pdTRUE) {
      yaogui_model_fail(
          &s_model, YAOGUI_ERROR_RANDOM_SOURCE, s_last_activity_ms);
    } else {
      const audio_event_t audio_event = AUDIO_EVENT_PLAY_COINS;
      (void)xQueueSend(s_audio_events, &audio_event, 0);
    }
  }
}

static void process_result(const random_result_t* result) {
  ESP_LOGI(TAG,
           "随机返回 t=%" PRIu32 "，历时=%" PRIu32 " ms",
           now_ms(),
           now_ms() - s_model.started_ms);
  if (result->succeeded) {
    yaogui_model_complete(
        &s_model, result->values, YAOGUI_SOURCE_ESP32_RF, now_ms());
  } else {
    yaogui_model_fail(&s_model, YAOGUI_ERROR_RANDOM_SOURCE, now_ms());
  }
}

static void tick(lv_timer_t* timer) {
  (void)timer;
  key_event_t key;
  while (xQueueReceive(s_key_events, &key, 0) == pdTRUE) process_key(&key);
  random_result_t result;
  while (xQueueReceive(s_results, &result, 0) == pdTRUE) {
    process_result(&result);
  }
  const yaogui_phase_t phase_before_tick = s_model.phase;
  yaogui_model_tick(&s_model, now_ms());
  if (phase_before_tick == YAOGUI_ROLLING && s_model.phase == YAOGUI_RESULT) {
    ESP_LOGI(TAG,
             "动画落定 t=%" PRIu32 "，历时=%" PRIu32 " ms",
             now_ms(),
             now_ms() - s_model.started_ms);
  }
  if (s_time_sync_indicator == YAOGUI_TIME_SYNC_WAITING) {
    if (yaogui_time_sync_generation() != s_time_sync_generation) {
      s_time_sync_indicator = YAOGUI_TIME_SYNC_SUCCESS;
      s_time_sync_started_ms = now_ms();
      s_standby_entered_ms = s_time_sync_started_ms;
    } else if ((uint32_t)(now_ms() - s_time_sync_started_ms) >=
               TIME_SYNC_TIMEOUT_MS) {
      s_time_sync_indicator = YAOGUI_TIME_SYNC_TIMEOUT;
      s_time_sync_started_ms = now_ms();
      s_standby_entered_ms = s_time_sync_started_ms;
    }
  } else if (s_time_sync_indicator != YAOGUI_TIME_SYNC_IDLE &&
             (uint32_t)(now_ms() - s_time_sync_started_ms) >=
                 TIME_SYNC_RESULT_MS) {
    s_time_sync_indicator = YAOGUI_TIME_SYNC_IDLE;
  }

  if (!s_battery_read ||
      (uint32_t)(now_ms() - s_battery_read_ms) >= BATTERY_REFRESH_MS) {
    s_battery_percent = bsp_battery_soc();
    s_battery_read = true;
    s_battery_read_ms = now_ms();
  }
  if (!s_standby_active &&
      (uint32_t)(now_ms() - s_last_activity_ms) >= APP_IDLE_DIM_MS &&
      s_model.phase != YAOGUI_ROLLING) {
    bsp_display_backlight(20);
    s_backlight_dimmed = true;
    s_screen_off = false;
    s_standby_active = true;
    s_standby_entered_ms = now_ms();
  }
  if (s_standby_active && !s_screen_off &&
      s_time_sync_indicator != YAOGUI_TIME_SYNC_WAITING &&
      (uint32_t)(now_ms() - s_standby_entered_ms) >= APP_STANDBY_OFF_MS) {
    bsp_display_backlight(0);
    s_backlight_dimmed = true;
    s_screen_off = true;
  }
  char date_text[48];
  int minute_of_day;
  int year;
  int month;
  int day;
  const bool time_valid = standby_clock(
      date_text, sizeof(date_text), &minute_of_day, &year, &month, &day);
  const yaogui_view_state_t state = {
      .model = &s_model,
      .now_ms = now_ms(),
      .battery_percent = s_battery_percent,
      .standby = s_standby_active,
      .minute_of_day = minute_of_day,
      .date_text = date_text,
      .year = year,
      .month = month,
      .day = day,
      .time_valid = time_valid,
      .time_sync_indicator = s_time_sync_indicator,
  };
  yaogui_view_render(s_view, &state);
}

static void release_start_resources(void) {
  if (s_random_task) {
    vTaskDelete(s_random_task);
    s_random_task = NULL;
  }
  if (s_audio_task) {
    vTaskDelete(s_audio_task);
    s_audio_task = NULL;
  }
  if (s_audio_events) vQueueDelete(s_audio_events);
  if (s_requests) vQueueDelete(s_requests);
  if (s_key_events) vQueueDelete(s_key_events);
  if (s_results) vQueueDelete(s_results);
  s_requests = NULL;
  s_audio_events = NULL;
  s_key_events = NULL;
  s_results = NULL;
  s_audio_ready = false;
}

esp_err_t yaogui_app_start(void) {
  if (s_random_task || s_audio_task || s_results || s_key_events) {
    return ESP_ERR_INVALID_STATE;
  }
  yaogui_model_init(&s_model);
  s_results = xQueueCreate(2, sizeof(random_result_t));
  s_key_events = xQueueCreate(4, sizeof(key_event_t));
  s_requests = xQueueCreate(1, sizeof(bool));
  s_audio_events = xQueueCreate(2, sizeof(audio_event_t));
  if (!s_results || !s_key_events || !s_requests || !s_audio_events) {
    release_start_resources();
    return ESP_ERR_NO_MEM;
  }
  if (xTaskCreate(random_task, "yaogui_rng", 4096, NULL, 4, &s_random_task) !=
      pdPASS) {
    s_random_task = NULL;
    release_start_resources();
    return ESP_ERR_NO_MEM;
  }
  if (xTaskCreate(audio_task, "yaogui_audio", 6144, NULL, 5, &s_audio_task) !=
      pdPASS) {
    s_audio_task = NULL;
    release_start_resources();
    return ESP_ERR_NO_MEM;
  }
  if (!bsp_lvgl_lock(1000)) {
    release_start_resources();
    return ESP_ERR_TIMEOUT;
  }
  lv_obj_t* loading_screen = yaogui_loading_screen_create();
  if (!loading_screen) {
    bsp_lvgl_unlock();
    release_start_resources();
    return ESP_ERR_NO_MEM;
  }
  lv_screen_load(loading_screen);
  bsp_lvgl_unlock();

  s_audio_ready = bsp_audio_init() == ESP_OK &&
                  bsp_audio_set_format(YAOGUI_COIN_SOUND_RATE, 16, 1) == ESP_OK;
  if (s_audio_ready) {
    bsp_audio_set_volume(42);
    const audio_event_t audio_event = AUDIO_EVENT_START_AMBIENT;
    (void)xQueueSend(s_audio_events, &audio_event, 0);
  } else {
    ESP_LOGW(TAG, "铜钱音效初始化失败，起卦将静音");
  }

  if (!bsp_lvgl_lock(1000)) {
    release_start_resources();
    return ESP_ERR_TIMEOUT;
  }
  lv_obj_clean(loading_screen);
  s_view = yaogui_view_create();
  if (!s_view) {
    lv_obj_t* error_label = lv_label_create(loading_screen);
    lv_label_set_text(error_label, "ERR: NO MEMORY");
    lv_obj_center(error_label);
    bsp_lvgl_unlock();
    release_start_resources();
    return ESP_ERR_NO_MEM;
  }
  s_tick_timer = lv_timer_create(tick, 50, NULL);
  if (!s_tick_timer) {
    yaogui_view_destroy(s_view);
    s_view = NULL;
    bsp_lvgl_unlock();
    release_start_resources();
    return ESP_ERR_NO_MEM;
  }
  lv_screen_load(yaogui_view_screen(s_view));
  lv_obj_delete(loading_screen);
  s_last_activity_ms = now_ms();
  s_standby_entered_ms = s_last_activity_ms;
  s_battery_read = false;
  s_backlight_dimmed = false;
  s_screen_off = false;
  s_standby_active = true;
  s_wake_gesture_active = false;
  s_time_sync_indicator = YAOGUI_TIME_SYNC_IDLE;
  char date_text[48];
  int minute_of_day;
  int year;
  int month;
  int day;
  const bool time_valid = standby_clock(
      date_text, sizeof(date_text), &minute_of_day, &year, &month, &day);
  const yaogui_view_state_t state = {
      .model = &s_model,
      .now_ms = now_ms(),
      .battery_percent = s_battery_percent,
      .standby = true,
      .minute_of_day = minute_of_day,
      .date_text = date_text,
      .year = year,
      .month = month,
      .day = day,
      .time_valid = time_valid,
      .time_sync_indicator = s_time_sync_indicator,
  };
  yaogui_view_render(s_view, &state);
  bsp_lvgl_unlock();
  return ESP_OK;
}

void yaogui_app_key(bsp_btn_t btn, bsp_btn_ev_t ev, void* user) {
  (void)user;
  if (!s_key_events) return;
  const key_event_t key = {.button = btn, .event = ev};
  (void)xQueueSend(s_key_events, &key, 0);
}
